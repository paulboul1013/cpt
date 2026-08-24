#include "object.h"
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int pdf_bytes_copy(pdf_bytes *destination, const unsigned char *data, size_t len) {
    if (len == 0) {
        destination->data = NULL;
        destination->len = 0;
        return 1;
    }

    destination->data = malloc(len);

    if (destination->data == NULL) {
        destination->len = 0;
        return 0;
    }

    memcpy(destination->data, data, len);
    destination->len = len;
    return 1;
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
static pdf_object *pdf_object_alloc(pdf_object_type type) {
    pdf_object *obj = malloc(sizeof(*obj));

    if (!obj) {
        return NULL;
    }

    obj->type = type;
    return obj;
}

pdf_object *pdf_object_new_null(void) {
    return pdf_object_alloc(PDF_OBJECT_NULL);
}

pdf_object *pdf_object_new_bool(int value) {
    pdf_object *obj = pdf_object_alloc(PDF_OBJECT_BOOL);

    if (obj == NULL) {
        return NULL;
    }

    obj->value.boolean = value != 0;
    return obj;
}

pdf_object *pdf_object_new_int(int64_t value) {
    pdf_object *obj = pdf_object_alloc(PDF_OBJECT_INT);

    if (obj == NULL) {
        return NULL;
    }

    obj->value.integer = value;

    return obj;
}

pdf_object *pdf_object_new_real(double value) {
    pdf_object *obj = pdf_object_alloc(PDF_OBJECT_REAL);

    if (obj == NULL) {
        return NULL;
    }

    obj->value.real = value;
    return obj;
}

pdf_object *pdf_object_new_ref(int64_t object_number, int64_t generation) {
    pdf_object *obj = pdf_object_alloc(PDF_OBJECT_REF);

    if (obj == NULL) {
        return NULL;
    }

    obj->value.reference.object_number = object_number;
    obj->value.reference.generation = generation;
    return obj;
}

void pdf_object_free(pdf_object *obj) {
    if (!obj) {
        return;
    }

    switch(obj->type) {
        case PDF_OBJECT_NAME: {
            free(obj->value.name.data);
            break;
        }

        case PDF_OBJECT_STRING:
            free(obj->value.string.data);
            break;

        case PDF_OBJECT_HEX_STRING:
            free(obj->value.hex_string.data);
            break;

        case PDF_OBJECT_ARRAY:{
            for(size_t i=0;i<obj->value.array.len;i++){
                pdf_object_free(obj->value.array.items[i]);
            }

            free(obj->value.array.items);

            break;
        }

        case PDF_OBJECT_DICT:{

            for (size_t i = 0;i < obj->value.dict.len;i++) {

                free(obj->value.dict.entries[i].key.data);

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

        if (new_cap < a->cap || new_cap > SIZE_MAX / sizeof(*a->items)) {
            return 0;
        }

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

static void print_bytes_hex(const pdf_bytes *bytes) {
    for (size_t i = 0; i < bytes->len; i++) {
        printf("%02X", bytes->data[i]);
    }
}

void pdf_object_dump(const pdf_object *obj,int depth) {
    if (obj==NULL) {
        return;
    }

    print_indent(depth);

    switch(obj->type) {
        case PDF_OBJECT_INT: {
            printf("INT %" PRId64 "\n", obj->value.integer);
            break;
        }

        case PDF_OBJECT_NULL:
            printf("NULL\n");
            break;

        case PDF_OBJECT_BOOL:
            printf("BOOL %s\n", obj->value.boolean ? "true" : "false");
            break;

        case PDF_OBJECT_REAL:
            printf("REAL %.17g\n", obj->value.real);
            break;

        case PDF_OBJECT_STRING:
            printf("STRING HEX ");
            print_bytes_hex(&obj->value.string);
            putchar('\n');
            break;

        case PDF_OBJECT_HEX_STRING:
            printf("HEX STRING HEX ");
            print_bytes_hex(&obj->value.hex_string);
            putchar('\n');
            break;

        case PDF_OBJECT_REF:
            printf("REF %" PRId64 " %" PRId64 "\n",
                   obj->value.reference.object_number,
                   obj->value.reference.generation);
            break;

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

                (void)fwrite(obj->value.dict.entries[i].key.data, 1,
                             obj->value.dict.entries[i].key.len, stdout);
                printf(":\n");

                pdf_object_dump(obj->value.dict.entries[i].value,depth+2);
            }

            break;

        }

        case PDF_OBJECT_NAME: {
            printf("NAME ");
            (void)fwrite(obj->value.name.data, 1, obj->value.name.len, stdout);
            putchar('\n');

            break;
        }

        default:
            printf("UNKNOWN\n");
            break;
    }
}

pdf_object *pdf_object_new_name(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    return pdf_object_new_name_bytes((const unsigned char *)name, strlen(name));
}

pdf_object *pdf_object_new_name_bytes(const unsigned char *data, size_t len) {
    pdf_object *obj = pdf_object_alloc(PDF_OBJECT_NAME);

    if (obj == NULL) {
        return NULL;
    }

    if (len > 0 && data == NULL) {
        free(obj);
        return NULL;
    }

    if (!pdf_bytes_copy(&obj->value.name, data, len)) {
        free(obj);
        return NULL;
    }

    return obj;
}

static pdf_object *pdf_object_new_bytes(pdf_object_type type,
                                         const unsigned char *data, size_t len) {
    pdf_object *obj = pdf_object_alloc(type);

    if (obj == NULL) {
        return NULL;
    }

    if (len > 0 && data == NULL) {
        free(obj);
        return NULL;
    }

    pdf_bytes *destination = type == PDF_OBJECT_STRING
                                 ? &obj->value.string
                                 : &obj->value.hex_string;

    if (!pdf_bytes_copy(destination, data, len)) {
        free(obj);
        return NULL;
    }

    return obj;
}

pdf_object *pdf_object_new_string_bytes(const unsigned char *data, size_t len) {
    return pdf_object_new_bytes(PDF_OBJECT_STRING, data, len);
}

pdf_object *pdf_object_new_hex_string_bytes(const unsigned char *data, size_t len) {
    return pdf_object_new_bytes(PDF_OBJECT_HEX_STRING, data, len);
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
    if (key == NULL) {
        return 0;
    }

    return pdf_dict_push_bytes(dict, (const unsigned char *)key, strlen(key), value);
}

int pdf_dict_push_bytes(pdf_object *dict, const unsigned char *key, size_t len,
                        pdf_object *value) {
    if (dict==NULL || dict->type!=PDF_OBJECT_DICT) {
        return 0;
    }

    pdf_dict *d = &dict->value.dict;

    if (d->len >= d->cap) {
        size_t new_cap = d->cap == 0 ? 4 : d->cap * 2;

        if (new_cap < d->cap || new_cap > SIZE_MAX / sizeof(*d->entries)) {
            return 0;
        }

        pdf_dict_entry *new_entries = realloc(d->entries,sizeof(pdf_dict_entry) * new_cap);

        if (new_entries == NULL) {
            return 0;
        }

        d->entries = new_entries;
        d->cap = new_cap;
    }


    pdf_bytes key_copy = {0};

    if (len > 0 && key == NULL) {
        return 0;
    }

    if (!pdf_bytes_copy(&key_copy, key, len)) {
        return 0;
    }

    d->entries[d->len].key = key_copy;
    d->entries[d->len].value = value;

    d->len++;

    return 1;
}

const pdf_object *pdf_dict_get(const pdf_object *dict, const char *key) {
    if (key == NULL) {
        return NULL;
    }

    return pdf_dict_get_bytes(dict, (const unsigned char *)key, strlen(key));
}

const pdf_object *pdf_dict_get_bytes(const pdf_object *dict,
                                     const unsigned char *key, size_t len) {
    if (dict == NULL || dict->type != PDF_OBJECT_DICT || (len > 0 && key == NULL)) {
        return NULL;
    }

    for (size_t i = dict->value.dict.len; i > 0; i--) {
        const pdf_dict_entry *entry = &dict->value.dict.entries[i - 1];

        if (entry->key.len == len &&
            (len == 0 || memcmp(entry->key.data, key, len) == 0)) {
            return entry->value;
        }
    }

    return NULL;
}
