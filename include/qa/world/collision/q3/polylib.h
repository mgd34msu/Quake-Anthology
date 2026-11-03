/* Port of src/world/collision/q3/polylib.ts. Winding storage is the
 * source allocation bytes (i32 count + f32 points). GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_POLYLIB_H
#define QA_WORLD_COLLISION_Q3_POLYLIB_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/world/collision/q3/allocation.h"
typedef struct qa_collision_winding_memory {
  void *ctx;
  qa_zone_allocation (*allocate)(void *ctx, double bytes);
  void (*free)(void *ctx, qa_zone_allocation allocation);
} qa_collision_winding_memory;
typedef enum qa_winding_side { QA_WINDING_FRONT=0, QA_WINDING_BACK=1, QA_WINDING_ON=2, QA_WINDING_CROSS=3 } qa_winding_side;
typedef struct qa_collision_winding {
  qa_zone_allocation allocation; double capacity; bool freed;
} qa_collision_winding;
double qa_collision_winding_num_points(qa_collision_winding *w);
void qa_collision_winding_set_num_points(qa_collision_winding *w, double value);
qa_vec3 qa_collision_winding_point(qa_collision_winding *w, double index);
void qa_collision_winding_set_point(qa_collision_winding *w, double index, qa_vec3 point);
void qa_collision_winding_append(qa_collision_winding *w, qa_vec3 point);
void qa_collision_winding_mark_freed(qa_collision_winding *w);
/* Malloc'd points snapshot; caller frees. */
qa_vec3 *qa_collision_winding_points(qa_collision_winding *w, size_t *out_count);
typedef struct qa_collision_winding_library {
  int32_t c_active_windings, c_peak_windings, c_winding_allocs, c_winding_points, c_removed;
  bool has_memory; qa_collision_winding_memory memory;
} qa_collision_winding_library;
void qa_collision_winding_library_init(qa_collision_winding_library *lib, const qa_collision_winding_memory *memory);
/* Heap windings; free with library free (also frees struct). */
qa_collision_winding *qa_winding_alloc(qa_collision_winding_library *lib, double points);
void qa_winding_free(qa_collision_winding_library *lib, qa_collision_winding *w);
/* A winding TS drops without FreeWinding (left to the GC on a throw, or kept live
 * on purpose): releases the C memory only. Counters keep counting it active and a
 * zone allocation stays allocated, as in TS. */
void qa_winding_abandon(qa_collision_winding_library *lib, qa_collision_winding *w);
typedef void (*qa_winding_print_fn)(void *ctx, const char *text);
void qa_winding_pw(qa_collision_winding_library *lib, qa_collision_winding *w, qa_winding_print_fn print, void *ctx);
qa_collision_winding *qa_winding_copy(qa_collision_winding_library *lib, qa_collision_winding *w);
qa_collision_winding *qa_winding_reverse(qa_collision_winding_library *lib, qa_collision_winding *w);
void qa_winding_remove_colinear(qa_collision_winding_library *lib, qa_collision_winding *w);
qa_plane qa_winding_plane(qa_collision_winding_library *lib, qa_collision_winding *w);
double qa_winding_area(qa_collision_winding_library *lib, qa_collision_winding *w);
qa_bounds qa_winding_library_bounds(qa_collision_winding_library *lib, qa_collision_winding *w);
qa_vec3 qa_winding_center(qa_collision_winding_library *lib, qa_collision_winding *w);
qa_collision_winding *qa_winding_base_for_plane(qa_collision_winding_library *lib, qa_plane plane);
typedef struct qa_winding_clip_result { qa_collision_winding *front; qa_collision_winding *back; } qa_winding_clip_result;
qa_winding_clip_result qa_winding_clip(qa_collision_winding_library *lib, qa_collision_winding *w, qa_plane plane, double epsilon);
qa_collision_winding *qa_winding_chop_in_place(qa_collision_winding_library *lib, qa_collision_winding *w, qa_plane plane, double epsilon);
qa_collision_winding *qa_winding_chop(qa_collision_winding_library *lib, qa_collision_winding *w, qa_plane plane);
qa_winding_side qa_winding_on_plane_side(qa_collision_winding_library *lib, qa_collision_winding *w, qa_plane plane);
void qa_winding_check(qa_collision_winding_library *lib, qa_collision_winding *w);
qa_collision_winding *qa_winding_add_to_convex_hull(qa_collision_winding_library *lib, qa_collision_winding *w, qa_collision_winding *hull, qa_vec3 normal);
qa_bounds qa_winding_bounds(const qa_vec3 *points, size_t count);
#endif
