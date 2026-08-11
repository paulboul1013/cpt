#ifndef PDF_PARSER_H
#define PDF_PARSER_H

#include "lexer.h"
#include "object.h"

typedef struct {
    pdf_lexer *lexer;
} pdf_parser;

void parser_init(pdf_parser *parser, pdf_lexer *lexer);

pdf_object *parser_parse_object(pdf_parser *parser);



#endif