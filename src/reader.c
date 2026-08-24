#include "reader.h"

#include <stdio.h>
#include <stdlib.h>

int reader_open(pdf_reader *reader,const char *filename){
    FILE *fp = fopen(filename,"rb");

    if (fp==NULL) {
        return 0;
    }

    if (fseek(fp,0,SEEK_END)!=0) {
        fclose(fp);
        return 0;
    }

    long file_size = ftell(fp);

    if (file_size < 0 ){
        fclose(fp);
        return 0;
    }

    rewind(fp);

    unsigned char *data = malloc((size_t)file_size);
    
    if (data==NULL && file_size!=0) {
        fclose(fp);
        return 0;
    }

    size_t read_size = fread(data,1,(size_t)file_size,fp);

    if (read_size!=(size_t)file_size) {
        free(data);
        return 0;
    }

    reader->data = data;
    reader->size=(size_t)file_size;
    reader->pos=0;

    return 1;
}

void reader_close(pdf_reader *reader) {
    free(reader->data);

    reader->data = NULL;
    reader->size=0;
    reader->pos=0;
}

int reader_peek(const pdf_reader *reader) {
    if (reader->pos>=reader->size){
        return -1;
    }

    return reader->data[reader->pos];
}

int reader_get(pdf_reader *reader) {
    if (reader->pos>=reader->size) {
        return -1;
    }

    return reader->data[reader->pos++];
}

int reader_seek(pdf_reader *reader,size_t offset) {
    if (offset > reader->size) {
        return 0;
    }

    reader->pos = offset;
    return 1;
}

size_t reader_tell(const pdf_reader *reader) {
    return reader->pos;
}

int reader_eof(const pdf_reader *reader) {
    return reader->pos >= reader->size;
}
