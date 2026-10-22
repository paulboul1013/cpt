#include "extgstate.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    pdf_document *document;
    const pdf_limits *limits;
} checker;

static int fail(pdf_error *e, pdf_error_code code, size_t loc, const char *why) {
    pdf_error_set(e, code, loc, "extgstate", "%s", why);
    return 0;
}

static int bytes_equal(pdf_bytes bytes, const char *text) {
    size_t len = strlen(text);
    return bytes.len == len && memcmp(bytes.data, text, len) == 0;
}

static int name_is(const pdf_object *o, const char *text) {
    return o && o->type == PDF_OBJECT_NAME && bytes_equal(o->value.name, text);
}

/* Brent's cycle detection needs no allocation. Each field has its own bounded
 * reference chain and location, so a sibling's offset cannot leak into errors. */
static const pdf_object *leaf(checker *c, const pdf_object *o, size_t *loc, pdf_error *e) {
    pdf_reference saved = {0, 0};
    size_t power = 1, distance = 0, hops = 0;
    int have_saved = 0;
    while (o && o->type == PDF_OBJECT_REF) {
        pdf_reference r = o->value.reference;
        if (have_saved && saved.object_number == r.object_number && saved.generation == r.generation) {
            fail(e, PDF_ERROR_MALFORMED, *loc, "ExtGState reference cycle");
            return NULL;
        }
        if (hops >= c->limits->max_nesting_depth) {
            fail(e, PDF_ERROR_RESOURCE_LIMIT, *loc, "ExtGState reference chain too deep");
            return NULL;
        }
        hops++;
        if (!have_saved || distance == power) {
            if (have_saved) power = power <= SIZE_MAX / 2 ? power * 2 : SIZE_MAX;
            saved = r;
            have_saved = 1;
            distance = 0;
        }
        distance++;
        size_t offset = pdf_document_reference_offset(c->document, r);
        if (offset) *loc = offset;
        pdf_error local;
        pdf_error_init(&local);
        const pdf_indirect_object *resolved = pdf_resolve(c->document, r, &local);
        if (!resolved) {
            pdf_error_set(e, local.code ? local.code : PDF_ERROR_MALFORMED,
                          local.offset ? local.offset : *loc,
                          local.module ? local.module : "extgstate", "%s",
                          local.code ? local.message : "cannot resolve ExtGState reference");
            return NULL;
        }
        if (resolved->is_stream) {
            fail(e, PDF_ERROR_MALFORMED, *loc, "ExtGState value must not be a stream");
            return NULL;
        }
        o = resolved->body;
    }
    if (!o) fail(e, PDF_ERROR_MALFORMED, *loc, "missing ExtGState resource or value");
    return o;
}

static const pdf_object *dictionary(checker *c, const pdf_object *o, size_t *loc, pdf_error *e) {
    o = leaf(c, o, loc, e);
    if (!o) return NULL;
    if (o->type != PDF_OBJECT_DICT) {
        fail(e, PDF_ERROR_MALFORMED, *loc, "ExtGState resource must be a dictionary");
        return NULL;
    }
    if (o->value.dict.len > c->limits->max_container_entries) {
        fail(e, PDF_ERROR_RESOURCE_LIMIT, *loc, "ExtGState dictionary exceeds entry limit");
        return NULL;
    }
    return o;
}

static int number(const pdf_object *o, double *value) {
    if (o->type == PDF_OBJECT_INT) *value = (double)o->value.integer;
    else if (o->type == PDF_OBJECT_REAL) *value = o->value.real;
    else return 0;
    return isfinite(*value);
}

