#include "../src/parser.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static pdf_indirect_object *parse_text_with_resolver(
    const char *input, pdf_error *error,
    pdf_stream_length_resolver resolver, void *context) {
    pdf_reader reader = {0};
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_indirect_object *result;

    reader.size = strlen(input);
    reader.data = malloc(reader.size == 0 ? 1 : reader.size);
    assert(reader.data != NULL);
    memcpy(reader.data, input, reader.size);
    pdf_limits_default(&reader.limits);

    lexer_init(&lexer, &reader, error);
    parser_init(&parser, &lexer, error);
    parser_set_length_resolver(&parser, resolver, context);
    result = parser_parse_indirect_object(&parser);
    if (result != NULL) {
        assert(parser_expect_eof(&parser));
    }
    parser_destroy(&parser);
    reader_close(&reader);
    return result;
}

static pdf_indirect_object *parse_text(const char *input, pdf_error *error) {
    return parse_text_with_resolver(input, error, NULL, NULL);
}

typedef struct {
    int64_t object_number;
    int64_t generation;
    int64_t value;
    int called;
    int succeeds;
} length_stub;

static int resolve_length(void *context, int64_t object_number,
                          int64_t generation, int64_t *value) {
    length_stub *stub = context;
    stub->called++;
    if (!stub->succeeds || stub->object_number != object_number ||
        stub->generation != generation) {
        return 0;
    }
    *value = stub->value;
    return 1;
}

static void test_reference_length_stream(void) {
    const char *input =
        "5 0 obj << /Length 7 2 R >> stream\nabcendstreamXYZ\nendstream endobj";
    length_stub stub = {7, 2, 15, 0, 1};
    pdf_error error;
    pdf_error_init(&error);
    pdf_indirect_object *indirect = parse_text_with_resolver(
        input, &error, resolve_length, &stub);
    assert(indirect != NULL);
    assert(stub.called == 1);
    assert(indirect->is_stream && indirect->stream.len == 15);
    assert(memcmp(indirect->stream.data, "abcendstreamXYZ", 15) == 0);
    assert(error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);

    pdf_error_clear(&error);
    stub.called = 0;
    indirect = parse_text_with_resolver(
        "5 0 obj << /Length 7 2 R >> stream\nendstream endobj",
        &error, resolve_length, &stub);
    assert(indirect == NULL);
    assert(stub.called == 1);
    assert(error.code == PDF_ERROR_MALFORMED);

    pdf_error_clear(&error);
    stub.called = 0;
    stub.value = 0;
    indirect = parse_text_with_resolver(
        "5 0 obj << /Length 7 2 R >> stream\nendstream endobj",
        &error, resolve_length, &stub);
    assert(indirect != NULL && indirect->is_stream && indirect->stream.len == 0);
    assert(stub.called == 1 && error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);

    pdf_error_clear(&error);
    stub.called = 0;
    indirect = parse_text_with_resolver(
        "5 0 obj << /Length 1 >> stream\nX\nendstream endobj",
        &error, resolve_length, &stub);
    assert(indirect != NULL && indirect->stream.len == 1);
    assert(stub.called == 0 && error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);
}

static void test_reference_length_errors(void) {
    const char *input =
        "5 0 obj << /Length 7 2 R >> stream\nX\nendstream endobj";
    size_t offset = (size_t)(strstr(input, "stream") - input);
    length_stub stub = {7, 2, 1, 0, 0};
    pdf_error error;
    pdf_error_init(&error);

    assert(parse_text_with_resolver(input, &error, resolve_length, &stub) == NULL);
    assert(stub.called == 1);
    assert(error.code == PDF_ERROR_MALFORMED && error.offset == offset);

    pdf_error_clear(&error);
    stub.succeeds = 1;
    stub.object_number = 8;
    assert(parse_text_with_resolver(input, &error, resolve_length, &stub) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED && error.offset == offset);

    pdf_error_clear(&error);
    stub.object_number = 7;
    stub.value = -1;
    assert(parse_text_with_resolver(input, &error, resolve_length, &stub) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED && error.offset == offset);

    pdf_error_clear(&error);
    stub.value = INT64_MAX;
    assert(parse_text_with_resolver(input, &error, resolve_length, &stub) == NULL);
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT && error.offset == offset);
}

