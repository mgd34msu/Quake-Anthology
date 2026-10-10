#include "value_internal.h"
#include "frame_internal.h"
#include "qa/unified_frame_events.h"
#include "qa/unified_frame_metadata.h"

#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define VALUE_LIMIT (32u*1024u*1024u)
#define FRAME_LIMIT (4u*1024u*1024u)
_Static_assert(sizeof(double) == sizeof(uint64_t) && FLT_RADIX == 2 && DBL_MANT_DIG == 53,
    "Unified frame values require binary64 numbers");

struct qa_unified_document {
    size_t references;
    qa_unified_frame_lease *lease;
    qa_unified_document_kind kind;
    qa_buffer source;
    qa_json_document *json;
    qa_unified_frame *frame;
    qa_unified_input_batch *inputs;
    qa_unified_frame_events *events;
    qa_unified_frame_metadata *metadata;
    qa_unified_document *metadata_owners[4];
    bool metadata_borrowed;
    qa_unified_handshake handshake;
    qa_unified_control *control;
    size_t bytes;
};

static bool typed_encode(const qa_unified_document *, qa_buffer *, qa_error *);
static bool typed_decode(qa_unified_document_kind, qa_bytes, qa_strings *, qa_unified_frame_lease *, qa_event_transaction *, qa_unified_document **, qa_error *);

static bool bad(qa_error *error, const char *message) {
    qa_error_set(error,QA_ERROR_FORMAT,0,"%s",message); return false;
}

static int base64_digit(uint8_t c) {
    if (c>='A' && c<='Z') return (int)(c-'A');
    if (c>='a' && c<='z') return (int)(c-'a')+26;
    if (c>='0' && c<='9') return (int)(c-'0')+52;
    return c=='+'?62:c=='/'?63:-1;
}

static bool valid_base64(qa_bytes value, size_t *decoded, qa_error *error) {
    if (value.size%4) return bad(error,"noncanonical checkpoint base64 length");
    size_t padding=0;
    if (value.size && value.data[value.size-1]=='=') padding++;
    if (value.size>1 && value.data[value.size-2]=='=') padding++;
    unsigned last=0;
    for (size_t i=0;i<value.size-padding;++i) {
        int digit=base64_digit(value.data[i]);
        if (digit<0) return bad(error,"invalid checkpoint base64 digit");
        last=(unsigned)digit;
    }
    if ((padding==2 && (last&15u)) || (padding==1 && (last&3u)))
        return bad(error,"noncanonical checkpoint base64 padding");
    if (decoded) *decoded=(value.size/4)*3-padding;
    return true;
}

static bool valid_bigint(qa_bytes value) {
    size_t start=value.size && value.data[0]=='-'?1:0;
    if (start==value.size || (value.data[start]=='0' && value.size-start!=1)) return false;
    for (size_t i=start;i<value.size;++i) if (value.data[i]<'0' || value.data[i]>'9') return false;
    return true;
}

static bool text_is(qa_buffer text, const char *expected) {
    return text.size==strlen(expected) && !memcmp(text.data,expected,text.size);
}

bool qa_unified_tag_check(const qa_json_document *d, qa_json_id id, unsigned depth, qa_error *error) {
    if (depth>128) return bad(error,"checkpoint nesting exceeds limit");
    qa_json_kind kind=qa_json_type(d,id);
    if (kind!=QA_JSON_OBJECT && kind!=QA_JSON_ARRAY) return kind!=QA_JSON_INVALID;
    qa_json_id tag=kind==QA_JSON_OBJECT?qa_json_get(d,id,"$qts"):QA_JSON_NONE;
    if (tag!=QA_JSON_NONE) {
        qa_buffer value={0};
        if (qa_json_size(d,id)!=2 || !qa_json_string(d,qa_json_get(d,id,"value"),&value,error))
            return bad(error,"invalid tagged checkpoint record");
        bool ok;
        if (qa_json_string_equal(d,tag,"bytes")) ok=valid_base64((qa_bytes){value.data,value.size},NULL,error);
        else if (qa_json_string_equal(d,tag,"bigint")) ok=valid_bigint((qa_bytes){value.data,value.size});
        else if (qa_json_string_equal(d,tag,"number"))
            ok=text_is(value,"-0") || text_is(value,"NaN") || text_is(value,"Infinity") || text_is(value,"-Infinity");
        else ok=false;
        qa_buffer_free(&value);
        return ok?true:bad(error,"invalid tagged checkpoint value");
    }
    size_t count=qa_json_size(d,id);
    for (size_t i=0;i<count;++i)
        if (!qa_unified_tag_check(d,qa_json_at(d,id,i),depth+1,error)) return false;
    return true;
}

static size_t wire_limit(qa_unified_document_kind kind) {
    switch (kind) {
    case QA_UNIFIED_HANDSHAKE_DOCUMENT: return 512;
    case QA_UNIFIED_INPUT_DOCUMENT: return 65536;
    case QA_UNIFIED_FRAME_DOCUMENT: return VALUE_LIMIT;
    case QA_UNIFIED_CONTROL_DOCUMENT: return FRAME_LIMIT;
    case QA_UNIFIED_CHECKPOINT: return VALUE_LIMIT;
    }
    return 0;
}

