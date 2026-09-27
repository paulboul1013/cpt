#include "xref.h"

#include "lexer.h"
#include "parser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pdf_reader *reader;
    pdf_error *error;
    size_t pos;
} xref_scan;

static int white(int c) {
    return c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

static void skip_white(xref_scan *scan) {
    while (scan->pos < scan->reader->size && white(scan->reader->data[scan->pos])) {
        scan->pos++;
    }
}

static int scan_uint(xref_scan *scan, size_t *value) {
    size_t result = 0;
    size_t start = scan->pos;
    while (scan->pos < scan->reader->size) {
        unsigned char c = scan->reader->data[scan->pos];
        if (c < '0' || c > '9') {
            break;
        }
        if (result > (SIZE_MAX - (size_t)(c - '0')) / 10) {
            pdf_error_set(scan->error, PDF_ERROR_RESOURCE_LIMIT, start, "xref",
                          "xref integer exceeds addressable size");
            return 0;
        }
        result = result * 10 + (size_t)(c - '0');
        scan->pos++;
    }
    if (scan->pos == start) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, start, "xref",
                      "expected unsigned integer");
        return 0;
    }
    *value = result;
    return 1;
}

static int scan_fixed_uint(xref_scan *scan, size_t digits, size_t *value) {
    size_t start = scan->pos;
    size_t result = 0;
    if (digits > scan->reader->size - scan->pos) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, start, "xref",
                      "truncated xref entry");
        return 0;
    }
    for (size_t i = 0; i < digits; i++) {
        unsigned char c = scan->reader->data[scan->pos++];
        if (c < '0' || c > '9') {
            pdf_error_set(scan->error, PDF_ERROR_MALFORMED, start, "xref",
                          "invalid xref entry field");
            return 0;
        }
        if (result > (SIZE_MAX - (size_t)(c - '0')) / 10) {
            pdf_error_set(scan->error, PDF_ERROR_RESOURCE_LIMIT, start, "xref",
                          "xref entry field exceeds addressable size");
            return 0;
        }
        result = result * 10 + (size_t)(c - '0');
    }
    *value = result;
    return 1;
}

static int expect_byte(xref_scan *scan, unsigned char byte, const char *reason) {
    if (scan->pos >= scan->reader->size || scan->reader->data[scan->pos] != byte) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos, "xref", "%s", reason);
        return 0;
    }
    scan->pos++;
    return 1;
}

static int scan_eol(xref_scan *scan) {
    if (scan->pos < scan->reader->size && scan->reader->data[scan->pos] == '\r') {
        scan->pos++;
        if (scan->pos < scan->reader->size && scan->reader->data[scan->pos] == '\n') {
            scan->pos++;
        }
        return 1;
    }
    return expect_byte(scan, '\n', "xref entry must end with an EOL");
}

static size_t find_marker(const pdf_reader *reader, const char *marker, size_t before) {
    size_t length = strlen(marker);
    if (before < length) {
        return SIZE_MAX;
    }
    for (size_t pos = before - length + 1; pos-- > 0;) {
        if ((pos == 0 || reader->data[pos - 1] == '\n' ||
             reader->data[pos - 1] == '\r') &&
            memcmp(reader->data + pos, marker, length) == 0) {
            return pos;
        }
    }
    return SIZE_MAX;
}

static int find_footer(xref_scan *scan, size_t *xref_offset,
                       size_t *footer_offset) {
    pdf_reader *reader = scan->reader;
    size_t eof = find_marker(reader, "%%EOF", reader->size);
    if (eof == SIZE_MAX) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, reader->size, "xref",
                      "missing %%EOF marker");
        return 0;
    }
    int saw_startxref = 0;
    size_t last_eof = eof;
    size_t marker = find_marker(reader, "startxref", eof);
    while (eof != SIZE_MAX) {
        if (marker != SIZE_MAX && marker >= eof) {
            marker = find_marker(reader, "startxref", eof);
        }
        if (marker != SIZE_MAX) {
            saw_startxref = 1;
            size_t pos = marker + strlen("startxref");
            size_t value = 0;
            int overflow = 0;
            if (pos < eof && white(reader->data[pos])) {
                while (pos < eof && white(reader->data[pos])) {
                    pos++;
                }
                size_t digits = pos;
                while (pos < eof && reader->data[pos] >= '0' &&
                       reader->data[pos] <= '9') {
                    size_t digit = (size_t)(reader->data[pos++] - '0');
                    if (value > (SIZE_MAX - digit) / 10) {
                        overflow = 1;
                        break;
                    }
                    value = value * 10 + digit;
                }
                if (pos > digits && !overflow) {
                    while (pos < eof && white(reader->data[pos])) {
                        pos++;
                    }
                    if (pos == eof && value < marker) {
                        *xref_offset = value;
                        *footer_offset = marker;
                        return 1;
                    }
                }
            }
        }
        eof = find_marker(reader, "%%EOF", eof);
    }
    pdf_error_set(scan->error, PDF_ERROR_MALFORMED, last_eof, "xref",
                  saw_startxref ? "invalid startxref offset or footer" :
                                   "missing startxref marker");
    return 0;
}

