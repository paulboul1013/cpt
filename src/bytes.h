#ifndef PDF_BYTES_H
#define PDF_BYTES_H

#include <stddef.h>

typedef struct {
    unsigned char *data;
    size_t len;
} pdf_bytes;

#endif
