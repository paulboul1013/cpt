#include "../src/object.h"
#include "../src/parser.h"
#include "../src/reader.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static pdf_object *next_object(pdf_parser *parser) {
    pdf_object *object = parser_parse_object(parser);
    assert(object != NULL);
    return object;
}

int main(void) {
    pdf_reader reader = {0};
    pdf_error error;
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_object *object;

    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/fixtures/primitives.txt", &error));
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_NULL);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_BOOL);
    assert(object->value.boolean != 0);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_BOOL);
    assert(object->value.boolean == 0);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_INT);
    assert(object->value.integer == 123);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_INT);
    assert(object->value.integer == -42);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_REAL);
    assert(fabs(object->value.real - 1.5) < 0.000001);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_NAME);
    assert(object->value.name.len == 4);
    assert(memcmp(object->value.name.data, "Name", 4) == 0);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_STRING);
    assert(object->value.string.len == 2);
    assert(memcmp(object->value.string.data, "Hi", 2) == 0);
    pdf_object_free(object);

    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_HEX_STRING);
    assert(object->value.hex_string.len == 2);
    assert(memcmp(object->value.hex_string.data, "Hi", 2) == 0);
    pdf_object_free(object);

    assert(parser_expect_eof(&parser));
    assert(error.code == PDF_ERROR_NONE);
    parser_destroy(&parser);
    reader_close(&reader);

    pdf_error_clear(&error);
    assert(reader_open(&reader, "tests/fixtures/trailing-object.txt", &error));
    lexer_init(&lexer, &reader, &error);
    parser_init(&parser, &lexer, &error);
    object = next_object(&parser);
    assert(object->type == PDF_OBJECT_INT);
    pdf_object_free(object);
    assert(!parser_expect_eof(&parser));
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(error.offset == 4);
    parser_destroy(&parser);
    reader_close(&reader);

    return 0;
}
