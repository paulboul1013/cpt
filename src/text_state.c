#define _POSIX_C_SOURCE 200809L
#include "text_state.h"
#include <math.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t refs, len;
    unsigned char data[];
} font_name;
typedef struct {
    font_name *font;
    double size, character, word, scale, leading, rise;
    int mode;
    pdf_matrix ctm;
} graphics_state;
struct pdf_text_state {
    graphics_state g, *stack;
    size_t depth, capacity, order, offset;
    pdf_matrix tm, lm;
    pdf_limits limits;
    pdf_text_metrics metrics;
    void *metrics_context;
    pdf_text_consumer consumer;
    void *consumer_context;
    int failed;
};
static void release(font_name *name) { if (name && --name->refs == 0) free(name); }
static int fail(pdf_text_state *s, pdf_error *e, pdf_error_code code, const char *why) {
    s->failed=1;
    pdf_error_set(e,code,s->offset,"text-state","%s",why);
    return 0;
}
static int numeric_failure(pdf_text_state *s, pdf_error *e) {
    return fail(s,e,PDF_ERROR_MALFORMED,"non-finite text geometry");
}
pdf_text_state *pdf_text_state_create(const pdf_limits *limits, pdf_text_metrics metrics,
    void *mc, pdf_text_consumer consumer, void *cc, pdf_error *e) {
    if (!e || e->code != PDF_ERROR_NONE) return NULL;
    pdf_text_state *s=calloc(1,sizeof(*s));
    if (!s) { pdf_error_set(e,PDF_ERROR_OUT_OF_MEMORY,0,"text-state","cannot allocate state"); return NULL; }
    if (limits) s->limits=*limits; else pdf_limits_default(&s->limits);
    s->g.scale=1;
    s->g.ctm=s->tm=s->lm=pdf_matrix_identity();
    s->metrics=metrics; s->metrics_context=mc;
    s->consumer=consumer; s->consumer_context=cc;
    return s;
}
void pdf_text_state_destroy(pdf_text_state *s) {
    if (!s) return;
    release(s->g.font);
    for (size_t i=0;i<s->depth;i++) release(s->stack[i].font);
    free(s->stack); free(s);
}
int pdf_text_state_snapshot(const pdf_text_state *s, pdf_text_snapshot *v) {
    if (!s || !v || s->failed) return 0;
    *v=(pdf_text_snapshot){0};
    v->font_set=s->g.font != NULL;
    if (s->g.font) v->font=(pdf_text_bytes){s->g.font->data,s->g.font->len};
    v->font_size=s->g.size; v->char_spacing=s->g.character; v->word_spacing=s->g.word;
    v->hscale=s->g.scale; v->leading=s->g.leading; v->rise=s->g.rise;
    v->rendering_mode=s->g.mode; v->ctm=s->g.ctm;
    v->text_matrix=s->tm; v->line_matrix=s->lm; v->saved_depth=s->depth;
    return 1;
}
static int save(pdf_text_state *s,pdf_error *e) {
    if (s->depth>=s->limits.max_nesting_depth || s->depth==SIZE_MAX ||
        (s->g.font && s->g.font->refs==SIZE_MAX))
        return fail(s,e,PDF_ERROR_RESOURCE_LIMIT,"q stack or font retain limit");
    if (s->depth==s->capacity) {
        size_t cap=s->capacity ? s->capacity : 4;
        if (s->capacity) {
            if (cap>SIZE_MAX/2) return fail(s,e,PDF_ERROR_RESOURCE_LIMIT,"q capacity overflow");
            cap*=2;
        }
        if (cap>s->limits.max_nesting_depth) cap=s->limits.max_nesting_depth;
        if (cap>SIZE_MAX/sizeof(*s->stack)) return fail(s,e,PDF_ERROR_RESOURCE_LIMIT,"q capacity overflow");
        graphics_state *stack=realloc(s->stack,cap*sizeof(*stack));
        if (!stack) return fail(s,e,PDF_ERROR_OUT_OF_MEMORY,"cannot grow q stack");
        s->stack=stack; s->capacity=cap;
    }
    if (s->g.font) s->g.font->refs++;
    s->stack[s->depth++]=s->g;
    return 1;
}
static int set_font(pdf_text_state *s, const pdf_bytes *name, double size,pdf_error *e) {
    if (size<=0) return fail(s,e,PDF_ERROR_MALFORMED,"font size must be positive");
    if (name->len>SIZE_MAX-sizeof(font_name)) return fail(s,e,PDF_ERROR_RESOURCE_LIMIT,"font name capacity overflow");
    font_name *f=malloc(sizeof(*f)+name->len);
    if (!f) return fail(s,e,PDF_ERROR_OUT_OF_MEMORY,"cannot copy font name");
    f->refs=1; f->len=name->len;
    if (name->len) memcpy(f->data,name->data,name->len);
    release(s->g.font); s->g.font=f; s->g.size=size;
    return 1;
}
static int move_line(pdf_text_state *s,double x,double y,pdf_error *e) {
    if (!pdf_matrix_compose((pdf_matrix){1,0,0,1,x,y},s->lm,&s->lm)) return numeric_failure(s,e);
    s->tm=s->lm;
    return 1;
}
static int advance_text(pdf_text_state *s,double dx,pdf_error *e) {
    if (!pdf_matrix_compose((pdf_matrix){1,0,0,1,dx,0},s->tm,&s->tm)) return numeric_failure(s,e);
    return 1;
}
static int show_string(pdf_text_state *s,const pdf_object *o,size_t segment,pdf_error *e) {
    const pdf_bytes *bytes=o->type==PDF_OBJECT_STRING ? &o->value.string : &o->value.hex_string;
    if (bytes->len && !s->metrics) return fail(s,e,PDF_ERROR_UNSUPPORTED,"unsupported font metrics");
    if (s->order==SIZE_MAX) return fail(s,e,PDF_ERROR_RESOURCE_LIMIT,"event source order overflow");
    pdf_text_event event={0};
    pdf_text_state_snapshot(s,&event.state);
    event.bytes=(pdf_text_bytes){bytes->data,bytes->len};
    event.offset=s->offset; event.segment_index=segment; event.source_order=s->order;
    pdf_matrix basis;
    pdf_matrix font={s->g.size*s->g.scale,0,0,s->g.size,0,s->g.rise};
    if (!pdf_matrix_compose(s->tm,s->g.ctm,&basis) ||
        !pdf_matrix_compose(font,basis,&event.rendering_matrix) ||
        !pdf_matrix_point(event.rendering_matrix,(pdf_point){0,0},&event.origin))
        return numeric_failure(s,e);
    double total=0;
    for (size_t i=0;i<bytes->len;i++) {
        double w=NAN;
        int ok=s->metrics(s->metrics_context,event.state.font,bytes->data[i],&w,e);
        if (!ok || e->code!=PDF_ERROR_NONE) return fail(s,e,PDF_ERROR_UNSUPPORTED,"unsupported font metrics");
        double dx=(w/1000*s->g.size+s->g.character+(bytes->data[i]==0x20?s->g.word:0))*s->g.scale;
        if (!isfinite(w) || !isfinite(dx) || !isfinite(total+dx)) return numeric_failure(s,e);
        total+=dx;
        if (!advance_text(s,dx,e)) return 0;
    }
    event.after=s->tm;
    if (!pdf_matrix_vector(basis,(pdf_point){total,0},&event.advance)) return numeric_failure(s,e);
    if (s->consumer) {
        int ok=s->consumer(s->consumer_context,&event,e);
        if (!ok || e->code!=PDF_ERROR_NONE) return fail(s,e,PDF_ERROR_MALFORMED,"text consumer aborted");
    }
    s->order++;
    return 1;
}
static int show(pdf_text_state *s,const pdf_content_operation *op,const double *n,pdf_error *e) {
    if (!s->g.font) return fail(s,e,PDF_ERROR_MALFORMED,"text show requires Tf");
    if (op->kind==PDF_CONTENT_DOUBLE_QUOTE) { s->g.word=n[0]; s->g.character=n[1]; }
    if ((op->kind==PDF_CONTENT_QUOTE || op->kind==PDF_CONTENT_DOUBLE_QUOTE) &&
        !move_line(s,0,-s->g.leading,e)) return 0;
    if (op->kind!=PDF_CONTENT_TJ_ARRAY)
        return show_string(s,op->operands[op->kind==PDF_CONTENT_DOUBLE_QUOTE?2:0],0,e);
    const pdf_array *a=&op->operands[0]->value.array;
    for (size_t i=0;i<a->len;i++) {
        const pdf_object *o=a->items[i];
        if (o->type==PDF_OBJECT_INT || o->type==PDF_OBJECT_REAL) {
            double adjustment=o->type==PDF_OBJECT_INT?(double)o->value.integer:o->value.real;
            double dx=-adjustment/1000*s->g.size*s->g.scale;
            if (!isfinite(adjustment) || !isfinite(dx)) return numeric_failure(s,e);
            if (!advance_text(s,dx,e)) return 0;
        } else if (!show_string(s,o,i,e)) return 0;
    }
    return 1;
}
int pdf_text_state_visit(void *context,const pdf_content_operation *op,pdf_error *e) {
    pdf_text_state *s=context;
    if (!s || !op || !e) return 0;
    if (s->failed || e->code != PDF_ERROR_NONE) return fail(s,e,PDF_ERROR_MALFORMED,"state is unusable after failure");
    s->offset=op->offset;
    double n[6]={0};
    for (size_t i=0;i<op->operand_count && i<6;i++) {
        const pdf_object *o=op->operands[i];
        if (o->type==PDF_OBJECT_INT) n[i]=(double)o->value.integer;
        if (o->type==PDF_OBJECT_REAL) n[i]=o->value.real;
        if (!isfinite(n[i])) return numeric_failure(s,e);
    }
    pdf_matrix m={n[0],n[1],n[2],n[3],n[4],n[5]};
    switch(op->kind) {
        case PDF_CONTENT_BT: s->tm=s->lm=pdf_matrix_identity(); return 1;
        case PDF_CONTENT_ET: return 1;
        case PDF_CONTENT_TC: s->g.character=n[0]; return 1;
        case PDF_CONTENT_TW: s->g.word=n[0]; return 1;
        case PDF_CONTENT_TZ: s->g.scale=n[0]/100; return 1;
        case PDF_CONTENT_TL: s->g.leading=n[0]; return 1;
        case PDF_CONTENT_TS: s->g.rise=n[0]; return 1;
        case PDF_CONTENT_TR: s->g.mode=(int)n[0]; return 1;
        case PDF_CONTENT_TF: return set_font(s,&op->operands[0]->value.name,n[1],e);
        case PDF_CONTENT_SAVE: return save(s,e);
        case PDF_CONTENT_RESTORE:
            if (!s->depth) return fail(s,e,PDF_ERROR_MALFORMED,"Q without q");
            release(s->g.font); s->g=s->stack[--s->depth]; return 1;
        case PDF_CONTENT_CM:
            return pdf_matrix_compose(m,s->g.ctm,&s->g.ctm) ? 1 : numeric_failure(s,e);
        case PDF_CONTENT_TM: s->tm=s->lm=m; return 1;
        case PDF_CONTENT_TD_LEADING: s->g.leading=-n[1]; /* fall through */
        case PDF_CONTENT_TD: return move_line(s,n[0],n[1],e);
        case PDF_CONTENT_NEXT_LINE: return move_line(s,0,-s->g.leading,e);
        case PDF_CONTENT_TJ: case PDF_CONTENT_TJ_ARRAY: case PDF_CONTENT_QUOTE:
        case PDF_CONTENT_DOUBLE_QUOTE: return show(s,op,n,e);
        default: return fail(s,e,PDF_ERROR_UNSUPPORTED,"text operation not yet implemented");
    }
}

