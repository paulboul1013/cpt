#include "pdf_builder.h"
#include "../src/reading_order.h"

/* Synthetic items: exact control over positions, sizes and text. */
typedef struct { const char *text; size_t page; double x, y, width, size; int horizontal; size_t replacements; } spec;

static void make(pdf_text_items *items, const spec *s, size_t n) {
    *items = (pdf_text_items){0};
    items->items = calloc(n ? n : 1, sizeof(*items->items));
    assert(items->items);
    items->len = items->cap = n;
    for (size_t i = 0; i < n; i++) {
        pdf_text_item *t = &items->items[i];
        t->text = (const unsigned char *)s[i].text;
        t->text_len = strlen(s[i].text);
        t->font = (const unsigned char *)"F1"; t->font_len = 2;
        t->page = s[i].page; t->sequence = i; t->source_order = i;
        t->x = s[i].x; t->y = s[i].y; t->width = t->dx = s[i].width;
        t->em_height = t->effective_size = t->font_size = s[i].size;
        t->horizontal = s[i].horizontal; t->replacements = s[i].replacements;
    }
}
static void expect(const spec *s, size_t n, const char *want) {
    pdf_text_items items;
    make(&items, s, n);
    pdf_plain_text text = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(pdf_plain_text_build(&items, NULL, &text, &e));
    if (text.len != strlen(want) || (text.len && memcmp(text.data, want, text.len))) {
        fprintf(stderr, "got [%.*s] want [%s]\n", (int)text.len, text.data ? (char *)text.data : "", want);
        assert(0);
    }
    pdf_plain_text_free(&text);
    assert(text.data == NULL && text.len == 0);
    free(items.items);
}

static void test_order_and_lines(void) {
    /* Case 1: drawn right to left, read left to right, gaps > 3 get spaces. */
    const spec reversed[] = {{"World", 1, 80, 100, 30, 12, 1, 0}, {"PDF", 1, 50, 100, 20, 12, 1, 0},
                             {"Hello", 1, 10, 100, 30, 12, 1, 0}};
    expect(reversed, 3, "Hello PDF World\n");
    /* Case 2: tolerance is max(1.5, 12 * 0.25) = 3: 3 joins, 3.01 splits. */
    const spec join[] = {{"A", 1, 0, 100, 10, 12, 1, 0}, {"B", 1, 10, 97, 10, 12, 1, 0}};
    expect(join, 2, "AB\n");
    const spec split[] = {{"A", 1, 0, 100, 10, 12, 1, 0}, {"B", 1, 10, 96.99, 10, 12, 1, 0}};
    expect(split, 2, "A\nB\n");
    /* Small fonts use the 1.5 floor; min() of the two sizes decides. */
    const spec small[] = {{"a", 1, 0, 100, 4, 4, 1, 0}, {"b", 1, 4, 98.6, 4, 40, 1, 0}};
    expect(small, 2, "ab\n");
    const spec small2[] = {{"a", 1, 0, 100, 4, 4, 1, 0}, {"b", 1, 4, 98.4, 4, 40, 1, 0}};
    expect(small2, 2, "a\nb\n");
    /* Anchor grouping is not chained: each item compares with the line's
     * first item, so a slow drift eventually starts a new line. */
    const spec drift[] = {{"1", 1, 0, 100, 5, 12, 1, 0}, {"2", 1, 5, 98, 5, 12, 1, 0},
                          {"3", 1, 10, 96, 5, 12, 1, 0}, {"4", 1, 15, 94, 5, 12, 1, 0}};
    expect(drift, 4, "12\n34\n");
    /* Lines top to bottom regardless of source order; same x ties by sequence. */
    const spec lines[] = {{"low", 1, 0, 50, 15, 12, 1, 0}, {"high", 1, 0, 200, 20, 12, 1, 0},
                          {"X", 1, 20, 200, 5, 12, 1, 0}, {"Y", 1, 20, 200, 5, 12, 1, 0}};
    expect(lines, 4, "highXY\nlow\n");
    /* Rise within tolerance stays on the line (superscript-like). */
    const spec rise[] = {{"E=mc", 1, 0, 100, 30, 12, 1, 0}, {"2", 1, 30, 102.5, 5, 12, 1, 0}};
    expect(rise, 2, "E=mc2\n");
}

static void test_spaces(void) {
    /* Case 3: an existing space is kept and not doubled. */
    const spec kept[] = {{"Hello ", 1, 0, 100, 36, 12, 1, 0}, {"World", 1, 40, 100, 30, 12, 1, 0}};
    expect(kept, 2, "Hello World\n");
    const spec lead[] = {{"Hello", 1, 0, 100, 30, 12, 1, 0}, {" World", 1, 40, 100, 36, 12, 1, 0}};
    expect(lead, 2, "Hello World\n");
    const spec nbsp[] = {{"A\xC2\xA0", 1, 0, 100, 10, 12, 1, 0}, {"B", 1, 20, 100, 10, 12, 1, 0}};
    expect(nbsp, 2, "A\xC2\xA0" "B\n");
    /* Case 4: threshold 12 * 0.25 = 3 after the previous item. */
    const spec under[] = {{"A", 1, 0, 100, 10, 12, 1, 0}, {"B", 1, 12.9, 100, 10, 12, 1, 0}};
    expect(under, 2, "AB\n");
    const spec equal[] = {{"A", 1, 0, 100, 10, 12, 1, 0}, {"B", 1, 13, 100, 10, 12, 1, 0}};
    expect(equal, 2, "AB\n");
    const spec over[] = {{"A", 1, 0, 100, 10, 12, 1, 0}, {"B", 1, 13.1, 100, 10, 12, 1, 0}};
    expect(over, 2, "A B\n");
    /* Threshold uses the previous item's size: 4pt then 40pt. */
    const spec sizes[] = {{"a", 1, 0, 100, 2, 4, 1, 0}, {"B", 1, 3.5, 100, 20, 40, 1, 0}};
    expect(sizes, 2, "a B\n");
    /* Overlap (negative gap) joins without deleting text. */
    const spec overlap[] = {{"Bold", 1, 0, 100, 30, 12, 1, 0}, {"Bold", 1, 0.3, 100, 30, 12, 1, 0}};
    expect(overlap, 2, "BoldBold\n");
    /* Text is verbatim: soft hyphen, replacement and trailing spaces stay. */
    const spec raw[] = {{"co\xC2\xADop \xEF\xBF\xBD  ", 1, 0, 100, 50, 12, 1, 1}};
    expect(raw, 1, "co\xC2\xADop \xEF\xBF\xBD  \n");
}

