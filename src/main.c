#include "reader.h"

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

    printf("first bytes: ");

    for(int i=0;i<8;i++) {
        int c=reader_get(&reader);

        if (c==-1){
            break;
        }
        
        putchar(c);
    }
    reader_seek(&reader, 0);

    putchar('\n');

    reader_close(&reader);

    return 0;
}