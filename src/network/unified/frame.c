#include "frame_internal.h"
#include "value_internal.h"
#include "qa/text.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { RECORD_FIELDS = 256, RECORD_DEPTH = 32 };
#define RECORD_BYTES (32u * 1024u * 1024u)

typedef struct record_reader {
    qa_bytes bytes;
    size_t at, allocated;
    qa_error *error;
    qa_unified_frame_lease *lease;
    qa_strings *strings;
    const qa_strings *baseline_strings;
    qa_unified_clone_alloc_fn clone_allocate;
    void *clone_context;
} record_reader;

static bool fail(qa_error *e, const char *message)
{ qa_error_set(e, QA_ERROR_FORMAT, 0, "%s", message); return false; }
static const void *field_read(const void *value, const qa_unified_field *field)
{ return value ? (const uint8_t *)value + field->offset : NULL; }
static void *field_write(void *value, const qa_unified_field *field)
{ return (uint8_t *)value + field->offset; }
static void *pointer_read(const void *field)
{ void *value = NULL; if (field) memcpy(&value, field, sizeof(value)); return value; }
static void pointer_write(void *field, void *value)
{ memcpy(field, &value, sizeof(value)); }
static size_t scalar_size(qa_unified_field_kind kind)
{
    switch (kind) {
    case QA_UNIFIED_FIELD_BOOL: return sizeof(bool);
    case QA_UNIFIED_FIELD_U8: case QA_UNIFIED_FIELD_I8: return 1;
    case QA_UNIFIED_FIELD_U16: case QA_UNIFIED_FIELD_I16: return 2;
    case QA_UNIFIED_FIELD_U32: case QA_UNIFIED_FIELD_I32: case QA_UNIFIED_FIELD_F32: return 4;
    case QA_UNIFIED_FIELD_U64: case QA_UNIFIED_FIELD_I64: case QA_UNIFIED_FIELD_F64: return 8;
    case QA_UNIFIED_FIELD_SIZE: return sizeof(size_t);
    default: return 0;
    }
}
static bool append(qa_unified_builder *b, const void *bytes, size_t count, qa_error *e)
{ return qa_unified_append(b, bytes, count, e); }
static bool byte(qa_unified_builder *b, uint8_t value, qa_error *e)
{ return append(b, &value, 1, e); }
static bool unsigned_write(qa_unified_builder *b, uint64_t value, qa_error *e)
{
    do {
        uint8_t next = (uint8_t)(value & 127u); value >>= 7;
        if (value) next |= 128u;
        if (!byte(b, next, e)) return false;
    } while (value);
    return true;
}
static bool signed_write(qa_unified_builder *b, int64_t value, qa_error *e)
{
    uint64_t word; memcpy(&word, &value, sizeof(word));
    return unsigned_write(b, (word << 1) ^ (value < 0 ? UINT64_MAX : 0), e);
}
static bool read_bytes(record_reader *r, void *out, size_t count)
{
    if (count > r->bytes.size - r->at) return fail(r->error, "Truncated typed Unified field");
    if (count) memcpy(out, r->bytes.data + r->at, count);
    r->at += count; return true;
}
static bool unsigned_read(record_reader *r, uint64_t *out)
{
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        uint8_t next;
        if (!read_bytes(r, &next, 1)) return false;
        if (shift == 63 && (next & 254u)) return fail(r->error, "Typed Unified integer overflows");
        value |= (uint64_t)(next & 127u) << shift;
        if (!(next & 128u)) {
            if (shift && next == 0) return fail(r->error, "Typed Unified integer is noncanonical");
            *out = value; return true;
        }
    }
    return fail(r->error, "Typed Unified integer overflows");
}
static bool signed_read(record_reader *r, int64_t *out)
{
    uint64_t value;
    if (!unsigned_read(r, &value)) return false;
    uint64_t word = (value >> 1) ^ (0u - (value & 1u));
    memcpy(out, &word, sizeof(word)); return true;
}
static bool little_write(qa_unified_builder *b, uint64_t word, size_t size, qa_error *e)
{
    uint8_t bytes[8];
    for (size_t i = 0; i < size; ++i) bytes[i] = (uint8_t)(word >> (i * 8));
    return append(b, bytes, size, e);
}
static bool little_read(record_reader *r, size_t size, uint64_t *out)
{
    uint8_t bytes[8]; uint64_t word = 0;
    if (!read_bytes(r, bytes, size)) return false;
    for (size_t i = 0; i < size; ++i) word |= (uint64_t)bytes[i] << (i * 8);
    *out = word; return true;
}
static void *allocate(record_reader *r, size_t count, size_t size)
{
    if (size && count > (RECORD_BYTES - r->allocated) / size) {
        fail(r->error, "Typed Unified state exceeds its owned memory bound"); return NULL;
    }
    size_t bytes = count * size;
    void *out;
    if (r->clone_allocate) {
        out = r->clone_allocate(r->clone_context, bytes ? bytes : 1,
            _Alignof(max_align_t), r->error);
        if (out) memset(out, 0, bytes ? bytes : 1);
    } else {
        out = r->lease ? qa_unified_frame_lease_alloc(r->lease, bytes ? bytes : 1, 1, _Alignof(max_align_t), r->error) :
            calloc(bytes ? bytes : 1, 1);
    }
    if (!out) { qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Allocating typed Unified state"); return NULL; }
    r->allocated += bytes; return out;
}
static bool scalar_write(qa_unified_builder *b, qa_unified_field_kind kind, const void *value, qa_error *e)
{
    switch (kind) {
    case QA_UNIFIED_FIELD_BOOL: return byte(b, *(const bool *)value ? 1 : 0, e);
    case QA_UNIFIED_FIELD_U8: return unsigned_write(b, *(const uint8_t *)value, e);
    case QA_UNIFIED_FIELD_I8: return signed_write(b, *(const int8_t *)value, e);
    case QA_UNIFIED_FIELD_U16: return unsigned_write(b, *(const uint16_t *)value, e);
    case QA_UNIFIED_FIELD_I16: return signed_write(b, *(const int16_t *)value, e);
    case QA_UNIFIED_FIELD_U32: { uint32_t v; memcpy(&v, value, 4); return unsigned_write(b, v, e); }
    case QA_UNIFIED_FIELD_I32: { int32_t v; memcpy(&v, value, 4); return signed_write(b, v, e); }
    case QA_UNIFIED_FIELD_U64: return unsigned_write(b, *(const uint64_t *)value, e);
    case QA_UNIFIED_FIELD_I64: return signed_write(b, *(const int64_t *)value, e);
    case QA_UNIFIED_FIELD_SIZE: return unsigned_write(b, *(const size_t *)value, e);
    case QA_UNIFIED_FIELD_F32: {
        float v; uint32_t word; memcpy(&v, value, 4); memcpy(&word, value, 4);
        if (!word) return byte(b, 0, e);
        if (word != UINT32_C(0x80000000) && (double)v >= INT32_MIN && (double)v <= INT32_MAX && truncf(v) == v)
            return byte(b, 1, e) && signed_write(b, (int32_t)v, e);
        return byte(b, 2, e) && little_write(b, word, 4, e);
    }
    case QA_UNIFIED_FIELD_F64: {
        double v; uint64_t word; memcpy(&v, value, 8); memcpy(&word, value, 8);
        if (!word) return byte(b, 0, e);
        if (word != UINT64_C(0x8000000000000000) && fabs(v) <= (double)QA_UNIFIED_SAFE_INTEGER && trunc(v) == v)
            return byte(b, 1, e) && signed_write(b, (int64_t)v, e);
        return byte(b, 2, e) && little_write(b, word, 8, e);
    }
    default: return fail(e, "Unknown typed Unified scalar field");
    }
}
static bool scalar_read(record_reader *r, qa_unified_field_kind kind, void *out)
{
    uint64_t u = 0; int64_t s = 0;
    switch (kind) {
    case QA_UNIFIED_FIELD_BOOL: {
        uint8_t value;
        if (!read_bytes(r, &value, 1) || value > 1) return fail(r->error, "Typed Unified boolean exceeds its domain");
        *(bool *)out = value != 0; return true;
    }
    case QA_UNIFIED_FIELD_U8: case QA_UNIFIED_FIELD_U16:
    case QA_UNIFIED_FIELD_U32: case QA_UNIFIED_FIELD_U64: case QA_UNIFIED_FIELD_SIZE:
        if (!unsigned_read(r, &u)) return false;
        if (kind == QA_UNIFIED_FIELD_U8) { if (u > UINT8_MAX) break; *(uint8_t *)out = (uint8_t)u; }
        else if (kind == QA_UNIFIED_FIELD_U16) { if (u > UINT16_MAX) break; *(uint16_t *)out = (uint16_t)u; }
        else if (kind == QA_UNIFIED_FIELD_U32) { if (u > UINT32_MAX) break; uint32_t v = (uint32_t)u; memcpy(out, &v, 4); }
        else if (kind == QA_UNIFIED_FIELD_SIZE) { if (u > SIZE_MAX) break; *(size_t *)out = (size_t)u; }
        else memcpy(out, &u, 8);
        return true;
    case QA_UNIFIED_FIELD_I8: case QA_UNIFIED_FIELD_I16:
    case QA_UNIFIED_FIELD_I32: case QA_UNIFIED_FIELD_I64:
        if (!signed_read(r, &s)) return false;
        if (kind == QA_UNIFIED_FIELD_I8) { if (s < INT8_MIN || s > INT8_MAX) break; *(int8_t *)out = (int8_t)s; }
        else if (kind == QA_UNIFIED_FIELD_I16) { if (s < INT16_MIN || s > INT16_MAX) break; *(int16_t *)out = (int16_t)s; }
        else if (kind == QA_UNIFIED_FIELD_I32) { if (s < INT32_MIN || s > INT32_MAX) break; int32_t v = (int32_t)s; memcpy(out, &v, 4); }
        else memcpy(out, &s, 8);
        return true;
    case QA_UNIFIED_FIELD_F32: case QA_UNIFIED_FIELD_F64: {
        uint8_t tag;
        if (!read_bytes(r, &tag, 1)) return false;
        if (tag == 0) { memset(out, 0, scalar_size(kind)); return true; }
        if (tag == 1) {
            if (!signed_read(r, &s)) return false;
            if (kind == QA_UNIFIED_FIELD_F32) {
                if (s < INT32_MIN || s > INT32_MAX || (int64_t)(float)s != s) break;
                float value = (float)s; memcpy(out, &value, 4);
            } else {
                if (s < -(int64_t)QA_UNIFIED_SAFE_INTEGER || s > (int64_t)QA_UNIFIED_SAFE_INTEGER) break;
                double value = (double)s; memcpy(out, &value, 8);
            }
            return true;
        }
        if (tag != 2 || !little_read(r, scalar_size(kind), &u)) return fail(r->error, "Invalid typed Unified float representation");
        if (kind == QA_UNIFIED_FIELD_F32) {
            uint32_t word = (uint32_t)u; memcpy(out, &word, 4);
        } else {
            memcpy(out, &u, 8);
        }
        return true;
    }
    default: return fail(r->error, "Unknown typed Unified scalar field");
    }
    return fail(r->error, "Typed Unified scalar exceeds its storage domain");
}

static bool field_equal(const qa_unified_field *, const void *, const void *, const qa_strings *, const qa_strings *);
static const qa_unified_record_layout *variant_layout(const qa_unified_field *field, const void *value)
{
    if (!value) return NULL;
    const uint8_t *tag = (const uint8_t *)value + field->count_offset;
    int32_t selected;
    if (field->kind == QA_UNIFIED_FIELD_VARIANT_BOOL) selected = *(const bool *)tag ? 1 : 0;
    else memcpy(&selected, tag, sizeof(selected));
    return selected >= 0 && (size_t)selected < field->maximum ? field->variants[(size_t)selected] : NULL;
}
bool qa_unified_record_equal(const qa_unified_record_layout *layout, const void *a, const void *b,
    const qa_strings *strings, const qa_strings *baseline_strings)
{
    if (a == b) return true;
    if (!a) { const void *swap = a; a = b; b = swap;
        const qa_strings *table = strings; strings = baseline_strings; baseline_strings = table; }
    for (size_t i = 0; i < layout->field_count; ++i)
        if (!field_equal(layout->fields + i, a, b, strings, baseline_strings)) return false;
    return true;
}
static bool field_equal(const qa_unified_field *f, const void *a, const void *b,
    const qa_strings *strings, const qa_strings *baseline_strings)
{
    const void *x = field_read(a, f), *y = field_read(b, f);
    size_t scalar = scalar_size(f->kind);
    uint64_t zero = 0;
    if (scalar) return !memcmp(x, y ? y : &zero, scalar);
    switch (f->kind) {
    case QA_UNIFIED_FIELD_NAME: {
        qa_string_id p = *(const qa_string_id *)x, q = y ? *(const qa_string_id *)y : QA_STRING_NONE;
        if (strings == baseline_strings) return p == q;
        if (!p || !q) return p == q;
        qa_bytes left = qa_strings_text(strings, p), right = qa_strings_text(baseline_strings, q);
        return left.data && right.data && left.size == right.size &&
            (!left.size || !memcmp(left.data, right.data, left.size));
    }
    case QA_UNIFIED_FIELD_STRING: {
        const char *p = *(char *const *)x, *q = y ? *(char *const *)y : NULL;
        return p == q || (p && q && !strcmp(p, q));
    }
    case QA_UNIFIED_FIELD_BYTES: {
        const qa_buffer *p = x, *q = y;
        return p->size == (q ? q->size : 0) && (!p->size || !memcmp(p->data, q->data, p->size));
    }
    case QA_UNIFIED_FIELD_RAW:
        if (y) return !memcmp(x, y, f->maximum);
        for (size_t i = 0; i < f->maximum; ++i) if (((const uint8_t *)x)[i]) return false;
        return true;
    case QA_UNIFIED_FIELD_RECORD: return qa_unified_record_equal(f->record, x, y, strings, baseline_strings);
    case QA_UNIFIED_FIELD_POINTER: {
        const void *next = pointer_read(x), *prior = y ? pointer_read(y) : NULL;
        return (!next && !prior) || (next && prior && qa_unified_record_equal(f->record, next, prior, strings, baseline_strings));
    }
    case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
        const qa_unified_record_layout *layout = variant_layout(f, a);
        return layout && (!b || layout == variant_layout(f, b)) && qa_unified_record_equal(layout, x, y, strings, baseline_strings);
    }
    case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
        size_t count = f->kind != QA_UNIFIED_FIELD_FIXED ? *(const size_t *)((const uint8_t *)a + f->count_offset) : f->maximum;
        size_t old = f->kind != QA_UNIFIED_FIELD_FIXED ? (b ? *(const size_t *)((const uint8_t *)b + f->count_offset) : 0) : f->maximum;
        if (count != old) return false;
        if (f->kind == QA_UNIFIED_FIELD_ARRAY) { x = pointer_read(x); y = y ? pointer_read(y) : NULL; }
        for (size_t i = 0; i < count; ++i)
            if (!qa_unified_record_equal(f->record, (const uint8_t *)x + i * f->record->size,
                y ? (const uint8_t *)y + i * f->record->size : NULL, strings, baseline_strings)) return false;
        return true;
    }
    default: return false;
    }
}
void qa_unified_record_dispose(const qa_unified_record_layout *layout, void *value)
{
    if (!value) return;
    for (size_t i = 0; i < layout->field_count; ++i) {
        const qa_unified_field *f = layout->fields + i; void *p = field_write(value, f);
        switch (f->kind) {
        case QA_UNIFIED_FIELD_STRING: free(*(char **)p); *(char **)p = NULL; break;
        case QA_UNIFIED_FIELD_BYTES: qa_buffer_free(p); break;
        case QA_UNIFIED_FIELD_RECORD: qa_unified_record_dispose(f->record, p); break;
        case QA_UNIFIED_FIELD_POINTER:
            qa_unified_record_dispose(f->record, pointer_read(p)); free(pointer_read(p)); pointer_write(p, NULL); break;
        case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
            const qa_unified_record_layout *selected = variant_layout(f, value);
            if (selected) qa_unified_record_dispose(selected, p);
            break;
        }
        case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
            size_t count = f->kind != QA_UNIFIED_FIELD_FIXED ? *(size_t *)((uint8_t *)value + f->count_offset) : f->maximum;
            void *rows = f->kind == QA_UNIFIED_FIELD_ARRAY ? pointer_read(p) : p;
            if (rows) for (size_t row = 0; row < count; ++row)
                qa_unified_record_dispose(f->record, (uint8_t *)rows + row * f->record->size);
            if (f->kind == QA_UNIFIED_FIELD_ARRAY) {
                free(rows); pointer_write(p, NULL); *(size_t *)((uint8_t *)value + f->count_offset) = 0;
            }
            break;
        }
        default: break;
        }
    }
}
static bool record_write(qa_unified_builder *, const qa_unified_record_layout *, const void *, const void *, unsigned, qa_error *);
static bool record_read(record_reader *, const qa_unified_record_layout *, const void *, void *, unsigned);
static bool field_clone(record_reader *, const qa_unified_field *, const void *, void *, unsigned);

