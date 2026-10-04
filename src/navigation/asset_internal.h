#ifndef QA_NAV_ASSET_INTERNAL_H
#define QA_NAV_ASSET_INTERNAL_H
#include "qa/binary.h"
#include "qa/navigation_asset.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct qa_nav_asset {
    atomic_uint references;
    qa_nav_asset_kind kind;
    void *storage;
    qa_aas_view aas;
    qa_nav_source_view kex;
};
bool nav_aas_read(qa_bytes, const int32_t *, qa_nav_asset **, qa_error *);
bool nav_kex_read(qa_bytes, qa_nav_asset **, qa_error *);
bool nav_aas_allocate(const size_t counts[QA_AAS_LUMP_COUNT], qa_nav_asset **, qa_error *);
static inline qa_vec3 nav_vector(const uint8_t *p) {
    return qa_v3(qa_load_f32le(p), qa_load_f32le(p + 4), qa_load_f32le(p + 8));
}
static inline qa_bounds nav_bounds(const uint8_t *p) {
    return (qa_bounds){nav_vector(p), nav_vector(p + 12)};
}
static inline bool nav_layout(size_t *size, size_t count, size_t stride, size_t *offset) {
    const size_t alignment = _Alignof(max_align_t);
    if (*size > SIZE_MAX - (alignment - 1))
        return false;
    size_t start = (*size + alignment - 1) / alignment * alignment;
    if (count > (SIZE_MAX - start) / stride)
        return false;
    *offset = start;
    *size = start + count * stride;
    return true;
}
#endif
