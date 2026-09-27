#include "contents.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"

void pdf_contents_context_init(pdf_contents_context *context, pdf_document *document) {
    if (context == NULL) return;
    context->document = document;
    context->total_decoded = 0;
}

void pdf_contents_result_free(pdf_contents_result *result) {
    if (result == NULL) return;
    free(result->data);
    *result = (pdf_contents_result){0};
}

static int append_bytes(pdf_contents_result *result, size_t *capacity,
                        const unsigned char *bytes, size_t len,
                        size_t remaining, size_t offset, pdf_error *error) {
    if (len > SIZE_MAX - result->len || result->len + len > remaining) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, offset, "contents",
                      "total decoded contents exceed configured limit");
        return 0;
    }
    size_t needed = result->len + len;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 8192 : *capacity;
        if (next > remaining) next = remaining;
        while (next < needed) {
            if (next > remaining / 2) {
                next = remaining;
                break;
            }
            next *= 2;
        }
        unsigned char *grown = realloc(result->data, next);
        if (grown == NULL) {
            pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, offset, "contents",
                          "could not allocate page contents");
            return 0;
        }
        result->data = grown;
        *capacity = next;
    }
    if (len != 0) memcpy(result->data + result->len, bytes, len);
    result->len = needed;
    return 1;
}

static int append_stream(pdf_contents_context *context, pdf_reference reference,
                         pdf_contents_result *result, size_t *capacity,
                         size_t remaining, pdf_error *error) {
    size_t offset = pdf_document_reference_offset(context->document, reference);
    const pdf_indirect_object *stream = pdf_resolve(context->document, reference, error);
    if (stream == NULL) {
        if (error->offset == 0) error->offset = offset;
        return 0;
    }
    if (!stream->is_stream) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "contents",
                      "Contents reference does not point to a stream");
        return 0;
    }
    size_t available = remaining - result->len;
    size_t max_stream = context->document->reader.limits.max_decoded_stream_size;
    if (available < max_stream) max_stream = available;
    pdf_bytes decoded = {0};
    if (!pdf_filter_decode(stream, max_stream, offset, &decoded, error)) return 0;
    int success = append_bytes(result, capacity, decoded.data, decoded.len,
                               remaining, offset, error);
    free(decoded.data);
    return success;
}

int pdf_contents_read(pdf_contents_context *context, const pdf_page *page,
                      pdf_contents_result *result, pdf_error *error) {
    if (result != NULL) *result = (pdf_contents_result){0};
    if (context == NULL || context->document == NULL || page == NULL ||
        result == NULL || error == NULL || context->document->xref == NULL) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "contents", "invalid contents context");
        return 0;
    }
    if (error->code != PDF_ERROR_NONE) return 0;
    const pdf_limits *limits = &context->document->reader.limits;
    size_t page_offset = pdf_document_reference_offset(context->document,
                                                       page->reference);
    if (context->total_decoded > limits->max_total_decoded_size) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, page_offset, "contents",
                      "total decoded contents exceed configured limit");
        return 0;
    }
    size_t remaining = limits->max_total_decoded_size - context->total_decoded;
    const pdf_object *contents = page->contents;
    if (contents == NULL) return 1;

    pdf_contents_result accumulated = {0};
    size_t capacity = 0;
    if (contents->type == PDF_OBJECT_REF) {
        pdf_reference reference = contents->value.reference;
        const pdf_indirect_object *indirect = pdf_resolve(context->document,
                                                           reference, error);
        if (indirect == NULL) {
            if (error->offset == 0) error->offset = page_offset;
            return 0;
        }
        if (indirect->is_stream) {
            if (!append_stream(context, reference, &accumulated, &capacity,
                               remaining, error)) goto fail;
            goto done;
        }
        contents = indirect->body;
        page_offset = pdf_document_reference_offset(context->document, reference);
    }
    if (contents == NULL || contents->type != PDF_OBJECT_ARRAY) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, page_offset, "contents",
                      "/Contents must be a stream reference or array");
        goto fail;
    }
    for (size_t i = 0; i < contents->value.array.len; i++) {
        const pdf_object *entry = contents->value.array.items[i];
        if (entry == NULL || entry->type != PDF_OBJECT_REF) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, page_offset, "contents",
                          "/Contents array members must be stream references");
            goto fail;
        }
        if (i != 0) {
            static const unsigned char newline = '\n';
            if (!append_bytes(&accumulated, &capacity, &newline, 1,
                              remaining, page_offset, error)) goto fail;
        }
        if (!append_stream(context, entry->value.reference, &accumulated,
                           &capacity, remaining, error)) goto fail;
    }
done:
    context->total_decoded += accumulated.len;
    *result = accumulated;
    return 1;
fail:
    pdf_contents_result_free(&accumulated);
    return 0;
}
