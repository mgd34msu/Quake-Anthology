#include "value_internal.h"

#include <fenv.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_unified_append(qa_unified_builder *b, const void *data, size_t size, qa_error *error) {
    if (size>b->maximum-b->size || (size && !data)) {
        qa_error_set(error,QA_ERROR_FORMAT,b->size,"unified value exceeds byte limit"); return false;
    }
    size_t needed=b->size+size;
    if (needed>b->capacity) {
        size_t capacity=b->capacity?b->capacity:256;
        while (capacity<needed) {
            if (capacity>b->maximum/2) { capacity=b->maximum; break; }
            capacity*=2;
        }
        uint8_t *next=realloc(b->data,capacity);
        if (!next) { qa_error_set(error,QA_ERROR_MEMORY,b->size,"allocating unified value"); return false; }
        b->data=next; b->capacity=capacity;
    }
    if (size) memcpy(b->data+b->size,data,size);
    b->size=needed; return true;
}

bool qa_unified_number_text(double value, char out[32], qa_error *error) {
    if (!isfinite(value)) { qa_error_set(error,QA_ERROR_FORMAT,0,"non-finite composition number"); return false; }
    if (value==0) { strcpy(out,signbit(value)?"-0":"0"); return true; }
    bool negative=signbit(value)!=0;
    double magnitude=fabs(value);
    char scientific[128];
    char digits[20]; size_t count=0; int exponent=0;
    fenv_t environment;
    if (feholdexcept(&environment)) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"cannot select binary64 serialization rounding"); return false;
    }
    if (fesetround(FE_TONEAREST)) {
        (void)fesetenv(&environment);
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"cannot select binary64 serialization rounding"); return false;
    }
    const char *decimal=localeconv()->decimal_point;
    size_t decimal_size=strlen(decimal);
    /* Powers of two have asymmetric rounding intervals. Check the nearest
     * decimal first, then its adjacent decimal grid points, before increasing
     * precision. This also retains ties-to-even selection of the nearest. */
    for (int precision=0;precision<17 && !count;++precision) {
        int length=snprintf(scientific,sizeof(scientific),"%.*e",precision,magnitude);
        if (length<0 || (size_t)length>=sizeof(scientific)) {
            (void)fesetenv(&environment);
            qa_error_set(error,QA_ERROR_FORMAT,0,"formatting unified number"); return false;
        }
        const char *exponent_at=strchr(scientific,'e');
        if (!exponent_at) {
            (void)fesetenv(&environment);
            qa_error_set(error,QA_ERROR_FORMAT,0,"invalid formatted unified number"); return false;
        }
        int scale=(int)strtol(exponent_at+1,NULL,10)-precision;
        uint64_t mantissa=0;
        for (const char *p=scientific;p<exponent_at;) {
            if (*p>='0' && *p<='9') mantissa=mantissa*10u+(uint64_t)(*p++-'0');
            else if (decimal_size && (size_t)(exponent_at-p)>=decimal_size && !memcmp(p,decimal,decimal_size)) p+=decimal_size;
            else {
                (void)fesetenv(&environment);
                qa_error_set(error,QA_ERROR_FORMAT,0,"invalid numeric locale"); return false;
            }
        }
        for (unsigned neighbor=0;neighbor<3;++neighbor) {
            uint64_t candidate=neighbor==0?mantissa:neighbor==1?mantissa-1u:mantissa+1u;
            char decimal_candidate[64];
            int written=snprintf(decimal_candidate,sizeof(decimal_candidate),"%" PRIu64 "e%d",candidate,scale);
            if (written<0 || (size_t)written>=sizeof(decimal_candidate)) continue;
            char *end=NULL;
            if (strtod(decimal_candidate,&end)!=magnitude || !end || *end) continue;
            written=snprintf(digits,sizeof(digits),"%" PRIu64,candidate);
            if (written<=0 || (size_t)written>=sizeof(digits)) continue;
            count=(size_t)written; exponent=scale+written-1; break;
        }
    }
    if (fesetenv(&environment)) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"cannot restore numeric environment"); return false; }
    if (!count) { qa_error_set(error,QA_ERROR_FORMAT,0,"unified number failed exact conversion"); return false; }
    while (count>1 && digits[count-1]=='0') count--;
    size_t used=0;
    if (negative) out[used++]='-';
    if (exponent>=-6 && exponent<21) {
        if (exponent<0) {
            out[used++]='0'; out[used++]='.';
            for (int i=-1;i>exponent;--i) out[used++]='0';
            memcpy(out+used,digits,count); used+=count;
        } else {
            size_t integer_digits=(size_t)exponent+1;
            for (size_t i=0;i<integer_digits;++i) out[used++]=i<count?digits[i]:'0';
            if (count>integer_digits) {
                out[used++]='.';
                memcpy(out+used,digits+integer_digits,count-integer_digits); used+=count-integer_digits;
            }
        }
    } else {
        out[used++]=digits[0];
        if (count>1) { out[used++]='.'; memcpy(out+used,digits+1,count-1); used+=count-1; }
        out[used++]='e'; out[used++]=exponent<0?'-':'+';
        unsigned power=(unsigned)(exponent<0?-exponent:exponent);
        if (power>=100) out[used++]=(char)('0'+power/100);
        if (power>=10) out[used++]=(char)('0'+(power/10)%10);
        out[used++]=(char)('0'+power%10);
    }
    out[used]='\0'; return true;
}

