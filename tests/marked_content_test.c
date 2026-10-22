#include "pdf_builder.h"
#include "../src/contents.h"
#include "../src/reading_order.h"

#include <math.h>
#include <stdint.h>

#define FONT "<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding " \
             "/FirstChar 32 /LastChar 67 /Widths [600 600 600 600 600 600 600 600 600 600 " \
             "600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 " \
             "600 600 600 600 600 600 600 600] /FontDescriptor << /MissingWidth 600 >> >>"
#define FONT2 "<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding " \
              "/FirstChar 32 /LastChar 32 /Widths [1000] /FontDescriptor << /MissingWidth 1000 >> >>"
#define RES "<< /Font << /F1 5 0 R /F2 " FONT2 " >> >>"
#define EMPTY "/Span << /ActualText <> >> BDC "
#define PASS "/Span << >> BDC "

typedef struct {
    size_t len, abort_at, replacements;
    pdf_error_code abort_code;
    pdf_text_event raw[128]; /* Numeric fields only; borrowed pointers discarded. */
} trace;

static void near(double a, double b) { assert(fabs(a - b) < 1e-8); }
static void same_matrix(pdf_matrix a, pdf_matrix b) {
    near(a.a, b.a); near(a.b, b.b); near(a.c, b.c);
    near(a.d, b.d); near(a.e, b.e); near(a.f, b.f);
}
static int raw_collect(void *ctx, const pdf_text_event *ev, pdf_error *e) {
    (void)e;
    trace *t = ctx;
    assert(t->len < 128);
    t->raw[t->len++] = *ev;
    t->raw[t->len - 1].bytes.data = NULL;
    t->raw[t->len - 1].state.font.data = NULL;
    return 1;
}
static int collect(void *ctx, const pdf_font_text_event *ev, pdf_error *e) {
    trace *t = ctx;
    if (t->len == t->abort_at) {
        if (t->abort_code) pdf_error_set(e, t->abort_code, 0, "test", "marked consumer stopped");
        return 0;
    }
    t->replacements += ev->replacements;
    return raw_collect(ctx, ev->raw, e);
}
static int metric(void *ctx, pdf_text_bytes font, pdf_text_bytes bytes,
                  size_t *used, double *width, pdf_error *e) {
    (void)ctx; (void)bytes; (void)e;
    *used = 1;
    *width = font.len == 2 && memcmp(font.data, "F2", 2) == 0 ? 1000 : 600;
    return 1;
}
static int bridge_run(const char *path, const pdf_limits *limits,
                      pdf_font_text_totals *totals, trace *t, pdf_error *e) {
    pdf_document doc = {0}; pdf_pages pages = {0};
    pdf_error_init(e);
    int ok = pdf_document_open(&doc, path, NULL, e) && pdf_pages_load(&doc, &pages, e);
    pdf_contents_context contents;
    pdf_contents_context_init(&contents, &doc);
    for (size_t i = 0; ok && i < pages.len; i++) {
        pdf_contents_result bytes = {0};
        ok = pdf_contents_read(&contents, &pages.items[i], &bytes, e) &&
            pdf_font_text_page_interpret(&doc, &pages.items[i], bytes.data, bytes.len, limits,
                pdf_document_reference_offset(&doc, pages.items[i].reference), totals, collect, t, e);
        pdf_contents_result_free(&bytes);
    }
    pdf_pages_free(&pages); pdf_document_close(&doc);
    return ok;
}
static const char *one(const char *content) {
    test_page p = {RES, content, NULL, NULL};
    const char *extra[] = {FONT};
    return build_pdf(&p, 1, extra, 1, NULL);
}
static int extract(const char *path, pdf_text_items *items, pdf_error *e) {
    pdf_document doc = {0}; pdf_error_init(e);
    int ok = pdf_document_open(&doc, path, NULL, e) && pdf_text_items_extract(&doc, NULL, items, e);
    pdf_document_close(&doc); return ok;
}

