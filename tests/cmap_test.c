#include "../src/cmap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define START "begincmap 1 begincodespacerange <0000> <FFFF> endcodespacerange "
#define END " endcmap"

static pdf_cmap *parse(const char *s, const pdf_limits *limits, pdf_error *e) {
    pdf_error_init(e);
    return pdf_cmap_parse((const unsigned char *)s, strlen(s), 2, limits, 123, e);
}
static void text_is(pdf_cmap *m, unsigned code, const char *expected) {
    const pdf_bytes *v = pdf_cmap_lookup(m, code);
    assert(v && v->len == strlen(expected) && memcmp(v->data, expected, v->len) == 0);
}
static void mutated_inputs(void) {
    const char *seed = START "1 beginbfchar <0001> <4E2D> endbfchar" END;
    unsigned state = 0xC1D123u;
    unsigned char input[256];
    size_t len = strlen(seed);
    for (size_t i = 0; i < 3000; i++) {
        memcpy(input, seed, len);
        for (size_t j = 0; j < 1 + i % 5; j++) {
            state = state * 1664525u + 1013904223u;
            size_t at = state % len;
            state = state * 1664525u + 1013904223u;
            input[at] = (unsigned char)(state >> 24);
        }
        pdf_error e;
        pdf_error_init(&e);
        pdf_cmap *m = pdf_cmap_parse(input, i % 7 ? len : i % len, 2, NULL, 123, &e);
        assert(m ? e.code == PDF_ERROR_NONE : e.code != PDF_ERROR_NONE);
        pdf_cmap_free(m);
    }
}
int main(void) {
    pdf_error e;
    pdf_cmap *m = parse(START "2 beginbfchar <0001> <4E2D> <0002> <D840DC00> endbfchar "
        "2 beginbfrange <0003> <0004> <0041> <0005> <0006> [<4F60597D> <6587>] endbfrange" END, NULL, &e);
    assert(m);
    text_is(m, 1, "中"); text_is(m, 2, "𠀀"); text_is(m, 3, "A");
    text_is(m, 4, "B"); text_is(m, 5, "你好"); text_is(m, 6, "文");
    assert(!pdf_cmap_lookup(m, 65535));
    pdf_cmap_free(m);
    m = parse("begincmap 2 begincodespacerange <0000> <00FF> <0100> <01FF> endcodespacerange "
              "0 beginbfchar endbfchar 1 beginbfchar <0101> <00000041FFFD> endbfchar endcmap", NULL, &e);
    assert(m);
    const pdf_bytes *nul = pdf_cmap_lookup(m, 257);
    const unsigned char expected[] = {0, 'A', 0xEF, 0xBF, 0xBD};
    assert(nul && nul->len == sizeof(expected) && memcmp(nul->data, expected, sizeof(expected)) == 0);
    pdf_cmap_free(m);
    const char *invalid[] = {
        "", "begincmap endcmap", START "1 beginbfchar <0001> <4E2D> endbfchar",
        "begincmap 0 begincodespacerange endcodespacerange endcmap",
        START "<0001> <0041>" END,
        START "2 beginbfchar <0001> <4E2D> endbfchar" END,
        START "0 beginbfchar <0001> <4E2D> endbfchar" END,
        START "1 beginbfchar <01> <4E2D> endbfchar" END,
        START "1 beginbfchar <0001> <> endbfchar" END,
        START "1 beginbfchar <0001> <41> endbfchar" END,
        START "1 beginbfchar <0001> <DC00> endbfchar" END,
        START "1 beginbfchar <0001> <D840> endbfchar" END,
        START "1 beginbfchar <0001> <D8400041> endbfchar" END,
        START "1 beginbfrange <0002> <0001> <0041> endbfrange" END,
        START "1 beginbfrange <0001> <0002> [<0041>] endbfrange" END,
        START "1 beginbfrange <0001> <0002> [<0041> <0042> <0043>] endbfrange" END,
        START "1 beginbfrange <0001> <0002> <FFFF> endbfrange" END,
        START "1 beginbfrange <0001> <0002> <00FF> endbfrange" END,
        START "2 beginbfchar <0001> <0041> <0001> <0042> endbfchar" END,
        "begincmap 1 begincodespacerange <0001> <0002> endcodespacerange "
            "1 beginbfchar <0003> <0041> endbfchar endcmap",
        START "endbfchar" END,
        START "-1 beginbfchar endbfchar" END,
        START "begincmap endcmap" END,
        "begincmap 2 begincodespacerange <0000> <FFFF> <0000> <FFFF> endcodespacerange endcmap",
        START "<<" END,
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        m = parse(invalid[i], NULL, &e);
        if (m) fprintf(stderr, "accepted malformed CMap case %zu\n", i);
        assert(!m && e.code == PDF_ERROR_MALFORMED && e.offset == 123);
    }
    assert(!parse(START "/Base usecmap" END, NULL, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED);
    assert(!parse(START "custommapping" END, NULL, &e) && e.code == PDF_ERROR_UNSUPPORTED);
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_container_entries = 2;
    assert(!parse(START "1 beginbfrange <0001> <FFFF> <0041> endbfrange" END, &limits, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    limits.max_container_entries = 100;
    limits.max_token_size = 20; /* 16 UTF-16 bytes expand to 24 UTF-8 bytes. */
    assert(!parse(START "1 beginbfchar <0001> <4E2D4E2D4E2D4E2D4E2D4E2D4E2D4E2D> endbfchar" END, &limits, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && strstr(e.message, "destination"));
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 1;
    assert(!parse(START END, &limits, &e) && e.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_limits_default(&limits);
    limits.max_nesting_depth = 1;
    assert(!parse(START "<< << >> >>" END, &limits, &e) && e.code == PDF_ERROR_RESOURCE_LIMIT);
    mutated_inputs();
    puts("CMap tests passed (including 3000 mutated inputs)");
    return 0;
}
