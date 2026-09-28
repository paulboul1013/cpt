#define _POSIX_C_SOURCE 200809L
#include "font_text.h"

#include <locale.h>
#include <stdint.h>

typedef struct {
    pdf_font_context *fonts;
    pdf_text_state *state;
    const pdf_font *active; /* Tracks the M8 state's current font name. */
    pdf_limits limits;
    pdf_font_text_totals *totals;
    pdf_font_text_consumer consumer;
    void *consumer_context;
} bridge;

/* Font errors carry file offsets; M7 replaces the visitor offset with the
 * decoded operation offset, so keep the font offset in the message only. */
static void wrap(pdf_error *e, const pdf_error *local) {
    pdf_error_set(e, local->code, local->offset, "font", "font byte %zu (%s): %s",
                  local->offset, local->module ? local->module : "font", local->message);
}

static int visit(void *context, const pdf_content_operation *op, pdf_error *e) {
    bridge *b = context;
    if (!pdf_text_state_visit(b->state, op, e)) return 0;
    if (op->kind != PDF_CONTENT_TF && op->kind != PDF_CONTENT_RESTORE) return 1;
    pdf_text_snapshot snapshot;
    if (!pdf_text_state_snapshot(b->state, &snapshot)) {
        pdf_error_set(e, PDF_ERROR_MALFORMED, op->offset, "font", "text state unavailable");
        return 0;
    }
    if (!snapshot.font_set) { b->active = NULL; return 1; }
    pdf_error local;
    pdf_error_init(&local);
    const pdf_font *font = pdf_font_context_resolve(b->fonts, snapshot.font.data,
                                                    snapshot.font.len, &local);
    if (!font) { wrap(e, &local); return 0; }
    b->active = font;
    return 1;
}

static int metrics(void *context, pdf_text_bytes font, unsigned char code,
                   double *width, pdf_error *e) {
    bridge *b = context;
    (void)font; /* b->active was resolved from this exact state font name. */
    if (!b->active) {
        pdf_error_set(e, PDF_ERROR_MALFORMED, 0, "font", "no active font for text show");
        return 0;
    }
    pdf_error local;
    pdf_error_init(&local);
    if (!pdf_font_width(b->active, code, width, NULL, &local)) { wrap(e, &local); return 0; }
    return 1;
}

static int consume(void *context, const pdf_text_event *raw, pdf_error *e) {
    bridge *b = context;
    if (!b->active) {
        pdf_error_set(e, PDF_ERROR_MALFORMED, raw->offset, "font", "no active font for text show");
        return 0;
    }
    if (b->totals->events >= b->limits.max_container_entries) {
        pdf_error_set(e, PDF_ERROR_RESOURCE_LIMIT, raw->offset, "font", "decoded event count exceeds limit");
        return 0;
    }
    pdf_error local;
    pdf_error_init(&local);
    pdf_font_utf8 utf8 = {0};
    if (!pdf_font_decode(b->active, raw->bytes.data, raw->bytes.len, &utf8, &local)) {
        wrap(e, &local);
        return 0;
    }
    if (b->totals->utf8_bytes > b->limits.max_total_decoded_size ||
        utf8.len > b->limits.max_total_decoded_size - b->totals->utf8_bytes) {
        pdf_font_utf8_free(&utf8);
        pdf_error_set(e, PDF_ERROR_RESOURCE_LIMIT, raw->offset, "font",
                      "total decoded UTF-8 exceeds configured limit");
        return 0;
    }
    b->totals->utf8_bytes += utf8.len;
    b->totals->events++;
    pdf_font_text_event event = {raw, b->active, utf8.data, utf8.len, utf8.replacements};
    int ok = !b->consumer || b->consumer(b->consumer_context, &event, e);
    pdf_font_utf8_free(&utf8);
    if (!ok || e->code != PDF_ERROR_NONE) {
        pdf_error_set(e, PDF_ERROR_MALFORMED, raw->offset, "font", "decoded text consumer aborted");
        return 0;
    }
    return 1;
}

int pdf_font_text_page_interpret(pdf_document *document, const pdf_page *page,
                                 const unsigned char *data, size_t len,
                                 const pdf_limits *limits, size_t page_offset,
                                 pdf_font_text_totals *totals,
                                 pdf_font_text_consumer consumer, void *context,
                                 pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!document || !totals) {
        pdf_error_set(e, PDF_ERROR_IO, page_offset, "font", "invalid page bridge input");
        return 0;
    }
    if (!pdf_text_page_check(page, page_offset, e)) return 0;
    bridge b = {0};
    if (limits) b.limits = *limits; else pdf_limits_default(&b.limits);
    b.totals = totals;
    b.consumer = consumer;
    b.consumer_context = context;
    b.fonts = pdf_font_context_create(document, page->resources, page_offset, &b.limits, e);
    if (b.fonts) b.state = pdf_text_state_create(&b.limits, metrics, &b, consume, &b, e);
    int ok = 0;
    if (b.state) {
        pdf_content_result result;
        ok = pdf_content_interpret(data, len, &b.limits, page_offset, visit, NULL, &b, &result, e);
    }
    pdf_text_state_destroy(b.state);
    pdf_font_context_destroy(b.fonts);
    return ok;
}

