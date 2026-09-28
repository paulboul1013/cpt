#include "../src/content_interpreter.h"
#include <assert.h>
#include <string.h>

static int count(void *ctx, const pdf_content_operation *op, pdf_error *error) {
    (void)error;
    assert(op->operand_count == 0);
    (*(size_t *)ctx)++;
    return 1;
}
static int inspect(void *ctx, const pdf_content_operation *op, pdf_error *error) {
    (void)error;
    size_t *calls = ctx;
    (*calls)++;
    if (op->kind == PDF_CONTENT_TJ_ARRAY) {
        const pdf_array *a = &op->operands[0]->value.array;
        assert(a->len == 4);
        assert(a->items[0]->value.string.len == 2);
        assert(memcmp(a->items[0]->value.string.data, "A\0", 2) == 0);
        assert(a->items[1]->value.integer == -120);
        assert(a->items[2]->value.hex_string.len == 1);
        assert(a->items[2]->value.hex_string.data[0] == 0xff);
        assert(a->items[3]->value.string.len == 1);
    }
    if (op->kind == PDF_CONTENT_TF) {
        assert(op->operand_count == 2);
        assert(op->operands[0]->type == PDF_OBJECT_NAME);
        assert(op->operands[1]->value.integer == 12);
    }
    if (op->kind == PDF_CONTENT_DOUBLE_QUOTE) {
        assert(op->operands[0]->value.integer == 2);
        assert(op->operands[1]->value.real == 0.5);
        assert(op->operands[2]->value.string.len == 1);
    }
    return 1;
}

static void typed_operations(void) {
    const char *input = "/F1 12 Tf q 1 0 0 1 2 3 cm BT 1 0 0 1 10 20 Tm "
        "1 2 Td 3 4 TD T* (x) Tj [(A\\000) -120 <ff> (B)] TJ (x) ' 2 .5 (z) \" ET Q";
    pdf_error error;
    pdf_error_init(&error);
    pdf_content_result result;
    size_t calls = 0;
    assert(pdf_content_interpret((const unsigned char *)input, strlen(input), NULL,
        123, inspect, NULL, &calls, &result, &error));
    assert(result.operations == 14 && calls == 14 && result.text_shows == 4);
    const char *bad[] = {"/F Tf", "/F 12 1 Tf", "12 12 Tf", "BT 1 2 Tm ET",
        "BT 1 2 3 4 5 (x) Tm ET", "BT /X Tj ET", "BT [(x) /X] TJ ET",
        "BT [true] TJ ET", "BT [[(x)]] TJ ET", "BT 0 (x) \" ET", "BT () () (x) \" ET",
        "1 2 Td", "1 2 TD", "T*", "(x) Tj", "[] TJ", "(x) '", "0 0 (x) \"",
        "1 0 0 1 0 0 Tm", "1 2 cm", "BT 1 T* ET"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        pdf_error_clear(&error);
        assert(!pdf_content_interpret((const unsigned char *)bad[i], strlen(bad[i]),
            NULL, 123, NULL, NULL, NULL, &result, &error));
        assert(error.code == PDF_ERROR_MALFORMED && result.operations == 0);
    }
}

static int abort_visit(void *ctx, const pdf_content_operation *op, pdf_error *error) {
    (*(size_t *)ctx)++;
    if (op->kind == PDF_CONTENT_TJ_ARRAY) {
        pdf_error_set(error, PDF_ERROR_UNSUPPORTED, 999, "consumer", "consumer stopped");
        return 0;
    }
    return 1;
}

static int stop_without_error(void *ctx, const pdf_content_operation *op, pdf_error *error) {
    (void)ctx; (void)op; (void)error;
    return 0;
}

static void warning(void *ctx, const char *name, size_t offset) {
    assert(strcmp(name, "mystery") == 0);
    assert(offset == 25);
    (*(size_t *)ctx)++;
}

