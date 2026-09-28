#include "pdf_builder.h"
#include "../src/font.h"

#include <math.h>

typedef struct {
    pdf_document doc;
    pdf_pages pages;
    pdf_font_context *ctx;
    size_t off[16];
} fixture;

#define RES "<< /Font << /F1 5 0 R >> >>"
#define FONT(extra) "<< /Type /Font /Subtype /Type1 /BaseFont /Test " extra " >>"
#define ABC "/FirstChar 65 /LastChar 67 /Widths [600 650 700]"

static const pdf_font *load(fixture *x, const char *resources, const char *const *extras,
                            size_t n, const pdf_limits *limits, pdf_error *e) {
    test_page page = {resources, NULL, NULL, NULL};
    const char *path = build_pdf(&page, 1, extras, n, x->off);
    pdf_error_init(e);
    assert(pdf_document_open(&x->doc, path, NULL, e)); /* limits apply to the font context */
    assert(pdf_pages_load(&x->doc, &x->pages, e));
    x->ctx = pdf_font_context_create(&x->doc, x->pages.items[0].resources, x->off[3], limits, e);
    assert(x->ctx);
    return pdf_font_context_resolve(x->ctx, (const unsigned char *)"F1", 2, e);
}
static void unload(fixture *x) {
    pdf_font_context_destroy(x->ctx);
    pdf_pages_free(&x->pages);
    pdf_document_close(&x->doc);
    *x = (fixture){0};
}
/* Font object 5 fails to load with the given code; returns error offset. */
static size_t expect_font(const char *font, pdf_error_code code) {
    fixture x = {0};
    pdf_error e;
    const char *extras[] = {font};
    assert(load(&x, RES, extras, 1, NULL, &e) == NULL);
    if (e.code != code) fprintf(stderr, "font %s: %s\n", font, e.message);
    assert(e.code == code);
    assert(strcmp(e.module, "font") == 0 || strcmp(e.module, "document") == 0);
    size_t offset = e.offset;
    /* Destroy-only after failure: later lookups keep failing, never succeed. */
    pdf_error again;
    pdf_error_init(&again);
    assert(!pdf_font_context_resolve(x.ctx, (const unsigned char *)"F1", 2, &again));
    assert(again.code == PDF_ERROR_MALFORMED);
    assert(offset == x.off[5] || code == PDF_ERROR_RESOURCE_LIMIT);
    unload(&x);
    return offset;
}
static void width_is(const pdf_font *f, unsigned char code, double expected,
                     pdf_font_width_source source) {
    pdf_error e;
    pdf_error_init(&e);
    double w = NAN;
    pdf_font_width_source got;
    assert(pdf_font_width(f, code, &w, &got, &e));
    assert(w == expected && got == source);
}

static void test_widths_and_first_char(void) {
    fixture x = {0};
    pdf_error e;
    const char *extras[] = {FONT(ABC)};
    const pdf_font *f = load(&x, RES, extras, 1, NULL, &e);
    assert(f && e.code == PDF_ERROR_NONE);
    width_is(f, 65, 600, PDF_FONT_WIDTH_TABLE);
    width_is(f, 66, 650, PDF_FONT_WIDTH_TABLE);
    width_is(f, 67, 700, PDF_FONT_WIDTH_TABLE);
    /* Case 1: 12pt advances 7.2/7.8/8.4, 23.4 total (M8 formula, spacing 0). */
    double total = 0;
    for (unsigned char c = 65; c <= 67; c++) {
        double w; assert(pdf_font_width(f, c, &w, NULL, &e)); total += w / 1000 * 12;
    }
    assert(fabs(total - 23.4) < 1e-9);
    /* Out of range with no descriptor: unsupported, located at the font. */
    double w;
    assert(!pdf_font_width(f, 64, &w, NULL, &e) && e.code == PDF_ERROR_UNSUPPORTED);
    assert(e.offset == x.off[5]);
    pdf_error_init(&e);
    assert(!pdf_font_width(f, 68, &w, NULL, &e) && e.code == PDF_ERROR_UNSUPPORTED);
    pdf_font_info info;
    assert(pdf_font_get_info(f, &info));
    assert(info.subtype == PDF_FONT_TYPE1 && info.encoding == PDF_FONT_ENCODING_ASCII_FALLBACK);
    assert(info.first_char == 65 && info.last_char == 67 && info.missing == PDF_FONT_MISSING_NONE);
    assert(info.base_font_len == 4 && memcmp(info.base_font, "Test", 4) == 0);
    assert(info.offset == x.off[5]);
    /* Cache: the same name returns the same immutable handle. */
    pdf_error_init(&e);
    assert(pdf_font_context_resolve(x.ctx, (const unsigned char *)"F1", 2, &e) == f);
    unload(&x);
}

