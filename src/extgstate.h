#ifndef PDF_EXTGSTATE_H
#define PDF_EXTGSTATE_H

#include "document.h"

/* Validate the complete extraction-safe ExtGState subset. All inputs are
 * borrowed for this call; resolved objects remain document-owned. No state
 * mutation, stream decoding, or retained allocation. NULL limits = defaults.
 * Errors use the nearest indirect object offset, or page_offset as fallback.
 * Names are length-aware and can contain NUL. */
int pdf_extgstate_check(pdf_document *document, const pdf_object *resources,
                       const unsigned char *name, size_t name_len,
                       const pdf_limits *limits, size_t page_offset,
                       pdf_error *error);

#endif
