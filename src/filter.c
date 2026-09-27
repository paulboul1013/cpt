#include "filter.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static int name_is(const pdf_object *object, const char *name) {
    size_t len = strlen(name);
    return object != NULL && object->type == PDF_OBJECT_NAME &&
           object->value.name.len == len &&
           memcmp(object->value.name.data, name, len) == 0;
}

static int check_decode_parms(const pdf_object *parms, size_t offset,
                              pdf_error *error) {
    if (parms == NULL || parms->type == PDF_OBJECT_NULL) {
        return 1;
    }
    if (parms->type != PDF_OBJECT_DICT) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "filter",
                      "/DecodeParms must be a dictionary or null");
        return 0;
    }
    for (size_t i = 0; i < parms->value.dict.len; i++) {
        const pdf_dict_entry *entry = &parms->value.dict.entries[i];
        if (entry->key.len != 9 ||
            memcmp(entry->key.data, "Predictor", 9) != 0) {
            pdf_error_set(error, PDF_ERROR_UNSUPPORTED, offset, "filter",
                          "unsupported /DecodeParms parameter");
            return 0;
        }
        if (entry->value->type != PDF_OBJECT_INT) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "filter",
                          "/Predictor must be an integer");
            return 0;
        }
        if (entry->value->value.integer != 1) {
            pdf_error_set(error, PDF_ERROR_UNSUPPORTED, offset, "filter",
                          "unsupported /Predictor value");
            return 0;
        }
    }
    return 1;
}

static int append_bytes(pdf_bytes *output, size_t *capacity,
                        const unsigned char *data, size_t len,
                        size_t max_output, size_t offset, pdf_error *error) {
    if (len > SIZE_MAX - output->len || output->len + len > max_output) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, offset, "filter",
                      "decoded stream exceeds configured size limit");
        return 0;
    }
    size_t needed = output->len + len;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 8192 : *capacity;
        if (next > max_output) next = max_output;
        while (next < needed) {
            if (next > max_output / 2) {
                next = max_output;
                break;
            }
            next *= 2;
        }
        unsigned char *grown = realloc(output->data, next);
        if (grown == NULL) {
            pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, offset, "filter",
                          "could not allocate decoded stream");
            return 0;
        }
        output->data = grown;
        *capacity = next;
    }
    if (len != 0) {
        memcpy(output->data + output->len, data, len);
    }
    output->len = needed;
    return 1;
}

static int inflate_stream(const pdf_bytes *input, size_t max_output,
                          size_t offset, pdf_bytes *output, pdf_error *error) {
    z_stream z = {0};
    if (inflateInit(&z) != Z_OK) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, offset, "filter",
                      "could not initialize FlateDecode");
        return 0;
    }
    size_t consumed = 0;
    size_t capacity = 0;
    int success = 0;
    for (;;) {
        if (z.avail_in == 0 && consumed < input->len) {
            size_t chunk = input->len - consumed;
            if (chunk > UINT_MAX) chunk = UINT_MAX;
            z.next_in = input->data + consumed;
            z.avail_in = (uInt)chunk;
            consumed += chunk;
        }
        unsigned char chunk[8192];
        z.next_out = chunk;
        z.avail_out = sizeof(chunk);
        uInt before_input = z.avail_in;
        int status = inflate(&z, Z_NO_FLUSH);
        size_t produced = sizeof(chunk) - z.avail_out;
        if (!append_bytes(output, &capacity, chunk, produced,
                          max_output, offset, error)) break;
        if (status == Z_STREAM_END) {
            if (z.avail_in != 0 || consumed != input->len) {
                pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "filter",
                              "trailing bytes after FlateDecode stream");
            } else {
                success = 1;
            }
            break;
        }
        if (status != Z_OK ||
            (produced == 0 && z.avail_in == before_input) ||
            (produced == 0 && z.avail_in == 0 && consumed == input->len)) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "filter",
                          "invalid or truncated FlateDecode stream");
            break;
        }
    }
    (void)inflateEnd(&z);
    if (!success) {
        free(output->data);
        *output = (pdf_bytes){0};
    }
    return success;
}

int pdf_filter_decode(const pdf_indirect_object *stream, size_t max_output,
                      size_t offset, pdf_bytes *output, pdf_error *error) {
    if (output != NULL) *output = (pdf_bytes){0};
    if (stream == NULL || !stream->is_stream || stream->body == NULL ||
        stream->body->type != PDF_OBJECT_DICT || output == NULL || error == NULL) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "filter",
                      "Contents target is not a stream");
        return 0;
    }
    const pdf_object *filter = pdf_dict_get(stream->body, "Filter");
    const pdf_object *parms = pdf_dict_get(stream->body, "DecodeParms");
    if (filter == NULL) {
        if (stream->stream.len > max_output) {
            pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, offset, "filter",
                          "decoded stream exceeds configured size limit");
            return 0;
        }
        if (stream->stream.len != 0) {
            output->data = malloc(stream->stream.len);
            if (output->data == NULL) {
                pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, offset, "filter",
                              "could not copy raw stream");
                return 0;
            }
            memcpy(output->data, stream->stream.data, stream->stream.len);
            output->len = stream->stream.len;
        }
        return 1;
    }
    if (filter->type == PDF_OBJECT_ARRAY) {
        pdf_error_set(error, PDF_ERROR_UNSUPPORTED, offset, "filter",
                      "filter arrays and chains are unsupported");
        return 0;
    }
    if (filter->type != PDF_OBJECT_NAME) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "filter",
                      "/Filter must be a name");
        return 0;
    }
    if (!name_is(filter, "FlateDecode")) {
        pdf_error_set(error, PDF_ERROR_UNSUPPORTED, offset, "filter",
                      "unsupported stream filter");
        return 0;
    }
    if (!check_decode_parms(parms, offset, error)) return 0;
    return inflate_stream(&stream->stream, max_output, offset, output, error);
}
