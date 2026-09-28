#define _POSIX_C_SOURCE 200809L
#include "reader.h"
#include "lexer.h"
#include "parser.h"
#include "object.h"
#include "error.h"
#include "xref.h"
#include "document.h"
#include "pages.h"
#include "contents.h"
#include "content_interpreter.h"
#include "text_items.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void content_warning(void *context, const char *name, size_t offset) {
    const size_t *file_offset = context;
    (void)name; /* Never echo untrusted operator bytes to the terminal. */
    fprintf(stderr, "content: warning at byte %zu: decoded byte %zu: unknown operator ignored\n",
            *file_offset, offset);
}

static int dump_content_pages(pdf_document *document, const pdf_pages *pages,
                               pdf_error *error) {
    pdf_content_result *summaries = NULL;
    if (pages->len > SIZE_MAX / sizeof(*summaries)) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, 0, "content",
                      "page count exceeds summary capacity");
        return 0;
    }
    if (pages->len != 0 && (summaries = calloc(pages->len, sizeof(*summaries))) == NULL) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, 0, "content",
                      "could not allocate page summaries");
        return 0;
    }
    pdf_contents_context context;
    pdf_contents_context_init(&context, document);
    int success = 1;
    for (size_t i = 0; i < pages->len; i++) {
        pdf_contents_result bytes = {0};
        size_t file_offset = pdf_document_reference_offset(document, pages->items[i].reference);
        success = pdf_contents_read(&context, &pages->items[i], &bytes, error) &&
            pdf_content_interpret(bytes.data, bytes.len, &document->reader.limits,
                file_offset, NULL, content_warning, &file_offset, &summaries[i], error);
        pdf_contents_result_free(&bytes);
        if (!success) break;
    }
    if (success) {
        printf("PAGES %zu\n", pages->len);
        for (size_t i = 0; i < pages->len; i++) {
            printf("PAGE %zu OPS %zu TEXT_SHOWS %zu\n", i + 1,
                   summaries[i].operations, summaries[i].text_shows);
        }
    }
    free(summaries);
    return success;
}

/* Debug mode: extract and format everything in memory, then write stdout
 * once, so no partial JSON is emitted on extraction or formatting failure. */
static int dump_text_items(pdf_document *document, pdf_error *error) {
    pdf_text_items items = {0};
    if (!pdf_text_items_extract(document, NULL, &items, error)) return 0;
    char *buffer = NULL;
    size_t size = 0;
    FILE *stage = open_memstream(&buffer, &size);
    int ok = stage != NULL;
    if (!ok) pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, 0, "text-items", "cannot stage text items");
    for (size_t i = 0; ok && i < items.len; i++)
        ok = pdf_text_item_dump(stage, &items.items[i], error);
    if (stage && fclose(stage) != 0 && ok) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, 0, "text-items", "cannot stage text items");
        ok = 0;
    }
    pdf_text_items_free(&items);
    if (ok && (fwrite(buffer, 1, size, stdout) != size || fflush(stdout) != 0)) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "text-items", "cannot write text items");
        ok = 0;
    }
    free(buffer);
    return ok;
}

