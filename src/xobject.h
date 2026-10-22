#ifndef PDF_XOBJECT_H
#define PDF_XOBJECT_H

#include "document.h"

/* Validate a named Image XObject for source-text extraction. Borrows all inputs;
 * resolved objects remain document-owned. Checks resource dictionaries, stream,
 * Type/Subtype and positive integer dimensions, without decoding image pixels,
 * filters or masks. Form/PS/unknown subtypes are unsupported, never discarded.
 * Each reference chain is cycle-checked and bounded by max_nesting_depth.
 * NULL limits uses document limits; errors use the nearest object file offset. */
int pdf_xobject_check_image(pdf_document *, const pdf_object *resources,
                            const unsigned char *name, size_t name_len,
                            const pdf_limits *, size_t page_offset, pdf_error *);
#endif
