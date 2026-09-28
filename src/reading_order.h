#ifndef PDF_READING_ORDER_H
#define PDF_READING_ORDER_H

#include "text_items.h"

/* M11 plain text: single-column, horizontal reading order over borrowed M10
 * items. Pages ascend; within a page items are grouped into lines by baseline
 * y against the line's anchor (its first item in y-descending order) with
 * tolerance max(1.5, min(anchor, item effective_size) * 0.25); lines run top
 * to bottom, items left to right, ties by sequence. A U+0020 is inserted when
 * the gap after an item exceeds 0.25 * its effective_size, unless either side
 * already has U+0020/U+00A0 there. Text is copied verbatim (no normalization).
 * Each line ends with "\n"; pages with text are separated by one blank line;
 * pages without text print nothing. No multi-column detection. */
typedef struct {
    unsigned char *data; /* Caller-owned; NULL when len is zero. */
    size_t len;
    size_t lines, pages_with_text, replacements;
} pdf_plain_text;

/* One visual line: indices into items->items in left-to-right order. */
typedef struct {
    size_t page;
    double anchor_y, anchor_size;
    const size_t *indices; /* Borrowed from the pdf_text_lines storage. */
    size_t count;
} pdf_text_line;
typedef struct {
    pdf_text_line *lines;
    size_t len;
    size_t *indices; /* Owned storage for every line's indices. */
} pdf_text_lines;

/* Group items into lines in reading order. Result must start zeroed; empty on
 * failure. Any item with horizontal == 0 is unsupported (v1.0 has no rotated,
 * mirrored or vertical text), located at its Page offset. */
int pdf_text_lines_build(const pdf_text_items *, pdf_text_lines *, pdf_error *);
void pdf_text_lines_free(pdf_text_lines *);
/* Debug view: one JSON line per text line (page, anchor, item sequences). */
int pdf_text_lines_dump(FILE *, const pdf_text_items *, const pdf_text_lines *, pdf_error *);

/* Build the plain text. Output size is limited by max_total_decoded_size
 * (NULL limits = defaults). Result must start zeroed; empty on failure. */
int pdf_plain_text_build(const pdf_text_items *, const pdf_limits *, pdf_plain_text *,
                         pdf_error *);
void pdf_plain_text_free(pdf_plain_text *);

#endif
