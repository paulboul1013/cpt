#include "document.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CACHE_UNLOADED,
    CACHE_LOADING,
    CACHE_READY,
    CACHE_FAILED
} cache_state;

struct pdf_cache_entry {
    cache_state state;
    pdf_indirect_object *object;
};

void pdf_document_close(pdf_document *document) {
    if (document == NULL) {
        return;
    }
    if (document->cache != NULL && document->xref != NULL) {
        for (size_t i = 0; i < document->xref->size; i++) {
            pdf_indirect_object_free(document->cache[i].object);
        }
    }
    free(document->cache);
    pdf_xref_free(document->xref);
    reader_close(&document->reader);
    memset(document, 0, sizeof(*document));
}

int pdf_document_open(pdf_document *document, const char *filename,
                      const pdf_limits *limits, pdf_error *error) {
    if (document == NULL || error == NULL) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "document", "invalid document or error");
        return 0;
    }
    memset(document, 0, sizeof(*document));
    if (!reader_open_with_limits(&document->reader, filename, error, limits) ||
        !reader_validate_pdf_header(&document->reader, error)) {
        pdf_document_close(document);
        return 0;
    }
    document->xref = pdf_xref_parse(&document->reader, error);
    if (document->xref == NULL) {
        pdf_document_close(document);
        return 0;
    }
    if (document->xref->size > SIZE_MAX / sizeof(*document->cache)) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, 0, "document",
                      "object cache size overflows");
        pdf_document_close(document);
        return 0;
    }
    document->cache = calloc(document->xref->size, sizeof(*document->cache));
    if (document->cache == NULL) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, 0, "document",
                      "could not allocate object cache");
        pdf_document_close(document);
        return 0;
    }
    return 1;
}

pdf_reference pdf_document_root(const pdf_document *document) {
    pdf_reference empty = {0, 0};
    return document != NULL && document->xref != NULL ? document->xref->root : empty;
}

size_t pdf_document_reference_offset(const pdf_document *document,
                                     pdf_reference reference) {
    if (document == NULL || document->xref == NULL ||
        reference.object_number <= 0 ||
        (uintmax_t)reference.object_number >= (uintmax_t)document->xref->size ||
        reference.generation < 0 || reference.generation > 65535) {
        return 0;
    }
    const pdf_xref_entry *entry = pdf_xref_get(document->xref,
                                              (size_t)reference.object_number);
    return entry != NULL && entry->present && entry->in_use &&
           entry->generation == reference.generation ? entry->offset : 0;
}

typedef struct {
    pdf_document *document;
    pdf_error *error;
} length_context;

static int resolve_length(void *context, int64_t number, int64_t generation,
                          int64_t *length) {
    length_context *length_ctx = context;
    pdf_reference reference = {number, generation};
    const pdf_indirect_object *object = pdf_resolve(length_ctx->document, reference,
                                                    length_ctx->error);
    if (object == NULL) {
        return 0;
    }
    if (object->is_stream || object->body == NULL ||
        object->body->type != PDF_OBJECT_INT || object->body->value.integer < 0) {
        pdf_error_set(length_ctx->error, PDF_ERROR_MALFORMED,
                      reader_tell(&length_ctx->document->reader), "document",
                      "indirect stream length is not a non-negative integer");
        return 0;
    }
    *length = object->body->value.integer;
    return 1;
}

const pdf_indirect_object *pdf_resolve(pdf_document *document,
                                       pdf_reference reference, pdf_error *error) {
    if (document == NULL || document->xref == NULL || error == NULL) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "document", "document is not open");
        return NULL;
    }
    if (reference.object_number <= 0 ||
        (uintmax_t)reference.object_number >= (uintmax_t)document->xref->size ||
        reference.generation < 0 || reference.generation > 65535) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, 0, "document",
                      "invalid indirect object reference");
        return NULL;
    }
    size_t number = (size_t)reference.object_number;
    const pdf_xref_entry *xref_entry = pdf_xref_get(document->xref, number);
    if (xref_entry == NULL || !xref_entry->present || !xref_entry->in_use ||
        xref_entry->generation != reference.generation) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, 0, "document",
                      "indirect object is free, missing, or has a different generation");
        return NULL;
    }
    pdf_cache_entry *entry = &document->cache[number];
    if (entry->state == CACHE_READY) {
        return entry->object;
    }
    if (entry->state == CACHE_LOADING) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, xref_entry->offset, "document",
                      "indirect object cycle");
        return NULL;
    }
    if (entry->state == CACHE_FAILED) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, xref_entry->offset, "document",
                      "indirect object previously failed to resolve");
        return NULL;
    }
    if (document->cache_count >= document->reader.limits.max_object_cache ||
        document->resolve_depth >= document->reader.limits.max_nesting_depth) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, xref_entry->offset, "document",
                      "object cache or resolve depth exceeds configured limit");
        return NULL;
    }
    size_t saved = reader_tell(&document->reader);
    entry->state = CACHE_LOADING;
    document->cache_count++;
    document->resolve_depth++;
    pdf_indirect_object *object = NULL;
    if (xref_entry->offset >= document->xref->startxref ||
        !reader_seek(&document->reader, xref_entry->offset)) {
        pdf_error_set(error, PDF_ERROR_MALFORMED, xref_entry->offset, "document",
                      "xref offset is outside indirect object section");
    } else {
        pdf_lexer lexer;
        pdf_parser parser;
        length_context context = {document, error};
        lexer_init(&lexer, &document->reader, error);
        parser_init(&parser, &lexer, error);
        parser_set_length_resolver(&parser, resolve_length, &context);
        object = parser_parse_indirect_object(&parser);
        parser_destroy(&parser);
        if (object != NULL &&
            (object->object_number != reference.object_number ||
             object->generation != reference.generation)) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, xref_entry->offset, "document",
                          "xref offset points to a different indirect object");
            pdf_indirect_object_free(object);
            object = NULL;
        }
    }
    (void)reader_seek(&document->reader, saved);
    document->resolve_depth--;
    if (object == NULL || error->code != PDF_ERROR_NONE) {
        pdf_indirect_object_free(object);
        entry->state = CACHE_FAILED;
        return NULL;
    }
    entry->object = object;
    entry->state = CACHE_READY;
    return object;
}
