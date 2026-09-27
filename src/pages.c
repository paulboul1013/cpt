#include "pages.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct page_ancestor {
    pdf_reference reference;
    const struct page_ancestor *parent;
} page_ancestor;

typedef struct {
    pdf_document *document;
    pdf_pages *pages;
    pdf_error *error;
    pdf_reference *seen; /* Open-addressed set; empty slots have object number zero. */
    size_t seen_len;
    size_t seen_cap;
    pdf_reference current_reference;
} page_walk;

static int same_reference(pdf_reference a, pdf_reference b) {
    return a.object_number == b.object_number && a.generation == b.generation;
}

static int name_is(const pdf_object *object, const char *name) {
    size_t len = strlen(name);
    return object && object->type == PDF_OBJECT_NAME &&
           object->value.name.len == len &&
           memcmp(object->value.name.data, name, len) == 0;
}

static int fail(page_walk *walk, pdf_error_code code, const char *message) {
    pdf_error_set(walk->error, code,
                  pdf_document_reference_offset(walk->document,
                                                walk->current_reference),
                  "pages", "%s", message);
    return 0;
}

static size_t reference_hash(pdf_reference reference) {
    uint64_t x = (uint64_t)reference.object_number;
    x ^= (uint64_t)reference.generation + UINT64_C(0x9e3779b97f4a7c15);
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (size_t)x;
}

static int mark_seen(page_walk *walk, pdf_reference reference) {
    if (!walk->seen_cap || walk->seen_len >= walk->seen_cap / 2) {
        size_t cap = walk->seen_cap ? walk->seen_cap * 2 : 16;
        if (cap < walk->seen_cap || cap > SIZE_MAX / sizeof(*walk->seen)) {
            return fail(walk, PDF_ERROR_RESOURCE_LIMIT,
                        "page tree reference set capacity exceeded");
        }
        pdf_reference *seen = calloc(cap, sizeof(*seen));
        if (!seen) return fail(walk, PDF_ERROR_OUT_OF_MEMORY,
                                "cannot allocate page tree reference set");
        for (size_t i = 0; i < walk->seen_cap; i++) {
            pdf_reference old = walk->seen[i];
            if (old.object_number == 0) continue;
            size_t slot = reference_hash(old) & (cap - 1);
            while (seen[slot].object_number != 0) slot = (slot + 1) & (cap - 1);
            seen[slot] = old;
        }
        free(walk->seen);
        walk->seen = seen;
        walk->seen_cap = cap;
    }
    size_t slot = reference_hash(reference) & (walk->seen_cap - 1);
    while (walk->seen[slot].object_number != 0) {
        if (same_reference(walk->seen[slot], reference)) {
            return fail(walk, PDF_ERROR_MALFORMED,
                        "page tree contains a repeated node");
        }
        slot = (slot + 1) & (walk->seen_cap - 1);
    }
    walk->seen[slot] = reference;
    walk->seen_len++;
    return 1;
}

static const pdf_object *resolve_value(page_walk *walk,
                                       const pdf_object *value) {
    if (!value || value->type != PDF_OBJECT_REF) {
        return value;
    }
    const pdf_indirect_object *object =
        pdf_resolve(walk->document, value->value.reference, walk->error);
    return object ? object->body : NULL;
}

static int number_value(const pdf_object *object, double *value) {
    if (!object) return 0;
    if (object->type == PDF_OBJECT_INT) {
        *value = (double)object->value.integer;
    } else if (object->type == PDF_OBJECT_REAL) {
        *value = object->value.real;
    } else {
        return 0;
    }
    return isfinite(*value);
}

static int validate_media_box(page_walk *walk, const pdf_object *box,
                              double values[4]) {
    if (!box || box->type != PDF_OBJECT_ARRAY || box->value.array.len != 4) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "/MediaBox must be an array of four numbers");
    }
    for (size_t i = 0; i < 4; i++) {
        if (!number_value(box->value.array.items[i], &values[i])) {
            return fail(walk, PDF_ERROR_MALFORMED,
                        "/MediaBox must contain finite numbers");
        }
    }
    if (values[2] <= values[0] || values[3] <= values[1]) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "/MediaBox has invalid bounds");
    }
    return 1;
}

static int inherited_properties(page_walk *walk, const pdf_object *dict,
                                const pdf_object **resources,
                                const pdf_object **media_box) {
    const pdf_object *local = pdf_dict_get(dict, "Resources");
    if (local) {
        local = resolve_value(walk, local);
        if (!local) return 0;
        if (local->type != PDF_OBJECT_DICT) {
            return fail(walk, PDF_ERROR_MALFORMED,
                        "/Resources must be a dictionary");
        }
        *resources = local;
    }
    local = pdf_dict_get(dict, "MediaBox");
    if (local) {
        double values[4];
        local = resolve_value(walk, local);
        if (!local) return 0;
        if (!validate_media_box(walk, local, values)) return 0;
        *media_box = local;
    }
    return 1;
}

static int append_page(page_walk *walk, pdf_page page) {
    pdf_pages *pages = walk->pages;
    size_t limit = walk->document->reader.limits.max_page_count;
    if (pages->len >= limit) {
        return fail(walk, PDF_ERROR_RESOURCE_LIMIT, "page count limit exceeded");
    }
    if (pages->len == pages->cap) {
        size_t cap = pages->cap ? pages->cap * 2 : 8;
        if (cap < pages->cap || cap > limit) cap = limit;
        if (cap <= pages->cap || cap > SIZE_MAX / sizeof(*pages->items)) {
            return fail(walk, PDF_ERROR_RESOURCE_LIMIT,
                        "page array capacity exceeded");
        }
        pdf_page *items = realloc(pages->items, cap * sizeof(*items));
        if (!items) return fail(walk, PDF_ERROR_OUT_OF_MEMORY,
                                "cannot allocate page array");
        pages->items = items;
        pages->cap = cap;
    }
    pages->items[pages->len++] = page;
    return 1;
}

