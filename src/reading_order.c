#define _POSIX_C_SOURCE 200809L
#include "reading_order.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double key;       /* y while grouping, x within a line. */
    size_t sequence;  /* Deterministic tie-breaker. */
    size_t index;
} sort_key;

static int fail(pdf_error *e, pdf_error_code code, size_t offset, const char *why) {
    pdf_error_set(e, code, offset, "reading-order", "%s", why);
    return 0;
}
static int by_key_desc(const void *a, const void *b) {
    const sort_key *x = a, *y = b;
    if (x->key != y->key) return x->key > y->key ? -1 : 1;
    return x->sequence < y->sequence ? -1 : x->sequence > y->sequence;
}
static int by_key_asc(const void *a, const void *b) {
    const sort_key *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->sequence < y->sequence ? -1 : x->sequence > y->sequence;
}

void pdf_text_lines_free(pdf_text_lines *lines) {
    if (!lines) return;
    free(lines->lines);
    free(lines->indices);
    *lines = (pdf_text_lines){0};
}

/* Close keys[start, end) as one line: order it left to right and record it. */
static void close_line(const pdf_text_items *items, sort_key *keys, size_t start, size_t end,
                       pdf_text_lines *out, size_t *used) {
    for (size_t k = start; k < end; k++) keys[k].key = items->items[keys[k].index].x;
    qsort(keys + start, end - start, sizeof(*keys), by_key_asc);
    const pdf_text_item *anchor = &items->items[keys[start].index];
    pdf_text_line *line = &out->lines[out->len++];
    line->page = anchor->page;
    line->indices = out->indices + *used;
    line->count = end - start;
    for (size_t k = start; k < end; k++) out->indices[(*used)++] = keys[k].index;
}

int pdf_text_lines_build(const pdf_text_items *items, pdf_text_lines *out, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!items || !out || (!items->items && items->len))
        return fail(e, PDF_ERROR_IO, 0, "invalid reading order input");
    if (out->lines || out->indices || out->len)
        return fail(e, PDF_ERROR_IO, 0, "line result must start empty");
    size_t n = items->len;
    for (size_t i = 0; i < n; i++) {
        const pdf_text_item *t = &items->items[i];
        if (!t->horizontal) {
            pdf_error_set(e, PDF_ERROR_UNSUPPORTED, t->page_offset, "reading-order",
                          "page %zu decoded byte %zu: unsupported non-horizontal text "
                          "(rotated, mirrored or vertical)", t->page, t->offset);
            return 0;
        }
    }
    if (!n) return 1;
    if (n > SIZE_MAX / sizeof(sort_key) || n > SIZE_MAX / sizeof(pdf_text_line))
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, 0, "too many text items to order");
    sort_key *keys = malloc(n * sizeof(*keys));
    out->lines = malloc(n * sizeof(*out->lines));
    out->indices = malloc(n * sizeof(*out->indices));
    if (!keys || !out->lines || !out->indices) {
        free(keys);
        pdf_text_lines_free(out);
        return fail(e, PDF_ERROR_OUT_OF_MEMORY, 0, "cannot allocate reading order");
    }
    size_t used = 0;
    for (size_t start = 0; start < n;) {
        size_t end = start;
        while (end < n && items->items[end].page == items->items[start].page) end++;
        for (size_t k = start; k < end; k++)
            keys[k] = (sort_key){items->items[k].y, items->items[k].sequence, k};
        qsort(keys + start, end - start, sizeof(*keys), by_key_desc);
        size_t line_start = start;
        for (size_t k = start + 1; k <= end; k++) {
            if (k < end) {
                const pdf_text_item *anchor = &items->items[keys[line_start].index];
                const pdf_text_item *t = &items->items[keys[k].index];
                double tol = fmax(1.5, fmin(anchor->effective_size, t->effective_size) * 0.25);
                if (anchor->y - t->y <= tol) continue;
            }
            const pdf_text_item *anchor = &items->items[keys[line_start].index];
            double anchor_y = anchor->y, anchor_size = anchor->effective_size;
            close_line(items, keys, line_start, k, out, &used);
            out->lines[out->len - 1].anchor_y = anchor_y;
            out->lines[out->len - 1].anchor_size = anchor_size;
            line_start = k;
        }
        start = end;
    }
    free(keys);
    return 1;
}