static void test_pages_and_empty(void) {
    /* Case 5: page 2 has no items: one blank line between 1 and 3. */
    const spec pages[] = {{"one", 1, 0, 100, 10, 12, 1, 0}, {"two", 1, 0, 80, 10, 12, 1, 0},
                          {"three", 3, 0, 100, 10, 12, 1, 0}};
    expect(pages, 3, "one\ntwo\n\nthree\n");
    expect(NULL, 0, "");
    /* Leading pages without text add no leading blank line. */
    const spec late[] = {{"only", 3, 0, 100, 10, 12, 1, 0}};
    expect(late, 1, "only\n");
    pdf_text_items items;
    make(&items, pages, 3);
    pdf_plain_text text = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(pdf_plain_text_build(&items, NULL, &text, &e));
    assert(text.lines == 3 && text.pages_with_text == 2 && text.replacements == 0);
    pdf_plain_text_free(&text);
    /* Output limit counts inserted spaces and newlines. */
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 14;
    assert(!pdf_plain_text_build(&items, &limits, &text, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && text.data == NULL && text.len == 0);
    pdf_error_init(&e);
    limits.max_total_decoded_size = 15;
    assert(pdf_plain_text_build(&items, &limits, &text, &e) && text.len == 15);
    pdf_plain_text_free(&text);
    /* Line dump is one JSON line per line. */
    pdf_text_lines lines = {0};
    assert(pdf_text_lines_build(&items, &lines, &e) && lines.len == 3);
    char *out = NULL; size_t size = 0;
    FILE *s = open_memstream(&out, &size);
    assert(pdf_text_lines_dump(s, &items, &lines, &e));
    fclose(s);
    assert(strstr(out, "{\"line\":2,\"page\":3,\"anchor_y\":100.000000000,\"anchor_size\":12.000000000,\"items\":[2]}"));
    free(out);
    pdf_text_lines_free(&lines);
    free(items.items);
}

static void test_non_horizontal_fails(void) {
    const spec rotated[] = {{"ok", 1, 0, 100, 10, 12, 1, 0}, {"up", 2, 0, 100, 10, 12, 0, 0}};
    pdf_text_items items;
    make(&items, rotated, 2);
    items.items[1].page_offset = 777;
    items.items[1].offset = 42;
    pdf_plain_text text = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(!pdf_plain_text_build(&items, NULL, &text, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && e.offset == 777 && text.data == NULL);
    assert(strstr(e.message, "page 2 decoded byte 42") && strstr(e.message, "non-horizontal"));
    free(items.items);
}

/* End to end through real PDFs: TJ, Tr=3, empty pages and rotation. */
static void test_pdf_end_to_end(void) {
    const char *font[] = {"<< /Type /Font /Subtype /Type1 /BaseFont /T /FirstChar 32 /LastChar 90 /Widths ["
        "250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 250 "
        "250 250 250 250 250 250 250 500 500 500 500 500 500 500 500 500 500 500 500 500 500 500 500 500 500 500 "
        "500 500 500 500 500 500 500] >>"};
    test_page pages[3] = {
        {"<< /Font << /F1 9 0 R >> >>", "BT /F1 10 Tf 1 0 0 1 100 700 Tm (WORLD) Tj 1 0 0 1 10 700 Tm (HELLO) Tj "
         "1 0 0 1 10 680 Tm [(AB) -800 (CD)] TJ 3 Tr 1 0 0 1 10 660 Tm (OCR LAYER) Tj ET", NULL, NULL},
        {"<< /Font << /F1 9 0 R >> >>", "BT /F1 10 Tf () Tj ET", NULL, NULL},
        {"<< /Font << /F1 9 0 R >> >>", "BT /F1 10 Tf 1 0 0 1 10 700 Tm (END) Tj ET", NULL, NULL}};
    pdf_document doc = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(pdf_document_open(&doc, build_pdf(pages, 3, font, 1, NULL), NULL, &e));
    pdf_text_items items = {0};
    pdf_plain_text text = {0};
    assert(pdf_text_items_extract(&doc, NULL, &items, &e));
    assert(pdf_plain_text_build(&items, NULL, &text, &e));
    const char *want = "HELLO WORLD\nAB CD\nOCR LAYER\n\nEND\n";
    assert(text.len == strlen(want) && memcmp(text.data, want, text.len) == 0);
    assert(text.pages_with_text == 2);
    pdf_plain_text_free(&text);
    pdf_text_items_free(&items);
    pdf_document_close(&doc);
}

int main(void) {
    test_order_and_lines();
    test_spaces();
    test_pages_and_empty();
    test_non_horizontal_fails();
    test_pdf_end_to_end();
    remove_test_pdf();
    puts("reading order tests passed");
    return 0;
}