static bool create_document(qa_unified_document_kind kind, qa_buffer source,
                              qa_unified_document **out, qa_error *error) {
    qa_unified_document *d=calloc(1,sizeof(*d));
    if (!d) { qa_buffer_free(&source); qa_error_set(error,QA_ERROR_MEMORY,0,"allocating unified document"); return false; }
    d->references=1; d->kind=kind; d->source=source; d->bytes=source.size;
    if (!qa_json_parse((qa_bytes){d->source.data,d->source.size},&d->json,error) ||
        !qa_unified_tag_check(d->json,qa_json_root(d->json),0,error) ||
        !qa_unified_schema_check(kind,d->json,error)) {
        qa_unified_document_destroy(d); return false;
    }
    *out=d; return true;
}

bool qa_unified_document_create(qa_unified_document_kind kind, qa_bytes bytes,
                                 qa_unified_document **out, qa_error *error) {
    if (kind==QA_UNIFIED_FRAME_DOCUMENT || kind==QA_UNIFIED_INPUT_DOCUMENT || kind==QA_UNIFIED_HANDSHAKE_DOCUMENT)
        return bad(error,"Typed Source records require their actual record constructor");
    size_t maximum=wire_limit(kind);
    if (!out || !maximum || !bytes.data || !bytes.size || bytes.size>maximum)
        return bad(error,"invalid unified document kind or byte extent");
    qa_buffer source={.data=malloc(bytes.size),.size=bytes.size};
    if (!source.data) { qa_error_set(error,QA_ERROR_MEMORY,0,"copying unified document"); return false; }
    memcpy(source.data,bytes.data,bytes.size);
    return create_document(kind,source,out,error);
}

bool qa_unified_document_decode(qa_unified_document_kind kind, qa_bytes bytes, qa_strings *strings, qa_unified_frame_lease *lease, qa_event_transaction *transaction,
                                 qa_unified_document **out, qa_error *error) {
    size_t maximum=wire_limit(kind);
    if (!out || !maximum || !bytes.data || !bytes.size || bytes.size>maximum)
        return bad(error,"invalid unified document kind or byte extent");
    if (kind==QA_UNIFIED_FRAME_DOCUMENT) return qa_unified_frame_decode(bytes,NULL,0,NULL,strings,out,error);
    if (kind==QA_UNIFIED_HANDSHAKE_DOCUMENT || kind==QA_UNIFIED_INPUT_DOCUMENT || (kind==QA_UNIFIED_CONTROL_DOCUMENT && bytes.size>=4 &&
        (!memcmp(bytes.data,"QUEV",4) || !memcmp(bytes.data,"QUMD",4) || !memcmp(bytes.data,"QUCT",4))))
        return typed_decode(kind,bytes,strings,lease,transaction,out,error);
    return qa_unified_document_create(kind,bytes,out,error);
}

bool qa_unified_document_validate(const qa_unified_document *d,
                                   qa_unified_document_validator validator,
                                   void *context, qa_error *error) {
    if (!d || !validator) return bad(error,"unified publication requires an owner validator");
    return validator(context,d,error);
}

bool qa_unified_document_encode(const qa_unified_document *d, qa_buffer *out, qa_error *error) {
    if (!d || !out) return bad(error,"invalid unified document output");
    if (d->kind==QA_UNIFIED_HANDSHAKE_DOCUMENT || d->inputs || d->events || d->metadata || d->control) return typed_encode(d,out,error);
    if (d->kind!=QA_UNIFIED_FRAME_DOCUMENT) {
        uint8_t *data=malloc(d->source.size?d->source.size:1);
        if (!data) { qa_error_set(error,QA_ERROR_MEMORY,0,"copying unified document"); return false; }
        if (d->source.size) memcpy(data,d->source.data,d->source.size);
        *out=(qa_buffer){data,d->source.size}; return true;
    }
    qa_unified_builder builder={0};
    if (!qa_unified_frame_write(d,NULL,0,VALUE_LIMIT,&builder,error)) { free(builder.data); return false; }
    *out=(qa_buffer){builder.data,builder.size}; return true;
}

bool qa_unified_document_retain(const qa_unified_document *source,
                                  qa_unified_document **out, qa_error *error) {
    if (!source || !out) return bad(error,"invalid unified document retention");
    qa_unified_document *d=(qa_unified_document *)source;
    if (d->references==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"unified document reference capacity exhausted"); return false;
    }
    ++d->references; *out=d; return true;
}

void qa_unified_document_destroy(qa_unified_document *d) {
    if (!d || --d->references) return;
    for (size_t i=0;i<4;++i) qa_unified_document_destroy(d->metadata_owners[i]);
    if (d->lease) { qa_unified_frame_lease_release(d->lease); return; }
    if (d->frame && d->frame->lease) { qa_unified_frame_destroy(d->frame); return; }
    if (d->events && (d->events->lease || d->events->page_owned)) { qa_unified_frame_events_destroy(d->events); return; }
    qa_unified_frame_destroy(d->frame);
    if (d->inputs) { qa_unified_inputs_free(d->inputs); free(d->inputs); }
    qa_unified_frame_events_destroy(d->events);
    if (d->metadata_borrowed) free(d->metadata);
    else qa_unified_frame_metadata_destroy(d->metadata);
    if (d->control) { qa_unified_record_dispose(&qa_unified_control_layout,d->control); free(d->control); }
    qa_json_destroy(d->json); qa_buffer_free(&d->source); free(d);
}
const qa_json_document *qa_unified_document_json(const qa_unified_document *d) { return d?d->json:NULL; }
qa_json_id qa_unified_document_root(const qa_unified_document *d) { return d?qa_json_root(d->json):QA_JSON_NONE; }
qa_unified_document_kind qa_unified_document_type(const qa_unified_document *d) { return d?d->kind:QA_UNIFIED_CHECKPOINT; }

