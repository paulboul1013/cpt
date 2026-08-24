#ifndef PDF_OBJECT_H
#define PDF_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "bytes.h"

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
    PDF_OBJECT_HEX_STRING,
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

typedef struct {
    int64_t object_number;
    int64_t generation;
} pdf_reference;


typedef struct {
    pdf_bytes key;
    pdf_object *value;
} pdf_dict_entry;

typedef struct {
    pdf_dict_entry *entries;

    size_t len;
    size_t cap;
} pdf_dict;

/*
PdfObject
│
├── INT
├── NAME
├── ARRAY
│
└── DICT
*/

struct pdf_object {
    pdf_object_type type;

    union {
        int boolean;
        int64_t integer;
        double real;

        pdf_bytes name;
        pdf_bytes string;
        pdf_bytes hex_string;
        pdf_reference reference;
        
        pdf_array array;

        pdf_dict dict;
    } value;
};


pdf_object *pdf_object_new_null(void);
pdf_object *pdf_object_new_bool(int value);
pdf_object *pdf_object_new_int(int64_t value);
pdf_object *pdf_object_new_real(double value);
pdf_object *pdf_object_new_array(void);
pdf_object *pdf_object_new_name(const char *name);
pdf_object *pdf_object_new_name_bytes(const unsigned char *data, size_t len);
pdf_object *pdf_object_new_string_bytes(const unsigned char *data, size_t len);
pdf_object *pdf_object_new_hex_string_bytes(const unsigned char *data, size_t len);
pdf_object *pdf_object_new_ref(int64_t object_number, int64_t generation);
pdf_object *pdf_object_new_dict(void);

/* On success, the array owns item. On failure, the caller retains item. */
int pdf_array_push(pdf_object *array,pdf_object *item);

void pdf_object_free(pdf_object *obj);

/* On success, the dictionary owns value and copies key. On failure, the caller retains value. */
int pdf_dict_push(pdf_object *dict,const char *key,pdf_object *value);
int pdf_dict_push_bytes(pdf_object *dict, const unsigned char *key, size_t len,
                        pdf_object *value);
const pdf_object *pdf_dict_get(const pdf_object *dict, const char *key);
const pdf_object *pdf_dict_get_bytes(const pdf_object *dict,
                                     const unsigned char *key, size_t len);

void pdf_object_dump(const pdf_object *obj,int depth);


#endif
