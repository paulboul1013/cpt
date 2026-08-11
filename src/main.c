#include "reader.h"
#include "lexer.h"

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

    while(1) {
        pdf_token token= lexer_next(&lexer);

        if (token.type==PDF_TOKEN_EOF) {
            break;
        }

        if (token.type==PDF_TOKEN_INT) {
            printf("INT: %ld\n",token.integer);
        }
        else {
            printf("INVALID\n");
        }
    }


    reader_close(&reader);

    return 0;
}