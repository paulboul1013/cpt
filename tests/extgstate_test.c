#include "pdf_builder.h"
#include "../src/extgstate.h"
#include "../src/text_state.h"
#include "../src/text_items.h"

#include <math.h>

/* extras[0] is Resources, followed by any referenced values. Keep the document
 * open for the whole call; check both direct and indirect Resources ownership. */
static pdf_error check(const char *const *extras, size_t n, const unsigned char *name,
                       size_t name_len, const pdf_limits *limits, int indirect, size_t *offsets) {
    test_page page = {"<< >>", "", NULL, NULL};
    pdf_document document = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(pdf_document_open(&document, build_pdf(&page, 1, extras, n, offsets), NULL, &e));
    pdf_object *ref = pdf_object_new_ref(5, 0);
    assert(ref);
    const pdf_object *resources = ref;
    if (!indirect) {
        const pdf_indirect_object *resolved = pdf_resolve(&document, ref->value.reference, &e);
        assert(resolved && !resolved->is_stream);
        resources = resolved->body;
    }
    int ok = pdf_extgstate_check(&document, resources, name, name_len, limits, 123, &e);
    assert(ok == (e.code == PDF_ERROR_NONE));
    pdf_object_free(ref);
    pdf_document_close(&document);
    return e;
}

static void fields(void) {
    struct { const char *body; pdf_error_code code; } cases[] = {
        {"<< >>", PDF_ERROR_NONE},
        {"<< /ca 1 /CA 1.0 /AIS false /SA true /SMask /None /SM .02 /Type /ExtGState >>", PDF_ERROR_NONE},
        {"<< /SM 0 /SA false >>", PDF_ERROR_NONE},
        {"<< /SM 1 >>", PDF_ERROR_NONE},
        {"<< /Type /Other >>", PDF_ERROR_MALFORMED},
        {"<< /Type null >>", PDF_ERROR_MALFORMED},
        {"<< /SM -0.01 >>", PDF_ERROR_MALFORMED},
        {"<< /SM 1.01 >>", PDF_ERROR_MALFORMED},
        {"<< /SM true >>", PDF_ERROR_MALFORMED},
        {"<< /SA 1 >>", PDF_ERROR_MALFORMED},
        {"<< /AIS /false >>", PDF_ERROR_MALFORMED},
        {"<< /AIS true >>", PDF_ERROR_UNSUPPORTED},
        {"<< /SMask /Other >>", PDF_ERROR_MALFORMED},
        {"<< /SMask false >>", PDF_ERROR_MALFORMED},
        {"<< /SMask << >> >>", PDF_ERROR_UNSUPPORTED},
        {"<< /CA .5 >>", PDF_ERROR_UNSUPPORTED},
        {"<< /ca 0 >>", PDF_ERROR_UNSUPPORTED},
        {"<< /ca 2 >>", PDF_ERROR_MALFORMED},
        {"<< /CA -1 >>", PDF_ERROR_MALFORMED},
        {"<< /ca /One >>", PDF_ERROR_MALFORMED},
        {"<< /Font [88 0 R 12] >>", PDF_ERROR_UNSUPPORTED},
        {"<< /Font [88 0 R -12.5] >>", PDF_ERROR_UNSUPPORTED},
        {"<< /Font /F1 >>", PDF_ERROR_MALFORMED},
        {"<< /Font [] >>", PDF_ERROR_MALFORMED},
        {"<< /Font [88 0 R] >>", PDF_ERROR_MALFORMED},
        {"<< /Font [88 0 R 12 13] >>", PDF_ERROR_MALFORMED},
        {"<< /Font [/F1 12] >>", PDF_ERROR_MALFORMED},
        {"<< /Font [<< >> 12] >>", PDF_ERROR_MALFORMED},
        {"<< /Font [88 0 R /Size] >>", PDF_ERROR_MALFORMED},
        {"<< /ca 1 /Unknown 1 >>", PDF_ERROR_UNSUPPORTED},
        {"<< /Unknown 1 /ca 1 >>", PDF_ERROR_UNSUPPORTED},
        {"<< /Type#00 /ExtGState >>", PDF_ERROR_UNSUPPORTED},
        {"<< /BM /Normal >>", PDF_ERROR_UNSUPPORTED},
        {"[]", PDF_ERROR_MALFORMED},
        {"null", PDF_ERROR_MALFORMED}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        char resources[512];
        snprintf(resources, sizeof(resources), "<< /ExtGState << /G %s >> >>", cases[i].body);
        const char *extras[] = {resources};
        for (int indirect = 0; indirect < 2; indirect++) {
            size_t at[6];
            pdf_error e = check(extras, 1, (const unsigned char *)"G", 1, NULL, indirect, at);
            assert(e.code == cases[i].code);
            if (e.code) assert(e.offset == (indirect ? at[5] : 123));
        }
    }
}

