/* Integration/debug consumer only. Explicit Courier-600 fixture adapter, never
 * linked into the product CLI as a fallback or claimed as a font parser. */
#include "../src/contents.h"
#include "../src/text_state.h"
#include <assert.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { pdf_text_event event; size_t page; unsigned char *bytes, *font; } saved_event;
typedef struct {
    saved_event *items;
    size_t len, cap, page, limit, abort_at, metrics_fail_at, metrics_calls;
} trace;
static void clear_trace(trace *t) {
    for(size_t i=0;i<t->len;i++) { free(t->items[i].bytes); free(t->items[i].font); }
    free(t->items); t->items=NULL; t->len=t->cap=0;
}
static int courier600(void *context,pdf_text_bytes font,unsigned char code,double *w,pdf_error *e) {
    trace *t=context;
    if (t->metrics_calls++==t->metrics_fail_at || font.len!=2 ||
        memcmp(font.data,"F1",2)!=0 || code<32 || code>126) {
        pdf_error_set(e,PDF_ERROR_UNSUPPORTED,0,"test-metrics","unsupported fixture metrics"); return 0;
    }
    *w=600; return 1;
}
static int collect(void *context,const pdf_text_event *ev,pdf_error *e) {
    trace *t=context;
    if(t->len>=t->limit || t->len==t->abort_at) {
        pdf_error_set(e,PDF_ERROR_RESOURCE_LIMIT,ev->offset,"test-collector","event budget exceeded"); return 0;
    }
    if(t->len==t->cap) {
        size_t cap=t->cap?t->cap*2:16;
        if(cap<t->cap || cap>SIZE_MAX/sizeof(*t->items)) {
            pdf_error_set(e,PDF_ERROR_RESOURCE_LIMIT,ev->offset,"test-collector","capacity overflow"); return 0;
        }
        saved_event *items=realloc(t->items,cap*sizeof(*items));
        if(!items) { pdf_error_set(e,PDF_ERROR_OUT_OF_MEMORY,ev->offset,"test-collector","allocation failed"); return 0; }
        t->items=items; t->cap=cap;
    }
    unsigned char *bytes=malloc(ev->bytes.len?ev->bytes.len:1);
    unsigned char *font=malloc(ev->state.font.len?ev->state.font.len:1);
    if(!bytes || !font) {
        free(bytes); free(font);
        pdf_error_set(e,PDF_ERROR_OUT_OF_MEMORY,ev->offset,"test-collector","allocation failed"); return 0;
    }
    if(ev->bytes.len) memcpy(bytes,ev->bytes.data,ev->bytes.len);
    if(ev->state.font.len) memcpy(font,ev->state.font.data,ev->state.font.len);
    saved_event item={*ev,t->page,bytes,font};
    item.event.bytes.data=bytes; item.event.state.font.data=font;
    t->items[t->len++]=item; return 1;
}
static int read_trace(const char *path,trace *t,pdf_error *e) {
    pdf_document doc={0}; pdf_pages pages={0}; pdf_contents_result bytes={0};
    pdf_limits limits; pdf_limits_default(&limits);
    /* Event budget spans the document, independent of M7's per-array budget. */
    if(t->limit>limits.max_container_entries) t->limit=limits.max_container_entries;
    int ok=pdf_document_open(&doc,path,&limits,e) && pdf_pages_load(&doc,&pages,e);
    pdf_contents_context contents; pdf_contents_context_init(&contents,&doc);
    for(size_t i=0;ok && i<pages.len;i++) {
        t->page=i+1;
        ok=pdf_contents_read(&contents,&pages.items[i],&bytes,e) &&
           pdf_text_page_interpret(&pages.items[i],bytes.data,bytes.len,&limits,
              pdf_document_reference_offset(&doc,pages.items[i].reference),
              courier600,t,collect,t,e);
        pdf_contents_result_free(&bytes);
    }
    pdf_pages_free(&pages); pdf_document_close(&doc);
    if(!ok) clear_trace(t);
    return ok;
}
static trace new_trace(void) {
    trace t={0}; t.limit=t.abort_at=t.metrics_fail_at=SIZE_MAX; return t;
}
static int dump(const trace *t,pdf_error *e) {
    for(size_t i=0;i<t->len;i++)
        if(!pdf_text_event_dump(stdout,&t->items[i].event,t->items[i].page,e)) return 0;
    return 1;
}
static void near(double a,double b) { assert(fabs(a-b)<=1e-8+1e-9*fabs(b)); }
static void verify(void) {
    const char *paths[]={"tests/fixtures/geometry-raw.pdf","tests/fixtures/geometry-flate.pdf","tests/fixtures/geometry-array.pdf","tests/fixtures/geometry-pages.pdf"};
    const double x[]={36,46,46,51.76,61.96,46,46,36,36,36};
    const double y[]={250,226,208,208,208,194,172,110,76,40};
    const double advance[]={57.6,57.6,7.2,7.2,7.2,22.08,64.8,72,57.6,100.8};
    for(size_t k=0;k<4;k++) {
        trace t=new_trace(); pdf_error e; pdf_error_init(&e);
        assert(read_trace(paths[k],&t,&e)); assert(t.len==(k==3?20:10));
        for(size_t i=0;i<t.len;i++) {
            near(t.items[i].event.origin.x,x[i%10]); near(t.items[i].event.origin.y,y[i%10]);
            near(t.items[i].event.advance.x,advance[i%10]); near(t.items[i].event.advance.y,0);
            assert(t.items[i].event.source_order==i%10);
        }
        clear_trace(&t);
    }
    const char *bad[]={"tests/fixtures/geometry-later-failure.pdf","tests/fixtures/rotate-negative.pdf","tests/fixtures/rotate-360.pdf"};
    for(size_t i=0;i<3;i++) {
        trace t=new_trace(); pdf_error e; pdf_error_init(&e);
        assert(!read_trace(bad[i],&t,&e)); assert(t.len==0 && t.items==NULL);
        assert(e.code==(i?PDF_ERROR_UNSUPPORTED:PDF_ERROR_MALFORMED)); assert(e.offset>0);
        if(i) assert(strstr(e.message,"decoded")==NULL);
    }
    for(size_t k=0;k<3;k++) {
        trace t=new_trace(); pdf_error e; pdf_error_init(&e);
        if(k==0)t.limit=15; /* page 1 fits, page 2 crosses document budget */
        if(k==1)t.metrics_fail_at=12;
        if(k==2)t.abort_at=3;
        assert(!read_trace(paths[3],&t,&e)); assert(t.len==0 && t.items==NULL);
    }
    pdf_page page={0}; page.media_box_values[0]=-100; page.media_box_values[1]=-200;
    const char *text="/F1 12 Tf BT (A) Tj ET";
    trace t=new_trace(); pdf_error e; pdf_error_init(&e);
    assert(pdf_text_page_interpret(&page,(const unsigned char *)text,strlen(text),NULL,123,courier600,&t,collect,&t,&e));
    near(t.items[0].event.origin.x,0); near(t.items[0].event.origin.y,0); clear_trace(&t);
}
int main(int argc,char **argv) {
    if(argc==1) { verify(); return 0; }
    if(argc!=2) return 1;
    trace t=new_trace(); pdf_error e; pdf_error_init(&e);
    if(!read_trace(argv[1],&t,&e)) { pdf_error_print(&e,stderr); return pdf_error_exit_code(&e); }
    int ok=dump(&t,&e); clear_trace(&t);
    if(!ok) { pdf_error_print(&e,stderr); return pdf_error_exit_code(&e); }
    return ferror(stdout)?2:0;
}