static void limits_and_cleanup(void) {
    pdf_limits limits;
    pdf_limits_default(&limits);
    pdf_error error;
    pdf_content_result result;
    struct { const char *text; size_t entries, depth, token; pdf_error_code code; } cases[] = {
        {"1 2 3 mystery", 2, 10, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"[1 2 3] mystery", 2, 10, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"<< /A 1 /A 2 /A 3 >> mystery", 2, 10, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"[[[]]] mystery", 10, 2, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"<< /A [<<>>] >> mystery", 10, 2, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"q q q", 10, 2, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"(abcd) mystery", 10, 10, 3, PDF_ERROR_RESOURCE_LIMIT},
        {"/abcd mystery", 10, 10, 3, PDF_ERROR_RESOURCE_LIMIT},
        {"1234 mystery", 10, 10, 3, PDF_ERROR_RESOURCE_LIMIT},
        {"mystery", 10, 10, 3, PDF_ERROR_RESOURCE_LIMIT},
        {"(((a))) mystery", 10, 2, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"[[]] mystery q q Q Q", 10, 2, 20, PDF_ERROR_NONE},
        {"1 2 mystery", 2, 2, 20, PDF_ERROR_NONE},
        {"BT ET", 0, 0, 20, PDF_ERROR_NONE},
        {"[] mystery", 10, 0, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"q", 10, 0, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"() mystery", 10, 0, 20, PDF_ERROR_RESOURCE_LIMIT},
        {"<< /A [1 2 R] >> mystery", 10, 10, 20, PDF_ERROR_MALFORMED},
        {"[<< /A (x) /B >>] mystery", 10, 10, 20, PDF_ERROR_MALFORMED},
        {"[1 2 >>", 10, 10, 20, PDF_ERROR_MALFORMED},
        {"true null false mystery BT ET", 10, 10, 20, PDF_ERROR_NONE},
        {"BT q Q ET", 10, 10, 20, PDF_ERROR_MALFORMED},
        {"q BT Q ET", 10, 10, 20, PDF_ERROR_MALFORMED},
        {"BT 1 0 0 1 0 0 cm ET", 10, 10, 20, PDF_ERROR_MALFORMED}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        pdf_error_init(&error);
        limits.max_container_entries = cases[i].entries;
        limits.max_nesting_depth = cases[i].depth;
        limits.max_token_size = cases[i].token;
        int ok = pdf_content_interpret((const unsigned char *)cases[i].text,
            strlen(cases[i].text), &limits, 333, NULL, NULL, NULL, &result, &error);
        assert(ok == (cases[i].code == PDF_ERROR_NONE));
        assert(error.code == cases[i].code);
        if (!ok) assert(result.operations == 0 && result.text_shows == 0);
    }
    const char *unsupported[] = {"BI", "/F Do", "/G gs", "4 Tr", "7 Tr", "0 0 d0", "0 0 0 0 0 0 d1", "/Span << /ActualText (x) >> BDC",
        "W", "W*", "0 0 m", "0 g", "BX", "EX", "/P MP"};
    for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
        pdf_error_init(&error);
        assert(!pdf_content_interpret((const unsigned char *)unsupported[i], strlen(unsupported[i]),
            NULL, 333, NULL, NULL, NULL, &result, &error));
        assert(error.code == PDF_ERROR_UNSUPPORTED);
    }
    const char *input = "BT [(A) <0042> -12] TJ ET";
    size_t calls = 0;
    pdf_error_init(&error);
    assert(!pdf_content_interpret((const unsigned char *)input, strlen(input), NULL,
        333, abort_visit, NULL, &calls, &result, &error));
    assert(calls == 2 && result.operations == 0 && result.error_offset == 20);
    assert(error.code == PDF_ERROR_UNSUPPORTED && error.offset == 333);
    assert(strstr(error.message, "consumer stopped") != NULL);
    pdf_error_clear(&error);
    assert(!pdf_content_interpret((const unsigned char *)"BT ET", 5, NULL, 333,
        stop_without_error, NULL, NULL, &result, &error));
    assert(error.code == PDF_ERROR_MALFORMED);
    pdf_error_clear(&error);
    input = "<< /A [null true (x)] >> mystery BT ET";
    calls = 0;
    assert(pdf_content_interpret((const unsigned char *)input, strlen(input), NULL,
        333, NULL, warning, &calls, &result, &error));
    assert(calls == 1 && result.operations == 2);
    /* Repeated independent calls must not retain BT, q, or operand state. */
    for (size_t i = 0; i < 200; i++) {
        pdf_error_clear(&error);
        assert(!pdf_content_interpret((const unsigned char *)"q BT [(a) /X] TJ", 15,
            NULL, 333, NULL, NULL, NULL, &result, &error));
        pdf_error_clear(&error);
        assert(pdf_content_interpret(NULL, 0, NULL, 333, NULL, NULL, NULL, &result, &error));
    }
}

