#ifndef PDF_CONTENT_LEXER_H
#define PDF_CONTENT_LEXER_H
#include "lexer.h"

/* Borrows the decoded bytes and limits. Returned tokens own their payloads;
 * destroy each with pdf_token_destroy(). All offsets are decoded byte offsets. */
typedef struct { pdf_lexer lexer; } pdf_content_lexer;
void pdf_content_lexer_init(pdf_content_lexer *lexer, const unsigned char *data,
                            size_t len, const pdf_limits *limits, pdf_error *error);
pdf_token pdf_content_lexer_next(pdf_content_lexer *lexer);
#endif
