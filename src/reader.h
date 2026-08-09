#ifndef PDF_READER_H
#define PDF_READER_H

#include <stddef.h>

/*

pdf_reader
│
├── data ────────┐
│                ▼
│     +---+---+---+---+---+---+---+
│     | % | P | D | F | - | 1 | . |
│     +---+---+---+---+---+---+---+
│                ▲
│                │
├── pos = 3 ─────┘
│
└── size = full pdf file size

*/

typedef struct {
    unsigned char *data;
    size_t size;
    size_t pos;
} pdf_reader;


//load full pdf file into memory
int reader_open(pdf_reader *reader,const char *filename);

//free reader usage memory
void reader_close(pdf_reader *reader);

//check current byte，but no move cursor
int reader_peek(const pdf_reader *reader);

//get current byte and move cursor
int reader_get(pdf_reader *reader);

// move into specified position
// sucess return 1
// fail if offset over file size 
int reader_seek(pdf_reader *reader,size_t offset);

//get current cursor position
size_t reader_tell(const pdf_reader *reader);

//check if reach EOF
int reader_eof(const pdf_reader *reader);

#endif