static void syntax(void) {
    const struct { const char *s; pdf_error_code code; size_t ops; const char *at; } cases[] = {
        {PASS "BT ET EMC", 0, 4, NULL},
        {"BT " EMPTY "EMC ET", 0, 4, NULL},
        {"/Other /P1 BDC EMC", 0, 2, NULL},
        {"/Span << /ActualText (x) /MCID 1 >> BDC EMC", 0, 2, NULL},
        {"q " EMPTY "Q q EMC Q", 0, 6, NULL},
        {EMPTY PASS "EMC EMC", 0, 4, NULL},
        {EMPTY "BT EMC ET", PDF_ERROR_MALFORMED, 0, "EMC"},
        {"BT " EMPTY "ET EMC", PDF_ERROR_MALFORMED, 0, "ET"},
        {"EMC", PDF_ERROR_MALFORMED, 0, "EMC"},
        {EMPTY, PDF_ERROR_MALFORMED, 0, ""},
        {"BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {"/Span BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {"/Span << >> 1 BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {"1 << >> BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {"/Span [] BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {"/Span null BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {"/Span (x) BDC", PDF_ERROR_MALFORMED, 0, "BDC"},
        {EMPTY "1 EMC", PDF_ERROR_MALFORMED, 0, "EMC"},
        {"/Span BMC", PDF_ERROR_UNSUPPORTED, 0, "BMC"},
        {"/Span MP", PDF_ERROR_UNSUPPORTED, 0, "MP"},
        {"/Span << >> DP", PDF_ERROR_UNSUPPORTED, 0, "DP"}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        pdf_error e; pdf_error_init(&e); pdf_content_result r;
        int ok = pdf_content_interpret((const unsigned char *)cases[i].s, strlen(cases[i].s),
            NULL, 123, NULL, NULL, NULL, &r, &e);
        assert(ok == (cases[i].code == 0) && e.code == cases[i].code);
        assert(r.operations == cases[i].ops && r.text_shows == 0);
        if (!ok) {
            size_t at = *cases[i].at ? (size_t)(strstr(cases[i].s, cases[i].at) - cases[i].s) : strlen(cases[i].s);
            assert(r.error_offset == at && e.offset == 123 && strstr(e.message, "decoded byte"));
        }
    }
    pdf_limits l; pdf_limits_default(&l);
    const char *s = PASS PASS PASS "EMC EMC EMC";
    pdf_error e; pdf_error_init(&e); pdf_content_result r;
    l.max_nesting_depth = 2;
    assert(!pdf_content_interpret((const unsigned char *)s, strlen(s), &l, 123,
        NULL, NULL, NULL, &r, &e) && e.code == PDF_ERROR_RESOURCE_LIMIT);
    assert(r.operations == 0 && r.error_offset == 2 * strlen(PASS) + strlen("/Span << >> "));
    l.max_nesting_depth = 3; pdf_error_init(&e);
    assert(pdf_content_interpret((const unsigned char *)s, strlen(s), &l, 123,
        NULL, NULL, NULL, &r, &e) && r.operations == 6);
    l.max_token_size = 4; pdf_error_init(&e);
    assert(!pdf_content_interpret((const unsigned char *)EMPTY "EMC", strlen(EMPTY "EMC"),
        &l, 123, NULL, NULL, NULL, &r, &e) && e.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_limits_default(&l); l.max_container_entries = 2; pdf_error_init(&e);
    s = "/Span << /A 1 /B 2 /C 3 >> BDC EMC";
    assert(!pdf_content_interpret((const unsigned char *)s, strlen(s), &l, 123,
        NULL, NULL, NULL, &r, &e) && e.code == PDF_ERROR_RESOURCE_LIMIT);
}

static void policy(void) {
    const char *bad[] = {
        "/Span << /ActualText (x) >> BDC EMC", "/Span << /ActualText <FEFF> >> BDC EMC",
        "/Span /P1 BDC EMC", "/Artifact << >> BDC EMC", "/Span#00 << >> BDC EMC",
        "/Span << /ActualText#00 <> >> BDC EMC", "/Span << /MCID 1 >> BDC EMC",
        "/Span << /Lang (en) >> BDC EMC", "/Span << /OC /X >> BDC EMC",
        "/Span << /ActualText 0 >> BDC EMC", "/Span << /ActualText /X >> BDC EMC",
        "/Span << /ActualText null >> BDC EMC", "/Span << /ActualText [] >> BDC EMC",
        "/Span << /ActualText << >> >> BDC EMC", "/Span << /ActualText false >> BDC EMC"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        trace t = {.abort_at = SIZE_MAX}; pdf_font_text_totals totals = {0}; pdf_error e;
        assert(!bridge_run(one(bad[i]), NULL, &totals, &t, &e));
        assert(e.code == (i < 9 ? PDF_ERROR_UNSUPPORTED : PDF_ERROR_MALFORMED));
        assert(strstr(e.message, "marked-content") && !strstr(e.message, "font byte") && t.len == 0);
        char offset[64]; snprintf(offset, sizeof(offset), "decoded byte %zu:", (size_t)(strstr(bad[i], "BDC") - bad[i]));
        assert(strstr(e.message, offset));
    }
    const char *s = "BT /F1 10 Tf " PASS "(A) Tj EMC " EMPTY "(B) Tj EMC "
        "/Span << /ActualText () >> BDC (C) Tj EMC (A) Tj ET";
    trace t = {.abort_at = SIZE_MAX}; pdf_font_text_totals totals = {0}; pdf_error e;
    assert(bridge_run(one(s), NULL, &totals, &t, &e));
    assert(t.len == 2 && totals.events == 4 && totals.utf8_bytes == 2);
    near(t.raw[1].origin.x, 18); assert(t.raw[1].source_order == 3);
    /* Raw M8 keeps all four events, including those with empty ActualText. */
    trace raw = {0}; pdf_page page = {0}; pdf_error_init(&e);
    assert(pdf_text_page_interpret(&page, (const unsigned char *)s, strlen(s), NULL,
        123, metric, NULL, raw_collect, &raw, &e) && raw.len == 4);
    near(raw.raw[3].origin.x, t.raw[1].origin.x);
    const char *empty_forms[] = {"/Span << /ActualText < \n > >> BDC ",
        "/Sp#61n << /Actual#54ext (\\\n) >> BDC "};
    for (size_t i = 0; i < 2; i++) {
        char content[256]; snprintf(content, sizeof(content), "BT /F1 10 Tf %s(B) Tj EMC (A) Tj ET", empty_forms[i]);
        t = (trace){.abort_at = SIZE_MAX}; totals = (pdf_font_text_totals){0};
        assert(bridge_run(one(content), NULL, &totals, &t, &e) && t.len == 1 && totals.events == 2);
        near(t.raw[0].origin.x, 6);
    }
}

static void geometry_and_nesting(void) {
    pdf_error e;
    /* 1, 2 and 24 shows must all advance, including multi-segment TJ. */
    const size_t counts[] = {1, 2, 24};
    for (size_t k = 0; k < 3; k++) {
        char s[2048] = "BT /F1 10 Tf " EMPTY;
        for (size_t i = 0; i < counts[k]; i++) strcat(s, "(A) Tj ");
        strcat(s, "[(B) -100 (C)] TJ EMC (A) Tj ET");
        trace t = {.abort_at = SIZE_MAX}; pdf_font_text_totals totals = {0};
        assert(bridge_run(one(s), NULL, &totals, &t, &e));
        assert(t.len == 1 && totals.events == counts[k] + 3 && totals.utf8_bytes == 1);
        near(t.raw[0].origin.x, 6 * (counts[k] + 2) + 1);
        assert(t.raw[0].source_order == counts[k] + 2 && t.raw[0].segment_index == 0);
    }
    const char *nested = "BT /F1 10 Tf (A) Tj " PASS EMPTY "(B) Tj " PASS
        "(C) Tj EMC " EMPTY "(B) Tj EMC EMC (A) Tj EMC " EMPTY
        "(C) Tj EMC (B) Tj ET";
    trace t = {.abort_at = SIZE_MAX}; pdf_font_text_totals totals = {0};
    assert(bridge_run(one(nested), NULL, &totals, &t, &e));
    assert(t.len == 3 && totals.events == 7 && totals.utf8_bytes == 3);
    near(t.raw[1].origin.x, 24); near(t.raw[2].origin.x, 36);
    /* A marker never participates in q/Q state restoration. */
    t = (trace){.abort_at = SIZE_MAX}; totals = (pdf_font_text_totals){0};
    assert(bridge_run(one("/F1 10 Tf q " EMPTY "Q q BT (B) Tj ET EMC Q BT (A) Tj ET"),
        NULL, &totals, &t, &e) && t.len == 1 && totals.events == 2);
    assert(t.raw[0].source_order == 1);

    /* Compare every retained event with the corresponding event in an
     * otherwise identical stream whose markers were removed. */
    const char *start = "BT /F1 10 Tf 1 0 0 1 10 20 Tm (A) Tj ";
    const char *middle = "/F2 20 Tf 1 Tc 2 Tw 80 Tz 1 0 0 1 30 40 Tm 5 6 Td (B ) Tj [(C) -100 (A)] TJ ";
    const char *end = "(B) Tj /F1 12 Tf 0 Tc 0 Tw 100 Tz 3 4 Td (C) Tj ET";
    char plain[2048], marked[2048];
    snprintf(plain, sizeof(plain), "%s%s%s", start, middle, end);
    snprintf(marked, sizeof(marked), "%s%s%sEMC %s", start, EMPTY, middle, end);
    trace control = {.abort_at = SIZE_MAX}; t = control;
    totals = (pdf_font_text_totals){0};
    assert(bridge_run(one(plain), NULL, &totals, &control, &e));
    totals = (pdf_font_text_totals){0};
    assert(bridge_run(one(marked), NULL, &totals, &t, &e));
    assert(control.len == 6 && t.len == 3 && totals.events == 6);
    near(t.raw[1].origin.x, 105.4); near(t.raw[1].origin.y, 46);
    near(t.raw[1].advance.x, 16.8); near(t.raw[2].origin.x, 38); near(t.raw[2].origin.y, 50);
    for (size_t i = 0; i < t.len; i++) {
        size_t j = i ? i + 3 : 0;
        near(t.raw[i].origin.x, control.raw[j].origin.x);
        near(t.raw[i].origin.y, control.raw[j].origin.y);
        near(t.raw[i].advance.x, control.raw[j].advance.x);
        near(t.raw[i].after.e, control.raw[j].after.e);
        same_matrix(t.raw[i].after, control.raw[j].after);
        same_matrix(t.raw[i].rendering_matrix, control.raw[j].rendering_matrix);
        same_matrix(t.raw[i].state.ctm, control.raw[j].state.ctm);
        same_matrix(t.raw[i].state.text_matrix, control.raw[j].state.text_matrix);
        same_matrix(t.raw[i].state.line_matrix, control.raw[j].state.line_matrix);
        near(t.raw[i].state.font_size, control.raw[j].state.font_size);
        near(t.raw[i].state.char_spacing, control.raw[j].state.char_spacing);
        near(t.raw[i].state.word_spacing, control.raw[j].state.word_spacing);
        near(t.raw[i].state.hscale, control.raw[j].state.hscale);
        assert(t.raw[i].source_order == j);
    }
    trace raw = {0}; pdf_page p = {0}; pdf_error_init(&e);
    assert(pdf_text_page_interpret(&p, (const unsigned char *)marked, strlen(marked), NULL,
        123, metric, NULL, raw_collect, &raw, &e) && raw.len == 6);
    for (size_t i = 0; i < raw.len; i++) {
        near(raw.raw[i].origin.x, control.raw[i].origin.x);
        near(raw.raw[i].origin.y, control.raw[i].origin.y);
        near(raw.raw[i].advance.x, control.raw[i].advance.x);
        same_matrix(raw.raw[i].after, control.raw[i].after);
        same_matrix(raw.raw[i].rendering_matrix, control.raw[i].rendering_matrix);
    }
}

static void budgets_failures_and_items(void) {
    pdf_error e; pdf_limits l; pdf_limits_default(&l);
    trace t = {.abort_at = SIZE_MAX}; pdf_font_text_totals totals = {0};
    /* Suppressed empty and unmapped events consume events but no UTF-8. */
    const char *s = "BT /F1 10 Tf " EMPTY "() Tj <00> Tj EMC (A) Tj ET";
    l.max_total_decoded_size = 1;
    assert(bridge_run(one(s), &l, &totals, &t, &e));
    assert(t.len == 1 && t.replacements == 0 && totals.events == 3 && totals.utf8_bytes == 1);
    pdf_limits_default(&l);
    char overflow[1024];
    snprintf(overflow, sizeof(overflow), "BT /F1 %.0f.0 Tf %.0f.0 Tz " EMPTY "(A) Tj EMC ET", 1e200, 1e200);
    totals = (pdf_font_text_totals){0}; t = (trace){.abort_at = SIZE_MAX};
    assert(!bridge_run(one(overflow), NULL, &totals, &t, &e));
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "non-finite") && t.len == 0);
    const char *invalid_fonts[] = {
        "<< /Type /Font /Subtype /Type1 /BaseFont /Test /FirstChar 65 /LastChar 65 /Widths [600] >>"
    };
    test_page bad_width = {RES, "BT /F1 12 Tf " EMPTY "(B) Tj EMC ET", NULL, NULL};
    totals = (pdf_font_text_totals){0}; t = (trace){.abort_at = SIZE_MAX};
    assert(!bridge_run(build_pdf(&bad_width, 1, invalid_fonts, 1, NULL), NULL, &totals, &t, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && strstr(e.message, "metrics") && t.len == 0);
    test_page bad_gs = {"<< /Font << /F1 5 0 R >> /ExtGState << /G << /Font [5 0 R 12] >> >> >>",
        EMPTY "/G gs EMC", NULL, NULL};
    const char *valid_fonts[] = {FONT};
    totals = (pdf_font_text_totals){0};
    assert(!bridge_run(build_pdf(&bad_gs, 1, valid_fonts, 1, NULL), NULL, &totals, &t, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && strstr(e.message, "ExtGState") && t.len == 0);
    totals = (pdf_font_text_totals){.events = l.max_container_entries - 1};
    t = (trace){.abort_at = SIZE_MAX};
    assert(!bridge_run(one(s), &l, &totals, &t, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && t.len == 0 && strstr(e.message, "event count"));
    const char *bad[] = {
        "BT " EMPTY "(A) Tj EMC ET", "BT /F1 10 Tf " EMPTY "(A) Tj ET EMC",
        EMPTY "/Bad gs EMC", EMPTY "0 g EMC",
        EMPTY "/F3 10 Tf EMC", "BT /F1 10 Tf " EMPTY "(A) Tj /Span /P BDC EMC EMC ET"
    };
    const pdf_error_code codes[] = {PDF_ERROR_MALFORMED, PDF_ERROR_MALFORMED,
        PDF_ERROR_MALFORMED, PDF_ERROR_UNSUPPORTED, PDF_ERROR_MALFORMED, PDF_ERROR_UNSUPPORTED};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        totals = (pdf_font_text_totals){0}; t = (trace){.abort_at = SIZE_MAX};
        assert(!bridge_run(one(bad[i]), NULL, &totals, &t, &e) && e.code == codes[i] && t.len == 0);
    }
    for (size_t i = 0; i < 3; i++) {
        totals = (pdf_font_text_totals){0}; t = (trace){.abort_at = 0,
            .abort_code = i == 0 ? 0 : i == 1 ? PDF_ERROR_RESOURCE_LIMIT : PDF_ERROR_OUT_OF_MEMORY};
        assert(!bridge_run(one("BT /F1 10 Tf " EMPTY "(B) Tj EMC (A) Tj ET"), NULL, &totals, &t, &e));
        assert(e.code == (i == 0 ? PDF_ERROR_MALFORMED : t.abort_code));
        assert(totals.events == 2 && t.len == 0);
    }
    pdf_text_items items = {0};
    assert(extract(one(s), &items, &e));
    assert(items.len == 1 && items.items[0].source_order == 2 && items.items[0].sequence == 0);
    assert(items.items[0].replacements == 0 && strcmp((const char *)items.items[0].text, "A") == 0);
    near(items.items[0].x, 6); pdf_text_items_free(&items);
    /* Later unbalanced, malformed and unsupported pages discard all items. */
    const char *later[] = {EMPTY, "/Span << /ActualText null >> BDC EMC", "/Span /P BDC EMC"};
    test_page pages[] = {{"<< /Font << /F1 7 0 R >> >>", "BT /F1 10 Tf (A) Tj ET", NULL, NULL},
        {"<< /Font << /F1 7 0 R >> >>", NULL, NULL, NULL}};
    const char *extra[] = {FONT};
    for (size_t i = 0; i < 3; i++) {
        pages[1].content = later[i];
        assert(!extract(build_pdf(pages, 2, extra, 1, NULL), &items, &e));
        assert(items.len == 0 && !items.items && !items.bytes);
        assert(e.code == (i == 2 ? PDF_ERROR_UNSUPPORTED : PDF_ERROR_MALFORMED));
    }
    pages[0].content = EMPTY "BT /F1 10 Tf (B) Tj ET EMC";
    pages[1].content = "BT /F1 10 Tf (A) Tj ET";
    assert(extract(build_pdf(pages, 2, extra, 1, NULL), &items, &e));
    assert(items.len == 1 && items.items[0].page == 2 && items.items[0].sequence == 0);
    pdf_text_items_free(&items);
    pages[0].content = EMPTY "BT /F1 10 Tf (B) Tj ET"; pages[1].content = "EMC";
    assert(!extract(build_pdf(pages, 2, extra, 1, NULL), &items, &e) && e.code == PDF_ERROR_MALFORMED);
    /* Contents stream concatenation can pair scopes within one page. */
    const char *second = "(B) Tj EMC (A) Tj ET";
    char stream[256]; snprintf(stream, sizeof(stream), "<< /Length %zu >>\nstream\n%s\nendstream", strlen(second), second);
    const char *split[] = {FONT, stream};
    test_page p = {RES, "BT /F1 10 Tf " EMPTY "(C) Tj", NULL, "[4 0 R 6 0 R]"};
    assert(extract(build_pdf(&p, 1, split, 2, NULL), &items, &e));
    assert(items.len == 1 && items.items[0].source_order == 2); near(items.items[0].x, 12);
    pdf_text_items_free(&items);
}

static void cmap_budget(void) {
    const char *map = "begincmap 1 begincodespacerange <0000> <FFFF> endcodespacerange "
        "1 beginbfchar <0001> <4E2D> endbfchar endcmap";
    char stream[512]; snprintf(stream, sizeof(stream), "<< /Length %zu >>\nstream\n%s\nendstream", strlen(map), map);
    const char *extra[] = {
        "<< /Type /Font /Subtype /Type0 /BaseFont /Chinese /Encoding /Identity-H /DescendantFonts [8 0 R] /ToUnicode 9 0 R >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Chinese /DW 1000 >>", stream
    };
    test_page pages[] = {
        {"<< /Font << /F1 7 0 R >> >>", EMPTY "BT /F1 12 Tf <0002> Tj ET EMC", NULL, NULL},
        {"<< /Font << /F1 7 0 R >> >>", EMPTY "BT /F1 12 Tf <0002> Tj ET EMC", NULL, NULL}
    };
    const char *path = build_pdf(pages, 2, extra, 3, NULL);
    trace t = {.abort_at = SIZE_MAX}; pdf_font_text_totals totals = {0}; pdf_error e;
    assert(bridge_run(path, NULL, &totals, &t, &e));
    assert(t.len == 0 && totals.events == 2 && totals.utf8_bytes == 0 && totals.cmap_bytes == 2 * strlen(map));
    pdf_limits l; pdf_limits_default(&l);
    totals = (pdf_font_text_totals){.cmap_bytes = l.max_total_decoded_size - strlen(map)};
    assert(!bridge_run(path, &l, &totals, &t, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && totals.events == 1 && t.len == 0);
}

int main(void) {
    syntax(); policy(); geometry_and_nesting(); budgets_failures_and_items(); cmap_budget();
    remove_test_pdf();
    puts("marked content tests passed");
    return 0;
}