static bool tagged(const qa_unified_document *d, qa_json_id id, const char *tag, qa_buffer *text, qa_error *error) {
    if (!d || !qa_json_string_equal(d->json,qa_json_get(d->json,id,"$qts"),tag)) return bad(error,"checkpoint tagged value has wrong type");
    return qa_json_string(d->json,qa_json_get(d->json,id,"value"),text,error);
}

bool qa_unified_document_bytes(const qa_unified_document *d, qa_json_id id, qa_buffer *out, qa_error *error) {
    if (!out) return bad(error,"missing checkpoint byte destination");
    qa_buffer text={0};
    if (!tagged(d,id,"bytes",&text,error)) return false;
    size_t size;
    if (!valid_base64((qa_bytes){text.data,text.size},&size,error)) { qa_buffer_free(&text); return false; }
    uint8_t *data=malloc(size?size:1);
    if (!data) { qa_buffer_free(&text); qa_error_set(error,QA_ERROR_MEMORY,0,"decoding checkpoint bytes"); return false; }
    size_t written=0;
    for (size_t i=0;i<text.size;i+=4) {
        uint32_t value=0;
        for (size_t j=0;j<4;++j) { int n=base64_digit(text.data[i+j]); value=(value<<6)|(n<0?0u:(uint32_t)n); }
        if (written<size) data[written++]=(uint8_t)(value>>16);
        if (written<size) data[written++]=(uint8_t)(value>>8);
        if (written<size) data[written++]=(uint8_t)value;
    }
    qa_buffer_free(&text); *out=(qa_buffer){data,size}; return true;
}

bool qa_unified_document_number(const qa_unified_document *d, qa_json_id id, double *out, qa_error *error) {
    if (!d || !out) return bad(error,"missing checkpoint numeric destination");
    if (qa_json_type(d->json,id)==QA_JSON_NUMBER) return qa_json_number(d->json,id,out,error);
    qa_buffer text={0};
    if (!tagged(d,id,"number",&text,error)) return false;
    double value;
    if (text_is(text,"-0")) value=-0.0;
    else if (text_is(text,"NaN")) value=NAN;
    else if (text_is(text,"Infinity")) value=INFINITY;
    else if (text_is(text,"-Infinity")) value=-INFINITY;
    else { qa_buffer_free(&text); return bad(error,"invalid checkpoint number tag"); }
    qa_buffer_free(&text); *out=value; return true;
}

bool qa_unified_document_bigint(const qa_unified_document *d, qa_json_id id, qa_buffer *out, qa_error *error) {
    if (!out) return bad(error,"missing checkpoint bigint destination");
    qa_buffer text={0};
    if (!tagged(d,id,"bigint",&text,error)) return false;
    if (text_is(text,"-0")) { text.data[0]='0'; text.data[1]=0; text.size=1; }
    *out=text; return true;
}

static bool checkpoint_tag(const char *tag, qa_bytes value, qa_buffer *out, qa_error *error) {
    qa_buffer quoted={0};
    if (!out || !qa_json_quote(value,&quoted,error)) return false;
    qa_unified_builder b={.maximum=VALUE_LIMIT};
    bool ok=qa_unified_append(&b,"{\"$qts\":\"",9,error) && qa_unified_append(&b,tag,strlen(tag),error) &&
        qa_unified_append(&b,"\",\"value\":",10,error) && qa_unified_append(&b,quoted.data,quoted.size,error) &&
        qa_unified_append(&b,"}",1,error);
    qa_buffer_free(&quoted);
    if (!ok) { free(b.data); return false; }
    *out=(qa_buffer){b.data,b.size}; return true;
}

bool qa_unified_checkpoint_bytes(qa_bytes bytes, qa_buffer *out, qa_error *error) {
    static const char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if ((bytes.size && !bytes.data) || bytes.size>VALUE_LIMIT/4*3) return bad(error,"checkpoint bytes exceed limit");
    size_t length=((bytes.size+2)/3)*4;
    uint8_t *text=malloc(length?length:1);
    if (!text) { qa_error_set(error,QA_ERROR_MEMORY,0,"encoding checkpoint bytes"); return false; }
    size_t j=0;
    for (size_t i=0;i<bytes.size;i+=3) {
        uint32_t n=(uint32_t)bytes.data[i]<<16;
        if (i+1<bytes.size) n|=(uint32_t)bytes.data[i+1]<<8;
        if (i+2<bytes.size) n|=bytes.data[i+2];
        text[j++]=(uint8_t)digits[(n>>18)&63u]; text[j++]=(uint8_t)digits[(n>>12)&63u];
        text[j++]=(uint8_t)(i+1<bytes.size?digits[(n>>6)&63u]:'=');
        text[j++]=(uint8_t)(i+2<bytes.size?digits[n&63u]:'=');
    }
    bool ok=checkpoint_tag("bytes",(qa_bytes){text,length},out,error); free(text); return ok;
}

