#include "value_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct reader { const qa_json_document *json; qa_json_id id; qa_error *error; } reader;
typedef bool (*check_fn)(reader);

static reader field(reader r, const char *name) { r.id=qa_json_get(r.json,r.id,name); return r; }
static bool fail(reader r, const char *why) { qa_error_set(r.error,QA_ERROR_FORMAT,0,"unified schema: %s",why); return false; }
static bool type(reader r, qa_json_kind kind) { return qa_json_type(r.json,r.id)==kind || fail(r,"wrong field type"); }
static bool absent(reader r) { return r.id==QA_JSON_NONE; }
static bool record(reader r) {
    return type(r,QA_JSON_OBJECT) && (qa_json_get(r.json,r.id,"$qts")==QA_JSON_NONE || fail(r,"tagged value is not a record"));
}
static bool literal(reader r, const char *text) { return qa_json_string_equal(r.json,r.id,text) || fail(r,"unknown field variant"); }
static bool is(reader r, const char *text) { return qa_json_string_equal(r.json,r.id,text); }
static bool number(reader r, double *out) {
    if (qa_json_type(r.json,r.id)==QA_JSON_NUMBER) return qa_json_number(r.json,r.id,out,r.error);
    if (is(field(r,"$qts"),"number")) {
        reader v=field(r,"value");
        if (is(v,"-0")) *out=-0.0;
        else if (is(v,"NaN")) *out=NAN;
        else if (is(v,"Infinity")) *out=INFINITY;
        else if (is(v,"-Infinity")) *out=-INFINITY;
        else return fail(r,"invalid numeric tag");
        return true;
    }
    return fail(r,"expected number");
}
static bool integer(reader r, double low, double high) {
    double value;
    return number(r,&value) && ((isfinite(value) && floor(value)==value && value>=low && value<=high) || fail(r,"integer outside range"));
}
static bool natural(reader r) { return integer(r,0,(double)QA_UNIFIED_SAFE_INTEGER); }
static bool list(reader r, size_t minimum, size_t maximum, check_fn check) {
    if (!type(r,QA_JSON_ARRAY)) return false;
    size_t count=qa_json_size(r.json,r.id);
    if (count<minimum || count>maximum) return fail(r,"list extent outside range");
    for (size_t i=0;i<count;++i) {
        reader element={r.json,qa_json_at(r.json,r.id,i),r.error};
        if (check && !check(element)) return false;
    }
    return true;
}
static bool choices(reader r, const char *values) {
    while (*values) {
        const char *end=strchr(values,' '); size_t count=end?(size_t)(end-values):strlen(values);
        char value[64];
        if (count>=sizeof(value)) return fail(r,"internal variant too long");
        memcpy(value,values,count); value[count]=0;
        if (is(r,value)) return true;
        if (!end) break;
        values=end+1;
    }
    return fail(r,"unknown variant");
}
static bool bounded_string(reader r, size_t maximum, bool no_zero) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    /* The donor counts UTF-16 code units. */
    size_t units=0;
    for (size_t i=0;i<text.size;++i) {
        uint8_t c=text.data[i];
        if ((c&0xc0u)!=0x80u) units+=c>=0xf0u?2u:1u;
    }
    bool ok=units<=maximum && (!no_zero || !memchr(text.data,0,text.size));
    qa_buffer_free(&text);
    return ok || fail(r,"invalid protocol string");
}
static bool protocol_string(reader r) { return bounded_string(r,8192,true); }
static bool namespaced(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    const uint8_t *colon=memchr(text.data,':',text.size);
    bool ok=colon && colon!=text.data && colon!=text.data+text.size-1;
    qa_buffer_free(&text); return ok || fail(r,"invalid namespaced identity");
}
static bool owner(reader r) { return record(r) && namespaced(field(r,"provider")) && integer(field(r,"generation"),1,(double)QA_UNIFIED_SAFE_INTEGER); }
static bool component_header(reader r) {
    reader sources=field(r,"sources"),native=field(r,"native");
    return record(r) && natural(field(r,"revision")) && list(sources,0,256,record) && (absent(native) || list(native,0,256,record)) &&
        (qa_json_size(r.json,sources.id)+qa_json_size(r.json,native.id)<=256 || fail(r,"too many component owners"));
}
static bool control(reader r) {
    reader kind=field(r,"kind");
    if (!integer(field(r,"epoch"),1,4294967295.0)) return false;
    if (is(kind,"offer")) return record(field(r,"composition")) && choices(field(r,"mode"),"singleplayer coop deathmatch") && integer(field(r,"maxClients"),1,256);
    if (is(kind,"components")) return component_header(field(r,"update")) && integer(field(field(r,"update"),"revision"),1,(double)QA_UNIFIED_SAFE_INTEGER);
    return literal(kind,"component-command") && owner(field(r,"owner")) && natural(field(r,"generation")) && list(field(r,"args"),1,128,protocol_string);
}

bool qa_unified_schema_check(qa_unified_document_kind kind, const qa_json_document *json, qa_error *error) {
    reader r={json,qa_json_root(json),error};
    if (kind==QA_UNIFIED_CHECKPOINT) return true;
    if (kind!=QA_UNIFIED_CONTROL_DOCUMENT)
        return fail(r,"typed records do not accept JSON");
    if (!literal(field(r,"schema"),"qts-control") || !integer(field(r,"version"),1,1)) return false;
    return control(field(r,"value"));
}
