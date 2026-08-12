#ifndef PDF_OBJECT_H
#define PDF_OBJECT_H

#include <stddef.h>

/*
pdf data format
<<
    /Type /Page
    /Parent 2 0 R
    /MediaBox [0 0 612 792]
>>

PdfObject(DICT)
│
├── "Type"
│    └── PdfObject(NAME)
│         └── "Page"
│
├── "Parent"
│    └── PdfObject(REF)
│         ├── object = 2
│         └── generation = 0
│
└── "MediaBox"
     └── PdfObject(ARRAY)
          ├── INT 0
          ├── INT 0
          ├── INT 612
          └── INT 792

*/

typedef enum {
    PDF_OBJECT_NULL,
    PDF_OBJECT_BOOL,
    PDF_OBJECT_INT,
    PDF_OBJECT_REAL,
    PDF_OBJECT_NAME,
    PDF_OBJECT_STRING,
    PDF_OBJECT_ARRAY,
    PDF_OBJECT_DICT,
    PDF_OBJECT_REF
} pdf_object_type;

typedef struct pdf_object pdf_object;

typedef struct {
    pdf_object **items;
    size_t len;
    size_t cap;
} pdf_array;


/*

pdf_object
│
├── type
│
└── value
     ┌─────────────┐
     │ integer     │
     │ real        │
     │ name        │
     │ array       │
     └─────────────┘
          ↑
     only use one

*/
struct pdf_object {
    pdf_object_type type;

    union {
        int boolean;
        long integer;
        double real;

        char *name;
        
        pdf_array array;
    } value;
};

pdf_object *pdf_object_new_int(long value);
pdf_object *pdf_object_new_array(void);

int pdf_array_push(pdf_object *array,pdf_object *item);

void pdf_object_free(pdf_object *obj);

void pdf_object_dump(const pdf_object *obj,int depth);


#endif