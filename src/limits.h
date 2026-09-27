#ifndef PDF_LIMITS_H
#define PDF_LIMITS_H

#include <stddef.h>

typedef struct {
    size_t max_input_size;
    size_t max_token_size;
    size_t max_nesting_depth;
    size_t max_container_entries;
    size_t max_object_cache;
    size_t max_xref_entries;
    size_t max_stream_size;
    size_t max_decoded_stream_size;
    size_t max_total_decoded_size;
    size_t max_page_count;
} pdf_limits;

void pdf_limits_default(pdf_limits *limits);

#endif
