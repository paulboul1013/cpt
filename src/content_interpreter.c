#include "content_interpreter.h"
#include "content_lexer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pdf_content_lexer lexer;
    pdf_token token;
    const pdf_limits *limits;
    pdf_error *error; /* Internal errors use decoded offsets. */
} content_parser;

static void advance(content_parser *parser) {
    pdf_token_destroy(&parser->token);
    parser->token = pdf_content_lexer_next(&parser->lexer);
}

static void fail(content_parser *parser, pdf_error_code code, const char *reason) {
    pdf_error_set(parser->error, code, parser->token.offset, "content", "%s", reason);
}

/* Object helpers also guard allocation arithmetic; check here to distinguish
 * capacity overflow (resource limit) from an allocator failure (out of memory). */
static int entry_capacity(content_parser *parser, size_t len, size_t cap, size_t size) {
    if (len < cap) return 1;
    if (cap > SIZE_MAX / 2 || (cap == 0 ? 4 : cap * 2) > SIZE_MAX / size) {
        fail(parser, PDF_ERROR_RESOURCE_LIMIT, "operand capacity overflow");
        return 0;
    }
    return 1;
}

static pdf_object *parse_operand(content_parser *parser, size_t depth);

static pdf_object *parse_container(content_parser *parser, size_t depth, int dict) {
    if (depth >= parser->limits->max_nesting_depth) {
        fail(parser, PDF_ERROR_RESOURCE_LIMIT, "operand nesting exceeds configured limit");
        return NULL;
    }
    pdf_object *container = dict ? pdf_object_new_dict() : pdf_object_new_array();
    if (container == NULL) {
        fail(parser, PDF_ERROR_OUT_OF_MEMORY, "could not allocate operand container");
        return NULL;
    }
    pdf_token_type closing = dict ? PDF_TOKEN_DICT_END : PDF_TOKEN_ARRAY_END;
    advance(parser);
    size_t entries = 0;
    while (parser->error->code == PDF_ERROR_NONE && parser->token.type != closing) {
        if (parser->token.type == PDF_TOKEN_EOF) {
            fail(parser, PDF_ERROR_MALFORMED, "unterminated operand container");
            break;
        }
        if (entries >= parser->limits->max_container_entries || entries == SIZE_MAX) {
            fail(parser, PDF_ERROR_RESOURCE_LIMIT, "operand container exceeds entry limit");
            break;
        }
        if (dict ? !entry_capacity(parser, container->value.dict.len,
                       container->value.dict.cap, sizeof(pdf_dict_entry)) :
                   !entry_capacity(parser, container->value.array.len,
                       container->value.array.cap, sizeof(pdf_object *))) break;
        pdf_token key;
        pdf_token_init(&key);
        if (dict) {
            if (parser->token.type != PDF_TOKEN_NAME) {
                fail(parser, PDF_ERROR_MALFORMED, "dictionary key must be a name");
                break;
            }
            pdf_token_move(&key, &parser->token);
            advance(parser);
        }
        pdf_object *item = parse_operand(parser, depth + 1);
        int pushed = item != NULL && (dict ?
            pdf_dict_push_bytes(container, key.bytes.data, key.bytes.len, item) :
            pdf_array_push(container, item));
        pdf_token_destroy(&key);
        if (!pushed) {
            pdf_object_free(item);
            if (parser->error->code == PDF_ERROR_NONE)
                fail(parser, PDF_ERROR_OUT_OF_MEMORY, "could not grow operand container");
            break;
        }
        entries++;
    }
    if (parser->error->code != PDF_ERROR_NONE) {
        pdf_object_free(container);
        return NULL;
    }
    advance(parser);
    return container;
}

static pdf_object *parse_operand(content_parser *parser, size_t depth) {
    if (parser->error->code != PDF_ERROR_NONE) return NULL;
    const pdf_token *t = &parser->token;
    pdf_object *object = NULL;
    switch (t->type) {
        case PDF_TOKEN_ARRAY_BEGIN: return parse_container(parser, depth, 0);
        case PDF_TOKEN_DICT_BEGIN: return parse_container(parser, depth, 1);
        case PDF_TOKEN_NULL: object = pdf_object_new_null(); break;
        case PDF_TOKEN_BOOL: object = pdf_object_new_bool(t->boolean); break;
        case PDF_TOKEN_INT: object = pdf_object_new_int(t->integer); break;
        case PDF_TOKEN_REAL: object = pdf_object_new_real(t->real); break;
        case PDF_TOKEN_NAME: object = pdf_object_new_name_bytes(t->bytes.data, t->bytes.len); break;
        case PDF_TOKEN_STRING: object = pdf_object_new_string_bytes(t->bytes.data, t->bytes.len); break;
        case PDF_TOKEN_HEX_STRING: object = pdf_object_new_hex_string_bytes(t->bytes.data, t->bytes.len); break;
        default:
            fail(parser, PDF_ERROR_MALFORMED, "expected direct content operand");
            return NULL;
    }
    if (object == NULL) fail(parser, PDF_ERROR_OUT_OF_MEMORY, "could not allocate operand");
    else advance(parser);
    return object;
}

