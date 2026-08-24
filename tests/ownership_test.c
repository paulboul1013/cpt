#include "../src/lexer.h"
#include "../src/object.h"
#include "../src/parser.h"
#include "../src/reader.h"

#include <assert.h>
#include <string.h>

int main(void) {
    pdf_reader reader = {0};
    pdf_lexer lexer;
    pdf_parser parser;
    pdf_error error;
    pdf_token source;
    pdf_token destination;
    pdf_object *array;
    pdf_object *dict;
    pdf_object *child;

    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/names.txt", &error));
    lexer_init(&lexer, &reader, &error);

    pdf_token_init(&source);
    source = lexer_next(&lexer);
    assert(source.type == PDF_TOKEN_NAME);
    assert(source.bytes.len == 4);
    assert(memcmp(source.bytes.data, "Type", 4) == 0);

    pdf_token_init(&destination);
    pdf_token_move(&destination, &source);
    assert(source.bytes.data == NULL);
    assert(destination.type == PDF_TOKEN_NAME);
    assert(destination.bytes.len == 4);
    assert(memcmp(destination.bytes.data, "Type", 4) == 0);
    pdf_token_destroy(&source);
    pdf_token_destroy(&destination);

    assert(reader_seek(&reader, 0));
    parser_init(&parser, &lexer, &error);
    const pdf_token *borrowed = parser_peek(&parser);
    assert(borrowed != NULL);
    assert(borrowed->type == PDF_TOKEN_NAME);
    assert(borrowed->bytes.len == 4);
    assert(memcmp(borrowed->bytes.data, "Type", 4) == 0);
    parser_destroy(&parser);
    reader_close(&reader);

    array = pdf_object_new_array();
    assert(array != NULL);
    child = pdf_object_new_int(123);
    assert(child != NULL);
    assert(pdf_array_push(array, child));
    pdf_object_free(array);

    dict = pdf_object_new_dict();
    assert(dict != NULL);
    child = pdf_object_new_int(456);
    assert(child != NULL);
    assert(pdf_dict_push(dict, "Value", child));
    pdf_object_free(dict);

    return 0;
}
