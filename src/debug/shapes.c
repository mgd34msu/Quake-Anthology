/* Port of src/debug/shapes.ts. GPL-2.0-or-later. */
#include "qa/debug/shapes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "qa/core/math.h"
#include "qa/foundation/js.h"

static const double QA_PI = 3.141592653589793;

typedef struct line_sink {
  qa_debug_line *out;
  size_t capacity;
  size_t count;
  bool depth_test;
} line_sink;

static void emit(line_sink *sink, qa_vec3 start, qa_vec3 end, qa_vec4 color) {
  if (sink->out != NULL && sink->count < sink->capacity) {
    sink->out[sink->count].start = start;
    sink->out[sink->count].end = end;
    sink->out[sink->count].color = color;
    sink->out[sink->count].depth_test = sink->depth_test;
  }
  sink->count++;
}

static void emit_arrow(line_sink *sink, qa_vec3 start, qa_vec3 end,
                       double size, qa_vec4 cap_color, qa_vec4 color) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 dir = qa_normalize3(delta);
  qa_vec3 apex = length > size ? qa_add3(start, qa_scale3(dir, length - size)) : end;
  if (length > size) emit(sink, start, apex, color);
  double extent = length > size ? size : length;
  qa_vec3 tip = qa_add3(apex, qa_scale3(dir, extent));
  /* Inline Vec3 literal in TS: no Math.fround staging. */
  qa_vec3 rotated = {dir.z, -dir.x, dir.y};
  qa_vec3 right =
      qa_normalize3(qa_sub3(rotated, qa_scale3(dir, qa_dot3(rotated, dir))));
  emit(sink, apex, tip, cap_color);
  emit(sink, qa_add3(apex, qa_scale3(right, extent)), tip, cap_color);
  emit(sink, qa_add3(apex, qa_scale3(right, -extent)), tip, cap_color);
}

static qa_vec3 bounds_corner(qa_debug_shape shape, int i, double z) {
  qa_vec3 v = {i > 1 ? shape.min.x : shape.max.x,
               (i + 1) % 4 > 1 ? shape.min.y : shape.max.y, z};
  return v;
}

static qa_vec3 circle_point(qa_debug_shape shape, int i, int count, double z) {
  /* Inline arithmetic in TS: plain double, no Math.fround. */
  qa_vec3 v = {shape.origin.x + cos(i * QA_PI * 2 / count) * shape.radius,
               shape.origin.y + sin(i * QA_PI * 2 / count) * shape.radius, z};
  return v;
}

static qa_vec3 sphere_ring(qa_debug_shape shape, int stacks, int slices,
                           int stack, int slice) {
  double phi = QA_PI * (stack + 1) / stacks;
  double theta = QA_PI * 2 * slice / slices;
  qa_vec3 unit = {sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi)};
  return qa_add3(shape.origin, qa_scale3(unit, shape.radius));
}

/* Math.trunc(Math.min(k + radius / d, cap)) as a loop bound: a NaN or hugely
 * negative radius runs no iterations in TS (i < NaN is false), so map NaN to 0
 * and floor at -1e9 instead of casting an out-of-range double to int. */
