#ifndef PDF_LEXER_H
#define PDF_LEXER_H

#include "reader.h"

/*
PDF file
   │
   ▼
PdfReader
   │
   │ bytes
   ▼
PdfLexer
   │
   │ tokens
   ▼
PDF_TOKEN_TYPE like int..
*/

typedef enum {
    PDF_TOKEN_EOF,

    PDF_TOKEN_INT,
    PDF_TOKEN_REAL,
    PDF_TOKEN_BOOL,
    PDF_TOKEN_NULL,
    PDF_TOKEN_NAME,
    PDF_TOKEN_KEYWORD,

    PDF_TOKEN_ARRAY_BEGIN,
    PDF_TOKEN_ARRAY_END,

    PDF_TOKEN_DICT_BEGIN,
    PDF_TOKEN_DICT_END,

    PDF_TOKEN_INVALID
} pdf_token_type;

typedef struct {
    pdf_token_type type;

    size_t offset;
    long integer;
    double real;
    int boolean;
    char *text;

} pdf_token;

/* A token owns text when text is not NULL; destroy is safe on an empty token. */
void pdf_token_init(pdf_token *token);
void pdf_token_destroy(pdf_token *token);
/* destination must be initialized; source ownership is transferred and reset. */
void pdf_token_move(pdf_token *destination, pdf_token *source);

typedef struct {
    pdf_reader *reader;
    pdf_error *error;
} pdf_lexer;

void lexer_init(pdf_lexer *lexer, pdf_reader *reader, pdf_error *error);

pdf_token lexer_next(pdf_lexer *lexer);

#endif
