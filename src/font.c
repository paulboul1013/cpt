#define _POSIX_C_SOURCE 200809L
#include "font.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* PDF WinAnsiEncoding (PDF Reference 1.5 Appendix D.1 glyph names, mapped to
 * Unicode through the Adobe Glyph List). 0x20-0x7E and 0xA0-0xFF equal their
 * Unicode scalar (0xA0 nbspace, 0xAD soft hyphen kept as-is). Per Appendix D
 * note 6, unused codes 0x7F, 0x81, 0x8D, 0x8F, 0x90 and 0x9D render as bullet
 * (U+2022). 0x00-0x1F have no glyph and decode as U+FFFD. */
static const uint16_t win_ansi_high[32] = {
    0x20AC, 0x2022, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x2022, 0x017D, 0x2022,
    0x2022, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x2022, 0x017E, 0x0178
};
#define REPLACEMENT 0xFFFDu

struct pdf_font {
    pdf_font_info info;
    unsigned char *name, *base;
    double widths[256];
    size_t max_utf8;
    int has_reference;
    pdf_reference reference; /* Cache key when the /Font entry is indirect. */
};
typedef struct {
    unsigned char *name;
    size_t len;
    pdf_font *font;
} binding;
struct pdf_font_context {
    pdf_document *document;
    const pdf_object *resources, *fonts_dict;
    size_t page_offset, fonts_offset;
    pdf_limits limits;
    binding *bindings;
    size_t binding_len, binding_cap;
    pdf_font **fonts;
    size_t font_len, font_cap;
    int failed;
};

static int fail_at(pdf_error *e, pdf_error_code code, size_t offset, const char *why) {
    pdf_error_set(e, code, offset, "font", "%s", why);
    return 0;
}
static int name_is(const pdf_object *o, const char *name) {
    size_t len = strlen(name);
    return o && o->type == PDF_OBJECT_NAME && o->value.name.len == len &&
           memcmp(o->value.name.data, name, len) == 0;
}
static int same_reference(pdf_reference a, pdf_reference b) {
    return a.object_number == b.object_number && a.generation == b.generation;
}

/* Follow a reference chain to its leaf value. *loc moves to each referenced
 * object's file offset. Brent's cycle detection keeps this allocation-free;
 * the chain is bounded by max_nesting_depth. */
static const pdf_object *leaf(pdf_font_context *c, const pdf_object *o, size_t *loc,
                              int *is_stream, pdf_error *e) {
    pdf_reference saved = {0, 0};
    size_t power = 1, lam = 0, hops = 0;
    int have_saved = 0;
    if (is_stream) *is_stream = 0;
    while (o && o->type == PDF_OBJECT_REF) {
        pdf_reference r = o->value.reference;
        if (have_saved && same_reference(saved, r)) {
            fail_at(e, PDF_ERROR_MALFORMED, *loc, "font reference cycle");
            return NULL;
        }
        if (hops++ >= c->limits.max_nesting_depth) {
            fail_at(e, PDF_ERROR_RESOURCE_LIMIT, *loc, "font reference chain too deep");
            return NULL;
        }
        if (!have_saved || lam == power) {
            if (have_saved) power *= 2;
            saved = r; have_saved = 1; lam = 0;
        }
        lam++;
        size_t offset = pdf_document_reference_offset(c->document, r);
        if (offset) *loc = offset;
        pdf_error local;
        pdf_error_init(&local);
        const pdf_indirect_object *resolved = pdf_resolve(c->document, r, &local);
        if (!resolved) {
            /* Keep the resolver's cause; fall back to the containing object
             * when it has no file position (e.g. an absent xref entry). */
            pdf_error_set(e, local.code ? local.code : PDF_ERROR_MALFORMED,
                          local.offset ? local.offset : *loc,
                          local.module ? local.module : "font", "%s",
                          local.code ? local.message : "cannot resolve font reference");
            return NULL;
        }
        if (is_stream) *is_stream = resolved->is_stream;
        o = resolved->body;
    }
    return o;
}
/* Resolve a dictionary-valued field; streams are not dictionaries here. */
static const pdf_object *leaf_dict(pdf_font_context *c, const pdf_object *o, size_t *loc,
                                   const char *why, pdf_error *e) {
    int stream = 0;
    o = leaf(c, o, loc, &stream, e);
    if (!o) return NULL;
    if (o->type != PDF_OBJECT_DICT || stream) {
        fail_at(e, PDF_ERROR_MALFORMED, *loc, why);
        return NULL;
    }
    return o;
}
static int leaf_number(pdf_font_context *c, const pdf_object *o, size_t loc,
                       double *value, const char *why, pdf_error *e) {
    o = leaf(c, o, &loc, NULL, e);
    if (!o) return 0;
    if (o->type == PDF_OBJECT_INT) *value = (double)o->value.integer;
    else if (o->type == PDF_OBJECT_REAL) *value = o->value.real;
    else return fail_at(e, PDF_ERROR_MALFORMED, loc, why);
    if (!isfinite(*value)) return fail_at(e, PDF_ERROR_MALFORMED, loc, why);
    return 1;
}
static int leaf_integer(pdf_font_context *c, const pdf_object *o, size_t loc,
                        int64_t *value, const char *why, pdf_error *e) {
    o = leaf(c, o, &loc, NULL, e);
    if (!o) return 0;
    if (o->type != PDF_OBJECT_INT) return fail_at(e, PDF_ERROR_MALFORMED, loc, why);
    *value = o->value.integer;
    return 1;
}
static const pdf_object *leaf_name(pdf_font_context *c, const pdf_object *o, size_t loc,
                                   const char *why, pdf_error *e) {
    o = leaf(c, o, &loc, NULL, e);
    if (!o) return NULL;
    if (o->type != PDF_OBJECT_NAME) { fail_at(e, PDF_ERROR_MALFORMED, loc, why); return NULL; }
    return o;
}

