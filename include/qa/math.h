#ifndef QA_MATH_H
#define QA_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "Engine vectors require binary32 floats");

typedef struct qa_vec3 { float x, y, z; } qa_vec3;
typedef struct qa_bounds { qa_vec3 mins, maxs; } qa_bounds;

#define QA_BYTE_NORMAL_COUNT 162u
extern const qa_vec3 qa_byte_normals[QA_BYTE_NORMAL_COUNT];
/* Decode rejects unknown indices. Encode requires finite components, does not
 * normalize, selects the first greatest positive dot product, and maps zero to
 * index zero. Both leave output unchanged on failure. */
bool qa_byte_normal(uint8_t index, qa_vec3 *out);
bool qa_normal_byte(qa_vec3 normal, uint8_t *out);

/* Native ANGLE2SHORT uses float multiplication and division. AngleMod uses
 * the original double ratio before narrowing the float angle to one turn. */
uint16_t qa_angle_to_word(float angle);
float qa_angle_mod(float angle);

static inline qa_vec3 qa_v3(float x, float y, float z) { return (qa_vec3){x, y, z}; }
static inline qa_vec3 qa_vec_add(qa_vec3 a, qa_vec3 b) { return qa_v3(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline qa_vec3 qa_vec_sub(qa_vec3 a, qa_vec3 b) { return qa_v3(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline qa_vec3 qa_vec_scale(qa_vec3 a, float b) { return qa_v3(a.x*b, a.y*b, a.z*b); }
static inline float qa_vec_dot(qa_vec3 a, qa_vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline qa_vec3 qa_vec_cross(qa_vec3 a, qa_vec3 b) {
    return qa_v3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
static inline float qa_vec_length(qa_vec3 v) { return sqrtf(qa_vec_dot(v, v)); }
static inline qa_vec3 qa_vec_normalize(qa_vec3 v) {
    float length = qa_vec_length(v);
    return length > 0.0f ? qa_vec_scale(v, 1.0f/length) : qa_v3(0, 0, 0);
}
static inline qa_vec3 qa_axes_angles(const qa_vec3 axis[3]) {
    const float degrees = 57.29577951308232f;
    float horizontal = sqrtf(axis[0].x*axis[0].x + axis[0].y*axis[0].y);
    return qa_v3(atan2f(-axis[0].z, horizontal)*degrees,
                 atan2f(axis[0].y, axis[0].x)*degrees,
                 atan2f(axis[1].z, axis[2].z)*degrees);
}
static inline qa_vec3 qa_vec_lerp(qa_vec3 a, qa_vec3 b, float t) {
    return qa_vec_add(a, qa_vec_scale(qa_vec_sub(b, a), t));
}
static inline bool qa_vec_finite(qa_vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }
static inline bool qa_bounds_valid(qa_bounds b) {
    return qa_vec_finite(b.mins) && qa_vec_finite(b.maxs) &&
           b.mins.x <= b.maxs.x && b.mins.y <= b.maxs.y && b.mins.z <= b.maxs.z;
}
static inline bool qa_bounds_overlap(qa_bounds a, qa_bounds b) {
    return a.mins.x <= b.maxs.x && a.maxs.x >= b.mins.x &&
           a.mins.y <= b.maxs.y && a.maxs.y >= b.mins.y &&
           a.mins.z <= b.maxs.z && a.maxs.z >= b.mins.z;
}
static inline bool qa_bounds_contains(qa_bounds b, qa_vec3 p) {
    return p.x >= b.mins.x && p.x <= b.maxs.x &&
           p.y >= b.mins.y && p.y <= b.maxs.y &&
           p.z >= b.mins.z && p.z <= b.maxs.z;
}
static inline qa_bounds qa_bounds_translate(qa_bounds b, qa_vec3 v) {
    return (qa_bounds){qa_vec_add(b.mins,v),qa_vec_add(b.maxs,v)};
}
static inline qa_bounds qa_bounds_union(qa_bounds a, qa_bounds b) {
    return (qa_bounds){qa_v3(fminf(a.mins.x,b.mins.x),fminf(a.mins.y,b.mins.y),fminf(a.mins.z,b.mins.z)),
                       qa_v3(fmaxf(a.maxs.x,b.maxs.x),fmaxf(a.maxs.y,b.maxs.y),fmaxf(a.maxs.z,b.maxs.z))};
}

#endif
