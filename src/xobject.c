#include "xobject.h"
#include <stdint.h>
#include <string.h>

static int fail(pdf_error *e, pdf_error_code code, size_t loc, const char *why) {
    pdf_error_set(e, code, loc, "xobject", "%s", why);
    return 0;
}

static int name_is(const pdf_object *o, const char *s) {
    return o && o->type == PDF_OBJECT_NAME && o->value.name.len == strlen(s) &&
        memcmp(o->value.name.data, s, strlen(s)) == 0;
}

/* A stream is allowed only at the XObject target, never at a resource table or
 * scalar field. Brent cycle detection uses constant memory per chain. */
static const pdf_object *leaf(pdf_document *d, const pdf_object *o, const pdf_limits *limits,
                               size_t *loc, int *stream, pdf_error *e) {
    pdf_reference saved = {0};
    size_t power = 1, distance = 0, hops = 0;
    int have_saved = 0;
    *stream = 0;
    while (o && o->type == PDF_OBJECT_REF) {
        pdf_reference r = o->value.reference;
        if (have_saved && r.object_number == saved.object_number && r.generation == saved.generation) {
            fail(e, PDF_ERROR_MALFORMED, *loc, "XObject reference cycle"); return NULL;
        }
        if (hops++ >= limits->max_nesting_depth) {
            fail(e, PDF_ERROR_RESOURCE_LIMIT, *loc, "XObject reference chain too deep"); return NULL;
        }
        if (!have_saved || distance == power) {
            if (have_saved) power = power <= SIZE_MAX / 2 ? power * 2 : SIZE_MAX;
            saved = r; have_saved = 1; distance = 0;
        }
        distance++;
        size_t at = pdf_document_reference_offset(d, r);
        if (at) *loc = at;
        const pdf_indirect_object *resolved = pdf_resolve(d, r, e);
        if (!resolved) return NULL;
        o = resolved->body;
        *stream = resolved->is_stream;
        if (*stream) return o;
    }
    if (!o) fail(e, PDF_ERROR_MALFORMED, *loc, "missing XObject resource or value");
    return o;
}

static const pdf_object *dictionary(pdf_document *d, const pdf_object *o,
    const pdf_limits *limits, size_t *loc, pdf_error *e) {
    int stream;
    o = leaf(d, o, limits, loc, &stream, e);
    if (!o) return NULL;
    if (stream || o->type != PDF_OBJECT_DICT) {
        fail(e, PDF_ERROR_MALFORMED, *loc, "XObject resource table must be a dictionary"); return NULL;
    }
    if (o->value.dict.len > limits->max_container_entries) {
        fail(e, PDF_ERROR_RESOURCE_LIMIT, *loc, "XObject dictionary exceeds entry limit"); return NULL;
    }
    return o;
}

static const pdf_object *field(pdf_document *d, const pdf_object *o, const pdf_limits *limits,
                                size_t *loc, pdf_error *e) {
    int stream;
    o = leaf(d, o, limits, loc, &stream, e);
    if (o && stream) {
        fail(e, PDF_ERROR_MALFORMED, *loc, "XObject field must not be a stream"); return NULL;
    }
    return o;
}

int pdf_xobject_check_image(pdf_document *d, const pdf_object *resources,
                            const unsigned char *name, size_t name_len,
                            const pdf_limits *limits, size_t page_offset, pdf_error *e) {
    if (!e || e->code) return 0;
    if (!d || (!name && name_len)) return fail(e, PDF_ERROR_IO, page_offset, "invalid XObject input");
    if (!limits) limits = &d->reader.limits;
    if (name_len > limits->max_token_size)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, page_offset, "XObject name exceeds token limit");
    size_t loc = page_offset;
    const pdf_object *r = dictionary(d, resources, limits, &loc, e);
    if (!r) return 0;
    const pdf_object *objects = dictionary(d, pdf_dict_get(r, "XObject"), limits, &loc, e);
    if (!objects) return 0;
    int stream;
    const pdf_object *image = leaf(d, pdf_dict_get_bytes(objects, name, name_len), limits, &loc, &stream, e);
    if (!image) return 0;
    if (!stream || image->type != PDF_OBJECT_DICT)
        return fail(e, PDF_ERROR_MALFORMED, loc, "XObject must be a stream dictionary");
    if (image->value.dict.len > limits->max_container_entries)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, loc, "XObject dictionary exceeds entry limit");
    const pdf_object *type = pdf_dict_get(image, "Type");
    size_t at = loc;
    if (type) {
        type = field(d, type, limits, &at, e);
        if (!type) return 0;
        if (!name_is(type, "XObject")) return fail(e, PDF_ERROR_MALFORMED, at, "invalid XObject Type");
    }
    at = loc;
    const pdf_object *subtype = field(d, pdf_dict_get(image, "Subtype"), limits, &at, e);
    if (!subtype) return 0;
    if (subtype->type != PDF_OBJECT_NAME)
        return fail(e, PDF_ERROR_MALFORMED, at, "XObject Subtype must be a name");
    if (!name_is(subtype, "Image"))
        return fail(e, PDF_ERROR_UNSUPPORTED, at, "only Image XObjects are supported");
    const char *dimensions[] = {"Width", "Height"};
    for (size_t i = 0; i < 2; i++) {
        at = loc;
        const pdf_object *v = field(d, pdf_dict_get(image, dimensions[i]), limits, &at, e);
        if (!v) return 0;
        if (v->type != PDF_OBJECT_INT || v->value.integer <= 0)
            return fail(e, PDF_ERROR_MALFORMED, at, "Image dimensions must be positive integers");
    }
    return 1;
}
