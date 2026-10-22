#include "document.h"
#include "pages.h"
#include "contents.h"
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    pdf_error e; pdf_error_init(&e);
    pdf_document doc;
    if (!pdf_document_open(&doc, argv[1], NULL, &e)) { pdf_error_print(&e, stderr); return 1; }
    pdf_pages pages = {0}; pdf_contents_result bytes = {0};
    pdf_contents_context c; pdf_contents_context_init(&c, &doc);
    int ok = pdf_pages_load(&doc, &pages, &e) && pages.len &&
             pdf_contents_read(&c, &pages.items[0], &bytes, &e);
    if (ok) fwrite(bytes.data, 1, bytes.len, stdout);
    else pdf_error_print(&e, stderr);
    pdf_contents_result_free(&bytes);
    pdf_pages_free(&pages); pdf_document_close(&doc);
    return ok ? 0 : 1;
}
