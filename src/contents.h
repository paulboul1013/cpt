#ifndef PDF_CONTENTS_H
#define PDF_CONTENTS_H

#include "pages.h"

typedef struct {
    pdf_document *document; /* Borrowed; must remain open. */
    size_t total_decoded;   /* Bytes returned for earlier pages, including separators. */
} pdf_contents_context;

typedef struct {
    unsigned char *data; /* Caller-owned; NULL when len is zero. */
    size_t len;
} pdf_contents_result;

void pdf_contents_context_init(pdf_contents_context *context, pdf_document *document);

/* Read one page. Result must start empty or have been freed. On failure it is
 * empty and the cross-page budget is unchanged. The page and document are borrowed. */
int pdf_contents_read(pdf_contents_context *context, const pdf_page *page,
                      pdf_contents_result *result, pdf_error *error);
void pdf_contents_result_free(pdf_contents_result *result);

#endif
