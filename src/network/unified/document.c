#include "value_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define VALUE_LIMIT (32u*1024u*1024u)
#define FRAME_LIMIT (4u*1024u*1024u)

struct qa_unified_document {
    size_t references;
    qa_unified_document_kind kind;
    qa_buffer source;
    qa_json_document *json;
};

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
    case QA_UNIFIED_FRAME_DOCUMENT: case QA_UNIFIED_CONTROL_DOCUMENT: return FRAME_LIMIT;
    case QA_UNIFIED_EVENTS_DOCUMENT: return 16u*1024u*1024u;
    case QA_UNIFIED_CHECKPOINT: case QA_UNIFIED_PREDICTION_DOCUMENT: return VALUE_LIMIT;
    }
    return 0;
}

static bool inflate_frame(qa_bytes input, qa_buffer *out, qa_error *error) {
    z_stream stream={0};
    if (inflateInit2(&stream,-MAX_WBITS)!=Z_OK) return bad(error,"initializing unified frame inflater");
    stream.next_in=(Bytef *)(void *)input.data; stream.avail_in=(uInt)input.size;
    qa_unified_builder b={.maximum=VALUE_LIMIT};
    uint8_t chunk[32768]; int result;
    do {
        stream.next_out=chunk; stream.avail_out=(uInt)sizeof(chunk);
        result=inflate(&stream,Z_NO_FLUSH);
        size_t size=sizeof(chunk)-stream.avail_out;
        if (!qa_unified_append(&b,chunk,size,error)) { inflateEnd(&stream); free(b.data); return false; }
    } while (result==Z_OK);
    bool ok=result==Z_STREAM_END && stream.avail_in==0;
    inflateEnd(&stream);
    if (!ok) { free(b.data); return bad(error,"invalid or trailing unified frame deflate stream"); }
    *out=(qa_buffer){b.data,b.size}; return true;
}

static bool create_document(qa_unified_document_kind kind, qa_buffer source,
                              qa_unified_document **out, qa_error *error) {
    qa_unified_document *d=calloc(1,sizeof(*d));
    if (!d) { qa_buffer_free(&source); qa_error_set(error,QA_ERROR_MEMORY,0,"allocating unified document"); return false; }
    d->references=1; d->kind=kind; d->source=source;
    if (!qa_json_parse((qa_bytes){d->source.data,d->source.size},&d->json,error) ||
        !qa_unified_tag_check(d->json,qa_json_root(d->json),0,error) ||
        !qa_unified_schema_check(kind,d->json,error)) {
        qa_unified_document_destroy(d); return false;
    }
    *out=d; return true;
}

bool qa_unified_document_create(qa_unified_document_kind kind, qa_bytes bytes,
                                 qa_unified_document **out, qa_error *error) {
    size_t maximum=kind==QA_UNIFIED_FRAME_DOCUMENT?VALUE_LIMIT:wire_limit(kind);
    if (!out || !maximum || !bytes.data || !bytes.size || bytes.size>maximum)
        return bad(error,"invalid unified document kind or byte extent");
    qa_buffer source={.data=malloc(bytes.size),.size=bytes.size};
    if (!source.data) { qa_error_set(error,QA_ERROR_MEMORY,0,"copying unified document"); return false; }
    memcpy(source.data,bytes.data,bytes.size);
    return create_document(kind,source,out,error);
}

bool qa_unified_document_decode(qa_unified_document_kind kind, qa_bytes bytes,
                                 qa_unified_document **out, qa_error *error) {
    size_t maximum=wire_limit(kind);
    if (!out || !maximum || !bytes.data || !bytes.size || bytes.size>maximum)
        return bad(error,"invalid unified document kind or byte extent");
    if (kind==QA_UNIFIED_FRAME_DOCUMENT) {
        qa_buffer source={0};
        if (!inflate_frame(bytes,&source,error)) return false;
        return create_document(kind,source,out,error);
    }
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
    if (d->kind!=QA_UNIFIED_FRAME_DOCUMENT) {
        uint8_t *data=malloc(d->source.size?d->source.size:1);
        if (!data) { qa_error_set(error,QA_ERROR_MEMORY,0,"copying unified document"); return false; }
        if (d->source.size) memcpy(data,d->source.data,d->source.size);
        *out=(qa_buffer){data,d->source.size}; return true;
    }
    z_stream stream={0};
    if (deflateInit2(&stream,1,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY)!=Z_OK)
        return bad(error,"initializing unified frame deflater");
    uLong capacity=deflateBound(&stream,(uLong)d->source.size);
    uint8_t *data=malloc((size_t)capacity);
    if (!data) { deflateEnd(&stream); qa_error_set(error,QA_ERROR_MEMORY,0,"compressing unified frame"); return false; }
    stream.next_in=d->source.data; stream.avail_in=(uInt)d->source.size;
    stream.next_out=data; stream.avail_out=(uInt)capacity;
    int status=deflate(&stream,Z_FINISH);
    size_t size=(size_t)stream.total_out;
    deflateEnd(&stream);
    if (status!=Z_STREAM_END || size>FRAME_LIMIT) { free(data); return bad(error,"unified frame exceeds compressed byte limit"); }
    *out=(qa_buffer){data,size}; return true;
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
