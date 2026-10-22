/* Diagnostic scanner. Reuse M7's private operand parser and dispatch policy so
 * strings, arrays, dictionaries, and repo support classifications stay exact.
 * Compile without a second copy of content_interpreter.c; see the Python CLI.
 * This tool does not change extraction policy or execute unsupported operators. */
#include "../src/content_interpreter.c"
#include "../src/contents.h"
#include "../src/extgstate.h"

static void hex_bytes(const unsigned char *data, size_t len) {
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
}

static const pdf_object *resolve_leaf(pdf_document *doc, const pdf_object *o,
                                      const pdf_indirect_object **indirect, pdf_error *e) {
    *indirect = NULL;
    for (size_t i = 0; o && o->type == PDF_OBJECT_REF; i++) {
        if (i >= doc->reader.limits.max_nesting_depth) {
            pdf_error_set(e, PDF_ERROR_RESOURCE_LIMIT, 0, "inventory", "resource reference depth exceeded");
            return NULL;
        }
        *indirect = pdf_resolve(doc, o->value.reference, e);
        if (!*indirect) return NULL;
        o = (*indirect)->body;
    }
    return o;
}

static void resource_usage(pdf_document *doc, const pdf_page *page, size_t number,
                           size_t offset, const char *op, const pdf_object *args) {
    if (strcmp(op, "BDC") == 0 && args->value.array.len == 2 &&
        args->value.array.items[1]->type == PDF_OBJECT_DICT) {
        const pdf_object *properties = args->value.array.items[1];
        printf("B\t%zu\t%zu\t", number, offset);
        for (size_t i = 0; i < properties->value.dict.len; i++) {
            if (i) putchar(',');
            const pdf_bytes *key = &properties->value.dict.entries[i].key;
            hex_bytes(key->data, key->len);
        }
        putchar('\n');
    }
    if (args->value.array.len != 1 || args->value.array.items[0]->type != PDF_OBJECT_NAME) return;
    const pdf_bytes *name = &args->value.array.items[0]->value.name;
    pdf_error e;
    pdf_error_init(&e);
    if (strcmp(op, "gs") == 0) {
        pdf_extgstate_check(doc, page->resources, name->data, name->len,
                           &doc->reader.limits, pdf_document_reference_offset(doc, page->reference), &e);
        printf("G\t%zu\t%zu\t", number, offset);
        hex_bytes(name->data, name->len);
        printf("\t%d\t%zu\t", e.code, e.offset);
        hex_bytes((const unsigned char *)e.message, strlen(e.message));
        putchar('\n');
    } else if (strcmp(op, "Do") == 0) {
        const pdf_indirect_object *indirect;
        const pdf_object *objects = resolve_leaf(doc, pdf_dict_get(page->resources, "XObject"), &indirect, &e);
        const pdf_object *target = resolve_leaf(doc, pdf_dict_get_bytes(objects, name->data, name->len), &indirect, &e);
        const pdf_indirect_object *target_indirect = indirect;
        const pdf_object *subtype = resolve_leaf(doc, pdf_dict_get(target, "Subtype"), &indirect, &e);
        printf("X\t%zu\t%zu\t", number, offset);
        hex_bytes(name->data, name->len);
        putchar('\t');
        if (subtype && subtype->type == PDF_OBJECT_NAME)
            hex_bytes(subtype->value.name.data, subtype->value.name.len);
        printf("\t%d\t%lld\n", e.code,
               target_indirect ? (long long)target_indirect->object_number : 0LL);
    }
}

static int scan(pdf_document *doc, const pdf_page *page, size_t number,
                const pdf_contents_result *bytes, pdf_error *e) {
    content_parser p = {0};
    p.limits = &doc->reader.limits; p.error = e;
    pdf_token_init(&p.token);
    pdf_content_lexer_init(&p.lexer, bytes->data, bytes->len, p.limits, e);
    pdf_object *args = pdf_object_new_array();
    if (!args) {
        pdf_error_set(e, PDF_ERROR_OUT_OF_MEMORY, 0, "inventory", "cannot allocate operand array");
        return 0;
    }
    content_scope scope = {0};
    advance(&p);
    while (!e->code && p.token.type != PDF_TOKEN_EOF) {
        if (p.token.type != PDF_TOKEN_KEYWORD) {
            if (args->value.array.len >= p.limits->max_container_entries) {
                fail(&p, PDF_ERROR_RESOURCE_LIMIT, "inventory operand limit"); break;
            }
            pdf_object *o = parse_operand(&p, 0);
            if (!o) break;
            if (!pdf_array_push(args, o)) {
                pdf_object_free(o); fail(&p, PDF_ERROR_OUT_OF_MEMORY, "inventory operand allocation"); break;
            }
            continue;
        }
        pdf_error local;
        pdf_error_init(&local);
        content_parser probe = p;
        probe.error = &local;
        pdf_content_result result = {0};
        dispatch(&probe, args, &scope, NULL, NULL, NULL, &result);
        char status = local.code == PDF_ERROR_UNSUPPORTED ? 'U' :
                      local.code == PDF_ERROR_MALFORMED ? 'M' :
                      local.code != PDF_ERROR_NONE ? 'E' : result.operations ? 'S' : 'I';
        printf("O\t%zu\t%zu\t", number, p.token.offset);
        hex_bytes((const unsigned char *)p.token.text, strlen(p.token.text));
        printf("\t%c\t%zu\n", status, args->value.array.len);
        resource_usage(doc, page, number, p.token.offset, p.token.text, args);
        /* Inline image data requires an image-aware scanner. Never tokenize
         * arbitrary image bytes and silently claim the page was fully scanned. */
        if (strcmp(p.token.text, "BI") == 0) {
            fail(&p, PDF_ERROR_UNSUPPORTED, "inline image encountered; inventory is incomplete"); break;
        }
        for (size_t i = 0; i < args->value.array.len; i++) pdf_object_free(args->value.array.items[i]);
        args->value.array.len = 0;
        advance(&p);
    }
    if (!e->code && (scope.in_text || scope.saved || scope.marked_depth || args->value.array.len))
        fail(&p, PDF_ERROR_MALFORMED, "unfinished text object, q stack, or operands");
    pdf_object_free(args);
    pdf_token_destroy(&p.token);
    return e->code == PDF_ERROR_NONE;
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    pdf_document doc = {0}; pdf_pages pages = {0};
    pdf_error e; pdf_error_init(&e);
    int ok = pdf_document_open(&doc, argv[1], NULL, &e) && pdf_pages_load(&doc, &pages, &e);
    pdf_contents_context c; pdf_contents_context_init(&c, &doc);
    printf("D\t%zu\n", pages.len);
    for (size_t i = 0; ok && i < pages.len; i++) {
        pdf_contents_result bytes = {0};
        ok = pdf_contents_read(&c, &pages.items[i], &bytes, &e);
        if (ok) {
            printf("P\t%zu\t%zu\t%zu\n", i + 1,
                   pdf_document_reference_offset(&doc, pages.items[i].reference), bytes.len);
            ok = scan(&doc, &pages.items[i], i + 1, &bytes, &e);
        }
        pdf_contents_result_free(&bytes);
    }
    pdf_pages_free(&pages); pdf_document_close(&doc);
    if (!ok) pdf_error_print(&e, stderr);
    return ok ? 0 : 1;
}