int pdf_text_page_interpret(const pdf_page *page,const unsigned char *data,size_t len,
    const pdf_limits *limits,size_t page_offset,pdf_text_metrics metrics,void *mc,
    pdf_text_consumer consumer,void *cc,pdf_error *e) {
    if (!e || e->code!=PDF_ERROR_NONE) return 0;
    if (!page) { pdf_error_set(e,PDF_ERROR_IO,page_offset,"text-state","missing page"); return 0; }
    if (page->rotation != 0) {
        pdf_error_set(e,PDF_ERROR_UNSUPPORTED,page_offset,"text-state","unsupported page rotation");
        return 0;
    }
    pdf_text_state *s=pdf_text_state_create(limits,metrics,mc,consumer,cc,e);
    if (!s) return 0;
    pdf_content_result result;
    int ok=pdf_content_interpret(data,len,limits,page_offset,pdf_text_state_visit,NULL,s,&result,e);
    pdf_text_state_destroy(s);
    return ok;
}

static void dump_hex(FILE *stream,pdf_text_bytes bytes) {
    fputc('"',stream);
    for(size_t i=0;i<bytes.len;i++) fprintf(stream,"%02x",bytes.data[i]);
    fputc('"',stream);
}
static void dump_matrix(FILE *stream,pdf_matrix m) {
    fprintf(stream,"[%.9f,%.9f,%.9f,%.9f,%.9f,%.9f]",m.a,m.b,m.c,m.d,m.e,m.f);
}
int pdf_text_event_dump(FILE *stream,const pdf_text_event *v,size_t page,pdf_error *e) {
    if(!e || e->code!=PDF_ERROR_NONE) return 0;
    if(!stream || !v) {
        pdf_error_set(e,PDF_ERROR_IO,0,"text-debug","invalid dump input"); return 0;
    }
    locale_t numeric=newlocale(LC_NUMERIC_MASK,"C",(locale_t)0);
    if(!numeric) {
        pdf_error_set(e,PDF_ERROR_OUT_OF_MEMORY,v->offset,"text-debug","cannot allocate numeric locale"); return 0;
    }
    locale_t previous=uselocale(numeric);
    if(!previous) {
        freelocale(numeric);
        pdf_error_set(e,PDF_ERROR_IO,v->offset,"text-debug","cannot select numeric locale"); return 0;
    }
        fprintf(stream,"{\"page\":%zu,\"order\":%zu,\"offset\":%zu,\"segment\":%zu,\"bytes\":",page,v->source_order,v->offset,v->segment_index);
        dump_hex(stream,v->bytes); fprintf(stream,",\"font_len\":%zu,\"font\":",v->state.font.len); dump_hex(stream,v->state.font);
        fprintf(stream,",\"size\":%.9f,\"Tc\":%.9f,\"Tw\":%.9f,\"scale\":%.9f,\"leading\":%.9f,\"rise\":%.9f,\"mode\":%d",
            v->state.font_size,v->state.char_spacing,v->state.word_spacing,v->state.hscale,v->state.leading,v->state.rise,v->state.rendering_mode);
        fprintf(stream,",\"origin\":[%.9f,%.9f],\"advance\":[%.9f,%.9f],\"rendering\":",v->origin.x,v->origin.y,v->advance.x,v->advance.y);
        dump_matrix(stream,v->rendering_matrix); fprintf(stream,",\"before\":"); dump_matrix(stream,v->state.text_matrix);
        fprintf(stream,",\"after\":"); dump_matrix(stream,v->after); fprintf(stream,",\"line\":"); dump_matrix(stream,v->state.line_matrix);
        fprintf(stream,",\"ctm\":"); dump_matrix(stream,v->state.ctm); fputs("}\n",stream);
    uselocale(previous);
    freelocale(numeric);
    if(ferror(stream)) {
        pdf_error_set(e,PDF_ERROR_IO,v->offset,"text-debug","cannot write diagnostic trace"); return 0;
    }
    return 1;
}
