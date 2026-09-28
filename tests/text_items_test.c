#include "pdf_builder.h"
#include "../src/text_items.h"

#include <math.h>
#include <stdint.h>

#define RES "<< /Font << /F1 5 0 R >> >>"
#define FONT(extra) "<< /Type /Font /Subtype /Type1 /BaseFont /Test " extra " >>"
#define ABC "/FirstChar 65 /LastChar 67 /Widths [600 650 700]"
#define AX "/FirstChar 65 /LastChar 90 /Widths [500 500 500 500 500 500 500 500 500 500 500 500 500 " \
           "500 500 500 500 500 500 500 500 500 500 500 500 1000]"

static void near(double a, double b) { assert(fabs(a - b) <= 1e-8 + 1e-9 * fabs(b)); }

static int extract_pages(const test_page *pages, size_t n, const char *const *extras, size_t nextra,
                         const pdf_limits *limits, pdf_text_items *items, pdf_error *e) {
    pdf_document doc = {0};
    pdf_error_init(e);
    assert(pdf_document_open(&doc, build_pdf(pages, n, extras, nextra, NULL), NULL, e));
    *items = (pdf_text_items){0};
    int ok = pdf_text_items_extract(&doc, limits, items, e);
    pdf_document_close(&doc);
    return ok;
}
static int extract(const char *resources, const char *content, const char *const *extras,
                   size_t n, pdf_text_items *items, pdf_error *e) {
    test_page page = {resources, content, NULL, NULL};
    return extract_pages(&page, 1, extras, n, NULL, items, e);
}
static void text_is(const pdf_text_item *item, const char *text, size_t len) {
    assert(item->text_len == len && memcmp(item->text, text, len) == 0 && item->text[len] == 0);
}

static void test_case1_basic_fields(void) {
    pdf_text_items items;
    pdf_error e;
    const char *abc[] = {FONT(ABC)};
    assert(extract(RES, "BT /F1 12 Tf 1 0 0 1 10 20 Tm (ABC) Tj ET", abc, 1, &items, &e));
    assert(items.len == 1);
    const pdf_text_item *t = &items.items[0];
    text_is(t, "ABC", 3);
    assert(t->font_len == 2 && memcmp(t->font, "F1", 2) == 0);
    near(t->x, 10); near(t->y, 20); near(t->width, 23.4); near(t->dx, 23.4); near(t->dy, 0);
    near(t->em_height, 12); near(t->effective_size, 12); near(t->font_size, 12);
    assert(t->horizontal == 1 && t->rendering_mode == 0 && t->replacements == 0);
    assert(t->page == 1 && t->source_order == 0 && t->sequence == 0);
    assert(t->subtype == PDF_FONT_TYPE1 && t->encoding == PDF_FONT_ENCODING_ASCII_FALLBACK);
    pdf_text_items_free(&items);
    assert(items.items == NULL && items.len == 0 && items.bytes == NULL);
}

static void test_case2_3_scaled_and_rotated(void) {
    pdf_text_items items;
    pdf_error e;
    const char *abc[] = {FONT(ABC)};
    /* Case 2: cm doubles user space; nominal size stays 12. */
    assert(extract(RES, "2 0 0 2 0 0 cm BT /F1 12 Tf 1 0 0 1 5 6 Tm (A) Tj ET", abc, 1, &items, &e));
    const pdf_text_item *t = &items.items[0];
    near(t->font_size, 12); near(t->em_height, 24); near(t->effective_size, 24);
    near(t->x, 10); near(t->y, 12); near(t->width, 14.4);
    assert(t->horizontal);
    pdf_text_items_free(&items);
    /* Case 3: 90-degree CTM keeps the item but flags it non-horizontal. */
    assert(extract(RES, "0 1 -1 0 0 0 cm BT /F1 10 Tf (A) Tj ET", abc, 1, &items, &e));
    t = &items.items[0];
    assert(t->horizontal == 0);
    near(t->dx, 0); near(t->dy, 6); near(t->width, 0); near(t->em_height, 10);
    pdf_text_items_free(&items);
    /* Negative horizontal scale: advance and width are signed, not abs(). */
    assert(extract(RES, "BT /F1 10 Tf -100 Tz (A) Tj ET", abc, 1, &items, &e));
    t = &items.items[0];
    near(t->width, -6); assert(t->horizontal == 0);
    pdf_text_items_free(&items);
    /* Negative Tc alone gives a negative width while staying horizontal. */
    assert(extract(RES, "BT /F1 10 Tf -20 Tc (AB) Tj ET", abc, 1, &items, &e));
    t = &items.items[0];
    near(t->width, 6 - 20 + 6.5 - 20); assert(t->horizontal == 1);
    pdf_text_items_free(&items);
    /* Rise moves the origin but not em height. */
    assert(extract(RES, "BT /F1 10 Tf 3 Ts (A) Tj ET", abc, 1, &items, &e));
    near(items.items[0].y, 3); near(items.items[0].em_height, 10);
    pdf_text_items_free(&items);
}

