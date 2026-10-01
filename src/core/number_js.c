#include "qa/text.h"
#include <fenv.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Decimal candidates and the exact binary64 value share an integer scale.
 * 2560 bits cover both binary64 endpoints and 17-digit decimal candidates,
 * including their powers of five for subnormal denominators. */
typedef struct decimal_integer { uint32_t words[80]; } decimal_integer;
static decimal_integer integer(uint64_t value)
{ decimal_integer out={{(uint32_t)value,(uint32_t)(value>>32)}}; return out; }
static bool multiply5(decimal_integer *value)
{
    uint64_t carry=0;
    for (size_t i=0;i<80;++i) {
        uint64_t product=(uint64_t)value->words[i]*5+carry;
        value->words[i]=(uint32_t)product; carry=product>>32;
    }
    return carry==0;
}
static bool shift(decimal_integer *value,unsigned bits)
{
    decimal_integer out={{0}}; unsigned word=bits/32,part=bits%32;
    for (unsigned i=0;i<80;++i) {
        if (!value->words[i]) continue;
        uint64_t expanded=(uint64_t)value->words[i]<<part;
        if (i+word>=80 || ((expanded>>32) && i+word+1>=80)) return false;
        out.words[i+word]|=(uint32_t)expanded;
        if (expanded>>32) out.words[i+word+1]|=(uint32_t)(expanded>>32);
    }
    *value=out; return true;
}
static int compare(const decimal_integer *a,const decimal_integer *b)
{
    for (size_t i=80;i;--i)
        if (a->words[i-1]!=b->words[i-1]) return a->words[i-1]<b->words[i-1]?-1:1;
    return 0;
}
static decimal_integer distance(const decimal_integer *a,const decimal_integer *b)
{
    if (compare(a,b)<0) { const decimal_integer *swap=a; a=b; b=swap; }
    decimal_integer out={{0}}; uint64_t borrow=0;
    for (size_t i=0;i<80;++i) {
        uint64_t left=a->words[i],right=(uint64_t)b->words[i]+borrow;
        out.words[i]=(uint32_t)(left-right); borrow=left<right;
    }
    return out;
}
static bool exact_distance(uint64_t mantissa,int binary_power,uint64_t coefficient,int power,
    decimal_integer *out)
{
    decimal_integer actual=integer(mantissa),decimal=integer(coefficient);
    int decimal_power=power;
    if (power>=0) {
        for (int i=0;i<power;++i) if (!multiply5(&decimal)) return false;
    } else {
        for (int i=0;i<-power;++i) if (!multiply5(&actual)) return false;
    }
    int common=binary_power<decimal_power?binary_power:decimal_power;
    if (!shift(&actual,(unsigned)(binary_power-common)) ||
        !shift(&decimal,(unsigned)(decimal_power-common))) return false;
    *out=distance(&actual,&decimal); return true;
}
static bool scientific(uint64_t coefficient,int power,char out[32])
{
    int count=snprintf(out,32,"%llue%+d",(unsigned long long)coefficient,power);
    return count>0 && count<32;
}
static bool number_text(uint64_t coefficient,int power,bool negative,char out[32])
{
    while (coefficient%10==0) { coefficient/=10; ++power; }
    char digits[24]; int count=snprintf(digits,sizeof(digits),"%llu",(unsigned long long)coefficient);
    if (count<1 || (size_t)count>=sizeof(digits)) return false;
    int position=count+power; size_t used=0;
    if (negative) out[used++]='-';
    if (count<=position && position<=21) {
        memcpy(out+used,digits,(size_t)count); used+=(size_t)count;
        for (int i=count;i<position;++i) out[used++]='0';
    } else if (position>0 && position<=21) {
        memcpy(out+used,digits,(size_t)position); used+=(size_t)position; out[used++]='.';
        memcpy(out+used,digits+position,(size_t)(count-position)); used+=(size_t)(count-position);
    } else if (position>-6 && position<=0) {
        out[used++]='0'; out[used++]='.';
        for (int i=position;i<0;++i) out[used++]='0';
        memcpy(out+used,digits,(size_t)count); used+=(size_t)count;
    } else {
        out[used++]=digits[0];
        if (count>1) { out[used++]='.'; memcpy(out+used,digits+1,(size_t)count-1); used+=(size_t)count-1; }
        int tail=snprintf(out+used,32-used,"e%+d",position-1);
        if (tail<1 || (size_t)tail>=32-used) return false;
        used+=(size_t)tail;
    }
    out[used]=0; return true;
}
static bool finite_text(double value,char out[32],qa_error *error)
{
    char initial[32]; double magnitude=fabs(value);
    if (!qa_format_number(magnitude,initial,error)) return false;
    uint64_t full=0; unsigned total=0,before=0,leading=0; bool dot=false,started=false;
    const char *cursor=initial;
    for (;*cursor && *cursor!='e' && *cursor!='E';++cursor) {
        if (*cursor=='.') { dot=true; continue; }
        if (*cursor<'0' || *cursor>'9') return false;
        if (!dot) ++before;
        if (!started && *cursor=='0') { ++leading; continue; }
        started=true; full=full*10+(unsigned)(*cursor-'0'); ++total;
    }
    int exponent=(int)before-(int)leading-1;
    if (*cursor) {
        ++cursor; bool minus=*cursor=='-'; if (*cursor=='-' || *cursor=='+') ++cursor;
        int supplied=0;
        for (;*cursor;++cursor) {
            if (*cursor<'0' || *cursor>'9') return false;
            supplied=supplied*10+*cursor-'0';
        }
        exponent+=minus?-supplied:supplied;
    }
    if (!full || !total || total>17) return false;
    uint64_t bits; memcpy(&bits,&magnitude,sizeof(bits));
    unsigned encoded=(unsigned)((bits>>52)&2047);
    uint64_t mantissa=bits&UINT64_C(0xfffffffffffff);
    if (encoded) mantissa|=UINT64_C(1)<<52;
    int binary_power=encoded?(int)encoded-1075:-1074;
    uint64_t divisor=1;
    for (unsigned i=1;i<total;++i) divisor*=10;
    for (unsigned precision=1;precision<=total;++precision,divisor/=10) {
        uint64_t floor=full/divisor,best=0; bool found=false;
        decimal_integer best_distance={{0}}; int power=exponent-(int)precision+1;
        for (int delta=-1;delta<=1;++delta) {
            uint64_t coefficient=delta<0?floor-1:floor+(unsigned)delta;
            char candidate[32]; double parsed; qa_error ignored={0};
            if (!coefficient || !scientific(coefficient,power,candidate) ||
                !qa_parse_number((qa_bytes){(const uint8_t *)candidate,strlen(candidate)},&parsed,&ignored) ||
                parsed!=magnitude) continue;
            decimal_integer difference;
            if (!exact_distance(mantissa,binary_power,coefficient,power,&difference)) return false;
            int order=found?compare(&difference,&best_distance):-1;
            if (!found || order<0 || (!order && !(coefficient&1u))) {
                found=true; best=coefficient; best_distance=difference;
            }
        }
        if (found) return number_text(best,power,signbit(value),out);
    }
    return false;
}
bool qa_format_ecmascript_number(double value,char out[32],qa_error *error)
{
    _Static_assert(sizeof(double)==8 && FLT_RADIX==2 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024,
        "ECMAScript number formatting requires binary64");
    if (!out) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing ECMAScript number output"); return false; }
    if (!isfinite(value)) { strcpy(out,isnan(value)?"NaN":signbit(value)?"-Infinity":"Infinity"); return true; }
    if (value==0) { strcpy(out,"0"); return true; }
    int rounding=fegetround();
    if (rounding<0 || fesetround(FE_TONEAREST)!=0) {
        qa_error_set(error,QA_ERROR_IO,0,"Selecting ECMAScript numeric rounding"); return false;
    }
    bool ok=finite_text(value,out,error);
    bool restored=fesetround(rounding)==0;
    if (!ok || !restored) {
        out[0]=0;
        qa_error_set(error,!restored?QA_ERROR_IO:QA_ERROR_FORMAT,0,"Formatting ECMAScript number");
        return false;
    }
    return true;
}