int main(void) {
    pdf_error e; pdf_content_result r;
    const char *settings="1 Tc 2 Tw -80 Tz 18 TL 4 Ts 3 Tr BT 0 Tr ET";
    pdf_error_init(&e);
    assert(pdf_content_interpret((const unsigned char *)settings,strlen(settings),NULL,
        10,NULL,NULL,NULL,&r,&e));
    const char *bad_settings[]={"Tc","1 2 Tw","/F Tz","() TL","[] Ts","1.0 Tr","-1 Tr","8 Tr"};
    for(size_t i=0;i<sizeof(bad_settings)/sizeof(*bad_settings);i++) {
        pdf_error_clear(&e);
        assert(!pdf_content_interpret((const unsigned char *)bad_settings[i],strlen(bad_settings[i]),
            NULL,10,NULL,NULL,NULL,&r,&e));
        assert(e.code==PDF_ERROR_MALFORMED);
    }
    for(int mode=0;mode<=7;mode++) {
        char input[16]; snprintf(input,sizeof(input),"%d Tr",mode);
        pdf_error_clear(&e);
        int ok=pdf_content_interpret((const unsigned char *)input,strlen(input),NULL,10,
                                    NULL,NULL,NULL,&r,&e);
        assert(ok==(mode<4));
        assert(e.code==(mode<4?PDF_ERROR_NONE:PDF_ERROR_UNSUPPORTED));
    }
    typed_operations();
    limits_and_cleanup();
    const char *inputs[] = {"", "BT ET q Q", "BT BT", "ET", "BT", "Q", "q", "12 0 R", "BI", "ID", "EI", "1 mystery BT ET", "1", "[1", "<< /A >>"};
    const pdf_error_code codes[] = {0,0,PDF_ERROR_MALFORMED,PDF_ERROR_MALFORMED,
        PDF_ERROR_MALFORMED,PDF_ERROR_MALFORMED,PDF_ERROR_MALFORMED,
        PDF_ERROR_MALFORMED,PDF_ERROR_UNSUPPORTED,PDF_ERROR_MALFORMED,
        PDF_ERROR_MALFORMED,0,PDF_ERROR_MALFORMED,PDF_ERROR_MALFORMED,PDF_ERROR_MALFORMED};
    for (size_t i = 0; i < sizeof(inputs)/sizeof(inputs[0]); i++) {
        pdf_error error;
        pdf_error_init(&error);
        pdf_content_result result;
        size_t calls = 0;
        int ok = pdf_content_interpret((const unsigned char *)inputs[i], strlen(inputs[i]),
            NULL, 123, count, NULL, &calls, &result, &error);
        assert(ok == (codes[i] == PDF_ERROR_NONE));
        assert(error.code == codes[i]);
        if (!ok) { assert(error.offset == 123); assert(strstr(error.message, "decoded byte ") != NULL); assert(result.operations == 0); }
        else assert(result.operations == calls);
        if (i == 2) assert(result.error_offset == 3);
    }
    return 0;
}