pdf_font_context *pdf_font_context_create(pdf_document *document, const pdf_object *resources,
                                          size_t page_offset, const pdf_limits *limits,
                                          pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return NULL;
    if (!document) { fail_at(e, PDF_ERROR_IO, page_offset, "missing document"); return NULL; }
    pdf_font_context *c = calloc(1, sizeof(*c));
    if (!c) { fail_at(e, PDF_ERROR_OUT_OF_MEMORY, page_offset, "cannot allocate font context"); return NULL; }
    c->document = document;
    c->resources = resources;
    c->page_offset = page_offset;
    if (limits) c->limits = *limits; else pdf_limits_default(&c->limits);
    return c;
}
static void font_free(pdf_font *f) {
    if (!f) return;
    free(f->name); free(f->base); free(f);
}
void pdf_font_context_destroy(pdf_font_context *c) {
    if (!c) return;
    for (size_t i = 0; i < c->binding_len; i++) free(c->bindings[i].name);
    for (size_t i = 0; i < c->font_len; i++) font_free(c->fonts[i]);
    free(c->bindings); free(c->fonts); free(c);
}

static unsigned char *copy_bytes(const unsigned char *data, size_t len) {
    unsigned char *copy = malloc(len + 1);
    if (!copy) return NULL;
    if (len) memcpy(copy, data, len);
    copy[len] = 0;
    return copy;
}
static int is_symbolic_base(const pdf_bytes *b) {
    const unsigned char *d = b->data;
    size_t n = b->len;
    if (n > 7 && d[6] == '+') {
        size_t i = 0;
        while (i < 6 && d[i] >= 'A' && d[i] <= 'Z') i++;
        if (i == 6) { d += 7; n -= 7; }
    }
    return (n == 6 && memcmp(d, "Symbol", 6) == 0) ||
           (n == 12 && memcmp(d, "ZapfDingbats", 12) == 0);
}

static int parse_encoding(pdf_font_context *c, pdf_font *f, const pdf_object *font,
                          size_t loc, pdf_error *e) {
    const pdf_object *enc = pdf_dict_get(font, "Encoding");
    f->info.encoding = PDF_FONT_ENCODING_ASCII_FALLBACK;
    if (!enc) return 1;
    int stream = 0;
    enc = leaf(c, enc, &loc, &stream, e);
    if (!enc) return 0;
    if (enc->type == PDF_OBJECT_DICT && !stream) {
        const pdf_object *type = pdf_dict_get(enc, "Type");
        if (type) {
            type = leaf_name(c, type, loc, "/Encoding /Type must be /Encoding", e);
            if (!type) return 0;
            if (!name_is(type, "Encoding"))
                return fail_at(e, PDF_ERROR_MALFORMED, loc, "/Encoding /Type must be /Encoding");
        }
        if (pdf_dict_get(enc, "Differences"))
            return fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported /Differences encoding");
        const pdf_object *base = pdf_dict_get(enc, "BaseEncoding");
        if (!base)
            return fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported builtin base encoding");
        enc = leaf_name(c, base, loc, "/BaseEncoding must be a name", e);
        if (!enc) return 0;
    } else if (enc->type != PDF_OBJECT_NAME) {
        return fail_at(e, PDF_ERROR_MALFORMED, loc, "/Encoding must be a name or dictionary");
    }
    if (!name_is(enc, "WinAnsiEncoding"))
        return fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported font encoding");
    f->info.encoding = PDF_FONT_ENCODING_WIN_ANSI;
    return 1;
}

