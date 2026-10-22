#include "pdf_builder.h"
#include "../src/reading_order.h"
#include <math.h>

#define FONT "<< /Type /Font /Subtype /Type1 /BaseFont /Test /FirstChar 65 /LastChar 65 /Widths [600] >>"
#define IMAGE "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8 /Length 1 >>\nstream\nx\nendstream"
#define RES "<< /Font << /F1 5 0 R >> /XObject << /Im 6 0 R >> >>"
#define TEXT "BT /F1 12 Tf 1 0 0 1 24 200 Tm (A) Tj ET "
#define DRAW "1 w 0 J 2 j 10 M [3 0 2] 1 d 0 0 m 10 0 l 10 1 2 3 4 5 c h W* n " \
             "0 0 10 10 re W S 0 0 10 10 re f 0 0 10 10 re B* /Im Do "

static void syntax(void) {
    const char *good[] = {DRAW, "0 w 2 J 0 j 1 M [2 1] -2 d", "0 0 m q Q 1 1 l n",
        "0 0 m q n Q 1 1 m n", "W n W* n S f B*", "/Im#00 Do"};
    for (size_t i = 0; i < sizeof(good)/sizeof(*good); i++) {
        pdf_error e; pdf_error_init(&e); pdf_content_result r;
        assert(pdf_content_interpret((const unsigned char *)good[i], strlen(good[i]),
            NULL, 123, NULL, NULL, NULL, &r, &e));
        assert(r.operations > 0 && r.text_shows == 0);
    }
    const char *bad[] = {"w", "-1 w", "1 2 w", "(x) w", "3 J", "-1 j", "1.0 J", "0 M",
        "[0 0] 0 d", "[-1] 0 d", "[1 /X] 0 d", "1 0 d", "[] /X d", "[1] d", "[] 1 d",
        "0 m", "1 1 l", "1 2 3 4 5 6 c", "h", "0 0 m n 1 1 l", "0 0 m q n Q 1 1 l", "0 0 re",
        "1 W", "1 W*", "1 n", "1 S", "1 f", "1 B*", "Do", "1 Do", "/A /B Do",
        "BT 0 0 m ET", "BT /Im Do ET", "BT W ET", "BT f ET"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        pdf_error e; pdf_error_init(&e); pdf_content_result r;
        assert(!pdf_content_interpret((const unsigned char *)bad[i], strlen(bad[i]),
            NULL, 123, NULL, NULL, NULL, &r, &e));
        assert(e.code == PDF_ERROR_MALFORMED && e.offset == 123);
        assert(r.operations == 0 && r.text_shows == 0);
    }
}

static int extract(const char *resources, const char *content, const char *image,
                   const pdf_limits *limits, pdf_text_items *items, pdf_error *e) {
    test_page p = {resources, content, NULL, NULL};
    const char *extras[] = {FONT, image};
    const char *path = build_pdf(&p, 1, extras, 2, NULL);
    pdf_document d = {0}; pdf_error_init(e);
    int ok = pdf_document_open(&d, path, NULL, e) && pdf_text_items_extract(&d, limits, items, e);
    pdf_document_close(&d);
    return ok;
}

