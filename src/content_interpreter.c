#include "content_interpreter.h"
#include "content_lexer.h"

#include <math.h>
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
    const char *types; /* n number, / name, s byte string, a TJ array, p properties;
                       * c variable color operands: numbers, optional final name. */
    int in_text; /* 1 requires BT; -1 forbids BT; 0 permits either. */
    int shows_text;
} operator_spec;

static const operator_spec operators[] = {
    {"w", PDF_CONTENT_LINE_WIDTH, "n", 0, 0},
    {"J", PDF_CONTENT_LINE_CAP, "i", 0, 0},
    {"j", PDF_CONTENT_LINE_JOIN, "i", 0, 0},
    {"M", PDF_CONTENT_MITER_LIMIT, "n", 0, 0},
    {"d", PDF_CONTENT_DASH, "Dn", 0, 0},
    {"m", PDF_CONTENT_MOVE, "nn", -1, 0},
    {"l", PDF_CONTENT_LINE, "nn", -1, 0},
    {"c", PDF_CONTENT_CURVE, "nnnnnn", -1, 0},
    {"h", PDF_CONTENT_CLOSE, "", -1, 0},
    {"re", PDF_CONTENT_RECT, "nnnn", -1, 0},
    {"W", PDF_CONTENT_CLIP, "", -1, 0},
    {"W*", PDF_CONTENT_CLIP_EVEN_ODD, "", -1, 0},
    {"n", PDF_CONTENT_END_PATH, "", -1, 0},
    {"S", PDF_CONTENT_STROKE, "", -1, 0},
    {"f", PDF_CONTENT_FILL, "", -1, 0},
    {"B*", PDF_CONTENT_FILL_STROKE_EVEN_ODD, "", -1, 0},
    {"Do", PDF_CONTENT_DO, "/", -1, 0},
    {"BDC", PDF_CONTENT_BDC, "/p", 0, 0},
    {"EMC", PDF_CONTENT_EMC, "", 0, 0},
    {"BT", PDF_CONTENT_BT, "", 0, 0},
    {"gs", PDF_CONTENT_GS, "/", 0, 0},
    {"cs", PDF_CONTENT_CS, "/", 0, 0},
    {"CS", PDF_CONTENT_CS_STROKE, "/", 0, 0},
    {"scn", PDF_CONTENT_SCN, "c", 0, 0},
    {"SCN", PDF_CONTENT_SCN_STROKE, "c", 0, 0},
    {"ET", PDF_CONTENT_ET, "", 1, 0},
    {"q", PDF_CONTENT_SAVE, "", 0, 0},
    {"Q", PDF_CONTENT_RESTORE, "", 0, 0},
    {"Tc", PDF_CONTENT_TC, "n", 0, 0},
    {"Tw", PDF_CONTENT_TW, "n", 0, 0},
    {"Tz", PDF_CONTENT_TZ, "n", 0, 0},
    {"TL", PDF_CONTENT_TL, "n", 0, 0},
    {"Ts", PDF_CONTENT_TS, "n", 0, 0},
    {"Tr", PDF_CONTENT_TR, "i", 0, 0},
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

static double number(const pdf_object *o) {
    return o->type == PDF_OBJECT_INT ? (double)o->value.integer : o->value.real;
}

static int is_string(const pdf_object *object) {
    return object->type == PDF_OBJECT_STRING || object->type == PDF_OBJECT_HEX_STRING;
}

static int matches_type(const pdf_object *object, char type) {
    switch (type) {
        case 'i': return object->type == PDF_OBJECT_INT;
        case 'n': return is_number(object);
        case '/': return object->type == PDF_OBJECT_NAME;
        case 's': return is_string(object);
        case 'p': return object->type == PDF_OBJECT_DICT || object->type == PDF_OBJECT_NAME;
        case 'D': {
            if (object->type != PDF_OBJECT_ARRAY) return 0;
            int positive = 0;
            for (size_t i = 0; i < object->value.array.len; i++) {
                const pdf_object *v = object->value.array.items[i];
                if (!is_number(v) || !isfinite(number(v)) || number(v) < 0) return 0;
                if (number(v) > 0) positive = 1;
            }
            return positive || object->value.array.len == 0;
        }
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

typedef struct {
    int in_text;
    int current_point; /* The current path is not part of the q/Q stack. */
    /* BT cannot nest. Its entry marked depth is sufficient to forbid EMC
     * closing an outer BDC, and ET closing across an inner BDC. q is separate. */
    size_t saved, marked_depth, text_marked_depth;
} content_scope;

static int dispatch(content_parser *parser, pdf_object *args, content_scope *scope,
                     pdf_content_visitor visitor,
                     pdf_content_warning warning, void *context,
                     pdf_content_result *result) {
    const char *name = parser->token.text;
    const char *const malformed[] = {"R", "obj", "endobj", "stream", "endstream", "ID", "EI"};
    const char *const unsupported[] = {"BI", "d0", "d1", "ri", "i", "v", "y",
        "s", "F", "f*", "B", "b", "b*",
        "SC", "sc", "G", "g", "RG", "rg", "K", "k", "sh",
        "MP", "DP", "BMC", "BX", "EX"};
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
    int color = spec->types[0] == 'c';
    size_t count = args->value.array.len;
    if (color ? count == 0 : count != strlen(spec->types)) {
        fail(parser, PDF_ERROR_MALFORMED, "wrong operator operand count");
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        const pdf_object *arg = args->value.array.items[i];
        int valid = color ? is_number(arg) || (i == count - 1 && arg->type == PDF_OBJECT_NAME)
                          : matches_type(arg, spec->types[i]);
        if (!valid) {
            fail(parser, PDF_ERROR_MALFORMED, "wrong operator operand type");
            return 0;
        }
        if (is_number(arg) && !isfinite(number(arg))) {
            fail(parser, PDF_ERROR_MALFORMED, "operator number must be finite"); return 0;
        }
    }
    if ((spec->kind == PDF_CONTENT_LINE_WIDTH && number(args->value.array.items[0]) < 0) ||
        (spec->kind == PDF_CONTENT_MITER_LIMIT && number(args->value.array.items[0]) < 1) ||
        ((spec->kind == PDF_CONTENT_LINE_CAP || spec->kind == PDF_CONTENT_LINE_JOIN) &&
         (args->value.array.items[0]->value.integer < 0 || args->value.array.items[0]->value.integer > 2))) {
        fail(parser, PDF_ERROR_MALFORMED, "line style operand out of range"); return 0;
    }
    if (spec->kind == PDF_CONTENT_DASH && args->value.array.items[0]->value.array.len == 0 &&
        number(args->value.array.items[1]) != 0) {
        fail(parser, PDF_ERROR_MALFORMED, "empty dash array requires zero phase"); return 0;
    }
    if (spec->kind == PDF_CONTENT_TR) {
        int64_t mode = args->value.array.items[0]->value.integer;
        if (mode < 0 || mode > 7) {
            fail(parser, PDF_ERROR_MALFORMED, "Tr must be an integer from 0 to 7"); return 0;
        }
        if (mode >= 4) {
            fail(parser, PDF_ERROR_UNSUPPORTED, "glyph clipping is unsupported"); return 0;
        }
    }
    if (spec->in_text > 0 && !scope->in_text) {
        fail(parser, PDF_ERROR_MALFORMED, "operator requires BT/ET text object");
        return 0;
    }
    if (spec->in_text < 0 && scope->in_text) {
        fail(parser, PDF_ERROR_MALFORMED, "drawing operator is not allowed inside BT/ET"); return 0;
    }
    if (scope->in_text && (spec->kind == PDF_CONTENT_SAVE ||
                     spec->kind == PDF_CONTENT_RESTORE || spec->kind == PDF_CONTENT_CM)) {
        fail(parser, PDF_ERROR_MALFORMED, "graphics state operator is not allowed inside BT/ET");
        return 0;
    }
    switch (spec->kind) {
        case PDF_CONTENT_MOVE: case PDF_CONTENT_RECT:
            scope->current_point = 1;
            break;
        case PDF_CONTENT_LINE: case PDF_CONTENT_CURVE: case PDF_CONTENT_CLOSE:
            if (!scope->current_point) {
                fail(parser, PDF_ERROR_MALFORMED, "path operator requires a current point"); return 0;
            }
            break;
        case PDF_CONTENT_END_PATH: case PDF_CONTENT_STROKE:
        case PDF_CONTENT_FILL: case PDF_CONTENT_FILL_STROKE_EVEN_ODD:
            scope->current_point = 0;
            break;
        case PDF_CONTENT_BT:
            if (scope->in_text) { fail(parser, PDF_ERROR_MALFORMED, "nested BT text object"); return 0; }
            scope->in_text = 1;
            scope->text_marked_depth = scope->marked_depth;
            break;
        case PDF_CONTENT_ET:
            if (scope->marked_depth != scope->text_marked_depth) {
                fail(parser, PDF_ERROR_MALFORMED, "ET crosses marked-content scope"); return 0;
            }
            scope->in_text = 0;
            break;
        case PDF_CONTENT_BDC:
            if (scope->marked_depth >= parser->limits->max_nesting_depth || scope->marked_depth == SIZE_MAX) {
                fail(parser, PDF_ERROR_RESOURCE_LIMIT, "marked-content exceeds configured depth"); return 0;
            }
            scope->marked_depth++;
            break;
        case PDF_CONTENT_EMC:
            if (scope->marked_depth == 0) {
                fail(parser, PDF_ERROR_MALFORMED, "EMC without matching BDC"); return 0;
            }
            if (scope->in_text && scope->marked_depth <= scope->text_marked_depth) {
                fail(parser, PDF_ERROR_MALFORMED, "EMC crosses BT/ET text object"); return 0;
            }
            scope->marked_depth--;
            break;
        case PDF_CONTENT_SAVE:
            if (scope->saved >= parser->limits->max_nesting_depth || scope->saved == SIZE_MAX) {
                fail(parser, PDF_ERROR_RESOURCE_LIMIT, "q stack exceeds configured depth"); return 0;
            }
            scope->saved++;
            break;
        case PDF_CONTENT_RESTORE:
            if (scope->saved == 0) { fail(parser, PDF_ERROR_MALFORMED, "Q without matching q"); return 0; }
            scope->saved--;
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
    content_scope scope = {0};
    if (args == NULL) fail(&parser, PDF_ERROR_OUT_OF_MEMORY, "could not allocate operands");
    else advance(&parser);
    while (decoded_error.code == PDF_ERROR_NONE && parser.token.type != PDF_TOKEN_EOF) {
        if (parser.token.type == PDF_TOKEN_KEYWORD) {
            if (!dispatch(&parser, args, &scope, visitor, warning, context, result)) break;
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
    if (decoded_error.code == PDF_ERROR_NONE && scope.marked_depth != 0)
        fail(&parser, PDF_ERROR_MALFORMED, "unfinished marked-content scope at end of content");
    if (decoded_error.code == PDF_ERROR_NONE && (scope.in_text || scope.saved != 0 || args->value.array.len != 0))
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
