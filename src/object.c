#include "object.h"
#include <stdlib.h>
#include <stdio.h>

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

    switch(obj->type) {
        case PDF_OBJECT_ARRAY:{
            for(size_t i=0;i<obj->value.array.len;i++){
                pdf_object_free(obj->value.array.items[i]);
            }

            free(obj->value.array.items);

            break;
        }

        default:
            break;
    }

    free(obj);
    
}

pdf_object *pdf_object_new_array(void) {
    pdf_object *obj = malloc(sizeof(pdf_object));

    if (!obj) {
        return NULL;
    }

    obj->type = PDF_OBJECT_ARRAY;
    obj->value.array.items = NULL;
    obj->value.array.len = 0;
    obj->value.array.cap = 0;

    return obj;
}

int pdf_array_push(pdf_object *array,pdf_object *item) {

    if (array==NULL || array->type!=PDF_OBJECT_ARRAY) {
        return 0;
    }

    pdf_array *a = &array->value.array;

    if (a->len >= a->cap) {
        size_t new_cap = a->cap==0 ? 4: a->cap*2;

        pdf_object **new_items=realloc(a->items,sizeof(pdf_object*) * new_cap);

        if (new_items==NULL) {
            return 0;
        }

        a->items = new_items;
        a->cap = new_cap;
    }

    a->items[a->len++] = item;

    return 1;
}

static void print_ident(int depth) {
    for(int i=0;i<depth;i++) {
        printf(" ");
    }
}

void pdf_object_dump(const pdf_object *obj,int depth) {
    if (obj==NULL) {
        return;
    }

    print_ident(depth);

    switch(obj->type) {
        case PDF_OBJECT_INT: {
            printf("INT %ld\n",obj->value.integer);
            break;
        }

        case PDF_OBJECT_ARRAY: {
            printf("ARRAY\n");

            for(size_t i=0; i<obj->value.array.len;i++) {
                pdf_object_dump(obj->value.array.items[i],depth+1);
            }
            break;
        }

        default:
            printf("UNKNOWN\n");
            break;
    }
}