int main(int argc,char *argv[]) {
    
    int standalone_object = 0;
    int indirect_object = 0;
    int dump_xref = 0;
    int dump_pages = 0;
    int dump_contents = 0;
    int dump_content = 0;
    int dump_items = 0;
    const char *filename = NULL;

    if (argc == 2) {
        filename = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--object") == 0) {
        standalone_object = 1;
        filename = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--indirect") == 0) {
        indirect_object = 1;
        filename = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--dump-xref") == 0) {
        dump_xref = 1;
        filename = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--dump-pages") == 0) {
        dump_pages = 1;
        filename = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--dump-content") == 0) {
        dump_content = 1;
        filename = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--dump-text-items") == 0) {
        dump_items = 1;
        filename = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--dump-contents") == 0) {
        dump_contents = 1;
        filename = argv[2];
    } else {
        fprintf(stderr,"usage: %s [--object|--indirect|--dump-xref|--dump-pages|--dump-contents|--dump-content|--dump-text-items] input-file\n",argv[0]);
        return 1;
    }

    if (dump_pages || dump_contents || dump_content || dump_items) {
        pdf_document document;
        pdf_pages pages = {0};
        pdf_error error;
        pdf_error_init(&error);
        if (!pdf_document_open(&document, filename, NULL, &error)) {
            pdf_error_print(&error, stderr);
            return pdf_error_exit_code(&error);
        }
        int loaded = dump_items ? dump_text_items(&document, &error)
                                : pdf_pages_load(&document, &pages, &error);
        if (dump_items) {
            /* Output already written by dump_text_items after full success. */
        } else if (loaded && dump_content) {
            loaded = dump_content_pages(&document, &pages, &error);
        } else if (loaded && dump_contents) {
            size_t *lengths = NULL;
            if (pages.len > SIZE_MAX / sizeof(*lengths)) {
                pdf_error_set(&error, PDF_ERROR_RESOURCE_LIMIT, 0, "contents",
                              "page count exceeds summary capacity");
                loaded = 0;
            } else if (pages.len != 0 &&
                       (lengths = calloc(pages.len, sizeof(*lengths))) == NULL) {
                pdf_error_set(&error, PDF_ERROR_OUT_OF_MEMORY, 0, "contents",
                              "could not allocate page lengths");
                loaded = 0;
            }
            if (loaded) {
                pdf_contents_context context;
                pdf_contents_context_init(&context, &document);
                for (size_t i = 0; i < pages.len; i++) {
                    pdf_contents_result result = {0};
                    if (!pdf_contents_read(&context, &pages.items[i], &result, &error)) {
                        pdf_contents_result_free(&result);
                        loaded = 0;
                        break;
                    }
                    lengths[i] = result.len;
                    pdf_contents_result_free(&result);
                }
            }
            if (loaded) {
                printf("PAGES %zu\n", pages.len);
                for (size_t i = 0; i < pages.len; i++) {
                    printf("PAGE %zu BYTES %zu\n", i + 1, lengths[i]);
                }
            }
            free(lengths);
        } else if (loaded) {
            printf("PAGES %zu\n", pages.len);
            for (size_t i = 0; i < pages.len; i++) {
                const pdf_page *page = &pages.items[i];
                const char *contents = "none";
                if (page->contents != NULL) {
                    if (page->contents->type == PDF_OBJECT_REF) contents = "reference";
                    else if (page->contents->type == PDF_OBJECT_ARRAY) contents = "array";
                    else contents = "direct";
                }
                printf("PAGE %zu %" PRId64 " %" PRId64
                       " MEDIABOX %g %g %g %g RESOURCES %s CONTENTS %s\n",
                       i + 1, page->reference.object_number,
                       page->reference.generation, page->media_box_values[0],
                       page->media_box_values[1], page->media_box_values[2],
                       page->media_box_values[3],
                       page->resources == NULL ? "none" : "present", contents);
            }
        }
        pdf_pages_free(&pages);
        pdf_document_close(&document);
        if (!loaded) {
            pdf_error_print(&error, stderr);
            return pdf_error_exit_code(&error);
        }
        return 0;
    }

    pdf_reader reader;
    pdf_error error;
    pdf_error_init(&error);

    if (!reader_open(&reader,filename,&error)) {
        pdf_error_print(&error, stderr);
        return pdf_error_exit_code(&error);
    }

    printf("file size: %zu bytes\n",reader.size);

    if (dump_xref) {
        pdf_xref *xref = pdf_xref_parse(&reader, &error);
        if (xref != NULL) {
            printf("XREF %zu startxref %zu\n", xref->size, xref->startxref);
            printf("ROOT %" PRId64 " %" PRId64 "\n",
                   xref->root.object_number, xref->root.generation);
            for (size_t i = 0; i < xref->size; i++) {
                const pdf_xref_entry *entry = pdf_xref_get(xref, i);
                if (entry->present) {
                    printf("%zu %c %zu %u\n", i, entry->in_use ? 'n' : 'f',
                           entry->offset, (unsigned)entry->generation);
                }
            }
            printf("TRAILER\n");
            pdf_object_dump(xref->trailer, 0);
            pdf_xref_free(xref);
        }
        reader_close(&reader);
        if (error.code != PDF_ERROR_NONE) {
            pdf_error_print(&error, stderr);
            return pdf_error_exit_code(&error);
        }
        return 0;
    }

    pdf_lexer lexer;
    lexer_init(&lexer,&reader,&error);

    pdf_parser parser;
    parser_init(&parser,&lexer,&error);

    if (indirect_object) {
        pdf_indirect_object *obj = parser_parse_indirect_object(&parser);

        if (obj != NULL) {
            if (parser_expect_eof(&parser)) {
                printf("INDIRECT %" PRId64 " %" PRId64 "\n",
                       obj->object_number, obj->generation);
                pdf_object_dump(obj->body, 0);
                if (obj->is_stream) {
                    printf("STREAM %zu bytes\n", obj->stream.len);
                }
            }
            pdf_indirect_object_free(obj);
        }
    } else if (standalone_object) {
        pdf_object *obj = parser_parse_object(&parser);

        if (obj != NULL) {
            pdf_object_dump(obj, 0);
            pdf_object_free(obj);
            (void)parser_expect_eof(&parser);
        }
    } else {
        while (1) {
            const pdf_token *next = parser_peek(&parser);

            if (next == NULL || next->type == PDF_TOKEN_EOF) {
                break;
            }

            pdf_object *obj = parser_parse_object(&parser);

            if (!obj) {
                break;
            }

            pdf_object_dump(obj,0);

            pdf_object_free(obj);
        }
    }

    parser_destroy(&parser);

    reader_close(&reader);

    if (error.code != PDF_ERROR_NONE) {
        pdf_error_print(&error, stderr);
        return pdf_error_exit_code(&error);
    }

    return 0;
}
