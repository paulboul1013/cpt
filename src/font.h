#ifndef PDF_FONT_H
#define PDF_FONT_H

#include <stdio.h>

#include "pages.h"

/* M9 Simple Font support: horizontal, single-byte /Type1 and /TrueType fonts
 * with explicit /FirstChar /LastChar /Widths. Encodings: /WinAnsiEncoding (name
 * or an /Encoding dictionary whose /BaseEncoding is /WinAnsiEncoding and that
 * has no /Differences), or no /Encoding at all (project ASCII fallback: only
 * 0x20-0x7E map, not a claim about the font's builtin encoding). Everything
 * else (Type3, MMType1, Type0/CID, Differences, ToUnicode, Symbol/ZapfDingbats,
 * symbolic descriptors, other encodings, missing metrics) is unsupported.
 * Embedded font programs are never read. */

typedef struct pdf_font_context pdf_font_context;
typedef struct pdf_font pdf_font;

typedef enum { PDF_FONT_TYPE1, PDF_FONT_TRUETYPE } pdf_font_subtype;
typedef enum {
    PDF_FONT_ENCODING_ASCII_FALLBACK, /* No /Encoding: 0x20-0x7E only. */
    PDF_FONT_ENCODING_WIN_ANSI        /* Name or BaseEncoding dictionary. */
} pdf_font_encoding;
typedef enum {
    PDF_FONT_WIDTH_TABLE,              /* Widths[code - FirstChar]. */
    PDF_FONT_WIDTH_MISSING,            /* Explicit descriptor /MissingWidth. */
    PDF_FONT_WIDTH_DESCRIPTOR_DEFAULT_ZERO /* Descriptor without /MissingWidth. */
} pdf_font_width_source;
typedef enum {
    PDF_FONT_MISSING_NONE,         /* No descriptor: out-of-range is unsupported. */
    PDF_FONT_MISSING_EXPLICIT,
    PDF_FONT_MISSING_DEFAULT_ZERO
} pdf_font_missing;

/* Read-only description of a parsed font. Pointers borrow the font handle. */
typedef struct {
    /* First resource name that loaded this font. Aliases of the same indirect
     * font share the handle, so this may differ from the name being shown. */
    const unsigned char *resource_name;
    size_t resource_name_len;
    const unsigned char *base_font;     /* /BaseFont bytes (arbitrary binary). */
    size_t base_font_len;
    pdf_font_subtype subtype;
    pdf_font_encoding encoding;
    unsigned first_char, last_char;
    pdf_font_missing missing;
    double missing_width;
    size_t offset; /* Nearest file offset: font reference, else Resources/Page. */
} pdf_font_info;

/* Caller-owned UTF-8. Must start zeroed (or freed). data is NUL-terminated for
 * convenience but len excludes it; never use strlen. Empty on failure. */
typedef struct {
    unsigned char *data;
    size_t len;
    size_t replacements; /* Bytes decoded as U+FFFD. */
} pdf_font_utf8;

/* One context per page. Borrows document and page Resources (may be NULL):
 * both must outlive the context. Copies limits (NULL = defaults). page_offset
 * locates errors that have no closer object. Allocation failure returns NULL
 * with an out-of-memory error at page_offset. */
pdf_font_context *pdf_font_context_create(pdf_document *, const pdf_object *resources,
                                          size_t page_offset, const pdf_limits *,
                                          pdf_error *);
/* Accepts NULL; frees every handle and binding. */
void pdf_font_context_destroy(pdf_font_context *);
/* Resolve a length-aware /Resources /Font name (may contain NUL). Returns a
 * borrowed immutable handle valid until destroy. Results are cached by name and
 * by indirect font reference; nothing is published on failure. After any
 * failure the context is destroy-only (later calls fail as malformed). Errors
 * use the nearest indirect object's file offset (font, /Font dictionary or a
 * referenced field). Direct fields use their containing indirect object; a
 * direct /Font inside Resources uses page_offset, since pdf_page does not keep
 * the Resources object's own offset. */
const pdf_font *pdf_font_context_resolve(pdf_font_context *, const unsigned char *name,
                                         size_t len, pdf_error *);

/* Constant-time width in 1000 text-space units for a raw code byte. source may
 * be NULL. Out-of-range codes without a descriptor are unsupported. */
int pdf_font_width(const pdf_font *, unsigned char code, double *width_1000,
                   pdf_font_width_source *source, pdf_error *);
/* Decode raw bytes to caller-owned UTF-8. One Unicode scalar per byte;
 * unmapped bytes become U+FFFD. Output length is limited by max_token_size.
 * Empty input succeeds with len 0. Widths are never consulted. */
int pdf_font_decode(const pdf_font *, const unsigned char *, size_t,
                    pdf_font_utf8 *, pdf_error *);
void pdf_font_utf8_free(pdf_font_utf8 *);

int pdf_font_get_info(const pdf_font *, pdf_font_info *);
const char *pdf_font_subtype_name(pdf_font_subtype);
const char *pdf_font_encoding_name(pdf_font_encoding);
const char *pdf_font_width_source_name(pdf_font_width_source);
const char *pdf_font_missing_name(pdf_font_missing);
/* Write a quoted, ASCII-only JSON string previewing UTF-8 bytes: printable
 * ASCII literal, everything else \\uXXXX (surrogate pairs above the BMP).
 * Truncated/invalid sequences print U+FFFD per byte; never reads past len. */
void pdf_font_json_preview(FILE *, const unsigned char *, size_t);
/* One JSON diagnostic line. Names are hex with explicit lengths; numbers use a
 * thread-local C numeric locale. Partial bytes may be written on I/O failure. */
int pdf_font_dump(FILE *, const pdf_font *, pdf_error *);

#endif
