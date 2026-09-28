#include "content_lexer.h"

void pdf_content_lexer_init(pdf_content_lexer *lexer, const unsigned char *data,
                            size_t len, const pdf_limits *limits, pdf_error *error) {
    lexer_init_bytes(&lexer->lexer, data, len, limits, error);
}

pdf_token pdf_content_lexer_next(pdf_content_lexer *lexer) {
    return lexer_next(&lexer->lexer);
}
