#include "internal.h"

static void float_bytes(uint8_t *bytes,double number)
{
    uint64_t bits;memcpy(&bits,&number,8);
    uint64_t fraction=bits&UINT64_C(0xfffffffffffff),significand;
    uint16_t exponent=(uint16_t)((bits>>52)&0x7ff),word=(uint16_t)((bits>>48)&0x8000);
    if(!exponent && !fraction) significand=0;
    else if(!exponent) {
        unsigned highest=0;for(uint64_t probe=fraction;probe>>=1;) ++highest;
        significand=fraction<<(63-highest);exponent=(uint16_t)(highest+15309);
    } else {
        significand=(fraction|UINT64_C(0x10000000000000))<<11;
        exponent=exponent==0x7ff?0x7fff:(uint16_t)(exponent+15360);
    }
    qa_store_u64le(bytes,significand);qa_store_u16le(bytes+8,(uint16_t)(word|exponent));
}
static bool float_value(const uint8_t *bytes,double *out,qa_error *error)
{
    uint64_t significand=qa_load_u64le(bytes);uint16_t word=qa_load_u16le(bytes+8);
    uint16_t exponent=word&0x7fff;uint64_t bits=(uint64_t)(word&0x8000)<<48;
    if(exponent || significand) {
        if(!(significand&UINT64_C(0x8000000000000000))) goto unsupported;
        if(exponent==0x7fff || (exponent>=15361 && exponent<=17406)) {
            if(significand&2047) goto unsupported;
            bits|=(uint64_t)(exponent==0x7fff?0x7ff:exponent-15360)<<52;
            bits|=(significand>>11)&UINT64_C(0xfffffffffffff);
        } else if(exponent>=15309 && exponent<=15360) {
            unsigned shift=63-(unsigned)(exponent-15309);
            if(significand&((UINT64_C(1)<<shift)-1)) goto unsupported;
            bits|=significand>>shift;
        } else goto unsupported;
    }
    memcpy(out,&bits,8);return true;
unsupported:
    qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"token_t long double exceeds the binary64 source profile");return false;
}
bool script_token_store(uint8_t *bytes,const qa_script_token *token,uint32_t start,uint32_t end,qa_error *error)
{
    if(token->text.size>=1024 || (token->text.size && !token->text.data) ||
       token->kind<QA_SCRIPT_PRIMITIVE || token->kind>QA_SCRIPT_PUNCTUATION) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Token does not fit the source token_t profile");return false;
    }
    memset(bytes,0,SCRIPT_TOKEN_BYTES);
    if(token->text.size) memcpy(bytes,token->text.data,token->text.size);
    qa_store_u32le(bytes+1024,(uint32_t)token->kind);qa_store_u32le(bytes+1028,token->subtype);
    qa_store_u32le(bytes+1032,(uint32_t)token->integer);float_bytes(bytes+1036,token->number);
    qa_store_u32le(bytes+1048,start);qa_store_u32le(bytes+1052,end);
    qa_store_u32le(bytes+1056,token->location.line);qa_store_u32le(bytes+1060,token->lines_crossed);
    return true;
}
bool script_token_load(const uint8_t *bytes,size_t extent,qa_script_location location,qa_bytes whitespace,
    qa_arena *arena,qa_script_token *out,qa_error *error)
{
    uint32_t type=qa_load_u32le(bytes+1024);const uint8_t *zero=memchr(bytes,0,1024);
    if(type>QA_SCRIPT_PUNCTUATION || !zero || (extent!=SIZE_MAX && extent>=1024)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Retained lexer token has an invalid raw type or text extent");return false;
    }
    size_t terminated=(size_t)(zero-bytes);
    if(extent==SIZE_MAX || extent<terminated) extent=terminated;
    char *text=script_string(arena,bytes,extent,error);
    if(!text) return false;
    uint32_t integer=qa_load_u32le(bytes+1032);int32_t signed_integer;
    memcpy(&signed_integer,&integer,4);double number;
    if(!float_value(bytes+1036,&number,error)) return false;
    location.line=qa_load_u32le(bytes+1056);
    *out=(qa_script_token){.kind=(qa_script_token_kind)type,.subtype=qa_load_u32le(bytes+1028),
        .lines_crossed=qa_load_u32le(bytes+1060),.integer=signed_integer,.number=number,
        .text={(uint8_t *)text,extent},.leading_whitespace=whitespace,.location=location};
    return true;
}
