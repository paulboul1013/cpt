#define _POSIX_C_SOURCE 200809L
#include "reader.h"

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int reader_open(pdf_reader *reader,const char *filename,pdf_error *error) {
    pdf_limits limits;
    pdf_limits_default(&limits);
    return reader_open_with_limits(reader, filename, error, &limits);
}

int reader_open_with_limits(pdf_reader *reader, const char *filename,
                            pdf_error *error, const pdf_limits *limits) {
    if (reader == NULL || filename == NULL) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "reader",
                      "input reader or filename is null");
        return 0;
    }

    reader->data = NULL;
    reader->size = 0;
    reader->pos = 0;

    if (limits == NULL) {
        pdf_limits_default(&reader->limits);
    } else {
        reader->limits = *limits;
    }

    FILE *fp = fopen(filename,"rb");

    if (fp==NULL) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "reader",
                      "could not open input file");
        return 0;
    }

    struct stat info;
    if (fstat(fileno(fp), &info) != 0 || !S_ISREG(info.st_mode)) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "reader",
                      "input is not a regular file");
        fclose(fp);
        return 0;
    }

    if (fseek(fp,0,SEEK_END)!=0) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "reader",
                      "could not seek to input end");
        fclose(fp);
        return 0;
    }

    long file_size = ftell(fp);

    if (file_size < 0 ){
        pdf_error_set(error, PDF_ERROR_IO, 0, "reader",
                      "could not determine input size");
        fclose(fp);
        return 0;
    }

    if ((uintmax_t)file_size > (uintmax_t)reader->limits.max_input_size) {
        pdf_error_set(error, PDF_ERROR_RESOURCE_LIMIT, 0, "reader",
                      "input exceeds configured size limit");
        fclose(fp);
        return 0;
    }

    rewind(fp);

    unsigned char *data = malloc((size_t)file_size);
    
    if (data==NULL && file_size!=0) {
        pdf_error_set(error, PDF_ERROR_OUT_OF_MEMORY, 0, "reader",
                      "could not allocate input buffer");
        fclose(fp);
        return 0;
    }

    size_t read_size = fread(data,1,(size_t)file_size,fp);

    if (read_size!=(size_t)file_size) {
        pdf_error_set(error, PDF_ERROR_IO, read_size, "reader",
                      "could not read complete input");
        free(data);
        fclose(fp);
        return 0;
    }

    fclose(fp);

    reader->data = data;
    reader->size=(size_t)file_size;
    reader->pos=0;

    return 1;
}

int reader_validate_pdf_header(const pdf_reader *reader, pdf_error *error) {
    static const unsigned char signature[] = "%PDF-";

    if (reader == NULL || (reader->data == NULL && reader->size != 0)) {
        pdf_error_set(error, PDF_ERROR_IO, 0, "reader", "input reader is invalid");
        return 0;
    }

    for (size_t offset = 0; offset < sizeof(signature) - 1; offset++) {
        if (offset >= reader->size) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "reader",
                          "incomplete PDF header");
            return 0;
        }
        if (reader->data[offset] != signature[offset]) {
            pdf_error_set(error, PDF_ERROR_MALFORMED, offset, "reader",
                          "expected %%PDF- header at byte zero");
            return 0;
        }
    }

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
