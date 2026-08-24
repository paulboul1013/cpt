#include "../src/lexer.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void assert_token(pdf_lexer *lexer, pdf_token_type type, size_t offset) {
    pdf_token token = lexer_next(lexer);

    assert(token.type == type);
    assert(token.offset == offset);
    pdf_token_destroy(&token);
}

int main(void) {
    pdf_reader reader = {0};
    pdf_error error;
    pdf_lexer lexer;
    pdf_token token;

    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/fixtures/token-values.txt", &error));
    lexer_init(&lexer, &reader, &error);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_INT);
    assert(token.offset == 22);
    assert(token.integer == 88);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_INT);
    assert(token.offset == 26);
    assert(token.integer == -42);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_REAL);
    assert(token.offset == 30);
    assert(fabs(token.real - 0.5) < 0.000001);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_REAL);
    assert(token.offset == 33);
    assert(fabs(token.real - 1.0) < 0.000001);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_REAL);
    assert(token.offset == 36);
    assert(fabs(token.real - 3.14) < 0.000001);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_REAL);
    assert(token.offset == 41);
    assert(fabs(token.real + 0.5) < 0.000001);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_BOOL);
    assert(token.offset == 46);
    assert(token.boolean != 0);
    pdf_token_destroy(&token);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_BOOL);
    assert(token.offset == 51);
    assert(token.boolean == 0);
    pdf_token_destroy(&token);

    assert_token(&lexer, PDF_TOKEN_NULL, 57);

    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_KEYWORD);
    assert(token.offset == 62);
    assert(strcmp(token.text, "R") == 0);
    pdf_token_destroy(&token);

    assert_token(&lexer, PDF_TOKEN_EOF, 64);
    reader_close(&reader);

    pdf_error_clear(&error);
    assert(reader_open(&reader, "tests/fixtures/invalid-exponent.txt", &error));
    lexer_init(&lexer, &reader, &error);
    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_INVALID);
    assert(token.offset == 0);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(reader_tell(&reader) > 0);
    pdf_token_destroy(&token);
    assert_token(&lexer, PDF_TOKEN_EOF, 4);
    assert(reader_eof(&reader));
    reader_close(&reader);

    pdf_error_clear(&error);
    assert(reader_open(&reader, "tests/fixtures/lone-sign.txt", &error));
    lexer_init(&lexer, &reader, &error);
    token = lexer_next(&lexer);
    assert(token.type == PDF_TOKEN_INVALID);
    assert(token.offset == 0);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(reader_tell(&reader) > 0);
    pdf_token_destroy(&token);
    assert_token(&lexer, PDF_TOKEN_EOF, 2);
    assert(reader_eof(&reader));
    reader_close(&reader);

    return 0;
}
