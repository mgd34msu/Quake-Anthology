#include "value_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct reader { const qa_json_document *json; qa_json_id id; qa_error *error; } reader;
typedef bool (*check_fn)(reader);

static reader field(reader r, const char *name) { r.id=qa_json_get(r.json,r.id,name); return r; }
static bool fail(reader r, const char *why) { qa_error_set(r.error,QA_ERROR_FORMAT,0,"unified schema: %s",why); return false; }
static bool type(reader r, qa_json_kind kind) { return qa_json_type(r.json,r.id)==kind || fail(r,"wrong field type"); }
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
static bool control(reader r) {
    reader kind=field(r,"kind");
    if (!integer(field(r,"epoch"),1,4294967295.0)) return false;
    if (is(kind,"offer")) {
        reader composition=field(r,"composition"); uint64_t generation;
        return record(composition) && record(field(composition,"composition")) &&
            qa_json_u64(r.json,field(composition,"generation").id,&generation,r.error) && generation &&
            choices(field(r,"mode"),"singleplayer coop deathmatch") && integer(field(r,"maxClients"),1,256);
    }
    return fail(r,"unknown control variant");
}

bool qa_unified_schema_check(qa_unified_document_kind kind, const qa_json_document *json, qa_error *error) {
    reader r={json,qa_json_root(json),error};
    if (kind==QA_UNIFIED_CHECKPOINT) return true;
    if (kind!=QA_UNIFIED_CONTROL_DOCUMENT)
        return fail(r,"typed records do not accept JSON");
    if (!literal(field(r,"schema"),"qts-control") || !integer(field(r,"version"),1,1)) return false;
    return control(field(r,"value"));
}
