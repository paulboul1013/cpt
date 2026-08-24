#include "error.h"

#include <stdarg.h>
#include <stdio.h>

void pdf_error_init(pdf_error *error) {
    if (error == NULL) {
        return;
    }

    error->code = PDF_ERROR_NONE;
    error->offset = 0;
    error->module = NULL;
    error->message[0] = '\0';
}

void pdf_error_clear(pdf_error *error) {
    pdf_error_init(error);
}

void pdf_error_set(pdf_error *error, pdf_error_code code, size_t offset,
                   const char *module, const char *format, ...) {
    va_list arguments;

    if (error == NULL || code == PDF_ERROR_NONE || error->code != PDF_ERROR_NONE) {
        return;
    }

    error->code = code;
    error->offset = offset;
    error->module = module == NULL ? "unknown" : module;

    va_start(arguments, format);
    (void)vsnprintf(error->message, sizeof(error->message), format, arguments);
    va_end(arguments);
}

const char *pdf_error_code_name(pdf_error_code code) {
    switch (code) {
        case PDF_ERROR_NONE:
            return "none";
        case PDF_ERROR_IO:
            return "io";
        case PDF_ERROR_MALFORMED:
            return "malformed";
        case PDF_ERROR_UNSUPPORTED:
            return "unsupported";
        case PDF_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";
        case PDF_ERROR_RESOURCE_LIMIT:
            return "resource-limit";
        default:
            return "unknown";
    }
}

int pdf_error_exit_code(const pdf_error *error) {
    if (error == NULL) {
        return 0;
    }

    switch (error->code) {
        case PDF_ERROR_NONE:
            return 0;
        case PDF_ERROR_IO:
            return 2;
        case PDF_ERROR_MALFORMED:
            return 3;
        case PDF_ERROR_UNSUPPORTED:
            return 4;
        case PDF_ERROR_OUT_OF_MEMORY:
            return 5;
        case PDF_ERROR_RESOURCE_LIMIT:
            return 6;
        default:
            return 1;
    }
}

void pdf_error_print(const pdf_error *error, FILE *stream) {
    if (error == NULL || error->code == PDF_ERROR_NONE || stream == NULL) {
        return;
    }

    fprintf(stream, "%s: %s error at byte %zu: %s\n",
            error->module == NULL ? "unknown" : error->module,
            pdf_error_code_name(error->code), error->offset,
            error->message[0] == '\0' ? "unspecified error" : error->message);
}
