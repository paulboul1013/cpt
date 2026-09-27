#include "../src/xref.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pdf_reader fixture_reader(const char *xref_rows, const char *trailer,
                                 const char *footer) {
    const char *prefix = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
    char buffer[2048];
    size_t xref_offset = strlen(prefix);
    int length = snprintf(buffer, sizeof(buffer),
                          "%sxref\n%s\ntrailer\n%s\nstartxref\n%zu\n%s",
                          prefix, xref_rows, trailer, xref_offset, footer);
    assert(length > 0 && (size_t)length < sizeof(buffer));
    pdf_reader reader = {0};
    reader.size = (size_t)length;
    reader.data = malloc(reader.size);
    assert(reader.data != NULL);
    memcpy(reader.data, buffer, reader.size);
    pdf_limits_default(&reader.limits);
    return reader;
}

static void test_hello_pdf(void) {
    pdf_reader reader = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(reader_open(&reader, "tests/hello.pdf", &error));
    assert(reader_seek(&reader, 7));
    pdf_xref *xref = pdf_xref_parse(&reader, &error);
    assert(xref != NULL);
    assert(reader_tell(&reader) == 7);
    assert(xref->startxref == 8639 && xref->size == 19);
    assert(xref->root.object_number == 17 && xref->root.generation == 0);
    assert(pdf_xref_get(xref, 0)->present && !pdf_xref_get(xref, 0)->in_use);
    assert(pdf_xref_get(xref, 2)->in_use && pdf_xref_get(xref, 2)->offset == 19);
    assert(pdf_xref_get(xref, 11)->in_use && pdf_xref_get(xref, 11)->offset == 6768);
    assert(pdf_xref_get(xref, 19) == NULL);
    assert(error.code == PDF_ERROR_NONE);
    pdf_xref_free(xref);
    reader_close(&reader);
}

static void test_multiple_subsections_and_trailing_bytes(void) {
    pdf_reader reader = fixture_reader(
        "0 1\n0000000000 65535 f \n1 1\n0000000009 00000 n ",
        "<< /Size 2 /Root 1 0 R >>", "%%EOF\ntrailing bytes\n%%EOF");
    pdf_error error;
    pdf_error_init(&error);
    pdf_xref *xref = pdf_xref_parse(&reader, &error);
    assert(xref != NULL && xref->size == 2);
    assert(pdf_xref_get(xref, 1)->in_use);
    assert(pdf_xref_get(xref, 1)->offset == 9);
    assert(xref->trailer->type == PDF_OBJECT_DICT);
    pdf_xref_free(xref);
    reader_close(&reader);
}

static void assert_invalid(const char *rows, const char *trailer,
                           const char *footer, pdf_error_code code) {
    pdf_reader reader = fixture_reader(rows, trailer, footer);
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_xref_parse(&reader, &error) == NULL);
    assert(error.code == code);
    assert(error.module != NULL && error.message[0] != '\0');
    reader_close(&reader);
}

static void test_invalid_xref(void) {
    const char *valid_rows = "0 2\n0000000000 65535 f \n0000000009 00000 n ";
    const char *valid_trailer = "<< /Size 2 /Root 1 0 R >>";
    assert_invalid(valid_rows, valid_trailer, "missing EOF", PDF_ERROR_MALFORMED);
    assert_invalid("0 2\n0000000000 65535 f \n0000000010 00000 n ",
                   valid_trailer, "%%EOF", PDF_ERROR_MALFORMED);
    assert_invalid("0 2\n0000000000 65535 f \n0000000009 00001 n ",
                   valid_trailer, "%%EOF", PDF_ERROR_MALFORMED);
    assert_invalid("0 2\n0000000000 65535 f \n0000000009 00000 n \n1 1\n0000000009 00000 n ",
                   valid_trailer, "%%EOF", PDF_ERROR_MALFORMED);
    assert_invalid(valid_rows, "<< /Size 1 /Root 1 0 R >>", "%%EOF",
                   PDF_ERROR_MALFORMED);
    assert_invalid(valid_rows, "<< /Size 3 /Root 1 0 R >>", "%%EOF",
                   PDF_ERROR_MALFORMED);
    assert_invalid(valid_rows, "<< /Size 2 /Root 0 0 R >>", "%%EOF",
                   PDF_ERROR_MALFORMED);
    assert_invalid("0 2\n0000000000 65535 n \n0000000009 00000 n ",
                   valid_trailer, "%%EOF", PDF_ERROR_MALFORMED);
    assert_invalid("0 2\n0000000000 00000 f \n0000000009 00000 n ",
                   valid_trailer, "%%EOF", PDF_ERROR_MALFORMED);
    assert_invalid(valid_rows, "<< /Size 2 /Root 1 0 R /Prev 9 >>", "%%EOF",
                   PDF_ERROR_UNSUPPORTED);
    assert_invalid(valid_rows, "<< /Size 2 /Root 1 0 R /Encrypt 2 0 R >>", "%%EOF",
                   PDF_ERROR_UNSUPPORTED);

    pdf_reader reader = fixture_reader(valid_rows, valid_trailer, "%%EOF");
    pdf_error error;
    pdf_error_init(&error);
    reader.data[0] = 'x';
    assert(pdf_xref_parse(&reader, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    reader_close(&reader);

    reader = fixture_reader(valid_rows, valid_trailer, "%%EOF");
    pdf_error_clear(&error);
    reader.data[reader.size - 6] = 'x';
    assert(pdf_xref_parse(&reader, &error) == NULL);
    assert(error.code == PDF_ERROR_MALFORMED);
    reader_close(&reader);

    reader = fixture_reader(valid_rows, valid_trailer, "%%EOF");
    pdf_error_clear(&error);
    reader.data[45] = '1';
    assert(pdf_xref_parse(&reader, &error) == NULL);
    assert(error.code == PDF_ERROR_UNSUPPORTED);
    reader_close(&reader);

    reader = fixture_reader(valid_rows, valid_trailer, "%%EOF");
    pdf_error_clear(&error);
    reader.limits.max_xref_entries = 1;
    assert(pdf_xref_parse(&reader, &error) == NULL);
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    reader_close(&reader);

    assert_invalid("0 2\n0000000003 65535 f \n0000000009 00000 n ",
                   valid_trailer, "%%EOF", PDF_ERROR_MALFORMED);

    reader = fixture_reader(valid_rows, valid_trailer, "%%EOF");
    pdf_error_clear(&error);
    size_t type_offset = 0;
    while (type_offset + 7 <= reader.size &&
           memcmp(reader.data + type_offset, "Catalog", 7) != 0) {
        type_offset++;
    }
    assert(type_offset + 7 <= reader.size);
    memcpy(reader.data + type_offset, "ObjStm ", 7);
    assert(pdf_xref_parse(&reader, &error) == NULL);
    assert(error.code == PDF_ERROR_UNSUPPORTED);
    reader_close(&reader);
}

int main(void) {
    test_hello_pdf();
    test_multiple_subsections_and_trailing_bytes();
    test_invalid_xref();
    return 0;
}
