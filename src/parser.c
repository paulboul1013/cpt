#include "parser.h"
#include <stdlib.h>

static pdf_token parser_next_token(pdf_parser *parser);
static pdf_token parser_peek_token(pdf_parser *parser);
static pdf_object *parse_array(pdf_parser *parser);
static pdf_object *parse_dict(pdf_parser *parser);

void parser_init(pdf_parser *parser, pdf_lexer *lexer) {
    parser->lexer = lexer;
    parser->has_lookahead = 0;
}


static pdf_token parser_next_token(pdf_parser *parser) {
    if (parser->has_lookahead) {
        parser->has_lookahead = 0;
        return parser->lookahead;
    }

    return lexer_next(parser->lexer);
}

pdf_object *parser_parse_object(pdf_parser *parser) {
    pdf_token token = parser_next_token(parser);

    switch(token.type) {
        case PDF_TOKEN_INT:
            return pdf_object_new_int(token.integer);

        case PDF_TOKEN_NAME:{
            pdf_object *obj = pdf_object_new_name(token.text);

            free(token.text);

            return obj;
        }

        case PDF_TOKEN_DICT_BEGIN:
            return parse_dict(parser);

        case PDF_TOKEN_ARRAY_BEGIN:
            return parse_array(parser);

        default:
            return NULL;
    }
}



static pdf_token parser_peek_token(pdf_parser *parser) {
    if (!parser->has_lookahead) {
        parser->lookahead = lexer_next(parser->lexer);
        parser->has_lookahead=1;
    }

    return parser->lookahead;
}


static pdf_object *parse_array(pdf_parser *parser) {
    pdf_object *array = pdf_object_new_array();
    
    if (!array) {
        return NULL;
    }

    while(1) {
        pdf_token token = parser_peek_token(parser);

        //watch current token is ']' end of array
        if (token.type==PDF_TOKEN_ARRAY_END) {
            parser_next_token(parser);
            break;
        }

        if (token.type==PDF_TOKEN_EOF) {
            pdf_object_free(array);
            return NULL;
        }

        pdf_object *item=parser_parse_object(parser);

        if (item==NULL) {
            pdf_object_free(array);
            return NULL;
        }

        if (!pdf_array_push(array,item)) {
            pdf_object_free(item);
            pdf_object_free(array);
            return NULL;
        }
    }

    return array;
}

static pdf_object *parse_dict(pdf_parser *parser) {
    pdf_object *dict = pdf_object_new_dict();

    if (dict == NULL) {
        return NULL;
    }

    while (1) {

        pdf_token token = parser_peek_token(parser);

        if (token.type == PDF_TOKEN_DICT_END) {

            parser_next_token(parser);

            break;
        }


        if (token.type == PDF_TOKEN_EOF) {

            pdf_object_free(dict);

            return NULL;
        }


        pdf_token key = parser_next_token(parser);

        if (key.type != PDF_TOKEN_NAME) {

            pdf_object_free(dict);

            return NULL;
        }


        pdf_object *value = parser_parse_object(parser);

        if (value == NULL) {

            free(key.text);

            pdf_object_free(dict);

            return NULL;
        }

        if (!pdf_dict_push(dict,key.text,value)) {

            free(key.text);

            pdf_object_free(value);
            pdf_object_free(dict);

            return NULL;
        }

        free(key.text);
    }

    return dict;
}