#include "../src/parser.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static pdf_indirect_object *parse_text(const char *input, pdf_error *error) {
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
    result = parser_parse_indirect_object(&parser);
    if (result != NULL) {
        assert(parser_expect_eof(&parser));
    }
    parser_destroy(&parser);
    reader_close(&reader);
    return result;
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

int main(void) {
    pdf_error error;
    pdf_error_init(&error);

    pdf_indirect_object *indirect = parse_text(
        "12 0 obj << /Type /Page /Parent 2 0 R >> endobj", &error);
    assert(indirect != NULL);
    assert(indirect->object_number == 12);
    assert(indirect->generation == 0);
    assert(indirect->body->type == PDF_OBJECT_DICT);
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
