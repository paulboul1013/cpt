/* M9 page bridge tests and staged diagnostic probe. With one argument it
 * stages the whole document through the real font adapter and prints the
 * trace only after every page succeeds. */
#include "pdf_builder.h"
#include "../src/contents.h"
#include "../src/font_text.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>

typedef struct {
    double x, y, advance;
    unsigned char utf8[32];
    size_t utf8_len, replacements, bytes_len;
    int mode;
} record;
typedef struct {
    record items[64];
    size_t len, abort_at;
    int fail_with_limit, truncate_preview;
    FILE *stage;
    size_t page;
} collector;

static int collect(void *context, const pdf_font_text_event *ev, pdf_error *e) {
    collector *c = context;
    if (c->len == c->abort_at) {
        if (c->fail_with_limit)
            pdf_error_set(e, PDF_ERROR_RESOURCE_LIMIT, 0, "test-consumer", "consumer budget");
        return 0;
    }
    if (c->truncate_preview) {
        /* A copied event whose UTF-8 ends mid-sequence must dump safely. */
        pdf_font_text_event copy = *ev;
        copy.utf8 = (const unsigned char *)"A\xE2\x82";
        copy.utf8_len = 3;
        char *out = NULL; size_t size = 0;
        FILE *s = open_memstream(&out, &size);
        assert(s && pdf_font_text_event_dump(s, &copy, 1, e));
        fclose(s);
        assert(strstr(out, "\"preview\":\"A\\ufffd\\ufffd\""));
        free(out);
    }
    if (c->stage) return pdf_font_text_event_dump(c->stage, ev, c->page, e);
    assert(c->len < 64 && ev->utf8_len <= sizeof(c->items[0].utf8));
    record *r = &c->items[c->len++];
    r->x = ev->raw->origin.x; r->y = ev->raw->origin.y; r->advance = ev->raw->advance.x;
    memcpy(r->utf8, ev->utf8, ev->utf8_len);
    assert(ev->utf8[ev->utf8_len] == 0);
    r->utf8_len = ev->utf8_len; r->replacements = ev->replacements;
    r->bytes_len = ev->raw->bytes.len; r->mode = ev->raw->state.rendering_mode;
    return 1;
}

/* Whole document through M6 -> M7 -> M8 -> M9. */
static int run(const char *path, const pdf_limits *limits, pdf_font_text_totals *totals,
               collector *c, size_t *page_offset, pdf_error *e) {
    pdf_document doc = {0};
    pdf_pages pages = {0};
    pdf_error_init(e);
    int ok = pdf_document_open(&doc, path, NULL, e) && pdf_pages_load(&doc, &pages, e);
    pdf_contents_context contents;
    pdf_contents_context_init(&contents, &doc);
    for (size_t i = 0; ok && i < pages.len; i++) {
        pdf_contents_result bytes = {0};
        size_t offset = pdf_document_reference_offset(&doc, pages.items[i].reference);
        if (page_offset) *page_offset = offset;
        c->page = i + 1;
        ok = pdf_contents_read(&contents, &pages.items[i], &bytes, e) &&
             pdf_font_text_page_interpret(&doc, &pages.items[i], bytes.data, bytes.len,
                                          limits, offset, totals, collect, c, e);
        pdf_contents_result_free(&bytes);
    }
    pdf_pages_free(&pages);
    pdf_document_close(&doc);
    return ok;
}
static int run_one(const char *resources, const char *content, const char *const *extras,
                   size_t n, collector *c, pdf_error *e, size_t *page_offset) {
    test_page page = {resources, content, NULL, NULL};
    pdf_font_text_totals totals = {0};
    *c = (collector){0};
    c->abort_at = SIZE_MAX;
    return run(build_pdf(&page, 1, extras, n, NULL), NULL, &totals, c, page_offset, e);
}
static void near(double a, double b) { assert(fabs(a - b) <= 1e-8 + 1e-9 * fabs(b)); }

#define RES "<< /Font << /F1 5 0 R >> >>"
#define FONT(extra) "<< /Type /Font /Subtype /Type1 /BaseFont /Test " extra " >>"
#define ABC "/FirstChar 65 /LastChar 67 /Widths [600 650 700]"

