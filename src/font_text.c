#define _POSIX_C_SOURCE 200809L
#include "font_text.h"
#include "extgstate.h"
#include "xobject.h"

#include <locale.h>
#include <string.h>

typedef enum { MARKED_PASS, MARKED_SUPPRESS } marked_mode;

typedef struct {
    pdf_document *document;
    const pdf_object *resources;
    size_t page_offset;
    pdf_font_context *fonts;
    pdf_text_state *state;
    const pdf_font *active; /* Tracks the M8 state's current font name. */
    pdf_limits limits;
    pdf_font_text_totals *totals;
    pdf_font_text_consumer consumer;
    void *consumer_context;
    size_t marked_depth, suppress_depth;
    marked_mode marked;
} bridge;

/* M7 owns grammar and the depth bound. Under A, suppression is monotone
 * inside a scope, so only the outermost SUPPRESS depth is needed to restore
 * PASS at its EMC. No operand pointer or per-frame allocation survives visit. */
static int begin_marked(bridge *b, const pdf_content_operation *op, pdf_error *e) {
    const pdf_bytes *tag = &op->operands[0]->value.name;
    const pdf_object *props = op->operands[1];
    if (tag->len != 4 || memcmp(tag->data, "Span", 4) != 0 || props->type != PDF_OBJECT_DICT) {
        pdf_error_set(e, PDF_ERROR_UNSUPPORTED, op->offset, "marked-content",
                      "marked-content requires /Span and inline properties");
        return 0;
    }
    marked_mode mode = MARKED_PASS;
    for (size_t i = 0; i < props->value.dict.len; i++) {
        const pdf_dict_entry *entry = &props->value.dict.entries[i];
        if (entry->key.len != 10 || memcmp(entry->key.data, "ActualText", 10) != 0) {
            pdf_error_set(e, PDF_ERROR_UNSUPPORTED, op->offset, "marked-content",
                          "marked-content property other than ActualText is unsupported");
            return 0;
        }
        const pdf_object *value = entry->value;
        if (value->type != PDF_OBJECT_STRING && value->type != PDF_OBJECT_HEX_STRING) {
            pdf_error_set(e, PDF_ERROR_MALFORMED, op->offset, "marked-content",
                          "marked-content ActualText must be a string");
            return 0;
        }
        size_t len = value->type == PDF_OBJECT_STRING ? value->value.string.len : value->value.hex_string.len;
        if (len != 0) {
            pdf_error_set(e, PDF_ERROR_UNSUPPORTED, op->offset, "marked-content",
                          "marked-content nonempty ActualText is unsupported");
            return 0;
        }
        mode = MARKED_SUPPRESS;
    }
    b->marked_depth++;
    if (mode == MARKED_SUPPRESS && b->marked == MARKED_PASS) {
        b->marked = MARKED_SUPPRESS;
        b->suppress_depth = b->marked_depth;
    }
    return 1;
}

/* Font errors carry file offsets; M7 replaces the visitor offset with the
 * decoded operation offset, so keep the font offset in the message only. */
static void wrap(pdf_error *e, const pdf_error *local) {
    pdf_error_set(e, local->code, local->offset, "font", "font byte %zu (%s): %s",
                  local->offset, local->module ? local->module : "font", local->message);
}

static int visit(void *context, const pdf_content_operation *op, pdf_error *e) {
    bridge *b = context;
    if (op->kind == PDF_CONTENT_DO) {
        const pdf_bytes *name = &op->operands[0]->value.name;
        pdf_error local; pdf_error_init(&local);
        if (pdf_xobject_check_image(b->document, b->resources, name->data, name->len,
                                    &b->limits, b->page_offset, &local)) return 1;
        pdf_error_set(e, local.code, local.offset, "xobject", "XObject byte %zu (%s): %s",
                      local.offset, local.module ? local.module : "xobject", local.message);
        return 0;
    }
    if (op->kind == PDF_CONTENT_BDC && !begin_marked(b, op, e)) return 0;
    if (op->kind == PDF_CONTENT_EMC) {
        if (b->marked == MARKED_SUPPRESS && b->marked_depth == b->suppress_depth)
            b->marked = MARKED_PASS;
        b->marked_depth--;
    }
    if (op->kind == PDF_CONTENT_GS) {
        const pdf_bytes *name = &op->operands[0]->value.name;
        pdf_error local;
        pdf_error_init(&local);
        if (pdf_extgstate_check(b->document, b->resources, name->data, name->len,
                               &b->limits, b->page_offset, &local)) return 1;
        pdf_error_set(e, local.code, local.offset, "extgstate", "ExtGState byte %zu (%s): %s",
                      local.offset, local.module ? local.module : "extgstate", local.message);
        return 0;
    }
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

static int metrics(void *context, pdf_text_bytes font, pdf_text_bytes remaining,
                   size_t *consumed, double *width, pdf_error *e) {
    bridge *b = context;
    (void)font; /* b->active was resolved from this exact state font name. */
    if (!b->active) {
        pdf_error_set(e, PDF_ERROR_MALFORMED, 0, "font", "no active font for text show");
        return 0;
    }
    pdf_error local;
    pdf_error_init(&local);
    unsigned code;
    if (!pdf_font_read_code(b->active, remaining.data, remaining.len, &code, consumed, &local) ||
        !pdf_font_width(b->active, code, width, NULL, &local)) { wrap(e, &local); return 0; }
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
    b->totals->events++;
    if (b->marked == MARKED_SUPPRESS) return 1;
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
    b.document = document;
    b.resources = page->resources;
    b.page_offset = page_offset;
    b.totals = totals;
    b.consumer = consumer;
    b.consumer_context = context;
    b.fonts = pdf_font_context_create(document, page->resources, page_offset, &b.limits,
                                      &totals->cmap_bytes, e);
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
    pdf_font_json_preview(s, v->utf8, v->utf8_len);
    fprintf(s, ",\"replacements\":%zu,\"widths\":[", v->replacements);
    const pdf_text_bytes *raw = &v->raw->bytes;
    for (size_t i = 0; ok && i < raw->len;) {
        double w;
        pdf_font_width_source source;
        unsigned code;
        size_t used;
        ok = pdf_font_read_code(v->font, raw->data + i, raw->len - i, &code, &used, e) &&
             pdf_font_width(v->font, code, &w, &source, e);
        if (ok) { fprintf(s, "%s%.9f", i ? "," : "", w); i += used; }
    }
    fputs("],\"width_sources\":[", s);
    for (size_t i = 0; ok && i < raw->len;) {
        double w;
        pdf_font_width_source source;
        unsigned code;
        size_t used;
        ok = pdf_font_read_code(v->font, raw->data + i, raw->len - i, &code, &used, e) &&
             pdf_font_width(v->font, code, &w, &source, e);
        if (ok) { fprintf(s, "%s\"%s\"", i ? "," : "", pdf_font_width_source_name(source)); i += used; }
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