static void test_case4_5_tj_empty_invisible(void) {
    pdf_text_items items;
    pdf_error e;
    const char *abc[] = {FONT(ABC)};
    /* Case 4: numbers only move later segments; no item for them. */
    assert(extract(RES, "BT /F1 10 Tf [(A) -500 (B) 250] TJ ET", abc, 1, &items, &e));
    assert(items.len == 2);
    near(items.items[1].x, items.items[0].x + items.items[0].width + 5);
    text_is(&items.items[1], "B", 1);
    pdf_text_items_free(&items);
    /* Case 5: empty string yields no item; Tr=3 is kept; quote operators too. */
    assert(extract(RES, "BT /F1 10 Tf 12 TL 3 Tr () Tj (C) Tj 0 Tr (A) ' 1 2 (B) \" [] TJ ET",
                   abc, 1, &items, &e));
    assert(items.len == 3);
    text_is(&items.items[0], "C", 1);
    assert(items.items[0].rendering_mode == 3 && items.items[0].source_order == 1);
    assert(items.items[1].source_order == 2 && items.items[2].source_order == 3);
    near(items.items[1].y, -12); near(items.items[2].y, -24);
    assert(items.items[2].sequence == 2);
    pdf_text_items_free(&items);
    /* A page whose only strings are empty produces no items and succeeds. */
    assert(extract(RES, "BT /F1 10 Tf () Tj ET", abc, 1, &items, &e));
    assert(items.len == 0 && items.items == NULL);
    pdf_text_items_free(&items);
}

static void test_names_encoding_and_pages(void) {
    pdf_text_items items;
    pdf_error e;
    const char *win[] = {FONT(ABC " /Encoding /WinAnsiEncoding /FontDescriptor << /MissingWidth 250 >>")};
    assert(extract("<< /Font << /F#001 5 0 R >> >>", "BT /F#001 10 Tf <418000> Tj ET", win, 1, &items, &e));
    const pdf_text_item *t = &items.items[0];
    text_is(t, "A\xE2\x82\xAC\xEF\xBF\xBD", 7);
    assert(t->replacements == 1 && t->encoding == PDF_FONT_ENCODING_WIN_ANSI);
    assert(t->font_len == 3 && memcmp(t->font, "F\0" "1", 3) == 0);
    near(t->width, 6 + 2.5 + 2.5);
    pdf_text_items_free(&items);
    /* Same /F1 name on two pages keeps each page's own font policy. */
    test_page pages[2] = {{"<< /Font << /F1 7 0 R >> >>", "BT /F1 10 Tf <80> Tj ET", NULL, NULL},
                          {"<< /Font << /F1 8 0 R >> >>", "BT /F1 10 Tf <80> Tj (A) Tj ET", NULL, NULL}};
    const char *two[] = {FONT("/FirstChar 128 /LastChar 128 /Widths [556] /Encoding /WinAnsiEncoding"),
                         FONT("/FirstChar 65 /LastChar 128 /Widths [" "600 600 600 600 600 600 600 600 600 600 "
                              "600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 "
                              "600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 "
                              "600 600 600 600 600 600 600 600 600 600 600 600 600 600]")};
    assert(extract_pages(pages, 2, two, 2, NULL, &items, &e));
    assert(items.len == 3);
    assert(items.items[0].page == 1 && items.items[1].page == 2 && items.items[2].page == 2);
    assert(items.items[1].source_order == 0 && items.items[2].sequence == 2);
    text_is(&items.items[0], "\xE2\x82\xAC", 3);
    text_is(&items.items[1], "\xEF\xBF\xBD", 3);
    assert(items.items[1].font == items.items[2].font); /* shared name storage */
    near(items.items[0].width, 5.56); near(items.items[1].width, 6);
    pdf_text_items_free(&items);
}

