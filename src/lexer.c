#include "lexer.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

void pdf_token_init(pdf_token *token) {
    if (token == NULL) {
        return;
    }

    token->type = PDF_TOKEN_EOF;
    token->offset = 0;
    token->integer = 0;
    token->real = 0.0;
    token->boolean = 0;
    token->bytes.data = NULL;
    token->bytes.len = 0;
    token->text = NULL;
}

void pdf_token_destroy(pdf_token *token) {
    if (token == NULL) {
        return;
    }

    free(token->bytes.data);
    free(token->text);
    pdf_token_init(token);
}

void pdf_token_move(pdf_token *destination, pdf_token *source) {
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    pdf_token_destroy(destination);
    *destination = *source;
    pdf_token_init(source);
}

static pdf_token token_with_type(pdf_token_type type, size_t offset) {
    pdf_token token;

    pdf_token_init(&token);
    token.type = type;
    token.offset = offset;

    return token;
}

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} byte_buffer;

static int byte_buffer_append(pdf_lexer *lexer, byte_buffer *buffer,
                              unsigned char byte, size_t offset) {
    if (buffer->len == buffer->cap) {
        size_t new_cap = buffer->cap == 0 ? 16 : buffer->cap * 2;

        if (new_cap < buffer->cap) {
            pdf_error_set(lexer->error, PDF_ERROR_RESOURCE_LIMIT, offset, "lexer",
                          "byte buffer capacity overflow");
            return 0;
        }

        unsigned char *new_data = realloc(buffer->data, new_cap);

        if (new_data == NULL) {
            pdf_error_set(lexer->error, PDF_ERROR_OUT_OF_MEMORY, offset, "lexer",
                          "could not grow byte buffer");
            return 0;
        }

        buffer->data = new_data;
        buffer->cap = new_cap;
    }

    buffer->data[buffer->len++] = byte;
    return 1;
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

//for reader readed bytes
void lexer_init(pdf_lexer *lexer, pdf_reader *reader, pdf_error *error) {
    lexer->reader = reader; 
    lexer->error = error;
}

static int is_whitespace(int c) {
    return c==0 ||
           c == '\t' ||
           c == '\n' ||
           c == '\f' ||
           c == '\r' ||
           c == ' ';
}

static int is_pdf_delimiter(int c) {
    return c == '(' ||
           c == ')' ||
           c == '<' ||
           c == '>' ||
           c == '[' ||
           c == ']' ||
           c == '{' ||
           c == '}' ||
           c == '/' ||
           c == '%';
}

static int is_name_end(int c) {
    return is_pdf_delimiter(c) || is_whitespace(c);
}

static int is_token_end(int c) {
    return is_name_end(c);
}

static pdf_token lex_name(pdf_lexer *lexer) {
    pdf_reader *reader = lexer->reader;
	size_t offset = reader_tell(reader);
	byte_buffer buffer = {0};

    //first parse '/'
    reader_get(reader);

    while(!reader_eof(reader)) {
        int c = reader_peek(reader);

        if (is_name_end(c)) {
            break;
        }

        if (c != '#') {
            reader_get(reader);
            if (!byte_buffer_append(lexer, &buffer, (unsigned char)c, offset)) {
                free(buffer.data);
                return token_with_type(PDF_TOKEN_INVALID, offset);
            }
            continue;
        }

        size_t escape_offset = reader_tell(reader);
        reader_get(reader);
        int high = -1;
        int low = -1;

        if (!reader_eof(reader) && !is_name_end(reader_peek(reader))) {
            high = hex_value(reader_get(reader));
        }
        if (!reader_eof(reader) && !is_name_end(reader_peek(reader))) {
            low = hex_value(reader_get(reader));
        }

        if (high < 0 || low < 0) {
            pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, escape_offset, "lexer",
                          "name escape must contain two hexadecimal digits");
            free(buffer.data);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }

        if (!byte_buffer_append(lexer, &buffer,
                                (unsigned char)((high << 4) | low), escape_offset)) {
            free(buffer.data);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }
    }

    pdf_token token = token_with_type(PDF_TOKEN_NAME, offset);
    token.bytes.data = buffer.data;
    token.bytes.len = buffer.len;

    return token;
}

static void skip_whitespace(pdf_lexer *lexer) {
    pdf_reader *r = lexer->reader;

    while(!reader_eof(r)) {
        int c= reader_peek(r);

        if (!is_whitespace(c)) {
            break;
        }
        
        reader_get(r);
    }
}

static void skip_ignored(pdf_lexer *lexer) {
    pdf_reader *reader = lexer->reader;

    while (!reader_eof(reader)) {
        skip_whitespace(lexer);

        if (reader_peek(reader) != '%') {
            return;
        }

        while (!reader_eof(reader)) {
            int c = reader_get(reader);

            if (c == '\n' || c == '\r') {
                break;
            }
        }
    }
}

static int is_number_start(int c) {
    return isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.';
}

