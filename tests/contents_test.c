#include "../src/contents.h"
#include "../src/filter.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_empty_pages_and_zero_stream(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/pages-multi.pdf", NULL, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context context;
    pdf_contents_context_init(&context, &document);
    for (size_t i = 0; i < pages.len; i++) {
        pdf_contents_result result = {0};
        assert(pdf_contents_read(&context, &pages.items[i], &result, &error));
        assert(result.len == 0 && result.data == NULL);
        assert(context.total_decoded == 0);
        pdf_contents_result_free(&result);
    }
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void test_flate_hello_and_reader_cursor(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/hello.pdf", NULL, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    assert(pages.len == 1);
    pdf_contents_context context;
    pdf_contents_context_init(&context, &document);
    pdf_contents_result result = {0};
    size_t position = reader_tell(&document.reader);
    assert(pdf_contents_read(&context, &pages.items[0], &result, &error));
    assert(reader_tell(&document.reader) == position);
    assert(result.len == 288 && context.total_decoded == 288);
    assert(memcmp(result.data, "0.1 w\n/Artifact BMC\n", 20) == 0);
    pdf_contents_result_free(&result);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void test_raw_arrays_ownership_and_budget(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/contents-basic.pdf", NULL,
                             &error));
    assert(pdf_pages_load(&document, &pages, &error));
    assert(pages.len == 3);
    static const unsigned char first[] = {'q', 0, 'Q'};
    static const unsigned char second[] = "q\n\nQ";
    static const unsigned char third[] = "BT\nET";
    const unsigned char *expected[] = {first, second, third};
    const size_t lengths[] = {sizeof(first), sizeof(second) - 1,
                              sizeof(third) - 1};
    pdf_contents_context context;
    pdf_contents_context_init(&context, &document);
    for (size_t i = 0; i < pages.len; i++) {
        pdf_contents_result result = {0};
        size_t position = reader_tell(&document.reader);
        assert(pdf_contents_read(&context, &pages.items[i], &result, &error));
        assert(result.len == lengths[i]);
        assert(memcmp(result.data, expected[i], lengths[i]) == 0);
        assert(reader_tell(&document.reader) == position);
        if (i == 0) {
            pdf_reference reference = pages.items[i].contents->value.reference;
            const pdf_indirect_object *raw = pdf_resolve(&document, reference, &error);
            assert(result.data != raw->stream.data);
        }
        pdf_contents_result_free(&result);
        assert(result.data == NULL && result.len == 0);
    }
    assert(context.total_decoded == 12);
    pdf_pages_free(&pages);
    pdf_document_close(&document);

    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 7;
    pdf_error_clear(&error);
    assert(pdf_document_open(&document, "tests/fixtures/contents-basic.pdf",
                             &limits, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context_init(&context, &document);
    for (size_t i = 0; i < 2; i++) {
        pdf_contents_result result = {0};
        assert(pdf_contents_read(&context, &pages.items[i], &result, &error));
        pdf_contents_result_free(&result);
    }
    assert(context.total_decoded == 7);
    pdf_contents_result result = {0};
    assert(!pdf_contents_read(&context, &pages.items[2], &result, &error));
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    assert(result.data == NULL && result.len == 0 && context.total_decoded == 7);
    pdf_pages_free(&pages);
    pdf_document_close(&document);

    pdf_limits_default(&limits);
    limits.max_decoded_stream_size = 2;
    pdf_error_clear(&error);
    assert(pdf_document_open(&document, "tests/fixtures/contents-basic.pdf",
                             &limits, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context_init(&context, &document);
    assert(!pdf_contents_read(&context, &pages.items[0], &result, &error));
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    assert(result.data == NULL && context.total_decoded == 0);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void expect_contents_error(const char *path, pdf_error_code code) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, path, NULL, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context context;
    pdf_contents_context_init(&context, &document);
    pdf_contents_result result = {0};
    size_t target = strcmp(path, "tests/fixtures/contents-invalid-type.pdf") == 0 ? 1 : 0;
    assert(!pdf_contents_read(&context, &pages.items[target], &result, &error));
    assert(error.code == code && error.offset > 0 && error.module != NULL);
    assert(result.data == NULL && result.len == 0 && context.total_decoded == 0);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void test_malformed_and_unsupported(void) {
    expect_contents_error("tests/fixtures/contents-invalid-type.pdf",
                          PDF_ERROR_MALFORMED);
    expect_contents_error("tests/fixtures/contents-not-stream.pdf",
                          PDF_ERROR_MALFORMED);
    expect_contents_error("tests/fixtures/contents-unsupported-filter.pdf",
                          PDF_ERROR_UNSUPPORTED);
    expect_contents_error("tests/fixtures/contents-filter-array.pdf",
                          PDF_ERROR_UNSUPPORTED);
    expect_contents_error("tests/fixtures/contents-corrupt-flate.pdf",
                          PDF_ERROR_MALFORMED);
    expect_contents_error("tests/fixtures/contents-truncated-flate.pdf",
                          PDF_ERROR_MALFORMED);
    expect_contents_error("tests/fixtures/contents-predictor.pdf",
                          PDF_ERROR_UNSUPPORTED);
}

static void test_flate_budget_and_invalid_array_members(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_decoded_stream_size = 3;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/contents-flate.pdf",
                             &limits, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context context;
    pdf_contents_context_init(&context, &document);
    pdf_contents_result result = {0};
    assert(!pdf_contents_read(&context, &pages.items[0], &result, &error));
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    assert(result.data == NULL && context.total_decoded == 0);
    pdf_pages_free(&pages);
    pdf_document_close(&document);

    pdf_error_clear(&error);
    assert(pdf_document_open(&document, "tests/fixtures/contents-predictor-one.pdf",
                             NULL, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context_init(&context, &document);
    assert(pdf_contents_read(&context, &pages.items[0], &result, &error));
    assert(result.len == 4 && memcmp(result.data, "q\nQ\n", 4) == 0);
    pdf_contents_result_free(&result);
    pdf_pages_free(&pages);
    pdf_document_close(&document);

    pdf_error_clear(&error);
    assert(pdf_document_open(&document, "tests/fixtures/contents-basic.pdf",
                             NULL, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    pdf_contents_context_init(&context, &document);
    pdf_page page = pages.items[0];
    pdf_object *array = pdf_object_new_array();
    assert(array != NULL);
    page.contents = array;
    assert(pdf_contents_read(&context, &page, &result, &error));
    assert(result.data == NULL && result.len == 0 && context.total_decoded == 0);
    pdf_object_free(array);

    array = pdf_object_new_array();
    assert(array != NULL);
    assert(pdf_array_push(array, pdf_object_new_int(1)));
    page.contents = array;
    assert(!pdf_contents_read(&context, &page, &result, &error));
    assert(error.code == PDF_ERROR_MALFORMED && context.total_decoded == 0);
    pdf_object_free(array);

    pdf_error_clear(&error);
    array = pdf_object_new_array();
    assert(array != NULL);
    assert(pdf_array_push(array, pdf_object_new_array()));
    page.contents = array;
    assert(!pdf_contents_read(&context, &page, &result, &error));
    assert(error.code == PDF_ERROR_MALFORMED && context.total_decoded == 0);
    pdf_object_free(array);

    pdf_error_clear(&error);
    pdf_object *missing = pdf_object_new_ref(999, 0);
    assert(missing != NULL);
    page.contents = missing;
    assert(!pdf_contents_read(&context, &page, &result, &error));
    assert(error.code == PDF_ERROR_MALFORMED && error.offset > 0);
    assert(context.total_decoded == 0 && result.data == NULL);
    pdf_object_free(missing);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void test_unfiltered_bytes_ignore_decode_parameters(void) {
    pdf_indirect_object stream = {0};
    stream.body = pdf_object_new_dict();
    assert(stream.body != NULL);
    pdf_object *parameters = pdf_object_new_dict();
    assert(parameters != NULL);
    assert(pdf_dict_push(parameters, "Predictor", pdf_object_new_int(12)));
    assert(pdf_dict_push(stream.body, "DecodeParms", parameters));
    stream.is_stream = 1;
    stream.stream.data = (unsigned char *)"q\0Q";
    stream.stream.len = 3;
    pdf_bytes output = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_filter_decode(&stream, 3, 42, &output, &error));
    assert(error.code == PDF_ERROR_NONE);
    assert(output.len == 3 && memcmp(output.data, "q\0Q", 3) == 0);
    free(output.data);
    pdf_object_free(stream.body);
}

int main(void) {
    test_empty_pages_and_zero_stream();
    test_flate_hello_and_reader_cursor();
    test_raw_arrays_ownership_and_budget();
    test_malformed_and_unsupported();
    test_flate_budget_and_invalid_array_members();
    test_unfiltered_bytes_ignore_decode_parameters();
    return 0;
}
