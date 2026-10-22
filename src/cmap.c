#include "cmap.h"
#include "lexer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned code; pdf_bytes utf8; } mapping;
struct pdf_cmap {
    mapping *items;
    size_t len, cap, bytes;
    unsigned char codespace[8192]; /* At most 65536 Identity-H codes. */
};
typedef struct {
    pdf_lexer lexer;
    pdf_token token;
    pdf_error local, *error;
    const pdf_limits *limits;
    size_t offset;
    unsigned code_bytes;
    pdf_cmap *map;
} parser;

static int fail(parser *p, pdf_error_code code, const char *why) {
    pdf_error_set(p->error, code, p->offset, "cmap", "decoded byte %zu: %s",
                  p->token.offset, why);
    return 0;
}
static int next(parser *p) {
    pdf_token_destroy(&p->token);
    p->token = lexer_next(&p->lexer);
    if (p->local.code != PDF_ERROR_NONE)
        return fail(p, p->local.code, p->local.message);
    return p->token.type != PDF_TOKEN_INVALID || fail(p, PDF_ERROR_MALFORMED, "invalid CMap token");
}
static int keyword(parser *p, const char *s) {
    return p->token.type == PDF_TOKEN_KEYWORD && p->token.text &&
           strcmp(p->token.text, s) == 0;
}
static int source(parser *p, unsigned *code) {
    if (p->token.type != PDF_TOKEN_HEX_STRING || p->token.bytes.len != p->code_bytes)
        return fail(p, PDF_ERROR_MALFORMED, "CMap source code has wrong byte length");
    *code = p->token.bytes.data[0];
    if (p->code_bytes == 2) *code = (*code << 8) | p->token.bytes.data[1];
    return 1;
}
static size_t put_utf8(unsigned char *out, uint32_t cp) {
    if (cp < 0x80) { out[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | cp >> 6);
        out[1] = (unsigned char)(0x80 | (cp & 63)); return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | cp >> 12);
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 63));
        out[2] = (unsigned char)(0x80 | (cp & 63)); return 3;
    }
    out[0] = (unsigned char)(0xF0 | cp >> 18);
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 63));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 63));
    out[3] = (unsigned char)(0x80 | (cp & 63)); return 4;
}
static int destination(parser *p, unsigned code, const pdf_bytes *utf16) {
    if (!utf16->len || utf16->len % 2 || utf16->len > 512)
        return fail(p, PDF_ERROR_MALFORMED, "CMap destination must be nonempty UTF-16BE");
    pdf_cmap *m = p->map;
    if (m->len >= p->limits->max_container_entries || m->len >= 65536)
        return fail(p, PDF_ERROR_RESOURCE_LIMIT, "CMap mapping count exceeds limit");
    /* Two passes avoid allocating from an unchecked expansion estimate. */
    size_t size = 0;
    unsigned char *out = NULL;
    for (int pass = 0; pass < 2; pass++) {
        size_t at = 0;
        for (size_t i = 0; i < utf16->len; i += 2) {
            uint32_t cp = (unsigned)utf16->data[i] << 8 | utf16->data[i + 1];
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 3 >= utf16->len) { free(out); return fail(p, PDF_ERROR_MALFORMED, "unpaired UTF-16 surrogate"); }
                uint32_t low = (unsigned)utf16->data[i + 2] << 8 | utf16->data[i + 3];
                if (low < 0xDC00 || low > 0xDFFF) { free(out); return fail(p, PDF_ERROR_MALFORMED, "unpaired UTF-16 surrogate"); }
                cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
                i += 2;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                free(out); return fail(p, PDF_ERROR_MALFORMED, "unpaired UTF-16 surrogate");
            }
            unsigned char encoded[4];
            size_t n = put_utf8(encoded, cp);
            if (!pass) {
                if (size > p->limits->max_token_size || n > p->limits->max_token_size - size ||
                    size > SIZE_MAX - n - 1)
                    return fail(p, PDF_ERROR_RESOURCE_LIMIT, "CMap destination exceeds token limit");
                size += n;
            } else { memcpy(out + at, encoded, n); at += n; }
        }
        if (!pass) {
            if (m->bytes > p->limits->max_total_decoded_size ||
                size > p->limits->max_total_decoded_size - m->bytes)
                return fail(p, PDF_ERROR_RESOURCE_LIMIT, "CMap UTF-8 storage exceeds limit");
            out = malloc(size + 1);
            if (!out) return fail(p, PDF_ERROR_OUT_OF_MEMORY, "cannot allocate CMap destination");
        }
    }
    if (m->len == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 16;
        if (cap > p->limits->max_container_entries) cap = p->limits->max_container_entries;
        if (cap > 65536) cap = 65536;
        mapping *items = realloc(m->items, cap * sizeof(*items));
        if (!items) { free(out); return fail(p, PDF_ERROR_OUT_OF_MEMORY, "cannot grow CMap"); }
        m->items = items; m->cap = cap;
    }
    out[size] = 0;
    m->items[m->len++] = (mapping){code, {out, size}};
    m->bytes += size;
    return 1;
}
static int mapped_token(parser *p, unsigned code) {
    if (p->token.type != PDF_TOKEN_HEX_STRING)
        return fail(p, PDF_ERROR_MALFORMED, "CMap destination must be a hex string");
    return destination(p, code, &p->token.bytes);
}
static int block(parser *p, size_t count, int kind) {
    /* kind: 0 codespace, 1 bfchar, 2 bfrange. */
    if (count > p->limits->max_container_entries)
        return fail(p, PDF_ERROR_RESOURCE_LIMIT, "CMap block count exceeds limit");
    for (size_t i = 0; i < count; i++) {
        unsigned lo, hi;
        if (!next(p) || !source(p, &lo) || !next(p)) return 0;
        if (kind == 1) { if (!mapped_token(p, lo)) return 0; continue; }
        if (!source(p, &hi)) return 0;
        if (hi < lo) return fail(p, PDF_ERROR_MALFORMED, "reversed CMap range");
        if (kind == 0) {
            for (unsigned c = lo; c <= hi; c++) {
                if (p->map->codespace[c / 8] & (1u << (c % 8)))
                    return fail(p, PDF_ERROR_MALFORMED, "overlapping CMap codespaces");
                p->map->codespace[c / 8] |= (unsigned char)(1u << (c % 8));
            }
            continue;
        }
        if (!next(p)) return 0;
        size_t expanded = (size_t)(hi - lo) + 1;
        if (p->map->len > p->limits->max_container_entries ||
            expanded > p->limits->max_container_entries - p->map->len)
            return fail(p, PDF_ERROR_RESOURCE_LIMIT, "CMap range expansion exceeds limit");
        if (p->token.type == PDF_TOKEN_ARRAY_BEGIN) {
            for (unsigned c = lo; c <= hi; c++)
                if (!next(p) || !mapped_token(p, c)) return 0;
            if (!next(p) || p->token.type != PDF_TOKEN_ARRAY_END)
                return fail(p, PDF_ERROR_MALFORMED, "CMap bfrange array length mismatch");
        } else {
            if (p->token.type != PDF_TOKEN_HEX_STRING)
                return fail(p, PDF_ERROR_MALFORMED, "CMap range destination must be a hex string or array");
            /* ISO 32000-1 9.10.3: only the final byte increments; carry into
             * a preceding byte is forbidden for a bfrange string target. */
            if (!p->token.bytes.len || hi - lo > 255u - p->token.bytes.data[p->token.bytes.len - 1])
                return fail(p, PDF_ERROR_MALFORMED, "CMap destination range overflow");
            for (unsigned c = lo; c <= hi; c++) {
                if (!destination(p, c, &p->token.bytes)) return 0;
                if (c != hi) p->token.bytes.data[p->token.bytes.len - 1]++;
            }
        }
    }
    const char *end = kind == 0 ? "endcodespacerange" : kind == 1 ? "endbfchar" : "endbfrange";
    return (next(p) && keyword(p, end)) || fail(p, PDF_ERROR_MALFORMED, "CMap block count mismatch");
}
static int compare(const void *a, const void *b) {
    unsigned x = ((const mapping *)a)->code, y = ((const mapping *)b)->code;
    return (x > y) - (x < y);
}
void pdf_cmap_free(pdf_cmap *m) {
    if (!m) return;
    for (size_t i = 0; i < m->len; i++) free(m->items[i].utf8.data);
    free(m->items); free(m);
}
pdf_cmap *pdf_cmap_parse(const unsigned char *data, size_t len, unsigned code_bytes,
                         const pdf_limits *limits, size_t offset, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return NULL;
    pdf_limits defaults;
    if (!limits) { pdf_limits_default(&defaults); limits = &defaults; }
    parser p = {0};
    p.error = e; p.offset = offset; p.limits = limits; p.code_bytes = code_bytes;
    if ((!data && len) || (code_bytes != 1 && code_bytes != 2)) {
        fail(&p, PDF_ERROR_IO, "invalid CMap input"); return NULL;
    }
    if (len > limits->max_decoded_stream_size || len > limits->max_total_decoded_size) {
        fail(&p, PDF_ERROR_RESOURCE_LIMIT, "CMap input exceeds decoded stream limit"); return NULL;
    }
    p.map = calloc(1, sizeof(*p.map));
    if (!p.map) { fail(&p, PDF_ERROR_OUT_OF_MEMORY, "cannot allocate CMap"); return NULL; }
    pdf_error_init(&p.local);
    lexer_init_bytes(&p.lexer, data, len, limits, &p.local);
    int begun = 0, ended = 0, space = 0, have_count = 0;
    size_t dict_depth = 0;
    int64_t count = 0;
    if (!next(&p)) goto error;
    while (p.token.type != PDF_TOKEN_EOF) {
        if (p.token.type == PDF_TOKEN_DICT_BEGIN) {
            if (dict_depth >= limits->max_nesting_depth) {
                fail(&p, PDF_ERROR_RESOURCE_LIMIT, "CMap dictionary nesting exceeds limit"); goto error;
            }
            dict_depth++;
        } else if (p.token.type == PDF_TOKEN_DICT_END) {
            if (!dict_depth) { fail(&p, PDF_ERROR_MALFORMED, "unmatched CMap dictionary end"); goto error; }
            dict_depth--;
        } else if (keyword(&p, "begincmap")) {
            if (begun || ended || dict_depth) { fail(&p, PDF_ERROR_MALFORMED, "unexpected begincmap"); goto error; }
            begun = 1;
        } else if (keyword(&p, "endcmap")) {
            if (!begun || ended || dict_depth) { fail(&p, PDF_ERROR_MALFORMED, "unexpected endcmap"); goto error; }
            ended = 1;
        } else if (keyword(&p, "begincodespacerange") || keyword(&p, "beginbfchar") || keyword(&p, "beginbfrange")) {
            if (!begun || ended || dict_depth || !have_count || count < 0 || (uint64_t)count > SIZE_MAX) {
                fail(&p, PDF_ERROR_MALFORMED, "CMap block requires a nonnegative count inside begincmap"); goto error;
            }
            int kind = keyword(&p, "begincodespacerange") ? 0 : keyword(&p, "beginbfchar") ? 1 : 2;
            if (!block(&p, (size_t)count, kind)) goto error;
            if (kind == 0 && count) space = 1;
        } else if (keyword(&p, "usecmap") || keyword(&p, "begincidchar") || keyword(&p, "begincidrange") ||
                   keyword(&p, "beginnotdefchar") || keyword(&p, "beginnotdefrange")) {
            fail(&p, PDF_ERROR_UNSUPPORTED, "unsupported CMap operator"); goto error;
        } else if (keyword(&p, "endbfchar") || keyword(&p, "endbfrange") || keyword(&p, "endcodespacerange")) {
            fail(&p, PDF_ERROR_MALFORMED, "unexpected CMap block terminator"); goto error;
        } else if (p.token.type == PDF_TOKEN_KEYWORD) {
            const char *allowed[] = {"findresource", "begin", "end", "dict", "def",
                "CMapName", "currentdict", "defineresource", "pop", "dup", "exch", "readonly"};
            int known = 0;
            for (size_t i = 0; i < sizeof(allowed) / sizeof(*allowed); i++)
                if (keyword(&p, allowed[i])) known = 1;
            if (!known) { fail(&p, PDF_ERROR_UNSUPPORTED, "unsupported CMap operator"); goto error; }
        } else if (p.token.type == PDF_TOKEN_ARRAY_BEGIN || p.token.type == PDF_TOKEN_ARRAY_END) {
            fail(&p, PDF_ERROR_UNSUPPORTED, "unsupported CMap array outside bfrange"); goto error;
        } else if (p.token.type == PDF_TOKEN_HEX_STRING && !dict_depth) {
            fail(&p, PDF_ERROR_MALFORMED, "CMap mapping outside a counted block"); goto error;
        }
        have_count = p.token.type == PDF_TOKEN_INT;
        if (have_count) count = p.token.integer;
        if (!next(&p)) goto error;
    }
    if (!begun || !ended || !space || dict_depth) { fail(&p, PDF_ERROR_MALFORMED, "incomplete ToUnicode CMap"); goto error; }
    if (p.map->len > 1) qsort(p.map->items, p.map->len, sizeof(*p.map->items), compare);
    for (size_t i = 0; i < p.map->len; i++) {
        unsigned code = p.map->items[i].code;
        if (!(p.map->codespace[code / 8] & (1u << (code % 8))) ||
            (i && p.map->items[i - 1].code == code)) {
            fail(&p, PDF_ERROR_MALFORMED, "duplicate or out-of-codespace CMap mapping"); goto error;
        }
    }
    pdf_token_destroy(&p.token);
    return p.map;
error:
    pdf_token_destroy(&p.token);
    pdf_cmap_free(p.map);
    return NULL;
}
const pdf_bytes *pdf_cmap_lookup(const pdf_cmap *m, unsigned code) {
    if (!m) return NULL;
    size_t lo = 0, hi = m->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (m->items[mid].code < code) lo = mid + 1;
        else hi = mid;
    }
    return lo < m->len && m->items[lo].code == code ? &m->items[lo].utf8 : NULL;
}
size_t pdf_cmap_storage_size(const pdf_cmap *m) {
    return m ? sizeof(*m) + m->cap * sizeof(*m->items) + m->bytes + m->len : 0;
}
