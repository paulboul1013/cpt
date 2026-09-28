/* Test-only helper: writes a small traditional-xref PDF to a temporary file so
 * font cases stay readable next to their assertions. All bytes come from the
 * test source; no external generator is involved. */
#ifndef PDF_TEST_BUILDER_H
#define PDF_TEST_BUILDER_H

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *resources; /* Dictionary text or "5 0 R"; NULL omits /Resources. */
    const char *content;   /* Raw content bytes, NUL-terminated; NULL = empty. */
    const char *extra;     /* Extra page dictionary entries, e.g. "/Rotate 90". */
    const char *contents;  /* /Contents override (e.g. "[4 0 R 7 0 R]"); NULL = own stream. */
} test_page;

/* Object layout: 1 Catalog, 2 Pages, then page i = 3+2i and its content
 * stream = 4+2i; extras[k] becomes object 3+2*npages+k. offsets[] (optional,
 * sized 3+2*npages+nextra) receives xref offsets indexed by object number. */
static char test_pdf_path[64];
static const char *build_pdf(const test_page *pages, size_t npages,
                             const char *const *extras, size_t nextra, size_t *offsets) {
    size_t count = 3 + 2 * npages + nextra;
    size_t *at = calloc(count, sizeof(*at));
    char *buffer = NULL;
    size_t size = 0;
    FILE *s = open_memstream(&buffer, &size);
    assert(at && s);
    fputs("%PDF-1.4\n", s);
    at[1] = (size_t)ftell(s);
    fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", s);
    at[2] = (size_t)ftell(s);
    fputs("2 0 obj\n<< /Type /Pages /MediaBox [0 0 400 300] /Kids [", s);
    for (size_t i = 0; i < npages; i++) fprintf(s, "%s%zu 0 R", i ? " " : "", 3 + 2 * i);
    fprintf(s, "] /Count %zu >>\nendobj\n", npages);
    for (size_t i = 0; i < npages; i++) {
        at[3 + 2 * i] = (size_t)ftell(s);
        fprintf(s, "%zu 0 obj\n<< /Type /Page /Parent 2 0 R", 3 + 2 * i);
        if (pages[i].resources) fprintf(s, " /Resources %s", pages[i].resources);
        if (pages[i].extra) fprintf(s, " %s", pages[i].extra);
        if (pages[i].contents) fprintf(s, " /Contents %s >>\nendobj\n", pages[i].contents);
        else fprintf(s, " /Contents %zu 0 R >>\nendobj\n", 4 + 2 * i);
        const char *content = pages[i].content ? pages[i].content : "";
        at[4 + 2 * i] = (size_t)ftell(s);
        fprintf(s, "%zu 0 obj\n<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
                4 + 2 * i, strlen(content), content);
    }
    for (size_t k = 0; k < nextra; k++) {
        size_t n = 3 + 2 * npages + k;
        at[n] = (size_t)ftell(s);
        fprintf(s, "%zu 0 obj\n%s\nendobj\n", n, extras[k]);
    }
    size_t xref = (size_t)ftell(s);
    fprintf(s, "xref\n0 %zu\n0000000000 65535 f \n", count);
    for (size_t n = 1; n < count; n++) fprintf(s, "%010zu 00000 n \n", at[n]);
    fprintf(s, "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", count, xref);
    assert(fclose(s) == 0);
    if (!test_pdf_path[0]) {
        const char *dir = getenv("TMPDIR");
        snprintf(test_pdf_path, sizeof(test_pdf_path), "%s/pdftext-font-XXXXXX",
                 dir && strlen(dir) < 40 ? dir : "/tmp");
        int fd = mkstemp(test_pdf_path);
        assert(fd >= 0);
        close(fd);
    }
    FILE *out = fopen(test_pdf_path, "wb");
    assert(out && fwrite(buffer, 1, size, out) == size && fclose(out) == 0);
    if (offsets) memcpy(offsets, at, count * sizeof(*at));
    free(buffer);
    free(at);
    return test_pdf_path;
}
static void remove_test_pdf(void) {
    if (test_pdf_path[0]) unlink(test_pdf_path);
}

#endif
