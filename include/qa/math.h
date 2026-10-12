#ifndef QA_MATH_H
#define QA_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "Engine vectors require binary32 floats");

typedef struct qa_vec2 { float x, y; } qa_vec2;
typedef struct qa_vec3 { float x, y, z; } qa_vec3;
typedef struct qa_vec4 { float x, y, z, w; } qa_vec4;
typedef struct qa_bounds { qa_vec3 mins, maxs; } qa_bounds;
typedef enum qa_angle_math_rule {
    QA_ANGLE_FLOAT_MATH, QA_ANGLE_NATIVE_MATH, QA_ANGLE_VM_MATH, QA_ANGLE_FLOAT_NATIVE_MATH
} qa_angle_math_rule;
/* Native qsrc rounds a double radian product to float before sin/cos;
 * compiled VM code uses a float product. FLOAT retains sinf/cosf consumers. */
void qa_angle_vectors(qa_vec3,qa_vec3 *forward,qa_vec3 *right,qa_vec3 *up,qa_angle_math_rule);
void qa_angle_axis(qa_vec3,qa_vec3 axis[3],qa_angle_math_rule);
void qa_axis_multiply(const qa_vec3 left[3],const qa_vec3 right[3],qa_vec3 out[3]);
qa_vec3 qa_vec_perpendicular(qa_vec3);
qa_vec3 qa_vec_rotate_around(qa_vec3 axis,qa_vec3 point,float degrees);
float qa_inverse_sqrt(float);
typedef enum qa_direction_math_rule {
    QA_DIRECTION_FLOAT, QA_DIRECTION_FLOAT_RATIO, QA_DIRECTION_FLOAT_DIVIDE,
    QA_DIRECTION_VM, QA_DIRECTION_NATIVE, QA_DIRECTION_INTEGER, QA_DIRECTION_GEAR,
    QA_DIRECTION_NATIVE_RATIO
} qa_direction_math_rule;
enum qa_vector_angle_flags {
    QA_VECTOR_NEGATIVE_PITCH=1u, QA_VECTOR_WRAP_YAW=2u, QA_VECTOR_WRAP_PITCH=4u,
    QA_VECTOR_SQUARED_HORIZONTAL=8u, QA_VECTOR_VERTICAL_CARDINAL=16u,
    QA_VECTOR_VERTICAL_SIGNED=32u, QA_VECTOR_WIDE_HORIZONTAL=64u,
    QA_VECTOR_NEGATE_BEFORE_WRAP=128u, QA_VECTOR_DOUBLE_SQRT=256u,
    QA_VECTOR_POLAR_PITCH=512u,
    QA_VECTOR_VIEW=QA_VECTOR_NEGATIVE_PITCH|QA_VECTOR_WRAP_YAW,
    QA_VECTOR_WRAPPED=QA_VECTOR_WRAP_YAW|QA_VECTOR_WRAP_PITCH,
    QA_VECTOR_SOURCE=QA_VECTOR_VIEW|QA_VECTOR_WRAP_PITCH|QA_VECTOR_SQUARED_HORIZONTAL|QA_VECTOR_VERTICAL_CARDINAL
};
float qa_vec_yaw(qa_vec3,qa_direction_math_rule,bool wrap);
qa_vec3 qa_vec_angles(qa_vec3,qa_direction_math_rule,unsigned flags);

#define QA_BYTE_NORMAL_COUNT 162u
extern const qa_vec3 qa_byte_normals[QA_BYTE_NORMAL_COUNT];
/* Decode rejects unknown indices. Encode requires finite components, does not
 * normalize, selects the first greatest positive dot product, and maps zero to
 * index zero. Both leave output unchanged on failure. */
bool qa_byte_normal(uint8_t index, qa_vec3 *out);
bool qa_normal_byte(qa_vec3 normal, uint8_t *out);

typedef enum qa_angle_word_rule {
    QA_ANGLE_WORD_NATIVE, QA_ANGLE_WORD_VM, QA_ANGLE_WORD_PRECISE, QA_ANGLE_WORD_ROUNDED
} qa_angle_word_rule;
/* Preserve the source's multiplication order and rounding at the boundary. */
uint16_t qa_angle_to_word(float angle,qa_angle_word_rule);
float qa_angle_mod(float angle,qa_angle_word_rule);
float qa_angle_wrap(float angle);
float qa_angle_subtract(float first,float second);
float qa_angle_delta(float target,float current);
typedef enum qa_angle_lerp_rule {
    QA_ANGLE_LERP_SINGLE_TURN, QA_ANGLE_LERP_NORMALIZED_DELTA
} qa_angle_lerp_rule;
static inline float qa_angle_lerp(float from,float to,float fraction,qa_angle_lerp_rule rule) {
    if (rule==QA_ANGLE_LERP_NORMALIZED_DELTA) {
        float delta=fmodf(to-from,360);
        if (delta>180) delta-=360;
        if (delta< -180) delta+=360;
        return from+fraction*delta;
    }
    if (to-from>180) to-=360;
    if (to-from< -180) to+=360;
    return from+fraction*(to-from);
}