static int actor_compare(qa_actor_id a, qa_actor_id b)
{
    if (a.registry != b.registry) return a.registry < b.registry ? -1 : 1;
    if (a.slot != b.slot) return a.slot < b.slot ? -1 : 1;
    return a.generation == b.generation ? 0 : a.generation < b.generation ? -1 : 1;
}
static int key_compare(const qa_unified_record_layout *layout, const void *left, const void *right)
{
    const uint8_t *a=(const uint8_t *)left+layout->key_offset, *b=(const uint8_t *)right+layout->key_offset;
    if (layout->key_kind==QA_UNIFIED_KEY_ACTOR) {
        qa_actor_id x,y; memcpy(&x,a,sizeof(x)); memcpy(&y,b,sizeof(y)); return actor_compare(x,y);
    }
    if (layout->key_kind==QA_UNIFIED_KEY_U32) {
        uint32_t x,y; memcpy(&x,a,sizeof(x)); memcpy(&y,b,sizeof(y)); return x==y?0:x<y?-1:1;
    }
    int32_t x,y; memcpy(&x,a,sizeof(x)); memcpy(&y,b,sizeof(y)); return x==y?0:x<y?-1:1;
}
static bool key_valid(const qa_unified_record_layout *layout, const void *row)
{
    const uint8_t *key=(const uint8_t *)row+layout->key_offset;
    if (layout->key_kind==QA_UNIFIED_KEY_ACTOR) { qa_actor_id actor; memcpy(&actor,key,sizeof(actor)); return actor.registry!=0; }
    if (layout->key_kind==QA_UNIFIED_KEY_I32) { int32_t number; memcpy(&number,key,sizeof(number)); return number>=0; }
    return layout->key_kind==QA_UNIFIED_KEY_U32;
}
static size_t key_baseline(const qa_unified_record_layout *layout, const void *rows, size_t count, const void *row)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order=key_compare(layout,(const uint8_t *)rows+middle*layout->size,row);
        if (order < 0) low = middle + 1; else high = middle;
    }
    if (low<count && !key_compare(layout,(const uint8_t *)rows+low*layout->size,row)) return low;
    return count;
}
static bool field_encode(qa_unified_builder *b, const qa_unified_field *f, const void *value, const void *baseline, unsigned depth, qa_error *e)
{
    const void *p = field_read(value, f), *old = field_read(baseline, f);
    if (scalar_size(f->kind)) return scalar_write(b, f->kind, p, e);
    switch (f->kind) {
    case QA_UNIFIED_FIELD_RAW: return append(b, p, f->maximum, e);
    case QA_UNIFIED_FIELD_NAME: case QA_UNIFIED_FIELD_STRING: {
        const char *text = f->kind == QA_UNIFIED_FIELD_NAME ?
            qa_strings_cstr(b->strings, *(const qa_string_id *)p) : *(char *const *)p;
        size_t length = text ? strlen(text) : 0, maximum = f->maximum ? f->maximum : RECORD_BYTES;
        if (length > maximum || (text && !qa_utf8_valid((qa_bytes){(const uint8_t *)text, length})))
            return fail(e, "Typed Unified text exceeds its UTF-8 boundary");
        return unsigned_write(b, text ? (uint64_t)length + 1 : 0, e) && (!length || append(b, text, length, e));
    }
    case QA_UNIFIED_FIELD_BYTES: {
        const qa_buffer *bytes = p; size_t maximum = f->maximum ? f->maximum : RECORD_BYTES;
        if (bytes->size > maximum || (bytes->size && !bytes->data)) return fail(e, "Typed Unified raw bytes exceed their field");
        return unsigned_write(b, bytes->size, e) && append(b, bytes->data, bytes->size, e);
    }
    case QA_UNIFIED_FIELD_RECORD: return record_write(b, f->record, p, old, depth + 1, e);
    case QA_UNIFIED_FIELD_POINTER: {
        const void *next = pointer_read(p), *prior = old ? pointer_read(old) : NULL;
        return byte(b, next ? 1 : 0, e) && (!next || record_write(b, f->record, next, prior, depth + 1, e));
    }
    case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
        const qa_unified_record_layout *selected = variant_layout(f, value);
        if (!selected) return fail(e, "Typed Unified union has an unknown Source discriminator");
        return record_write(b, selected, p, selected == variant_layout(f, baseline) ? old : NULL, depth + 1, e);
    }
    case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
        size_t count = f->kind != QA_UNIFIED_FIELD_FIXED ? *(const size_t *)((const uint8_t *)value + f->count_offset) : f->maximum;
        size_t prior_count = baseline && f->kind != QA_UNIFIED_FIELD_FIXED ? *(const size_t *)((const uint8_t *)baseline + f->count_offset) : old ? f->maximum : 0;
        if (f->kind == QA_UNIFIED_FIELD_ARRAY) {
            p = pointer_read(p); old = old ? pointer_read(old) : NULL;
            if (count > f->maximum || (count && !p) || !unsigned_write(b, count, e)) return fail(e, "Typed Unified array exceeds its declared extent");
        }
        if (f->kind == QA_UNIFIED_FIELD_INLINE_ARRAY && (count > f->maximum || !unsigned_write(b, count, e)))
            return fail(e, "Typed Unified inline array exceeds its declared extent");
        for (size_t i = 0; i < count; ++i) {
            const void *row = (const uint8_t *)p + i * f->record->size;
            size_t prior = i < prior_count ? i : prior_count;
            if (f->kind == QA_UNIFIED_FIELD_ARRAY && f->record->key_kind != QA_UNIFIED_KEY_NONE) {
                prior = key_baseline(f->record, old, prior_count, row);
                if (!unsigned_write(b, prior < prior_count ? (uint64_t)prior + 1 : 0, e)) return false;
            }
            const void *base = prior < prior_count ? (const uint8_t *)old + prior * f->record->size : NULL;
            if (!record_write(b, f->record, row, base, depth + 1, e)) return false;
        }
        return true;
    }
    default: return fail(e, "Unknown typed Unified field layout");
    }
}
static bool record_write(qa_unified_builder *b, const qa_unified_record_layout *layout, const void *value, const void *baseline, unsigned depth, qa_error *e)
{
    if (!layout || !value || depth > RECORD_DEPTH || layout->field_count > RECORD_FIELDS)
        return fail(e, "Typed Unified record exceeds its fixed layout");
    uint8_t mask[RECORD_FIELDS / 8] = {0};
    size_t bytes = (layout->field_count + 7) / 8;
    for (size_t i = 0; i < layout->field_count; ++i)
        if (!field_equal(layout->fields + i, value, baseline, b->strings, b->baseline_strings)) mask[i / 8] |= (uint8_t)(1u << (i % 8));
    if (!append(b, mask, bytes, e)) return false;
    for (size_t i = 0; i < layout->field_count; ++i)
        if ((mask[i / 8] & (1u << (i % 8))) && !field_encode(b, layout->fields + i, value, baseline, depth, e)) return false;
    return true;
}
static bool field_decode(record_reader *r, const qa_unified_field *f, const void *baseline, void *value, unsigned depth)
{
    void *p = field_write(value, f); const void *old = field_read(baseline, f);
    if (scalar_size(f->kind)) return scalar_read(r, f->kind, p);
    switch (f->kind) {
    case QA_UNIFIED_FIELD_RAW: return read_bytes(r, p, f->maximum);
    case QA_UNIFIED_FIELD_NAME: case QA_UNIFIED_FIELD_STRING: {
        uint64_t extent;
        if (!unsigned_read(r, &extent)) return false;
        if (!extent) return true;
        size_t maximum = f->maximum ? f->maximum : RECORD_BYTES;
        if (extent - 1 > maximum || extent - 1 > r->bytes.size - r->at) return fail(r->error, "Typed Unified text exceeds its byte boundary");
        size_t length = (size_t)extent - 1; qa_bytes bytes = {r->bytes.data + r->at, length};
        if (memchr(bytes.data, 0, length) || !qa_utf8_valid(bytes)) return fail(r->error, "Typed Unified text is not NUL-free UTF-8");
        if (f->kind == QA_UNIFIED_FIELD_NAME) {
            if (!qa_strings_intern(r->strings, bytes, p, r->error)) return false;
            r->at += length; return true;
        }
        char *text = allocate(r, (size_t)extent, 1);
        if (!text) return false;
        *(char **)p = text; return read_bytes(r, text, length);
    }
    case QA_UNIFIED_FIELD_BYTES: {
        uint64_t extent; size_t maximum = f->maximum ? f->maximum : RECORD_BYTES;
        if (!unsigned_read(r, &extent) || extent > maximum || extent > r->bytes.size - r->at)
            return fail(r->error, "Typed Unified raw bytes exceed their byte boundary");
        qa_buffer *bytes = p;
        if (!extent) return true;
        bytes->data = allocate(r, (size_t)extent, 1);
        if (!bytes->data) return false;
        bytes->size = (size_t)extent; return read_bytes(r, bytes->data, bytes->size);
    }
    case QA_UNIFIED_FIELD_RECORD: return record_read(r, f->record, old, p, depth + 1);
    case QA_UNIFIED_FIELD_POINTER: {
        uint8_t present;
        if (!read_bytes(r, &present, 1) || present > 1) return fail(r->error, "Typed Unified pointer presence is invalid");
        if (!present) return true;
        void *row = allocate(r, 1, f->record->size);
        if (!row) return false;
        pointer_write(p, row); return record_read(r, f->record, old ? pointer_read(old) : NULL, row, depth + 1);
    }
    case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
        const qa_unified_record_layout *selected = variant_layout(f, value);
        if (!selected) return fail(r->error, "Typed Unified union has an unknown Source discriminator");
        return record_read(r, selected, selected == variant_layout(f, baseline) ? old : NULL, p, depth + 1);
    }
    case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
        uint64_t extent = f->maximum;
        size_t old_count = old ? (f->kind != QA_UNIFIED_FIELD_FIXED ? *(const size_t *)((const uint8_t *)baseline + f->count_offset) : f->maximum) : 0;
        if (f->kind == QA_UNIFIED_FIELD_ARRAY) {
            if (!unsigned_read(r, &extent) || extent > f->maximum) return fail(r->error, "Typed Unified array exceeds its fixed bound");
            old = old ? pointer_read(old) : NULL;
            *(size_t *)((uint8_t *)value + f->count_offset) = (size_t)extent;
            if (extent) {
                void *rows = allocate(r, (size_t)extent, f->record->size);
                if (!rows) return false;
                pointer_write(p, rows);
            }
            p = pointer_read(p);
        }
        if (f->kind == QA_UNIFIED_FIELD_INLINE_ARRAY) {
            if (!unsigned_read(r, &extent) || extent > f->maximum)
                return fail(r->error, "Typed Unified inline array exceeds its fixed bound");
            *(size_t *)((uint8_t *)value + f->count_offset) = (size_t)extent;
        }
        for (size_t i = 0; i < (size_t)extent; ++i) {
            uint64_t index = i < old_count ? (uint64_t)i + 1 : 0;
            if (f->kind == QA_UNIFIED_FIELD_ARRAY && f->record->key_kind != QA_UNIFIED_KEY_NONE) {
                if (!unsigned_read(r, &index) || index > old_count) return fail(r->error, "Typed Unified entity delta lost its baseline row");
            }
            const void *base = index ? (const uint8_t *)old + ((size_t)index - 1) * f->record->size : NULL;
            void *row = (uint8_t *)p + i * f->record->size;
            if (!record_read(r, f->record, base, row, depth + 1)) return false;
            if (f->kind == QA_UNIFIED_FIELD_ARRAY && f->record->key_kind != QA_UNIFIED_KEY_NONE) {
                if (!key_valid(f->record,row) || (base && key_compare(f->record,base,row)))
                    return fail(r->error,"Typed Unified entity delta changed its actual baseline key");
                if (i && key_compare(f->record,(uint8_t *)row-f->record->size,row)>=0)
                    return fail(r->error,"Typed Unified entities are not in Source key order");
            }
        }
        return true;
    }
    default: return fail(r->error, "Unknown typed Unified field layout");
    }
}
static bool field_clone(record_reader *r, const qa_unified_field *f, const void *source, void *value, unsigned depth)
{
    if (depth > RECORD_DEPTH) return fail(r->error, "Typed Unified clone exceeds its fixed record depth");
    if (!source) return true;
    const void *p = field_read(source, f); void *out = field_write(value, f);
    size_t scalar = scalar_size(f->kind);
    if (scalar || f->kind == QA_UNIFIED_FIELD_RAW) { memcpy(out, p, scalar ? scalar : f->maximum); return true; }
    switch (f->kind) {
    case QA_UNIFIED_FIELD_NAME: {
        qa_string_id id = *(const qa_string_id *)p;
        if (!id || r->strings == r->baseline_strings) { *(qa_string_id *)out = id; return true; }
        return qa_strings_intern(r->strings, qa_strings_text(r->baseline_strings, id), out, r->error);
    }
    case QA_UNIFIED_FIELD_STRING: {
        const char *text = *(char *const *)p;
        if (!text) return true;
        size_t count = strlen(text) + 1; char *copy = allocate(r, count, 1);
        if (!copy) return false;
        memcpy(copy, text, count); *(char **)out = copy; return true;
    }
    case QA_UNIFIED_FIELD_BYTES: {
        const qa_buffer *bytes = p; qa_buffer *copy = out;
        if (!bytes->size) return true;
        copy->data = allocate(r, bytes->size, 1);
        if (!copy->data) return false;
        copy->size = bytes->size; memcpy(copy->data, bytes->data, bytes->size); return true;
    }
    case QA_UNIFIED_FIELD_RECORD:
        for (size_t i = 0; i < f->record->field_count; ++i)
            if (!field_clone(r, f->record->fields + i, p, out, depth + 1)) return false;
        return true;
    case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
        const qa_unified_record_layout *selected = variant_layout(f, source);
        if (!selected || selected != variant_layout(f, value))
            return fail(r->error, "Typed Unified delta changed its Source union without its fields");
        for (size_t i = 0; i < selected->field_count; ++i)
            if (!field_clone(r, selected->fields + i, p, out, depth + 1)) return false;
        return true;
    }
    case QA_UNIFIED_FIELD_POINTER: {
        const void *row = pointer_read(p);
        if (!row) return true;
        void *copy = allocate(r, 1, f->record->size);
        if (!copy) return false;
        pointer_write(out, copy);
        for (size_t i = 0; i < f->record->field_count; ++i)
            if (!field_clone(r, f->record->fields + i, row, copy, depth + 1)) return false;
        return true;
    }
    case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
        size_t count = f->kind != QA_UNIFIED_FIELD_FIXED ? *(const size_t *)((const uint8_t *)source + f->count_offset) : f->maximum;
        if (f->kind != QA_UNIFIED_FIELD_FIXED)
            *(size_t *)((uint8_t *)value + f->count_offset) = count;
        if (f->kind == QA_UNIFIED_FIELD_ARRAY) {
            p = pointer_read(p);
            if (count) {
                void *rows = allocate(r, count, f->record->size);
                if (!rows) return false;
                pointer_write(out, rows);
            }
            out = pointer_read(out);
        }
        for (size_t row = 0; row < count; ++row)
            for (size_t i = 0; i < f->record->field_count; ++i)
                if (!field_clone(r, f->record->fields + i,
                    (const uint8_t *)p + row * f->record->size,
                    (uint8_t *)out + row * f->record->size, depth + 1)) return false;
        return true;
    }
    default: return fail(r->error, "Unknown typed Unified field layout");
    }
}
bool qa_unified_record_clone_alloc(const qa_unified_record_layout *layout, const void *source,
    void *out, qa_unified_clone_alloc_fn allocator, void *context, qa_error *error)
{
    if (!layout || !source || !out) return fail(error, "Typed Unified clone requires its fixed record owner");
    record_reader reader = {.error = error, .clone_allocate = allocator, .clone_context = context};
    for (size_t i = 0; i < layout->field_count; ++i)
        if (!field_clone(&reader, layout->fields + i, source, out, 0)) return false;
    return true;
}
bool qa_unified_record_clone(const qa_unified_record_layout *layout, const void *source,
    void *out, qa_error *error)
{
    if (qa_unified_record_clone_alloc(layout, source, out, NULL, NULL, error)) return true;
    if (layout && source && out) {
        qa_unified_record_dispose(layout, out); memset(out, 0, layout->size);
    }
    return false;
}
static bool record_read(record_reader *r, const qa_unified_record_layout *layout, const void *baseline, void *value, unsigned depth)
{
    if (!layout || depth > RECORD_DEPTH || layout->field_count > RECORD_FIELDS)
        return fail(r->error, "Typed Unified record exceeds its fixed layout");
    uint8_t mask[RECORD_FIELDS / 8] = {0}; size_t bytes = (layout->field_count + 7) / 8;
    if (!read_bytes(r, mask, bytes)) return false;
    if (layout->field_count % 8 && (mask[bytes - 1] >> (layout->field_count % 8)))
        return fail(r->error, "Typed Unified record has unknown field bits");
    for (size_t i = 0; i < layout->field_count; ++i) {
        bool changed = (mask[i / 8] & (1u << (i % 8))) != 0;
        const qa_unified_field *field = layout->fields + i;
        bool repeated_count = false; size_t previous_count = 0;
        if (field->kind == QA_UNIFIED_FIELD_INLINE_ARRAY) {
            for (size_t j = 0; j < i; ++j)
                if (layout->fields[j].kind == QA_UNIFIED_FIELD_INLINE_ARRAY &&
                    layout->fields[j].count_offset == field->count_offset) repeated_count = true;
            previous_count = *(size_t *)((uint8_t *)value + field->count_offset);
        }
        bool okay = changed ? field_decode(r, layout->fields + i, baseline, value, depth) :
            field_clone(r, layout->fields + i, baseline, value, depth);
        if (!okay) return false;
        if (repeated_count) {
            size_t *count = (size_t *)((uint8_t *)value + field->count_offset);
            if (*count != previous_count) {
                if (*count < previous_count) *count = previous_count;
                return fail(r->error, "Typed Unified inline arrays disagree on their actual row count");
            }
        }
    }
    return true;
}
bool qa_unified_record_delta_encode(const qa_unified_record_layout *layout, const void *value,
    const void *baseline, size_t maximum, qa_buffer *out, const qa_strings *strings, qa_error *error)
{
    if (!out || !maximum) return fail(error, "Typed Unified encoding requires its bounded output");
    qa_unified_builder builder = {.maximum = maximum, .strings = strings, .baseline_strings = strings};
    if (!qa_unified_record_delta_write(layout,value,baseline,&builder,error)) { free(builder.data); return false; }
    *out = (qa_buffer){builder.data, builder.size}; return true;
}
bool qa_unified_record_delta_write(const qa_unified_record_layout *layout, const void *value,
    const void *baseline, qa_unified_builder *out, qa_error *error)
{
    if (!out || !out->maximum || out->size>out->maximum)
        return fail(error,"Typed Unified encoding requires its bounded reusable output");
    return record_write(out,layout,value,baseline,0,error);
}
bool qa_unified_record_delta_decode(const qa_unified_record_layout *layout, qa_bytes bytes,
    const void *baseline, void *out, qa_unified_frame_lease *lease, qa_strings *strings,
    const qa_strings *baseline_strings, qa_error *error)
{
    if (!out || !bytes.data || !bytes.size) return fail(error, "Typed Unified decoding requires its fixed record output");
    record_reader reader = {.bytes = bytes, .error = error, .lease = lease,
        .strings = strings, .baseline_strings = baseline_strings};
    bool okay = record_read(&reader, layout, baseline, out, 0) && reader.at == bytes.size;
    if (!okay) {
        if (!lease) qa_unified_record_dispose(layout, out);
        memset(out, 0, layout->size);
        if (error && error->code == QA_OK) fail(error, "Typed Unified record has trailing bytes");
    }
    return okay;
}
static bool measure_add(size_t *total, size_t count, size_t size, qa_error *error)
{
    if (size && count > (RECORD_BYTES - *total) / size)
        return fail(error, "Typed Unified state exceeds its owned memory bound");
    *total += count * size; return true;
}
static bool record_measure(const qa_unified_record_layout *layout, const void *value,
    size_t *total, unsigned depth, qa_error *error)
{
    if (!layout || !value || depth > RECORD_DEPTH || layout->field_count > RECORD_FIELDS)
        return fail(error, "Typed Unified state lost its fixed record layout");
    if (layout == &qa_unified_actor_layout) {
        const qa_actor_id *actor = value;
        if (!actor->registry && (actor->generation || actor->slot))
            return fail(error, "Typed Unified actor has an incomplete Source identity");
    }
    for (size_t i = 0; i < layout->field_count; ++i) {
        const qa_unified_field *f = layout->fields + i; const void *p = field_read(value, f);
        switch (f->kind) {
        case QA_UNIFIED_FIELD_NAME: break;
        case QA_UNIFIED_FIELD_STRING: {
            const char *text = *(char *const *)p;
            if (text) {
                size_t length = strlen(text);
                if ((f->maximum && length > f->maximum) ||
                    !qa_utf8_valid((qa_bytes){(const uint8_t *)text, length}) ||
                    !measure_add(total, length + 1, 1, error)) return false;
            }
            break;
        }
        case QA_UNIFIED_FIELD_BYTES: {
            const qa_buffer *bytes = p;
            if ((bytes->size && !bytes->data) || (f->maximum && bytes->size > f->maximum))
                return fail(error, "Typed Unified bytes exceed their actual owned extent");
            if (!measure_add(total, bytes->size, 1, error)) return false;
            break;
        }
        case QA_UNIFIED_FIELD_RECORD:
            if (!record_measure(f->record, p, total, depth + 1, error)) return false;
            break;
        case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
            const qa_unified_record_layout *selected = variant_layout(f, value);
            if (!selected || !record_measure(selected, p, total, depth + 1, error)) return false;
            break;
        }
        case QA_UNIFIED_FIELD_POINTER: {
            const void *row = pointer_read(p);
            if (row && (!measure_add(total, 1, f->record->size, error) ||
                !record_measure(f->record, row, total, depth + 1, error))) return false;
            break;
        }
        case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
            size_t count = f->kind == QA_UNIFIED_FIELD_FIXED ? f->maximum :
                *(const size_t *)((const uint8_t *)value + f->count_offset);
            if (count > f->maximum) return fail(error, "Typed Unified array exceeds its fixed Source bound");
            const void *rows = f->kind == QA_UNIFIED_FIELD_ARRAY ? pointer_read(p) : p;
            if (count && !rows) return fail(error, "Typed Unified array lost its owned rows");
            if (f->kind == QA_UNIFIED_FIELD_ARRAY && !measure_add(total, count, f->record->size, error)) return false;
            for (size_t row = 0; row < count; ++row) {
                const void *item = (const uint8_t *)rows + row * f->record->size;
                if (f->kind==QA_UNIFIED_FIELD_ARRAY && f->record->key_kind!=QA_UNIFIED_KEY_NONE) {
                    if (!key_valid(f->record,item)) return fail(error,"Typed Unified roster has no actual Source key");
                    if (row && key_compare(f->record,(const uint8_t *)item-f->record->size,item)>=0)
                        return fail(error,"Typed Unified roster is not in Source key order");
                }
                if (!record_measure(f->record, item, total, depth + 1, error)) return false;
            }
            break;
        }
        default: break;
        }
    }
    return true;
}
bool qa_unified_record_measure(const qa_unified_record_layout *layout, const void *value,
    size_t *out, qa_error *error)
{
    size_t total = layout ? layout->size : 0;
    if (!out || !record_measure(layout, value, &total, 0, error)) return false;
    *out = total; return true;
}

