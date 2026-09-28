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
#include "reading_order.h"

#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
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

typedef enum {
    MODE_TEXT, MODE_OBJECTS, MODE_OBJECT, MODE_INDIRECT, MODE_XREF, MODE_HEADER,
    MODE_TRAILER, MODE_DUMP_OBJECT, MODE_PAGES, MODE_CONTENTS, MODE_CONTENT, MODE_ITEMS
} cli_mode;
static const struct { const char *flag; cli_mode mode; } MODES[] = {
    {"--dump-objects", MODE_OBJECTS}, {"--object", MODE_OBJECT}, {"--indirect", MODE_INDIRECT},
    {"--dump-header", MODE_HEADER}, {"--dump-xref", MODE_XREF}, {"--dump-trailer", MODE_TRAILER},
    {"--dump-object", MODE_DUMP_OBJECT}, {"--dump-pages", MODE_PAGES},
    {"--dump-contents", MODE_CONTENTS}, {"--dump-content", MODE_CONTENT},
    {"--dump-text-items", MODE_ITEMS},
};
#define PDFTEXT_VERSION "1.0.0"

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
        "usage: %s [-o output.txt] input.pdf\n"
        "       %s MODE input.pdf\n"
        "       %s --dump-object N input.pdf\n"
        "       %s --help | --version\n"
        "\n"
        "Without MODE, extracts the text layer as UTF-8 plain text in reading order\n"
        "(single column, horizontal text) to stdout, or to FILE with -o.\n"
        "\n"
        "MODE (debug output to stdout, one mode at a time):\n"
        "  --dump-header      PDF header version\n"
        "  --dump-xref        traditional xref table and trailer\n"
        "  --dump-trailer     trailer dictionary\n"
        "  --dump-object N    indirect object N (generation from xref)\n"
        "  --dump-pages       page tree summary\n"
        "  --dump-contents    decoded Contents length per page\n"
        "  --dump-content     content operator summary per page\n"
        "  --dump-text-items  one JSON line per text item (source order)\n"
        "  --dump-objects     every object in the file, sequentially\n"
        "  --object           one standalone object\n"
        "  --indirect         one indirect object\n"
        "\n"
        "Exit status: 0 success, 1 usage, 2 I/O, 3 malformed PDF, 4 unsupported\n"
        "feature, 5 out of memory, 6 resource limit exceeded.\n",
        program, program, program, program);
}
static int usage_error(const char *program, const char *why) {
    fprintf(stderr, "%s: %s\n", program, why);
    usage(stderr, program);
    return 1;
}
static int fail_with(pdf_error *error) {
    pdf_error_print(error, stderr);
    return pdf_error_exit_code(error);
}

/* Write all bytes to path atomically: a hidden temp file in the same directory,
 * then rename. On failure the temp file is removed and path is untouched. A
 * symlink at path is replaced by a regular file (rename semantics). */
static int write_file_atomic(const char *path, const unsigned char *data, size_t len,
                             pdf_error *error) {
    static const char pattern[] = ".pdftext-XXXXXX";
    const char *slash = strrchr(path, '/');
    size_t dir = slash ? (size_t)(slash - path) + 1 : 0;
    char *temp = malloc(dir + sizeof(pattern));
    if (!temp) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, 0, "output", "cannot allocate output path");
        return 0;
    }
    memcpy(temp, path, dir);
    memcpy(temp + dir, pattern, sizeof(pattern));
    int fd = mkstemp(temp);
    if (fd < 0) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "output", "cannot create temporary file for %s: %s",
                      path, strerror(errno));
        free(temp);
        return 0;
    }
    int saved = 0;
    mode_t mask = umask(0);
    umask(mask);
    FILE *stream = NULL;
    if (fchmod(fd, 0666 & ~mask) != 0 || !(stream = fdopen(fd, "wb"))) {
        saved = errno;
        close(fd);
    } else {
        if (len && fwrite(data, 1, len, stream) != len) saved = errno ? errno : EIO;
        if (fflush(stream) != 0 && !saved) saved = errno;
        if (fsync(fileno(stream)) != 0 && !saved) saved = errno;
        if (fclose(stream) != 0 && !saved) saved = errno;
    }
    if (!saved && rename(temp, path) != 0) saved = errno;
    if (saved) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "output", "cannot write %s: %s", path, strerror(saved));
        unlink(temp);
    }
    free(temp);
    return !saved;
}

static int extract_text(const char *filename, const char *output) {
    pdf_document document;
    pdf_error error;
    pdf_error_init(&error);
    if (!pdf_document_open(&document, filename, NULL, &error)) return fail_with(&error);
    pdf_text_items items = {0};
    pdf_plain_text text = {0};
    int ok = pdf_text_items_extract(&document, NULL, &items, &error) &&
             pdf_plain_text_build(&items, &document.reader.limits, &text, &error);
    pdf_text_items_free(&items);
    pdf_document_close(&document);
    if (ok) {
        if (output) ok = write_file_atomic(output, text.data, text.len, &error);
        else if ((text.len && fwrite(text.data, 1, text.len, stdout) != text.len) || fflush(stdout) != 0) {
            pdf_error_set(&error, PDF_ERROR_IO, 0, "output", "cannot write text to stdout");
            ok = 0;
        }
    }
    if (ok && text.replacements)
        fprintf(stderr, "pdftext: warning: %zu undecodable bytes replaced with U+FFFD\n", text.replacements);
    if (ok && text.lines == 0) fputs("pdftext: no extractable text layer\n", stderr);
    pdf_plain_text_free(&text);
    return ok ? 0 : fail_with(&error);
}

