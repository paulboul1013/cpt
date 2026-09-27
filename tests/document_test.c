#include "../src/document.h"

#include <assert.h>
#include <string.h>

static void test_open_and_resolve(void) {
    pdf_document document;
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/document-length.pdf", NULL,
                             &error));
    assert(pdf_document_root(&document).object_number == 1);
    assert(reader_seek(&document.reader, 7));
    pdf_reference stream_ref = {4, 0};
    const pdf_indirect_object *stream = pdf_resolve(&document, stream_ref, &error);
    assert(stream != NULL && stream->is_stream);
    assert(stream->stream.len == 3 && memcmp(stream->stream.data, "abc", 3) == 0);
    assert(reader_tell(&document.reader) == 7);
    assert(pdf_resolve(&document, stream_ref, &error) == stream);
    assert(document.cache_count == 2);
    assert(error.code == PDF_ERROR_NONE);
    pdf_document_close(&document);
    pdf_document_close(&document);
}

static void test_invalid_reference_and_offset(void) {
    pdf_document document;
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/document-length.pdf", NULL,
                             &error));
    pdf_reference zero = {0, 0};
    assert(pdf_resolve(&document, zero, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_error_clear(&error);
    pdf_reference missing = {99, 0};
    assert(pdf_resolve(&document, missing, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_error_clear(&error);
    pdf_reference wrong_gen = {1, 1};
    assert(pdf_resolve(&document, wrong_gen, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_error_clear(&error);
    pdf_reference valid = {1, 0};
    document.xref->entries[1].in_use = 0;
    assert(pdf_resolve(&document, valid, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    document.xref->entries[1].in_use = 1;
    pdf_error_clear(&error);
    document.xref->entries[1].offset = document.xref->startxref;
    assert(pdf_resolve(&document, valid, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_document_close(&document);

    pdf_error_clear(&error);
    assert(pdf_document_open(&document, "tests/fixtures/document-length.pdf", NULL,
                             &error));
    document.xref->entries[1].offset = document.xref->entries[2].offset;
    assert(pdf_resolve(&document, valid, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(strstr(error.message, "different indirect object") != NULL);
    pdf_document_close(&document);
}

static void test_cycle_and_failed_cache(void) {
    pdf_document document;
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/document-length-cycle.pdf",
                             NULL, &error));
    pdf_reference ref = {4, 0};
    assert(pdf_resolve(&document, ref, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(strstr(error.message, "cycle") != NULL);
    pdf_error_clear(&error);
    assert(pdf_resolve(&document, ref, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(strstr(error.message, "previously failed") != NULL);
    pdf_document_close(&document);
}

static void test_limits_and_open_failures(void) {
    pdf_document document;
    pdf_error error;
    pdf_limits limits;
    pdf_limits_default(&limits);
    pdf_error_init(&error);
    assert(!pdf_document_open(&document, "tests/numbers.txt", NULL, &error));
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_error_clear(&error);
    assert(!pdf_document_open(&document, "tests/fixtures/xref-bad-offset.pdf",
                              NULL, &error));
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_error_clear(&error);
    limits.max_object_cache = 1;
    assert(pdf_document_open(&document, "tests/fixtures/document-length.pdf",
                             &limits, &error));
    pdf_reference stream = {4, 0};
    assert(pdf_resolve(&document, stream, &error) == NULL);
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_document_close(&document);
    pdf_error_clear(&error);
    pdf_limits_default(&limits);
    limits.max_nesting_depth = 1;
    assert(pdf_document_open(&document, "tests/fixtures/document-length.pdf",
                             &limits, &error));
    assert(pdf_resolve(&document, stream, &error) == NULL);
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_document_close(&document);
}

int main(void) {
    test_open_and_resolve();
    test_invalid_reference_and_offset();
    test_cycle_and_failed_cache();
    test_limits_and_open_failures();
    return 0;
}
