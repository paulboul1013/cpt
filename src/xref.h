#ifndef PDF_XREF_H
#define PDF_XREF_H

#include <stddef.h>
#include <stdint.h>

#include "object.h"
#include "reader.h"

typedef struct {
    size_t offset; /* Byte offset for in-use entries; next free number otherwise. */
    uint16_t generation;
    int present;
    int in_use;
} pdf_xref_entry;

typedef struct {
    size_t startxref;
    size_t size;
    pdf_reference root;
    pdf_xref_entry *entries;
    pdf_object *trailer; /* Owned dictionary. */
} pdf_xref;

/* Parses a traditional xref table and restores the reader cursor.
 * The caller owns the returned table, including its trailer dictionary. */
pdf_xref *pdf_xref_parse(pdf_reader *reader, pdf_error *error);
const pdf_xref_entry *pdf_xref_get(const pdf_xref *xref, size_t object_number);
void pdf_xref_free(pdf_xref *xref);

#endif
