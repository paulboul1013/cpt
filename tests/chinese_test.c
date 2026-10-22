#include "pdf_builder.h"
#include "../src/text_items.h"
#include "../src/reading_order.h"

#include <math.h>

static int extract(const char *path, const pdf_limits *limits, pdf_text_items *items,
                    pdf_error *e) {
    pdf_document doc;
    pdf_error_init(e);
    if (!pdf_document_open(&doc, path, limits, e)) return 0;
    int ok = pdf_text_items_extract(&doc, limits, items, e);
    pdf_document_close(&doc);
    return ok;
}
static void near(double a, double b) { assert(fabs(a - b) < 1e-8); }

#define TYPE0 "<< /Type /Font /Subtype /Type0 /BaseFont /Chinese /Encoding /Identity-H " \
              "/DescendantFonts [6 0 R] /ToUnicode 7 0 R >>"
#define CID "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Chinese "
#define MAP "begincmap 1 begincodespacerange <0000> <FFFF> endcodespacerange " \
            "2 beginbfchar <0001> <4E2D> <0002> <6587> endbfchar endcmap"

static int run_font(const char *font, const char *desc, const char *map, const char *content,
                    pdf_text_items *items, pdf_error *e) {
    char stream[4096];
    assert(strlen(map) + 100 < sizeof(stream));
    snprintf(stream, sizeof(stream), "<< /Length %zu >>\nstream\n%s\nendstream", strlen(map), map);
    const char *extras[] = {font, desc, stream};
    test_page page = {"<< /Font << /F1 5 0 R /F2 << /Type /Font /Subtype /Type1 "
        "/BaseFont /Test /FirstChar 65 /LastChar 66 /Widths [500 600] >> >> >>",
        content, NULL, NULL};
    return extract(build_pdf(&page, 1, extras, 3, NULL), NULL, items, e);
}

