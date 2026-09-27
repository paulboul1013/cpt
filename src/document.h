#ifndef PDF_DOCUMENT_H
#define PDF_DOCUMENT_H

#include "parser.h"
#include "xref.h"

typedef struct pdf_cache_entry pdf_cache_entry;

typedef struct pdf_document {
    pdf_reader reader;
    pdf_xref *xref;
    pdf_cache_entry *cache;
    size_t cache_count;
    size_t resolve_depth;
} pdf_document;

/* Opens a whole PDF. On failure, the document is closed and reusable. */
int pdf_document_open(pdf_document *document, const char *filename,
                      const pdf_limits *limits, pdf_error *error);
void pdf_document_close(pdf_document *document);
pdf_reference pdf_document_root(const pdf_document *document);
/* Returns the xref byte offset for an in-use reference, or zero if unavailable. */
size_t pdf_document_reference_offset(const pdf_document *document,
                                     pdf_reference reference);

/* Returns a document-owned indirect object. The pointer is valid until close. */
const pdf_indirect_object *pdf_resolve(pdf_document *document,
                                       pdf_reference reference, pdf_error *error);

#endif
