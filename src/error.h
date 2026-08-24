#ifndef PDF_ERROR_H
#define PDF_ERROR_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    PDF_ERROR_NONE = 0,
    PDF_ERROR_IO,
    PDF_ERROR_MALFORMED,
    PDF_ERROR_UNSUPPORTED,
    PDF_ERROR_OUT_OF_MEMORY,
    PDF_ERROR_RESOURCE_LIMIT
} pdf_error_code;

typedef struct {
    pdf_error_code code;
    size_t offset;
    const char *module;
    char message[256];
} pdf_error;

void pdf_error_init(pdf_error *error);
void pdf_error_clear(pdf_error *error);
void pdf_error_set(pdf_error *error, pdf_error_code code, size_t offset,
                   const char *module, const char *format, ...);
const char *pdf_error_code_name(pdf_error_code code);
int pdf_error_exit_code(const pdf_error *error);
void pdf_error_print(const pdf_error *error, FILE *stream);

#endif