static unsigned radix_digit(unsigned char c)
{
    return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:
        c>='A'&&c<='F'?c-'A'+10:UINT_MAX;
}
static bool radix_number(qa_bytes input,unsigned radix,double *out)
{
    unsigned width=radix==16?4:radix==8?3:1,total=0;
    uint64_t significand=0; bool guard=false,sticky=false;
    for (size_t i=2;i<input.size;++i) {
        unsigned digit=radix_digit(input.data[i]);
        if (digit>=radix) return false;
        for (unsigned bit=width;bit;--bit) {
            bool value=(digit&(1u<<(bit-1)))!=0;
            if (!total && !value) continue;
            if (total<53) significand=(significand<<1)|(unsigned)value;
            else if (total==53) guard=value;
            else sticky|=value;
            if (total<1025) ++total;
        }
    }
    if (total<=53) *out=(double)significand;
    else if (total>1024) *out=INFINITY;
    else {
        if (guard && (sticky || (significand&1u))) ++significand;
        *out=scalbn((double)significand,(int)total-53);
    }
    return true;
}
static bool parse_js(qa_bytes input,double *out,qa_error *error)
{
    size_t cursor=0,start=SIZE_MAX,end=0; uint32_t scalar;
    while (cursor<input.size) {
        size_t begin=cursor;
        if (!qa_utf8_next(input,&cursor,&scalar)) return false;
        if (!qa_unicode_whitespace(scalar)) {
            if (start==SIZE_MAX) start=begin;
            end=cursor;
        }
    }
    if (start==SIZE_MAX) { *out=0; return true; }
    input=(qa_bytes){input.data+start,end-start};
    size_t at=0;
    bool negative=input.data[0]=='-',sign=negative || input.data[0]=='+';
    if (sign) ++at;
    if (input.size-at==8 && !memcmp(input.data+at,"Infinity",8)) {
        *out=negative?-INFINITY:INFINITY; return true;
    }
    if (!sign && input.size>2 && input.data[0]=='0') {
        unsigned radix=input.data[1]=='x'||input.data[1]=='X'?16:
            input.data[1]=='b'||input.data[1]=='B'?2:input.data[1]=='o'||input.data[1]=='O'?8:0;
        if (radix) return radix_number(input,radix,out);
    }
    size_t digits=0;
    while (at<input.size && input.data[at]>='0' && input.data[at]<='9') { ++at; ++digits; }
    if (at<input.size && input.data[at]=='.') {
        ++at;
        while (at<input.size && input.data[at]>='0' && input.data[at]<='9') { ++at; ++digits; }
    }
    if (!digits) return false;
    if (at<input.size && (input.data[at]=='e' || input.data[at]=='E')) {
        ++at;
        if (at<input.size && (input.data[at]=='+' || input.data[at]=='-')) ++at;
        size_t begin=at;
        while (at<input.size && input.data[at]>='0' && input.data[at]<='9') ++at;
        if (at==begin) return false;
    }
    return at==input.size && qa_parse_number(input,out,error);
}
bool qa_parse_ecmascript_number(qa_bytes input,double *out,qa_error *error)
{
    if (!out || (input.size && !input.data) || input.size==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid ECMAScript numeric input"); return false;
    }
    int rounding=fegetround();
    if (rounding<0 || fesetround(FE_TONEAREST)!=0) {
        qa_error_set(error,QA_ERROR_IO,0,"Selecting ECMAScript numeric rounding"); return false;
    }
    double value=0; bool ok=parse_js(input,&value,error);
    bool restored=fesetround(rounding)==0;
    if (!ok || !restored) {
        if (!restored || !error || error->code==QA_OK)
            qa_error_set(error,restored?QA_ERROR_FORMAT:QA_ERROR_IO,0,"Parsing ECMAScript numeric text");
        return false;
    }
    *out=value; return true;
}