static inline qa_vec3 qa_v3(float x, float y, float z) { return (qa_vec3){x, y, z}; }
static inline bool qa_vec_equal(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static inline bool qa_bounds_equal(qa_bounds a, qa_bounds b) {
    return qa_vec_equal(a.mins, b.mins) && qa_vec_equal(a.maxs, b.maxs);
}
static inline qa_vec3 qa_vec_add(qa_vec3 a, qa_vec3 b) { return qa_v3(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline qa_vec3 qa_vec_sub(qa_vec3 a, qa_vec3 b) { return qa_v3(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline qa_vec3 qa_vec_scale(qa_vec3 a, float b) { return qa_v3(a.x*b, a.y*b, a.z*b); }
static inline qa_vec3 qa_vec_ma(qa_vec3 a,float scale,qa_vec3 b) { return qa_vec_add(a,qa_vec_scale(b,scale)); }
static inline float qa_vec_dot(qa_vec3 a, qa_vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline double qa_vec_dot_wide(qa_vec3 a,qa_vec3 b) { return (double)a.x*b.x+(double)a.y*b.y+(double)a.z*b.z; }
static inline qa_vec3 qa_vec_cross(qa_vec3 a, qa_vec3 b) {
    return qa_v3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
static inline float qa_vec_length(qa_vec3 v) { return sqrtf(qa_vec_dot(v, v)); }
static inline float qa_vec_normalize_length(qa_vec3 *v) {
    float length=qa_vec_length(*v);
    if(length!=0.0f) *v=qa_vec_scale(*v,1.0f/length);
    return length;
}
static inline qa_vec3 qa_vec_normalize(qa_vec3 v) { qa_vec_normalize_length(&v);return v; }
static inline qa_vec3 qa_vec_fast_normalize(qa_vec3 v) { return qa_vec_scale(v,qa_inverse_sqrt(qa_vec_dot(v,v))); }
static inline qa_vec3 qa_axis_to_local(qa_vec3 v,const qa_vec3 axis[3]) {
    return qa_v3(qa_vec_dot(v,axis[0]),qa_vec_dot(v,axis[1]),qa_vec_dot(v,axis[2]));
}
static inline qa_vec3 qa_axis_from_local(qa_vec3 v,const qa_vec3 axis[3]) {
    return qa_vec_add(qa_vec_add(qa_vec_scale(axis[0],v.x),qa_vec_scale(axis[1],v.y)),qa_vec_scale(axis[2],v.z));
}
static inline qa_vec3 qa_axis_remap(const qa_vec3 from[3],const qa_vec3 to[3],qa_vec3 v) {
    return qa_axis_from_local(qa_axis_to_local(v,from),to);
}
static inline qa_vec3 qa_axis_remap_point(qa_vec3 from_origin,const qa_vec3 from[3],qa_vec3 to_origin,const qa_vec3 to[3],qa_vec3 point) {
    return qa_vec_add(to_origin,qa_axis_remap(from,to,qa_vec_sub(point,from_origin)));
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
static inline bool qa_bounds_overlap_strict(qa_bounds a,qa_bounds b) {
    return a.mins.x<b.maxs.x && a.mins.y<b.maxs.y && a.mins.z<b.maxs.z &&
        a.maxs.x>b.mins.x && a.maxs.y>b.mins.y && a.maxs.z>b.mins.z;
}
static inline bool qa_bounds_expands(qa_bounds previous,qa_bounds requested) {
    return requested.mins.x<previous.mins.x || requested.mins.y<previous.mins.y || requested.mins.z<previous.mins.z ||
        requested.maxs.x>previous.maxs.x || requested.maxs.y>previous.maxs.y || requested.maxs.z>previous.maxs.z;
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
static inline float qa_bounds_distance(qa_bounds bounds, qa_vec3 point) {
    qa_vec3 delta = qa_v3(fmaxf(bounds.mins.x-point.x,fmaxf(0,point.x-bounds.maxs.x)),
                          fmaxf(bounds.mins.y-point.y,fmaxf(0,point.y-bounds.maxs.y)),
                          fmaxf(bounds.mins.z-point.z,fmaxf(0,point.z-bounds.maxs.z)));
    return qa_vec_length(delta);
}

#endif
