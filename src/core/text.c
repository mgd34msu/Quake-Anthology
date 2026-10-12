#include "qa/text.h"

#include <stdlib.h>
#include <string.h>
#include <unicode/ucasemap.h>
#include <unicode/utf8.h>

bool qa_info_next(const char **cursor,qa_bytes *key,qa_bytes *value)
{
    const char *p=*cursor;
    if (*p=='\\') ++p;
    const char *separator=strchr(p,'\\');
    if (!separator) return false;
    const char *text=separator+1,*end=strchr(text,'\\');
    if (!end) end=text+strlen(text);
    *key=(qa_bytes){(const uint8_t *)p,(size_t)(separator-p)};
    *value=(qa_bytes){(const uint8_t *)text,(size_t)(end-text)};
    *cursor=end;
    return true;
}

bool qa_info_append(char *out, size_t capacity, size_t *length,
    qa_bytes key, qa_bytes value, unsigned policy, qa_error *error)
{
    if (!out || !length || *length >= capacity ||
        (key.size && !key.data) || (value.size && !value.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid info pair output");
        return false;
    }
    size_t remaining = capacity - 1 - *length;
    if (!(policy & QA_INFO_TRUNCATE) &&
        (remaining < 2 || key.size > remaining - 2 || value.size > remaining - 2 - key.size)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Info pair exceeds output storage");
        return false;
    }
    const qa_bytes parts[] = {
        {(const uint8_t *)"\\", 1}, key, {(const uint8_t *)"\\", 1}, value};
    for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); ++i) {
        size_t amount = parts[i].size < remaining ? parts[i].size : remaining;
        if (amount) memmove(out + *length, parts[i].data, amount);
        *length += amount;
        remaining -= amount;
    }
    out[*length] = 0;
    return true;
}

int qa_text_bytes_compare(qa_bytes left,qa_bytes right,bool ascii_fold)
{
    size_t count=left.size<right.size?left.size:right.size;
    if (ascii_fold) {
        for (size_t i=0;i<count;++i) {
            unsigned char a=left.data[i],b=right.data[i];
            if (a>='A' && a<='Z') a+='a'-'A';
            if (b>='A' && b<='Z') b+='a'-'A';
            if (a!=b) return a<b?-1:1;
        }
    } else if (count) {
        int order=memcmp(left.data,right.data,count);
        if (order) return order<0?-1:1;
    }
    return left.size==right.size?0:left.size<right.size?-1:1;
}

bool qa_text_bytes_equal(qa_bytes left,qa_bytes right,bool ascii_fold)
{
    return left.size==right.size && !qa_text_bytes_compare(left,right,ascii_fold);
}

bool qa_text_bytes_prefix(qa_bytes text,qa_bytes prefix,bool ascii_fold)
{
    return prefix.size<=text.size &&
        qa_text_bytes_equal((qa_bytes){text.data,prefix.size},prefix,ascii_fold);
}

int qa_text_ascii_compare(const char *left,const char *right)
{
    return qa_text_bytes_compare((qa_bytes){(const uint8_t *)left,strlen(left)},
                                (qa_bytes){(const uint8_t *)right,strlen(right)},true);
}

bool qa_text_ascii_equal(const char *left,const char *right)
{
    return qa_text_bytes_equal((qa_bytes){(const uint8_t *)left,strlen(left)},
                              (qa_bytes){(const uint8_t *)right,strlen(right)},true);
}

bool qa_text_q3_color_escape(uint32_t current,uint32_t next,uint8_t *index)
{
    if (current!='^' || !next || next=='^') return false;
    *index=(uint8_t)((next-(uint32_t)'0')&7u);
    return true;
}

bool qa_info_key_equal(qa_bytes key,qa_bytes wanted,unsigned policy)
{
    return qa_text_bytes_equal(key,wanted,(policy&QA_INFO_CASE_FOLD)!=0);
}

bool qa_info_find(const char *text,qa_bytes wanted,unsigned policy,qa_bytes *out)
{
    const char *cursor=text;
    qa_bytes key,value;
    bool found=false;
    *out=(qa_bytes){(const uint8_t *)"",0};
    while (qa_info_next(&cursor,&key,&value)) {
        if (!qa_info_key_equal(key,wanted,policy)) continue;
        *out=value;found=true;
        if (!(policy&QA_INFO_LAST)) break;
    }
    return found;
}