static int dump_header(const char *filename) {
    pdf_reader reader;
    pdf_error error;
    pdf_error_init(&error);
    if (!reader_open(&reader, filename, &error)) return fail_with(&error);
    int ok = reader_validate_pdf_header(&reader, &error);
    if (ok) {
        const unsigned char *d = reader.data;
        size_t n = reader.size;
        if (n >= 8 && d[5] >= '0' && d[5] <= '9' && d[6] == '.' && d[7] >= '0' && d[7] <= '9' &&
            (n == 8 || d[8] == '\r' || d[8] == '\n' || d[8] == ' ' || d[8] == '\t' ||
             d[8] == '\f' || d[8] == '\0' || d[8] == '%'))
            printf("HEADER PDF-%c.%c\n", d[5], d[7]);
        else {
            pdf_error_set(&error, PDF_ERROR_MALFORMED, 5, "reader", "invalid PDF header version");
            ok = 0;
        }
    }
    reader_close(&reader);
    return ok ? 0 : fail_with(&error);
}

static int dump_object_number(const char *filename, int64_t number) {
    pdf_document document;
    pdf_error error;
    pdf_error_init(&error);
    if (!pdf_document_open(&document, filename, NULL, &error)) return fail_with(&error);
    const pdf_xref_entry *entry = pdf_xref_get(document.xref, (size_t)number);
    int ok = entry && entry->present && entry->in_use;
    if (!ok) pdf_error_set(&error, PDF_ERROR_MALFORMED, 0, "document",
                           "object %" PRId64 " is not an in-use xref entry", number);
    const pdf_indirect_object *object = NULL;
    if (ok) {
        pdf_reference reference = {number, entry->generation};
        object = pdf_resolve(&document, reference, &error);
        ok = object != NULL;
    }
    if (ok) {
        printf("OBJECT %" PRId64 " %" PRId64 "\n", object->object_number, object->generation);
        pdf_object_dump(object->body, 0);
        if (object->is_stream) printf("STREAM %zu bytes\n", object->stream.len);
    }
    pdf_document_close(&document);
    return ok ? 0 : fail_with(&error);
}

static int run_cli(int argc, char *argv[]) {
    const char *program = argc > 0 && argv[0] ? argv[0] : "pdftext";
    cli_mode mode = MODE_TEXT;
    int mode_set = 0, options_done = 0;
    int64_t object_number = 0;
    const char *filename = NULL, *output = NULL;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(stdout, program); return 0; }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) { printf("pdftext %s\n", PDFTEXT_VERSION); return 0; }
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!options_done && strcmp(arg, "--") == 0) { options_done = 1; continue; }
        if (options_done || arg[0] != '-' || arg[1] == '\0') {
            if (filename) return usage_error(program, "only one input file is allowed");
            filename = arg;
            continue;
        }
        if (strcmp(arg, "-o") == 0) {
            if (output) return usage_error(program, "-o given more than once");
            if (++i >= argc || argv[i][0] == '\0') return usage_error(program, "-o requires a file name");
            if (argv[i][0] == '-')
                return usage_error(program, "-o file name must not start with '-' (use ./NAME)");
            output = argv[i];
            continue;
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "--version") == 0)
            return usage_error(program, "--help and --version take no other arguments");
        size_t k = 0;
        while (k < sizeof(MODES) / sizeof(MODES[0]) && strcmp(arg, MODES[k].flag) != 0) k++;
        if (k == sizeof(MODES) / sizeof(MODES[0])) return usage_error(program, "unknown option");
        if (mode_set) return usage_error(program, "only one mode may be given");
        mode = MODES[k].mode;
        mode_set = 1;
        if (mode == MODE_DUMP_OBJECT) {
            if (++i >= argc) return usage_error(program, "--dump-object requires an object number");
            const char *text = argv[i];
            char *end = NULL;
            errno = 0;
            long long value = (text[0] >= '0' && text[0] <= '9') ? strtoll(text, &end, 10) : 0;
            if (!end || *end != '\0' || errno || value <= 0)
                return usage_error(program, "--dump-object requires a positive object number");
            object_number = value;
        }
    }
    if (!filename) return usage_error(program, "missing input file");
    if (output && mode != MODE_TEXT) return usage_error(program, "-o is only valid for text output");

    if (mode == MODE_TEXT) return extract_text(filename, output);
    if (mode == MODE_HEADER) return dump_header(filename);
    if (mode == MODE_DUMP_OBJECT) return dump_object_number(filename, object_number);

    int standalone_object = mode == MODE_OBJECT;
    int indirect_object = mode == MODE_INDIRECT;
    int dump_xref = mode == MODE_XREF || mode == MODE_TRAILER;
    int trailer_only = mode == MODE_TRAILER;
    int dump_pages = mode == MODE_PAGES;
    int dump_contents = mode == MODE_CONTENTS;
    int dump_content = mode == MODE_CONTENT;
    int dump_items = mode == MODE_ITEMS;

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

    if (!trailer_only) printf("file size: %zu bytes\n",reader.size);

    if (trailer_only) {
        pdf_xref *xref = pdf_xref_parse(&reader, &error);
        if (xref != NULL) {
            printf("TRAILER\n");
            pdf_object_dump(xref->trailer, 0);
            pdf_xref_free(xref);
        }
        reader_close(&reader);
        if (error.code != PDF_ERROR_NONE) return fail_with(&error);
        return 0;
    }

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

/* Every mode that succeeded must also have written stdout completely
 * (e.g. a full disk or a closed pipe turns into an I/O error, exit 2). */
int main(int argc, char *argv[]) {
    int status = run_cli(argc, argv);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        if (status == 0) {
            fprintf(stderr, "output: io error at byte 0: cannot write stdout\n");
            return 2;
        }
    }
    return status;
}