static void assert_malformed(const char *input, size_t expected_offset) {
    pdf_error error;
    pdf_error_init(&error);

    assert(parse_text(input, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(error.offset == expected_offset);
    assert(strcmp(error.module, "parser") == 0);
    assert(error.message[0] != '\0');
}

static void test_direct_length_stream(void) {
    static const unsigned char input[] =
        "3 0 obj << /Length 14 >> stream\r\nA\0endstreamXYZ\nendstream\nendobj";
    static const unsigned char expected[] = "A\0endstreamXYZ";
    pdf_reader reader = {0};
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_error error;
    pdf_error_init(&error);
    reader.size = sizeof(input) - 1;
    reader.data = malloc(reader.size);
    assert(reader.data != NULL);
    memcpy(reader.data, input, reader.size);
    pdf_limits_default(&reader.limits);
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);

    pdf_indirect_object *indirect = parser_parse_indirect_object(&parser);
    assert(indirect != NULL);
    assert(indirect->is_stream);
    assert(indirect->body->type == PDF_OBJECT_DICT);
    assert(indirect->stream.len == sizeof(expected) - 1);
    assert(parser_expect_eof(&parser));
    assert(error.code == PDF_ERROR_NONE);

    parser_destroy(&parser);
    reader_close(&reader);
    assert(memcmp(indirect->stream.data, expected, indirect->stream.len) == 0);
    pdf_indirect_object_free(indirect);
}

static void assert_stream_error(const char *input, pdf_error_code code,
                                size_t offset, size_t max_stream_size) {
    pdf_reader reader = {0};
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_error error;
    pdf_error_init(&error);
    reader.size = strlen(input);
    reader.data = malloc(reader.size);
    assert(reader.data != NULL);
    memcpy(reader.data, input, reader.size);
    pdf_limits_default(&reader.limits);
    reader.limits.max_stream_size = max_stream_size;
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);
    assert(parser_parse_indirect_object(&parser) == NULL);
    assert(error.code == code);
    assert(error.offset == offset);
    assert(strcmp(error.module, "parser") == 0);
    parser_destroy(&parser);
    reader_close(&reader);
}

static void test_stream_errors(void) {
    const char *missing = "1 0 obj << >> stream\nX\nendstream endobj";
    const char *negative = "1 0 obj << /Length -1 >> stream\nX\nendstream endobj";
    const char *real = "1 0 obj << /Length 1.0 >> stream\nX\nendstream endobj";
    const char *reference = "1 0 obj << /Length 2 0 R >> stream\nX\nendstream endobj";
    const char *oversized = "1 0 obj << /Length 2 >> stream\nXY\nendstream endobj";
    const char *truncated = "1 0 obj << /Length 100 >> stream\nX";
    const char *bad_eol = "1 0 obj << /Length 1 >> stream\rX\nendstream endobj";
    const char *bad_marker = "1 0 obj << /Length 1 >> stream\nX\nnope endobj";
    const char *missing_marker = "1 0 obj << /Length 1 >> stream\nX";
    const char *bad_endobj = "1 0 obj << /Length 1 >> stream\nX\nendstream nope";
    const char *missing_endobj = "1 0 obj << /Length 1 >> stream\nX\nendstream";
    const char *extra_space = "1 0 obj << /Length 1 >> stream\nX  endstream endobj";
    assert_stream_error(missing, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(missing, "stream") - missing), SIZE_MAX);
    assert_stream_error(negative, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(negative, "stream") - negative), SIZE_MAX);
    assert_stream_error(real, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(real, "stream") - real), SIZE_MAX);
    assert_stream_error(reference, PDF_ERROR_UNSUPPORTED,
                        (size_t)(strstr(reference, "stream") - reference), SIZE_MAX);
    assert_stream_error(oversized, PDF_ERROR_RESOURCE_LIMIT,
                        (size_t)(strstr(oversized, "stream") - oversized), 1);
    assert_stream_error(truncated, PDF_ERROR_MALFORMED, strlen(truncated), SIZE_MAX);
    assert_stream_error(bad_eol, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(bad_eol, "stream") - bad_eol + 6), SIZE_MAX);
    assert_stream_error(bad_marker, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(bad_marker, "nope") - bad_marker), SIZE_MAX);
    assert_stream_error(missing_marker, PDF_ERROR_MALFORMED,
                        strlen(missing_marker), SIZE_MAX);
    assert_stream_error(bad_endobj, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(bad_endobj, "nope") - bad_endobj), SIZE_MAX);
    assert_stream_error(missing_endobj, PDF_ERROR_MALFORMED,
                        strlen(missing_endobj), SIZE_MAX);
    assert_stream_error(extra_space, PDF_ERROR_MALFORMED,
                        (size_t)(strstr(extra_space, "  endstream") - extra_space),
                        SIZE_MAX);
}

