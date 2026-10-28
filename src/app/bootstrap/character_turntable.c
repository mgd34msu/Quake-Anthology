/* Port of src/app/bootstrap/character-turntable.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/character_turntable.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
qa_app_character_turntable_scene qa_app_finish_turntable_scene(const qa_app_turntable_vertex *vertices, size_t vertex_count,
    const double *indices, size_t index_count, const qa_app_turntable_texture *textures, size_t texture_count, double initial_yaw) {
  if (vertex_count == 0 || index_count < 3) qa_range_error("Character model has no preview geometry");
  double min_x = INFINITY, min_y = INFINITY, min_z = INFINITY, max_x = -INFINITY, max_y = -INFINITY, max_z = -INFINITY;
  for (size_t i = 0; i < vertex_count; i++) {
    qa_vec3 p = vertices[i].position;
    if (p.x < min_x) min_x = p.x;
    if (p.x > max_x) max_x = p.x;
    if (p.y < min_y) min_y = p.y;
    if (p.y > max_y) max_y = p.y;
    if (p.z < min_z) min_z = p.z;
    if (p.z > max_z) max_z = p.z;
  }
  qa_vec3 center = {(min_x + max_x) / 2, (min_y + max_y) / 2, (min_z + max_z) / 2};
  double radius = 1;
  for (size_t i = 0; i < vertex_count; i++) {
    double dx = vertices[i].position.x - center.x, dy = vertices[i].position.y - center.y;
    double distance = sqrt(dx * dx + dy * dy); /* Math.hypot(x, y) */
    if (distance > radius) radius = distance;
  }
  qa_app_character_turntable_scene scene = {0};
  scene.vertices = vertices; scene.vertex_count = vertex_count;
  scene.indices = indices; scene.index_count = index_count;
  scene.textures = textures; scene.texture_count = texture_count;
  scene.center = center;
  scene.radius = radius;
  scene.half_height = fmax(1, (max_z - min_z) / 2);
  scene.floor_z = min_z;
  scene.initial_yaw = initial_yaw;
  return scene;
}
static uint8_t missing_pixels[16 * 16 * 4];
static qa_app_turntable_texture missing_texture;
const qa_app_turntable_texture *qa_app_turntable_missing_texture(void) {
  static bool built;
  if (!built) {
    built = true;
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        size_t offset = ((size_t)y * 16 + (size_t)x) * 4;
        bool even = ((x + y) & 1) == 0;
        missing_pixels[offset] = even ? 72 : 38;
        missing_pixels[offset + 1] = even ? 72 : 38;
        missing_pixels[offset + 2] = even ? 78 : 44;
        missing_pixels[offset + 3] = 255;
      }
    missing_texture = (qa_app_turntable_texture){16, 16, missing_pixels, sizeof missing_pixels, "none"};
  }
  return &missing_texture;
}
double qa_app_turntable_yaw(double initial_yaw, double elapsed_milliseconds) {
  double elapsed = fmax(0, elapsed_milliseconds);
  return initial_yaw + (fmod(elapsed, QA_APP_TURNTABLE_ROTATION_MILLISECONDS) / QA_APP_TURNTABLE_ROTATION_MILLISECONDS) * 2 * 3.141592653589793;
}
const char qa_app_character_turntable_resource[] = "resource:character-turntable:viewer";
struct qa_app_character_turntable {
  qa_app_character_turntable_host host;
  char *key;
  qa_app_character_turntable_scene scene;
  bool has_scene;
  const void *picture;
  double started, last_frame;
  double last_requested, last_rendered;
  bool has_rotation_start;
  bool closed;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_character_turntable *qa_app_character_turntable_create(qa_app_character_turntable_host host) {
  qa_app_character_turntable *turntable = calloc(1, sizeof *turntable);
  if (!turntable) return NULL;
  turntable->host = host;
  turntable->last_frame = -1;
  return turntable;
}
void qa_app_character_turntable_destroy(qa_app_character_turntable *turntable) {
  if (!turntable) return;
  qa_app_character_turntable_close(turntable);
  free(turntable->key);
  free(turntable);
}
const void *qa_app_character_turntable_request(qa_app_character_turntable *turntable, const char *preview_key, double now_milliseconds) {
  if (turntable->closed) return qa_app_character_turntable_resource;
  turntable->last_requested = now_milliseconds;
  if (!turntable->key || strcmp(turntable->key, preview_key) != 0) {
    free(turntable->key);
    turntable->key = dup(preview_key);
    turntable->has_rotation_start = false;
    qa_app_character_turntable_scene scene = {0};
    /* Load failures print once via the host path in TS and leave the
     * previous scene unshown; here the error degrades to no scene. */
    const char *error = turntable->host.load_scene(turntable->host.ctx, preview_key, &scene);
    turntable->has_scene = error == NULL;
    if (!error) {
      turntable->scene = scene;
      turntable->last_rendered = -INFINITY;
    }
  }
  return qa_app_character_turntable_resource;
}
void qa_app_character_turntable_tick(qa_app_character_turntable *turntable, double milliseconds) {
  if (turntable->closed || !turntable->has_scene) return;
  if (milliseconds - turntable->last_requested > 300) return;
  if (milliseconds - turntable->last_rendered < QA_APP_TURNTABLE_FRAME_MILLISECONDS) return;
  if (!turntable->has_rotation_start) { turntable->started = milliseconds; turntable->has_rotation_start = true; }
  double elapsed = fmax(0, milliseconds - turntable->started);
  double yaw = qa_app_turntable_yaw(turntable->scene.initial_yaw, elapsed);
  turntable->picture = turntable->host.render(turntable->host.ctx, &turntable->scene, yaw);
  turntable->last_rendered = milliseconds;
}
const void *qa_app_character_turntable_picture(qa_app_character_turntable *turntable, const void *resource) {
  (void)turntable;
  return resource == (const void *)qa_app_character_turntable_resource ? resource : NULL;
}
void qa_app_character_turntable_close(qa_app_character_turntable *turntable) {
  if (!turntable || turntable->closed) return;
  turntable->closed = true;
  turntable->host.close_render(turntable->host.ctx);
}
