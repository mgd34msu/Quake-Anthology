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
static bool fields(reader r, const char *names, check_fn check) {
    if (!record(r)) return false;
    while (*names) {
        const char *end=strchr(names,' '); size_t count=end?(size_t)(end-names):strlen(names);
        char key[64];
        if (count>=sizeof(key)) return fail(r,"internal field name too long");
        memcpy(key,names,count); key[count]=0;
        if (!check(field(r,key))) return false;
        if (!end) break;
        names=end+1;
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
static bool content(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size>=8 && text.data[0]=='q' && text.data[1]>='1' && text.data[1]<='3' && text.data[2]==':';
    size_t colons=0,start=0;
    for (size_t i=0;i<text.size;++i) if (text.data[i]==':') { if (i==start) ok=false; colons++; start=i+1; }
    ok=ok && colons==3 && start<text.size && !memchr(text.data,0,text.size);
    qa_buffer_free(&text); return ok || fail(r,"invalid content identity");
}
static bool digest(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size==71 && !memcmp(text.data,"sha256:",7);
    for (size_t i=7;ok && i<text.size;++i) ok=(text.data[i]>='0' && text.data[i]<='9') || (text.data[i]>='a' && text.data[i]<='f');
    qa_buffer_free(&text); return ok || fail(r,"invalid content digest");
}
static bool actor(reader r) { return fields(r,"slot generation",natural); }
static bool connection_actor(reader r) { return actor(r) && integer(field(r,"slot"),0,1048575); }
static bool source_registry(reader r) {
    uint64_t registry;
    return qa_json_u64(r.json,r.id,&registry,r.error) &&
        (registry != 0 || fail(r,"Source actor has no registry"));
}
static bool owner(reader r) { return record(r) && namespaced(field(r,"provider")) && integer(field(r,"generation"),1,(double)QA_UNIFIED_SAFE_INTEGER); }
static bool resource(reader r) {
    if (!record(r) || !content(field(r,"content")) || !digest(field(r,"digest")) || !natural(field(r,"byteLength"))) return false;
    qa_buffer path={0}; reader path_reader=field(r,"path");
    if (!qa_json_string(r.json,path_reader.id,&path,r.error)) return false;
    bool ok=path.size && path.data[0]!='/' && !memchr(path.data,'\\',path.size) && !memchr(path.data,0,path.size);
    if (path.size>=2 && path.data[1]==':' && ((path.data[0]>='A' && path.data[0]<='Z') || (path.data[0]>='a' && path.data[0]<='z'))) ok=false;
    size_t start=0;
    for (size_t i=0;i<=path.size;++i) if (i==path.size || path.data[i]=='/') {
        size_t length=i-start;
        if (!length || (path.data[start]=='.' && (length==1 || (length==2 && path.data[start+1]=='.')))) ok=false;
        start=i+1;
    }
    qa_buffer_free(&path); return ok || fail(r,"invalid relative resource path");
}
static bool control_resource(reader r) { return resource(r) && bounded_string(field(r,"path"),1024,true) && integer(field(r,"byteLength"),0,2147483647); }
static bool component_header(reader r) {
    reader sources=field(r,"sources"),native=field(r,"native");
    return record(r) && natural(field(r,"revision")) && list(sources,0,256,record) && (absent(native) || list(native,0,256,record)) &&
        (qa_json_size(r.json,sources.id)+qa_json_size(r.json,native.id)<=256 || fail(r,"too many component owners"));
}
static bool command_name(reader r) {
    if (!bounded_string(r,128,true)) return false;
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size>0;
    for (size_t i=0;ok && i<text.size;++i) {
        uint8_t c=text.data[i];
        ok=(c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || c=='+' || (i>0 && (c=='-' || (c>='0' && c<='9')));
    }
    qa_buffer_free(&text); return ok || fail(r,"invalid command name");
}
static bool control(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"disconnect")) return bounded_string(field(r,"reason"),1024,true);
    if (!integer(field(r,"epoch"),1,4294967295.0)) return false;
    if (is(kind,"offer")) return record(field(r,"composition")) && choices(field(r,"mode"),"singleplayer coop deathmatch") && integer(field(r,"maxClients"),1,256);
    if (is(kind,"ready")) return digest(field(r,"composition")) && protocol_string(field(r,"userinfo"));
    if (is(kind,"admitted")) return fields(r,"client actor",connection_actor) &&
        source_registry(field(field(r,"actor"),"registry")) && natural(field(r,"sourceEntity"));
    if (is(kind,"resources")) return list(field(r,"resources"),0,32768,control_resource);
    if (is(kind,"components")) return component_header(field(r,"update")) && integer(field(field(r,"update"),"revision"),1,(double)QA_UNIFIED_SAFE_INTEGER);
    if (is(kind,"component-command")) return owner(field(r,"owner")) && natural(field(r,"generation")) && list(field(r,"args"),1,128,protocol_string);
    if (is(kind,"source-command")) return protocol_string(field(r,"instance")) && record(field(r,"activation")) &&
        integer(field(field(r,"activation"),"publication"),1,(double)QA_UNIFIED_SAFE_INTEGER) &&
        natural(field(field(r,"activation"),"mapRevision")) && list(field(r,"args"),1,128,protocol_string);
    if (is(kind,"command")) return command_name(field(r,"name")) && list(field(r,"args"),0,128,protocol_string);
    return literal(kind,"userinfo") && protocol_string(field(r,"value"));
}

bool qa_unified_schema_check(qa_unified_document_kind kind, const qa_json_document *json, qa_error *error) {
    reader r={json,qa_json_root(json),error};
    if (kind==QA_UNIFIED_CHECKPOINT) return true;
    if (kind!=QA_UNIFIED_CONTROL_DOCUMENT)
        return fail(r,"typed records do not accept JSON");
    if (!literal(field(r,"schema"),"qts-control") || !integer(field(r,"version"),1,1)) return false;
    return control(field(r,"value"));
}