static int parse_descriptor(pdf_font_context *c, pdf_font *f, const pdf_object *font,
                            size_t loc, pdf_error *e) {
    const pdf_object *d = pdf_dict_get(font, "FontDescriptor");
    f->info.missing = PDF_FONT_MISSING_NONE;
    if (!d) return 1;
    d = leaf_dict(c, d, &loc, "/FontDescriptor must be a dictionary", e);
    if (!d) return 0;
    const pdf_object *flags = pdf_dict_get(d, "Flags");
    if (flags) {
        int64_t value;
        if (!leaf_integer(c, flags, loc, &value, "/Flags must be an integer", e)) return 0;
        if ((uint64_t)value & 4u)
            return fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported symbolic font");
    }
    const pdf_object *missing = pdf_dict_get(d, "MissingWidth");
    f->info.missing = PDF_FONT_MISSING_DEFAULT_ZERO;
    f->info.missing_width = 0;
    if (missing) {
        if (!leaf_number(c, missing, loc, &f->info.missing_width,
                         "/MissingWidth must be a finite number", e)) return 0;
        f->info.missing = PDF_FONT_MISSING_EXPLICIT;
    }
    return 1;
}

static int parse_widths(pdf_font_context *c, pdf_font *f, const pdf_object *font,
                        size_t loc, pdf_error *e) {
    const pdf_object *first = pdf_dict_get(font, "FirstChar");
    const pdf_object *last = pdf_dict_get(font, "LastChar");
    const pdf_object *widths = pdf_dict_get(font, "Widths");
    int present = (first != NULL) + (last != NULL) + (widths != NULL);
    if (present == 0)
        return fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported font metrics: no /Widths");
    if (present != 3)
        return fail_at(e, PDF_ERROR_MALFORMED, loc,
                       "/FirstChar, /LastChar and /Widths must appear together");
    int64_t lo, hi;
    if (!leaf_integer(c, first, loc, &lo, "/FirstChar must be an integer", e) ||
        !leaf_integer(c, last, loc, &hi, "/LastChar must be an integer", e)) return 0;
    if (lo < 0 || hi > 255 || lo > hi)
        return fail_at(e, PDF_ERROR_MALFORMED, loc, "/FirstChar and /LastChar must satisfy 0 <= first <= last <= 255");
    size_t array_loc = loc;
    widths = leaf(c, widths, &array_loc, NULL, e);
    if (!widths) return 0;
    size_t count = (size_t)(hi - lo) + 1;
    if (widths->type != PDF_OBJECT_ARRAY)
        return fail_at(e, PDF_ERROR_MALFORMED, array_loc, "/Widths must be an array");
    if (widths->value.array.len != count)
        return fail_at(e, PDF_ERROR_MALFORMED, array_loc, "/Widths length does not match /FirstChar../LastChar");
    for (size_t i = 0; i < count; i++) {
        if (!leaf_number(c, widths->value.array.items[i], array_loc,
                         &f->widths[(size_t)lo + i], "/Widths entries must be finite numbers", e))
            return 0;
    }
    f->info.first_char = (unsigned)lo;
    f->info.last_char = (unsigned)hi;
    return 1;
}

