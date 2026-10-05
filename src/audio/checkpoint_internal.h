#ifndef QA_AUDIO_CHECKPOINT_INTERNAL_H
#define QA_AUDIO_CHECKPOINT_INTERNAL_H
#include "qa/audio_save.h"
#include "qa/source_save.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 &&
               sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Audio checkpoint fields require binary32 and binary64");
static inline bool qa_ac_bad(qa_source_save_io *reader, const char *text) {
    if (!reader->failed) qa_error_set(reader->error, QA_ERROR_FORMAT, reader->offset, "%s", text);
    reader->failed = true; return false;
}
static inline bool qa_ac_write(qa_source_save_io *writer, const void *data, size_t count) {
    return qa_source_save_bytes(writer, (void *)data, count);
}
static inline bool qa_ac_read(qa_source_save_io *reader, size_t count, qa_bytes *out) {
    return qa_source_save_span(reader, count, out);
}
static inline bool qa_ac_u32(qa_source_save_io *w, uint32_t value) {
    return qa_source_save_u32(w, &value);
}
static inline bool qa_ac_u64(qa_source_save_io *w, uint64_t value) {
    return qa_source_save_u64(w, &value);
}
static inline uint32_t qa_ac_get32(qa_source_save_io *r) {
    uint32_t value = 0; (void)qa_source_save_u32(r, &value); return value;
}
static inline uint64_t qa_ac_get64(qa_source_save_io *r) {
    uint64_t value = 0; (void)qa_source_save_u64(r, &value); return value;
}
static inline int32_t qa_ac_geti32(qa_source_save_io *r) {
    int32_t value = 0; (void)qa_source_save_i32(r, &value); return value;
}
static inline int64_t qa_ac_geti64(qa_source_save_io *r) {
    int64_t value = 0; (void)qa_source_save_i64(r, &value); return value;
}
static inline bool qa_ac_float(qa_source_save_io *w, float value) {
    return qa_source_save_f32(w, &value);
}
static inline bool qa_ac_double(qa_source_save_io *w, double value) {
    return qa_source_save_f64(w, &value);
}
static inline float qa_ac_getfloat(qa_source_save_io *r) {
    float value = 0; (void)qa_source_save_f32(r, &value);
    if (!isfinite(value)) qa_ac_bad(r, "Nonfinite audio checkpoint field");
    return value;
}
static inline double qa_ac_getdouble(qa_source_save_io *r) {
    double value = 0; (void)qa_source_save_f64(r, &value);
    if (!isfinite(value)) qa_ac_bad(r, "Nonfinite audio checkpoint field");
    return value;
}
static inline bool qa_ac_vec(qa_source_save_io *w, qa_vec3 value) {
    return qa_ac_float(w, value.x) && qa_ac_float(w, value.y) && qa_ac_float(w, value.z);
}
static inline qa_vec3 qa_ac_getvec(qa_source_save_io *r) {
    qa_vec3 v; v.x = qa_ac_getfloat(r); v.y = qa_ac_getfloat(r); v.z = qa_ac_getfloat(r); return v;
}
static inline bool qa_ac_bool(qa_source_save_io *r) {
    uint32_t value = qa_ac_get32(r); if (value > 1) qa_ac_bad(r, "Invalid audio checkpoint boolean"); return value != 0;
}
static inline bool qa_ac_blob(qa_source_save_io *w, qa_bytes bytes) {
    return qa_ac_u64(w, bytes.size) && qa_ac_write(w, bytes.data, bytes.size);
}
static inline bool qa_ac_getblob(qa_source_save_io *r, qa_bytes *out) {
    uint64_t count = qa_ac_get64(r);
    if (count > SIZE_MAX) return qa_ac_bad(r, "Audio checkpoint field exceeds address space");
    return qa_ac_read(r, (size_t)count, out);
}






static inline bool qa_ac_finish(qa_source_save_io *writer, qa_buffer *out) {
    bool ok = qa_source_save_finish(writer, out);
    qa_source_save_dispose(writer);
    return ok;
}
#endif
