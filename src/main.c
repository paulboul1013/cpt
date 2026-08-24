#include "reader.h"
#include "lexer.h"
#include "parser.h"
#include "object.h"
#include "error.h"

#include <stdio.h>

int main(int argc,char *argv[]) {
    
    if (argc!=2) {
        fprintf(stderr,"usage: %s document.pdf\n",argv[0]);
        return 1;
    }

    pdf_reader reader;
    pdf_error error;
    pdf_error_init(&error);

    if (!reader_open(&reader,argv[1],&error)) {
        pdf_error_print(&error, stderr);
        return pdf_error_exit_code(&error);
    }

    printf("file size: %zu bytes\n",reader.size);

    pdf_lexer lexer;
    lexer_init(&lexer,&reader,&error);

    pdf_parser parser;
    parser_init(&parser,&lexer,&error);

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

    parser_destroy(&parser);

    reader_close(&reader);

    if (error.code != PDF_ERROR_NONE) {
        pdf_error_print(&error, stderr);
        return pdf_error_exit_code(&error);
    }

    return 0;
}
