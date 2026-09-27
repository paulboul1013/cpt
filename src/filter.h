#ifndef PDF_FILTER_H
#define PDF_FILTER_H

#include "parser.h"

/* Decode one document-owned stream into caller-owned bytes. Output must start
 * empty or have been freed; it remains empty on failure. max_output is the
 * smaller of stream and remaining page budget. */
int pdf_filter_decode(const pdf_indirect_object *stream, size_t max_output,
                      size_t offset, pdf_bytes *output, pdf_error *error);

#endif