static void test_numeric_cases(void) {
    collector c;
    pdf_error e;
    /* Case 1: 600/650/700 at 12pt -> 23.4. */
    const char *abc[] = {FONT(ABC)};
    assert(run_one(RES, "BT /F1 12 Tf 1 0 0 1 10 20 Tm (ABC) Tj (A) Tj ET", abc, 1, &c, &e, NULL));
    assert(c.len == 2);
    near(c.items[0].x, 10); near(c.items[0].y, 20); near(c.items[0].advance, 23.4);
    near(c.items[1].x, 33.4); near(c.items[1].advance, 7.2);
    assert(c.items[0].utf8_len == 3 && memcmp(c.items[0].utf8, "ABC", 3) == 0);
    /* Case 2: MissingWidth 250, Tw 3 applies to raw 0x20 only, not 0xA0. */
    const char *missing[] = {FONT(ABC " /Encoding /WinAnsiEncoding /FontDescriptor << /MissingWidth 250 >>")};
    assert(run_one(RES, "BT /F1 12 Tf 3 Tw ( ) Tj (\\240) Tj ET", missing, 1, &c, &e, NULL));
    assert(c.len == 2);
    near(c.items[0].advance, 6); near(c.items[1].advance, 3);
    assert(c.items[0].utf8_len == 1 && c.items[0].utf8[0] == 0x20);
    assert(c.items[1].utf8_len == 2 && memcmp(c.items[1].utf8, "\xC2\xA0", 2) == 0);
    /* Case 3: WinAnsi 41 80 E9 -> 6 UTF-8 bytes; advance from raw widths. */
    const char *win[] = {FONT("/FirstChar 65 /LastChar 65 /Widths [700] /Encoding /WinAnsiEncoding "
                              "/FontDescriptor << /MissingWidth 300 >>")};
    assert(run_one(RES, "BT /F1 10 Tf <4180E9> Tj <00> Tj ET", win, 1, &c, &e, NULL));
    near(c.items[0].advance, 13);
    assert(c.items[0].bytes_len == 3 && c.items[0].utf8_len == 6);
    assert(memcmp(c.items[0].utf8, "\x41\xE2\x82\xAC\xC3\xA9", 6) == 0);
    near(c.items[1].advance, 3);
    assert(c.items[1].utf8_len == 3 && c.items[1].replacements == 1);
    /* ASCII fallback replacement still advances by the raw code's width. */
    const char *ascii[] = {FONT("/FirstChar 128 /LastChar 128 /Widths [444]")};
    assert(run_one(RES, "BT /F1 10 Tf <80> Tj ET", ascii, 1, &c, &e, NULL));
    near(c.items[0].advance, 4.44);
    assert(c.items[0].replacements == 1 && memcmp(c.items[0].utf8, "\xEF\xBF\xBD", 3) == 0);
}

static void test_font_switching(void) {
    collector c;
    pdf_error e;
    const char *fonts[] = {FONT("/FirstChar 65 /LastChar 65 /Widths [500]"),
                           FONT("/FirstChar 65 /LastChar 65 /Widths [1000] /Encoding /WinAnsiEncoding")};
    /* q/Q restores F1 across BT, Tf size changes, and TJ adjustments. */
    const char *content =
        "/F1 10 Tf q /F2 10 Tf BT (A) Tj ET Q BT (A) Tj /F2 20 Tf (A) Tj ET "
        "BT [(A) -500 (A)] TJ ET";
    assert(run_one("<< /Font << /F1 5 0 R /F2 6 0 R /F9 << /Subtype /Type3 >> >> >>",
                   content, fonts, 2, &c, &e, NULL));
    assert(c.len == 5);
    near(c.items[0].advance, 10); near(c.items[1].advance, 5); near(c.items[2].advance, 20);
    near(c.items[3].advance, 20); near(c.items[4].x, 20 + 10);
    /* Nested q/Q restores each level; Q back to "no font" cannot show text. */
    const char *three[] = {FONT("/FirstChar 65 /LastChar 65 /Widths [100]"),
                           FONT("/FirstChar 65 /LastChar 65 /Widths [200]"),
                           FONT("/FirstChar 65 /LastChar 65 /Widths [300]")};
    assert(run_one("<< /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R >> >>",
                   "/F1 10 Tf q /F2 10 Tf q /F3 10 Tf BT (A) Tj ET Q BT (A) Tj ET Q BT (A) Tj ET",
                   three, 3, &c, &e, NULL));
    assert(c.len == 3);
    near(c.items[0].advance, 3); near(c.items[1].advance, 2); near(c.items[2].advance, 1);
    assert(!run_one("<< /Font << /F1 5 0 R >> >>", "q /F1 10 Tf Q BT (A) Tj ET", three, 1, &c, &e, NULL));
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "requires Tf") && c.len == 0);
    /* Font state spans Contents streams: Tf in the first, show in the second. */
    test_page page = {RES, "BT /F1 12 Tf", NULL, "[4 0 R 6 0 R]"};
    const char *extras[] = {FONT(ABC), "<< /Length 11 >>\nstream\n(CBA) Tj ET\nendstream"};
    pdf_font_text_totals totals = {0};
    c = (collector){0}; c.abort_at = SIZE_MAX;
    assert(run(build_pdf(&page, 1, extras, 2, NULL), NULL, &totals, &c, NULL, &e));
    assert(c.len == 1); near(c.items[0].advance, 23.4);
    assert(totals.events == 1 && totals.utf8_bytes == 3);
}