static int grow_entries(pdf_xref *xref, size_t *capacity, size_t needed,
                        size_t limit, pdf_error *error, size_t offset) {
    if (needed > limit) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, offset, "xref",
                      "xref entries exceed configured limit");
        return 0;
    }
    if (needed <= *capacity) {
        return 1;
    }
    size_t next = *capacity == 0 ? 16 : *capacity;
    while (next < needed) {
        if (next > limit / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    if (next > limit) {
        next = needed;
    }
    if (next > SIZE_MAX / sizeof(*xref->entries)) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, offset, "xref",
                      "xref entry allocation overflow");
        return 0;
    }
    pdf_xref_entry *entries = realloc(xref->entries, next * sizeof(*entries));
    if (entries == NULL) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, offset, "xref",
                      "could not allocate xref entries");
        return 0;
    }
    memset(entries + *capacity, 0, (next - *capacity) * sizeof(*entries));
    xref->entries = entries;
    *capacity = next;
    return 1;
}

static int read_entry(xref_scan *scan, pdf_xref_entry *entry) {
    size_t offset, generation;
    if (!scan_fixed_uint(scan, 10, &offset) ||
        !expect_byte(scan, ' ', "invalid xref entry separator") ||
        !scan_fixed_uint(scan, 5, &generation) ||
        !expect_byte(scan, ' ', "invalid xref entry separator")) {
        return 0;
    }
    if (generation > 65535 || scan->pos >= scan->reader->size) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos, "xref",
                      "invalid xref generation");
        return 0;
    }
    unsigned char flag = scan->reader->data[scan->pos++];
    if (flag != 'n' && flag != 'f') {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos - 1, "xref",
                      "xref entry must be in-use or free");
        return 0;
    }
    if (scan->pos < scan->reader->size && scan->reader->data[scan->pos] == ' ') {
        scan->pos++;
    }
    if (!scan_eol(scan)) {
        return 0;
    }
    entry->offset = offset;
    entry->generation = (uint16_t)generation;
    entry->in_use = flag == 'n';
    entry->present = 1;
    return 1;
}

static int validate_target(const pdf_reader *reader, const pdf_xref *xref,
                           size_t number, pdf_error *error) {
    const pdf_xref_entry *entry = &xref->entries[number];
    if (!entry->present || !entry->in_use) {
        return 1;
    }
    if (entry->offset >= xref->startxref) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, entry->offset, "xref",
                      "xref offset does not point to an indirect object");
        return 0;
    }
    xref_scan scan = {(pdf_reader *)reader, error, entry->offset};
    size_t found_number, found_generation;
    if (reader->data[scan.pos] < '0' || reader->data[scan.pos] > '9' ||
        !scan_uint(&scan, &found_number) || found_number != number ||
        scan.pos >= reader->size || !white(reader->data[scan.pos])) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, entry->offset, "xref",
                      "xref offset points to a different object number");
        return 0;
    }
    skip_white(&scan);
    if (!scan_uint(&scan, &found_generation) ||
        found_generation != entry->generation ||
        scan.pos >= reader->size || !white(reader->data[scan.pos])) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, entry->offset, "xref",
                      "xref offset points to a different generation");
        return 0;
    }
    skip_white(&scan);
    if (scan.pos + 3 > reader->size ||
        memcmp(reader->data + scan.pos, "obj", 3) != 0 ||
        (scan.pos + 3 < reader->size && !white(reader->data[scan.pos + 3]))) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, entry->offset, "xref",
                      "xref offset does not point to an obj header");
        return 0;
    }
    pdf_reader view = *reader;
    view.pos = scan.pos + 3;
    pdf_error probe_error;
    pdf_error_init(&probe_error);
    pdf_lexer lexer;
    pdf_parser parser;
    lexer_init(&lexer, &view, &probe_error);
    parser_init(&parser, &lexer, &probe_error);
    pdf_object *body = parser_parse_object(&parser);
    const pdf_object *type = pdf_dict_get(body, "Type");
    int object_stream = type != NULL && type->type == PDF_OBJECT_NAME &&
                        type->value.name.len == 6 &&
                        memcmp(type->value.name.data, "ObjStm", 6) == 0;
    pdf_object_free(body);
    parser_destroy(&parser);
    if (object_stream) {
        pdf_error_set(error, PDF_ERROR_UNSUPPORTED, entry->offset, "xref",
                      "object streams are unsupported");
        return 0;
    }
    return 1;
}