typedef struct canonical_member {
    qa_buffer key;
    qa_json_id value;
    size_t ordinal;
} canonical_member;

/* UTF-8 is validated by qa_json. Compare UTF-16 code units, as JS sort does,
 * including the ordering of supplementary characters before BMP private use. */
typedef struct utf16_cursor { qa_bytes bytes; size_t cursor; uint16_t low; } utf16_cursor;
static uint32_t next_unit(utf16_cursor *s) {
    if (s->low) { uint16_t low=s->low; s->low=0; return low; }
    if (s->cursor==s->bytes.size) return UINT32_MAX;
    uint32_t c=s->bytes.data[s->cursor++];
    unsigned continuation=0;
    if (c>=0xf0) { c&=7u; continuation=3; }
    else if (c>=0xe0) { c&=15u; continuation=2; }
    else if (c>=0xc0) { c&=31u; continuation=1; }
    while (continuation--) c=(c<<6)|(s->bytes.data[s->cursor++]&63u);
    if (c>=0x10000) { c-=0x10000; s->low=(uint16_t)(0xdc00u+(c&1023u)); return 0xd800u+(c>>10); }
    return c;
}

static int compare_members(const void *left, const void *right) {
    const canonical_member *a=left,*b=right;
    utf16_cursor x={{a->key.data,a->key.size},0,0},y={{b->key.data,b->key.size},0,0};
    for (;;) {
        uint32_t u=next_unit(&x),v=next_unit(&y);
        if (u!=v) return u==UINT32_MAX?-1:v==UINT32_MAX?1:u<v?-1:1;
        if (u==UINT32_MAX) return a->ordinal<b->ordinal?-1:a->ordinal>b->ordinal?1:0;
    }
}

static bool same_key(const canonical_member *a, const canonical_member *b) {
    return a->key.size==b->key.size && !memcmp(a->key.data,b->key.data,a->key.size);
}

static bool canonical_string(qa_bytes text, qa_unified_builder *b, qa_error *error) {
    static const char hex[]="0123456789abcdef";
    if (!qa_unified_append(b,"\"",1,error)) return false;
    size_t start=0;
    for (size_t i=0;i<text.size;++i) {
        uint8_t c=text.data[i];
        if (c>=32 && c!='"' && c!='\\') continue;
        if (!qa_unified_append(b,text.data+start,i-start,error)) return false;
        char escape[6]={'\\',0,0,0,0,0}; size_t count=2;
        switch (c) {
        case '"': escape[1]='"'; break;
        case '\\': escape[1]='\\'; break;
        case '\b': escape[1]='b'; break;
        case '\t': escape[1]='t'; break;
        case '\n': escape[1]='n'; break;
        case '\f': escape[1]='f'; break;
        case '\r': escape[1]='r'; break;
        default: escape[1]='u'; escape[2]='0'; escape[3]='0'; escape[4]=hex[c>>4]; escape[5]=hex[c&15u]; count=6; break;
        }
        if (!qa_unified_append(b,escape,count,error)) return false;
        start=i+1;
    }
    return qa_unified_append(b,text.data+start,text.size-start,error) && qa_unified_append(b,"\"",1,error);
}