typedef struct {
    pdf_plain_text *out;
    size_t cap, limit;
} text_buffer;
static int append(text_buffer *b, const unsigned char *data, size_t len, pdf_error *e) {
    pdf_plain_text *out = b->out;
    if (len > b->limit - out->len)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, 0, "plain text output exceeds configured limit");
    if (out->len + len > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < out->len + len) cap = cap > SIZE_MAX / 2 ? out->len + len : cap * 2;
        unsigned char *data2 = realloc(out->data, cap);
        if (!data2) return fail(e, PDF_ERROR_OUT_OF_MEMORY, 0, "cannot grow plain text");
        out->data = data2;
        b->cap = cap;
    }
    if (len) memcpy(out->data + out->len, data, len);
    out->len += len;
    return 1;
}
static int is_space_at_end(const pdf_text_item *t) {
    return (t->text_len >= 1 && t->text[t->text_len - 1] == ' ') ||
           (t->text_len >= 2 && t->text[t->text_len - 2] == 0xC2 && t->text[t->text_len - 1] == 0xA0);
}
static int is_space_at_start(const pdf_text_item *t) {
    return (t->text_len >= 1 && t->text[0] == ' ') ||
           (t->text_len >= 2 && t->text[0] == 0xC2 && t->text[1] == 0xA0);
}

int pdf_plain_text_build(const pdf_text_items *items, const pdf_limits *limits,
                         pdf_plain_text *out, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!out) return fail(e, PDF_ERROR_IO, 0, "invalid plain text output");
    if (out->data || out->len) return fail(e, PDF_ERROR_IO, 0, "plain text result must start empty");
    pdf_limits defaults;
    if (!limits) { pdf_limits_default(&defaults); limits = &defaults; }
    pdf_text_lines lines = {0};
    if (!pdf_text_lines_build(items, &lines, e)) return 0;
    text_buffer b = {out, 0, limits->max_total_decoded_size};
    int ok = 1;
    size_t page = 0, lines_out = 0, pages = 0, replacements = 0;
    for (size_t l = 0; ok && l < lines.len; l++) {
        const pdf_text_line *line = &lines.lines[l];
        if (line->page != page) {
            if (page) ok = append(&b, (const unsigned char *)"\n", 1, e);
            page = line->page;
            pages++;
        }
        const pdf_text_item *prev = NULL;
        for (size_t k = 0; ok && k < line->count; k++) {
            const pdf_text_item *t = &items->items[line->indices[k]];
            if (prev) {
                double gap = t->x - (prev->x + prev->width);
                if (gap > prev->effective_size * 0.25 && !is_space_at_end(prev) && !is_space_at_start(t))
                    ok = append(&b, (const unsigned char *)" ", 1, e);
            }
            if (ok) ok = append(&b, t->text, t->text_len, e);
            replacements += t->replacements;
            prev = t;
        }
        if (ok) ok = append(&b, (const unsigned char *)"\n", 1, e);
        lines_out++;
    }
    pdf_text_lines_free(&lines);
    if (!ok) { pdf_plain_text_free(out); return 0; }
    out->lines = lines_out;
    out->pages_with_text = pages;
    out->replacements = replacements;
    return 1;
}

void pdf_plain_text_free(pdf_plain_text *text) {
    if (!text) return;
    free(text->data);
    *text = (pdf_plain_text){0};
}

int pdf_text_lines_dump(FILE *s, const pdf_text_items *items, const pdf_text_lines *lines,
                        pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!s || !items || !lines) return fail(e, PDF_ERROR_IO, 0, "invalid line dump input");
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) return fail(e, PDF_ERROR_OUT_OF_MEMORY, 0, "cannot allocate numeric locale");
    locale_t previous = uselocale(numeric);
    if (!previous) { freelocale(numeric); return fail(e, PDF_ERROR_IO, 0, "cannot select numeric locale"); }
    for (size_t l = 0; l < lines->len; l++) {
        const pdf_text_line *line = &lines->lines[l];
        fprintf(s, "{\"line\":%zu,\"page\":%zu,\"anchor_y\":%.9f,\"anchor_size\":%.9f,\"items\":[",
                l, line->page, line->anchor_y, line->anchor_size);
        for (size_t k = 0; k < line->count; k++)
            fprintf(s, "%s%zu", k ? "," : "", items->items[line->indices[k]].sequence);
        fputs("]}\n", s);
    }
    uselocale(previous);
    freelocale(numeric);
    if (ferror(s)) return fail(e, PDF_ERROR_IO, 0, "cannot write line dump");
    return 1;
}
