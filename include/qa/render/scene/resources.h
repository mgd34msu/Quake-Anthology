/* Port of src/render/scene/resources.ts. GPL-2.0-or-later.
 * Image identity is pointer identity in TS. Here images are boxed:
 * qa_scene_image_handle owns the qa_renderer_image value; the handle
 * address is the resident identity. Rentals freed by registry destroy. */
#ifndef QA_RENDER_SCENE_RESOURCES_H
#define QA_RENDER_SCENE_RESOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/media/types.h"
typedef struct qa_scene_image_handle { qa_renderer_image image; } qa_scene_image_handle;
typedef struct qa_scene_image_registry qa_scene_image_registry;
typedef void (*qa_scene_animation_fn)(void *ctx, double milliseconds);
typedef void (*qa_scene_animation_stop_fn)(void *ctx);
struct qa_scene_image_registry {
  qa_renderer_resource_owner owner;
  qa_media_clock clock; bool has_clock; /* false: default clock returns 0 (TS performance.now, unobservable here) */
  void *_lifetime; /* shared with forks */
  qa_scene_image_registry *parent;
  qa_scene_image_registry **children; size_t child_count, child_capacity;
  qa_scene_image_handle **allocations; size_t allocation_count, allocation_capacity;
  qa_scene_image_handle **images; size_t image_count, image_capacity;
  bool closed;
  void *_animations;
};
/* clock NULL -> TS default clock. */
void qa_scene_image_registry_init(qa_scene_image_registry *r, qa_renderer_resource_owner owner, const qa_media_clock *clock);
void qa_scene_image_registry_destroy(qa_scene_image_registry *r);
bool qa_scene_image_registry_is_closed(const qa_scene_image_registry *r);
/* child must be uninitialized storage; destroy with qa_scene_image_registry_destroy. */
void qa_scene_image_registry_fork(qa_scene_image_registry *r, const qa_media_clock *clock_or_null, qa_scene_image_registry *child);
/* source: use generated form when source_name non-NULL handling by caller. */
const qa_scene_image_handle *qa_scene_image_registry_register(qa_scene_image_registry *r, const char *name, qa_render_image content, qa_texture_sampling sampling, qa_renderer_image source_image);
const qa_scene_image_handle *qa_scene_image_registry_register_generated(qa_scene_image_registry *r, const char *name, qa_render_image content, qa_texture_sampling sampling);
const qa_scene_image_handle *qa_scene_image_registry_allocate(qa_scene_image_registry *r, double width, double height, qa_renderer_image source_image);
/* Operation image values must equal a handle's image for identity ops. */
void qa_scene_image_registry_commit(qa_scene_image_registry *r, qa_image_resource_operation operation);
void qa_scene_image_registry_update(qa_scene_image_registry *r, qa_renderer_image image, double level, qa_image_level content);
void qa_scene_image_registry_release(qa_scene_image_registry *r, qa_renderer_image image);
void qa_scene_image_registry_texture_mode(qa_scene_image_registry *r, qa_texture_filter filter);
void qa_scene_image_registry_require(qa_scene_image_registry *r, qa_renderer_image image);
bool qa_scene_image_registry_is_resident(qa_scene_image_registry *r, qa_renderer_image image);
/* Returns true when tracked. stop via returned token id. */
typedef struct qa_scene_animation_token { qa_scene_image_registry *registry; double ordinal; } qa_scene_animation_token;
qa_scene_animation_token qa_scene_image_registry_track_animation(qa_scene_image_registry *r, qa_renderer_image image, qa_scene_animation_fn update, void *ctx, qa_scene_animation_stop_fn on_stop, void *stop_ctx);
void qa_scene_animation_token_stop(qa_scene_animation_token token);
/* Drained array malloc'd; caller frees array only. */
qa_image_resource_operation *qa_scene_image_registry_drain_operations(qa_scene_image_registry *r, size_t *out_count);
qa_image_resource_operation *qa_scene_image_registry_drain_pending(qa_scene_image_registry *r, size_t *out_count);
typedef void (*qa_scene_isolated_fn)(void *ctx);
typedef void (*qa_scene_collect_fn)(void *ctx, qa_image_resource_operation *operations, size_t count);
void qa_scene_image_registry_isolate(qa_scene_image_registry *r, qa_scene_isolated_fn operation, void *ctx, qa_scene_collect_fn collect, void *collect_ctx);
void qa_scene_image_registry_close(qa_scene_image_registry *r);
qa_render_image qa_scene_rgba_image(qa_image_level level, qa_image_level *level_storage);
#endif
