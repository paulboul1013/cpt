#include "lexer.h"

#include <ctype.h>

//for reader readed bytes
void lexer_init(pdf_lexer *lexer, pdf_reader *reader) {
    lexer->reader = reader; 
}

static int is_whitespace(int c) {
    return c==0 ||
           c == '\t' ||
           c == '\n' ||
           c == '\f' ||
           c == '\r' ||
           c == ' ';
}

static void skip_whitespace(pdf_lexer *lexer) {
    pdf_reader *r = lexer->reader;

    while(!reader_eof(r)) {
        int c= reader_peek(r);

        if (!is_whitespace(c)) {
            break;
        }
        
        reader_get(r);
    }
}

static pdf_token lex_integer(pdf_lexer *lexer) {
    pdf_reader *reader = lexer->reader;

    long value = 0;
    int sign = 1;

    int c = reader_peek(reader);

    if (c=='-') {
        sign = -1;
        reader_get(reader);
    }
    else if (c=='+') {
        reader_get(reader);
    }

    while(!reader_eof(reader)) {
        c=reader_peek(reader);

        if (!isdigit((unsigned char)c)) {
            break;
        }

        reader_get(reader);

        value = value * 10 +(c-'0');
    }

    pdf_token token;
    token.type = PDF_TOKEN_INT;
    token.integer = sign * value;

    return token;
}

pdf_token lexer_next(pdf_lexer *lexer) {
    skip_whitespace(lexer);

    pdf_reader *reader = lexer->reader;

    if (reader_eof(reader)) {
        pdf_token token = {
            .type = PDF_TOKEN_EOF
        };

        return token;
    }

    int c = reader_peek(reader);

    if (isdigit((unsigned char)c) || c=='-' || c=='+') {
        return lex_integer(lexer);
    }

    //unkown byte
    reader_get(reader);

    pdf_token token = {
        .type = PDF_TOKEN_INVALID
    };

    return token;
}