static void references_and_limits(void) {
    const unsigned char *g = (const unsigned char *)"G";
    size_t at[32];
    pdf_error e;
    /* Every known field, entry, dictionary and Resources can be indirect. */
    const char *all[] = {"6 0 R", "<< /ExtGState 7 0 R >>", "<< /G 8 0 R /Alias 8 0 R >>",
        "9 0 R", "<< /Type 10 0 R /SM 11 0 R /SA 12 0 R /SMask 13 0 R /AIS 14 0 R /CA 15 0 R /ca 15 0 R >>",
        "/ExtGState", ".02", "true", "/None", "false", "1"};
    e = check(all, 11, g, 1, NULL, 1, at); assert(!e.code);
    e = check(all, 11, (const unsigned char *)"Alias", 5, NULL, 1, NULL); assert(!e.code);
    const char *bad[] = {"<< /ExtGState << /G 6 0 R >> >>", "<< /CA 7 0 R >>", "2"};
    e = check(bad, 3, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[7]);
    const char *sibling[] = {"<< /ExtGState << /G 6 0 R >> >>", "<< /ca 7 0 R /SA 1 >>", "1"};
    e = check(sibling, 3, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[6]);
    const char *cycle[] = {"<< /ExtGState << /G 6 0 R >> >>", "7 0 R", "6 0 R"};
    e = check(cycle, 3, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && strstr(e.message, "cycle"));
    const char *self[] = {"<< /ExtGState << /G 6 0 R >> >>", "6 0 R"};
    e = check(self, 2, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[6]);
    const char *chain[] = {"<< /ExtGState << /G 6 0 R >> >>", "7 0 R", "<< >>"};
    pdf_limits limits;
    pdf_limits_default(&limits);
    limits.max_nesting_depth = 2;
    e = check(chain, 3, g, 1, &limits, 1, NULL); assert(!e.code);
    limits.max_nesting_depth = 1;
    e = check(chain, 3, g, 1, &limits, 1, at);
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && e.offset == at[6]);
    const char *missing[] = {"<< /ExtGState << /G 99 0 R >> >>"};
    e = check(missing, 1, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[5]);
    const char *stream[] = {"<< /ExtGState << /G 6 0 R >> >>", "<< /Length 0 >>\nstream\n\nendstream"};
    e = check(stream, 2, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[6]);
    const char *stream_field[] = {"<< /ExtGState << /G << /SMask 6 0 R >> >> >>", stream[1]};
    e = check(stream_field, 2, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[6]);
    const char *missing_dicts[] = {"<< >>", "<< /ExtGState [] >>", "<< /ExtGState << >> >>", "[]"};
    for (size_t i = 0; i < sizeof(missing_dicts)/sizeof(*missing_dicts); i++) {
        e = check(&missing_dicts[i], 1, g, 1, NULL, 1, at);
        assert(e.code == PDF_ERROR_MALFORMED && e.offset == at[5]);
    }
    const char *nul[] = {"<< /ExtGState << /G << /ca .5 >> /G#00 << >> >> >>"};
    e = check(nul, 1, (const unsigned char *)"G\0", 2, NULL, 1, NULL); assert(!e.code);
    e = check(nul, 1, g, 1, NULL, 1, NULL); assert(e.code == PDF_ERROR_UNSUPPORTED);
    pdf_limits_default(&limits);
    limits.max_token_size = 1;
    e = check(nul, 1, (const unsigned char *)"G\0", 2, &limits, 1, NULL);
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    const char *count[] = {"<< /ExtGState << /G << /ca 1 /CA 1 >> >> >>"};
    pdf_limits_default(&limits);
    limits.max_container_entries = 1;
    e = check(count, 1, g, 1, &limits, 1, NULL); assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    const char *array[] = {"<< /ExtGState << /G << /Font [88 0 R 12] >> >> >>"};
    e = check(array, 1, g, 1, &limits, 1, NULL); assert(e.code == PDF_ERROR_RESOURCE_LIMIT);
    const char *font_size[] = {"<< /ExtGState << /G << /Font 6 0 R >> >> >>", "[88 0 R 7 0 R]", "12"};
    e = check(font_size, 3, g, 1, NULL, 1, at);
    assert(e.code == PDF_ERROR_UNSUPPORTED && e.offset == at[7]);
}