static pdf_token lex_number(pdf_lexer *lexer) {
    pdf_reader *reader = lexer->reader;
    size_t offset = reader_tell(reader);
    size_t start = offset;
    int has_integer_digits = 0;
    int has_fraction = 0;
    int has_fraction_digits = 0;
    int c = reader_peek(reader);

    if (c == '-' || c == '+') {
        reader_get(reader);
    }

    while (!reader_eof(reader) && isdigit((unsigned char)reader_peek(reader))) {
        has_integer_digits = 1;
        reader_get(reader);
    }

    if (!reader_eof(reader) && reader_peek(reader) == '.') {
        has_fraction = 1;
        reader_get(reader);

        while (!reader_eof(reader) && isdigit((unsigned char)reader_peek(reader))) {
            has_fraction_digits = 1;
            reader_get(reader);
        }
    }

    while (!reader_eof(reader) && !is_token_end(reader_peek(reader))) {
        reader_get(reader);
    }

    size_t end = reader_tell(reader);
    size_t length = end - start;
    char *lexeme = malloc(length + 1);

    if (lexeme == NULL) {
        pdf_error_set(lexer->error, PDF_ERROR_OUT_OF_MEMORY, offset, "lexer",
                      "could not allocate numeric token");
        return token_with_type(PDF_TOKEN_INVALID, offset);
    }

    memcpy(lexeme, reader->data + start, length);
    lexeme[length] = '\0';

    if ((!has_integer_digits && !has_fraction_digits) ||
        (!has_integer_digits && !has_fraction) ||
        (strchr(lexeme, 'e') != NULL) || (strchr(lexeme, 'E') != NULL) ||
        (end < reader->size && !is_token_end(reader->data[end]))) {
        pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset, "lexer",
                      "invalid numeric token");
        free(lexeme);
        return token_with_type(PDF_TOKEN_INVALID, offset);
    }

    pdf_token token = token_with_type(has_fraction ? PDF_TOKEN_REAL : PDF_TOKEN_INT,
                                      offset);

    errno = 0;
    if (has_fraction) {
        char *parse_end = NULL;
        token.real = strtod(lexeme, &parse_end);

        if (errno == ERANGE || parse_end == NULL || *parse_end != '\0') {
            pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset, "lexer",
                          "real value is out of range or malformed");
            pdf_token_destroy(&token);
            free(lexeme);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }
    } else {
        char *parse_end = NULL;
        long long value = strtoll(lexeme, &parse_end, 10);

        if (errno == ERANGE || parse_end == NULL || *parse_end != '\0') {
            pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset, "lexer",
                          "integer value is out of range or malformed");
            pdf_token_destroy(&token);
            free(lexeme);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }

        token.integer = (int64_t)value;
    }

    free(lexeme);
    return token;
}

static pdf_token lex_keyword(pdf_lexer *lexer) {
    pdf_reader *reader = lexer->reader;
    size_t offset = reader_tell(reader);
    size_t start = offset;

    while (!reader_eof(reader) && !is_token_end(reader_peek(reader))) {
        reader_get(reader);
    }

    size_t length = reader_tell(reader) - start;
    char *text = malloc(length + 1);

    if (text == NULL) {
        pdf_error_set(lexer->error, PDF_ERROR_OUT_OF_MEMORY, offset, "lexer",
                      "could not allocate keyword token");
        return token_with_type(PDF_TOKEN_INVALID, offset);
    }

    memcpy(text, reader->data + start, length);
    text[length] = '\0';

    if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
        pdf_token token = token_with_type(PDF_TOKEN_BOOL, offset);
        token.boolean = strcmp(text, "true") == 0;
        free(text);
        return token;
    }

    if (strcmp(text, "null") == 0) {
        free(text);
        return token_with_type(PDF_TOKEN_NULL, offset);
    }

    pdf_token token = token_with_type(PDF_TOKEN_KEYWORD, offset);
    token.text = text;
    return token;
}