static int loop_bound(double value) {
  if (isnan(value)) return 0;
  double t = trunc(value);
  return t < -1e9 ? -1000000000 : (int)t;
}
size_t qa_debug_shape_lines(qa_debug_shape shape, qa_vec4 color,
                            bool depth_test, qa_debug_line *out,
                            size_t capacity) {
  line_sink sink = {out, capacity, 0, depth_test};
  switch (shape.kind) {
    case QA_DEBUG_SHAPE_LINE:
      emit(&sink, shape.start, shape.end, color);
      break;
    case QA_DEBUG_SHAPE_POINT: {
      double h = shape.size * 0.5;
      qa_vec3 axes[3] = {{h, 0, 0}, {0, h, 0}, {0, 0, h}};
      for (int i = 0; i < 3; i++)
        emit(&sink, qa_sub3(shape.origin, axes[i]),
             qa_add3(shape.origin, axes[i]), color);
      break;
    }
    case QA_DEBUG_SHAPE_BOUNDS: {
      for (int i = 0; i < 4; i++) {
        emit(&sink, bounds_corner(shape, i, shape.min.z),
             bounds_corner(shape, i, shape.max.z), color);
        double zs[2] = {shape.min.z, shape.max.z};
        for (int k = 0; k < 2; k++)
          emit(&sink, bounds_corner(shape, i, zs[k]),
               bounds_corner(shape, (i + 1) % 4, zs[k]), color);
      }
      break;
    }
    case QA_DEBUG_SHAPE_CIRCLE:
    case QA_DEBUG_SHAPE_CYLINDER: {
      /* Math.trunc(Math.min(5 + radius/8, 16)); count may be <= 0 for
       * very negative radii, exactly as the TS loop then emits none. */
      int count = loop_bound(qa_js_min(5 + shape.radius / 8, 16));
      for (int i = 0; i < count; i++) {
        if (shape.kind == QA_DEBUG_SHAPE_CIRCLE) {
          emit(&sink, circle_point(shape, i, count, shape.origin.z),
               circle_point(shape, (i + 1) % count, count, shape.origin.z),
               color);
        } else {
          double bottom = shape.origin.z - shape.half_height;
          double top = shape.origin.z + shape.half_height;
          emit(&sink, circle_point(shape, i, count, bottom),
               circle_point(shape, (i + 1) % count, count, bottom), color);
          emit(&sink, circle_point(shape, i, count, top),
               circle_point(shape, (i + 1) % count, count, top), color);
          emit(&sink, circle_point(shape, i, count, bottom),
               circle_point(shape, i, count, top), color);
        }
      }
      break;
    }
    case QA_DEBUG_SHAPE_SPHERE: {
      int stacks = loop_bound(qa_js_min(4 + shape.radius / 32, 10));
      int slices = loop_bound(qa_js_min(6 + shape.radius / 32, 16));
      qa_vec3 zr = {0, 0, shape.radius};
      qa_vec3 north = qa_add3(shape.origin, zr);
      qa_vec3 south = qa_sub3(shape.origin, zr);
      for (int i = 0; i < slices; i++) {
        int next = (i + 1) % slices;
        emit(&sink, north, sphere_ring(shape, stacks, slices, 0, next), color);
        emit(&sink, sphere_ring(shape, stacks, slices, 0, next),
             sphere_ring(shape, stacks, slices, 0, i), color);
        emit(&sink, sphere_ring(shape, stacks, slices, 0, i), north, color);
        emit(&sink, south, sphere_ring(shape, stacks, slices, stacks - 2, i),
             color);
        emit(&sink, sphere_ring(shape, stacks, slices, stacks - 2, i),
             sphere_ring(shape, stacks, slices, stacks - 2, next), color);
        emit(&sink, sphere_ring(shape, stacks, slices, stacks - 2, next),
             south, color);
      }
      for (int j = 0; j < stacks - 2; j++)
        for (int i = 0; i < slices; i++) {
          int next = (i + 1) % slices;
          emit(&sink, sphere_ring(shape, stacks, slices, j, i),
               sphere_ring(shape, stacks, slices, j, next), color);
          emit(&sink, sphere_ring(shape, stacks, slices, j, next),
               sphere_ring(shape, stacks, slices, j + 1, next), color);
          emit(&sink, sphere_ring(shape, stacks, slices, j + 1, next),
               sphere_ring(shape, stacks, slices, j + 1, i), color);
          emit(&sink, sphere_ring(shape, stacks, slices, j + 1, i),
               sphere_ring(shape, stacks, slices, j, i), color);
        }
      break;
    }
    case QA_DEBUG_SHAPE_ARROW:
      emit_arrow(&sink, shape.start, shape.end, shape.size, shape.cap_color,
                 color);
      break;
    case QA_DEBUG_SHAPE_RAY:
      emit_arrow(&sink, shape.origin,
                 qa_add3(shape.origin, qa_scale3(shape.direction, shape.length)),
                 shape.size, color, color);
      break;
    default:
      fprintf(stderr, "Error: Invalid debug shape kind\n");
      abort();
  }
  return sink.count;
}