static void test_growth_keeps_pointers(void) {
    char content[8192] = "BT /F1 10 Tf ";
    for (int i = 0; i < 200; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%s(ABCDEFGHIJKLMNOP%c) Tj ", i % 2 ? "/F1 10 Tf " : "/F2 10 Tf ",
                 'A' + i % 26);
        strcat(content, line);
    }
    strcat(content, "ET");
    const char *fonts[] = {FONT(AX), FONT(AX)};
    pdf_text_items items;
    pdf_error e;
    assert(extract("<< /Font << /F1 5 0 R /F2 6 0 R >> >>", content, fonts, 2, &items, &e));
    assert(items.len == 200 && items.bytes_cap >= items.bytes_len);
    double x = 0;
    for (size_t i = 0; i < 200; i++) {
        const pdf_text_item *t = &items.items[i];
        assert(t->text_len == 17 && memcmp(t->text, "ABCDEFGHIJKLMNOP", 16) == 0);
        assert(t->text[16] == 'A' + i % 26 && t->text[17] == 0);
        assert(t->font_len == 2 && t->font[1] == (i % 2 ? '1' : '2'));
        near(t->x, x);
        x += t->width;
        assert(t->sequence == i);
    }
    assert(items.text_bytes == 200 * 17);
    pdf_text_items_free(&items);
}

