#ifndef PDF_FONT_TEXT_H
#define PDF_FONT_TEXT_H

#include "font.h"
#include "text_state.h"

/* M9 page bridge: M7 operations -> M8 geometry with real font widths, plus
 * per-string UTF-8 decoding with the same active font. Empty ActualText scopes
 * suppress decoded callbacks while retaining all raw geometry/state updates.
 * This is a raw geometry
 * + decode diagnostic event, not an M10 TextItem and not a glyph bbox. */
typedef struct {
    const pdf_text_event *raw;  /* M8 event; nested pointers borrowed. */
    const pdf_font *font;       /* Active font handle for this string. */
    const unsigned char *utf8;  /* NUL-terminated; utf8_len excludes it. */
    size_t utf8_len;
    size_t replacements;        /* Unmapped character codes decoded as U+FFFD. */
} pdf_font_text_event;

/* Everything in the event is valid only during the callback: copy to retain.
 * Zero/error aborts (unset error becomes malformed); prior events are not
 * rolled back, so consumers must stage the whole document. */
typedef int (*pdf_font_text_consumer)(void *, const pdf_font_text_event *, pdf_error *);

/* Document-wide staged-output counters, zero-initialize before the first page.
 * utf8_bytes is capped by max_total_decoded_size (independent of M6's decoded
 * contents counter), cmap_bytes by max_total_decoded_size (including repeated
 * CMap decoding on later pages), and events by max_container_entries. After a failure the
 * document result must be discarded. events counts all raw events, including
 * empty strings and suppressed strings; utf8_bytes counts published UTF-8 only. */
typedef struct {
    size_t utf8_bytes;
    size_t events;
    size_t cmap_bytes;
} pdf_font_text_totals;

/* One page: fresh M8 state and font context over borrowed decoded Contents.
 * Rejects nonzero Rotate. Every successful Tf resolves and validates its font,
 * even if nothing is shown; Q reselects the restored font. Unused font
 * resources are not loaded. gs validates the extraction-safe ExtGState subset
 * before M8, without modifying state; bare M8 still rejects gs. ExtGState
 * errors retain their object byte offset in the M7 decoded error message.
 * Do validates a named Image XObject before M8; Form and other subtypes remain
 * unsupported. Image pixels/filters/masks are not decoded and no OCR is done.
 * Line styles/path painting/clipping do not filter source text or alter its
 * geometry. XObject errors also retain their original object byte offset.
 * BDC accepts /Span with inline empty properties or zero-byte ActualText;
 * nonempty ActualText, named properties, other tags/keys remain unsupported.
 * Malformed ActualText types retain their classification; marked-content
 * errors use the decoded operator offset without a font-byte wrapper.
 * Marked state is page-local and independent of q/Q.
 * Font errors are reported through M7 at the Page
 * offset, with the decoded byte and original font byte offset in the message.
 * document/page must stay open for the call. NULL limits = defaults. */
int pdf_font_text_page_interpret(pdf_document *, const pdf_page *,
                                 const unsigned char *, size_t, const pdf_limits *,
                                 size_t page_offset, pdf_font_text_totals *,
                                 pdf_font_text_consumer, void *, pdf_error *);

/* Two JSON diagnostic lines for a borrowed event (call during the callback):
 * the M8 raw geometry line, then a "decode" line with UTF-8 hex, an ASCII-only
 * escaped preview, replacement count, font policy and per-code width/source.
 * Partial bytes may be written on I/O failure: stage and write after success. */
int pdf_font_text_event_dump(FILE *, const pdf_font_text_event *, size_t page, pdf_error *);

#endif