bool qa_unified_checkpoint_bigint(const char *decimal, qa_buffer *out, qa_error *error) {
    if (!decimal || !valid_bigint((qa_bytes){(const uint8_t *)decimal,strlen(decimal)})) return bad(error,"invalid checkpoint bigint");
    if (!strcmp(decimal,"-0")) decimal="0";
    return checkpoint_tag("bigint",(qa_bytes){(const uint8_t *)decimal,strlen(decimal)},out,error);
}

bool qa_unified_checkpoint_number(double value, qa_buffer *out, qa_error *error) {
    if (!out) return bad(error,"missing checkpoint numeric destination");
    const char *special=isnan(value)?"NaN":isinf(value)?(signbit(value)?"-Infinity":"Infinity"):
        value==0 && signbit(value)?"-0":NULL;
    if (special) return checkpoint_tag("number",(qa_bytes){(const uint8_t *)special,strlen(special)},out,error);
    char text[32];
    if (!qa_unified_number_text(value,text,error)) return false;
    size_t size=strlen(text); uint8_t *data=malloc(size);
    if (!data) { qa_error_set(error,QA_ERROR_MEMORY,0,"encoding checkpoint number"); return false; }
    memcpy(data,text,size); *out=(qa_buffer){data,size}; return true;
}


const qa_unified_frame *qa_unified_document_frame(const qa_unified_document *d)
{ return d ? d->frame : NULL; }
const qa_unified_input_batch *qa_unified_document_inputs(const qa_unified_document *d)
{ return d ? d->inputs : NULL; }
const qa_unified_frame_events *qa_unified_document_events(const qa_unified_document *d)
{ return d ? d->events : NULL; }
const qa_unified_frame_metadata *qa_unified_document_metadata(const qa_unified_document *d)
{ return d ? d->metadata : NULL; }
const qa_unified_control *qa_unified_document_control(const qa_unified_document *d)
{ return d ? d->control : NULL; }
qa_unified_control_kind qa_unified_document_control_type(const qa_unified_document *d)
{
    if (!d || d->kind!=QA_UNIFIED_CONTROL_DOCUMENT) return QA_UNIFIED_CONTROL_INVALID;
    if (d->control) return d->control->kind;
    if (d->events) return QA_UNIFIED_CONTROL_EVENTS;
    if (d->metadata) return QA_UNIFIED_CONTROL_METADATA;
    qa_json_id value=qa_json_get(d->json,qa_json_root(d->json),"value");
    qa_json_id kind=qa_json_get(d->json,value,"kind");
    if (qa_json_string_equal(d->json,kind,"offer")) return QA_UNIFIED_CONTROL_OFFER;
    return QA_UNIFIED_CONTROL_INVALID;
}
bool qa_unified_document_epoch(const qa_unified_document *d, uint32_t *out, qa_error *error)
{
    if (!d || !out) return bad(error,"Unified document has no actual epoch output");
    uint32_t epoch=d->control?d->control->epoch:d->frame?d->frame->epoch:d->inputs?d->inputs->epoch:
        d->events?d->events->epoch:d->metadata?d->metadata->epoch:0;
    if (!epoch && d->json && qa_unified_document_control_type(d)!=QA_UNIFIED_CONTROL_INVALID) {
        uint64_t value;
        qa_json_id root=qa_json_get(d->json,qa_json_root(d->json),"value");
        if (!qa_json_u64(d->json,qa_json_get(d->json,root,"epoch"),&value,error) || !value || value>UINT32_MAX)
            return bad(error,"Unified setup epoch exceeds its actual native namespace");
        epoch=(uint32_t)value;
    }
    if (!epoch) return bad(error,"Unified document has no actual world epoch");
    *out=epoch; return true;
}
const qa_unified_handshake *qa_unified_document_handshake(const qa_unified_document *d)
{ return d && d->kind==QA_UNIFIED_HANDSHAKE_DOCUMENT ? &d->handshake : NULL; }
size_t qa_unified_document_memory(const qa_unified_document *d)
{
    if (!d) return 0;
    if (!d->frame || !d->frame->lease) return d->bytes;
    size_t bytes=qa_unified_frame_lease_used(d->frame->lease);
    const qa_unified_world_frame *world=d->frame->world;
    if (world && world->lease && world->lease!=d->frame->lease) {
        size_t shared=qa_unified_frame_lease_used(world->lease);
        if (shared>SIZE_MAX-bytes) return SIZE_MAX;
        bytes+=shared;
    }
    return bytes;
}
bool qa_unified_document_equal(const qa_unified_document *a, const qa_unified_document *b)
{
    if (a==b) return true;
    if (!a || !b || a->kind!=b->kind) return false;
    if (a->frame || b->frame) return a->frame && b->frame && qa_unified_frame_equal(a->frame,b->frame);
    if (a->inputs || b->inputs) return a->inputs && b->inputs && qa_unified_record_equal(&qa_unified_inputs_layout,a->inputs,b->inputs, NULL, NULL);
    if (a->events || b->events) return a->events && b->events && qa_unified_record_equal(&qa_unified_events_layout,a->events,b->events, a->events->strings, b->events->strings);
    if (a->metadata || b->metadata) return a->metadata && b->metadata && qa_unified_record_equal(&qa_unified_metadata_layout,a->metadata,b->metadata, NULL, NULL);
    if (a->control || b->control) return a->control && b->control && qa_unified_record_equal(&qa_unified_control_layout,a->control,b->control, NULL, NULL);
    if (a->kind==QA_UNIFIED_HANDSHAKE_DOCUMENT) return qa_unified_record_equal(&qa_unified_handshake_layout,&a->handshake,&b->handshake, NULL, NULL);
    return a->source.size==b->source.size && (!a->source.size || !memcmp(a->source.data,b->source.data,a->source.size));
}
static qa_unified_document *typed_document(qa_unified_document_kind kind, size_t bytes,
    qa_unified_frame_lease *lease, qa_event_transaction *transaction, qa_error *error)
{
    qa_unified_document *d=transaction ? qa_event_ring_alloc(transaction,sizeof(*d),_Alignof(qa_unified_document),error) : lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*d),_Alignof(qa_unified_document),error) : calloc(1,sizeof(*d));
    if (!d) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating actual typed Unified document"); return NULL; }
    memset(d,0,sizeof(*d));
    d->references=1; d->kind=kind; d->bytes=bytes; return d;
}
bool qa_unified_document_create_handshake(const qa_unified_handshake *value,
    qa_unified_document **out, qa_error *error)
{
    if (!value || !out || *out || (unsigned)value->kind>QA_UNIFIED_CONNECT)
        return bad(error,"Unified handshake requires its actual native kind and tokens");
    qa_unified_document *d=typed_document(QA_UNIFIED_HANDSHAKE_DOCUMENT,sizeof(*value),NULL, NULL,error);
    if (!d) return false;
    d->handshake=*value;
    if (value->kind==QA_UNIFIED_HELLO) memset(&d->handshake.token,0,sizeof(d->handshake.token));
    *out=d; return true;
}
static bool control_document_owned(qa_unified_control **owned, qa_unified_frame_lease *lease, qa_unified_document **out, qa_error *error)
{
    size_t bytes;
    if (!owned || !*owned || !out || *out || !qa_unified_control_check(*owned,&bytes,error)) return false;
    qa_unified_document *d=typed_document(QA_UNIFIED_CONTROL_DOCUMENT,bytes,lease, NULL,error);
    if (!d) return false;
    if(lease && !qa_unified_frame_lease_retain(lease,error)) return false;
    d->lease=lease; d->control=*owned; *owned=NULL; *out=d; return true;
}
static void *control_allocate(void *lease,size_t bytes,size_t alignment,qa_error *error)
{ return qa_unified_frame_lease_alloc(lease,1,bytes,alignment,error); }
bool qa_unified_document_create_control(const qa_unified_control *value, qa_unified_frame_lease *lease,
    qa_unified_document **out, qa_error *error)
{
    size_t bytes;
    if (!value || !out || *out || !qa_unified_control_check(value,&bytes,error)) return false;
    qa_unified_control *owned=lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*owned),_Alignof(qa_unified_control),error) : calloc(1,sizeof(*owned));
    if (!owned) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual Unified control"); return false; }
    bool okay=qa_unified_record_clone_alloc(&qa_unified_control_layout,value,owned,
        lease?control_allocate:NULL,lease,(qa_bytes){0},error) && control_document_owned(&owned,lease,out,error);
    if (owned && !lease) { qa_unified_record_dispose(&qa_unified_control_layout,owned); free(owned); }
    return okay;
}
bool qa_unified_document_create_frame(qa_unified_frame **owned, qa_unified_document **out, qa_error *error)
{
    size_t bytes=0;
    if (!owned || !*owned || !out || *out || !(*owned)->world)
        return bad(error,"Unified FRAME transfer requires its actual owned world cut");
    qa_unified_frame *frame=*owned;
    if (!qa_unified_world_frame_clock_check(frame->world,error)) return false;
    if (!frame->lease && !qa_unified_record_measure(&qa_unified_frame_layout,frame,&bytes,error)) return false;
    qa_unified_document *d=typed_document(QA_UNIFIED_FRAME_DOCUMENT,bytes,frame->lease, NULL,error);
    if (!d) return false;
    if (frame->lease) {
        bytes=qa_unified_frame_lease_used(frame->lease);
        if (frame->world->lease && frame->world->lease!=frame->lease) {
            size_t world_bytes=qa_unified_frame_lease_used(frame->world->lease);
            if (world_bytes>VALUE_LIMIT || bytes>VALUE_LIMIT-world_bytes)
                return bad(error,"Unified FRAME exceeds its retained memory bound");
            bytes+=world_bytes;
        }
        if (bytes>VALUE_LIMIT) return bad(error,"Unified FRAME exceeds its retained memory bound");
        d->bytes=bytes;
    }
    d->frame=*owned; *owned=NULL; *out=d; return true;
}
bool qa_unified_document_create_inputs(qa_unified_input_batch **owned, qa_unified_document **out, qa_error *error)
{
    size_t bytes;
    if (!owned || !*owned || !out || *out || !qa_unified_inputs_check(*owned,&bytes,error)) return false;
    qa_unified_document *d=typed_document(QA_UNIFIED_INPUT_DOCUMENT,bytes,NULL, NULL,error);
    if (!d) return false;
    d->inputs=*owned; *owned=NULL; *out=d; return true;
}
bool qa_unified_document_create_events(qa_unified_frame_events **owned, qa_event_transaction *transaction, qa_unified_document **out, qa_error *error)
{
    size_t bytes;
    if (!owned || !*owned || !out || *out || !(*owned)->epoch ||
        !qa_unified_record_measure(&qa_unified_events_layout,*owned,&bytes,error)) return false;
    qa_unified_document *d=typed_document(QA_UNIFIED_CONTROL_DOCUMENT,bytes,(*owned)->lease,transaction,error);
    if (!d) return false;
    d->events=*owned; *owned=NULL; *out=d; return true;
}
bool qa_unified_document_create_metadata(qa_unified_frame_metadata **owned, qa_unified_document **out, qa_error *error)
{
    size_t bytes;
    if (!owned || !*owned || !out || *out || !qa_unified_metadata_check(*owned,&bytes,error)) return false;
    qa_unified_document *d=typed_document(QA_UNIFIED_CONTROL_DOCUMENT,bytes,NULL, NULL,error);
    if (!d) return false;
    d->metadata=*owned; *owned=NULL; *out=d; return true;
}
bool qa_unified_metadata_apply(const qa_unified_document *previous, const qa_unified_document *update,
    qa_unified_document **out, qa_error *error)
{
    const qa_unified_frame_metadata *old=qa_unified_document_metadata(previous);
    const qa_unified_frame_metadata *next=qa_unified_document_metadata(update);
    if (!out || *out || !next || (previous && !old) ||
        (!old && (!next->replace_configurations || !next->replace_styles || !next->replace_q3 || !next->replace_q1)) ||
        (old && (old->epoch!=next->epoch || !old->replace_configurations || !old->replace_styles || !old->replace_q3 || !old->replace_q1 ||
            next->frame<old->frame || next->configuration_revision<old->configuration_revision ||
            next->roster_revision<old->roster_revision || next->style_revision<old->style_revision || next->q1_revision<old->q1_revision ||
            (!next->replace_configurations && (next->configuration_revision!=old->configuration_revision ||
                next->roster_revision!=old->roster_revision)) ||
            (!next->replace_styles && next->style_revision!=old->style_revision) ||
            (!next->replace_q1 && next->q1_revision!=old->q1_revision))))
        return bad(error,"Unified metadata update lost its actual retained revision domains");
    qa_unified_frame_metadata merged=*next;
    merged.replace_configurations=true; merged.replace_styles=true; merged.replace_q3=true; merged.replace_q1=true;
    if (old) {
        if (!next->replace_configurations) { merged.configurations=old->configurations; merged.configuration_count=old->configuration_count; }
        if (!next->replace_styles) { merged.styles=old->styles; merged.style_count=old->style_count; }
        if (!next->replace_q3) { merged.q3_configurations=old->q3_configurations; merged.q3_configuration_count=old->q3_configuration_count; }
        if (!next->replace_q1) merged.q1=old->q1;
        qa_unified_frame_metadata same=*old; same.frame=next->frame;
        if (next->replace_configurations && next->configuration_revision==old->configuration_revision &&
            next->roster_revision==old->roster_revision) {
            qa_unified_frame_metadata compared=same;
            compared.configurations=next->configurations; compared.configuration_count=next->configuration_count;
            if (!qa_unified_record_equal(&qa_unified_metadata_layout,&same,&compared, NULL, NULL))
                return bad(error,"Unified configuration changed without its actual Source revision");
        }
        if (next->replace_styles && next->style_revision==old->style_revision) {
            qa_unified_frame_metadata compared=same;
            compared.styles=next->styles; compared.style_count=next->style_count;
            if (!qa_unified_record_equal(&qa_unified_metadata_layout,&same,&compared, NULL, NULL))
                return bad(error,"Unified lightstyle changed without its actual Source revision");
        }
        if (next->replace_q1 && next->q1_revision==old->q1_revision) {
            qa_unified_frame_metadata compared=same; compared.q1=next->q1;
            if (!qa_unified_record_equal(&qa_unified_metadata_layout,&same,&compared, NULL, NULL))
                return bad(error,"Unified Q1 world changed without its actual Source revision");
        }
        if (next->replace_q3) {
            for (size_t i=0;i<next->q3_configuration_count;++i) {
                const qa_unified_q3_configuration *row=next->q3_configurations+i;
                for (size_t p=0;p<old->q3_configuration_count;++p) {
                    const qa_unified_q3_configuration *prior=old->q3_configurations+p;
                    if (strcmp(prior->provider_name,row->provider_name) || strcmp(prior->instance,row->instance) ||
                        strcmp(prior->content,row->content) || prior->publication!=row->publication || prior->map_revision!=row->map_revision) continue;
                    if (row->configuration_revision<prior->configuration_revision ||
                        (row->configuration_revision==prior->configuration_revision &&
                            !qa_unified_record_equal(&qa_unified_q3_configuration_layout,prior,row, NULL, NULL)))
                        return bad(error,"Unified Q3 configuration changed without its actual Source revision");
                }
            }
        }
    }
    if (next->replace_configurations && next->replace_styles && next->replace_q3 && next->replace_q1)
        return qa_unified_document_retain(update,out,error);
    size_t bytes;
    if (!qa_unified_metadata_check(&merged,&bytes,error)) return false;
    qa_unified_frame_lease *lease=update->lease;
    qa_unified_frame_metadata *record=lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*record),
        _Alignof(qa_unified_frame_metadata),error) : malloc(sizeof(*record));
    if (!record) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining complete Unified metadata"); return false; }
    *record=merged;
    qa_unified_document *d=typed_document(QA_UNIFIED_CONTROL_DOCUMENT,bytes,lease,NULL,error);
    if (!d || (lease && !qa_unified_frame_lease_retain(lease,error))) {
        if (!lease) { free(record);free(d); } return false;
    }
    d->lease=lease;d->metadata=record;d->metadata_borrowed=true;
    bool replaced[4]={next->replace_configurations,next->replace_styles,next->replace_q3,next->replace_q1};
    for (size_t i=0;i<4;++i) {
        const qa_unified_document *source=replaced[i]?update:previous;
        if (source->metadata_owners[i]) source=source->metadata_owners[i];
        if (!qa_unified_document_retain(source,d->metadata_owners+i,error)) {
            qa_unified_document_destroy(d);return false;
        }
    }
    *out=d;return true;
}
bool qa_unified_document_write(const qa_unified_document *d,size_t maximum,
    qa_unified_builder *out,qa_error *error)
{
    if(d->frame) return qa_unified_frame_write(d,NULL,0,maximum,out,error);
    if(d->inputs) return qa_unified_inputs_write(d->inputs,maximum,out,error);
    out->size=0;out->maximum=maximum;out->strings=out->baseline_strings=NULL;
    if(!d->control && !d->metadata && !d->events && d->kind!=QA_UNIFIED_HANDSHAKE_DOCUMENT)
        return qa_unified_append(out,d->source.data,d->source.size,error);
    const char *magic;
    const qa_unified_record_layout *layout;
    const void *record;
    size_t limit=FRAME_LIMIT;
    if(d->control) { magic="QUCT";layout=&qa_unified_control_layout;record=d->control; }
    else if(d->kind==QA_UNIFIED_HANDSHAKE_DOCUMENT) {
        magic="QUHS";layout=&qa_unified_handshake_layout;record=&d->handshake;limit=512;
    } else if(d->metadata) { magic="QUMD";layout=&qa_unified_metadata_layout;record=d->metadata; }
    else {
        magic="QUEV";layout=&qa_unified_events_layout;record=d->events;
        out->strings=out->baseline_strings=d->events->strings;
    }
    if(out->maximum>limit) out->maximum=limit;
    return qa_unified_append(out,magic,4,error) &&
        qa_unified_record_delta_write(layout,record,NULL,out,error);
}
static bool typed_encode(const qa_unified_document *d,qa_buffer *out,qa_error *error)
{
    qa_unified_builder builder={0};
    if(!qa_unified_document_write(d,FRAME_LIMIT,&builder,error)) { free(builder.data);return false; }
    *out=(qa_buffer){builder.data,builder.size};return true;
}
bool qa_unified_inputs_write(const qa_unified_input_batch *batch, size_t maximum,
    qa_unified_builder *out, qa_error *error)
{
    if (!batch || !batch->epoch || batch->count>64 || !out || maximum<5)
        return bad(error,"Unified INPUT lacks its actual retained command batch");
    out->size=0; out->maximum=maximum<65536?maximum:65536;
    return qa_unified_append(out,"QUIN",4,error) &&
        qa_unified_record_delta_write(&qa_unified_inputs_layout,batch,NULL,out,error);
}
static bool typed_decode(qa_unified_document_kind kind, qa_bytes bytes, qa_strings *strings, qa_unified_frame_lease *lease, qa_event_transaction *transaction, qa_unified_document **out, qa_error *error)
{
    if (kind==QA_UNIFIED_HANDSHAKE_DOCUMENT) {
        if (bytes.size<5 || memcmp(bytes.data,"QUHS",4)) return bad(error,"Unified handshake has the wrong external envelope");
        qa_unified_handshake value={0};
        return qa_unified_record_delta_decode(&qa_unified_handshake_layout,
            (qa_bytes){bytes.data+4,bytes.size-4},NULL,&value,NULL, NULL, NULL, NULL, NULL,error) &&
            qa_unified_document_create_handshake(&value,out,error);
    }
    if (kind==QA_UNIFIED_CONTROL_DOCUMENT && bytes.size>=4 && !memcmp(bytes.data,"QUCT",4)) {
        qa_unified_control *record=lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*record),_Alignof(max_align_t),error) : calloc(1,sizeof(*record));
        if (!record) { qa_error_set(error,QA_ERROR_MEMORY,0,"Receiving actual Unified control"); return false; }
        bool okay=qa_unified_record_delta_decode(&qa_unified_control_layout,
            (qa_bytes){bytes.data+4,bytes.size-4},NULL,record,lease, NULL, NULL, NULL, NULL,error) && control_document_owned(&record,lease,out,error);
        if (record && !lease) { qa_unified_record_dispose(&qa_unified_control_layout,record); free(record); }
        return okay;
    }
    bool input=kind==QA_UNIFIED_INPUT_DOCUMENT;
    bool metadata=!input && bytes.size>=4 && !memcmp(bytes.data,"QUMD",4);
    if (bytes.size<5 || memcmp(bytes.data,input?"QUIN":metadata?"QUMD":"QUEV",4)) return bad(error,"Typed Unified record has the wrong external envelope");
    qa_bytes body={bytes.data+4,bytes.size-4};
    if (input) {
        qa_unified_input_batch *record=lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*record),_Alignof(max_align_t),error) : calloc(1,sizeof(*record));
        if (!record) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating actual Unified input batch"); return false; }
        size_t measured;
        bool okay=qa_unified_record_delta_decode(&qa_unified_inputs_layout,body,NULL,record,lease, NULL, NULL, NULL, NULL,error) &&
            qa_unified_inputs_check(record,&measured,error);
        qa_unified_document *d=okay?typed_document(kind,measured,lease,NULL,error):NULL;
        okay=d && (!lease || qa_unified_frame_lease_retain(lease,error));
        if(okay) { d->lease=lease;d->inputs=record;record=NULL;*out=d; }
        if (record && !lease) { qa_unified_inputs_free(record); free(record); }
        return okay;
    }
    if (metadata) {
        qa_unified_frame_metadata *record=lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*record),_Alignof(max_align_t),error) : calloc(1,sizeof(*record));
        if (!record) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating actual Unified metadata"); return false; }
        size_t measured;
        bool okay=qa_unified_record_delta_decode(&qa_unified_metadata_layout,body,NULL,record,lease, NULL, NULL, NULL, NULL,error) &&
            qa_unified_metadata_check(record,&measured,error);
        qa_unified_document *d=okay?typed_document(kind,measured,lease,NULL,error):NULL;
        okay=d && (!lease || qa_unified_frame_lease_retain(lease,error));
        if(okay) { d->lease=lease;d->metadata=record;record=NULL;*out=d; }
        if(!lease) qa_unified_frame_metadata_destroy(record);
        return okay;
    }
    qa_unified_frame_events *record=transaction ? qa_event_ring_alloc(transaction,sizeof(*record),_Alignof(qa_unified_frame_events),error) : lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*record),_Alignof(max_align_t),error) : calloc(1,sizeof(*record));
    if (!record) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating actual Unified events"); return false; }
    size_t measured;
    memset(record,0,sizeof(*record));
    record->strings=strings; qa_strings_retain(strings);
    if(lease && !qa_unified_frame_lease_retain(lease,error)) {
        qa_strings_destroy(strings);return false;
    }
    record->lease=lease;
    bool okay=qa_unified_record_delta_decode(&qa_unified_events_layout,body,NULL,record,lease,transaction?qa_event_ring_alloc:NULL,transaction, strings, NULL,error);
    record->strings=strings; record->page_owned=transaction!=NULL;
    okay=okay &&
        qa_unified_events_check(record,&measured,error);
    if (okay) okay=qa_unified_document_create_events(&record,transaction,out,error);
    qa_unified_frame_events_destroy(record);
    return okay;
}
bool qa_unified_frame_write(const qa_unified_document *document, const qa_unified_document *baseline,
    uint32_t sequence, size_t maximum, qa_unified_builder *out, qa_error *error)
{
    if (!document || !document->frame || ((sequence!=0)!=(baseline!=NULL)) || (baseline && !baseline->frame))
        return bad(error,"Unified delta encoding requires its actual acknowledged frame");
    if (!out || maximum<9) return bad(error,"Unified FRAME lacks its actual bounded wire buffer");
    out->size=0; out->maximum=maximum<VALUE_LIMIT?maximum:VALUE_LIMIT;
    out->strings=document->frame->strings; out->baseline_strings=baseline?baseline->frame->strings:NULL;
    uint8_t header[8]; memcpy(header,"QUFR",4); qa_store_u32le(header+4,sequence);
    return qa_unified_append(out,header,sizeof(header),error) &&
        qa_unified_record_delta_write(&qa_unified_frame_layout,document->frame,baseline?baseline->frame:NULL,out,error);
}
bool qa_unified_frame_baseline(qa_bytes bytes, uint32_t *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size<9 || bytes.size>VALUE_LIMIT || memcmp(bytes.data,"QUFR",4))
        return bad(error,"Invalid Unified typed frame extent");
    *out=qa_load_u32le(bytes.data+4); return true;
}
bool qa_unified_frame_decode(qa_bytes bytes, const qa_unified_document *baseline, uint32_t sequence,
    qa_unified_frame_pool *pool, qa_strings *strings, qa_unified_document **out, qa_error *error)
{
    uint32_t required;
    if (!out || !qa_unified_frame_baseline(bytes,&required,error)) return false;
    if (required!=sequence || ((required!=0)!=(baseline!=NULL)) || (baseline && !baseline->frame))
        return bad(error,"Unified delta decoding lacks its exact acknowledged frame");
    qa_unified_frame *record=qa_unified_frame_create(pool,strings,error);
    if (!record) return false;
    qa_unified_frame_lease *lease=record->lease;
    bool okay=qa_unified_record_delta_decode(&qa_unified_frame_layout,(qa_bytes){bytes.data+8,bytes.size-8},
        baseline?baseline->frame:NULL,record,lease, NULL, NULL, strings, baseline?baseline->frame->strings:NULL,error);
    record->lease=lease; record->strings=strings;
    if (okay && record->world) {
        if (lease) { okay=qa_unified_frame_lease_retain(lease,error); if (okay) record->world->lease=lease; else record->world=NULL; }
        if (okay) { record->world->references=1; record->world->strings=strings; qa_strings_retain(strings); }
    }
    size_t measured;
    if (okay) okay=qa_unified_frame_check(record,&measured,error) && qa_unified_document_create_frame(&record,out,error);
    qa_unified_frame_destroy(record); return okay;
}