static void test_zero_negative_real_and_boundaries(void) {
    fixture x = {0};
    pdf_error e;
    const char *extras[] = {FONT("/FirstChar 0 /LastChar 255 /Widths 6 0 R"), NULL};
    char widths[256 * 8 + 8] = "[";
    for (int i = 0; i < 256; i++) {
        char item[16];
        snprintf(item, sizeof(item), "%s%s", i ? " " : "",
                 i == 0 ? "0" : i == 1 ? "-12.5" : i == 255 ? "999.25" : "500");
        strcat(widths, item);
    }
    strcat(widths, "]");
    extras[1] = widths;
    const pdf_font *f = load(&x, RES, extras, 2, NULL, &e);
    assert(f);
    width_is(f, 0, 0, PDF_FONT_WIDTH_TABLE);
    width_is(f, 1, -12.5, PDF_FONT_WIDTH_TABLE);
    width_is(f, 255, 999.25, PDF_FONT_WIDTH_TABLE);
    width_is(f, 128, 500, PDF_FONT_WIDTH_TABLE);
    unload(&x);
}

static void test_missing_width_provenance(void) {
    fixture x = {0};
    pdf_error e;
    const char *explicit_font[] = {FONT(ABC " /FontDescriptor 6 0 R"),
                                   "<< /Type /FontDescriptor /Flags 32 /MissingWidth 250 >>"};
    const pdf_font *f = load(&x, RES, explicit_font, 2, NULL, &e);
    assert(f);
    width_is(f, 65, 600, PDF_FONT_WIDTH_TABLE);
    width_is(f, 0x20, 250, PDF_FONT_WIDTH_MISSING);
    width_is(f, 0xA0, 250, PDF_FONT_WIDTH_MISSING);
    pdf_font_info info; pdf_font_get_info(f, &info);
    assert(info.missing == PDF_FONT_MISSING_EXPLICIT && info.missing_width == 250);
    unload(&x);

    const char *explicit_zero[] = {FONT(ABC " /FontDescriptor << /MissingWidth 6 0 R >>"), "0"};
    f = load(&x, RES, explicit_zero, 2, NULL, &e);
    assert(f);
    width_is(f, 0x20, 0, PDF_FONT_WIDTH_MISSING);
    unload(&x);

    const char *default_zero[] = {FONT(ABC " /FontDescriptor << /Type /FontDescriptor >>")};
    f = load(&x, RES, default_zero, 1, NULL, &e);
    assert(f);
    width_is(f, 0x20, 0, PDF_FONT_WIDTH_DESCRIPTOR_DEFAULT_ZERO);
    width_is(f, 66, 650, PDF_FONT_WIDTH_TABLE);
    pdf_font_get_info(f, &info);
    assert(info.missing == PDF_FONT_MISSING_DEFAULT_ZERO);
    unload(&x);
}

static void test_reference_leaves_and_aliases(void) {
    fixture x = {0};
    pdf_error e;
    /* /Font dict by ref, alias F2 -> same indirect font, ref-to-ref font, and
     * FirstChar/LastChar/Widths/Encoding/element values through references. */
    const char *extras[] = {
        "<< /F1 6 0 R /F2 6 0 R /F3 7 0 R >>",
        "<< /Type /Font /Subtype /TrueType /BaseFont /ABCDEF+Arial /FirstChar 8 0 R "
        "/LastChar 9 0 R /Widths 10 0 R /Encoding 11 0 R >>",
        "6 0 R", "65", "66", "[600 12 0 R]", "/WinAnsiEncoding", "625.5"};
    const pdf_font *f = load(&x, "<< /Font 5 0 R >>", extras, 8, NULL, &e);
    assert(f);
    width_is(f, 66, 625.5, PDF_FONT_WIDTH_TABLE);
    pdf_font_info info; pdf_font_get_info(f, &info);
    assert(info.subtype == PDF_FONT_TRUETYPE && info.encoding == PDF_FONT_ENCODING_WIN_ANSI);
    assert(info.offset == x.off[6]);
    assert(pdf_font_context_resolve(x.ctx, (const unsigned char *)"F2", 2, &e) == f);
    /* F3 reaches object 6 through another indirect hop: separate cache key. */
    const pdf_font *g = pdf_font_context_resolve(x.ctx, (const unsigned char *)"F3", 2, &e);
    assert(g && e.code == PDF_ERROR_NONE);
    width_is(g, 65, 600, PDF_FONT_WIDTH_TABLE);
    unload(&x);
}

