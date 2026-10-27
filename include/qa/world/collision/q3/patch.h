/* Port of src/world/collision/q3/patch.ts. PatchCollide is heap-owned
 * (planes carry source signbits). DebugSurface host via fn+ctx.
 * generate's allocator path stores planes/facets identically; raw
 * hunk byte-pointer identity is not reproduced. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_PATCH_H
#define QA_WORLD_COLLISION_Q3_PATCH_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/core/cvars/index.h"
#include "qa/world/collision/q3/allocation.h"
#include "qa/world/collision/q3/polylib.h"
typedef struct qa_patch_plane { qa_plane plane; double signbits; } qa_patch_plane;
typedef struct qa_patch_border { double plane; bool inward; bool no_adjust; } qa_patch_border;
typedef struct qa_patch_facet { double surface; qa_patch_border *borders; size_t border_count; } qa_patch_facet;
typedef struct qa_patch_collide {
  qa_patch_plane *planes; size_t plane_count;
  qa_patch_facet *facets; size_t facet_count;
  qa_bounds bounds;
} qa_patch_collide;
void qa_patch_collide_destroy(qa_patch_collide *p);
typedef qa_hunk_allocation (*qa_patch_allocator_fn)(void *ctx, const char *source, double bytes);
typedef struct qa_patch_shape {
  const char *kind; /* "point"|"box"|"capsule" */
  qa_vec3 mins, extents; double radius; qa_vec3 offset;
} qa_patch_shape;
typedef void (*qa_collision_debug_polygon_fn)(void *ctx, double color, double num_points, const qa_vec3 *points, size_t count);
typedef struct qa_collision_debug_host {
  void *ctx;
  qa_cvar_registry *cvars;
  void (*print)(void *ctx, const char *text);
  void (*developer_print)(void *ctx, const char *text);
  const qa_collision_winding_memory *windings; /* NULL-absent */
} qa_collision_debug_host;
typedef struct qa_collision_debug_surface {
  qa_collision_winding_library windings;
  int32_t c_total_patch_blocks, c_total_patch_surfaces, c_total_patch_edges;
  qa_collision_debug_host host;
  const qa_patch_collide *hit_patch; const qa_patch_facet *hit_facet;
  bool debug_block; qa_vec3 debug_block_points[4];
  bool point_update_registered, shape_update_registered, size_registered;
} qa_collision_debug_surface;
void qa_collision_debug_surface_init(qa_collision_debug_surface *d, qa_collision_debug_host host);
void qa_collision_debug_print_winding(qa_collision_debug_surface *d, qa_collision_winding *w);
void qa_collision_debug_print(qa_collision_debug_surface *d, const char *text);
void qa_collision_debug_developer_print(qa_collision_debug_surface *d, const char *text);
void qa_collision_debug_clear_level_patches(qa_collision_debug_surface *d);
void qa_collision_debug_record_trace(qa_collision_debug_surface *d, const qa_patch_collide *patch, const qa_patch_facet *facet, bool is_point);
void qa_collision_debug_record_mixed_border(qa_collision_debug_surface *d, const qa_vec3 points[4]);
void qa_collision_debug_draw(qa_collision_debug_surface *d, qa_collision_debug_polygon_fn draw, void *ctx);
qa_patch_collide *qa_generate_patch_collide(double width, double height, const qa_vec3 *points, size_t point_count,
  qa_collision_debug_surface *debug, qa_patch_allocator_fn allocate, void *alloc_ctx);
bool qa_position_in_patch(const qa_patch_collide *patch, qa_vec3 start, qa_patch_shape shape);
typedef struct qa_patch_trace_hit { double fraction; qa_plane plane; } qa_patch_trace_hit;
bool qa_trace_patch(const qa_patch_collide *patch, qa_vec3 start, qa_vec3 end, qa_patch_shape shape, double max_fraction, qa_collision_debug_surface *debug, qa_patch_trace_hit *out);
#endif
