#include "../src/content_lexer.h"
#include <assert.h>
#include <string.h>

int main(void) {
    const unsigned char input[] = "% ignored\r\n/F#31 12 -.5 (a\\000\\n\\(b\\)) <0041f> [ ] << >> ' \" T*";
    const pdf_token_type types[] = {PDF_TOKEN_NAME, PDF_TOKEN_INT, PDF_TOKEN_REAL,
        PDF_TOKEN_STRING, PDF_TOKEN_HEX_STRING, PDF_TOKEN_ARRAY_BEGIN,
        PDF_TOKEN_ARRAY_END, PDF_TOKEN_DICT_BEGIN, PDF_TOKEN_DICT_END,
        PDF_TOKEN_KEYWORD, PDF_TOKEN_KEYWORD, PDF_TOKEN_KEYWORD, PDF_TOKEN_EOF};
    pdf_limits limits;
    pdf_limits_default(&limits);
    pdf_error error;
    pdf_error_init(&error);
    pdf_content_lexer lexer;
    pdf_content_lexer_init(&lexer, input, sizeof(input)-1, &limits, &error);
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        pdf_token t = pdf_content_lexer_next(&lexer);
        assert(t.type == types[i]);
        if (i == 0) { assert(t.offset == 11); assert(t.bytes.len == 2); assert(memcmp(t.bytes.data, "F1", 2) == 0); }
        if (i == 3) { assert(t.bytes.len == 6); assert(memcmp(t.bytes.data, "a\0\n(b)", 6) == 0); }
        if (i == 4) { assert(t.bytes.len == 3); assert(memcmp(t.bytes.data, "\0A\xf0", 3) == 0); }
        if (i == 11) assert(strcmp(t.text, "T*") == 0);
        pdf_token_destroy(&t);
    }
    assert(error.code == PDF_ERROR_NONE);
    const char *bad[] = {"(abc", "<0g>", "/A#q0", ">", "]", "{", "1e3"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        pdf_error_clear(&error);
        pdf_content_lexer_init(&lexer, (const unsigned char *)bad[i], strlen(bad[i]), &limits, &error);
        pdf_token t = pdf_content_lexer_next(&lexer);
        assert(t.type == (i == 4 ? PDF_TOKEN_ARRAY_END : PDF_TOKEN_INVALID));
        pdf_token_destroy(&t);
    }
    pdf_error_clear(&error);
    limits.max_token_size = 2;
    pdf_content_lexer_init(&lexer, (const unsigned char *)"(abc)", 5, &limits, &error);
    pdf_token t = pdf_content_lexer_next(&lexer);
    assert(t.type == PDF_TOKEN_INVALID && error.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_token_destroy(&t);
    pdf_limits_default(&limits);
    pdf_error_clear(&error);
    const unsigned char binary[] = {'(', 'a', 0, 'b', ')', ' ', '<', '0', '>', ' ',
        '(', 'a', '\r', '\n', 'b', '\r', 'c', '\n', 'd', '\\', '\r', '\n', 'e', ')'};
    pdf_content_lexer_init(&lexer, binary, sizeof(binary), &limits, &error);
    t = pdf_content_lexer_next(&lexer);
    assert(t.type == PDF_TOKEN_STRING && t.bytes.len == 3);
    assert(memcmp(t.bytes.data, "a\0b", 3) == 0);
    pdf_token_destroy(&t);
    t = pdf_content_lexer_next(&lexer);
    assert(t.type == PDF_TOKEN_HEX_STRING && t.bytes.len == 1 && t.bytes.data[0] == 0);
    pdf_token_destroy(&t);
    t = pdf_content_lexer_next(&lexer);
    assert(t.type == PDF_TOKEN_STRING && t.bytes.len == 8);
    assert(memcmp(t.bytes.data, "a\nb\nc\nde", 8) == 0);
    pdf_token_destroy(&t);
    assert(error.code == PDF_ERROR_NONE);
    return 0;
}
