#ifndef PDF_CONTENT_INTERPRETER_H
#define PDF_CONTENT_INTERPRETER_H

#include "error.h"
#include "limits.h"
#include "object.h"

typedef enum {
    PDF_CONTENT_BT, PDF_CONTENT_ET, PDF_CONTENT_TF, PDF_CONTENT_TM,
    PDF_CONTENT_TD, PDF_CONTENT_TD_LEADING, PDF_CONTENT_NEXT_LINE,
    PDF_CONTENT_TJ, PDF_CONTENT_TJ_ARRAY, PDF_CONTENT_QUOTE,
    PDF_CONTENT_DOUBLE_QUOTE, PDF_CONTENT_SAVE, PDF_CONTENT_RESTORE,
    PDF_CONTENT_CM
} pdf_content_operator;

typedef struct {
    pdf_content_operator kind;
    size_t offset; /* Decoded-content byte offset of operator. */
    const pdf_object *const *operands; /* Borrowed, including all descendants. */
    size_t operand_count;
} pdf_content_operation;

/* Operands are type/arity checked and read-only; valid only during this call.
 * Return zero to stop; optionally set error's code/message. Prior callbacks are
 * not rolled back if a later operation fails: consumers must stage page output.
 * No font decoding or geometry is performed here. */
typedef int (*pdf_content_visitor)(void *context,
                                  const pdf_content_operation *operation,
                                  pdf_error *error);
/* Optional debug notification; name is borrowed and may contain arbitrary bytes
 * other than NUL. Do not print it unescaped. Unknown operators are not visited. */
typedef void (*pdf_content_warning)(void *context, const char *name, size_t offset);

typedef struct {
    size_t operations; /* Supported operations only; zero on any failure. */
    size_t text_shows;
    size_t error_offset; /* Exact decoded offset on failure, zero on success. */
} pdf_content_result;

/* Borrows data/limits for this call; NULL data is allowed only for len == 0.
 * NULL limits selects defaults. Each call has independent page state.
 * Error offset is file_offset (Page/Contents reference), while its message and
 * result.error_offset identify decoded bytes. result/error must be non-NULL.
 * No stdout/stderr output; warnings occur only when a warning callback is given. */
int pdf_content_interpret(const unsigned char *data, size_t len,
                           const pdf_limits *limits, size_t file_offset,
                           pdf_content_visitor visitor, pdf_content_warning warning,
                           void *context, pdf_content_result *result, pdf_error *error);
#endif