static void nonfinite_and_bare_m8(void) {
    pdf_document document = {0};
    pdf_object *resources = pdf_object_new_dict(), *states = pdf_object_new_dict();
    pdf_object *state = pdf_object_new_dict();
    assert(resources && states && state);
    assert(pdf_dict_push(resources, "ExtGState", states));
    assert(pdf_dict_push(states, "G", state));
    const double values[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < 3; i++) {
        pdf_error e;
        pdf_error_init(&e);
        assert(pdf_dict_push(state, "SM", pdf_object_new_real(values[i])));
        assert(!pdf_extgstate_check(&document, resources, (const unsigned char *)"G", 1, NULL, 123, &e));
        assert(e.code == PDF_ERROR_MALFORMED && e.offset == 123);
    }
    pdf_object_free(resources);
    pdf_error e;
    pdf_error_init(&e);
    pdf_page page = {0};
    assert(!pdf_text_page_interpret(&page, (const unsigned char *)"/G gs", 5, NULL,
                                   123, NULL, NULL, NULL, NULL, &e));
    assert(e.code == PDF_ERROR_UNSUPPORTED && e.offset == 123);
    assert(strstr(e.message, "decoded byte 3") && strstr(e.message, "resource validation"));
}

static void page_scope_and_cleanup(void) {
    test_page pages[] = {
        {"<< /Font << /F1 7 0 R >> /ExtGState << /G << /SM .02 >> >> >>",
         "/G gs BT /F1 12 Tf (A) Tj ET", NULL, NULL},
        {"<< /Font << /F1 7 0 R >> /ExtGState << /G << /SA true >> >> >>",
         "BT /F1 12 Tf /G gs (A) Tj ET", NULL, NULL}
    };
    const char *extras[] = {"<< /Type /Font /Subtype /Type1 /BaseFont /Test /FirstChar 65 /LastChar 65 /Widths [600] >>"};
    for (int variant = 0; variant < 4; variant++) {
        if (variant == 1) pages[1].resources = "<< /Font << /F1 7 0 R >> /ExtGState << /G << /CA .5 >> >> >>";
        if (variant == 2) pages[1].resources = "<< /Font << /F1 7 0 R >> /ExtGState << /G << /Font [7 0 R 12] >> >> >>";
        if (variant == 3) pages[1].resources = "<< /Font << /F1 7 0 R >> /ExtGState << /G << /SA 1 >> >> >>";
        pdf_document document = {0};
        pdf_text_items items = {0};
        pdf_error e;
        pdf_error_init(&e);
        assert(pdf_document_open(&document, build_pdf(pages, 2, extras, 1, NULL), NULL, &e));
        int ok = pdf_text_items_extract(&document, NULL, &items, &e);
        if (variant == 0) {
            assert(ok && items.len == 2 && items.items[0].page == 1 && items.items[1].page == 2);
            assert(items.items[0].width == items.items[1].width && fabs(items.items[0].width - 7.2) < 1e-9);
            assert(items.items[0].text_len == 1 && items.items[0].text[0] == 'A' && items.items[1].text[0] == 'A');
        } else {
            assert(!ok && items.len == 0 && items.items == NULL && items.bytes == NULL);
            assert(e.code == (variant == 3 ? PDF_ERROR_MALFORMED : PDF_ERROR_UNSUPPORTED));
            assert(strstr(e.message, "ExtGState byte") && strstr(e.message, "decoded byte 16"));
        }
        pdf_text_items_free(&items);
        pdf_document_close(&document);
    }
    /* Resolver resource-limit errors retain their cause and module. */
    test_page page = {"<< >>", "", NULL, NULL};
    const char *resource[] = {"<< /ExtGState << /G << >> >> >>"};
    pdf_document document = {0};
    pdf_error e;
    pdf_error_init(&e);
    assert(pdf_document_open(&document, build_pdf(&page, 1, resource, 1, NULL), NULL, &e));
    document.reader.limits.max_object_cache = 0;
    pdf_object *ref = pdf_object_new_ref(5, 0);
    assert(ref && !pdf_extgstate_check(&document, ref, (const unsigned char *)"G", 1, NULL, 123, &e));
    assert(e.code == PDF_ERROR_RESOURCE_LIMIT && strcmp(e.module, "document") == 0);
    pdf_object_free(ref);
    pdf_document_close(&document);
}

int main(void) {
    page_scope_and_cleanup();
    fields();
    references_and_limits();
    nonfinite_and_bare_m8();
    remove_test_pdf();
    puts("ExtGState tests passed");
    return 0;
}
