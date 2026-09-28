#ifndef PDF_TEXT_ITEMS_H
#define PDF_TEXT_ITEMS_H

#include "font_text.h"

/* M10 TextItem: one owned item per non-empty shown string (Tj, ', ", or a TJ
 * string segment), in page/source order. Not sorted, merged or normalized;
 * reading order belongs to M11. All coordinates are default user space. */
typedef struct {
    const unsigned char *text;  /* UTF-8, NUL-terminated; text_len excludes it. */
    size_t text_len;
    const unsigned char *font;  /* Resource name bytes (may contain NUL). */
    size_t font_len;
    size_t page;                /* 1-based. */
    size_t source_order;        /* M8 page-local order; empty strings leave gaps. */
    size_t sequence;            /* 0-based, document-wide. */
    size_t offset;              /* Decoded content offset of the operator. */
    size_t page_offset;         /* Page object file offset, for diagnostics. */
    double x, y;                /* M8 baseline origin (CTM and rise applied). */
    double dx, dy;              /* Signed advance vector. */
    double width;               /* == dx; may be negative. */
    double em_height;           /* hypot(c,d) of rendering matrix: effective em
                                 * size, NOT a glyph bounding box. */
    double font_size;           /* Nominal Tf size. */
    double effective_size;      /* == em_height; for M11 line tolerance. */
    int rendering_mode;         /* 0-3; 3 is invisible but kept. */
    int horizontal;             /* |b|,|c| <= 1e-9*max(|a|,|d|), a>0, d>0. */
    size_t replacements;        /* Bytes decoded as U+FFFD. */
    pdf_font_subtype subtype;
    pdf_font_encoding encoding;
} pdf_text_item;

/* Caller-owned; zero-initialize. Owns items and every byte they point to. */
typedef struct {
    pdf_text_item *items;
    size_t len, cap;
    unsigned char *bytes;       /* Text and font storage. */
    size_t bytes_len, bytes_cap;
    size_t text_bytes, font_bytes;
} pdf_text_items;

/* Extract every page of an open document. The collection must be empty. All
 * or nothing: on failure it stays empty and error keeps the original cause.
 * NULL limits uses the document's limits. Items are limited by
 * max_container_entries and UTF-8 bytes by max_total_decoded_size; the M9
 * bridge enforces both first (counting empty strings too), so those M10
 * checks are defensive. Stored font-name bytes (shared only for consecutive
 * items with the same name on a page) have their own counter under
 * max_total_decoded_size. The document must stay open during the call only. */
int pdf_text_items_extract(pdf_document *, const pdf_limits *, pdf_text_items *, pdf_error *);
/* Accepts NULL or an empty collection; leaves it zeroed. */
void pdf_text_items_free(pdf_text_items *);
/* One JSON line: text/font as hex with lengths plus an ASCII-only preview;
 * numbers use nine decimals in a thread-local C numeric locale. Partial bytes
 * may be written on I/O failure: call only after successful extraction. */
int pdf_text_item_dump(FILE *, const pdf_text_item *, pdf_error *);

#endif
