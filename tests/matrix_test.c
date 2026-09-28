#include "../src/matrix.h"
#include <assert.h>
#include <float.h>
#include <math.h>

static void near(double a, double b) { assert(fabs(a-b) <= 1e-8 + 1e-9*fabs(b)); }
int main(void) {
    pdf_matrix id = pdf_matrix_identity(), out;
    pdf_matrix scale = {2,0,0,2,0,0}, shift = {1,0,0,1,10,0};
    pdf_point p;
    assert(pdf_matrix_compose(shift, scale, &out));
    assert(pdf_matrix_point(out, (pdf_point){1,0}, &p)); near(p.x,22);
    assert(pdf_matrix_compose(scale, shift, &out));
    assert(pdf_matrix_point(out, (pdf_point){1,0}, &p)); near(p.x,12);
    pdf_matrix rotate = {0,1,-1,0,100,200};
    assert(pdf_matrix_point(rotate, (pdf_point){10,0}, &p)); near(p.x,100); near(p.y,210);
    assert(pdf_matrix_vector(rotate, (pdf_point){10,0}, &p)); near(p.x,0); near(p.y,10);
    pdf_matrix shear = {1,2,3,4,5,6};
    assert(pdf_matrix_compose(id, shear, &out));
    assert(pdf_matrix_point(out,(pdf_point){2,3}, &p)); near(p.x,16); near(p.y,22);
    assert(pdf_matrix_compose(shear,id,&out)); near(out.f,6);
    assert(pdf_matrix_compose((pdf_matrix){0},shear,&out));
    assert(pdf_matrix_point(out,(pdf_point){2,3}, &p)); near(p.x,5); near(p.y,6);
    out=id;
    assert(!pdf_matrix_compose((pdf_matrix){DBL_MAX,0,0,1,0,0},scale,&out)); near(out.a,1);
    assert(!pdf_matrix_point(scale,(pdf_point){DBL_MAX,0},&p));
    assert(!pdf_matrix_vector(id,(pdf_point){NAN,0},&p));
    assert(!pdf_matrix_finite((pdf_matrix){1,0,0,1,INFINITY,0}));
    return 0;
}
