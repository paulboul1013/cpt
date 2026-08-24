#include "lexer.h"
#include <stdlib.h>

#include <ctype.h>

void pdf_token_init(pdf_token *token) {
    if (token == NULL) {
        return;
    }

    token->type = PDF_TOKEN_EOF;
    token->integer = 0;
    token->text = NULL;
}

void pdf_token_destroy(pdf_token *token) {
    if (token == NULL) {
        return;
    }

    free(token->text);
    pdf_token_init(token);
}

void pdf_token_move(pdf_token *destination, pdf_token *source) {
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    pdf_token_destroy(destination);
    *destination = *source;
    pdf_token_init(source);
}

static pdf_token token_with_type(pdf_token_type type) {
    pdf_token token;

    pdf_token_init(&token);
    token.type = type;

    return token;
}

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

static int is_pdf_delimiter(int c) {
    return c == '(' ||
           c == ')' ||
           c == '<' ||
           c == '>' ||
           c == '[' ||
           c == ']' ||
           c == '{' ||
           c == '}' ||
           c == '/' ||
           c == '%';
}

static int is_name_end(int c) {
    return is_pdf_delimiter(c) || is_whitespace(c);
}

static pdf_token lex_name(pdf_lexer *lexer) {
    pdf_reader *reader = lexer->reader;

    //first parse '/'
    reader_get(reader);

    size_t start = reader_tell(reader);

    //find end of name
    while(!reader_eof(reader)) {
        int c= reader_peek(reader);

        if (is_name_end(c)) {
            break;
        }

        reader_get(reader);
    }

    size_t end = reader_tell(reader);

    size_t len = end - start;

    char *name=malloc(len+1);

    if (name==NULL) {
        return token_with_type(PDF_TOKEN_INVALID);
    }

    for(size_t i=0;i<len;i++){
        name[i]=(char)reader->data[start+i];
    }

    name[len]='\0';

    pdf_token token = token_with_type(PDF_TOKEN_NAME);
    token.text = name;

    return token;
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

    pdf_token_init(&token);
    token.type = PDF_TOKEN_INT;
    token.integer = sign * value;

    return token;
}

pdf_token lexer_next(pdf_lexer *lexer) {
    skip_whitespace(lexer);

    pdf_reader *reader = lexer->reader;

    //EOF
    if (reader_eof(reader)) {
        return token_with_type(PDF_TOKEN_EOF);
    }

    int c = reader_peek(reader);

    if (c=='<') {
        reader_get(reader);

        if (reader_peek(reader)=='<') {
            reader_get(reader);

            return token_with_type(PDF_TOKEN_DICT_BEGIN);
        }


        return token_with_type(PDF_TOKEN_INVALID);
    }

    if (c == '>') {

        reader_get(reader);

        if (reader_peek(reader) == '>') {

            reader_get(reader);

            return token_with_type(PDF_TOKEN_DICT_END);
        }

        return token_with_type(PDF_TOKEN_INVALID);
    }

    if (c=='/') {
        return lex_name(lexer);
    }

    //integer
    if (isdigit((unsigned char)c) || c=='-' || c=='+') {
        return lex_integer(lexer);
    }

    //array begin
    if (c=='[') {
        reader_get(reader);

        return token_with_type(PDF_TOKEN_ARRAY_BEGIN);
    }

    //array end
    if (c==']') {
        reader_get(reader);

        return token_with_type(PDF_TOKEN_ARRAY_END);
    }
    

    //unkown byte
    reader_get(reader);

    return token_with_type(PDF_TOKEN_INVALID);
}
