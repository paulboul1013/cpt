#define _POSIX_C_SOURCE 200809L
#include "text_items.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "contents.h"

typedef struct { size_t text, font; } item_offsets;
typedef struct {
    pdf_text_items *out;
    item_offsets *offsets;    /* Arena offsets; pointers are fixed up on success. */
    size_t offsets_cap;
    pdf_limits limits;
    size_t page, page_offset;
    int have_font;            /* Last font name on this page, for sharing. */
    size_t font_at, font_len;
} collector;

static int fail(pdf_error *e, pdf_error_code code, size_t offset, const char *why) {
    pdf_error_set(e, code, offset, "text-items", "%s", why);
    return 0;
}

static int reserve_items(collector *c, size_t offset, pdf_error *e) {
    pdf_text_items *out = c->out;
    if (out->len >= c->limits.max_container_entries)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, offset, "text item count exceeds limit");
    if (out->len < out->cap) return 1;
    size_t cap = out->cap ? out->cap * 2 : 16;
    if (cap < out->cap || cap > c->limits.max_container_entries) cap = c->limits.max_container_entries;
    if (cap <= out->len || cap > SIZE_MAX / sizeof(*out->items) || cap > SIZE_MAX / sizeof(*c->offsets))
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, offset, "text item capacity overflow");
    pdf_text_item *items = realloc(out->items, cap * sizeof(*items));
    if (!items) return fail(e, PDF_ERROR_OUT_OF_MEMORY, offset, "cannot grow text items");
    out->items = items;
    item_offsets *offsets = realloc(c->offsets, cap * sizeof(*offsets));
    if (!offsets) return fail(e, PDF_ERROR_OUT_OF_MEMORY, offset, "cannot grow text items");
    c->offsets = offsets;
    out->cap = c->offsets_cap = cap;
    return 1;
}

static int reserve_bytes(collector *c, size_t need, size_t offset, pdf_error *e) {
    pdf_text_items *out = c->out;
    if (need > SIZE_MAX - out->bytes_len)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, offset, "text byte capacity overflow");
    size_t want = out->bytes_len + need;
    if (want <= out->bytes_cap) return 1;
    size_t cap = out->bytes_cap ? out->bytes_cap : 256;
    while (cap < want) cap = cap > SIZE_MAX / 2 ? want : cap * 2;
    unsigned char *bytes = realloc(out->bytes, cap);
    if (!bytes) return fail(e, PDF_ERROR_OUT_OF_MEMORY, offset, "cannot grow text bytes");
    out->bytes = bytes;
    out->bytes_cap = cap;
    return 1;
}

static int consume(void *context, const pdf_font_text_event *ev, pdf_error *e) {
    collector *c = context;
    pdf_text_items *out = c->out;
    const pdf_text_event *raw = ev->raw;
    if (raw->bytes.len == 0) return 1; /* Nothing to extract; Tf already validated. */
    size_t max = c->limits.max_total_decoded_size;
    if (ev->utf8_len > max - out->text_bytes || out->text_bytes > max)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, raw->offset, "text item UTF-8 exceeds limit");
    pdf_text_bytes font = raw->state.font;
    int share = c->have_font && c->font_len == font.len &&
                (!font.len || memcmp(out->bytes + c->font_at, font.data, font.len) == 0);
    if (!share && (font.len > max - out->font_bytes || out->font_bytes > max))
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, raw->offset, "text item font names exceed limit");
    if (!reserve_items(c, raw->offset, e)) return 0;
    size_t need = ev->utf8_len + 1;
    if (!share && font.len > SIZE_MAX - need)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, raw->offset, "text byte capacity overflow");
    if (!reserve_bytes(c, need + (share ? 0 : font.len), raw->offset, e)) return 0;
    item_offsets at = {out->bytes_len, c->font_at};
    if (ev->utf8_len) memcpy(out->bytes + out->bytes_len, ev->utf8, ev->utf8_len);
    out->bytes[out->bytes_len + ev->utf8_len] = 0;
    out->bytes_len += need;
    out->text_bytes += ev->utf8_len;
    if (!share) {
        at.font = out->bytes_len;
        if (font.len) memcpy(out->bytes + out->bytes_len, font.data, font.len);
        out->bytes_len += font.len;
        out->font_bytes += font.len;
        c->have_font = 1; c->font_at = at.font; c->font_len = font.len;
    }
    pdf_font_info info;
    pdf_font_get_info(ev->font, &info);
    pdf_matrix m = raw->rendering_matrix;
    double scale = fmax(fabs(m.a), fabs(m.d));
    pdf_text_item item = {0};
    item.text_len = ev->utf8_len;
    item.font_len = font.len;
    item.page = c->page;
    item.source_order = raw->source_order;
    item.sequence = out->len;
    item.offset = raw->offset;
    item.page_offset = c->page_offset;
    item.x = raw->origin.x; item.y = raw->origin.y;
    item.dx = raw->advance.x; item.dy = raw->advance.y;
    item.width = raw->advance.x;
    item.em_height = item.effective_size = hypot(m.c, m.d);
    item.font_size = raw->state.font_size;
    item.rendering_mode = raw->state.rendering_mode;
    item.horizontal = m.a > 0 && m.d > 0 && fabs(m.b) <= 1e-9 * scale && fabs(m.c) <= 1e-9 * scale;
    item.replacements = ev->replacements;
    item.subtype = info.subtype;
    item.encoding = info.encoding;
    if (!isfinite(item.em_height))
        return fail(e, PDF_ERROR_MALFORMED, raw->offset, "non-finite text item height");
    c->offsets[out->len] = at;
    out->items[out->len++] = item;
    return 1;
}

