#include "sysv_libc_private.h"
#include "sysv_libc_format_float.h"
#include "sysv_libc_format.h"
#include "sysv_scan.h"
#include "qa/binary.h"

typedef enum format_type { FT_NONE, FT_INT, FT_I64, FT_POINTER, FT_DOUBLE, FT_EXTENDED } format_type;
typedef enum format_length { FL_NONE, FL_HH, FL_H, FL_L, FL_LL, FL_EXTENDED, FL_J, FL_Z, FL_T, FL_Q } format_length;
enum { FF_LEFT = 1, FF_PLUS = 2, FF_SPACE = 4, FF_ALTERNATE = 8, FF_ZERO = 16 };
typedef struct format_amount { bool argument; size_t index; int literal; } format_amount;
typedef struct format_token {
    size_t start,bytes,index;
    char code;
    unsigned flags;
    format_length length;
    format_type type;
    format_amount width,precision;
    bool has_precision;
} format_token;
typedef struct format_value { uint64_t integer; sysv_format_binary floating; } format_value;
typedef struct format_reader { uint64_t descriptor,cursor,registers; uint32_t gp,fp; bool initialized; } format_reader;
typedef struct formatter {
    guest_sysv_runtime *runtime;
    qa_native_guest *guest;
    qa_native_target target;
    qa_buffer output;
    size_t output_capacity, supplied_count;
    bool supplied, owned_output;
    guest_abi_plan *entry_plan;
    size_t fixed_count;
    uint64_t destination,capacity,format,arguments,used,total;
    qa_buffer text;
    format_token *tokens;
    size_t token_count,token_capacity,sequence;
    format_type *types;
    size_t type_count, type_capacity;
    size_t distinct;
    bool positional,sequential,conflict,fortify;
    format_value *values;
    format_reader reader;
    unsigned rounding;
    int format_errno;
} formatter;
static void formatter_free(formatter *);
static bool invalid(formatter *f,int code)
{ f->format_errno = code; return false; }
static unsigned integer_bits(formatter *f,format_length length)
{
    switch (length) {
    case FL_HH: return 8;
    case FL_H: return 16;
    case FL_LL: case FL_J: case FL_Q: return 64;
    case FL_EXTENDED: return f->target.os == QA_NATIVE_OS_WINDOWS ? 32 : 64;
    case FL_L: return f->target.os == QA_NATIVE_OS_WINDOWS ? 32 : f->target.pointer_bytes*8;
    case FL_Z: case FL_T: return f->target.pointer_bytes*8;
    default: return 32;
    }
}
static bool format_unsigned(formatter *f, uint64_t address, size_t bytes, uint64_t *out, qa_error *error)
{
    if (f->runtime) return sysv_unsigned(f->runtime, address, bytes, out, error);
    uint8_t data[8];
    if (!bytes || bytes > sizeof(data) || !qa_native_guest_read(f->guest, address, data, bytes, error)) return false;
    *out = 0;
    for (size_t i = 0; i < bytes; ++i) *out |= (uint64_t)data[i] << (i*8);
    return true;
}
static bool format_store(formatter *f, uint64_t address, size_t bytes, uint64_t value, qa_error *error)
{
    if (f->runtime) return sysv_store(f->runtime, address, bytes, value, error);
    uint8_t data[8];
    if (!bytes || bytes > sizeof(data)) return sysv_fail(error, QA_ERROR_ARGUMENT, "invalid guest format scalar width");
    for (size_t i = 0; i < bytes; ++i) data[i] = (uint8_t)(value >> (i*8));
    return qa_native_guest_write(f->guest, address, (qa_bytes){data, bytes}, error);
}
static bool format_string(formatter *f, qa_error *error)
{
    if (f->runtime) {
        bool terminated;
        return sysv_string(f->runtime, f->format, SYSV_MAX_STRING, &f->text, &terminated, error) &&
            (terminated || sysv_fail(error, QA_ERROR_ARGUMENT, "guest format is not terminated"));
    }
    size_t capacity = 0;
    for (size_t i = 0; i < SYSV_MAX_STRING; ++i) {
        uint64_t byte;
        if (f->format > UINT64_MAX-i || !format_unsigned(f, f->format+i, 1, &byte, error)) return false;
        if (!byte) return true;
        if (!guest_grow((void **)&f->text.data, &capacity, f->text.size+1, 1, error)) return false;
        f->text.data[f->text.size++] = (uint8_t)byte;
    }
    return sysv_fail(error, QA_ERROR_ARGUMENT, "guest format exceeds its terminated string limit");
}
static bool number(formatter *f,size_t *at,int *out)
{
    uint64_t value = 0;
    while (*at < f->text.size && f->text.data[*at] >= '0' && f->text.data[*at] <= '9') {
        unsigned digit = f->text.data[(*at)++]-'0';
        if (value > ((uint64_t)INT32_MAX-digit)/10) return invalid(f,75);
        value = value*10+digit;
    }
    *out = (int)value; return true;
}
static bool position(formatter *f,size_t *at,size_t *out)
{
    size_t start = *at; int value;
    if (!number(f,at,&value)) return false;
    *out = SIZE_MAX;
    if (*at >= f->text.size || f->text.data[*at] != '$') { *at = start; return true; }
    ++*at;
    if (f->target.os == QA_NATIVE_OS_WINDOWS || value < 1 || value > 4096) return invalid(f,22);
    *out = (size_t)value-1; return true;
}
static bool reference(formatter *f,format_type type,size_t supplied,size_t *out,qa_error *error)
{
    if (supplied == SIZE_MAX) { f->sequential = true; *out = f->sequence++; }
    else {
        f->positional = true; *out = supplied;
    }
    if (*out == SIZE_MAX || !guest_grow((void **)&f->types, &f->type_capacity,
        *out+1, sizeof(*f->types), error)) return false;
    if (*out >= f->type_count) {
        memset(f->types+f->type_count, 0, (*out+1-f->type_count)*sizeof(*f->types));
        f->type_count = *out+1;
    }
    if (f->types[*out] && f->types[*out] != type) f->conflict = true;
    else if (!f->types[*out]) { f->types[*out] = type; ++f->distinct; }
    return !(f->positional && f->sequential) || invalid(f,22);
}
static bool amount(formatter *f,size_t *at,format_amount *out,qa_error *error)
{
    if (*at < f->text.size && f->text.data[*at] == '*') {
        ++*at; size_t supplied; out->argument = true;
        return position(f,at,&supplied) && reference(f,FT_INT,supplied,&out->index,error);
    }
    return number(f,at,&out->literal);
}
static bool token_add(formatter *f,format_token token,qa_error *error)
{
    if (!guest_grow((void **)&f->tokens,&f->token_capacity,f->token_count+1,sizeof(*f->tokens),error)) return false;
    f->tokens[f->token_count++] = token; return true;
}
static bool parse(formatter *f,qa_error *error)
{
    size_t at = 0;
    while (at < f->text.size) {
        size_t start = at;
        while (at < f->text.size && f->text.data[at] != '%') ++at;
        if (at > start && !token_add(f,(format_token){.start = start,.bytes = at-start},error)) return false;
        if (at == f->text.size) break;
        ++at;
        if (at < f->text.size && f->text.data[at] == '%') {
            if (!token_add(f,(format_token){.start = at,.bytes = 1},error)) return false;
            ++at; continue;
        }
        format_token token = {0}; size_t supplied;
        if (!position(f,&at,&supplied)) return false;
        for (; at < f->text.size; ++at) {
            uint8_t c = f->text.data[at]; unsigned flag = c == '-' ? FF_LEFT : c == '+' ? FF_PLUS :
                c == ' ' ? FF_SPACE : c == '#' ? FF_ALTERNATE : c == '0' ? FF_ZERO : 0;
            if (!flag && c != '\'') break;
            token.flags |= flag;
        }
        if (!amount(f,&at,&token.width,error)) return false;
        if (at < f->text.size && f->text.data[at] == '.') {
            ++at; token.has_precision = true; if (!amount(f,&at,&token.precision,error)) return false;
        }
        if (at < f->text.size) {
            uint8_t c = f->text.data[at];
            if (c == 'h') { ++at; token.length = FL_H;
                if (at < f->text.size && f->text.data[at] == 'h') { ++at; token.length = FL_HH; } }
            else if (c == 'l') { ++at; token.length = FL_L;
                if (at < f->text.size && f->text.data[at] == 'l') { ++at; token.length = FL_LL; } }
            else if (c == 'L' || c == 'j' || c == 'z' || c == 't' || c == 'q') {
                ++at; token.length = c == 'L' ? FL_EXTENDED : c == 'j' ? FL_J : c == 'z' ? FL_Z : c == 't' ? FL_T : FL_Q;
            } else if (c == 'I' && f->target.os == QA_NATIVE_OS_WINDOWS) {
                ++at; token.length = FL_Z;
                if (at+1 < f->text.size && f->text.data[at] == '6' && f->text.data[at+1] == '4') {
                    at += 2; token.length = FL_LL;
                } else if (at+1 < f->text.size && f->text.data[at] == '3' && f->text.data[at+1] == '2') {
                    at += 2; token.length = FL_NONE;
                }
            } else if (c == 'w' && f->target.os == QA_NATIVE_OS_WINDOWS) { ++at; token.length = FL_L; }
            else if (c == 'I' || c == 'w') return invalid(f,22);
        }
        if (at == f->text.size) return invalid(f,22);
        token.code = (char)f->text.data[at++];
        if (!strchr("diouxXfFeEgGaAcCsSpn",token.code)) return invalid(f,22);
        token.type = strchr("fFeEgGaA",token.code) ? token.length == FL_EXTENDED && f->target.os != QA_NATIVE_OS_WINDOWS ? FT_EXTENDED : FT_DOUBLE :
            strchr("sSpn",token.code) ? FT_POINTER : strchr("cC",token.code) ? FT_INT : integer_bits(f,token.length) == 64 ? FT_I64 : FT_INT;
        if (!reference(f,token.type,supplied,&token.index,error) || !token_add(f,token,error)) return false;
    }
    return true;
}
static bool reader_initialize(formatter *f,qa_error *error)
{
    format_reader *r = &f->reader; r->initialized = true; r->cursor = f->arguments;
    if (f->runtime->target.pointer_bytes == 4) return true;
    r->descriptor = f->arguments; uint64_t gp,fp;
    if (!r->descriptor || !sysv_unsigned(f->runtime,r->descriptor,4,&gp,error) ||
        !sysv_unsigned(f->runtime,r->descriptor+4,4,&fp,error) ||
        !sysv_pointer(f->runtime,r->descriptor+8,&r->cursor,error) ||
        !sysv_pointer(f->runtime,r->descriptor+16,&r->registers,error)) return false;
    if (gp > 48 || gp%8 || fp < 48 || fp > 176 || (fp-48)%16)
        return sysv_fail(error,QA_ERROR_ARGUMENT,"invalid actual System V x64 va_list register offsets");
    r->gp = (uint32_t)gp; r->fp = (uint32_t)fp; return true;
}
static bool next_argument(formatter *f,format_type type,format_value *out,qa_error *error)
{
    format_reader *r = &f->reader;
    if (!r->initialized && !reader_initialize(f,error)) return false;
    bool extended = type == FT_EXTENDED, floating = extended || type == FT_DOUBLE;
    size_t p = f->runtime->target.pointer_bytes;
    size_t bytes = extended ? p == 8 ? 16 : 12 : floating || type == FT_I64 ? 8 : type == FT_POINTER ? p : 4;
    uint64_t address;
    if (r->descriptor && !extended && (floating ? r->fp < 176 : r->gp < 48)) {
        if (!r->registers) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V va_list register save area is null");
        size_t offset = floating ? r->fp : r->gp;
        if (r->registers > UINT64_MAX-offset) return false;
        address = r->registers+offset;
        if (floating) { r->fp += 16; if (!sysv_store(f->runtime,r->descriptor+4,4,r->fp,error)) return false; }
        else { r->gp += 8; if (!sysv_store(f->runtime,r->descriptor,4,r->gp,error)) return false; }
    } else {
        size_t alignment = r->descriptor && extended ? 16 : p, slot = (bytes+p-1)/p*p;
        if (!r->cursor || r->cursor > UINT64_MAX-(alignment-1)) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V va_list overflow storage is null or overflowing");
        address = (r->cursor+alignment-1)/alignment*alignment;
        if (address > UINT64_MAX-slot) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V va_list overflow cursor exceeds target address");
        r->cursor = address+slot;
        if (r->descriptor && !sysv_put_pointer(f->runtime,r->descriptor+8,r->cursor,error)) return false;
    }
    *out = (format_value){0}; uint64_t bits;
    if (!sysv_unsigned(f->runtime,address,extended ? 8 : bytes,&bits,error)) return false;
    if (floating) {
        uint64_t high = 0;
        if (extended && !sysv_unsigned(f->runtime,address+8,2,&high,error)) return false;
        out->floating = sysv_format_decode(bits,(uint16_t)high,extended);
    } else out->integer = bits;
    return true;
}
static bool decoded_value(formatter *f, format_type type, const qa_native_value *value,
    format_value *out, qa_error *error)
{
    *out = (format_value){0};
    if (type == FT_DOUBLE && value->type == QA_NATIVE_F64) {
        uint64_t bits; memcpy(&bits, &value->as.f64, sizeof(bits));
        out->floating = sysv_format_decode(bits, 0, false); return true;
    }
    if (type == FT_EXTENDED) {
        size_t bytes = f->target.pointer_bytes == 8 ? 16 : 12;
        if (value->type == QA_NATIVE_BYTES && value->as.bytes.data && value->as.bytes.size == bytes) {
            const uint8_t *data = value->as.bytes.data;
            out->floating = sysv_format_decode(qa_load_u64le(data), qa_load_u16le(data+8), true); return true;
        }
    } else if ((type == FT_POINTER && value->type == QA_NATIVE_ADDRESS) ||
        (type == FT_INT && value->type == QA_NATIVE_I32) || (type == FT_I64 && value->type == QA_NATIVE_I64)) {
        out->integer = sysv_integer(value); return true;
    }
    return sysv_fail(error, QA_ERROR_ARGUMENT, "guest format value differs from its actual promoted layout");
}
static bool entry_value(formatter *f, size_t index, format_type type, format_value *out, qa_error *error)
{
    qa_buffer storage = {0}; qa_native_value value = {0};
    bool okay = guest_abi_decode_argument(f->entry_plan, f->guest, f->fixed_count+index,
        &value, &storage, error) && decoded_value(f, type, &value, out, error);
    qa_buffer_free(&storage); return okay;
}
static bool value_get(formatter *f,size_t index,format_type type,format_value *out,qa_error *error)
{
    if (f->supplied) {
        if (index >= f->supplied_count) return sysv_fail(error, QA_ERROR_ARGUMENT, "guest format argument is absent from its actual ABI decode");
        *out = f->values[index]; return true;
    }
    if (f->entry_plan) return entry_value(f, index, type, out, error);
    if (f->positional) { if (index >= 4096 || !f->values) return invalid(f,22); *out = f->values[index]; return true; }
    return next_argument(f,type,out,error);
}
static bool amount_get(formatter *f,format_amount amount,int64_t *out,qa_error *error)
{
    if (!amount.argument) { *out = amount.literal; return true; }
    format_value value;
    if (!value_get(f,amount.index,FT_INT,&value,error)) return false;
    uint32_t bits = (uint32_t)value.integer;
    *out = bits <= INT32_MAX ? (int32_t)bits : -(int32_t)(UINT32_MAX-bits)-1; return true;
}
static bool output_append(formatter *f,const void *data,size_t bytes,qa_error *error)
{
    if (f->owned_output) {
        if (f->output.size > f->capacity || bytes > f->capacity-f->output.size ||
            f->total > INT32_MAX || bytes > INT32_MAX-f->total)
            return sysv_fail(error, QA_ERROR_ARGUMENT, "formatted source message exceeds its actual output limit");
        if (!guest_grow((void **)&f->output.data, &f->output_capacity, f->output.size+bytes+1, 1, error)) return false;
        if (bytes) memcpy(f->output.data+f->output.size, data, bytes);
        f->output.size += bytes; f->total += bytes; return true;
    }
    uint64_t available = f->destination && f->capacity > f->used ? f->capacity-f->used : 0;
    size_t copied = available < bytes ? (size_t)available : bytes;
    if (copied) {
        if (f->destination > UINT64_MAX-f->used || !sysv_write(f->runtime,f->destination+f->used,data,copied,error)) return false;
        f->used += copied;
    }
    f->total += bytes;
    return f->total <= INT32_MAX || invalid(f,75);
}
static bool output_repeat(formatter *f,uint8_t byte,uint64_t count,qa_error *error)
{
    if (f->owned_output) {
        if (count > f->capacity-f->output.size)
            return sysv_fail(error, QA_ERROR_ARGUMENT, "formatted source padding exceeds its actual output limit");
        uint8_t block[4096]; memset(block, byte, sizeof(block));
        while (count) {
            size_t bytes = count > sizeof(block) ? sizeof(block) : (size_t)count;
            if (!output_append(f, block, bytes, error)) return false;
            count -= bytes;
        }
        return true;
    }
    uint64_t available = f->destination && f->capacity > f->used ? f->capacity-f->used : 0;
    uint64_t copied = count < available ? count : available, at = 0;
    uint8_t block[4096]; memset(block,byte,sizeof(block));
    while (at < copied) {
        size_t bytes = copied-at > sizeof(block) ? sizeof(block) : (size_t)(copied-at);
        if (!output_append(f,block,bytes,error)) return false;
        at += bytes;
    }
    f->total += count-copied; return f->total <= INT32_MAX || invalid(f,75);
}
static bool readonly_format(formatter *f)
{
    uint64_t cursor = f->format,end = cursor+f->text.size+1;
    while (cursor < end) {
        bool found = false;
        for (size_t i = 0; i < qa_native_guest_mapping_count(f->runtime->guest); ++i) {
            qa_native_guest_mapping mapping;
            if (!qa_native_guest_mapping_at(f->guest,i,&mapping,NULL)) return false;
            if (cursor < mapping.base || cursor-mapping.base >= mapping.bytes) continue;
            if (mapping.permissions&QA_NATIVE_GUEST_WRITE) return false;
            uint64_t stop = mapping.base+mapping.bytes; cursor = stop < end ? stop : end; found = true; break;
        }
        if (!found) return false;
    }
    return true;
}
static bool body_string(formatter *f,uint64_t address,bool wide,int64_t precision,qa_buffer *out,qa_error *error)
{
    if (!address) {
        size_t bytes = precision >= 0 && precision < 6 ?
            f->target.os == QA_NATIVE_OS_WINDOWS ? (size_t)precision : 0 : 6;
        if (!bytes) return true;
        out->data = malloc(bytes);
        if (!out->data) return sysv_fail(error,QA_ERROR_MEMORY,"formatting null guest string");
        memcpy(out->data,"(null)",bytes); out->size = bytes; return true;
    }
    size_t limit = precision < 0 ? SYSV_MAX_STRING : (size_t)precision,capacity = 0;
    size_t width = wide ? f->target.os == QA_NATIVE_OS_WINDOWS ? 2 : 4 : 1;
    for (size_t i = 0; i < limit; ++i) {
        uint64_t value;
        if (address > UINT64_MAX-i*width || !format_unsigned(f,address+i*width,width,&value,error)) return false;
        if (!value) return true;
        if (wide && value > (f->target.os == QA_NATIVE_OS_WINDOWS ? 255u : 127u)) return invalid(f,84);
        if (!guest_grow((void **)&out->data,&capacity,out->size+1,1,error)) return false;
        out->data[out->size++] = (uint8_t)value;
    }
    return precision >= 0 || sysv_fail(error,QA_ERROR_ARGUMENT,"guest printf string exceeds actual terminated runtime limit");
}
static size_t digits(uint64_t value,unsigned base,bool upper,char *out)
{
    char reversed[64]; size_t count = 0;
    do { unsigned d = (unsigned)(value%base); reversed[count++] = (char)(d < 10 ? '0'+d : (upper ? 'A' : 'a')+d-10); value /= base; } while (value);
    for (size_t i = 0; i < count; ++i) out[i] = reversed[count-i-1];
    return count;
}
static bool conversion(formatter *f,const format_token *token,qa_error *error)
{
    int64_t signed_width,precision = -1;
    if (!amount_get(f,token->width,&signed_width,error) ||
        (token->has_precision && !amount_get(f,token->precision,&precision,error))) return false;
    bool left = (token->flags&FF_LEFT) || signed_width < 0;
    uint64_t width = signed_width < 0 ? (uint64_t)-signed_width : (uint64_t)signed_width;
    if (precision < 0) precision = -1;
    if (precision > SYSV_MAX_STRING) return sysv_fail(error,QA_ERROR_ARGUMENT,"guest printf precision exceeds actual conversion limit");
    format_value value;
    if (!value_get(f,token->index,token->type,&value,error)) return false;
    char code = token->code,prefix[4]; size_t prefix_bytes = 0; qa_buffer body = {0};
    char integer_body[64]; size_t integer_count = 0; uint64_t precision_zeros = 0;
    bool zero = (token->flags&FF_ZERO) && !left,ok = true;
    if (strchr("diouxX",code)) {
        unsigned bits = integer_bits(f,token->length); uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1)<<bits)-1;
        uint64_t magnitude = value.integer&mask; bool sign = code == 'd' || code == 'i',negative = sign && ((magnitude>>(bits-1))&1);
        if (negative) magnitude = (UINT64_C(0)-magnitude)&mask;
        unsigned base = code == 'o' ? 8 : code == 'x' || code == 'X' ? 16 : 10;
        if (precision || magnitude) integer_count = digits(magnitude,base,code == 'X',integer_body);
        if (precision > (int64_t)integer_count) precision_zeros = (uint64_t)precision-integer_count;
        if (sign) {
            if (negative) prefix[prefix_bytes++] = '-'; else if (token->flags&FF_PLUS) prefix[prefix_bytes++] = '+';
            else if (token->flags&FF_SPACE) prefix[prefix_bytes++] = ' ';
        } else if ((token->flags&FF_ALTERNATE) && base == 16 && magnitude) {
            prefix[prefix_bytes++] = '0'; prefix[prefix_bytes++] = code == 'X' ? 'X' : 'x';
        } else if ((token->flags&FF_ALTERNATE) && base == 8 && !precision_zeros && (!integer_count || integer_body[0] != '0')) prefix[prefix_bytes++] = '0';
        if (precision >= 0) zero = false;
    } else if (strchr("fFeEgGaA",code)) {
        ok = sysv_format_float_dialect(value.floating,code,(int)precision,(token->flags&FF_ALTERNATE) != 0,
            f->rounding,f->target.os == QA_NATIVE_OS_WINDOWS,&body,error);
        if (value.floating.negative) prefix[prefix_bytes++] = '-'; else if (token->flags&FF_PLUS) prefix[prefix_bytes++] = '+';
        else if (token->flags&FF_SPACE) prefix[prefix_bytes++] = ' ';
        if (value.floating.kind) zero = false;
        if (body.size >= 2 && body.data[0] == '0' && (body.data[1] == 'x' || body.data[1] == 'X')) {
            prefix[prefix_bytes++] = '0'; prefix[prefix_bytes++] = (char)body.data[1];
            memmove(body.data,body.data+2,body.size-2); body.size -= 2;
        }
    } else if (code == 'p') {
        uint64_t pointer = f->target.pointer_bytes == 4 ? (uint32_t)value.integer : value.integer;
        if (f->target.os == QA_NATIVE_OS_WINDOWS) {
            size_t count = f->target.pointer_bytes*2;
            for (size_t i = 0; i < count; ++i)
                integer_body[i] = "0123456789ABCDEF"[(pointer >> ((count-i-1)*4))&15];
            integer_count = count;
        } else if (!pointer) { memcpy(integer_body,"(nil)",5); integer_count = 5; zero = false; }
        else { prefix[prefix_bytes++] = '0'; prefix[prefix_bytes++] = 'x'; integer_count = digits(pointer,16,false,integer_body);
            if (precision > (int64_t)integer_count) precision_zeros = (uint64_t)precision-integer_count; }
    } else if (code == 's' || code == 'S') {
        bool wide = token->length == FL_L || (code == 'S' && token->length != FL_H);
        ok = body_string(f,value.integer,wide,precision,&body,error); zero = false;
    } else if (code == 'c' || code == 'C') {
        bool wide = token->length == FL_L || (code == 'C' && token->length != FL_H);
        uint32_t character = wide ? f->target.os == QA_NATIVE_OS_WINDOWS ? (uint16_t)value.integer : (uint32_t)value.integer : (uint8_t)value.integer;
        if (wide && character > (f->target.os == QA_NATIVE_OS_WINDOWS ? 255u : 127u)) return invalid(f,84);
        integer_body[0] = (char)character; integer_count = 1; zero = false;
    } else if (code == 'n') {
        if (f->target.os == QA_NATIVE_OS_WINDOWS) return invalid(f,22);
        if (f->fortify && !readonly_format(f)) return sysv_fail(error,QA_ERROR_ARGUMENT,"glibc fortified %n refers to writable format storage");
        if (!value.integer) return sysv_fail(error,QA_ERROR_ARGUMENT,"null guest printf count pointer");
        return format_store(f,value.integer,integer_bits(f,token->length)/8,f->total,error);
    }
    uint64_t body_bytes = body.size+integer_count+precision_zeros;
    uint64_t padding = width > prefix_bytes+body_bytes ? width-prefix_bytes-body_bytes : 0;
    if (ok && !left && !zero) ok = output_repeat(f,' ',padding,error);
    if (ok) ok = output_append(f,prefix,prefix_bytes,error);
    if (ok && zero) ok = output_repeat(f,'0',padding,error);
    if (ok && precision_zeros) ok = output_repeat(f,'0',precision_zeros,error);
    if (ok) ok = output_append(f,body.data,body.size,error) && output_append(f,integer_body,integer_count,error);
    if (ok && left) ok = output_repeat(f,' ',padding,error);
    qa_buffer_free(&body); return ok;
}
static bool format_layouts(formatter *f, guest_abi_layout **out, qa_error *error)
{
    if (f->conflict) return invalid(f,22);
    size_t count = f->distinct;
    guest_abi_layout *layouts = count ? calloc(count, sizeof(*layouts)) : NULL;
    if (count && !layouts) return sysv_fail(error, QA_ERROR_MEMORY, "owning promoted guest format layouts");
    for (size_t i = 0; i < count; ++i) {
        format_type type = f->types[i];
        if (!type) { free(layouts); return invalid(f,22); }
        size_t bytes = type == FT_POINTER ? f->target.pointer_bytes :
            type == FT_INT ? 4 : type == FT_EXTENDED ? f->target.pointer_bytes == 8 ? 16 : 12 : 8;
        size_t alignment = f->target.pointer_bytes == 4 ? 4 : bytes;
        qa_native_value_type kind = type == FT_POINTER ? QA_NATIVE_ADDRESS : type == FT_INT ? QA_NATIVE_I32 :
            type == FT_DOUBLE ? QA_NATIVE_F64 : type == FT_EXTENDED ? QA_NATIVE_BYTES : QA_NATIVE_I64;
        layouts[i] = (guest_abi_layout){.kind = kind, .bytes = bytes, .alignment = alignment,
            .stack_only = type == FT_EXTENDED};
    }
    *out = layouts; return true;
}
static bool entry_values(formatter *f, qa_error *error)
{
    if (!f->positional || !f->distinct) return true;
    f->values = calloc(f->distinct,sizeof(*f->values));
    if (!f->values) return sysv_fail(error,QA_ERROR_MEMORY,"owning positional guest entry values");
    for (size_t i = 0; i < f->distinct; ++i)
        if (!entry_value(f,i,f->types[i],f->values+i,error)) return false;
    f->supplied = true; f->supplied_count = f->distinct; return true;
}
bool sysv_format_install(guest_sysv_runtime *r,qa_error *error)
{
    qa_native_value_type pointer = QA_NATIVE_ADDRESS,size = sysv_size_type(r);
    const qa_native_value_type normal[] = {pointer,size,pointer,pointer},checked[] = {pointer,size,QA_NATIVE_I32,size,pointer,pointer};
    const char *base = r->target.pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
    for (size_t i = 0; i < 2; ++i) {
        const char *versions[] = {i ? "GLIBC_2.3.4" : base,NULL};
        if (!sysv_service_add(r,SYSV_FORMAT,(uint32_t)i+1,0,0,0,"libc.so.6",i ? "__vsnprintf_chk" : "vsnprintf",
            versions,2,i ? checked : normal,i ? 6 : 4,QA_NATIVE_I32,NULL,NULL,error)) return false;
    }
    const qa_native_value_type entry[] = {pointer,size,pointer};
    const char *versions[] = {base,NULL};
    return sysv_service_add(r,SYSV_FORMAT,3,0,0,0,"libc.so.6","snprintf",
        versions,2,entry,3,QA_NATIVE_I32,NULL,NULL,error) && sysv_scan_install(r,error);
}
bool sysv_format_call(sysv_service *service,const qa_native_value *args,qa_native_value *out,qa_error *error)
{
    if (service->operation >= 4) return sysv_scan_call(service,args,out,error);
    bool checked = service->operation == 2;
    bool entry = service->operation == 3;
    formatter *f = calloc(1,sizeof(*f));
    if (!f) return sysv_fail(error,QA_ERROR_MEMORY,"allocating guest format argument graph");
    f->runtime = service->runtime; f->guest = f->runtime->guest; f->target = f->runtime->target;
    f->destination = args[0].as.address; f->capacity = sysv_integer(args+1);
    f->format = args[checked ? 4 : 2].as.address;
    if (!entry) f->arguments = args[checked ? 5 : 3].as.address;
    f->fortify = checked && args[2].as.i32 > 0; out->type = QA_NATIVE_I32;
    bool ok = true;
    if (checked && sysv_integer(args+3) < f->capacity) ok = sysv_fail(error,QA_ERROR_ARGUMENT,"glibc fortified destination size is smaller than maxlen");
    if (ok && f->capacity) {
        if (!f->destination) ok = sysv_fail(error,QA_ERROR_ARGUMENT,"glibc nonempty snprintf requires an actual destination");
        else ok = sysv_store(f->runtime,f->destination,1,0,error);
    }
    if (ok && !f->format) { f->format_errno = 22; goto finish; }
    if (ok) {
        bool terminated;
        ok = sysv_string(f->runtime,f->format,SYSV_MAX_STRING,&f->text,&terminated,error);
        if (ok && !terminated) ok = sysv_fail(error,QA_ERROR_ARGUMENT,"guest format exceeds actual terminated string limit");
    }
    if (ok) {
        qa_native_guest_cpu cpu;
        ok = qa_native_guest_cpu_read(f->runtime->guest,&cpu,error);
        if (ok) f->rounding = (cpu.fp_control>>10)&3;
    }
    if (ok) ok = parse(f,error);
    if (ok && entry) {
        guest_abi_layout *layouts = NULL;
        guest_abi_signature signature = {.abi = f->target.abi, .parameters = service->parameters,
            .parameter_count = service->parameter_count, .result = service->result, .variadic = true};
        ok = format_layouts(f,&layouts,error) &&
            guest_abi_plan_create(&signature,layouts,f->distinct,&f->entry_plan,error);
        free(layouts);
        f->fixed_count = service->parameter_count;
        if (ok) ok = entry_values(f,error);
    }
    if (ok && !entry && (f->sequence || f->distinct)) ok = reader_initialize(f,error);
    if (ok && !entry && f->positional) {
        if (f->conflict) ok = invalid(f,22);
        else {
            f->values = calloc(4096,sizeof(*f->values));
            if (!f->values) ok = sysv_fail(error,QA_ERROR_MEMORY,"retaining positional guest printf arguments");
            for (size_t i = 0; i < f->distinct && ok; ++i)
                ok = f->types[i] ? next_argument(f,f->types[i],f->values+i,error) : invalid(f,22);
        }
    }
    for (size_t i = 0; i < f->token_count && ok; ++i)
        ok = f->tokens[i].code ? conversion(f,f->tokens+i,error) :
            output_append(f,f->text.data+f->tokens[i].start,f->tokens[i].bytes,error);
finish:
    if (ok || f->format_errno) {
        ok = true;
        if (f->destination && f->capacity) {
            uint64_t at = f->used >= f->capacity ? f->capacity-1 : f->used;
            ok = f->destination <= UINT64_MAX-at && sysv_store(f->runtime,f->destination+at,1,0,error);
        }
        if (ok && f->format_errno) ok = sysv_errno(f->runtime,f->format_errno,error);
        out->as.i32 = f->format_errno ? -1 : (int32_t)f->total;
    }
    formatter_free(f); return ok;
}

