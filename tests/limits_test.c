#include "../src/limits.h"
#include "../src/parser.h"
#include "../src/reader.h"

#include <assert.h>

static void assert_resource_error(const pdf_error *error) {
    assert(error->code == PDF_ERROR_RESOURCE_LIMIT);
    assert(error->module != NULL);
}

int main(void) {
    pdf_limits limits;
    pdf_error error;
    pdf_reader reader = {0};
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_token token;

    pdf_limits_default(&limits);
    assert(limits.max_input_size == 256U * 1024U * 1024U);
    assert(limits.max_token_size == 16U * 1024U * 1024U);
    assert(limits.max_nesting_depth == 256);
    assert(limits.max_container_entries == 1000000);
    assert(limits.max_xref_entries == 1000000);
    assert(limits.max_stream_size == 256U * 1024U * 1024U);
    assert(limits.max_decoded_stream_size == 256U * 1024U * 1024U);
    assert(limits.max_total_decoded_size == 256U * 1024U * 1024U);

    limits.max_input_size = 1;
    pdf_error_init(&error);
    assert(!reader_open_with_limits(&reader, "tests/numbers.txt", &error, &limits));
    assert_resource_error(&error);

    pdf_limits_default(&limits);
    limits.max_token_size = 4;
    pdf_error_clear(&error);
    assert(reader_open_with_limits(&reader, "tests/fixtures/long-name.txt", &error,
                                   &limits));
    lexer_init(&lexer, &reader, &error);
    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_INVALID);
    assert_resource_error(&error);
    pdf_token_destroy(&token);
    reader_close(&reader);

    pdf_limits_default(&limits);
    limits.max_nesting_depth = 2;
    pdf_error_clear(&error);
    assert(reader_open_with_limits(&reader, "tests/fixtures/deep-array.txt", &error,
                                   &limits));
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);
    assert(parser_parse_object(&parser) == NULL);
    assert_resource_error(&error);
    parser_destroy(&parser);
    reader_close(&reader);

    pdf_limits_default(&limits);
    limits.max_container_entries = 2;
    pdf_error_clear(&error);
    assert(reader_open_with_limits(&reader, "tests/fixtures/three-array.txt", &error,
                                   &limits));
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);
    assert(parser_parse_object(&parser) == NULL);
    assert_resource_error(&error);
    parser_destroy(&parser);
    reader_close(&reader);

    return 0;
}
