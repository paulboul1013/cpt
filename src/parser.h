#ifndef PDF_PARSER_H
#define PDF_PARSER_H

#include "lexer.h"
#include "object.h"

/* Resolve an indirect /Length to an integer; return zero if unavailable or invalid.
 * The parser restores its reader cursor after the callback, so the resolver may
 * seek the same input while looking up the referenced object. */
typedef int (*pdf_stream_length_resolver)(void *context, int64_t object_number,
                                          int64_t generation, int64_t *length);

typedef struct {
    pdf_lexer *lexer;
    pdf_error *error;
    const pdf_limits *limits;

    pdf_token lookahead[3];
    size_t lookahead_len;
    size_t depth;
    pdf_stream_length_resolver length_resolver;
    void *length_resolver_context;

} pdf_parser;

typedef struct {
    int64_t object_number;
    int64_t generation;
    pdf_object *body; /* Owned stream dictionary or ordinary object. */
    int is_stream;    /* Set even when stream.len is zero. */
    pdf_bytes stream; /* Owned raw bytes; empty for ordinary objects. */
} pdf_indirect_object;

void parser_init(pdf_parser *parser, pdf_lexer *lexer, pdf_error *error);
void parser_set_length_resolver(pdf_parser *parser,
                                pdf_stream_length_resolver resolver, void *context);

/* parser_peek returns a borrowed token owned by parser. */
const pdf_token *parser_peek(pdf_parser *parser);

/* parser_next moves token ownership into an initialized destination. */
int parser_next(pdf_parser *parser, pdf_token *token);

/* Releases an unconsumed lookahead token. */
void parser_destroy(pdf_parser *parser);

pdf_object *parser_parse_object(pdf_parser *parser);

/* Parses one indirect object; the caller owns the result and its raw stream. */
pdf_indirect_object *parser_parse_indirect_object(pdf_parser *parser);
void pdf_indirect_object_free(pdf_indirect_object *object);

/* Validates that the next token is EOF after a standalone object. */
int parser_expect_eof(pdf_parser *parser);



#endif