static pdf_token lex_literal_string(pdf_lexer *lexer, size_t offset) {
    pdf_reader *reader = lexer->reader;
    byte_buffer buffer = {0};
    int depth = 1;

    reader_get(reader);

    while (!reader_eof(reader)) {
        size_t byte_offset = reader_tell(reader);
        int c = reader_get(reader);

        if (c == '(') {
            depth++;
            if (!byte_buffer_append(lexer, &buffer, (unsigned char)c, byte_offset)) {
                free(buffer.data);
                return token_with_type(PDF_TOKEN_INVALID, offset);
            }
            continue;
        }

        if (c == ')') {
            depth--;
            if (depth == 0) {
                pdf_token token = token_with_type(PDF_TOKEN_STRING, offset);
                token.bytes.data = buffer.data;
                token.bytes.len = buffer.len;
                return token;
            }

            if (!byte_buffer_append(lexer, &buffer, (unsigned char)c, byte_offset)) {
                free(buffer.data);
                return token_with_type(PDF_TOKEN_INVALID, offset);
            }
            continue;
        }

        if (c != '\\') {
            if (!byte_buffer_append(lexer, &buffer, (unsigned char)c, byte_offset)) {
                free(buffer.data);
                return token_with_type(PDF_TOKEN_INVALID, offset);
            }
            continue;
        }

        if (reader_eof(reader)) {
            pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, byte_offset, "lexer",
                          "literal string has truncated escape");
            free(buffer.data);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }

        int escaped = reader_get(reader);
        switch (escaped) {
            case 'n': escaped = '\n'; break;
            case 'r': escaped = '\r'; break;
            case 't': escaped = '\t'; break;
            case 'b': escaped = '\b'; break;
            case 'f': escaped = '\f'; break;
            case '\r':
                if (!reader_eof(reader) && reader_peek(reader) == '\n') {
                    reader_get(reader);
                }
                continue;
            case '\n':
                continue;
            default:
                if (escaped >= '0' && escaped <= '7') {
                    int value = escaped - '0';

                    for (int count = 1; count < 3 && !reader_eof(reader); count++) {
                        int octal = reader_peek(reader);
                        if (octal < '0' || octal > '7') {
                            break;
                        }
                        value = value * 8 + (reader_get(reader) - '0');
                    }
                    escaped = value & 0xff;
                }
                break;
        }

        if (!byte_buffer_append(lexer, &buffer, (unsigned char)escaped, byte_offset)) {
            free(buffer.data);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }
    }

    pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset, "lexer",
                  "literal string is not terminated");
    free(buffer.data);
    return token_with_type(PDF_TOKEN_INVALID, offset);
}

static pdf_token lex_hex_string(pdf_lexer *lexer, size_t offset) {
    pdf_reader *reader = lexer->reader;
    byte_buffer buffer = {0};
    int high = -1;

    while (!reader_eof(reader)) {
        size_t byte_offset = reader_tell(reader);
        int c = reader_get(reader);

        if (c == '>') {
            if (high >= 0 && !byte_buffer_append(lexer, &buffer,
                                                   (unsigned char)(high << 4), byte_offset)) {
                free(buffer.data);
                return token_with_type(PDF_TOKEN_INVALID, offset);
            }

            pdf_token token = token_with_type(PDF_TOKEN_HEX_STRING, offset);
            token.bytes.data = buffer.data;
            token.bytes.len = buffer.len;
            return token;
        }

        if (is_whitespace(c)) {
            continue;
        }

        int value = hex_value(c);
        if (value < 0) {
            pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, byte_offset, "lexer",
                          "hex string contains a non-hexadecimal byte");
            while (!reader_eof(reader) && reader_get(reader) != '>') {
            }
            free(buffer.data);
            return token_with_type(PDF_TOKEN_INVALID, offset);
        }

        if (high < 0) {
            high = value;
        } else {
            if (!byte_buffer_append(lexer, &buffer,
                                    (unsigned char)((high << 4) | value), byte_offset)) {
                free(buffer.data);
                return token_with_type(PDF_TOKEN_INVALID, offset);
            }
            high = -1;
        }
    }

    pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset,
                  "lexer", "hex string is not terminated");
    free(buffer.data);
    return token_with_type(PDF_TOKEN_INVALID, offset);
}

pdf_token lexer_next(pdf_lexer *lexer) {
    skip_ignored(lexer);

    pdf_reader *reader = lexer->reader;
	size_t offset = reader_tell(reader);

    //EOF
    if (reader_eof(reader)) {
        return token_with_type(PDF_TOKEN_EOF, offset);
    }

    int c = reader_peek(reader);

    if (c=='<') {
        reader_get(reader);

        if (reader_peek(reader)=='<') {
            reader_get(reader);

            return token_with_type(PDF_TOKEN_DICT_BEGIN, offset);
        }


        return lex_hex_string(lexer, offset);
    }

    if (c == '>') {

        reader_get(reader);

        if (reader_peek(reader) == '>') {

            reader_get(reader);

            return token_with_type(PDF_TOKEN_DICT_END, offset);
        }

        pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset, "lexer",
                      "single '>' is not a dictionary delimiter");
        return token_with_type(PDF_TOKEN_INVALID, offset);
    }

    if (c=='/') {
        return lex_name(lexer);
    }

    if (c=='(') {
        return lex_literal_string(lexer, offset);
    }

    //integer
    if (is_number_start(c)) {
        return lex_number(lexer);
    }

    //array begin
    if (c=='[') {
        reader_get(reader);

        return token_with_type(PDF_TOKEN_ARRAY_BEGIN, offset);
    }

    //array end
    if (c==']') {
        reader_get(reader);

        return token_with_type(PDF_TOKEN_ARRAY_END, offset);
    }

    if (isalpha((unsigned char)c)) {
        return lex_keyword(lexer);
    }
    

    //unkown byte
    reader_get(reader);

    pdf_error_set(lexer->error, PDF_ERROR_MALFORMED, offset, "lexer",
                  "unexpected byte 0x%02x", (unsigned int)(unsigned char)c);
    return token_with_type(PDF_TOKEN_INVALID, offset);
}
