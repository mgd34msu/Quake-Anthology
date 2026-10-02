#include "sysv_libc_private.h"
#include "sysv_libc_format_float.h"

sysv_format_binary sysv_format_decode(uint64_t bits,uint16_t high,bool extended)
{
    sysv_format_binary value = {.extended = extended};
    unsigned exponent;
    if (extended) {
        value.negative = (high&0x8000) != 0; exponent = high&0x7fff;
        if (exponent && !(bits >> 63)) { value.kind = 2; value.indefinite = true; return value; }
        value.coefficient = bits; value.exponent = (int)(exponent ? exponent : 1)-16383-63;
        value.denormal = !exponent && bits && !(bits>>63);
        if (exponent == 0x7fff) value.kind = (bits&UINT64_C(0x7fffffffffffffff)) ? 2 : 1;
    } else {
        value.negative = (bits>>63) != 0; exponent = (unsigned)((bits>>52)&0x7ff);
        uint64_t fraction = bits&UINT64_C(0xfffffffffffff);
        value.coefficient = fraction|(exponent ? UINT64_C(1)<<52 : 0);
        value.exponent = (int)(exponent ? exponent : 1)-1023-52;
        value.denormal = !exponent && fraction;
        if (exponent == 0x7ff) {
            value.kind = fraction ? 2 : 1;
            value.signaling = fraction && !(fraction & (UINT64_C(1)<<51));
            value.indefinite = value.negative && fraction == (UINT64_C(1)<<51);
        }
    }
    return value;
}
typedef struct integer_digits { uint32_t *words; size_t count,capacity; } integer_digits;
static bool multiply(integer_digits *n,uint32_t factor,qa_error *error)
{
    uint64_t carry = 0;
    for (size_t i = 0; i < n->count; ++i) {
        uint64_t value = (uint64_t)n->words[i]*factor+carry;
        n->words[i] = (uint32_t)(value%UINT64_C(1000000000)); carry = value/UINT64_C(1000000000);
    }
    if (carry) {
        if (!guest_grow((void **)&n->words,&n->capacity,n->count+1,sizeof(*n->words),error)) return false;
        n->words[n->count++] = (uint32_t)carry;
    }
    return true;
}
static size_t unsigned_digits(uint64_t value,unsigned base,char *out)
{
    char reverse[64]; size_t n = 0;
    do { unsigned digit = (unsigned)(value%base); reverse[n++] = (char)(digit < 10 ? '0'+digit : 'a'+digit-10); value /= base; } while (value);
    for (size_t i = 0; i < n; ++i) out[i] = reverse[n-i-1];
    return n;
}
static bool exact_decimal(sysv_format_binary value,char **out,size_t *length,int *scale,qa_error *error)
{
    integer_digits n = {0}; uint64_t coefficient = value.coefficient;
    do {
        if (!guest_grow((void **)&n.words,&n.capacity,n.count+1,sizeof(*n.words),error)) { free(n.words); return false; }
        n.words[n.count++] = (uint32_t)(coefficient%UINT64_C(1000000000)); coefficient /= UINT64_C(1000000000);
    } while (coefficient);
    int exponent = value.exponent; bool ok = true;
    *scale = exponent < 0 ? -exponent : 0;
    if (value.coefficient) {
        while (exponent > 0 && ok) {
            unsigned step = exponent > 29 ? 29 : (unsigned)exponent;
            ok = multiply(&n,UINT32_C(1)<<step,error); exponent -= (int)step;
        }
        while (exponent < 0 && ok) {
            unsigned step = exponent < -12 ? 12 : (unsigned)-exponent; uint32_t factor = 1;
            for (unsigned i = 0; i < step; ++i) factor *= 5;
            ok = multiply(&n,factor,error); exponent += (int)step;
        }
    } else *scale = 0;
    if (!ok) { free(n.words); return false; }
    char *digits = malloc(n.count*9+1);
    if (!digits) { free(n.words); return sysv_fail(error,QA_ERROR_MEMORY,"allocating exact guest floating decimal digits"); }
    size_t at = unsigned_digits(n.words[n.count-1],10,digits);
    for (size_t i = n.count-1; i; --i) {
        char chunk[9]; size_t bytes = unsigned_digits(n.words[i-1],10,chunk);
        memset(digits+at,'0',9-bytes); memcpy(digits+at+9-bytes,chunk,bytes); at += 9;
    }
    digits[at] = 0; free(n.words); *out = digits; *length = at; return true;
}
static bool round_up(bool remainder,int half,bool odd,bool negative,unsigned mode)
{
    return remainder && (mode == 0 ? half > 0 || (!half && odd) : mode == 1 ? negative : mode == 2 ? !negative : false);
}
static bool scaled_digits(const char *exact,size_t length,int scale,int places,
    bool negative,unsigned mode,char **out,size_t *out_length,qa_error *error)
{
    int64_t kept = (int64_t)length+places-scale;
    size_t bytes = kept > 0 ? (size_t)kept : 1;
    char *digits = malloc(bytes+2);
    if (!digits) return sysv_fail(error,QA_ERROR_MEMORY,"rounding exact guest floating digits");
    if (kept <= 0) digits[0] = '0';
    else {
        size_t copy = bytes < length ? bytes : length; memcpy(digits,exact,copy);
        if (bytes > copy) memset(digits+copy,'0',bytes-copy);
    }
    bool remainder = false; int half = -1;
    if (kept < (int64_t)length) {
        size_t first = kept > 0 ? (size_t)kept : 0;
        for (size_t i = first; i < length; ++i) if (exact[i] != '0') { remainder = true; break; }
        if (kept >= 0) {
            char leading = exact[first]; half = leading > '5' ? 1 : leading < '5' ? -1 : 0;
            if (!half) for (size_t i = first+1; i < length; ++i) if (exact[i] != '0') { half = 1; break; }
        }
    }
    if (round_up(remainder,half,((digits[bytes-1]-'0')&1) != 0,negative,mode)) {
        size_t at = bytes;
        while (at && digits[at-1] == '9') digits[--at] = '0';
        if (at) ++digits[at-1];
        else { memmove(digits+1,digits,bytes); digits[0] = '1'; ++bytes; }
    }
    digits[bytes] = 0; *out = digits; *out_length = bytes; return true;
}
static bool append(qa_buffer *out,const void *bytes,size_t count,qa_error *error)
{
    if (!count) return true;
    if (out->size > SIZE_MAX-count) return sysv_fail(error,QA_ERROR_MEMORY,"guest floating format extent overflow");
    uint8_t *data = realloc(out->data,out->size+count);
    if (!data) return sysv_fail(error,QA_ERROR_MEMORY,"allocating guest floating format output");
    memcpy(data+out->size,bytes,count); out->data = data; out->size += count; return true;
}
static bool zeros(qa_buffer *out,size_t count,qa_error *error)
{
    if (!count) return true;
    if (out->size > SIZE_MAX-count) return false;
    uint8_t *data = realloc(out->data,out->size+count);
    if (!data) return sysv_fail(error,QA_ERROR_MEMORY,"padding guest floating digits");
    memset(data+out->size,'0',count); out->data = data; out->size += count; return true;
}
static bool fixed(qa_buffer *out,const char *digits,size_t count,size_t places,bool point,qa_error *error)
{
    if (!places) return append(out,digits,count,error) && (!point || append(out,".",1,error));
    size_t padded = count > places ? count : places+1, head = padded-places, leading = padded-count;
    if (leading) {
        if (!zeros(out,head,error) || !append(out,".",1,error) || !zeros(out,leading-head,error)) return false;
        return append(out,digits,count,error);
    }
    return append(out,digits,head,error) && append(out,".",1,error) && append(out,digits+head,places,error);
}
static bool suffix(qa_buffer *out,int exponent,char marker,size_t minimum,qa_error *error)
{
    char digits[16],head[] = {marker,exponent < 0 ? '-' : '+'};
    unsigned magnitude = exponent < 0 ? (unsigned)-exponent : (unsigned)exponent;
    size_t count = unsigned_digits(magnitude,10,digits);
    return append(out,head,2,error) && zeros(out,count < minimum ? minimum-count : 0,error) && append(out,digits,count,error);
}
static bool hex_float(sysv_format_binary value,int precision,bool alternate,unsigned mode,bool windows,qa_buffer *out,qa_error *error)
{
    int fraction = value.extended ? 60 : 52;
    size_t natural = (size_t)fraction/4, places = precision < 0 ? natural : (size_t)precision;
    int exponent = !value.coefficient ? 0 : value.denormal ? value.extended ? -16382 : -1022 : value.exponent+(value.extended ? 63 : 52);
    if (value.extended && value.coefficient) exponent -= 3;
    uint64_t coefficient = value.coefficient; size_t trailing = 0;
    if (places < natural) {
        unsigned shift = (unsigned)((natural-places)*4); uint64_t mask = (UINT64_C(1)<<shift)-1;
        uint64_t remainder = coefficient&mask, half = UINT64_C(1)<<(shift-1); coefficient >>= shift;
        if (round_up(remainder != 0,remainder > half ? 1 : remainder < half ? -1 : 0,(coefficient&1) != 0,value.negative,mode)) ++coefficient;
    } else trailing = places-natural;
    char digits[32]; size_t count = unsigned_digits(coefficient,16,digits);
    if (count+trailing > places+1) {
        count = 1; digits[0] = '1'; trailing = places; exponent += 4;
    }
    size_t leading = places+1-count-trailing;
    qa_buffer full = {0}; bool ok = zeros(&full,leading,error) && append(&full,digits,count,error) && zeros(&full,trailing,error);
    if (ok) {
        size_t tail = places;
        if (precision < 0 && !windows) while (tail && full.data[tail] == '0') --tail;
        ok = append(out,"0x",2,error) && append(out,full.data,1,error) &&
            (!(tail || alternate) || (append(out,".",1,error) && append(out,full.data+1,tail,error))) && suffix(out,exponent,'p',1,error);
    }
    qa_buffer_free(&full); return ok;
}
bool sysv_format_float_dialect(sysv_format_binary value,char conversion,int precision,bool alternate,
    unsigned mode,bool windows,qa_buffer *out,qa_error *error)
{
    if (!out || out->data || out->size || precision > SYSV_MAX_STRING || mode > 3)
        return sysv_fail(error,QA_ERROR_ARGUMENT,"invalid guest floating format request");
    bool upper = conversion >= 'A' && conversion <= 'Z'; char lower = upper ? (char)(conversion-'A'+'a') : conversion;
    bool ok;
    if (value.kind) {
        const char *text = value.kind == 1 ? "inf" : windows && value.signaling ? "nan(snan)" :
            windows && value.indefinite ? "nan(ind)" : "nan";
        ok = append(out,text,strlen(text),error);
    } else if (lower == 'a') ok = hex_float(value,precision,alternate,mode,windows,out,error);
    else {
        char *exact = NULL,*digits = NULL; size_t length,count; int scale;
        if (!exact_decimal(value,&exact,&length,&scale,error)) return false;
        int places = precision < 0 ? 6 : precision;
        int exponent = value.coefficient ? (int)length-1-scale : 0;
        if (lower == 'f') {
            ok = scaled_digits(exact,length,scale,places,value.negative,mode,&digits,&count,error) &&
                fixed(out,digits,count,(size_t)places,alternate,error);
        } else {
            size_t significant = lower == 'e' ? (size_t)places+1 : places ? (size_t)places : 1;
            ok = scaled_digits(exact,length,scale,(int)significant-1-exponent,value.negative,mode,&digits,&count,error);
            if (ok) {
                if (count > significant) { ++exponent; count = significant; }
                if (count < significant) {
                    char *expanded = realloc(digits,significant+1);
                    if (!expanded) ok = sysv_fail(error,QA_ERROR_MEMORY,"padding significant guest floating digits");
                    else { memmove(expanded+significant-count,expanded,count); memset(expanded,'0',significant-count); digits = expanded; count = significant; }
                }
                if (ok && (lower == 'e' || exponent < -4 || exponent >= (int)significant)) {
                    size_t tail = count-1;
                    if (lower == 'g' && !alternate) while (tail && digits[tail] == '0') --tail;
                    ok = append(out,digits,1,error) && (!(tail || alternate) ||
                        (append(out,".",1,error) && append(out,digits+1,tail,error))) && suffix(out,exponent,'e',2,error);
                } else if (ok) {
                    size_t decimal_places = (size_t)((int)significant-1-exponent);
                    ok = fixed(out,digits,count,decimal_places,alternate,error);
                    if (ok && !alternate) {
                        bool has_point = memchr(out->data,'.',out->size) != NULL;
                        if (has_point) { while (out->size && out->data[out->size-1] == '0') --out->size;
                            if (out->size && out->data[out->size-1] == '.') --out->size; }
                    }
                }
            }
        }
        free(exact); free(digits);
    }
    if (!ok) { qa_buffer_free(out); return false; }
    if (upper) for (size_t i = 0; i < out->size; ++i) if (out->data[i] >= 'a' && out->data[i] <= 'z') out->data[i] -= 'a'-'A';
    return true;
}
bool sysv_format_float(sysv_format_binary value,char conversion,int precision,bool alternate,
    unsigned mode,qa_buffer *out,qa_error *error)
{ return sysv_format_float_dialect(value,conversion,precision,alternate,mode,false,out,error); }