void pdf_text_items_free(pdf_text_items *items) {
    if (!items) return;
    free(items->items);
    free(items->bytes);
    *items = (pdf_text_items){0};
}

int pdf_text_items_extract(pdf_document *document, const pdf_limits *limits,
                           pdf_text_items *out, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!document || !out) return fail(e, PDF_ERROR_IO, 0, "invalid text item input");
    if (out->items || out->bytes || out->len || out->cap || out->bytes_len ||
        out->bytes_cap || out->text_bytes || out->font_bytes)
        return fail(e, PDF_ERROR_IO, 0, "text item collection must start empty");
    collector c = {0};
    c.out = out;
    c.limits = limits ? *limits : document->reader.limits;
    pdf_pages pages = {0};
    int ok = pdf_pages_load(document, &pages, e);
    pdf_contents_context contents;
    pdf_contents_context_init(&contents, document);
    pdf_font_text_totals totals = {0};
    for (size_t i = 0; ok && i < pages.len; i++) {
        pdf_contents_result data = {0};
        size_t offset = pdf_document_reference_offset(document, pages.items[i].reference);
        c.page = i + 1;
        c.page_offset = offset;
        c.have_font = 0;
        ok = pdf_contents_read(&contents, &pages.items[i], &data, e) &&
             pdf_font_text_page_interpret(document, &pages.items[i], data.data, data.len,
                                          &c.limits, offset, &totals, consume, &c, e);
        pdf_contents_result_free(&data);
    }
    pdf_pages_free(&pages);
    if (ok) {
        for (size_t i = 0; i < out->len; i++) {
            out->items[i].text = out->bytes + c.offsets[i].text;
            out->items[i].font = out->bytes + c.offsets[i].font;
        }
    } else {
        pdf_text_items_free(out);
    }
    free(c.offsets);
    return ok;
}

static void hex(FILE *s, const unsigned char *d, size_t n) {
    fputc('"', s);
    for (size_t i = 0; i < n; i++) fprintf(s, "%02x", d[i]);
    fputc('"', s);
}

int pdf_text_item_dump(FILE *s, const pdf_text_item *v, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!s || !v || (!v->text && v->text_len) || (!v->font && v->font_len))
        return fail(e, PDF_ERROR_IO, 0, "invalid text item dump input");
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) return fail(e, PDF_ERROR_OUT_OF_MEMORY, v->offset, "cannot allocate numeric locale");
    locale_t previous = uselocale(numeric);
    if (!previous) {
        freelocale(numeric);
        return fail(e, PDF_ERROR_IO, v->offset, "cannot select numeric locale");
    }
    fprintf(s, "{\"page\":%zu,\"order\":%zu,\"sequence\":%zu,\"offset\":%zu,\"text_len\":%zu,\"text\":",
            v->page, v->source_order, v->sequence, v->offset, v->text_len);
    hex(s, v->text, v->text_len);
    fputs(",\"preview\":", s);
    pdf_font_json_preview(s, v->text, v->text_len);
    fprintf(s, ",\"font_len\":%zu,\"font\":", v->font_len);
    hex(s, v->font, v->font_len);
    fprintf(s, ",\"x\":%.9f,\"y\":%.9f,\"advance\":[%.9f,%.9f],\"width\":%.9f,\"em_height\":%.9f,"
               "\"font_size\":%.9f,\"effective_size\":%.9f,\"mode\":%d,\"horizontal\":%d,"
               "\"replacements\":%zu,\"subtype\":\"%s\",\"encoding\":\"%s\"}\n",
            v->x, v->y, v->dx, v->dy, v->width, v->em_height, v->font_size, v->effective_size,
            v->rendering_mode, v->horizontal, v->replacements,
            pdf_font_subtype_name(v->subtype), pdf_font_encoding_name(v->encoding));
    uselocale(previous);
    freelocale(numeric);
    if (ferror(s)) return fail(e, PDF_ERROR_IO, v->offset, "cannot write text item");
    return 1;
}