static void test_name_with_nul(void) {
    fixture x = {0};
    pdf_error e;
    const char *extras[] = {FONT(ABC)};
    assert(load(&x, "<< /Font << /F#001 5 0 R >> >>", extras, 1, NULL, &e) == NULL);
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "not found"));
    unload(&x);
    pdf_error_init(&e);
    test_page page = {"<< /Font << /F#001 5 0 R >> >>", NULL, NULL, NULL};
    const char *path = build_pdf(&page, 1, extras, 1, x.off);
    assert(pdf_document_open(&x.doc, path, NULL, &e) && pdf_pages_load(&x.doc, &x.pages, &e));
    x.ctx = pdf_font_context_create(&x.doc, x.pages.items[0].resources, x.off[3], NULL, &e);
    const pdf_font *f = pdf_font_context_resolve(x.ctx, (const unsigned char *)"F\0" "1", 3, &e);
    assert(f);
    pdf_font_info info; pdf_font_get_info(f, &info);
    assert(info.resource_name_len == 3 && memcmp(info.resource_name, "F\0" "1", 3) == 0);
    /* Dump is hex only: no raw NUL/control bytes reach the stream. */
    char *out = NULL; size_t len = 0;
    FILE *s = open_memstream(&out, &len);
    assert(pdf_font_dump(s, f, &e));
    fclose(s);
    assert(strlen(out) == len && strstr(out, "\"font_len\":3,\"font\":\"460031\""));
    assert(strstr(out, "\"missing\":\"none\""));
    free(out);
    unload(&x);
}

static void test_resource_errors(void) {
    const struct { const char *resources; pdf_error_code code; int at_page; } cases[] = {
        {NULL, PDF_ERROR_MALFORMED, 1},
        {"<< >>", PDF_ERROR_MALFORMED, 1},
        {"<< /Font 7 >>", PDF_ERROR_MALFORMED, 1},
        {"<< /Font << /F2 5 0 R >> >>", PDF_ERROR_MALFORMED, 1},
        {"<< /Font 6 0 R >>", PDF_ERROR_MALFORMED, 0}, /* stream is not a dictionary */
        {"<< /Font << /F1 7 0 R >> >>", PDF_ERROR_MALFORMED, 1}, /* absent object: container */
        {"<< /Font << /F1 << /Type /Font /Subtype /Type3 >> >> >>", PDF_ERROR_UNSUPPORTED, 1},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fixture x = {0};
        pdf_error e;
        const char *extras[] = {FONT(ABC), "<< /Length 0 >>\nstream\n\nendstream"};
        assert(load(&x, cases[i].resources, extras, 2, NULL, &e) == NULL);
        assert(e.code == cases[i].code);
        if (cases[i].at_page) assert(e.offset == x.off[3]);
        else assert(e.offset != 0 && e.offset != x.off[3]);
        unload(&x);
    }
}