static bool actors_remap(const qa_unified_record_layout *layout, void *value,
    qa_unified_actor_remap_fn callback, void *context, unsigned depth, qa_error *error)
{
    if (depth > RECORD_DEPTH) return fail(error, "Typed Unified actor walk exceeds its fixed layout");
    if (layout == &qa_unified_actor_layout) {
        qa_actor_id *actor = value;
        if (!actor->registry && !actor->generation && !actor->slot) return true;
        qa_actor_id replacement;
        if (!callback(context, *actor, &replacement, error)) return false;
        *actor = replacement; return true;
    }
    for (size_t i = 0; i < layout->field_count; ++i) {
        const qa_unified_field *f = layout->fields + i; void *p = field_write(value, f);
        switch (f->kind) {
        case QA_UNIFIED_FIELD_RECORD:
            if (!actors_remap(f->record, p, callback, context, depth + 1, error)) return false;
            break;
        case QA_UNIFIED_FIELD_VARIANT: case QA_UNIFIED_FIELD_VARIANT_BOOL: {
            const qa_unified_record_layout *selected = variant_layout(f, value);
            if (!selected || !actors_remap(selected, p, callback, context, depth + 1, error)) return false;
            break;
        }
        case QA_UNIFIED_FIELD_POINTER:
            if (pointer_read(p) && !actors_remap(f->record, pointer_read(p), callback, context, depth + 1, error)) return false;
            break;
        case QA_UNIFIED_FIELD_ARRAY: case QA_UNIFIED_FIELD_INLINE_ARRAY: case QA_UNIFIED_FIELD_FIXED: {
            size_t count = f->kind == QA_UNIFIED_FIELD_FIXED ? f->maximum : *(size_t *)((uint8_t *)value + f->count_offset);
            void *rows = f->kind == QA_UNIFIED_FIELD_ARRAY ? pointer_read(p) : p;
            if (count > f->maximum || (count && !rows)) return fail(error, "Typed Unified actor walk lost its real rows");
            for (size_t row = 0; row < count; ++row)
                if (!actors_remap(f->record, (uint8_t *)rows + row * f->record->size, callback, context, depth + 1, error)) return false;
            break;
        }
        default: break;
        }
    }
    return true;
}
bool qa_unified_record_actor_remap(const qa_unified_record_layout *layout, void *value,
    qa_unified_actor_remap_fn callback, void *context, qa_error *error)
{
    if (!layout || !value || !callback) return fail(error, "Typed Unified actor walk requires its real owner");
    return actors_remap(layout, value, callback, context, 0, error);
}