static void test_zero_length_stream(void) {
    pdf_error error;
    pdf_error_init(&error);
    pdf_indirect_object *indirect = parse_text(
        "4 0 obj << /Length 0 >> stream\nendstream endobj", &error);
    assert(indirect != NULL);
    assert(indirect->is_stream);
    assert(indirect->stream.len == 0);
    assert(indirect->stream.data == NULL);
    assert(error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);

    pdf_error_clear(&error);
    indirect = parse_text("4 0 obj << /Length 1 >> stream\nXendstream endobj", &error);
    assert(indirect != NULL && indirect->is_stream);
    assert(indirect->stream.len == 1 && indirect->stream.data[0] == 'X');
    assert(error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);
}

static void test_hello_pdf_stream(void) {
    static const size_t object_offset = 6768;
    static const unsigned char prefix[] =
        "11 0 obj\n<</Length 290/Filter/FlateDecode>>\nstream\n";
    pdf_reader reader = {0};
    pdf_error error;
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/hello.pdf", &error));
    assert(reader_validate_pdf_header(&reader, &error));
    assert(object_offset + sizeof(prefix) - 1 + 290 <= reader.size);
    assert(memcmp(reader.data + object_offset, prefix, sizeof(prefix) - 1) == 0);
    assert(reader_seek(&reader, object_offset));
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);
    pdf_indirect_object *indirect = parser_parse_indirect_object(&parser);
    assert(indirect != NULL);
    assert(indirect->object_number == 11 && indirect->is_stream);
    assert(indirect->stream.len == 290);
    assert(memcmp(indirect->stream.data,
                  reader.data + object_offset + sizeof(prefix) - 1, 290) == 0);
    assert(indirect->stream.data[0] == 0x78);
    assert(indirect->stream.data[1] == 0x9c);
    assert(error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);
    parser_destroy(&parser);
    reader_close(&reader);
}

typedef struct {
    pdf_reader *source;
    int calls;
} fixture_length_context;

/* Test-only offsets stand in for the xref table, which is not parsed yet. */
static int resolve_fixture_length(void *context, int64_t object_number,
                                  int64_t generation, int64_t *length) {
    fixture_length_context *fixture = context;
    size_t offset;
    fixture->calls++;
    if (generation != 0) {
        return 0;
    }
    if (object_number == 3) {
        offset = 270;
    } else if (object_number == 9) {
        offset = 6553;
    } else {
        return 0;
    }

    pdf_error error;
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_error_init(&error);
    assert(reader_seek(fixture->source, offset));
    lexer_init(&lexer, fixture->source, &error);
    parser_init(&parser, &lexer, &error);
    pdf_indirect_object *indirect = parser_parse_indirect_object(&parser);
    int valid = indirect != NULL && indirect->object_number == object_number &&
                indirect->generation == generation &&
                indirect->body->type == PDF_OBJECT_INT;
    if (valid) {
        *length = indirect->body->value.integer;
    }
    pdf_indirect_object_free(indirect);
    parser_destroy(&parser);
    return valid;
}