static int read_trailer(xref_scan *scan, pdf_xref *xref, size_t footer_offset,
                        size_t highest) {
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_token token;
    pdf_token_init(&token);
    if (!reader_seek(scan->reader, scan->pos)) {
        return 0;
    }
    lexer_init(&lexer, scan->reader, scan->error);
    parser_init(&parser, &lexer, scan->error);
    xref->trailer = parser_parse_object(&parser);
    if (xref->trailer == NULL) {
        goto fail;
    }
    if (xref->trailer->type != PDF_OBJECT_DICT) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos, "xref",
                      "trailer must be a dictionary");
        goto fail;
    }
    const pdf_object *size = pdf_dict_get(xref->trailer, "Size");
    const pdf_object *root = pdf_dict_get(xref->trailer, "Root");
    if (pdf_dict_get(xref->trailer, "Prev") != NULL ||
        pdf_dict_get(xref->trailer, "Encrypt") != NULL) {
        pdf_error_set(scan->error, PDF_ERROR_UNSUPPORTED, scan->pos, "xref",
                      "incremental updates and encrypted PDFs are unsupported");
        goto fail;
    }
    if (size == NULL || size->type != PDF_OBJECT_INT || size->value.integer <= 0 ||
        (uintmax_t)size->value.integer != (uintmax_t)highest) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos, "xref",
                      "trailer /Size does not match xref entries");
        goto fail;
    }
    xref->size = (size_t)size->value.integer;
    if (root == NULL || root->type != PDF_OBJECT_REF ||
        root->value.reference.object_number <= 0 ||
        (uintmax_t)root->value.reference.object_number >= (uintmax_t)xref->size) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos, "xref",
                      "trailer /Root must reference an in-use object");
        goto fail;
    }
    xref->root = root->value.reference;
    const pdf_xref_entry *root_entry = &xref->entries[xref->root.object_number];
    if (!root_entry->present || !root_entry->in_use ||
        root_entry->generation != xref->root.generation) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, scan->pos, "xref",
                      "trailer /Root must reference an in-use object");
        goto fail;
    }
    if (!parser_next(&parser, &token) || token.type != PDF_TOKEN_KEYWORD ||
        token.text == NULL || strcmp(token.text, "startxref") != 0 ||
        token.offset != footer_offset) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, footer_offset, "xref",
                      "expected startxref after trailer");
        goto fail;
    }
    if (!parser_next(&parser, &token) || token.type != PDF_TOKEN_INT ||
        token.integer < 0 || (uintmax_t)token.integer != (uintmax_t)xref->startxref) {
        pdf_error_set(scan->error, PDF_ERROR_MALFORMED, footer_offset, "xref",
                      "startxref does not match xref table offset");
        goto fail;
    }
    pdf_token_destroy(&token);
    parser_destroy(&parser);
    return 1;

fail:
    pdf_token_destroy(&token);
    parser_destroy(&parser);
    return 0;
}

