#ifndef QA_AUDIO_CHECKPOINT_INTERNAL_H
#define QA_AUDIO_CHECKPOINT_INTERNAL_H
#include "qa/audio_save.h"
#include "qa/binary.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 &&
               sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Audio checkpoint fields require binary32 and binary64");
typedef struct qa_ac_writer {
    qa_buffer buffer;
    size_t capacity;
    qa_error *error;
    bool failed;
} qa_ac_writer;
typedef struct qa_ac_reader {
    qa_bytes bytes;
    size_t offset;
    qa_error *error;
    bool failed;
} qa_ac_reader;
static inline bool qa_ac_bad(qa_ac_reader *reader, const char *text) {
    if (!reader->failed) qa_error_set(reader->error, QA_ERROR_FORMAT, reader->offset, "%s", text);
    reader->failed = true; return false;
}
static inline bool qa_ac_write(qa_ac_writer *writer, const void *data, size_t count) {
    if (writer->failed) return false;
    if (count > SIZE_MAX - writer->buffer.size) {
        qa_error_set(writer->error, QA_ERROR_MEMORY, 0, "Audio checkpoint extent overflows storage");
        writer->failed = true; return false;
    }
    size_t size = writer->buffer.size + count;
    if (size > writer->capacity) {
        size_t capacity = writer->capacity ? writer->capacity : 1024;
        while (capacity < size) {
            if (capacity > SIZE_MAX / 2) { capacity = size; break; }
            capacity *= 2;
        }
        uint8_t *next = realloc(writer->buffer.data, capacity);
        if (!next) {
            qa_error_set(writer->error, QA_ERROR_MEMORY, 0, "Retaining audio checkpoint fields");
            writer->failed = true; return false;
        }
        writer->buffer.data = next; writer->capacity = capacity;
    }
    if (count) memcpy(writer->buffer.data + writer->buffer.size, data, count);
    writer->buffer.size = size; return true;
}
static inline bool qa_ac_read(qa_ac_reader *reader, size_t count, qa_bytes *out) {
    if (reader->failed) return false;
    if (count > reader->bytes.size - reader->offset) return qa_ac_bad(reader, "Truncated audio checkpoint field");
    *out = (qa_bytes){reader->bytes.data + reader->offset, count}; reader->offset += count; return true;
}
static inline bool qa_ac_u32(qa_ac_writer *w, uint32_t value) {
    uint8_t bytes[4]; qa_store_u32le(bytes, value); return qa_ac_write(w, bytes, 4);
}
static inline bool qa_ac_u64(qa_ac_writer *w, uint64_t value) {
    uint8_t bytes[8]; qa_store_u64le(bytes, value); return qa_ac_write(w, bytes, 8);
}
static inline uint32_t qa_ac_get32(qa_ac_reader *r) {
    qa_bytes bytes; return qa_ac_read(r, 4, &bytes) ? qa_load_u32le(bytes.data) : 0;
}
static inline uint64_t qa_ac_get64(qa_ac_reader *r) {
    qa_bytes bytes; return qa_ac_read(r, 8, &bytes) ? qa_load_u64le(bytes.data) : 0;
}
static inline int32_t qa_ac_geti32(qa_ac_reader *r) {
    uint32_t bits = qa_ac_get32(r); int32_t value; memcpy(&value, &bits, 4); return value;
}
static inline int64_t qa_ac_geti64(qa_ac_reader *r) {
    uint64_t bits = qa_ac_get64(r); int64_t value; memcpy(&value, &bits, 8); return value;
}
static inline bool qa_ac_float(qa_ac_writer *w, float value) {
    uint32_t bits; memcpy(&bits, &value, 4); return qa_ac_u32(w, bits);
}
static inline bool qa_ac_double(qa_ac_writer *w, double value) {
    uint64_t bits; memcpy(&bits, &value, 8); return qa_ac_u64(w, bits);
}
static inline float qa_ac_getfloat(qa_ac_reader *r) {
    uint32_t bits = qa_ac_get32(r); float value; memcpy(&value, &bits, 4);
    if (!isfinite(value)) qa_ac_bad(r, "Nonfinite audio checkpoint field");
    return value;
}
static inline double qa_ac_getdouble(qa_ac_reader *r) {
    uint64_t bits = qa_ac_get64(r); double value; memcpy(&value, &bits, 8);
    if (!isfinite(value)) qa_ac_bad(r, "Nonfinite audio checkpoint field");
    return value;
}
static inline bool qa_ac_vec(qa_ac_writer *w, qa_vec3 value) {
    return qa_ac_float(w, value.x) && qa_ac_float(w, value.y) && qa_ac_float(w, value.z);
}
static inline qa_vec3 qa_ac_getvec(qa_ac_reader *r) {
    qa_vec3 v; v.x = qa_ac_getfloat(r); v.y = qa_ac_getfloat(r); v.z = qa_ac_getfloat(r); return v;
}
static inline bool qa_ac_bool(qa_ac_reader *r) {
    uint32_t value = qa_ac_get32(r); if (value > 1) qa_ac_bad(r, "Invalid audio checkpoint boolean"); return value != 0;
}
static inline bool qa_ac_blob(qa_ac_writer *w, qa_bytes bytes) {
    return qa_ac_u64(w, bytes.size) && qa_ac_write(w, bytes.data, bytes.size);
}
static inline bool qa_ac_getblob(qa_ac_reader *r, qa_bytes *out) {
    uint64_t count = qa_ac_get64(r);
    if (count > SIZE_MAX) return qa_ac_bad(r, "Audio checkpoint field exceeds address space");
    return qa_ac_read(r, (size_t)count, out);
}
static inline bool qa_ac_ref(qa_ac_writer *w, const qa_audio_checkpoint_refs *refs,
                            qa_audio_reference_kind kind, uint64_t id) {
    if (!id || id == UINT64_MAX) return qa_ac_u32(w, id ? 1 : 0);
    qa_buffer bytes = {0};
    if (!refs || !refs->encode || !refs->encode(refs->context, kind, id, &bytes, w->error)) {
        if (!refs || !refs->encode) qa_error_set(w->error, QA_ERROR_ARGUMENT, 0, "Audio portable reference encoder is absent");
        qa_buffer_free(&bytes); w->failed = true; return false;
    }
    bool ok = qa_ac_u32(w, 2) && qa_ac_blob(w, (qa_bytes){bytes.data, bytes.size});
    qa_buffer_free(&bytes); return ok;
}
static inline uint64_t qa_ac_getref(qa_ac_reader *r, const qa_audio_checkpoint_refs *refs,
                                    qa_audio_reference_kind kind) {
    uint32_t type = qa_ac_get32(r); if (type < 2) return type ? UINT64_MAX : 0;
    if (type != 2) { qa_ac_bad(r, "Invalid audio portable reference tag"); return 0; }
    qa_bytes bytes; uint64_t id = 0;
    if (!qa_ac_getblob(r, &bytes)) return 0;
    if (!refs || !refs->decode || !refs->decode(refs->context, kind, bytes, &id, r->error)) {
        if (!refs || !refs->decode) qa_error_set(r->error, QA_ERROR_ARGUMENT, r->offset, "Audio portable reference decoder is absent");
        r->failed = true; return 0;
    }
    if (!id || id == UINT64_MAX) qa_ac_bad(r, "Audio portable reference resolved to an empty identity");
    return id;
}
static inline bool qa_ac_finish(qa_ac_writer *writer, qa_buffer *out) {
    if (writer->failed) { qa_buffer_free(&writer->buffer); return false; }
    *out = writer->buffer; return true;
}
#endif
