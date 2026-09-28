/* Read-only evidence collector using the project's real M6/M7 interfaces.
 * It visits page 1 only. Hex payloads are authoritative; ASCII previews are
 * convenience views for bytes 0x20..0x7e, not font/Unicode decoding. */
#include "contents.h"
#include "content_interpreter.h"
#include <stdio.h>

static const char *names[] = {"BT", "ET", "Tf", "Tm", "Td", "TD", "T*",
    "Tj", "TJ", "'", "\"", "q", "Q", "cm"};
static void bytes(const pdf_bytes *b) {
    for (size_t i=0;i<b->len;i++) printf("%02X",b->data[i]);
}
static void operand(const pdf_object *o) {
    switch (o->type) {
    case PDF_OBJECT_INT: printf("%lld",(long long)o->value.integer); break;
    case PDF_OBJECT_REAL: printf("%.17g",o->value.real); break;
    case PDF_OBJECT_NAME: printf("NAME_HEX=");bytes(&o->value.name);break;
    case PDF_OBJECT_STRING:
    case PDF_OBJECT_HEX_STRING: {
        const pdf_bytes *b=o->type==PDF_OBJECT_STRING?&o->value.string:&o->value.hex_string;
        printf("STRING_HEX=");bytes(b);break;
    }
    case PDF_OBJECT_ARRAY:
        putchar('[');
        for(size_t i=0;i<o->value.array.len;i++) { if(i) putchar(' ');operand(o->value.array.items[i]); }
        putchar(']');break;
    default: printf("TYPE=%d",o->type);break;
    }
}
static void strings(const pdf_object *o) {
    if(o->type==PDF_OBJECT_STRING || o->type==PDF_OBJECT_HEX_STRING) {
        const pdf_bytes *b=o->type==PDF_OBJECT_STRING?&o->value.string:&o->value.hex_string;
        printf("RAW_STRING_HEX ");bytes(b);putchar('\n');
        printf("ASCII_PREVIEW ");
        for(size_t i=0;i<b->len;i++) {
            unsigned char c=b->data[i];
            if(c>=32 && c<=126) putchar(c);else printf("\\x%02X",c);
        }
        putchar('\n');
    } else if(o->type==PDF_OBJECT_ARRAY) {
        for(size_t i=0;i<o->value.array.len;i++) strings(o->value.array.items[i]);
    }
}
static int visit(void *ctx,const pdf_content_operation *op,pdf_error *error) {
    (void)ctx;(void)error;
    printf("OP decoded=%zu %s",op->offset,names[op->kind]);
    for(size_t i=0;i<op->operand_count;i++) {putchar(' ');operand(op->operands[i]);}
    putchar('\n');
    for(size_t i=0;i<op->operand_count;i++) strings(op->operands[i]);
    return 1;
}
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    pdf_error error;pdf_error_init(&error);
    pdf_document doc;pdf_pages pages={0};pdf_contents_result decoded={0};
    if(!pdf_document_open(&doc,argv[1],NULL,&error)) {pdf_error_print(&error,stderr);return pdf_error_exit_code(&error);}
    int ok=pdf_pages_load(&doc,&pages,&error);
    if(ok && pages.len) {
        pdf_contents_context ctx;pdf_contents_context_init(&ctx,&doc);
        ok=pdf_contents_read(&ctx,&pages.items[0],&decoded,&error);
        if(ok) {
            printf("PAGE 1 DECODED_BYTES %zu\nDECODED_PREFIX ",decoded.len);
            for(size_t i=0;i<decoded.len && i<160;i++) {
                unsigned char c=decoded.data[i];
                if(c>=32 && c<=126) putchar(c);else printf("\\x%02X",c);
            }
            putchar('\n');
            pdf_content_result result;
            size_t offset=pdf_document_reference_offset(&doc,pages.items[0].reference);
            ok=pdf_content_interpret(decoded.data,decoded.len,&doc.reader.limits,offset,visit,NULL,NULL,&result,&error);
            if(ok) printf("PAGE 1 OPS %zu TEXT_SHOWS %zu\n",result.operations,result.text_shows);
        }
    }
    pdf_contents_result_free(&decoded);pdf_pages_free(&pages);pdf_document_close(&doc);
    if(!ok) pdf_error_print(&error,stderr);
    return ok?0:pdf_error_exit_code(&error);
}