static void formatter_free(formatter *f)
{
    if (!f) return;
    qa_buffer_free(&f->text); qa_buffer_free(&f->output);
    free(f->tokens); free(f->types); free(f->values); guest_abi_plan_destroy(f->entry_plan); free(f);
}
static formatter *formatter_guest(const qa_native_guest *guest, uint64_t format, qa_error *error)
{
    if (!format) { sysv_fail(error, QA_ERROR_ARGUMENT, "guest source format is null"); return NULL; }
    if (!guest_ready(guest, error)) return NULL;
    formatter *f = calloc(1, sizeof(*f));
    if (!f) { sysv_fail(error, QA_ERROR_MEMORY, "owning stopped guest format"); return NULL; }
    f->guest = (qa_native_guest *)guest; f->target = guest->options.image.target; f->format = format;
    if (!format_string(f, error) || !parse(f, error) || f->conflict || f->format_errno) {
        if (error && error->code == QA_OK) sysv_fail(error, QA_ERROR_ARGUMENT, "invalid promoted guest format argument graph");
        formatter_free(f); return NULL;
    }
    for (size_t i = 0; i < f->distinct; ++i) if (!f->types[i]) {
        sysv_fail(error, QA_ERROR_ARGUMENT, "positional guest format has an untyped argument gap");
        formatter_free(f); return NULL;
    }
    return f;
}
bool guest_format_select(const qa_native_guest *guest, uint64_t format,
    guest_abi_layout **out, size_t *count, qa_error *error)
{
    if (!out || *out || !count)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "guest format selector needs empty owned layouts");
    formatter *f = formatter_guest(guest, format, error);
    if (!f) return false;
    guest_abi_layout *layouts = NULL;
    bool okay = format_layouts(f,&layouts,error);
    if (okay) { *out = layouts; *count = f->distinct; } else free(layouts);
    formatter_free(f); return okay;
}
static bool render_output(formatter *f, size_t maximum, qa_buffer *out, qa_error *error)
{
    qa_native_guest_cpu cpu;
    bool okay = qa_native_guest_cpu_read(f->guest, &cpu, error);
    if (okay) {
        f->rounding = f->target.os == QA_NATIVE_OS_WINDOWS ? (cpu.mxcsr>>13)&3 : (cpu.fp_control>>10)&3;
        f->owned_output = true; f->capacity = maximum;
    }
    for (size_t i = 0; okay && i < f->token_count; ++i)
        okay = f->tokens[i].code ? conversion(f, f->tokens+i, error) :
            output_append(f, f->text.data+f->tokens[i].start, f->tokens[i].bytes, error);
    if (okay && f->format_errno) okay = sysv_fail(error, QA_ERROR_ARGUMENT, "guest source format conversion failed");
    if (okay) {
        if (!f->output.data) {
            f->output.data = malloc(1);
            if (!f->output.data) okay = sysv_fail(error, QA_ERROR_MEMORY, "owning empty guest formatted output");
        }
        if (okay) { f->output.data[f->output.size] = 0; *out = f->output; f->output = (qa_buffer){0}; }
    }
    return okay;
}
bool guest_format_render(qa_native_guest *guest, uint64_t format, const qa_native_value *values,
    size_t count, size_t maximum, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !maximum || maximum > INT32_MAX ||
        (count && !values))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "guest format output requires actual stopped ownership and empty storage");
    if (!guest_mutable(guest, error)) return false;
    formatter *f = formatter_guest(guest, format, error);
    if (!f) return false;
    bool okay = count == f->distinct;
    if (!okay) sysv_fail(error, QA_ERROR_ARGUMENT, "promoted guest format differs from its complete ABI argument count");
    if (okay && count) {
        f->values = calloc(count, sizeof(*f->values));
        if (!f->values) okay = sysv_fail(error, QA_ERROR_MEMORY, "owning decoded guest format values");
    }
    for (size_t i = 0; okay && i < count; ++i)
        okay = decoded_value(f, f->types[i], values+i, f->values+i, error);
    if (!okay && error && error->code == QA_OK)
        sysv_fail(error, QA_ERROR_ARGUMENT, "guest format value differs from its actual promoted layout");
    if (okay) {
        f->supplied = true; f->supplied_count = count;
        okay = render_output(f, maximum, out, error);
    }
    formatter_free(f); return okay;
}
bool guest_format_render_entry(qa_native_guest *guest, const qa_native_signature *signature,
    uint64_t format, size_t maximum, qa_buffer *out, qa_error *error)
{
    if (!guest || !signature || !signature->variadic || signature->abi != guest->options.image.target.abi ||
        !out || out->data || out->size || !maximum || maximum > INT32_MAX)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "guest format entry needs its genuine variadic prefix and empty output");
    if (!guest_mutable(guest, error)) return false;
    guest_abi_layout *layouts = NULL;
    formatter *f = formatter_guest(guest, format, error);
    bool okay = f && format_layouts(f,&layouts,error) &&
        guest_abi_plan_native(signature,layouts,f->distinct,&f->entry_plan,error);
    free(layouts);
    if (okay) f->fixed_count = signature->parameter_count;
    if (okay) okay = entry_values(f,error);
    if (okay) okay = render_output(f, maximum, out, error);
    formatter_free(f); return okay;
}
