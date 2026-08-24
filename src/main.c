#include "reader.h"
#include "lexer.h"
#include "parser.h"
#include "object.h"
#include "error.h"

#include <stdio.h>
#include <string.h>

int main(int argc,char *argv[]) {
    
    int standalone_object = 0;
    const char *filename = NULL;

    if (argc == 2) {
        filename = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--object") == 0) {
        standalone_object = 1;
        filename = argv[2];
    } else {
        fprintf(stderr,"usage: %s [--object] document.pdf\n",argv[0]);
        return 1;
    }

    pdf_reader reader;
    pdf_error error;
    pdf_error_init(&error);

    if (!reader_open(&reader,filename,&error)) {
        pdf_error_print(&error, stderr);
        return pdf_error_exit_code(&error);
    }

    printf("file size: %zu bytes\n",reader.size);

    pdf_lexer lexer;
    lexer_init(&lexer,&reader,&error);

    pdf_parser parser;
    parser_init(&parser,&lexer,&error);

    if (standalone_object) {
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