static UChar32 next_code(qa_bytes input, size_t *cursor) {
    size_t remaining = input.size - *cursor;
    int32_t at = 0, length = (int32_t)(remaining < 4 ? remaining : 4);
    UChar32 code;
    U8_NEXT(input.data + *cursor, at, length, code);
    *cursor += (size_t)at;
    return code;
}
bool qa_utf8_valid(qa_bytes input) {
    if ((!input.data && input.size) || input.size > PTRDIFF_MAX)
        return false;
    size_t cursor = 0;
    while (cursor < input.size)
        if (next_code(input, &cursor) < 0)
            return false;
    return true;
}
bool qa_utf8_next(qa_bytes input, size_t *cursor, uint32_t *scalar) {
    if (*cursor >= input.size)
        return false;
    UChar32 code = next_code(input, cursor);
    *scalar = code < 0 ? UINT32_C(0xfffd) : (uint32_t)code;
    return true;
}
bool qa_utf8_next_strict(qa_bytes input, size_t *cursor, uint32_t *scalar) {
    if (*cursor >= input.size)
        return false;
    size_t next = *cursor;
    UChar32 code = next_code(input, &next);
    if (code < 0)
        return false;
    *cursor = next;
    *scalar = (uint32_t)code;
    return true;
}
size_t qa_utf8_encode(uint32_t code, char out[4]) {
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        code = 0xfffd;
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 63));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 63));
        out[2] = (char)(0x80 | (code & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 63));
    out[2] = (char)(0x80 | ((code >> 6) & 63));
    out[3] = (char)(0x80 | (code & 63));
    return 4;
}
bool qa_text_to_source_bytes(qa_bytes input, uint8_t *out, size_t capacity,
    size_t *size, qa_error *error)
{
    if ((!input.data && input.size) || !out || !capacity || !size) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source text requires bounded output");
        return false;
    }
    size_t cursor=0,used=0; uint32_t scalar;
    while (qa_utf8_next(input,&cursor,&scalar)) {
        if (scalar>255) {
            qa_error_set(error,QA_ERROR_FORMAT,cursor,"Text is not representable as source bytes");
            return false;
        }
        if (used==capacity-1) {
            qa_error_set(error,QA_ERROR_FORMAT,cursor,"Source text exceeds its byte storage");
            return false;
        }
        out[used++]=(uint8_t)scalar;
    }
    out[used]=0; *size=used;
    return true;
}
bool qa_text_from_source_bytes(qa_bytes input, char *out, size_t capacity,
    size_t *size, qa_error *error)
{
    if ((!input.data && input.size) || !out || !capacity || !size) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source text requires bounded output");
        return false;
    }
    size_t used=0;
    for (size_t i=0;i<input.size;++i) {
        char bytes[4]; size_t count=qa_utf8_encode(input.data[i],bytes);
        if (count>=capacity-used) {
            qa_error_set(error,QA_ERROR_FORMAT,i,"Source text exceeds its UTF-8 storage");
            return false;
        }
        memcpy(out+used,bytes,count); used+=count;
    }
    out[used]=0; *size=used;
    return true;
}
bool qa_unicode_whitespace(uint32_t c) {
    return (c >= 9 && c <= 13) || c == 0x20 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
           c == 0x205f || c == 0x3000 || c == 0xfeff;
}

bool qa_utf8_repair(qa_bytes input, qa_buffer *out, qa_error *error) {
    if (!out || (!input.data && input.size) || input.size > INT32_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid UTF-8 resource input");
        return false;
    }
    size_t at = 0;
    size_t needed = 0;
    while (at < input.size) {
        size_t begin = at;
        UChar32 code = next_code(input, &at);
        size_t count = code < 0 ? 3 : at - begin;
        if (count >= SIZE_MAX - needed) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "UTF-8 resource size overflow");
            return false;
        }
        needed += count;
    }
    uint8_t *data = malloc(needed + 1);
    if (!data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UTF-8 resource text");
        return false;
    }
    at = 0;
    size_t written = 0;
    while (at < input.size) {
        size_t begin = at;
        UChar32 code = next_code(input, &at);
        size_t count = code < 0 ? 3 : at - begin;
        if (code < 0)
            memcpy(data + written, "\xef\xbf\xbd", 3);
        else
            memcpy(data + written, input.data + begin, count);
        written += count;
    }
    data[written] = 0;
    *out = (qa_buffer){data, written};
    return true;
}

static bool utf8_lower(qa_bytes input, qa_arena *storage, qa_buffer *out, qa_error *error) {
    if (!out || (!input.data && input.size) || input.size > INT32_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Unicode lowercase input");
        return false;
    }
    UErrorCode status = U_ZERO_ERROR;
    UCaseMap *map = ucasemap_open("", 0, &status);
    if (U_FAILURE(status) || !map) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "opening Unicode case map: %s",
                     u_errorName(status));
        return false;
    }
    const char *source = input.size ? (const char *)input.data : "";
    int32_t needed = ucasemap_utf8ToLower(map, NULL, 0, source, (int32_t)input.size, &status);
    if ((U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) || needed < 0 ||
        needed == INT32_MAX) {
        ucasemap_close(map);
        qa_error_set(error, QA_ERROR_FORMAT, 0, "lowercasing UTF-8: %s", u_errorName(status));
        return false;
    }
    uint8_t *data = storage ? qa_arena_alloc(storage, (size_t)needed + 1, 1, error) :
        malloc((size_t)needed + 1);
    if (!data) {
        ucasemap_close(map);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating lowercase text");
        return false;
    }
    status = U_ZERO_ERROR;
    int32_t written =
        ucasemap_utf8ToLower(map, (char *)data, needed + 1, source, (int32_t)input.size, &status);
    ucasemap_close(map);
    if (U_FAILURE(status) || written < 0 || written > needed) {
        if (!storage) free(data);
        qa_error_set(error, QA_ERROR_FORMAT, 0, "lowercasing UTF-8: %s", u_errorName(status));
        return false;
    }
    data[written] = 0;
    *out = (qa_buffer){data, (size_t)written};
    return true;
}

bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error)
{
    return utf8_lower(input, NULL, out, error);
}

bool qa_utf8_lower_arena(qa_bytes input, qa_arena *storage, qa_buffer *out, qa_error *error)
{
    return utf8_lower(input, storage, out, error);
}

bool qa_text_join_words(const char *const *words,size_t count,char *out,size_t capacity,qa_error *error)
{
    size_t used=0;
    for(size_t i=0;i<count;++i) {
        size_t size=strlen(words[i]);
        if(used>=capacity || size+(i?1u:0u)>=capacity-used) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Joined words exceed text storage");
            return false;
        }
        if(i) out[used++]=' ';
        memcpy(out+used,words[i],size);used+=size;
    }
    if(!capacity) return false;
    out[used]=0;
    return true;
}