typedef struct {
    const char *name;
    pdf_content_operator kind;
    const char *types; /* n number, / name, s byte string, a TJ array. */
    int in_text;
    int shows_text;
} operator_spec;

static const operator_spec operators[] = {
    {"BT", PDF_CONTENT_BT, "", 0, 0},
    {"ET", PDF_CONTENT_ET, "", 1, 0},
    {"q", PDF_CONTENT_SAVE, "", 0, 0},
    {"Q", PDF_CONTENT_RESTORE, "", 0, 0},
    {"Tf", PDF_CONTENT_TF, "/n", 0, 0},
    {"Tm", PDF_CONTENT_TM, "nnnnnn", 1, 0},
    {"Td", PDF_CONTENT_TD, "nn", 1, 0},
    {"TD", PDF_CONTENT_TD_LEADING, "nn", 1, 0},
    {"T*", PDF_CONTENT_NEXT_LINE, "", 1, 0},
    {"Tj", PDF_CONTENT_TJ, "s", 1, 1},
    {"TJ", PDF_CONTENT_TJ_ARRAY, "a", 1, 1},
    {"'", PDF_CONTENT_QUOTE, "s", 1, 1},
    {"\"", PDF_CONTENT_DOUBLE_QUOTE, "nns", 1, 1},
    {"cm", PDF_CONTENT_CM, "nnnnnn", 0, 0}
};

static int in_names(const char *name, const char *const *names, size_t count) {
    for (size_t i = 0; i < count; i++) if (strcmp(name, names[i]) == 0) return 1;
    return 0;
}

static int is_number(const pdf_object *object) {
    return object->type == PDF_OBJECT_INT || object->type == PDF_OBJECT_REAL;
}

static int is_string(const pdf_object *object) {
    return object->type == PDF_OBJECT_STRING || object->type == PDF_OBJECT_HEX_STRING;
}

static int matches_type(const pdf_object *object, char type) {
    switch (type) {
        case 'n': return is_number(object);
        case '/': return object->type == PDF_OBJECT_NAME;
        case 's': return is_string(object);
        case 'a':
            if (object->type != PDF_OBJECT_ARRAY) return 0;
            for (size_t i = 0; i < object->value.array.len; i++) {
                const pdf_object *item = object->value.array.items[i];
                if (!is_number(item) && !is_string(item)) return 0;
            }
            return 1;
        default: return 0;
    }
}

static int dispatch(content_parser *parser, pdf_object *args, int *in_text,
                     size_t *saved, pdf_content_visitor visitor,
                     pdf_content_warning warning, void *context,
                     pdf_content_result *result) {
    const char *name = parser->token.text;
    const char *const malformed[] = {"R", "obj", "endobj", "stream", "endstream", "ID", "EI"};
    const char *const unsupported[] = {"BI", "Do", "gs", "Tc", "Tw", "Tz", "TL", "Tr", "Ts", "d0", "d1",
        "w", "J", "j", "M", "d", "ri", "i", "m", "l", "c", "v", "y", "h", "re",
        "S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n", "W", "W*",
        "CS", "cs", "SC", "SCN", "sc", "scn", "G", "g", "RG", "rg", "K", "k", "sh",
        "MP", "DP", "BMC", "BDC", "EMC", "BX", "EX"};
    if (in_names(name, malformed, sizeof(malformed)/sizeof(malformed[0]))) {
        fail(parser, PDF_ERROR_MALFORMED, "invalid content operator or indirect syntax");
        return 0;
    }
    if (in_names(name, unsupported, sizeof(unsupported)/sizeof(unsupported[0]))) {
        fail(parser, PDF_ERROR_UNSUPPORTED, "content operator is not supported");
        return 0;
    }
    const operator_spec *spec = NULL;
    for (size_t i = 0; i < sizeof(operators)/sizeof(operators[0]); i++) {
        if (strcmp(name, operators[i].name) == 0) { spec = &operators[i]; break; }
    }
    if (spec == NULL) {
        if (warning != NULL) warning(context, name, parser->token.offset);
        return 1;
    }
    if (args->value.array.len != strlen(spec->types)) {
        fail(parser, PDF_ERROR_MALFORMED, "wrong operator operand count");
        return 0;
    }
    for (size_t i = 0; i < args->value.array.len; i++) {
        if (!matches_type(args->value.array.items[i], spec->types[i])) {
            fail(parser, PDF_ERROR_MALFORMED, "wrong operator operand type");
            return 0;
        }
    }
    if (spec->in_text && !*in_text) {
        fail(parser, PDF_ERROR_MALFORMED, "operator requires BT/ET text object");
        return 0;
    }
    if (*in_text && (spec->kind == PDF_CONTENT_SAVE ||
                     spec->kind == PDF_CONTENT_RESTORE || spec->kind == PDF_CONTENT_CM)) {
        fail(parser, PDF_ERROR_MALFORMED, "graphics state operator is not allowed inside BT/ET");
        return 0;
    }
    switch (spec->kind) {
        case PDF_CONTENT_BT:
            if (*in_text) { fail(parser, PDF_ERROR_MALFORMED, "nested BT text object"); return 0; }
            *in_text = 1;
            break;
        case PDF_CONTENT_ET: *in_text = 0; break;
        case PDF_CONTENT_SAVE:
            if (*saved >= parser->limits->max_nesting_depth || *saved == SIZE_MAX) {
                fail(parser, PDF_ERROR_RESOURCE_LIMIT, "q stack exceeds configured depth"); return 0;
            }
            (*saved)++;
            break;
        case PDF_CONTENT_RESTORE:
            if (*saved == 0) { fail(parser, PDF_ERROR_MALFORMED, "Q without matching q"); return 0; }
            (*saved)--;
            break;
        default: break;
    }
    if (result->operations == SIZE_MAX || (spec->shows_text && result->text_shows == SIZE_MAX)) {
        fail(parser, PDF_ERROR_RESOURCE_LIMIT, "operation count overflow");
        return 0;
    }
    pdf_content_operation op = {spec->kind, parser->token.offset,
        (const pdf_object *const *)args->value.array.items, args->value.array.len};
    if (visitor != NULL) {
        int accepted = visitor(context, &op, parser->error);
        if (!accepted || parser->error->code != PDF_ERROR_NONE) {
            fail(parser, PDF_ERROR_MALFORMED, "content visitor aborted");
            parser->error->offset = op.offset;
            return 0;
        }
    }
    result->operations++;
    if (spec->shows_text) result->text_shows++;
    return 1;
}