static void hex(FILE *s, const unsigned char *d, size_t n) {
    fputc('"', s);
    for (size_t i = 0; i < n; i++) fprintf(s, "%02x", d[i]);
    fputc('"', s);
}
/* ASCII-only JSON preview of UTF-8 from pdf_font_decode. Bytes are bounds-
 * checked; a truncated or invalid sequence prints U+FFFD and advances one byte.
 * Supplementary scalars are emitted as JSON surrogate pairs. */
static void preview(FILE *s, const unsigned char *d, size_t n) {
    fputc('"', s);
    for (size_t i = 0; i < n;) {
        uint32_t cp = d[i];
        size_t k = cp < 0x80 ? 1 : (cp & 0xE0) == 0xC0 ? 2 : (cp & 0xF0) == 0xE0 ? 3 :
                   (cp & 0xF8) == 0xF0 ? 4 : 0;
        for (size_t j = 1; k && j < k; j++)
            if (i + j >= n || (d[i + j] & 0xC0) != 0x80) k = 0;
        if (!k) { cp = 0xFFFD; k = 1; }
        else if (k == 2) cp = (cp & 0x1Fu) << 6 | (d[i + 1] & 0x3Fu);
        else if (k == 3) cp = (cp & 0x0Fu) << 12 | (d[i + 1] & 0x3Fu) << 6 | (d[i + 2] & 0x3Fu);
        else if (k == 4) cp = (cp & 0x07u) << 18 | (d[i + 1] & 0x3Fu) << 12 |
                              (d[i + 2] & 0x3Fu) << 6 | (d[i + 3] & 0x3Fu);
        if (cp >= 0x20 && cp <= 0x7E && cp != '"' && cp != '\\') fputc((int)cp, s);
        else if (cp >= 0x10000)
            fprintf(s, "\\u%04x\\u%04x", (unsigned)(0xD800 + ((cp - 0x10000) >> 10)),
                    (unsigned)(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        else fprintf(s, "\\u%04x", (unsigned)cp);
        i += k;
    }
    fputc('"', s);
}

int pdf_font_text_event_dump(FILE *s, const pdf_font_text_event *v, size_t page, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!s || !v || !v->raw || !v->font || (!v->utf8 && v->utf8_len)) {
        pdf_error_set(e, PDF_ERROR_IO, 0, "font-debug", "invalid dump input");
        return 0;
    }
    if (!pdf_text_event_dump(s, v->raw, page, e)) return 0;
    pdf_font_info info;
    pdf_font_get_info(v->font, &info);
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) {
        pdf_error_set(e, PDF_ERROR_OUT_OF_MEMORY, v->raw->offset, "font-debug", "cannot allocate numeric locale");
        return 0;
    }
    locale_t previous = uselocale(numeric);
    if (!previous) {
        freelocale(numeric);
        pdf_error_set(e, PDF_ERROR_IO, v->raw->offset, "font-debug", "cannot select numeric locale");
        return 0;
    }
    int ok = 1;
    fprintf(s, "{\"page\":%zu,\"order\":%zu,\"decode\":{\"subtype\":\"%s\",\"encoding\":\"%s\","
               "\"utf8_len\":%zu,\"utf8\":", page, v->raw->source_order,
            pdf_font_subtype_name(info.subtype), pdf_font_encoding_name(info.encoding), v->utf8_len);
    hex(s, v->utf8, v->utf8_len);
    fputs(",\"preview\":", s);
    preview(s, v->utf8, v->utf8_len);
    fprintf(s, ",\"replacements\":%zu,\"widths\":[", v->replacements);
    const pdf_text_bytes *raw = &v->raw->bytes;
    for (size_t i = 0; ok && i < raw->len; i++) {
        double w;
        pdf_font_width_source source;
        ok = pdf_font_width(v->font, raw->data[i], &w, &source, e);
        if (ok) fprintf(s, "%s%.9f", i ? "," : "", w);
    }
    fputs("],\"width_sources\":[", s);
    for (size_t i = 0; ok && i < raw->len; i++) {
        double w;
        pdf_font_width_source source;
        ok = pdf_font_width(v->font, raw->data[i], &w, &source, e);
        if (ok) fprintf(s, "%s\"%s\"", i ? "," : "", pdf_font_width_source_name(source));
    }
    fputs("]}}\n", s);
    uselocale(previous);
    freelocale(numeric);
    if (!ok) return 0;
    if (ferror(s)) {
        pdf_error_set(e, PDF_ERROR_IO, v->raw->offset, "font-debug", "cannot write diagnostic trace");
        return 0;
    }
    return 1;
}
