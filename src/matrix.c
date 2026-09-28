#include "matrix.h"
#include <math.h>

pdf_matrix pdf_matrix_identity(void) { return (pdf_matrix){1,0,0,1,0,0}; }
int pdf_matrix_finite(pdf_matrix m) {
    return isfinite(m.a) && isfinite(m.b) && isfinite(m.c) &&
           isfinite(m.d) && isfinite(m.e) && isfinite(m.f);
}
int pdf_matrix_compose(pdf_matrix a, pdf_matrix b, pdf_matrix *out) {
    if (!out || !pdf_matrix_finite(a) || !pdf_matrix_finite(b)) return 0;
    pdf_matrix r = {a.a*b.a+a.b*b.c, a.a*b.b+a.b*b.d,
                    a.c*b.a+a.d*b.c, a.c*b.b+a.d*b.d,
                    a.e*b.a+a.f*b.c+b.e, a.e*b.b+a.f*b.d+b.f};
    if (!pdf_matrix_finite(r)) return 0;
    *out = r;
    return 1;
}
int pdf_matrix_point(pdf_matrix m, pdf_point p, pdf_point *out) {
    if (!out || !pdf_matrix_finite(m) || !isfinite(p.x) || !isfinite(p.y)) return 0;
    pdf_point r = {m.a*p.x+m.c*p.y+m.e, m.b*p.x+m.d*p.y+m.f};
    if (!isfinite(r.x) || !isfinite(r.y)) return 0;
    *out = r;
    return 1;
}
int pdf_matrix_vector(pdf_matrix m, pdf_point v, pdf_point *out) {
    if (!pdf_matrix_finite(m)) return 0;
    m.e = m.f = 0;
    return pdf_matrix_point(m,v,out);
}
