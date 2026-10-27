/*
 * Port of src/contracts/math.ts: shared vector/matrix contract shapes.
 * Field names and order mirror the TypeScript interfaces exactly.
 * C accommodation: TypeScript `number` fields are double; binary32
 * staging belongs to the module that computes with them.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MATH_H
#define QA_CONTRACTS_MATH_H

#include <stddef.h>
#include <stdint.h>

typedef struct qa_vec2 { double x; double y; } qa_vec2;
typedef struct qa_vec3 { double x; double y; double z; } qa_vec3;
typedef struct qa_vec4 { double x; double y; double z; double w; } qa_vec4;
/* TS MutableVec3 is the same shape; C mutability is by pointer. */
typedef qa_vec3 qa_mutable_vec3;

typedef struct qa_bounds { qa_vec3 min; qa_vec3 max; } qa_bounds;
typedef struct qa_plane { qa_vec3 normal; double distance; } qa_plane;

/** Quake axes are forward, left, up (TS `Axis = readonly [Vec3, Vec3, Vec3]`). */
typedef struct qa_axis { qa_vec3 vectors[3]; } qa_axis;

typedef struct qa_angle_vectors {
  qa_vec3 forward;
  qa_vec3 right;
  qa_vec3 up;
} qa_angle_vectors;

/** Column-major storage for column vectors, matching OpenGL (TS Mat4 tuple). */
typedef struct qa_mat4 { double values[16]; } qa_mat4;

/* C accommodation shared by all contract headers: a TS Uint8Array /
 * Float32Array / readonly number[] field becomes pointer + count. */
typedef struct qa_byte_view { const uint8_t *bytes; size_t length; } qa_byte_view;
typedef struct qa_double_view { const double *values; size_t count; } qa_double_view;
typedef struct qa_string_view_list { const char *const *values; size_t count; } qa_string_view_list;

#endif
