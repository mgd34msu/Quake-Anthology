/* Port of src/app/bootstrap/network/remote-world.ts. Loaded content
 * and scene queries are landed world values held opaquely; the
 * queries object is built through the host so content/world typing
 * stays with its owners. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_REMOTE_WORLD_H
#define QA_APP_BOOTSTRAP_NETWORK_REMOTE_WORLD_H
#include <stdbool.h>
typedef struct qa_app_remote_world_host {
  void *ctx;
  void *(*create_scene_queries)(void *ctx, void *loaded_content);
  void (*bind_collision_settings)(void *ctx, void *queries, void *settings);
  void (*register_map)(void *ctx, void *settings);
  void (*destroy_queries)(void *ctx, void *queries);
} qa_app_remote_world_host;
typedef struct qa_app_remote_world_content qa_app_remote_world_content;
/* `loaded` may be NULL (no world supplied yet). NULL only on OOM. */
qa_app_remote_world_content *qa_app_remote_world_content_create(
    qa_app_remote_world_host host, void *loaded);
void qa_app_remote_world_content_destroy(qa_app_remote_world_content *content);
void qa_app_remote_world_bind_collision_settings(qa_app_remote_world_content *content, void *settings);
bool qa_app_remote_world_ready(const qa_app_remote_world_content *content);
/* Aborts "Remote server has not supplied a world" when unset. */
void *qa_app_remote_world_loaded(qa_app_remote_world_content *content);
void qa_app_remote_world_set_loaded(qa_app_remote_world_content *content, void *loaded);
void *qa_app_remote_world_scene(qa_app_remote_world_content *content);
#endif
