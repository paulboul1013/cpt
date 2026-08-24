#include "parser.h"
#include <stdlib.h>
#include <string.h>

static pdf_object *parse_array(pdf_parser *parser);
static pdf_object *parse_dict(pdf_parser *parser);
static const pdf_token *parser_peek_n(pdf_parser *parser, size_t index);

static size_t parser_offset(const pdf_parser *parser) {
    if (parser == NULL || parser->lexer == NULL || parser->lexer->reader == NULL) {
        return 0;
    }

    return reader_tell(parser->lexer->reader);
}

static int is_reference_marker(const pdf_token *token) {
    return token != NULL && token->type == PDF_TOKEN_KEYWORD && token->text != NULL &&
           strcmp(token->text, "R") == 0;
}

void parser_init(pdf_parser *parser, pdf_lexer *lexer, pdf_error *error) {
    parser->lexer = lexer;
    parser->error = error;
    for (size_t i = 0; i < 3; i++) {
        pdf_token_init(&parser->lookahead[i]);
    }
    parser->lookahead_len = 0;
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

    if (parser->lookahead_len > 0) {
        pdf_token_move(token, &parser->lookahead[0]);

        for (size_t i = 1; i < parser->lookahead_len; i++) {
            pdf_token_move(&parser->lookahead[i - 1], &parser->lookahead[i]);
        }

        parser->lookahead_len--;
        return 1;
    }

    pdf_token_destroy(token);
    *token = lexer_next(parser->lexer);
    return 1;
}

const pdf_token *parser_peek(pdf_parser *parser) {
    return parser_peek_n(parser, 0);
}

static const pdf_token *parser_peek_n(pdf_parser *parser, size_t index) {
    if (parser == NULL) {
        return NULL;
    }

    if (index >= 3) {
        pdf_error_set(parser->error, PDF_ERROR_RESOURCE_LIMIT, parser_offset(parser),
                      "parser", "lookahead depth exceeds parser buffer");
        return NULL;
    }

    if (parser->lexer == NULL) {
        pdf_error_set(parser->error, PDF_ERROR_MALFORMED, parser_offset(parser),
                      "parser", "parser has no lexer");
        return NULL;
    }

    while (parser->lookahead_len <= index) {
        parser->lookahead[parser->lookahead_len] = lexer_next(parser->lexer);
        parser->lookahead_len++;
    }

    return &parser->lookahead[index];
}

void parser_destroy(pdf_parser *parser) {
    if (parser == NULL) {
        return;
    }

    for (size_t i = 0; i < parser->lookahead_len; i++) {
        pdf_token_destroy(&parser->lookahead[i]);
    }

    parser->lookahead_len = 0;
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
        case PDF_TOKEN_NULL: {
            pdf_object *object = pdf_object_new_null();

            if (object == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate null object");
            }
            pdf_token_destroy(&token);
            return object;
        }

        case PDF_TOKEN_BOOL: {
            pdf_object *object = pdf_object_new_bool(token.boolean);

            if (object == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate boolean object");
            }
            pdf_token_destroy(&token);
            return object;
        }

        case PDF_TOKEN_INT: {
            const pdf_token *generation = parser_peek_n(parser, 0);
            const pdf_token *marker = generation == NULL
                                          ? NULL
                                          : parser_peek_n(parser, 1);

            if (generation != NULL && generation->type == PDF_TOKEN_INT &&
                is_reference_marker(marker)) {
                pdf_token generation_token;
                pdf_token marker_token;
                pdf_token_init(&generation_token);
                pdf_token_init(&marker_token);
                (void)parser_next(parser, &generation_token);
                (void)parser_next(parser, &marker_token);

                if (token.integer < 0 || generation_token.integer < 0) {
                    pdf_error_set(parser->error, PDF_ERROR_MALFORMED, token.offset,
                                  "parser", "reference numbers must be non-negative");
                    pdf_token_destroy(&generation_token);
                    pdf_token_destroy(&marker_token);
                    pdf_token_destroy(&token);
                    return NULL;
                }

                pdf_object *reference = pdf_object_new_ref(token.integer,
                                                            generation_token.integer);

                if (reference == NULL) {
                    pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                                  "parser", "could not allocate reference object");
                }

                pdf_token_destroy(&generation_token);
                pdf_token_destroy(&marker_token);
                pdf_token_destroy(&token);
                return reference;
            }

            pdf_object *object = pdf_object_new_int(token.integer);

            if (object == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate integer object");
            }

            pdf_token_destroy(&token);

            return object;
        }

        case PDF_TOKEN_REAL: {
            pdf_object *object = pdf_object_new_real(token.real);

            if (object == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate real object");
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

        case PDF_TOKEN_STRING: {
            pdf_object *obj = pdf_object_new_string_bytes(token.bytes.data,
                                                            token.bytes.len);

            if (obj == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate string object");
            }
            pdf_token_destroy(&token);
            return obj;
        }

        case PDF_TOKEN_HEX_STRING: {
            pdf_object *obj = pdf_object_new_hex_string_bytes(token.bytes.data,
                                                               token.bytes.len);

            if (obj == NULL) {
                pdf_error_set(parser->error, PDF_ERROR_OUT_OF_MEMORY, token.offset,
                              "parser", "could not allocate hex string object");
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

int parser_expect_eof(pdf_parser *parser) {
    const pdf_token *token = parser_peek(parser);

    if (token == NULL) {
        return 0;
    }

    if (token->type == PDF_TOKEN_EOF) {
        pdf_token end;
        pdf_token_init(&end);
        (void)parser_next(parser, &end);
        pdf_token_destroy(&end);
        return 1;
    }

    pdf_error_set(parser->error, PDF_ERROR_MALFORMED, token->offset, "parser",
                  "trailing token after standalone object");

    pdf_token trailing;
    pdf_token_init(&trailing);
    (void)parser_next(parser, &trailing);
    pdf_token_destroy(&trailing);
    return 0;
}