static void font_variants(void) {
    pdf_error e;
    pdf_text_items items = {0};
    /* DW defaults to 1000; explicit DW and both W forms index CIDs. */
    const char *desc[] = {CID ">>", CID "/DW 750 >>",
                          CID "/DW 750 /W [1 [500] 2 2 600] >>"};
    const double widths[] = {24, 18, 13.2};
    for (size_t i = 0; i < 3; i++) {
        assert(run_font(TYPE0, desc[i], MAP, "BT /F1 12 Tf <00010002> Tj ET", &items, &e));
        near(items.items[0].width, widths[i]);
        pdf_text_items_free(&items);
    }
    /* A one-byte Simple Font can use ToUnicode without changing geometry. */
    const char *simple = "<< /Type /Font /Subtype /TrueType /BaseFont /Test "
        "/FirstChar 65 /LastChar 66 /Widths [500 600] /ToUnicode 7 0 R "
        "/FontDescriptor << /Flags 4 >> >>";
    const char *map = "begincmap 1 begincodespacerange <00> <FF> endcodespacerange "
        "2 beginbfchar <41> <4E2D> <42> <6587> endbfchar endcmap";
    assert(run_font(simple, CID ">>", map, "BT /F1 12 Tf (AB) Tj ET", &items, &e));
    assert(strcmp((const char *)items.items[0].text, "中文") == 0);
    near(items.items[0].width, 13.2);
    pdf_text_items_free(&items);
    /* Codes cannot span adjacent strings, even within one TJ array. */
    assert(!run_font(TYPE0, CID ">>", MAP, "BT /F1 12 Tf [<00> <01>] TJ ET", &items, &e));
    assert(e.code == PDF_ERROR_MALFORMED && items.len == 0);
    assert(!run_font(TYPE0, CID "/W [1 2 500 2 [600]] >>", MAP,
                     "BT /F1 12 Tf ET", &items, &e));
    assert(e.code == PDF_ERROR_MALFORMED && items.len == 0);
    assert(!run_font(TYPE0, CID ">>", "/Base usecmap " MAP,
                     "BT /F1 12 Tf ET", &items, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && items.len == 0);
    /* Q must restore both font metrics and the two-byte code boundary. */
    assert(run_font(TYPE0, CID ">>", MAP,
        "/F1 12 Tf q /F2 12 Tf BT (AB) Tj ET Q BT <00010002> Tj ET", &items, &e));
    assert(items.len == 2 && strcmp((const char *)items.items[0].text, "AB") == 0);
    assert(strcmp((const char *)items.items[1].text, "中文") == 0);
    near(items.items[0].width, 13.2); near(items.items[1].width, 24);
    pdf_text_items_free(&items);
}

static void chinese_text(void) {
    const char *paths[] = {"tests/fixtures/chinese-raw.pdf",
        "tests/fixtures/chinese-flate.pdf", "tests/fixtures/chinese-cff.pdf"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(*paths); i++) {
        pdf_error e;
        pdf_text_items items = {0};
        int ok = extract(paths[i], NULL, &items, &e);
        if (!ok) fprintf(stderr, "%s: %s\n", paths[i], e.message);
        assert(ok && items.len == 1);
        assert(items.items[0].text_len == strlen("中文測試"));
        assert(memcmp(items.items[0].text, "中文測試", strlen("中文測試")) == 0);
        near(items.items[0].x, 24); near(items.items[0].y, 200);
        near(items.items[0].width, 44.4); /* 1000 + 900 + 800 + DW 1000 at 12pt */
        pdf_plain_text text = {0};
        assert(pdf_plain_text_build(&items, NULL, &text, &e));
        assert(text.len == strlen("中文測試\n") &&
               memcmp(text.data, "中文測試\n", text.len) == 0);
        pdf_plain_text_free(&text);
        pdf_text_items_free(&items);
    }
}
static void unicode_and_geometry(void) {
    pdf_error e;
    pdf_text_items items = {0};
    assert(extract("tests/fixtures/chinese-unicode.pdf", NULL, &items, &e));
    assert(items.len == 1 && items.items[0].replacements == 1);
    assert(strcmp((const char *)items.items[0].text, "你好𠀀AB�") == 0);
    near(items.items[0].width, 60); /* Unicode expansion does not change glyph count. */
    pdf_text_items_free(&items);
    assert(extract("tests/fixtures/chinese-spacing.pdf", NULL, &items, &e));
    assert(items.len == 3);
    near(items.items[0].dx, 19.84);
    near(items.items[1].x, 48.64);
    near(items.items[1].dx, 18.88);
    near(items.items[2].x, 67.52);
    near(items.items[2].dx, 13.6); /* Neither 0020 nor 2020 receives Tw. */
    pdf_text_items_free(&items);
}
static void failures(void) {
    const char *bad[] = {"chinese-odd.pdf", "chinese-bad-surrogate.pdf",
                         "chinese-bad-range.pdf", "chinese-bad-widths.pdf",
                         "chinese-vertical.pdf"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        char path[128];
        snprintf(path, sizeof(path), "tests/fixtures/%s", bad[i]);
        pdf_text_items items = {0};
        pdf_error e;
        assert(!extract(path, NULL, &items, &e));
        assert(e.code == (i == 4 ? PDF_ERROR_UNSUPPORTED : PDF_ERROR_MALFORMED));
        assert(items.len == 0 && items.items == NULL && items.bytes == NULL);
        pdf_text_items_free(&items);
    }
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_decoded_stream_size = 100;
    pdf_error e;
    pdf_text_items items = {0};
    assert(!extract("tests/fixtures/chinese-flate.pdf", &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && items.len == 0);
    pdf_limits_default(&limits);
    limits.max_stream_size = 100;
    assert(!extract("tests/fixtures/chinese-raw.pdf", &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && !items.items);
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 4096;
    assert(!extract("tests/fixtures/chinese-raw.pdf", &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && strstr(e.message, "storage"));

    /* A good first page must not survive a later malformed ToUnicode. */
    const char *simple = "<< /Type /Font /Subtype /Type1 /BaseFont /Test "
                         "/FirstChar 65 /LastChar 65 /Widths [500] >>";
    const char *bad_font = "<< /Type /Font /Subtype /Type0 /BaseFont /Chinese "
        "/Encoding /Identity-H /DescendantFonts [9 0 R] /ToUnicode 10 0 R >>";
    const char *desc = "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Chinese >>";
    const char *extra[] = {simple, bad_font, desc, "<< /Length 0 >>\nstream\n\nendstream"};
    test_page pages[] = {
        {"<< /Font << /F1 7 0 R >> >>", "BT /F1 12 Tf (A) Tj ET", NULL, NULL},
        {"<< /Font << /F1 8 0 R >> >>", "BT /F1 12 Tf <0001> Tj ET", NULL, NULL}
    };
    assert(!extract(build_pdf(pages, 2, extra, 4, NULL), NULL, &items, &e));
    assert(e.code == PDF_ERROR_MALFORMED && !items.items && !items.bytes && !items.len);

    /* The same font is reloaded on page two: decoded CMap bytes must stay
     * charged to the whole document, while retained storage is page-local. */
    char padded_map[11000], stream[11200];
    memset(padded_map, ' ', 10000);
    memcpy(padded_map + 10000, MAP, strlen(MAP) + 1);
    snprintf(stream, sizeof(stream), "<< /Length %zu >>\nstream\n%s\nendstream",
             strlen(padded_map), padded_map);
    const char *shared[] = {
        "<< /Type /Font /Subtype /Type0 /BaseFont /Chinese /Encoding /Identity-H "
        "/DescendantFonts [8 0 R] /ToUnicode 9 0 R >>", CID ">>", stream
    };
    for (size_t i = 0; i < 2; i++) {
        pages[i].resources = "<< /Font << /F1 7 0 R >> >>";
        pages[i].content = "BT /F1 12 Tf <00010002> Tj ET";
    }
    pdf_limits_default(&limits);
    limits.max_total_decoded_size = 16000;
    assert(!extract(build_pdf(pages, 2, shared, 3, NULL), &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && !items.items && !items.bytes && !items.len);
}
static void empty_actualtext(void) {
    pdf_error e; pdf_text_items items = {0};
    const char *begin = "BT /F1 12 Tf /Span << /ActualText <> >> BDC ";
    char content[512];
    /* Unmapped two-byte codes still advance without producing U+FFFD. */
    snprintf(content, sizeof(content), "%s<00030004> Tj EMC <00010002> Tj ET", begin);
    assert(run_font(TYPE0, CID "/DW 750 >>", MAP, content, &items, &e));
    assert(items.len == 1 && items.items[0].replacements == 0);
    assert(strcmp((const char *)items.items[0].text, "中文") == 0);
    near(items.items[0].x, 18); near(items.items[0].width, 18);
    assert(items.items[0].source_order == 1 && items.items[0].sequence == 0);
    pdf_text_items_free(&items);
    snprintf(content, sizeof(content), "%s[<00> <01>] TJ EMC ET", begin);
    assert(!run_font(TYPE0, CID ">>", MAP, content, &items, &e));
    assert(e.code == PDF_ERROR_MALFORMED && items.len == 0);
    /* Suppression cannot bypass font/CMap/width validation at Tf. */
    const char *outside = "/Span << /ActualText () >> BDC BT /F1 12 Tf <0001> Tj ET EMC";
    assert(!run_font(TYPE0, CID "/W [1 2 500 2 [600]] >>", MAP, outside, &items, &e));
    assert(e.code == PDF_ERROR_MALFORMED && items.len == 0);
    assert(!run_font(TYPE0, CID ">>", "/Base usecmap " MAP, outside, &items, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && items.len == 0);
    assert(!run_font("<< /Type /Font /Subtype /Type3 >>", CID ">>", MAP, outside, &items, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && items.len == 0);
}
int main(void) {
    empty_actualtext();
    chinese_text(); unicode_and_geometry(); font_variants(); failures();
    remove_test_pdf();
    puts("Chinese extraction tests passed");
    return 0;
}
