#include "parser.h"
#include <stdlib.h>

static pdf_object *parse_array(pdf_parser *parser);
static pdf_object *parse_dict(pdf_parser *parser);

static size_t parser_offset(const pdf_parser *parser) {
    if (parser == NULL || parser->lexer == NULL || parser->lexer->reader == NULL) {
        return 0;
    }

    return reader_tell(parser->lexer->reader);
}

void parser_init(pdf_parser *parser, pdf_lexer *lexer, pdf_error *error) {
    parser->lexer = lexer;
    parser->error = error;
    pdf_token_init(&parser->lookahead);
    parser->has_lookahead = 0;
}

int parser_next(pdf_parser *parser, pdf_token *token) {
    if (parser == NULL || token == NULL) {
        return 0;
    }

    if (parser->lexer == NULL) {
        pdf_error_set(parser->error, PDF_ERROR_MALFORMED, parser_offset(parser),
                      "parser", "parser has no lexer");
        return 0;
    }

    if (parser->has_lookahead) {
        pdf_token_move(token, &parser->lookahead);
        parser->has_lookahead = 0;
        return 1;
    }

    pdf_token_destroy(token);
    *token = lexer_next(parser->lexer);
    return 1;
}

const pdf_token *parser_peek(pdf_parser *parser) {
    if (parser == NULL) {
        return NULL;
    }

    if (parser->lexer == NULL) {
        pdf_error_set(parser->error, PDF_ERROR_MALFORMED, parser_offset(parser),
                      "parser", "parser has no lexer");
        return NULL;
    }

    if (!parser->has_lookahead) {
        parser->lookahead = lexer_next(parser->lexer);
        parser->has_lookahead=1;
    }

    return &parser->lookahead;
}

void parser_destroy(pdf_parser *parser) {
    if (parser == NULL) {
        return;
    }

    if (parser->has_lookahead) {
        pdf_token_destroy(&parser->lookahead);
        parser->has_lookahead = 0;
    }

    parser->lexer = NULL;
    parser->error = NULL;
}

pdf_object *parser_parse_object(pdf_parser *parser) {
    pdf_token token;
    pdf_token_init(&token);

    if (!parser_next(parser, &token)) {
        return NULL;
    }

    switch(token.type) {
        case PDF_TOKEN_INT: {
            pdf_object *object = pdf_object_new_int(token.integer);

            if (object == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate integer object");
            }

            pdf_token_destroy(&token);

            return object;
        }

        case PDF_TOKEN_NAME:{
            pdf_object *obj = pdf_object_new_name_bytes(token.bytes.data, token.bytes.len);

            if (obj == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate name object");
            }

            pdf_token_destroy(&token);

            return obj;
        }

        case PDF_TOKEN_DICT_BEGIN:
            pdf_token_destroy(&token);
            return parse_dict(parser);

        case PDF_TOKEN_ARRAY_BEGIN:
            pdf_token_destroy(&token);
            return parse_array(parser);

        default:
            pdf_error_set(parser->error, PDF_ERROR_MALFORMED, token.offset,
                          "parser", "unexpected token while parsing object");
            pdf_token_destroy(&token);
            return NULL;
    }
}

static pdf_object *parse_array(pdf_parser *parser) {
    pdf_object *array = pdf_object_new_array();
    
    if (!array) {
        pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, parser_offset(parser),
                      "parser", "could not allocate array object");
        return NULL;
    }

    while(1) {
        const pdf_token *token = parser_peek(parser);

        if (token == NULL) {
            pdf_error_set(parser->error, PDF_ERROR_MALFORMED, parser_offset(parser),
                          "parser", "could not read array token");
            pdf_object_free(array);
            return NULL;
        }

        //watch current token is ']' end of array
        if (token->type==PDF_TOKEN_ARRAY_END) {
            pdf_token end;
            pdf_token_init(&end);
            parser_next(parser, &end);
            pdf_token_destroy(&end);
            break;
        }

        if (token->type==PDF_TOKEN_EOF) {
            pdf_error_set(parser->error, PDF_ERROR_MALFORMED, token->offset, "parser",
                          "unexpected end of input while parsing array");
            pdf_object_free(array);
            return NULL;
        }

        size_t item_offset = token->offset;
        pdf_object *item=parser_parse_object(parser);

        if (item==NULL) {
            pdf_object_free(array);
            return NULL;
        }

        if (!pdf_array_push(array,item)) {
            pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, item_offset,
                          "parser", "could not grow array object");
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
        pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, parser_offset(parser),
                      "parser", "could not allocate dictionary object");
        return NULL;
    }

    while (1) {

        const pdf_token *token = parser_peek(parser);

        if (token == NULL) {
            pdf_error_set(parser->error, PDF_ERROR_MALFORMED, parser_offset(parser),
                          "parser", "could not read dictionary token");
            pdf_object_free(dict);
            return NULL;
        }

        if (token->type == PDF_TOKEN_DICT_END) {

            pdf_token end;
            pdf_token_init(&end);
            parser_next(parser, &end);
            pdf_token_destroy(&end);

            break;
        }


        if (token->type == PDF_TOKEN_EOF) {

            pdf_error_set(parser->error, PDF_ERROR_MALFORMED, token->offset, "parser",
                          "unexpected end of input while parsing dictionary");

            pdf_object_free(dict);

            return NULL;
        }


        pdf_token key;
        pdf_token_init(&key);
        parser_next(parser, &key);

        if (key.type != PDF_TOKEN_NAME) {

            pdf_error_set(parser->error, PDF_ERROR_MALFORMED, key.offset, "parser",
                          "dictionary key must be a name");

            pdf_token_destroy(&key);
            pdf_object_free(dict);

            return NULL;
        }


        pdf_object *value = parser_parse_object(parser);

        if (value == NULL) {

            pdf_token_destroy(&key);

            pdf_object_free(dict);

            return NULL;
        }

        if (!pdf_dict_push_bytes(dict, key.bytes.data, key.bytes.len, value)) {

            pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, key.offset,
                          "parser", "could not grow dictionary object");

            pdf_token_destroy(&key);

            pdf_object_free(value);
            pdf_object_free(dict);

            return NULL;
        }

        pdf_token_destroy(&key);
    }

    return dict;
}
