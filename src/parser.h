#ifndef PDF_PARSER_H
#define PDF_PARSER_H

#include "lexer.h"
#include "object.h"

typedef struct {
    pdf_lexer *lexer;
    pdf_error *error;

    pdf_token lookahead; //watch next token
    int has_lookahead; //save next token

} pdf_parser;

void parser_init(pdf_parser *parser, pdf_lexer *lexer, pdf_error *error);

/* parser_peek returns a borrowed token owned by parser. */
const pdf_token *parser_peek(pdf_parser *parser);

/* parser_next moves token ownership into an initialized destination. */
int parser_next(pdf_parser *parser, pdf_token *token);

/* Releases an unconsumed lookahead token. */
void parser_destroy(pdf_parser *parser);

pdf_object *parser_parse_object(pdf_parser *parser);

/* Validates that the next token is EOF after a standalone object. */
int parser_expect_eof(pdf_parser *parser);



#endif