static void test_failures_are_all_or_nothing(void) {
    pdf_text_items items;
    pdf_error e;
    const char *abc[] = {FONT(ABC)};
    /* Case 6: later page /ToUnicode discards earlier items. */
    test_page pages[2] = {{"<< /Font << /F1 7 0 R >> >>", "BT /F1 10 Tf (A) Tj ET", NULL, NULL},
                          {"<< /Font << /F1 8 0 R >> >>", "BT /F1 10 Tf (A) Tj ET", NULL, NULL}};
    const char *later[] = {FONT(ABC), FONT(ABC " /ToUnicode 5 0 R")};
    assert(!extract_pages(pages, 2, later, 2, NULL, &items, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && strstr(e.message, "ToUnicode"));
    assert(items.items == NULL && items.len == 0 && items.bytes == NULL);
    /* Item count limit. */
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_container_entries = 2;
    test_page three = {RES, "BT /F1 10 Tf (A) Tj (B) Tj (C) Tj ET", NULL, NULL};
    assert(!extract_pages(&three, 1, abc, 1, &limits, &items, &e));
    /* M9 enforces the shared event budget before the M10 defensive check. */
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && items.items == NULL);
    assert(strstr(e.message, "event count") || strstr(e.message, "text item count"));
    /* Font-name bytes have their own counter and can trip before UTF-8:
     * alternating names are stored again for each item. */
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 20;
    test_page names = {"<< /Font << /LongFontName 5 0 R /Other 5 0 R >> >>",
                       "BT /LongFontName 10 Tf (A) Tj /Other 10 Tf (A) Tj /LongFontName 10 Tf (A) Tj ET",
                       NULL, NULL};
    assert(!extract_pages(&names, 1, abc, 1, &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && strstr(e.message, "font names") && items.bytes == NULL);
    limits.max_total_decoded_size = 40;
    assert(extract_pages(&names, 1, abc, 1, &limits, &items, &e));
    assert(items.font_bytes == 12 + 5 + 12 && items.text_bytes == 3);
    pdf_text_items_free(&items);
    /* UTF-8 byte limit across pages. */
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 100;
    test_page big[2] = {{"<< /Font << /F1 7 0 R >> >>", "BT /F1 10 Tf (ABCABC) Tj ET", NULL, NULL},
                        {"<< /Font << /F1 7 0 R >> >>", "BT /F1 10 Tf (ABC) Tj ET", NULL, NULL}};
    assert(extract_pages(big, 2, abc, 1, &limits, &items, &e));
    pdf_text_items_free(&items);
    limits.max_total_decoded_size = 8;
    assert(!extract_pages(big, 2, abc, 1, &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && items.bytes == NULL && strstr(e.message, "UTF-8"));
    /* Collection must start empty; nothing is touched. */
    pdf_document doc = {0};
    pdf_error_init(&e);
    assert(pdf_document_open(&doc, build_pdf(&three, 1, abc, 1, NULL), NULL, &e));
    pdf_text_items dirty = {0};
    dirty.len = 1;
    assert(!pdf_text_items_extract(&doc, NULL, &dirty, &e) && e.code == PDF_ERROR_IO && dirty.len == 1);
    dirty = (pdf_text_items){0};
    dirty.font_bytes = 1; /* any nonzero counter would corrupt limit accounting */
    pdf_error_init(&e);
    assert(!pdf_text_items_extract(&doc, NULL, &dirty, &e) && e.code == PDF_ERROR_IO);
    pdf_document_close(&doc);
    pdf_text_items_free(NULL);
}

static void test_dump(void) {
    pdf_text_items items;
    pdf_error e;
    const char *win[] = {FONT(ABC " /Encoding /WinAnsiEncoding /FontDescriptor << >>")};
    assert(extract("<< /Font << /F#0a 5 0 R >> >>", "BT /F#0a 10 Tf <41220a5c> Tj ET", win, 1, &items, &e));
    char *out = NULL;
    size_t size = 0;
    FILE *s = open_memstream(&out, &size);
    assert(pdf_text_item_dump(s, &items.items[0], &e));
    fclose(s);
    assert(strlen(out) == size && out[size - 1] == '\n' && !memchr(out, '\n', size - 1));
    assert(strstr(out, "\"preview\":\"A\\u0022\\ufffd\\u005c\""));
    assert(strstr(out, "\"font_len\":2,\"font\":\"460a\""));
    assert(strstr(out, "\"em_height\":10.000000000,\"font_size\":10.000000000"));
    assert(strstr(out, "\"replacements\":1,\"subtype\":\"Type1\",\"encoding\":\"WinAnsiEncoding\"}"));
    for (size_t i = 0; i < size; i++) assert(out[i] == '\n' || (out[i] >= 0x20 && out[i] < 0x7F));
    free(out);
    pdf_text_items_free(&items);
}

/* Existing M8/M9 fixtures: items carry the already-accepted origins/advances. */
static void test_m8_fixtures(void) {
    const char *paths[] = {"tests/fixtures/geometry-raw.pdf", "tests/fixtures/geometry-flate.pdf",
                           "tests/fixtures/geometry-array.pdf", "tests/fixtures/geometry-pages.pdf"};
    const double x[] = {36, 46, 46, 51.76, 61.96, 46, 46, 36, 36, 36};
    const double y[] = {250, 226, 208, 208, 208, 194, 172, 110, 76, 40};
    const double w[] = {57.6, 57.6, 7.2, 7.2, 7.2, 22.08, 64.8, 72, 57.6, 100.8};
    const double h[] = {12, 12, 12, 12, 12, 12, 12, 24, 12, 24};
    for (size_t k = 0; k < 4; k++) {
        pdf_document doc = {0};
        pdf_error e;
        pdf_error_init(&e);
        pdf_text_items items = {0};
        assert(pdf_document_open(&doc, paths[k], NULL, &e));
        assert(pdf_text_items_extract(&doc, NULL, &items, &e));
        assert(items.len == (k == 3 ? 20 : 10));
        for (size_t i = 0; i < items.len; i++) {
            const pdf_text_item *t = &items.items[i];
            near(t->x, x[i % 10]); near(t->y, y[i % 10]); near(t->width, w[i % 10]);
            near(t->em_height, h[i % 10]); assert(t->horizontal);
            assert(t->page == 1 + i / 10 && t->sequence == i);
        }
        assert(items.items[6].rendering_mode == 3);
        text_is(&items.items[6], "INVISIBLE", 9);
        pdf_text_items_free(&items);
        pdf_document_close(&doc);
    }
}

int main(void) {
    test_m8_fixtures();
    test_case1_basic_fields();
    test_case2_3_scaled_and_rotated();
    test_case4_5_tj_empty_invisible();
    test_names_encoding_and_pages();
    test_growth_keeps_pointers();
    test_failures_are_all_or_nothing();
    test_dump();
    remove_test_pdf();
    puts("text item tests passed");
    return 0;
}
