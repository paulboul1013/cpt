#include "limits.h"

void pdf_limits_default(pdf_limits *limits) {
    if (limits == NULL) {
        return;
    }

    limits->max_input_size = (size_t)256 * 1024 * 1024;
    limits->max_token_size = (size_t)16 * 1024 * 1024;
    limits->max_nesting_depth = 256;
    limits->max_container_entries = 1000000;
    limits->max_object_cache = 1000000;
    limits->max_xref_entries = 1000000;
    limits->max_stream_size = (size_t)256 * 1024 * 1024;
    limits->max_decoded_stream_size = (size_t)256 * 1024 * 1024;
    limits->max_total_decoded_size = (size_t)256 * 1024 * 1024;
    limits->max_page_count = 100000;
}
