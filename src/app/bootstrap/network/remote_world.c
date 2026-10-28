/* Port of src/app/bootstrap/network/remote-world.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/remote_world.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_remote_world_content {
  qa_app_remote_world_host host; void *loaded; void *queries; void *collision_settings;
};
qa_app_remote_world_content *qa_app_remote_world_content_create(
    qa_app_remote_world_host host, void *loaded) {
  qa_app_remote_world_content *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->host = host; c->loaded = loaded;
  return c;
}
void qa_app_remote_world_content_destroy(qa_app_remote_world_content *content) {
  if (!content) return;
  if (content->queries && content->host.destroy_queries)
    content->host.destroy_queries(content->host.ctx, content->queries);
  free(content);
}
void qa_app_remote_world_bind_collision_settings(qa_app_remote_world_content *content, void *settings) {
  content->host.register_map(content->host.ctx, settings);
  content->collision_settings = settings;
  if (content->queries)
    content->host.bind_collision_settings(content->host.ctx, content->queries, settings);
}
bool qa_app_remote_world_ready(const qa_app_remote_world_content *content) {
  return content->loaded != NULL;
}
void *qa_app_remote_world_loaded(qa_app_remote_world_content *content) {
  if (content->loaded == NULL) qa_range_error("Remote server has not supplied a world");
  return content->loaded;
}
void qa_app_remote_world_set_loaded(qa_app_remote_world_content *content, void *loaded) {
  content->loaded = loaded;
  if (content->queries && content->host.destroy_queries)
    content->host.destroy_queries(content->host.ctx, content->queries);
  content->queries = NULL;
}
void *qa_app_remote_world_scene(qa_app_remote_world_content *content) {
  if (content->queries == NULL) {
    content->queries = content->host.create_scene_queries(
        content->host.ctx, qa_app_remote_world_loaded(content));
    if (content->collision_settings != NULL)
      content->host.bind_collision_settings(content->host.ctx, content->queries,
          content->collision_settings);
  }
  return content->queries;
}