static int walk_node(page_walk *walk, pdf_reference reference,
                     const page_ancestor *parent, size_t depth,
                     const pdf_object *resources,
                     const pdf_object *media_box) {
    walk->current_reference = reference;
    if (depth > walk->document->reader.limits.max_nesting_depth) {
        return fail(walk, PDF_ERROR_RESOURCE_LIMIT,
                    "page tree depth limit exceeded");
    }
    for (const page_ancestor *ancestor = parent; ancestor;
         ancestor = ancestor->parent) {
        if (same_reference(ancestor->reference, reference)) {
            return fail(walk, PDF_ERROR_MALFORMED, "page tree cycle");
        }
    }
    if (!mark_seen(walk, reference)) return 0;
    const pdf_indirect_object *indirect =
        pdf_resolve(walk->document, reference, walk->error);
    if (!indirect) return 0;
    const pdf_object *dict = indirect->body;
    if (!dict || dict->type != PDF_OBJECT_DICT) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "page tree object must be a dictionary");
    }
    const pdf_object *type = pdf_dict_get(dict, "Type");
    int is_pages = name_is(type, "Pages");
    if (!is_pages && !name_is(type, "Page")) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "page tree object has invalid /Type");
    }
    const pdf_object *declared_parent = pdf_dict_get(dict, "Parent");
    if (parent) {
        if (!declared_parent || declared_parent->type != PDF_OBJECT_REF ||
            !same_reference(declared_parent->value.reference, parent->reference)) {
            return fail(walk, PDF_ERROR_MALFORMED,
                        "page tree /Parent does not match its parent");
        }
    } else if (declared_parent) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "root /Pages must not have /Parent");
    }
    if (!inherited_properties(walk, dict, &resources, &media_box)) return 0;
    if (!is_pages) {
        if (!parent) return fail(walk, PDF_ERROR_MALFORMED,
                                 "Catalog /Pages must refer to a /Pages node");
        if (!media_box) return fail(walk, PDF_ERROR_MALFORMED,
                                    "page has no inherited /MediaBox");
        pdf_page page = {0};
        page.reference = reference;
        page.resources = resources;
        page.media_box = media_box;
        page.contents = pdf_dict_get(dict, "Contents");
        if (!validate_media_box(walk, media_box, page.media_box_values)) return 0;
        return append_page(walk, page);
    }
    const pdf_object *kids = pdf_dict_get(dict, "Kids");
    const pdf_object *count = pdf_dict_get(dict, "Count");
    if (!kids || kids->type != PDF_OBJECT_ARRAY || !count ||
        count->type != PDF_OBJECT_INT || count->value.integer < 0) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "/Pages requires array /Kids and nonnegative integer /Count");
    }
    size_t before = walk->pages->len;
    page_ancestor current = {reference, parent};
    for (size_t i = 0; i < kids->value.array.len; i++) {
        const pdf_object *kid = kids->value.array.items[i];
        if (!kid || kid->type != PDF_OBJECT_REF) {
            return fail(walk, PDF_ERROR_MALFORMED,
                        "/Kids entries must be indirect references");
        }
        if (!walk_node(walk, kid->value.reference, &current, depth + 1,
                       resources, media_box)) return 0;
        walk->current_reference = reference;
    }
    if ((uint64_t)count->value.integer != (uint64_t)(walk->pages->len - before)) {
        return fail(walk, PDF_ERROR_MALFORMED,
                    "/Count does not match descendant page count");
    }
    return 1;
}

void pdf_pages_free(pdf_pages *pages) {
    if (!pages) return;
    free(pages->items);
    *pages = (pdf_pages){0};
}

int pdf_pages_load(pdf_document *document, pdf_pages *pages, pdf_error *error) {
    if (!document || !pages || !error) return 0;
    pdf_error_clear(error);
    pdf_pages_free(pages);
    page_walk walk = {document, pages, error, NULL, 0, 0, {0, 0}};
    if (!document->xref) {
        return fail(&walk, PDF_ERROR_MALFORMED, "document is not open");
    }
    walk.current_reference = pdf_document_root(document);
    const pdf_indirect_object *catalog =
        pdf_resolve(document, walk.current_reference, error);
    if (!catalog) return 0;
    if (!catalog->body || catalog->body->type != PDF_OBJECT_DICT ||
        !name_is(pdf_dict_get(catalog->body, "Type"), "Catalog")) {
        return fail(&walk, PDF_ERROR_MALFORMED,
                    "trailer /Root must be a /Catalog dictionary");
    }
    const pdf_object *root = pdf_dict_get(catalog->body, "Pages");
    if (!root || root->type != PDF_OBJECT_REF) {
        return fail(&walk, PDF_ERROR_MALFORMED,
                    "Catalog /Pages must be an indirect reference");
    }
    int ok = walk_node(&walk, root->value.reference, NULL, 1, NULL, NULL);
    free(walk.seen);
    if (!ok) {
        pdf_pages_free(pages);
        return 0;
    }
    return 1;
}