static void test_hello_pdf_reference_streams(void) {
    static const struct {
        size_t offset;
        int64_t object_number;
        size_t expected_length;
        const char *prefix;
    } cases[] = {
        {19, 2, 180, "2 0 obj\n<</Length 3 0 R/Filter/FlateDecode>>\nstream\n"},
        {290, 8, 6179,
         "8 0 obj\n<</Length 9 0 R/Filter/FlateDecode/Length1 9872>>\nstream\n"},
    };
    pdf_reader reader = {0};
    pdf_error error;
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/hello.pdf", &error));
    assert(reader_validate_pdf_header(&reader, &error));
    fixture_length_context fixture = {&reader, 0};

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t prefix_len = strlen(cases[i].prefix);
        assert(cases[i].offset + prefix_len + cases[i].expected_length <= reader.size);
        assert(memcmp(reader.data + cases[i].offset,
                      cases[i].prefix, prefix_len) == 0);
        assert(reader_seek(&reader, cases[i].offset));
        lexer_init(&lexer, &reader, &error);
        parser_init(&parser, &lexer, &error);
        parser_set_length_resolver(&parser, resolve_fixture_length, &fixture);
        pdf_indirect_object *indirect = parser_parse_indirect_object(&parser);
        assert(indirect != NULL && indirect->is_stream);
        assert(indirect->object_number == cases[i].object_number);
        assert(indirect->stream.len == cases[i].expected_length);
        assert(memcmp(indirect->stream.data,
                      reader.data + cases[i].offset + prefix_len,
                      cases[i].expected_length) == 0);
        assert(error.code == PDF_ERROR_NONE);
        pdf_indirect_object_free(indirect);
        parser_destroy(&parser);
    }
    assert(fixture.calls == 2);
    reader_close(&reader);
}

int main(void) {
    test_reference_length_stream();
    test_reference_length_errors();
    test_direct_length_stream();
    test_zero_length_stream();
    test_stream_errors();
    test_hello_pdf_stream();
    test_hello_pdf_reference_streams();
    pdf_error error;
    pdf_error_init(&error);

    pdf_indirect_object *indirect = parse_text(
        "12 0 obj << /Type /Page /Parent 2 0 R >> endobj", &error);
    assert(indirect != NULL);
    assert(indirect->object_number == 12);
    assert(indirect->generation == 0);
    assert(indirect->body->type == PDF_OBJECT_DICT);
    assert(!indirect->is_stream && indirect->stream.data == NULL);
    const pdf_object *parent = pdf_dict_get(indirect->body, "Parent");
    assert(parent != NULL && parent->type == PDF_OBJECT_REF);
    assert(parent->value.reference.object_number == 2);
    assert(error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);

    pdf_error_clear(&error);
    indirect = parse_text("5 0 obj 2 0 R endobj", &error);
    assert(indirect != NULL && indirect->body->type == PDF_OBJECT_REF);
    assert(indirect->body->value.reference.object_number == 2);
    assert(error.code == PDF_ERROR_NONE);
    pdf_indirect_object_free(indirect);

    assert_malformed("0 0 obj null endobj", 0);
    assert_malformed("-1 0 obj null endobj", 0);
    assert_malformed("1 -1 obj null endobj", 2);
    assert_malformed("1 65536 obj null endobj", 2);
    assert_malformed("1 obj null endobj", 2);
    assert_malformed("1 0 R null endobj", 4);
    assert_malformed("1 0 obj endobj", 8);
    assert_malformed("1 0 obj null", 12);
    assert_malformed("1 0 obj null nope", 13);
    assert_malformed("1 0 obj [1 2 endobj", 13);

    pdf_reader reader = {0};
    pdf_lexer lexer;
    pdf_parser parser;
    const char *sequence = "2 3 obj (Hi) endobj 7 0 obj [1 2] endobj";
    reader.size = strlen(sequence);
    reader.data = malloc(reader.size);
    assert(reader.data != NULL);
    memcpy(reader.data, sequence, reader.size);
    pdf_limits_default(&reader.limits);
    pdf_error_clear(&error);
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);

    indirect = parser_parse_indirect_object(&parser);
    assert(indirect != NULL && indirect->object_number == 2);
    assert(indirect->generation == 3);
    assert(indirect->body->type == PDF_OBJECT_STRING);
    pdf_indirect_object_free(indirect);

    indirect = parser_parse_indirect_object(&parser);
    assert(indirect != NULL && indirect->object_number == 7);
    assert(indirect->body->type == PDF_OBJECT_ARRAY);
    assert(indirect->body->value.array.len == 2);
    pdf_indirect_object_free(indirect);
    assert(parser_expect_eof(&parser));
    assert(error.code == PDF_ERROR_NONE);
    parser_destroy(&parser);
    reader_close(&reader);

    return 0;
}
