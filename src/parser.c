#include "parser.h"

void parser_init(pdf_parser *parser, pdf_lexer *lexer) {
    parser->lexer = lexer;
}

pdf_object *parser_parse_object(pdf_parser *parser) {
    pdf_token token = lexer_next(parser->lexer);

    switch(token.type) {
        case PDF_TOKEN_INT:
            return pdf_object_new_int(token.integer);

        default:
            return NULL;
    }
}