static void test_tf_validation(void) {
    collector c;
    pdf_error e;
    size_t page_offset;
    const char *type3[] = {"<< /Type /Font /Subtype /Type3 /BaseFont /T >>"};
    const char *shows[] = {"BT /F1 12 Tf () Tj ET", "BT /F1 12 Tf [] TJ ET",
                           "BT /F1 12 Tf [100 -20] TJ ET", "/F1 12 Tf"};
    for (size_t i = 0; i < 4; i++) {
        assert(!run_one(RES, shows[i], type3, 1, &c, &e, &page_offset));
        assert(e.code == PDF_ERROR_UNSUPPORTED && c.len == 0);
        assert(e.offset == page_offset && strstr(e.message, "decoded byte"));
        assert(strstr(e.message, "font byte") && strstr(e.message, "Type3"));
    }
    /* Missing binding: malformed at the Page, never at a decoded offset. */
    const char *abc[] = {FONT(ABC)};
    assert(!run_one(RES, "BT /F2 12 Tf ET", abc, 1, &c, &e, &page_offset));
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == page_offset);
    assert(strstr(e.message, "font resource not found"));
    /* Resolver failure inside a font field keeps its module and font byte. */
    const char *broken[] = {FONT("/FirstChar 65 /LastChar 65 /Widths 9 0 R")};
    assert(!run_one(RES, "BT /F1 12 Tf ET", broken, 1, &c, &e, &page_offset));
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == page_offset);
    assert(strstr(e.message, "font byte") && strstr(e.message, "(document)"));
    /* Width outside the table without a descriptor: unsupported metrics. */
    assert(!run_one(RES, "BT /F1 12 Tf (AZ) Tj ET", abc, 1, &c, &e, &page_offset));
    assert(e.code == PDF_ERROR_UNSUPPORTED && strstr(e.message, "font metrics") && c.len == 0);
    /* Earlier events were delivered before the failure; caller must stage. */
    assert(!run_one(RES, "BT /F1 12 Tf (A) Tj /F1 12 Tf (Z) Tj ET", abc, 1, &c, &e, NULL));
    assert(c.len == 1);
    /* Nonzero Rotate is rejected by the shared M8 page policy. */
    test_page rotated = {RES, "BT /F1 12 Tf (A) Tj ET", "/Rotate 90", NULL};
    pdf_font_text_totals totals = {0};
    c = (collector){0}; c.abort_at = SIZE_MAX;
    assert(!run(build_pdf(&rotated, 1, abc, 1, NULL), NULL, &totals, &c, NULL, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && strstr(e.message, "rotation"));
}

