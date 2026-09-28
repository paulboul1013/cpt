#include "../src/pages.h"

#include <assert.h>
#include <string.h>

static void test_source_order_and_inheritance(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/pages-multi.pdf", NULL,
                             &error));
    assert(pdf_pages_load(&document, &pages, &error));
    assert(error.code == PDF_ERROR_NONE && pages.len == 3);
    assert(pages.items[0].rotation == 0);
    assert(pages.items[0].reference.object_number == 5);
    assert(pages.items[1].reference.object_number == 6);
    assert(pages.items[2].reference.object_number == 4);
    assert(pages.items[0].media_box_values[2] == 300);
    assert(pages.items[1].media_box_values[3] == 400);
    assert(pages.items[2].media_box_values[2] == 100);
    assert(pages.items[0].resources == pages.items[2].resources);
    assert(pages.items[1].resources != pages.items[0].resources);
    assert(pages.items[0].contents != NULL);
    assert(pages.items[1].contents == NULL);
    assert(pages.items[2].contents == NULL);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void test_indirect_inherited_properties(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document,
                             "tests/fixtures/pages-indirect-properties.pdf",
                             NULL, &error));
    assert(pdf_pages_load(&document, &pages, &error));
    assert(pages.len == 3);
    assert(pages.items[2].media_box_values[2] == 100);
    assert(pages.items[0].resources == pages.items[2].resources);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void expect_failure(const char *path, pdf_error_code code) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, path, NULL, &error));
    assert(!pdf_pages_load(&document, &pages, &error));
    assert(error.code == code);
    assert(error.module != NULL && error.message[0] != '\0');
    assert(error.offset > 0 && error.offset < document.reader.size);
    assert(pages.items == NULL && pages.len == 0);
    pdf_pages_free(&pages);
    pdf_document_close(&document);
}

static void test_malformed_trees(void) {
    expect_failure("tests/fixtures/pages-count-mismatch.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-cycle.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-parent-mismatch.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-missing-media-box.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-bad-resources.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-bad-catalog.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-missing-kids.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-bad-page-type.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-bad-media-box.pdf", PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/pages-duplicate.pdf", PDF_ERROR_MALFORMED);
}

static void test_page_limit(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_page_count = 2;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/pages-multi.pdf",
                             &limits, &error));
    assert(!pdf_pages_load(&document, &pages, &error));
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    assert(pages.items == NULL && pages.len == 0);
    pdf_document_close(&document);
}

static void test_depth_limit(void) {
    pdf_document document = {0};
    pdf_pages pages = {0};
    pdf_error error;
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_nesting_depth = 1;
    pdf_error_init(&error);
    assert(pdf_document_open(&document, "tests/fixtures/pages-multi.pdf",
                             &limits, &error));
    assert(!pdf_pages_load(&document, &pages, &error));
    assert(error.code == PDF_ERROR_RESOURCE_LIMIT);
    pdf_document_close(&document);
}

static void test_rotation(void) {
    pdf_document doc={0}; pdf_pages pages={0}; pdf_error e; pdf_error_init(&e);
    assert(pdf_document_open(&doc,"tests/fixtures/rotate-inherit.pdf",NULL,&e));
    assert(pdf_pages_load(&doc,&pages,&e));
    assert(pages.len==3 && pages.items[0].rotation==90);
    assert(pages.items[1].rotation==0 && pages.items[2].rotation==-180);
    pdf_pages_free(&pages); pdf_document_close(&doc);
    const char *paths[]={"tests/fixtures/rotate-negative.pdf","tests/fixtures/rotate-360.pdf"};
    for(size_t i=0;i<2;i++) {
        assert(pdf_document_open(&doc,paths[i],NULL,&e));
        assert(pdf_pages_load(&doc,&pages,&e));
        assert(pages.items[0].rotation==(i?360:-90));
        pdf_pages_free(&pages); pdf_document_close(&doc);
    }
    expect_failure("tests/fixtures/rotate-bad-type.pdf",PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/rotate-bad-angle.pdf",PDF_ERROR_MALFORMED);
    expect_failure("tests/fixtures/rotate-min.pdf",PDF_ERROR_MALFORMED);
}
int main(void) {
    test_rotation();
    test_source_order_and_inheritance();
    test_indirect_inherited_properties();
    test_malformed_trees();
    test_page_limit();
    test_depth_limit();
    return 0;
}
