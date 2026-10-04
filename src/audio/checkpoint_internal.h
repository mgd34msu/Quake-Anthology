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
bool qa_audio_environment_definition_shared(const qa_audio_environment *, const qa_audio_environment *);
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
static inline bool qa_ac_ref(qa_source_save_io *w, const qa_audio_checkpoint_refs *refs,
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
static inline uint64_t qa_ac_getref(qa_source_save_io *r, const qa_audio_checkpoint_refs *refs,
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
static inline bool qa_ac_put_listener(qa_source_save_io *w, const qa_audio_checkpoint_refs *refs,
                                      const qa_audio_listener *v) {
    bool ok = qa_ac_u32(w, v->seat) && qa_ac_ref(w, refs, QA_AUDIO_REFERENCE_ACTOR, v->actor) &&
        qa_ac_vec(w, v->origin);
    for (size_t i = 0; ok && i < 3; ++i) ok = qa_ac_vec(w, v->axis[i]);
    return ok && qa_ac_float(w, v->gain) && qa_ac_u32(w, v->underwater);
}
static inline void qa_ac_get_listener(qa_source_save_io *r, const qa_audio_checkpoint_refs *refs,
                                      qa_audio_listener *v, const char *invalid) {
    v->seat = qa_ac_get32(r); v->actor = qa_ac_getref(r, refs, QA_AUDIO_REFERENCE_ACTOR);
    v->origin = qa_ac_getvec(r);
    for (size_t i = 0; i < 3; ++i) v->axis[i] = qa_ac_getvec(r);
    v->gain = qa_ac_getfloat(r); v->underwater = qa_ac_bool(r);
    if (v->seat == QA_AUDIO_WORLD || v->gain < 0) qa_ac_bad(r, invalid);
}
static inline bool qa_ac_put_play(qa_source_save_io *w, const qa_audio_checkpoint_refs *refs, const qa_audio_play *v) {
    return qa_ac_u32(w, v->family) && qa_ac_ref(w, refs, QA_AUDIO_REFERENCE_ACTOR, v->actor) &&
        qa_ac_ref(w, refs, QA_AUDIO_REFERENCE_OWNER, v->owner) &&
        qa_ac_ref(w, refs, QA_AUDIO_REFERENCE_RESOURCE, v->resource_id) && qa_ac_u32(w, v->audience) &&
        qa_ac_u32(w, v->origin_kind) && qa_ac_ref(w, refs, QA_AUDIO_REFERENCE_ACTOR, v->origin_actor) &&
        qa_ac_vec(w, v->origin) && qa_ac_u32(w, (uint32_t)v->channel) && qa_ac_float(w, v->volume) &&
        qa_ac_float(w, v->attenuation) && qa_ac_double(w, v->delay_seconds) &&
        qa_ac_double(w, v->server_milliseconds) && qa_ac_u32(w, v->has_server_time);
}
static inline void qa_ac_get_play(qa_source_save_io *r, const qa_audio_checkpoint_refs *refs, qa_audio_play *v) {
    v->family = (qa_audio_family)qa_ac_get32(r); v->actor = qa_ac_getref(r, refs, QA_AUDIO_REFERENCE_ACTOR);
    v->owner = qa_ac_getref(r, refs, QA_AUDIO_REFERENCE_OWNER);
    v->resource_id = qa_ac_getref(r, refs, QA_AUDIO_REFERENCE_RESOURCE); v->audience = qa_ac_get32(r);
    v->origin_kind = (qa_audio_origin_kind)qa_ac_get32(r);
    v->origin_actor = qa_ac_getref(r, refs, QA_AUDIO_REFERENCE_ACTOR); v->origin = qa_ac_getvec(r);
    v->channel = qa_ac_geti32(r); v->volume = qa_ac_getfloat(r); v->attenuation = qa_ac_getfloat(r);
    v->delay_seconds = qa_ac_getdouble(r); v->server_milliseconds = qa_ac_getdouble(r); v->has_server_time = qa_ac_bool(r);
    if ((unsigned)v->family > QA_AUDIO_Q3 || (unsigned)v->origin_kind > QA_AUDIO_ACTOR ||
        v->volume < 0 || v->volume > 1 || v->attenuation < 0 ||
        (v->channel < 0 && !(v->family == QA_AUDIO_Q1 && v->channel == -1))) qa_ac_bad(r, "Invalid saved audio play policy");
}
static inline bool qa_ac_finish(qa_source_save_io *writer, qa_buffer *out) {
    bool ok = qa_source_save_finish(writer, out);
    qa_source_save_dispose(writer);
    return ok;
}
#endif