pdf_xref *pdf_xref_parse(pdf_reader *reader, pdf_error *error) {
    if (reader == NULL || error == NULL) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "xref", "reader or error is null");
        return NULL;
    }
    size_t saved = reader_tell(reader);
    if (!reader_validate_pdf_header(reader, error)) {
        return NULL;
    }
    xref_scan scan = {reader, error, 0};
    size_t xref_offset, footer_offset;
    if (!find_footer(&scan, &xref_offset, &footer_offset)) {
        return NULL;
    }
    pdf_xref *xref = calloc(1, sizeof(*xref));
    if (xref == NULL) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, xref_offset, "xref",
                      "could not allocate xref table");
        return NULL;
    }
    xref->startxref = xref_offset;
    scan.pos = xref_offset;
    if (scan.pos + 4 > reader->size ||
        memcmp(reader->data + scan.pos, "xref", 4) != 0 ||
        (scan.pos + 4 < reader->size && !white(reader->data[scan.pos + 4]))) {
        int indirect = scan.pos < reader->size &&
                       reader->data[scan.pos] >= '0' && reader->data[scan.pos] <= '9';
        pdf_error_set(error, indirect ? PDF_ERROR_UNSUPPORTED : PDF_ERROR_MALFORMED,
                      xref_offset, "xref",
                      indirect ? "xref streams are unsupported" :
                                 "startxref does not point to an xref table");
        goto fail;
    }
    scan.pos += 4;
    size_t capacity = 0;
    size_t highest = 0;
    size_t limit = reader->limits.max_xref_entries;
    skip_white(&scan);
    while (scan.pos < reader->size && reader->data[scan.pos] >= '0' &&
           reader->data[scan.pos] <= '9') {
        size_t first, count;
        size_t subsection_offset = scan.pos;
        if (!scan_uint(&scan, &first) || scan.pos >= reader->size ||
            !white(reader->data[scan.pos])) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, subsection_offset, "xref",
                          "invalid xref subsection header");
            goto fail;
        }
        skip_white(&scan);
        if (!scan_uint(&scan, &count) || count == 0 ||
            scan.pos >= reader->size || !white(reader->data[scan.pos])) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, subsection_offset, "xref",
                          "invalid xref subsection count");
            goto fail;
        }
        skip_white(&scan);
        if (first >= limit || count > limit - first ||
            !grow_entries(xref, &capacity, first + count, limit, error,
                          subsection_offset)) {
            pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, subsection_offset,
                          "xref", "xref entries exceed configured limit");
            goto fail;
        }
        if (first + count > highest) {
            highest = first + count;
        }
        for (size_t i = 0; i < count; i++) {
            if (xref->entries[first + i].present) {
                pdf_error_set(error, PDF_ERROR_MALFORMED, scan.pos, "xref",
                              "duplicate xref entry");
                goto fail;
            }
            if (!read_entry(&scan, &xref->entries[first + i])) {
                goto fail;
            }
        }
        skip_white(&scan);
    }
    if (highest == 0 || capacity == 0 || !xref->entries[0].present ||
        xref->entries[0].in_use || xref->entries[0].generation != 65535) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, scan.pos, "xref",
                      "xref must contain free object zero");
        goto fail;
    }
    if (scan.pos + 7 > reader->size ||
        memcmp(reader->data + scan.pos, "trailer", 7) != 0 ||
        (scan.pos + 7 < reader->size && !white(reader->data[scan.pos + 7]))) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, scan.pos, "xref",
                      "expected trailer dictionary");
        goto fail;
    }
    scan.pos += 7;
    skip_white(&scan);
    if (!read_trailer(&scan, xref, footer_offset, highest)) {
        goto fail;
    }
    for (size_t i = 0; i < xref->size; i++) {
        if (xref->entries[i].present && !xref->entries[i].in_use &&
            xref->entries[i].offset >= xref->size) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, xref_offset, "xref",
                          "free entry points outside xref table");
            goto fail;
        }
        if (!validate_target(reader, xref, i, error)) {
            goto fail;
        }
    }
    (void)reader_seek(reader, saved);
    return xref;

fail:
    pdf_xref_free(xref);
    (void)reader_seek(reader, saved);
    return NULL;
}

const pdf_xref_entry *pdf_xref_get(const pdf_xref *xref, size_t number) {
    if (xref == NULL || number >= xref->size) {
        return NULL;
    }
    return &xref->entries[number];
}

void pdf_xref_free(pdf_xref *xref) {
    if (xref == NULL) {
        return;
    }
    pdf_object_free(xref->trailer);
    free(xref->entries);
    free(xref);
}