static void extraction(void) {
    pdf_text_items items = {0}; pdf_error e;
    assert(extract(RES, TEXT DRAW TEXT, IMAGE, NULL, &items, &e));
    assert(items.len == 2);
    for (size_t i = 0; i < items.len; i++) {
        assert(items.items[i].text_len == 1 && items.items[i].text[0] == 'A');
        assert(fabs(items.items[i].x - 24) < 1e-8);
        assert(fabs(items.items[i].y - 200) < 1e-8);
    }
    pdf_text_items_free(&items);
    /* The clipping policy is source text extraction, including outside paths. */
    assert(extract(RES, "0 0 1 1 re W n " TEXT, IMAGE, NULL, &items, &e));
    assert(items.len == 1); pdf_text_items_free(&items);
    const struct { const char *res, *image; pdf_error_code code; } bad[] = {
        {"<< >>", IMAGE, PDF_ERROR_MALFORMED},
        {"<< /XObject [] >>", IMAGE, PDF_ERROR_MALFORMED},
        {"<< /XObject << >> >>", IMAGE, PDF_ERROR_MALFORMED},
        {RES, "<< /Subtype /Image /Width 1 /Height 1 >>", PDF_ERROR_MALFORMED},
        {RES, "<< /Subtype /Form /Length 0 >>\nstream\n\nendstream", PDF_ERROR_UNSUPPORTED},
        {RES, "<< /Subtype /Bogus /Length 0 >>\nstream\n\nendstream", PDF_ERROR_UNSUPPORTED},
        {RES, "<< /Subtype 1 /Length 0 >>\nstream\n\nendstream", PDF_ERROR_MALFORMED},
        {RES, "<< /Subtype /Image /Width 0 /Height 1 /Length 1 >>\nstream\nx\nendstream", PDF_ERROR_MALFORMED},
        {RES, "6 0 R", PDF_ERROR_MALFORMED},
        {"<< /XObject 6 0 R >>", "6 0 R", PDF_ERROR_MALFORMED},
        {RES, "<< /Type /Font /Subtype /Image /Width 1 /Height 1 /Length 1 >>\nstream\nx\nendstream", PDF_ERROR_MALFORMED}
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        assert(!extract(bad[i].res, TEXT "/Im Do", bad[i].image, NULL, &items, &e));
        assert(e.code == bad[i].code && !items.items && !items.len);
    }
    pdf_limits limits; pdf_limits_default(&limits); limits.max_nesting_depth = 0;
    assert(!extract(RES, "/Im Do", IMAGE, &limits, &items, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    /* Raw M8 cannot validate a document resource. */
    pdf_content_result r; pdf_error_init(&e);
    pdf_text_state *s = pdf_text_state_create(NULL, NULL, NULL, NULL, NULL, &e);
    assert(s && !pdf_content_interpret((const unsigned char *)"/Im Do", 6, NULL,
        123, pdf_text_state_visit, NULL, s, &r, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED);
    pdf_text_state_destroy(s);
}

static void references(void) {
    const struct {
        const char *resource, *content, *target, *next, *last;
        size_t depth;
        pdf_error_code code;
    } cases[] = {
        {RES, "/Im Do", "7 0 R", IMAGE, "null", 2, 0},
        {RES, "/Im Do", "7 0 R", IMAGE, "null", 1, PDF_ERROR_RESOURCE_LIMIT},
        {RES, "/Im Do", "7 0 R", "6 0 R", "null", 10, PDF_ERROR_MALFORMED},
        {RES, "/Im Do", "<< /Subtype /Image /Width 7 0 R /Height 8 0 R /Length 1 >>\nstream\nx\nendstream",
            "1", "2", 10, 0},
        {RES, "/Im Do", "<< /Subtype /Image /Width 7 0 R /Height 1 /Length 1 >>\nstream\nx\nendstream",
            IMAGE, "null", 10, PDF_ERROR_MALFORMED},
        {"<< /XObject << /Im#00 6 0 R >> >>", "/Im#00 Do", IMAGE, "null", "null", 10, 0},
        {"7 0 R", "/Im Do", IMAGE, "<< /XObject 8 0 R >>", "<< /Im 6 0 R >>", 1, 0},
        {RES, "/Im Do", "<< /Subtype /Image /Width /Wrong /Height 1 /Length 1 >>\nstream\nx\nendstream",
            "null", "null", 10, PDF_ERROR_MALFORMED}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
        test_page p = {cases[i].resource, cases[i].content, NULL, NULL};
        const char *extra[] = {FONT, cases[i].target, cases[i].next, cases[i].last};
        const char *path = build_pdf(&p, 1, extra, 4, NULL);
        pdf_document d = {0}; pdf_error e; pdf_error_init(&e);
        assert(pdf_document_open(&d, path, NULL, &e));
        pdf_limits limits; pdf_limits_default(&limits); limits.max_nesting_depth = cases[i].depth;
        pdf_text_items items = {0};
        int ok = pdf_text_items_extract(&d, &limits, &items, &e);
        assert(ok == (cases[i].code == 0) && e.code == cases[i].code);
        assert(items.len == 0);
        pdf_text_items_free(&items); pdf_document_close(&d);
    }
}

int main(void) {
    syntax(); extraction(); references(); remove_test_pdf(); return 0;
}
