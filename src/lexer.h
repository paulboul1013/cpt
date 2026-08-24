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
    PDF_TOKEN_NAME,

    PDF_TOKEN_ARRAY_BEGIN,
    PDF_TOKEN_ARRAY_END,

    PDF_TOKEN_DICT_BEGIN,
    PDF_TOKEN_DICT_END,

    PDF_TOKEN_INVALID
} pdf_token_type;

typedef struct {
    pdf_token_type type;

    long integer;
    char *text;

} pdf_token;

typedef struct {
    pdf_reader *reader;
} pdf_lexer;

void lexer_init(pdf_lexer *lexer, pdf_reader *reader);

pdf_token lexer_next(pdf_lexer *lexer);

#endif
