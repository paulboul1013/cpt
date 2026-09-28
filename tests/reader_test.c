#include "../src/reader.h"

#include <assert.h>
#include <string.h>

static void assert_invalid_header(unsigned char *data, size_t size,
                                  size_t expected_offset) {
    pdf_reader reader = {0};
    pdf_error error;

    reader.data = data;
    reader.size = size;
    pdf_error_init(&error);

    assert(!reader_validate_pdf_header(&reader, &error));
    assert(error.code == PDF_ERROR_MALFORMED);
    assert(error.offset == expected_offset);
    assert(strcmp(error.module, "reader") == 0);
    assert(reader_tell(&reader) == 0);
}

int main(void) {
    pdf_reader reader = {0};
    pdf_error error;
    unsigned char short_header[] = "%PD";
    unsigned char wrong_header[] = "x%PDF-1.7\n";

    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/hello.pdf", &error));
    assert(reader_seek(&reader, 2));
    assert(reader_validate_pdf_header(&reader, &error));
    assert(error.code == PDF_ERROR_NONE);
    assert(reader_tell(&reader) == 2);
    reader_close(&reader);

    assert_invalid_header(NULL, 0, 0);
    assert_invalid_header(short_header, sizeof(short_header) - 1, 3);
    assert_invalid_header(wrong_header, sizeof(wrong_header) - 1, 0);

    return 0;
}