int pdf_content_interpret(const unsigned char *data, size_t len,
                           const pdf_limits *limits, size_t file_offset,
                           pdf_content_visitor visitor, pdf_content_warning warning,
                           void *context, pdf_content_result *result, pdf_error *error) {
    if (result != NULL) *result = (pdf_content_result){0};
    if (result == NULL || error == NULL || (data == NULL && len != 0)) {
        pdf_error_set(error, PDF_ERROR_IO, file_offset, "content", "invalid content input");
        return 0;
    }
    if (error->code != PDF_ERROR_NONE) return 0;
    pdf_limits defaults;
    if (limits == NULL) { pdf_limits_default(&defaults); limits = &defaults; }
    pdf_error decoded_error;
    pdf_error_init(&decoded_error);
    content_parser parser = {0};
    parser.error = &decoded_error;
    parser.limits = limits;
    pdf_token_init(&parser.token);
    pdf_content_lexer_init(&parser.lexer, data, len, limits, &decoded_error);
    pdf_object *args = pdf_object_new_array();
    int in_text = 0;
    size_t saved = 0;
    if (args == NULL) fail(&parser, PDF_ERROR_OUT_OF_MEMORY, "could not allocate operands");
    else advance(&parser);
    while (decoded_error.code == PDF_ERROR_NONE && parser.token.type != PDF_TOKEN_EOF) {
        if (parser.token.type == PDF_TOKEN_KEYWORD) {
            if (!dispatch(&parser, args, &in_text, &saved, visitor, warning, context, result)) break;
            for (size_t i = 0; i < args->value.array.len; i++) pdf_object_free(args->value.array.items[i]);
            args->value.array.len = 0;
            advance(&parser);
        } else {
            if (args->value.array.len >= limits->max_container_entries) {
                fail(&parser, PDF_ERROR_RESOURCE_LIMIT, "operand stack exceeds entry limit");
                break;
            }
            if (!entry_capacity(&parser, args->value.array.len,
                                 args->value.array.cap, sizeof(pdf_object *))) break;
            pdf_object *operand = parse_operand(&parser, 0);
            if (operand == NULL) break;
            if (!pdf_array_push(args, operand)) {
                pdf_object_free(operand);
                fail(&parser, PDF_ERROR_OUT_OF_MEMORY, "could not grow operand stack");
            }
        }
    }
    if (decoded_error.code == PDF_ERROR_NONE && (in_text || saved != 0 || args->value.array.len != 0))
        fail(&parser, PDF_ERROR_MALFORMED, "unfinished text object, q stack, or operands at end of content");
    pdf_token_destroy(&parser.token);
    pdf_object_free(args);
    if (decoded_error.code != PDF_ERROR_NONE) {
        *result = (pdf_content_result){0, 0, decoded_error.offset};
        pdf_error_set(error, decoded_error.code, file_offset, "content",
                      "decoded byte %zu: %s", decoded_error.offset, decoded_error.message);
        return 0;
    }
    return 1;
}