static void test_budgets_and_abort(void) {
    const char *abc[] = {FONT(ABC)};
    test_page pages[2] = {{"<< /Font << /F1 7 0 R >> >>", "BT /F1 12 Tf (ABC) Tj ET", NULL, NULL},
                          {"<< /Font << /F1 7 0 R >> >>", "BT /F1 12 Tf (AB) Tj ET", NULL, NULL}};
    const char *path = build_pdf(pages, 2, abc, 1, NULL);
    pdf_limits limits;
    pdf_limits_default(&limits);
    collector c = {0};
    c.abort_at = SIZE_MAX;
    pdf_error e;
    pdf_font_text_totals totals = {0};
    assert(run(path, &limits, &totals, &c, NULL, &e));
    assert(totals.utf8_bytes == 5 && totals.events == 2);
    /* Page 1 fits, page 2 crosses the document UTF-8 budget. */
    limits.max_total_decoded_size = 4;
    totals = (pdf_font_text_totals){0};
    c = (collector){0}; c.abort_at = SIZE_MAX;
    assert(!run(path, &limits, &totals, &c, NULL, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && strstr(e.message, "UTF-8") && c.len == 1);
    /* Event count is document-wide as well. */
    pdf_limits_default(&limits);
    totals = (pdf_font_text_totals){0, limits.max_container_entries - 1};
    c = (collector){0}; c.abort_at = SIZE_MAX;
    assert(!run(path, &limits, &totals, &c, NULL, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && c.len == 1);
    /* Consumer abort without an error becomes malformed. */
    totals = (pdf_font_text_totals){0};
    c = (collector){0}; c.abort_at = 1;
    assert(!run(path, NULL, &totals, &c, NULL, &e));
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "consumer"));
    /* A consumer's own error survives M8 and M7 wrapping (first error wins). */
    totals = (pdf_font_text_totals){0};
    c = (collector){0}; c.abort_at = 0; c.fail_with_limit = 1;
    assert(!run(path, NULL, &totals, &c, NULL, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && strstr(e.message, "consumer budget"));
    /* Dump preview is bounds-checked for truncated UTF-8. */
    totals = (pdf_font_text_totals){0};
    c = (collector){0}; c.abort_at = SIZE_MAX; c.truncate_preview = 1;
    assert(run(path, NULL, &totals, &c, NULL, &e) && c.len == 2);
}

/* Existing M8 fixtures through the real adapter match the M8 geometry table. */
static void test_m8_fixtures(void) {
    const char *paths[] = {"tests/fixtures/geometry-raw.pdf", "tests/fixtures/geometry-flate.pdf",
                           "tests/fixtures/geometry-array.pdf", "tests/fixtures/geometry-pages.pdf"};
    const double x[] = {36, 46, 46, 51.76, 61.96, 46, 46, 36, 36, 36};
    const double y[] = {250, 226, 208, 208, 208, 194, 172, 110, 76, 40};
    const double advance[] = {57.6, 57.6, 7.2, 7.2, 7.2, 22.08, 64.8, 72, 57.6, 100.8};
    for (size_t k = 0; k < 4; k++) {
        collector c = {0};
        c.abort_at = SIZE_MAX;
        pdf_error e;
        pdf_font_text_totals totals = {0};
        assert(run(paths[k], NULL, &totals, &c, NULL, &e));
        assert(c.len == (k == 3 ? 20 : 10));
        for (size_t i = 0; i < c.len; i++) {
            near(c.items[i].x, x[i % 10]); near(c.items[i].y, y[i % 10]);
            near(c.items[i].advance, advance[i % 10]);
            assert(c.items[i].utf8_len == c.items[i].bytes_len && c.items[i].replacements == 0);
        }
        assert(c.items[6].mode == 3 && memcmp(c.items[6].utf8, "INVISIBLE", 9) == 0);
    }
}

static int probe(const char *path) {
    char *buffer = NULL;
    size_t size = 0;
    collector *c = calloc(1, sizeof(*c));
    FILE *stage = open_memstream(&buffer, &size);
    if (!c || !stage) return 5;
    c->abort_at = SIZE_MAX;
    c->stage = stage;
    pdf_error e;
    pdf_font_text_totals totals = {0};
    int ok = run(path, NULL, &totals, c, NULL, &e);
    if (ok && ferror(stage)) { pdf_error_init(&e); pdf_error_set(&e, PDF_ERROR_IO, 0, "font-debug", "cannot stage trace"); ok = 0; }
    fclose(stage);
    free(c);
    if (!ok) { free(buffer); pdf_error_print(&e, stderr); return pdf_error_exit_code(&e); }
    int written = fwrite(buffer, 1, size, stdout) == size && fflush(stdout) == 0;
    free(buffer);
    return written ? 0 : 2;
}

int main(int argc, char **argv) {
    /* Honour the environment locale so runners can prove the dump ignores it. */
    if (argc == 2) { setlocale(LC_ALL, ""); return probe(argv[1]); }
    if (argc != 1) return 1;
    test_numeric_cases();
    test_font_switching();
    test_tf_validation();
    test_budgets_and_abort();
    test_m8_fixtures();
    remove_test_pdf();
    puts("font text tests passed");
    return 0;
}
