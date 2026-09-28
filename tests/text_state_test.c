#include "../src/text_state.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

static void near(double a, double b) { assert(fabs(a-b) <= 1e-8+1e-9*fabs(b)); }
static int run(pdf_text_state *s, const char *input, pdf_error *e) {
    pdf_content_result r;
    return pdf_content_interpret((const unsigned char *)input, strlen(input), NULL,
                                  123, pdf_text_state_visit, NULL, s, &r, e);
}
static void lifecycle(void) {
    pdf_error e; pdf_error_init(&e);
    pdf_text_state *s = pdf_text_state_create(NULL,NULL,NULL,NULL,NULL,&e);
    assert(s);
    pdf_text_snapshot v;
    assert(pdf_text_state_snapshot(s,&v));
    assert(!v.font_set); near(v.hscale,1); near(v.ctm.a,1);
    assert(run(s,"/F#001 12 Tf q /F2 20 Tf q /F3 30 Tf Q Q "
                 "2 0 0 2 0 0 cm 1 0 0 1 10 0 cm "
                 "BT 1 0 0 1 72 720 Tm 0 -18 TD T* ET BT ET",&e));
    assert(pdf_text_state_snapshot(s,&v));
    assert(v.font.len==3 && memcmp(v.font.data,"F\0" "1",3)==0);
    near(v.font_size,12); near(v.leading,18); near(v.text_matrix.e,0);
    near(v.line_matrix.f,0); near(v.ctm.e,20); assert(v.saved_depth==0);
    pdf_text_state_destroy(s);
    s=pdf_text_state_create(NULL,NULL,NULL,NULL,NULL,&e);
    assert(run(s,"BT 0 1 -1 0 100 200 Tm 10 0 Td ET",&e));
    assert(pdf_text_state_snapshot(s,&v)); near(v.text_matrix.e,100); near(v.text_matrix.f,210);
    pdf_text_state_destroy(s);
}
static pdf_text_event events[32];
static unsigned char strings[32][32];
static size_t count, metrics_calls;
static int width(void *ctx,pdf_text_bytes font,unsigned char code,double *out,pdf_error *e) {
    (void)ctx; (void)e; (void)code;
    assert(font.len); metrics_calls++; *out=500; return 1;
}
static int collect(void *ctx,const pdf_text_event *ev,pdf_error *e) {
    (void)ctx; (void)e; assert(count<32 && ev->bytes.len<32);
    events[count]=*ev;
    if(ev->bytes.len) memcpy(strings[count],ev->bytes.data,ev->bytes.len);
    count++; return 1;
}
static void geometry(void) {
    pdf_error e; pdf_error_init(&e); count=metrics_calls=0;
    pdf_text_state *s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F1 12 Tf 1 Tc 3 Tw 80 Tz 4 Ts 18 TL 3 Tr "
        "BT 1 0 0 1 72 720 Tm (A B) Tj [-120 (X) 120 () (Y)] TJ "
        "10 0 Td (Z) Tj (W) ' 2 .5 (A B) \" ET BT (N) Tj ET",&e));
    assert(count==8 && metrics_calls==11);
    near(events[0].origin.x,72); near(events[0].origin.y,724);
    near(events[0].advance.x,19.2); near(events[0].after.e,91.2);
    near(events[0].state.line_matrix.e,72); assert(events[0].state.rendering_mode==3);
    near(events[1].origin.x,92.352); assert(events[1].segment_index==1);
    near(events[2].advance.x,0); assert(events[2].bytes.len==0);
    near(events[3].origin.x,96.8); assert(events[3].segment_index==4);
    near(events[4].origin.x,82); near(events[5].origin.y,706);
    near(events[6].origin.y,688); near(events[6].advance.x,17.2);
    near(events[7].origin.x,0); near(events[7].origin.y,4);
    near(events[7].state.char_spacing,.5); near(events[7].state.word_spacing,2);
    for(size_t i=0;i<count;i++) assert(events[i].source_order==i);
    pdf_text_state_destroy(s);
}
static int bad_metrics(void *ctx,pdf_text_bytes font,unsigned char code,double *w,pdf_error *e) {
    (void)font; (void)e;
    int mode=*(int *)ctx;
    if(mode==1 && code=='B') return 0;
    *w=mode==2?INFINITY:500;
    if(mode==3) pdf_error_set(e,PDF_ERROR_RESOURCE_LIMIT,999,"metrics","budget exhausted");
    return 1;
}
static int abort_consumer(void *ctx,const pdf_text_event *ev,pdf_error *e) {
    (void)ctx; (void)ev; (void)e; return 0;
}
static void failures(void) {
    const char *bad[]={"BT () Tj ET","BT [] TJ ET","BT [10] TJ ET",
        "BT () ' ET","BT 0 0 () \" ET","/F 0 Tf","/F -1 Tf",
        "q /F 12 Tf Q BT () Tj ET"};
    for(size_t k=0;k<100;k++) for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        pdf_error e; pdf_error_init(&e);
        pdf_text_state *s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
        assert(!run(s,bad[i],&e)); assert(e.code==PDF_ERROR_MALFORMED && e.offset==123);
        assert(strstr(e.message,"decoded byte"));
        pdf_text_state_destroy(s);
    }
    pdf_error e; pdf_error_init(&e); count=0;
    pdf_text_state *s=pdf_text_state_create(NULL,NULL,NULL,collect,NULL,&e);
    assert(run(s,"/F 12 Tf BT () Tj [120 -120] TJ [] TJ ET",&e));
    assert(count==1);
    assert(!run(s,"BT (X) Tj ET",&e)); assert(e.code==PDF_ERROR_UNSUPPORTED);
    pdf_text_state_destroy(s);
    for(int mode=1;mode<=3;mode++) {
        pdf_error_clear(&e); count=0;
        s=pdf_text_state_create(NULL,bad_metrics,&mode,collect,NULL,&e);
        assert(!run(s,"/F 12 Tf BT (A) Tj (AB) Tj ET",&e));
        assert(count==(mode==1?1:0));
        assert(e.code==(mode==1?PDF_ERROR_UNSUPPORTED:mode==2?PDF_ERROR_MALFORMED:PDF_ERROR_RESOURCE_LIMIT));
        pdf_text_state_destroy(s);
    }
    pdf_error_clear(&e);
    s=pdf_text_state_create(NULL,width,NULL,abort_consumer,NULL,&e);
    assert(!run(s,"q /F 12 Tf BT (A) Tj ET Q",&e)); assert(e.code==PDF_ERROR_MALFORMED);
    pdf_text_state_destroy(s);
    pdf_limits limits; pdf_limits_default(&limits); limits.max_nesting_depth=1;
    pdf_error_clear(&e); s=pdf_text_state_create(&limits,NULL,NULL,NULL,NULL,&e);
    assert(!run(s,"/F 12 Tf q q Q Q",&e)); assert(e.code==PDF_ERROR_RESOURCE_LIMIT);
    pdf_text_state_destroy(s);
    /* M7 guarantees numeric types. Feed finite extreme values at that seam to
       isolate arithmetic overflow from lexical number limits. */
    pdf_object huge={.type=PDF_OBJECT_REAL,.value.real=1e308};
    pdf_object zero={.type=PDF_OBJECT_INT,.value.integer=0};
    const pdf_object *operands[]={&huge,&zero,&zero,&huge,&zero,&zero};
    pdf_content_operation op={PDF_CONTENT_CM,55,operands,6};
    pdf_error_clear(&e); s=pdf_text_state_create(NULL,NULL,NULL,NULL,NULL,&e);
    assert(pdf_text_state_visit(s,&op,&e)); assert(!pdf_text_state_visit(s,&op,&e));
    assert(e.code==PDF_ERROR_MALFORMED && e.offset==55);
    pdf_text_state_destroy(s);
    pdf_error_clear(&e); s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F 12 Tf",&e));
    op=(pdf_content_operation){PDF_CONTENT_TZ,66,operands,1};
    assert(pdf_text_state_visit(s,&op,&e));
    op.kind=PDF_CONTENT_TF;
    pdf_object name={.type=PDF_OBJECT_NAME,.value.name={(unsigned char *)"F",1}};
    const pdf_object *tf[]={&name,&huge}; op.operands=tf; op.operand_count=2;
    assert(pdf_text_state_visit(s,&op,&e));
    assert(!run(s,"BT (X) Tj ET",&e)); assert(e.code==PDF_ERROR_MALFORMED);
    pdf_text_state_destroy(s);
}
static void transforms_and_save(void) {
    pdf_error e; pdf_error_init(&e); count=0;
    pdf_text_state *s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F1 12 Tf 1 Tc 3 Tw 80 Tz 18 TL 4 Ts 3 Tr q "
        "/F2 20 Tf 0 Tc 0 Tw -100 Tz 30 TL 0 Ts 0 Tr "
        "2 0 0 2 0 0 cm BT 0 1 -1 0 100 200 Tm (A) Tj ET Q "
        "BT (A) Tj ET",&e));
    near(events[0].advance.x,0); near(events[0].advance.y,-20);
    near(events[1].advance.x,5.6); near(events[1].origin.y,4);
    near(events[1].state.font_size,12); near(events[1].state.leading,18);
    near(events[1].state.word_spacing,3); assert(events[1].state.rendering_mode==3);
    pdf_text_state_destroy(s);
    pdf_error_clear(&e); count=0;
    s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F 12 Tf 0 Tz BT (A) Tj ET",&e)); near(events[0].advance.x,0);
    pdf_text_state_destroy(s);
}
static void raw_codes_and_boundaries(void) {
    pdf_error e; pdf_error_init(&e); count=metrics_calls=0;
    pdf_text_state *s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F1 12 Tf 3 Tw q q q q q q q q q q "
        "BT 1 2 3 4 5 6 Tm <0020ff> Tj ET Q Q Q Q Q Q Q Q Q Q",&e));
    assert(count==1 && metrics_calls==3 && memcmp(strings[0],"\0 \xff",3)==0);
    near(events[0].advance.x,21); near(events[0].advance.y,42);
    pdf_text_snapshot v; assert(pdf_text_state_snapshot(s,&v)); assert(v.saved_depth==0);
    pdf_text_state_destroy(s);
    pdf_error_clear(&e); s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    pdf_object name={.type=PDF_OBJECT_NAME,.value.name={NULL,SIZE_MAX}};
    pdf_object size={.type=PDF_OBJECT_INT,.value.integer=12};
    const pdf_object *tf[]={&name,&size};
    pdf_content_operation op={PDF_CONTENT_TF,99,tf,2};
    assert(!pdf_text_state_visit(s,&op,&e)); assert(e.code==PDF_ERROR_RESOURCE_LIMIT);
    pdf_text_state_destroy(s);
    pdf_error_clear(&e); s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F1 12 Tf",&e));
    pdf_object huge={.type=PDF_OBJECT_REAL,.value.real=1e308};
    const pdf_object *tc[]={&huge}; op=(pdf_content_operation){PDF_CONTENT_TC,99,tc,1};
    assert(pdf_text_state_visit(s,&op,&e));
    count=0;
    assert(!run(s,"BT (AA) Tj ET",&e)); assert(e.code==PDF_ERROR_MALFORMED && count==0);
    pdf_text_state_destroy(s);
    pdf_error_clear(&e); s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    huge.value.real=NAN; op=(pdf_content_operation){PDF_CONTENT_TC,99,tc,1};
    assert(!pdf_text_state_visit(s,&op,&e)); assert(e.code==PDF_ERROR_MALFORMED);
    pdf_text_state_destroy(s);
}
static void debug_dump(void) {
    pdf_error e; pdf_error_init(&e); count=0;
    pdf_text_state *s=pdf_text_state_create(NULL,width,NULL,collect,NULL,&e);
    assert(run(s,"/F#001 12 Tf q BT 1 0 0 1 30 40 Tm (A) Tj ET Q",&e));
    pdf_text_snapshot v; assert(pdf_text_state_snapshot(s,&v));
    near(v.text_matrix.e,36); near(v.line_matrix.e,30);
    /* Collector copied event scalars; supply owned byte spans for dump. */
    events[0].bytes=(pdf_text_bytes){strings[0],1};
    events[0].state.font=(pdf_text_bytes){(const unsigned char *)"F\0" "1",3};
    FILE *f=tmpfile(); assert(f);
    assert(pdf_text_event_dump(f,&events[0],1,&e));
    rewind(f); char text[2048]; size_t n=fread(text,1,sizeof(text)-1,f); text[n]=0;
    assert(strstr(text,"\"font_len\":3,\"font\":\"460031\""));
    assert(strstr(text,"\"origin\":[30.000000000,40.000000000]"));
    assert(fclose(f)==0); pdf_text_state_destroy(s);
}
int main(void) { lifecycle(); geometry(); failures(); transforms_and_save(); raw_codes_and_boundaries(); debug_dump(); return 0; }