bool qa_unified_canonical(const qa_json_document *d, qa_json_id id, qa_unified_builder *b,
                           unsigned depth, qa_error *error) {
    if (depth>128) { qa_error_set(error,QA_ERROR_FORMAT,0,"composition nesting exceeds limit"); return false; }
    qa_json_kind kind=qa_json_type(d,id);
    if (kind==QA_JSON_NULL) return qa_unified_append(b,"null",4,error);
    if (kind==QA_JSON_BOOL) {
        bool value;
        return qa_json_bool(d,id,&value,error) && qa_unified_append(b,value?"true":"false",value?4:5,error);
    }
    if (kind==QA_JSON_NUMBER) {
        double value; char number[32];
        fenv_t environment;
        if (feholdexcept(&environment)) {
            qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"cannot select composition parsing rounding"); return false;
        }
        if (fesetround(FE_TONEAREST)) {
            (void)fesetenv(&environment);
            qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"cannot select composition parsing rounding"); return false;
        }
        bool ok=qa_json_number(d,id,&value,error) && qa_unified_number_text(value,number,error);
        if (fesetenv(&environment)) {
            qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"cannot restore composition parsing environment"); return false;
        }
        return ok && qa_unified_append(b,number,strlen(number),error);
    }
    if (kind==QA_JSON_STRING) {
        qa_buffer text={0};
        bool ok=qa_json_string(d,id,&text,error) && canonical_string((qa_bytes){text.data,text.size},b,error);
        qa_buffer_free(&text); return ok;
    }
    if (kind==QA_JSON_ARRAY) {
        if (!qa_unified_append(b,"[",1,error)) return false;
        size_t count=qa_json_size(d,id);
        for (size_t i=0;i<count;++i)
            if ((i && !qa_unified_append(b,",",1,error)) || !qa_unified_canonical(d,qa_json_at(d,id,i),b,depth+1,error)) return false;
        return qa_unified_append(b,"]",1,error);
    }
    if (kind==QA_JSON_OBJECT) {
        size_t count=qa_json_size(d,id);
        if (count>SIZE_MAX/sizeof(canonical_member)) return false;
        canonical_member *members=calloc(count?count:1,sizeof(*members));
        if (!members) { qa_error_set(error,QA_ERROR_MEMORY,0,"sorting composition members"); return false; }
        bool ok=true;
        for (size_t i=0;i<count;++i) {
            members[i].value=qa_json_at(d,id,i); members[i].ordinal=i;
            if (!qa_json_string(d,qa_json_key_at(d,id,i),&members[i].key,error)) { ok=false; break; }
        }
        if (ok) {
            qsort(members,count,sizeof(*members),compare_members);
            ok=qa_unified_append(b,"{",1,error);
            bool first=true;
            for (size_t i=0;ok && i<count;++i) {
                if (i+1<count && same_key(&members[i],&members[i+1])) continue;
                ok=(first || qa_unified_append(b,",",1,error))
                    && canonical_string((qa_bytes){members[i].key.data,members[i].key.size},b,error)
                    && qa_unified_append(b,":",1,error)
                    && qa_unified_canonical(d,members[i].value,b,depth+1,error);
                first=false;
            }
            if (ok) ok=qa_unified_append(b,"}",1,error);
        }
        for (size_t i=0;i<count;++i) qa_buffer_free(&members[i].key);
        free(members); return ok;
    }
    qa_error_set(error,QA_ERROR_FORMAT,0,"invalid composition value"); return false;
}

bool qa_unified_composition_create(qa_bytes json, qa_unified_composition *out, qa_error *error) {
    if (!out || json.size>32u*1024u*1024u) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid composition destination or size"); return false;
    }
    qa_json_document *d=NULL;
    if (!qa_json_parse(json,&d,error)) return false;
    qa_unified_builder b={.maximum=32u*1024u*1024u};
    qa_json_id root=qa_json_root(d);
    double version;
    bool ok=qa_json_type(d,root)==QA_JSON_OBJECT &&
        qa_json_number(d,qa_json_get(d,root,"schemaVersion"),&version,error) && version==1 &&
        qa_json_type(d,qa_json_get(d,root,"recipe"))==QA_JSON_OBJECT &&
        qa_json_type(d,qa_json_get(d,root,"snapshotSchema"))==QA_JSON_STRING &&
        qa_json_type(d,qa_json_get(d,root,"actorConfigurations"))==QA_JSON_ARRAY;
    if (!ok) qa_error_set(error,QA_ERROR_FORMAT,0,"invalid unified composition schema");
    else ok=qa_unified_canonical(d,root,&b,0,error);
    qa_json_destroy(d);
    if (!ok) { free(b.data); return false; }
    qa_unified_composition result={.canonical={b.data,b.size}};
    qa_sha256((qa_bytes){b.data,b.size},&result.digest); *out=result; return true;
}

void qa_unified_composition_free(qa_unified_composition *c) {
    if (!c) return;
    qa_buffer_free(&c->canonical); memset(&c->digest,0,sizeof(c->digest));
}
