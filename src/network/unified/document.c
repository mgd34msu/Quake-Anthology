#include "value_internal.h"

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
    case QA_UNIFIED_FRAME_DOCUMENT: return VALUE_LIMIT;
    case QA_UNIFIED_CONTROL_DOCUMENT: return FRAME_LIMIT;
    case QA_UNIFIED_EVENTS_DOCUMENT: return 16u*1024u*1024u;
    case QA_UNIFIED_CHECKPOINT: case QA_UNIFIED_PREDICTION_DOCUMENT: return VALUE_LIMIT;
    }
    return 0;
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
    if (kind==QA_UNIFIED_FRAME_DOCUMENT) return qa_unified_frame_decode(bytes,NULL,0,out,error);
    return qa_unified_document_create(kind,bytes,out,error);
}

bool qa_unified_document_child(const qa_unified_document *document, qa_json_id id,
    qa_unified_document_kind kind, qa_unified_document **out, qa_error *error) {
    return qa_unified_document_create(kind, qa_json_source(document ? document->json : NULL, id), out, error);
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
    return qa_unified_frame_encode(d,NULL,0,VALUE_LIMIT,out,error);
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

/* Typed frame values share the document's immutable Source projection. The
 * retained acknowledged document supplies unchanged fields; no simulation or
 * presentation state is reconstructed by the transport. */
typedef enum frame_value {
    FRAME_SAME, FRAME_NULL, FRAME_FALSE, FRAME_TRUE, FRAME_INTEGER, FRAME_NUMBER,
    FRAME_STRING, FRAME_ARRAY, FRAME_OBJECT, FRAME_ARRAY_DELTA, FRAME_OBJECT_DELTA,
    FRAME_ACTOR_ARRAY, FRAME_BYTES
} frame_value;

static bool frame_byte(qa_unified_builder *b, uint8_t value, qa_error *e)
{ return qa_unified_append(b, &value, 1, e); }

static bool frame_unsigned(qa_unified_builder *b, uint64_t value, qa_error *e)
{
    do {
        uint8_t byte = (uint8_t)(value & 127u); value >>= 7;
        if (value) byte |= 128u;
        if (!frame_byte(b, byte, e)) return false;
    } while (value);
    return true;
}

static bool frame_text_equal(const qa_json_document *a, qa_json_id ai,
    const qa_json_document *b, qa_json_id bi)
{
    if (!a || !b || ai == QA_JSON_NONE || bi == QA_JSON_NONE) return false;
    qa_bytes x = qa_json_source(a, ai), y = qa_json_source(b, bi);
    return x.size == y.size && (!x.size || !memcmp(x.data, y.data, x.size));
}

static bool frame_string(qa_unified_builder *b, const qa_json_document *d, qa_json_id id, qa_error *e)
{
    qa_buffer value = {0};
    if (!qa_json_string(d, id, &value, e)) return false;
    bool okay = frame_unsigned(b, value.size, e) && qa_unified_append(b, value.data, value.size, e);
    qa_buffer_free(&value); return okay;
}

static bool frame_keys_equal(const qa_json_document *a, qa_json_id ai,
    const qa_json_document *b, qa_json_id bi)
{
    size_t count = qa_json_size(a, ai);
    if (qa_json_type(b, bi) != QA_JSON_OBJECT || count != qa_json_size(b, bi)) return false;
    for (size_t i = 0; i < count; ++i)
        if (!frame_text_equal(a, qa_json_key_at(a, ai, i), b, qa_json_key_at(b, bi, i))) return false;
    return true;
}

typedef struct frame_identity { uint32_t slot; uint64_t generation; } frame_identity;

static int frame_identity_compare(frame_identity a, frame_identity b)
{
    if (a.slot != b.slot) return a.slot < b.slot ? -1 : 1;
    return a.generation == b.generation ? 0 : a.generation < b.generation ? -1 : 1;
}

static bool frame_actor(const qa_json_document *d, qa_json_id row, frame_identity *identity)
{
    qa_json_id actor = qa_json_get(d, row, "actor");
    if (actor == QA_JSON_NONE) actor = qa_json_get(d, row, "id");
    uint64_t slot, generation;
    if (!qa_json_u64(d, qa_json_get(d, actor, "slot"), &slot, NULL) || slot > UINT32_MAX ||
        !qa_json_u64(d, qa_json_get(d, actor, "generation"), &generation, NULL))
        return false;
    *identity = (frame_identity){(uint32_t)slot, generation}; return true;
}

static bool frame_actor_array(const qa_json_document *d, qa_json_id array)
{
    if (qa_json_type(d, array) != QA_JSON_ARRAY) return false;
    frame_identity previous = {0};
    size_t count = qa_json_size(d, array);
    for (size_t i = 0; i < count; ++i) {
        frame_identity identity;
        if (!frame_actor(d, qa_json_at(d, array, i), &identity) || (i && frame_identity_compare(identity, previous) <= 0)) return false;
        previous = identity;
    }
    return true;
}

static bool frame_write_value(qa_unified_builder *b, const qa_unified_document *to, qa_json_id id,
    const qa_unified_document *from, qa_json_id old, unsigned depth, qa_error *e)
{
    if (depth > 128) return bad(e, "Unified frame nesting exceeds its value domain");
    const qa_json_document *d = to->json, *base = from ? from->json : NULL;
    qa_json_kind kind = qa_json_type(d, id);
    if (frame_text_equal(d, id, base, old)) return frame_byte(b, FRAME_SAME, e);
    switch (kind) {
    case QA_JSON_NULL: return frame_byte(b, FRAME_NULL, e);
    case QA_JSON_BOOL: {
        bool value;
        return qa_json_bool(d, id, &value, e) && frame_byte(b, value ? FRAME_TRUE : FRAME_FALSE, e);
    }
    case QA_JSON_NUMBER: {
        double value, prior;
        if (!qa_json_number(d, id, &value, e)) return false;
        if (qa_json_type(base, old) == QA_JSON_NUMBER && qa_json_number(base, old, &prior, e) &&
            value == prior && (value != 0 || signbit(value) == signbit(prior)))
            return frame_byte(b, FRAME_SAME, e);
        if (value >= -(double)QA_UNIFIED_SAFE_INTEGER && value <= (double)QA_UNIFIED_SAFE_INTEGER &&
            trunc(value) == value && !(value == 0 && signbit(value))) {
            int64_t integer = (int64_t)value;
            uint64_t word = integer < 0 ? (uint64_t)(-integer) * 2u - 1u : (uint64_t)integer * 2u;
            return frame_byte(b, FRAME_INTEGER, e) && frame_unsigned(b, word, e);
        }
        uint64_t word; uint8_t bytes[8];
        memcpy(&word, &value, sizeof(word)); qa_store_u64le(bytes, word);
        return frame_byte(b, FRAME_NUMBER, e) && qa_unified_append(b, bytes, sizeof(bytes), e);
    }
    case QA_JSON_STRING: return frame_byte(b, FRAME_STRING, e) && frame_string(b, d, id, e);
    case QA_JSON_OBJECT: {
        if (qa_json_string_equal(d, qa_json_get(d, id, "$qts"), "bytes")) {
            qa_buffer bytes = {0};
            if (!qa_unified_document_bytes(to, id, &bytes, e)) return false;
            bool okay = frame_byte(b, FRAME_BYTES, e) && frame_unsigned(b, bytes.size, e) &&
                qa_unified_append(b, bytes.data, bytes.size, e);
            qa_buffer_free(&bytes); return okay;
        }
        bool delta = frame_keys_equal(d, id, base, old);
        size_t count = qa_json_size(d, id);
        if (!frame_byte(b, delta ? FRAME_OBJECT_DELTA : FRAME_OBJECT, e) ||
            (!delta && !frame_unsigned(b, count, e))) return false;
        for (size_t i = 0; i < count; ++i) {
            qa_json_id prior = delta ? qa_json_at(base, old, i) : QA_JSON_NONE;
            if ((!delta && !frame_string(b, d, qa_json_key_at(d, id, i), e)) ||
                !frame_write_value(b, to, qa_json_at(d, id, i), from, prior, depth + 1, e)) return false;
        }
        return true;
    }
    case QA_JSON_ARRAY: {
        size_t count = qa_json_size(d, id), old_count = qa_json_size(base, old);
        bool delta = qa_json_type(base, old) == QA_JSON_ARRAY;
        bool actors = delta && count != old_count && frame_actor_array(d, id) && frame_actor_array(base, old);
        if (!frame_byte(b, actors ? FRAME_ACTOR_ARRAY : delta && count == old_count ? FRAME_ARRAY_DELTA : FRAME_ARRAY, e) ||
            ((actors || !delta || count != old_count) && !frame_unsigned(b, count, e))) return false;
        size_t cursor = 0;
        for (size_t i = 0; i < count; ++i) {
            qa_json_id prior = delta && i < old_count ? qa_json_at(base, old, i) : QA_JSON_NONE;
            if (actors) {
                frame_identity identity = {0}, previous = {0};
                (void)frame_actor(d, qa_json_at(d, id, i), &identity);
                while (cursor < old_count) {
                    (void)frame_actor(base, qa_json_at(base, old, cursor), &previous);
                    if (frame_identity_compare(previous, identity) >= 0) break;
                    ++cursor;
                }
                bool found = cursor < old_count && frame_identity_compare(previous, identity) == 0;
                prior = found ? qa_json_at(base, old, cursor) : QA_JSON_NONE;
                if (!frame_unsigned(b, found ? cursor + 1 : 0, e)) return false;
            }
            if (!frame_write_value(b, to, qa_json_at(d, id, i), from, prior, depth + 1, e)) return false;
        }
        return true;
    }
    case QA_JSON_INVALID: break;
    }
    return bad(e, "Unified frame contains an invalid typed value");
}

typedef struct frame_reader { qa_bytes bytes; size_t at; qa_error *error; } frame_reader;

static bool frame_read_bytes(frame_reader *r, size_t size, qa_bytes *out)
{
    if (size > r->bytes.size - r->at) return bad(r->error, "Truncated Unified typed frame");
    *out = (qa_bytes){r->bytes.data + r->at, size}; r->at += size; return true;
}

static bool frame_read_unsigned(frame_reader *r, uint64_t *out)
{
    uint64_t value = 0;
    for (unsigned shift = 0; shift <= 63; shift += 7) {
        qa_bytes byte;
        if (!frame_read_bytes(r, 1, &byte)) return false;
        if (shift == 63 && byte.data[0] > 1) return bad(r->error, "Unified frame integer overflow");
        value |= (uint64_t)(byte.data[0] & 127u) << shift;
        if (!(byte.data[0] & 128u)) {
            if (shift && !byte.data[0]) return bad(r->error, "Noncanonical Unified frame integer");
            *out = value; return true;
        }
    }
    return bad(r->error, "Unified frame integer overflow");
}

static bool frame_read_string(frame_reader *r, qa_unified_builder *b)
{
    uint64_t size; qa_bytes bytes; qa_buffer quoted = {0};
    if (!frame_read_unsigned(r, &size) || size > SIZE_MAX || !frame_read_bytes(r, (size_t)size, &bytes) ||
        !qa_json_quote(bytes, &quoted, r->error)) return false;
    bool okay = qa_unified_append(b, quoted.data, quoted.size, r->error);
    qa_buffer_free(&quoted); return okay;
}

static bool frame_read_value(frame_reader *r, qa_unified_builder *b,
    const qa_json_document *base, qa_json_id old, unsigned depth)
{
    if (depth > 128) return bad(r->error, "Unified frame nesting exceeds its value domain");
    qa_bytes tag;
    if (!frame_read_bytes(r, 1, &tag)) return false;
    frame_value kind = (frame_value)tag.data[0];
    switch (kind) {
    case FRAME_SAME: {
        if (qa_json_type(base, old) == QA_JSON_INVALID) return bad(r->error, "Unified delta lacks its field baseline");
        qa_bytes value = qa_json_source(base, old);
        return qa_unified_append(b, value.data, value.size, r->error);
    }
    case FRAME_NULL: return qa_unified_append(b, "null", 4, r->error);
    case FRAME_FALSE: return qa_unified_append(b, "false", 5, r->error);
    case FRAME_TRUE: return qa_unified_append(b, "true", 4, r->error);
    case FRAME_INTEGER: case FRAME_NUMBER: {
        double value;
        if (kind == FRAME_INTEGER) {
            uint64_t word;
            if (!frame_read_unsigned(r, &word) || word > QA_UNIFIED_SAFE_INTEGER * 2u)
                return bad(r->error, "Unified frame integer exceeds its exact value domain");
            value = (word & 1u) ? -(double)(word / 2u + 1u) : (double)(word / 2u);
        } else {
            qa_bytes bytes;
            if (!frame_read_bytes(r, 8, &bytes)) return false;
            uint64_t word = qa_load_u64le(bytes.data); memcpy(&value, &word, sizeof(value));
        }
        char text[32];
        return qa_unified_number_text(value, text, r->error) && qa_unified_append(b, text, strlen(text), r->error);
    }
    case FRAME_STRING: return frame_read_string(r, b);
    case FRAME_BYTES: {
        uint64_t size; qa_bytes bytes; qa_buffer tagged_bytes = {0};
        if (!frame_read_unsigned(r, &size) || size > SIZE_MAX || !frame_read_bytes(r, (size_t)size, &bytes) ||
            !qa_unified_checkpoint_bytes(bytes, &tagged_bytes, r->error)) return false;
        bool okay = qa_unified_append(b, tagged_bytes.data, tagged_bytes.size, r->error);
        qa_buffer_free(&tagged_bytes); return okay;
    }
    case FRAME_OBJECT: case FRAME_OBJECT_DELTA: case FRAME_ARRAY: case FRAME_ARRAY_DELTA: case FRAME_ACTOR_ARRAY: {
        bool object = kind == FRAME_OBJECT || kind == FRAME_OBJECT_DELTA;
        bool delta = kind == FRAME_OBJECT_DELTA || kind == FRAME_ARRAY_DELTA;
        bool actors = kind == FRAME_ACTOR_ARRAY;
        qa_json_kind expected = object ? QA_JSON_OBJECT : QA_JSON_ARRAY;
        if ((delta || actors) && qa_json_type(base, old) != expected)
            return bad(r->error, "Unified container delta lacks its typed baseline");
        uint64_t count = qa_json_size(base, old);
        if (!delta && !frame_read_unsigned(r, &count)) return false;
        if (count > r->bytes.size - r->at) return bad(r->error, "Unified frame container exceeds its record extent");
        if (!qa_unified_append(b, object ? "{" : "[", 1, r->error)) return false;
        size_t previous = 0;
        for (size_t i = 0; i < (size_t)count; ++i) {
            if (i && !qa_unified_append(b, ",", 1, r->error)) return false;
            qa_json_id prior = qa_json_type(base, old) == expected ? qa_json_at(base, old, i) : QA_JSON_NONE;
            if (object) {
                if (delta) {
                    qa_bytes key = qa_json_source(base, qa_json_key_at(base, old, i));
                    if (!qa_unified_append(b, key.data, key.size, r->error)) return false;
                } else if (!frame_read_string(r, b)) return false;
                if (!qa_unified_append(b, ":", 1, r->error)) return false;
                if (!delta) prior = QA_JSON_NONE;
            }
            if (actors) {
                uint64_t index;
                if (!frame_read_unsigned(r, &index) || index > qa_json_size(base, old) || (index && index <= previous))
                    return bad(r->error, "Unified actor delta changes its ordered baseline membership");
                prior = index ? qa_json_at(base, old, (size_t)index - 1) : QA_JSON_NONE;
                if (index) previous = (size_t)index;
            }
            if (!frame_read_value(r, b, base, prior, depth + 1)) return false;
        }
        return qa_unified_append(b, object ? "}" : "]", 1, r->error);
    }
    }
    return bad(r->error, "Unknown Unified typed frame field");
}

bool qa_unified_frame_encode(const qa_unified_document *document, const qa_unified_document *baseline,
    uint32_t sequence, size_t maximum, qa_buffer *out, qa_error *e)
{
    if (!out || !document || document->kind != QA_UNIFIED_FRAME_DOCUMENT ||
        ((sequence != 0) != (baseline != NULL)) || (baseline && baseline->kind != QA_UNIFIED_FRAME_DOCUMENT))
        return bad(e, "Unified delta encoding requires its actual frame baseline");
    qa_unified_builder b = {.maximum = maximum < VALUE_LIMIT ? maximum : VALUE_LIMIT};
    uint8_t header[8] = {'Q','U','F','R',0,0,0,0}; qa_store_u32le(header + 4, sequence);
    bool okay = qa_unified_append(&b, header, sizeof(header), e) && frame_write_value(&b, document,
        qa_json_root(document->json), baseline, baseline ? qa_json_root(baseline->json) : QA_JSON_NONE, 0, e);
    if (!okay) { free(b.data); return false; }
    *out = (qa_buffer){b.data, b.size}; return true;
}

bool qa_unified_frame_baseline(qa_bytes bytes, uint32_t *out, qa_error *e)
{
    if (!out || !bytes.data || bytes.size < 9 || bytes.size > VALUE_LIMIT || memcmp(bytes.data, "QUFR", 4))
        return bad(e, "Invalid Unified typed frame extent");
    *out = qa_load_u32le(bytes.data + 4); return true;
}

bool qa_unified_frame_decode(qa_bytes bytes, const qa_unified_document *baseline, uint32_t sequence,
    qa_unified_document **out, qa_error *e)
{
    uint32_t required;
    if (!out || !qa_unified_frame_baseline(bytes, &required, e)) return false;
    if (required != sequence || ((required != 0) != (baseline != NULL)) ||
        (baseline && baseline->kind != QA_UNIFIED_FRAME_DOCUMENT))
        return bad(e, "Unified delta decoding lacks its exact acknowledged frame");
    frame_reader r = {bytes, 8, e}; qa_unified_builder b = {.maximum = VALUE_LIMIT};
    bool okay = frame_read_value(&r, &b, baseline ? baseline->json : NULL,
        baseline ? qa_json_root(baseline->json) : QA_JSON_NONE, 0) && r.at == bytes.size;
    if (!okay) { free(b.data); if (!e || e->code == QA_OK) bad(e, "Trailing Unified typed frame fields"); return false; }
    return create_document(QA_UNIFIED_FRAME_DOCUMENT, (qa_buffer){b.data, b.size}, out, e);
}