static void test_font_policy_errors(void) {
    const pdf_error_code M = PDF_ERROR_MALFORMED, U = PDF_ERROR_UNSUPPORTED;
    const struct { const char *font; pdf_error_code code; } cases[] = {
        {"<< /Subtype /Type1 /BaseFont /A " ABC " >>", M},
        {"<< /Type /XObject /Subtype /Type1 /BaseFont /A " ABC " >>", M},
        {"<< /Type /Font /BaseFont /A " ABC " >>", M},
        {"<< /Type /Font /Subtype (Type1) /BaseFont /A " ABC " >>", M},
        {"<< /Type /Font /Subtype /Type3 /BaseFont /A " ABC " >>", U},
        {"<< /Type /Font /Subtype /MMType1 /BaseFont /A " ABC " >>", U},
        {"<< /Type /Font /Subtype /Type0 /BaseFont /A " ABC " >>", U},
        {"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /A " ABC " >>", U},
        {"<< /Type /Font /Subtype /Type1 " ABC " >>", M},
        {"<< /Type /Font /Subtype /Type1 /BaseFont 3 " ABC " >>", M},
        {"<< /Type /Font /Subtype /Type1 /BaseFont /Symbol " ABC " >>", U},
        {"<< /Type /Font /Subtype /Type1 /BaseFont /ZapfDingbats " ABC " >>", U},
        {"<< /Type /Font /Subtype /TrueType /BaseFont /ABCDEF+Symbol " ABC " >>", U},
        {FONT(ABC " /ToUnicode << >>"), U},
        {FONT(ABC " /Encoding /MacRomanEncoding"), U},
        {FONT(ABC " /Encoding /MacExpertEncoding"), U},
        {FONT(ABC " /Encoding /StandardEncoding"), U},
        {FONT(ABC " /Encoding /Identity-H"), U},
        {FONT(ABC " /Encoding 1"), M},
        {FONT(ABC " /Encoding << /BaseEncoding /WinAnsiEncoding /Differences [] >>"), U},
        {FONT(ABC " /Encoding << /Type /Encoding >>"), U},
        {FONT(ABC " /Encoding << /Type /Font /BaseEncoding /WinAnsiEncoding >>"), M},
        {FONT(ABC " /Encoding << /BaseEncoding (WinAnsiEncoding) >>"), M},
        {FONT(ABC " /Encoding << /BaseEncoding /MacRomanEncoding >>"), U},
        {FONT(ABC " /FontDescriptor 3"), M},
        {FONT(ABC " /FontDescriptor << /Flags 4 >>"), U},
        {FONT(ABC " /FontDescriptor << /Flags 36 >>"), U},
        {FONT(ABC " /FontDescriptor << /Flags 32.0 >>"), M},
        {FONT(ABC " /FontDescriptor << /MissingWidth /None >>"), M},
        {FONT(""), U},
        {"<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>", U},
        {FONT("/FontDescriptor << /MissingWidth 500 >>"), U},
        {FONT("/FirstChar 65 /LastChar 67"), M},
        {FONT("/Widths [1 2 3]"), M},
        {FONT("/FirstChar 65.0 /LastChar 67 /Widths [1 2 3]"), M},
        {FONT("/FirstChar 65 /LastChar (C) /Widths [1 2 3]"), M},
        {FONT("/FirstChar -1 /LastChar 1 /Widths [1 2 3]"), M},
        {FONT("/FirstChar 255 /LastChar 256 /Widths [1 2]"), M},
        {FONT("/FirstChar 67 /LastChar 65 /Widths [1 2 3]"), M},
        {FONT("/FirstChar 65 /LastChar 67 /Widths [1 2]"), M},
        {FONT("/FirstChar 65 /LastChar 67 /Widths [1 2 3 4]"), M},
        {FONT("/FirstChar 65 /LastChar 67 /Widths [1 /Two 3]"), M},
        {FONT("/FirstChar 65 /LastChar 67 /Widths << >>"), M},
        {FONT("/FirstChar 65 /LastChar 67 /Widths [1 2 null]"), M},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        expect_font(cases[i].font, cases[i].code);
}

static void test_reference_cycles_and_depth(void) {
    /* Self-cycle and two-object cycle are malformed; locate the last object. */
    fixture x = {0};
    pdf_error e;
    const char *self[] = {"<< /F1 6 0 R >>", "6 0 R"};
    assert(!load(&x, "<< /Font 5 0 R >>", self, 2, NULL, &e));
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "cycle") && e.offset == x.off[6]);
    unload(&x);
    const char *pair[] = {FONT("/FirstChar 6 0 R /LastChar 67 /Widths [1 2 3]"), "7 0 R", "6 0 R"};
    assert(!load(&x, RES, pair, 3, NULL, &e));
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "cycle"));
    assert(e.offset == x.off[6] || e.offset == x.off[7]);
    unload(&x);
    /* A long acyclic chain obeys max_nesting_depth as a resource limit. */
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_nesting_depth = 3;
    const char *chain[] = {FONT("/FirstChar 6 0 R /LastChar 67 /Widths [1 2 3]"),
                           "7 0 R", "8 0 R", "9 0 R", "65"};
    assert(!load(&x, RES, chain, 5, &limits, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    unload(&x);
    limits.max_nesting_depth = 8;
    assert(load(&x, RES, chain, 5, &limits, &e));
    unload(&x);
}

static void test_cache_limits(void) {
    fixture x = {0};
    pdf_error e;
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_container_entries = 1;
    const char *extras[] = {FONT(ABC)};
    assert(load(&x, "<< /Font << /F1 5 0 R /F2 5 0 R >> >>", extras, 1, &limits, &e));
    assert(!pdf_font_context_resolve(x.ctx, (const unsigned char *)"F2", 2, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    unload(&x);
}

/* Independent expectation for all 256 WinAnsi codes (PDF Reference Appendix D). */
static unsigned expected_win_ansi(unsigned b) {
    static const unsigned high[32] = {
        0x20AC, 0x2022, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x2022, 0x017D, 0x2022,
        0x2022, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x2022, 0x017E, 0x0178};
    if (b < 0x20) return 0xFFFD;
    if (b == 0x7F) return 0x2022;
    if (b >= 0x80 && b < 0xA0) return high[b - 0x80];
    return b;
}
static void utf8_of(unsigned cp, unsigned char *out, size_t *n) {
    if (cp < 0x80) { out[0] = (unsigned char)cp; *n = 1; }
    else if (cp < 0x800) { out[0] = (unsigned char)(0xC0 | cp >> 6); out[1] = (unsigned char)(0x80 | (cp & 63)); *n = 2; }
    else { out[0] = (unsigned char)(0xE0 | cp >> 12); out[1] = (unsigned char)(0x80 | ((cp >> 6) & 63));
           out[2] = (unsigned char)(0x80 | (cp & 63)); *n = 3; }
}
static void decode_is(const pdf_font *f, const char *in, size_t n, const char *out, size_t m,
                      size_t replacements) {
    pdf_error e;
    pdf_error_init(&e);
    pdf_font_utf8 r = {0};
    assert(pdf_font_decode(f, (const unsigned char *)in, n, &r, &e));
    assert(r.len == m && memcmp(r.data, out, m) == 0 && r.data[m] == 0);
    assert(r.replacements == replacements);
    pdf_font_utf8_free(&r);
    assert(r.data == NULL && r.len == 0 && r.replacements == 0);
}

static void test_win_ansi_decode(void) {
    fixture x = {0};
    pdf_error e;
    const char *extras[] = {FONT("/FirstChar 32 /LastChar 32 /Widths [250] /Encoding /WinAnsiEncoding")};
    const pdf_font *f = load(&x, RES, extras, 1, NULL, &e);
    assert(f);
    /* Case 3: 41 80 E9 -> 41 E2 82 AC C3 A9; three raw codes, six UTF-8 bytes. */
    decode_is(f, "\x41\x80\xE9", 3, "\x41\xE2\x82\xAC\xC3\xA9", 6, 0);
    /* Case 4: unused codes are bullets, NUL is a replacement. */
    decode_is(f, "\x7F\x81\x8D\x8F\x90\x9D", 6,
              "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2", 18, 0);
    decode_is(f, "\0", 1, "\xEF\xBF\xBD", 3, 1);
    decode_is(f, "\n\t", 2, "\xEF\xBF\xBD\xEF\xBF\xBD", 6, 2);
    decode_is(f, "\xA0\xAD", 2, "\xC2\xA0\xC2\xAD", 4, 0);
    decode_is(f, "\x91\x92\x93\x94\x8C\x9C", 6,
              "\xE2\x80\x98\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x9D\xC5\x92\xC5\x93", 16, 0);
    decode_is(f, "", 0, "", 0, 0);
    /* All 256 codes against the independent table. */
    for (unsigned b = 0; b < 256; b++) {
        unsigned char in = (unsigned char)b, out[4];
        size_t n;
        utf8_of(expected_win_ansi(b), out, &n);
        decode_is(f, (const char *)&in, 1, (const char *)out, n, b < 0x20);
    }
    /* Width lookup is by raw code and independent of UTF-8 length. */
    width_is(f, 0x20, 250, PDF_FONT_WIDTH_TABLE);
    /* Result must start empty. */
    pdf_font_utf8 dirty = {0};
    dirty.len = 1;
    pdf_error_init(&e);
    assert(!pdf_font_decode(f, (const unsigned char *)"A", 1, &dirty, &e));
    unload(&x);

    const char *dict[] = {FONT(ABC " /Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding >>")};
    f = load(&x, RES, dict, 1, NULL, &e);
    assert(f);
    decode_is(f, "\x80", 1, "\xE2\x82\xAC", 3, 0);
    unload(&x);
}

static void test_ascii_fallback_and_budget(void) {
    fixture x = {0};
    pdf_error e;
    const char *extras[] = {FONT(ABC)};
    const pdf_font *f = load(&x, RES, extras, 1, NULL, &e);
    assert(f);
    decode_is(f, " A~", 3, " A~", 3, 0);
    decode_is(f, "\x80\xE9\x7F\x1F", 4, "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD", 12, 4);
    unload(&x);

    /* Single-string UTF-8 output is capped by max_token_size (decoded length). */
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_token_size = 5;
    const char *win[] = {FONT(ABC " /Encoding /WinAnsiEncoding")};
    test_page page = {"<< /Font << /F 5 0 R >> >>", NULL, NULL, NULL};
    const char *path = build_pdf(&page, 1, win, 1, x.off);
    pdf_error_init(&e);
    assert(pdf_document_open(&x.doc, path, NULL, &e) && pdf_pages_load(&x.doc, &x.pages, &e));
    x.ctx = pdf_font_context_create(&x.doc, x.pages.items[0].resources, x.off[3], &limits, &e);
    f = pdf_font_context_resolve(x.ctx, (const unsigned char *)"F", 1, &e);
    assert(f);
    pdf_font_utf8 r = {0};
    assert(pdf_font_decode(f, (const unsigned char *)"AB\xE9", 3, &r, &e) && r.len == 4);
    pdf_font_utf8_free(&r);
    assert(!pdf_font_decode(f, (const unsigned char *)"A\x80\xE9", 3, &r, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && r.data == NULL && r.len == 0);
    pdf_font_context_destroy(x.ctx);
    /* Resource names longer than max_token_size fail before any lookup. */
    pdf_error_init(&e);
    x.ctx = pdf_font_context_create(&x.doc, x.pages.items[0].resources, x.off[3], &limits, &e);
    assert(!pdf_font_context_resolve(x.ctx, (const unsigned char *)"Longer", 6, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_font_context_destroy(x.ctx);
    /* A limit smaller than one encoded scalar must not underflow. */
    limits.max_token_size = 2;
    pdf_error_init(&e);
    x.ctx = pdf_font_context_create(&x.doc, x.pages.items[0].resources, x.off[3], &limits, &e);
    f = pdf_font_context_resolve(x.ctx, (const unsigned char *)"F", 1, &e);
    assert(f);
    assert(pdf_font_decode(f, (const unsigned char *)"AB", 2, &r, &e) && r.len == 2);
    pdf_font_utf8_free(&r);
    assert(!pdf_font_decode(f, (const unsigned char *)"\x80", 1, &r, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && r.data == NULL);
    unload(&x);
}

static void test_same_name_two_pages(void) {
    test_page pages[2] = {{"<< /Font << /F1 7 0 R >> >>", NULL, NULL, NULL},
                          {"<< /Font << /F1 8 0 R >> >>", NULL, NULL, NULL}};
    const char *extras[] = {FONT("/FirstChar 65 /LastChar 65 /Widths [500]"),
                            FONT("/FirstChar 65 /LastChar 65 /Widths [900]")};
    size_t off[16];
    const char *path = build_pdf(pages, 2, extras, 2, off);
    pdf_document doc = {0};
    pdf_pages list = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(pdf_document_open(&doc, path, NULL, &e) && pdf_pages_load(&doc, &list, &e));
    const double expected[] = {500, 900};
    for (size_t i = 0; i < 2; i++) {
        pdf_font_context *c = pdf_font_context_create(&doc, list.items[i].resources, off[3 + 2 * i], NULL, &e);
        const pdf_font *f = pdf_font_context_resolve(c, (const unsigned char *)"F1", 2, &e);
        assert(f);
        width_is(f, 65, expected[i], PDF_FONT_WIDTH_TABLE);
        pdf_font_context_destroy(c);
    }
    pdf_pages_free(&list);
    pdf_document_close(&doc);
}

int main(void) {
    test_widths_and_first_char();
    test_zero_negative_real_and_boundaries();
    test_missing_width_provenance();
    test_reference_leaves_and_aliases();
    test_name_with_nul();
    test_resource_errors();
    test_font_policy_errors();
    test_reference_cycles_and_depth();
    test_cache_limits();
    test_win_ansi_decode();
    test_ascii_fallback_and_budget();
    test_same_name_two_pages();
    pdf_font_context_destroy(NULL);
    pdf_font_utf8_free(NULL);
    remove_test_pdf();
    puts("font tests passed");
    return 0;
}
