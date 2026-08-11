#include "object.h"
#include <stdlib.h>

/*
obj
 │
 ▼
+----------------------+
| type = PDF_OBJECT_INT|
+----------------------+
| value.integer = 123  |
+----------------------+
*/
pdf_object *pdf_object_new_int(long value) {
    pdf_object *obj = malloc(sizeof(*obj));

    if (!obj) {
        return NULL;
    }

    obj->type = PDF_OBJECT_INT;
    obj->value.integer = value;

    return obj;
}

void pdf_object_free(pdf_object *obj) {
    if (!obj) {
        return;
    }

    free(obj);//only for int,real,bool

    //todo recursive free for array
    
}