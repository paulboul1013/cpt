#ifndef PDF_MATRIX_H
#define PDF_MATRIX_H

typedef struct { double a, b, c, d, e, f; } pdf_matrix;
typedef struct { double x, y; } pdf_point;

pdf_matrix pdf_matrix_identity(void);
int pdf_matrix_finite(pdf_matrix m);
/* Apply first, then second. Output is unchanged on non-finite input/result.
 * Singular matrices are valid. No inverse or coordinate-system conversion. */
int pdf_matrix_compose(pdf_matrix first, pdf_matrix second, pdf_matrix *out);
int pdf_matrix_point(pdf_matrix m, pdf_point p, pdf_point *out);
int pdf_matrix_vector(pdf_matrix m, pdf_point v, pdf_point *out);
#endif