static int field(checker *c, pdf_bytes key, const pdf_object *raw, size_t loc, pdf_error *e) {
    /* Reject unknown keys even when earlier fields were accepted. */
    if (!bytes_equal(key, "Type") && !bytes_equal(key, "SM") &&
        !bytes_equal(key, "SA") && !bytes_equal(key, "SMask") &&
        !bytes_equal(key, "AIS") && !bytes_equal(key, "CA") &&
        !bytes_equal(key, "ca") && !bytes_equal(key, "Font"))
        return fail(e, PDF_ERROR_UNSUPPORTED, loc, "unsupported ExtGState key");
    const pdf_object *o = leaf(c, raw, &loc, e);
    if (!o) return 0;
    if (bytes_equal(key, "Type"))
        return name_is(o, "ExtGState") || fail(e, PDF_ERROR_MALFORMED, loc, "invalid ExtGState Type");
    if (bytes_equal(key, "SMask")) {
        if (o->type == PDF_OBJECT_DICT) {
            if (o->value.dict.len > c->limits->max_container_entries)
                return fail(e, PDF_ERROR_RESOURCE_LIMIT, loc, "soft mask dictionary exceeds entry limit");
            return fail(e, PDF_ERROR_UNSUPPORTED, loc, "ExtGState soft mask is unsupported");
        }
        return name_is(o, "None") || fail(e, PDF_ERROR_MALFORMED, loc, "ExtGState SMask must be /None or a dictionary");
    }
    if (bytes_equal(key, "SA") || bytes_equal(key, "AIS")) {
        if (o->type != PDF_OBJECT_BOOL)
            return fail(e, PDF_ERROR_MALFORMED, loc, "ExtGState SA/AIS must be boolean");
        if (bytes_equal(key, "AIS") && o->value.boolean)
            return fail(e, PDF_ERROR_UNSUPPORTED, loc, "ExtGState AIS true is unsupported");
        return 1;
    }
    if (bytes_equal(key, "Font")) {
        if (o->type != PDF_OBJECT_ARRAY)
            return fail(e, PDF_ERROR_MALFORMED, loc, "ExtGState Font must be [font-ref size]");
        if (o->value.array.len > c->limits->max_container_entries)
            return fail(e, PDF_ERROR_RESOURCE_LIMIT, loc, "ExtGState Font array exceeds entry limit");
        if (o->value.array.len != 2 || o->value.array.items[0]->type != PDF_OBJECT_REF)
            return fail(e, PDF_ERROR_MALFORMED, loc, "ExtGState Font must be [font-ref size]");
        const pdf_object *size = leaf(c, o->value.array.items[1], &loc, e);
        double value;
        if (!size) return 0;
        if (!number(size, &value))
            return fail(e, PDF_ERROR_MALFORMED, loc, "ExtGState Font size must be finite number");
        return fail(e, PDF_ERROR_UNSUPPORTED, loc, "ExtGState Font is unsupported");
    }
    double value;
    if (!number(o, &value) || value < 0 || value > 1)
        return fail(e, PDF_ERROR_MALFORMED, loc, "ExtGState SM/CA/ca must be finite number in 0..1");
    if (!bytes_equal(key, "SM") && value != 1)
        return fail(e, PDF_ERROR_UNSUPPORTED, loc, "ExtGState alpha other than 1 is unsupported");
    return 1;
}

int pdf_extgstate_check(pdf_document *document, const pdf_object *resources,
                       const unsigned char *name, size_t name_len,
                       const pdf_limits *limits, size_t page_offset, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return 0;
    if (!document || (!name && name_len))
        return fail(e, PDF_ERROR_IO, page_offset, "invalid ExtGState input");
    pdf_limits defaults;
    if (!limits) { pdf_limits_default(&defaults); limits = &defaults; }
    if (name_len > limits->max_token_size)
        return fail(e, PDF_ERROR_RESOURCE_LIMIT, page_offset, "ExtGState name exceeds token limit");
    checker c = {document, limits};
    size_t loc = page_offset;
    const pdf_object *r = dictionary(&c, resources, &loc, e);
    if (!r) return 0;
    const pdf_object *states = dictionary(&c, pdf_dict_get(r, "ExtGState"), &loc, e);
    if (!states) return 0;
    const pdf_object *state = dictionary(&c, pdf_dict_get_bytes(states, name, name_len), &loc, e);
    if (!state) return 0;
    for (size_t i = 0; i < state->value.dict.len; i++) {
        const pdf_dict_entry *entry = &state->value.dict.entries[i];
        if (!field(&c, entry->key, entry->value, loc, e)) return 0;
    }
    return 1;
}
