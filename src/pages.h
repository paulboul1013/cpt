#ifndef PDF_PAGES_H
#define PDF_PAGES_H

#include "document.h"

typedef struct {
    pdf_reference reference;
    const pdf_object *resources; /* Effective dictionary, or NULL. Borrowed. */
    const pdf_object *media_box; /* Effective four-element array. Borrowed. */
    double media_box_values[4];
    int64_t rotation; /* Effective inherited /Rotate, raw multiple of 90; default 0. */
    const pdf_object *contents; /* Raw /Contents value, or NULL. Borrowed. */
} pdf_page;

typedef struct {
    pdf_page *items; /* Owned array; members are borrowed from document. */
    size_t len;
    size_t cap;
} pdf_pages;

/* The document must remain open for the lifetime of the loaded pages. */
int pdf_pages_load(pdf_document *document, pdf_pages *pages, pdf_error *error);
void pdf_pages_free(pdf_pages *pages);

#endif
