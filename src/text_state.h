#ifndef PDF_TEXT_STATE_H
#define PDF_TEXT_STATE_H

#include "content_interpreter.h"
#include "matrix.h"
#include "pages.h"

typedef struct { const unsigned char *data; size_t len; } pdf_text_bytes;
typedef struct {
    pdf_text_bytes font; /* Borrowed immutable bytes; font_set distinguishes empty name. */
    int font_set;
    double font_size, char_spacing, word_spacing, hscale, leading, rise;
    int rendering_mode;
    pdf_matrix ctm, text_matrix, line_matrix;
    size_t saved_depth;
} pdf_text_snapshot;
typedef struct {
    pdf_text_bytes bytes;
    pdf_text_snapshot state; /* State before showing this string. */
    pdf_matrix after, rendering_matrix;
    pdf_point origin, advance; /* Default user-space baseline origin/vector, NOT bbox. */
    size_t offset, segment_index, source_order;
} pdf_text_event;

/* Horizontal single-byte Simple Font seam. All bytes are borrowed for the call.
 * Return finite width in 1000 units. No Unicode mapping or guessed fallback.
 * Zero or error aborts. Contexts remain caller-owned throughout state lifetime. */
typedef int (*pdf_text_metrics)(void *, pdf_text_bytes font, unsigned char code,
                                double *width_1000, pdf_error *);
/* Event and all nested pointers valid only for callback. Copy to retain.
 * Zero/error aborts; prior events are not rolled back: stage entire output. */
typedef int (*pdf_text_consumer)(void *, const pdf_text_event *, pdf_error *);
typedef struct pdf_text_state pdf_text_state;

/* Owned opaque state; copies limits (NULL = defaults). No default font.
 * Allocation errors offset 0. Destroy accepts NULL and frees retained names/stack.
 * After any visitor/interpreter failure, only destroy is allowed. */
pdf_text_state *pdf_text_state_create(const pdf_limits *, pdf_text_metrics, void *,
                                      pdf_text_consumer, void *, pdf_error *);
void pdf_text_state_destroy(pdf_text_state *);
/* Direct M7 visitor: requires M7-validated operands/grammar. Local errors use
 * operation decoded offset; M7 alone translates to file offset. No reentry. */
int pdf_text_state_visit(void *, const pdf_content_operation *, pdf_error *);
/* Read-only copy; font pointer valid until next visit or destroy. */
int pdf_text_state_snapshot(const pdf_text_state *, pdf_text_snapshot *);
/* Diagnostic JSON line for a valid library event (including a retained copy).
 * Caller supplies 1-based page ordinal. Raw bytes/font use hex and explicit font
 * length; doubles use 9 decimal places and thread-local C numeric locale.
 * Borrows FILE/event; does not close/flush or change process locale. On I/O
 * failure partial diagnostic bytes may exist: call only after staged success.
 * Error offset is event decoded offset; no content/file offset translation. */
int pdf_text_event_dump(FILE *, const pdf_text_event *, size_t page, pdf_error *);
/* Fresh state per page; borrowed decoded Contents (already concatenated by M6).
 * Reject nonzero effective Rotate at page_offset; caller passes Page ref offset.
 * No page dictionary parsing, decoding, stdout, or rollback of prior events. */
int pdf_text_page_interpret(const pdf_page *, const unsigned char *, size_t,
                            const pdf_limits *, size_t page_offset,
                            pdf_text_metrics, void *, pdf_text_consumer, void *,
                            pdf_error *);
#endif