static pdf_font *parse_font(pdf_font_context *c, const pdf_object *entry, size_t loc,
                            const unsigned char *name, size_t len, pdf_error *e) {
    const pdf_object *font = leaf_dict(c, entry, &loc, "font resource must be a dictionary", e);
    if (!font) return NULL;
    pdf_font *f = calloc(1, sizeof(*f));
    if (!f) { fail_at(e, PDF_ERROR_OUT_OF_MEMORY, loc, "cannot allocate font"); return NULL; }
    f->info.offset = loc;
    f->max_utf8 = c->limits.max_token_size;
    const pdf_object *type = pdf_dict_get(font, "Type");
    const pdf_object *subtype = pdf_dict_get(font, "Subtype");
    const pdf_object *base = pdf_dict_get(font, "BaseFont");
    if (!type) { fail_at(e, PDF_ERROR_MALFORMED, loc, "font requires /Type /Font"); goto fail; }
    if (!(type = leaf_name(c, type, loc, "font requires /Type /Font", e))) goto fail;
    if (!name_is(type, "Font")) { fail_at(e, PDF_ERROR_MALFORMED, loc, "font requires /Type /Font"); goto fail; }
    if (!subtype) { fail_at(e, PDF_ERROR_MALFORMED, loc, "font requires a /Subtype name"); goto fail; }
    if (!(subtype = leaf_name(c, subtype, loc, "font requires a /Subtype name", e))) goto fail;
    if (name_is(subtype, "Type1")) f->info.subtype = PDF_FONT_TYPE1;
    else if (name_is(subtype, "TrueType")) f->info.subtype = PDF_FONT_TRUETYPE;
    else if (name_is(subtype, "Type3")) { fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported font subtype /Type3"); goto fail; }
    else if (name_is(subtype, "MMType1")) { fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported font subtype /MMType1"); goto fail; }
    else if (name_is(subtype, "Type0")) { fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported font subtype /Type0"); goto fail; }
    else { fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported font subtype"); goto fail; }
    if (!base) { fail_at(e, PDF_ERROR_MALFORMED, loc, "font requires a /BaseFont name"); goto fail; }
    if (!(base = leaf_name(c, base, loc, "font requires a /BaseFont name", e))) goto fail;
    if (is_symbolic_base(&base->value.name)) {
        fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported Symbol/ZapfDingbats font"); goto fail;
    }
    if (pdf_dict_get(font, "ToUnicode")) {
        fail_at(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported /ToUnicode CMap"); goto fail;
    }
    if (!parse_encoding(c, f, font, loc, e) || !parse_descriptor(c, f, font, loc, e) ||
        !parse_widths(c, f, font, loc, e)) goto fail;
    f->name = copy_bytes(name, len);
    f->base = copy_bytes(base->value.name.data, base->value.name.len);
    if (!f->name || !f->base) { fail_at(e, PDF_ERROR_OUT_OF_MEMORY, loc, "cannot copy font names"); goto fail; }
    f->info.resource_name = f->name; f->info.resource_name_len = len;
    f->info.base_font = f->base; f->info.base_font_len = base->value.name.len;
    return f;
fail:
    font_free(f);
    return NULL;
}

static int grow(void **items, size_t *cap, size_t len, size_t size, size_t limit) {
    if (len < *cap) return 1;
    size_t next = *cap ? *cap * 2 : 4;
    if (next < *cap || next > limit) next = limit;
    if (next <= len || next > SIZE_MAX / size) return -1;
    void *grown = realloc(*items, next * size);
    if (!grown) return 0;
    *items = grown; *cap = next;
    return 1;
}
static int reserve(pdf_font_context *c, int need_font, pdf_error *e) {
    size_t limit = c->limits.max_container_entries;
    int ok = grow((void **)&c->bindings, &c->binding_cap, c->binding_len, sizeof(*c->bindings), limit);
    if (ok > 0 && need_font)
        ok = grow((void **)&c->fonts, &c->font_cap, c->font_len, sizeof(*c->fonts), limit);
    if (ok < 0) return fail_at(e, PDF_ERROR_RESOURCE_LIMIT, c->fonts_offset, "font cache limit exceeded");
    if (!ok) return fail_at(e, PDF_ERROR_OUT_OF_MEMORY, c->fonts_offset, "cannot grow font cache");
    return 1;
}

static const pdf_font *resolve(pdf_font_context *c, const unsigned char *name, size_t len,
                               pdf_error *e) {
    for (size_t i = 0; i < c->binding_len; i++)
        if (c->bindings[i].len == len && (!len || memcmp(c->bindings[i].name, name, len) == 0))
            return c->bindings[i].font;
    if (len > c->limits.max_token_size) {
        fail_at(e, PDF_ERROR_RESOURCE_LIMIT, c->page_offset, "font name exceeds token limit");
        return NULL;
    }
    if (!c->fonts_dict) {
        size_t loc = c->page_offset;
        const pdf_object *fonts = c->resources ? pdf_dict_get(c->resources, "Font") : NULL;
        if (!fonts) { fail_at(e, PDF_ERROR_MALFORMED, loc, "page has no /Resources /Font"); return NULL; }
        fonts = leaf_dict(c, fonts, &loc, "/Resources /Font must be a dictionary", e);
        if (!fonts) return NULL;
        c->fonts_dict = fonts; c->fonts_offset = loc;
    }
    const pdf_object *entry = pdf_dict_get_bytes(c->fonts_dict, name, len);
    if (!entry) {
        fail_at(e, PDF_ERROR_MALFORMED, c->fonts_offset, "font resource not found");
        return NULL;
    }
    pdf_font *font = NULL;
    if (entry->type == PDF_OBJECT_REF) {
        for (size_t i = 0; i < c->font_len && !font; i++)
            if (c->fonts[i]->has_reference && same_reference(c->fonts[i]->reference, entry->value.reference))
                font = c->fonts[i];
    }
    if (!reserve(c, font == NULL, e)) return NULL;
    unsigned char *copy = copy_bytes(name, len);
    if (!copy) { fail_at(e, PDF_ERROR_OUT_OF_MEMORY, c->fonts_offset, "cannot copy font name"); return NULL; }
    if (!font) {
        font = parse_font(c, entry, c->fonts_offset, name, len, e);
        if (!font) { free(copy); return NULL; }
        if (entry->type == PDF_OBJECT_REF) {
            font->has_reference = 1;
            font->reference = entry->value.reference;
        }
        c->fonts[c->font_len++] = font;
    }
    c->bindings[c->binding_len++] = (binding){copy, len, font};
    return font;
}

const pdf_font *pdf_font_context_resolve(pdf_font_context *c, const unsigned char *name,
                                         size_t len, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return NULL;
    if (!c || (!name && len)) { fail_at(e, PDF_ERROR_IO, 0, "invalid font lookup"); return NULL; }
    if (c->failed) { fail_at(e, PDF_ERROR_MALFORMED, c->page_offset, "font context is unusable after failure"); return NULL; }
    const pdf_font *font = resolve(c, name, len, e);
    if (!font) {
        c->failed = 1;
        if (e->code == PDF_ERROR_NONE) fail_at(e, PDF_ERROR_MALFORMED, c->page_offset, "font lookup failed");
    }
    return font;
}

int pdf_font_width(const pdf_font *f, unsigned char code, double *width,
                   pdf_font_width_source *source, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!f || !width) return fail_at(e, PDF_ERROR_IO, 0, "invalid width lookup");
    pdf_font_width_source from;
    if (code >= f->info.first_char && code <= f->info.last_char) {
        *width = f->widths[code]; from = PDF_FONT_WIDTH_TABLE;
    } else if (f->info.missing == PDF_FONT_MISSING_EXPLICIT) {
        *width = f->info.missing_width; from = PDF_FONT_WIDTH_MISSING;
    } else if (f->info.missing == PDF_FONT_MISSING_DEFAULT_ZERO) {
        *width = 0; from = PDF_FONT_WIDTH_DESCRIPTOR_DEFAULT_ZERO;
    } else {
        return fail_at(e, PDF_ERROR_UNSUPPORTED, f->info.offset,
                       "unsupported font metrics: code outside /Widths and no /FontDescriptor");
    }
    if (source) *source = from;
    return 1;
}

static uint32_t code_point(const pdf_font *f, unsigned char b) {
    if (b >= 0x20 && b <= 0x7E) return b;
    if (f->info.encoding == PDF_FONT_ENCODING_ASCII_FALLBACK || b < 0x20) return REPLACEMENT;
    if (b == 0x7F) return 0x2022;
    if (b < 0xA0) return win_ansi_high[b - 0x80];
    return b;
}
static size_t utf8_size(uint32_t cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }

int pdf_font_decode(const pdf_font *f, const unsigned char *data, size_t len,
                    pdf_font_utf8 *out, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!f || !out || (!data && len)) return fail_at(e, PDF_ERROR_IO, 0, "invalid decode input");
    if (out->data || out->len || out->replacements)
        return fail_at(e, PDF_ERROR_IO, f->info.offset, "decode result must start empty");
    if (len > f->max_utf8)
        return fail_at(e, PDF_ERROR_RESOURCE_LIMIT, f->info.offset, "decode input exceeds token limit");
    size_t total = 0, replacements = 0;
    for (size_t i = 0; i < len; i++) {
        size_t n = utf8_size(code_point(f, data[i]));
        if (n > f->max_utf8 - total || total > SIZE_MAX - n - 1)
            return fail_at(e, PDF_ERROR_RESOURCE_LIMIT, f->info.offset, "decoded UTF-8 exceeds token limit");
        total += n;
    }
    unsigned char *buffer = malloc(total + 1);
    if (!buffer) return fail_at(e, PDF_ERROR_OUT_OF_MEMORY, f->info.offset, "cannot allocate UTF-8");
    size_t at = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t cp = code_point(f, data[i]);
        if (cp == REPLACEMENT) replacements++;
        if (cp < 0x80) buffer[at++] = (unsigned char)cp;
        else if (cp < 0x800) {
            buffer[at++] = (unsigned char)(0xC0 | cp >> 6);
            buffer[at++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            buffer[at++] = (unsigned char)(0xE0 | cp >> 12);
            buffer[at++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buffer[at++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else {
            buffer[at++] = (unsigned char)(0xF0 | cp >> 18);
            buffer[at++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            buffer[at++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buffer[at++] = (unsigned char)(0x80 | (cp & 0x3F));
        }
    }
    buffer[at] = 0;
    *out = (pdf_font_utf8){buffer, at, replacements};
    return 1;
}
void pdf_font_utf8_free(pdf_font_utf8 *out) {
    if (!out) return;
    free(out->data);
    *out = (pdf_font_utf8){0};
}

int pdf_font_get_info(const pdf_font *f, pdf_font_info *info) {
    if (!f || !info) return 0;
    *info = f->info;
    return 1;
}
const char *pdf_font_subtype_name(pdf_font_subtype s) {
    return s == PDF_FONT_TYPE1 ? "Type1" : s == PDF_FONT_TRUETYPE ? "TrueType" : "unknown";
}
const char *pdf_font_encoding_name(pdf_font_encoding x) {
    return x == PDF_FONT_ENCODING_WIN_ANSI ? "WinAnsiEncoding" :
           x == PDF_FONT_ENCODING_ASCII_FALLBACK ? "ascii-fallback" : "unknown";
}
const char *pdf_font_width_source_name(pdf_font_width_source s) {
    return s == PDF_FONT_WIDTH_TABLE ? "widths" : s == PDF_FONT_WIDTH_MISSING ? "missing-width" :
           s == PDF_FONT_WIDTH_DESCRIPTOR_DEFAULT_ZERO ? "descriptor-default-zero" : "unknown";
}
const char *pdf_font_missing_name(pdf_font_missing m) {
    return m == PDF_FONT_MISSING_NONE ? "none" : m == PDF_FONT_MISSING_EXPLICIT ? "explicit" :
           m == PDF_FONT_MISSING_DEFAULT_ZERO ? "descriptor-default-zero" : "unknown";
}

static void hex(FILE *s, const unsigned char *d, size_t n) {
    fputc('"', s);
    for (size_t i = 0; i < n; i++) fprintf(s, "%02x", d[i]);
    fputc('"', s);
}
int pdf_font_dump(FILE *s, const pdf_font *f, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!s || !f) return fail_at(e, PDF_ERROR_IO, 0, "invalid font dump input");
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) return fail_at(e, PDF_ERROR_OUT_OF_MEMORY, f->info.offset, "cannot allocate numeric locale");
    locale_t previous = uselocale(numeric);
    if (!previous) {
        freelocale(numeric);
        return fail_at(e, PDF_ERROR_IO, f->info.offset, "cannot select numeric locale");
    }
    const pdf_font_info *i = &f->info;
    fprintf(s, "{\"font_len\":%zu,\"font\":", i->resource_name_len);
    hex(s, i->resource_name, i->resource_name_len);
    fprintf(s, ",\"base_font_len\":%zu,\"base_font\":", i->base_font_len);
    hex(s, i->base_font, i->base_font_len);
    fprintf(s, ",\"subtype\":\"%s\",\"encoding\":\"%s\",\"first_char\":%u,\"last_char\":%u,"
               "\"missing\":\"%s\",\"missing_width\":%.9f,\"offset\":%zu}\n",
            pdf_font_subtype_name(i->subtype), pdf_font_encoding_name(i->encoding),
            i->first_char, i->last_char, pdf_font_missing_name(i->missing),
            i->missing_width, i->offset);
    uselocale(previous);
    freelocale(numeric);
    if (ferror(s)) return fail_at(e, PDF_ERROR_IO, f->info.offset, "cannot write font diagnostic");
    return 1;
}
