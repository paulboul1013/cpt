#include "../src/error.h"

#include <assert.h>
#include <string.h>

int main(void) {
    pdf_error error;

    pdf_error_init(&error);
    assert(error.code == PDF_ERROR_NONE);

    pdf_error_set(&error, PDF_ERROR_MALFORMED, 17, "lexer",
                  "unexpected byte 0x%02x", 0x40);
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(error.offset == 17);
    assert(strcmp(error.module, "lexer") == 0);
    assert(strcmp(error.message, "unexpected byte 0x40") == 0);

    pdf_error_set(&error, PDF_ERROR_IO, 3, "reader", "must not replace first error");
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(error.offset == 17);

    assert(strcmp(pdf_error_code_name(PDF_ERROR_IO), "io") == 0);
    assert(strcmp(pdf_error_code_name(PDF_ERROR_MALFORMED), "malformed") == 0);
    assert(strcmp(pdf_error_code_name(PDF_ERROR_UNSUPPORTED), "unsupported") == 0);
    assert(strcmp(pdf_error_code_name(PDF_ERROR_OUT_OF_MEMORY), "out-of-memory") == 0);
    assert(strcmp(pdf_error_code_name(PDF_ERROR_RESOURCE_LIMIT), "resource-limit") == 0);
    assert(pdf_error_exit_code(&error) == 3);

    pdf_error_clear(&error);
    assert(error.code == PDF_ERROR_NONE);
    assert(pdf_error_exit_code(&error) == 0);

    return 0;
}
