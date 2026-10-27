/* Port of src/render/commands/frame.ts. SceneImageRegistry
 * (render/scene/resources.ts, sibling slice) is mirrored as the
 * fn+ctx surface this module uses, with a collision-proof name.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_COMMANDS_FRAME_H
#define QA_RENDER_COMMANDS_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* TS SceneImageRegistry surface used here (owner: render/scene/resources.ts). */
typedef struct qa_render_frame_image_registry {
  void *ctx;
  qa_renderer_resource_owner owner;
  const qa_image_resource_operation *(*drain_operations)(void *ctx, size_t *out_count);
  void (*require)(void *ctx, qa_renderer_image image);
} qa_render_frame_image_registry;
/* TS PreparedWorldView (render/scene/world.ts) surface used here. */
typedef struct qa_render_prepared_world_view {
  const qa_image_resource_operation *image_operations; size_t image_operation_count;
  qa_render_view view;
} qa_render_prepared_world_view;
typedef struct qa_scene_frame_builder qa_scene_frame_builder;
struct qa_scene_frame_builder { void *impl; qa_render_frame_image_registry images; };
bool qa_scene_frame_builder_init(qa_scene_frame_builder *builder, qa_render_frame_image_registry images);
void qa_scene_frame_builder_destroy(qa_scene_frame_builder *builder);
void qa_scene_frame_builder_begin(qa_scene_frame_builder *builder, const char *buffer, bool clear);
void qa_scene_frame_builder_begin_default(qa_scene_frame_builder *builder);
void qa_scene_frame_builder_resources(qa_scene_frame_builder *builder, const qa_image_resource_operation *operations, size_t count);
void qa_scene_frame_builder_view(qa_scene_frame_builder *builder, qa_render_view view);
void qa_scene_frame_builder_world(qa_scene_frame_builder *builder, qa_render_prepared_world_view prepared);
void qa_scene_frame_builder_picture(qa_scene_frame_builder *builder, qa_seat_id seat, qa_rect viewport, qa_rect rect, qa_renderer_image image, qa_texture_rect uv, qa_vec4 color);
void qa_scene_frame_builder_command(qa_scene_frame_builder *builder, qa_render_command command);
/* Returned frame borrows builder storage until the next begin/discard. */
qa_render_frame qa_scene_frame_builder_finish(qa_scene_frame_builder *builder, bool present);
void qa_scene_frame_builder_discard(qa_scene_frame_builder *builder);
typedef struct qa_clipped_picture { qa_rect rect; qa_texture_rect uv; } qa_clipped_picture;
bool qa_clip_picture(qa_rect rect, qa_texture_rect uv, qa_rect clip, qa_clipped_picture *out);
#endif
