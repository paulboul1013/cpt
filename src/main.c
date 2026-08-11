#include "reader.h"
#include "lexer.h"
#include "parser.h"
#include "object.h"

#include <stdio.h>

int main(int argc,char *argv[]) {
    
    if (argc!=2) {
        fprintf(stderr,"usage: %s document.pdf\n",argv[0]);
        return 1;
    }

    pdf_reader reader;

    if (!reader_open(&reader,argv[1])) {
        fprintf(stderr,"failed to open PDF\n");
        return 1;
    }

    printf("file size: %zu bytes\n",reader.size);

    pdf_lexer lexer;
    lexer_init(&lexer,&reader);

    pdf_parser parser;
    parser_init(&parser,&lexer);

    while(!reader_eof(&reader)) {
        pdf_object *obj = parser_parse_object(&parser);

        if (!obj) {
            break;
        }

        if (obj->type==PDF_OBJECT_INT) {
            printf("INT: %ld\n",obj->value.integer);
        }
        else {
            printf("INVALID\n");
        }

        pdf_object_free(obj);
    }


    reader_close(&reader);

    return 0;
}