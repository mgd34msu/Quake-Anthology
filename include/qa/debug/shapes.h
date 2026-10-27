/* Port of src/debug/shapes.ts. Geometry adapted from q2repro
 * refresh/debug.c. GPL-2.0-or-later.
 * C accommodation: the TS tagged union is one struct with a kind tag
 * (unused fields ignored, as in the TS switch); the returned array
 * becomes a caller buffer + count. The function returns the total
 * line count and writes at most `capacity` lines (pass out=NULL /
 * capacity=0 to only count). */
#ifndef QA_DEBUG_SHAPES_H
#define QA_DEBUG_SHAPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"

typedef struct qa_debug_line {
  qa_vec3 start;
  qa_vec3 end;
  qa_vec4 color;
  bool depth_test;
} qa_debug_line;

typedef enum qa_debug_shape_kind {
  QA_DEBUG_SHAPE_LINE,
  QA_DEBUG_SHAPE_POINT,
  QA_DEBUG_SHAPE_CIRCLE,
  QA_DEBUG_SHAPE_SPHERE,
  QA_DEBUG_SHAPE_BOUNDS,
  QA_DEBUG_SHAPE_CYLINDER,
  QA_DEBUG_SHAPE_ARROW,
  QA_DEBUG_SHAPE_RAY
} qa_debug_shape_kind;

typedef struct qa_debug_shape {
  qa_debug_shape_kind kind;
  qa_vec3 start;     /* line, arrow */
  qa_vec3 end;       /* line, arrow */
  qa_vec3 origin;    /* point, circle, sphere, cylinder, ray */
  double size;       /* point, arrow, ray */
  double radius;     /* circle, sphere, cylinder */
  qa_vec3 min;       /* bounds */
  qa_vec3 max;       /* bounds */
  double half_height; /* cylinder */
  qa_vec4 cap_color; /* arrow */
  qa_vec3 direction; /* ray */
  double length;     /* ray */
} qa_debug_shape;

/* TS debugShapeLines. An out-of-range kind is the TS exhaustive-switch
 * `never` return; in C it aborts like the plain-Error pattern. */
size_t qa_debug_shape_lines(qa_debug_shape shape, qa_vec4 color,
                            bool depth_test, qa_debug_line *out,
                            size_t capacity);

#endif
