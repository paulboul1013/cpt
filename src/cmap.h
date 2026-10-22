#ifndef PDF_CMAP_H
#define PDF_CMAP_H

#include "bytes.h"
#include "limits.h"
#include "error.h"

/* Owned, immutable ToUnicode map. Fixed one- or two-byte source codes;
 * UTF-16BE destinations may contain multiple scalars. No PostScript execution
 * or UseCMap. Parse borrows input only for the call. Limits bound expansion,
 * token sizes, decoded input and retained UTF-8. Failure returns NULL. */
typedef struct pdf_cmap pdf_cmap;
pdf_cmap *pdf_cmap_parse(const unsigned char *, size_t, unsigned code_bytes,
                         const pdf_limits *, size_t file_offset, pdf_error *);
void pdf_cmap_free(pdf_cmap *);
/* Borrowed UTF-8, or NULL for an unmapped code. Valid until free. */
const pdf_bytes *pdf_cmap_lookup(const pdf_cmap *, unsigned code);
/* Retained heap bytes, including the map, entry capacity and UTF-8 storage. */
size_t pdf_cmap_storage_size(const pdf_cmap *);

#endif
