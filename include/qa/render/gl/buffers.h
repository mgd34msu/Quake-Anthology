/* Port of src/render/gl/buffers.ts. The TS WeakMap packed cache becomes
 * a small identity list owned by the process (entries die only with
 * qa_gl_packed_cache_clear, unlike GC). GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_BUFFERS_H
#define QA_RENDER_GL_BUFFERS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
typedef struct qa_gl_geometry_arrays {
  float *positions; size_t position_count;
  float *colors; size_t color_count;
  float *coordinates; size_t coordinate_count;
  float *coordinates2; size_t coordinate2_count;
  float *world_positions; size_t world_position_count;
  float *normals; size_t normal_count;
  uint32_t *indices; size_t index_count;
} qa_gl_geometry_arrays;
void qa_gl_geometry_arrays_destroy(qa_gl_geometry_arrays *arrays);
typedef struct qa_gl_geometry_buffer qa_gl_geometry_buffer;
struct qa_gl_geometry_buffer { void *impl; };
bool qa_gl_geometry_buffer_init(qa_gl_geometry_buffer *buffer);
void qa_gl_geometry_buffer_destroy(qa_gl_geometry_buffer *buffer);
/* Returned arrays borrow buffer/cache storage until the next pack. */
qa_gl_geometry_arrays qa_gl_geometry_buffer_pack(qa_gl_geometry_buffer *buffer, qa_draw_batch batch);
/* Fresh allocation; free with qa_gl_geometry_arrays_destroy. */
qa_gl_geometry_arrays qa_pack_gl_geometry(qa_draw_batch batch);
void qa_gl_packed_cache_clear(void);
#endif
