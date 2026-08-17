#include "object.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char *pdf_strdup(const char *s)
{
    size_t len = strlen(s);

    char *copy = malloc(len + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, s, len + 1);

    return copy;
}


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
        case PDF_OBJECT_NAME: {
            free(obj->value.name);
            break;
        }

        case PDF_OBJECT_ARRAY:{
            for(size_t i=0;i<obj->value.array.len;i++){
                pdf_object_free(obj->value.array.items[i]);
            }

            free(obj->value.array.items);

            break;
        }

        case PDF_OBJECT_DICT:{

            for (size_t i = 0;i < obj->value.dict.len;i++) {

                free(obj->value.dict.entries[i].key);

                pdf_object_free(obj->value.dict.entries[i].value);
            }

            free(obj->value.dict.entries);

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

static void print_indent(int depth) {
    for(int i=0;i<depth;i++) {
        printf(" ");
    }
}

void pdf_object_dump(const pdf_object *obj,int depth) {
    if (obj==NULL) {
        return;
    }

    print_indent(depth);

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

        case PDF_OBJECT_DICT:{

            printf("DICT\n");

            for (size_t i = 0;i < obj->value.dict.len;i++) {

                print_indent(depth + 1);

                printf("%s:\n",obj->value.dict.entries[i].key);

                pdf_object_dump(obj->value.dict.entries[i].value,depth+2);
            }

            break;

        }

        case PDF_OBJECT_NAME: {
            printf(
                "NAME %s\n",
                obj->value.name
            );

            break;
        }

        default:
            printf("UNKNOWN\n");
            break;
    }
}

pdf_object *pdf_object_new_name(const char *name) {
    pdf_object *obj = malloc(sizeof(pdf_object));

    if (obj == NULL) {
        return NULL;
    }

    char *copy = pdf_strdup(name);

    if (copy == NULL) {
        free(obj);
        return NULL;
    }

    obj->type = PDF_OBJECT_NAME;
    obj->value.name = copy;

    return obj;
}

pdf_object *pdf_object_new_dict(void)
{
    pdf_object *obj = malloc(sizeof(pdf_object));

    if (obj == NULL) {
        return NULL;
    }

    obj->type = PDF_OBJECT_DICT;

    obj->value.dict.entries = NULL;
    obj->value.dict.len = 0;
    obj->value.dict.cap = 0;

    return obj;
}

int pdf_dict_push(pdf_object *dict,const char *key,pdf_object *value) {
    if (dict==NULL || dict->type!=PDF_OBJECT_DICT) {
        return 0;
    }

    pdf_dict *d = &dict->value.dict;

    if (d->len >= d->cap) {
        size_t new_cap = d->cap == 0 ? 4 : d->cap * 2;

        pdf_dict_entry *new_entries = realloc(d->entries,sizeof(pdf_dict_entry) * new_cap);

        if (new_entries == NULL) {
            return 0;
        }

        d->entries = new_entries;
        d->cap = new_cap;
    }


    char *key_copy = pdf_strdup(key);

    if (key_copy == NULL) {
        return 0;
    }

    d->entries[d->len].key = key_copy;
    d->entries[d->len].value = value;

    d->len++;

    return 1;
}