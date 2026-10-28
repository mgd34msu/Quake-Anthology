/* Port of src/app/bootstrap/character-turntable.ts. Model decode
 * and software rasterization are landed/host services; scene bounds
 * (finishScene), request throttling and frame-timing constants are
 * kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CHARACTER_TURNTABLE_H
#define QA_APP_BOOTSTRAP_CHARACTER_TURNTABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#define QA_APP_TURNTABLE_WIDTH 288
#define QA_APP_TURNTABLE_HEIGHT 384
#define QA_APP_TURNTABLE_FRAME_MILLISECONDS 40
#define QA_APP_TURNTABLE_ROTATION_MILLISECONDS 9000
/* TS TurntableTexture / TurntableVertex. */
typedef struct qa_app_turntable_texture {
  double width, height;
  const uint8_t *pixels; size_t pixel_count;
  const char *alpha_test;
} qa_app_turntable_texture;
typedef struct qa_app_turntable_vertex {
  qa_vec3 position, normal;
  qa_vec2 tex_coord;
  double texture;
} qa_app_turntable_vertex;
/* TS CharacterTurntableScene (computed bounds). */
typedef struct qa_app_character_turntable_scene {
  const qa_app_turntable_vertex *vertices; size_t vertex_count;
  const double *indices; size_t index_count;
  const qa_app_turntable_texture *textures; size_t texture_count;
  qa_vec3 center;
  double radius, half_height, floor_z, initial_yaw;
} qa_app_character_turntable_scene;
/* TS finishScene: aborts "Character model has no preview geometry". */
qa_app_character_turntable_scene qa_app_finish_turntable_scene(const qa_app_turntable_vertex *vertices, size_t vertex_count,
    const double *indices, size_t index_count, const qa_app_turntable_texture *textures, size_t texture_count, double initial_yaw);
/* Checkerboard missing texture (16x16 RGBA, static storage). */
const qa_app_turntable_texture *qa_app_turntable_missing_texture(void);
typedef struct qa_app_character_turntable qa_app_character_turntable;
typedef struct qa_app_character_turntable_host {
  void *ctx;
  /* loadCharacterTurntableScene for a preview key; error or NULL. */
  const char *(*load_scene)(void *ctx, const char *preview_key, qa_app_character_turntable_scene *out);
  /* renderCharacterTurntable into the latest picture; returns token. */
  const void *(*render)(void *ctx, const qa_app_character_turntable_scene *scene, double yaw_radians);
  void (*close_render)(void *ctx);
} qa_app_character_turntable_host;
qa_app_character_turntable *qa_app_character_turntable_create(qa_app_character_turntable_host host);
void qa_app_character_turntable_destroy(qa_app_character_turntable *turntable);
/* Fixed ResourceId "resource:character-turntable:viewer" as a token. */
extern const char qa_app_character_turntable_resource[];
/* TS request(): returns the fixed viewer resource token. */
const void *qa_app_character_turntable_request(qa_app_character_turntable *turntable, const char *preview_key, double now_milliseconds);
/* TS tick(milliseconds): renders at most every 40 ms while requested. */
void qa_app_character_turntable_tick(qa_app_character_turntable *turntable, double milliseconds);
/* TS picture(): image picture token for the viewer resource or NULL. */
const void *qa_app_character_turntable_picture(qa_app_character_turntable *turntable, const void *resource);
void qa_app_character_turntable_close(qa_app_character_turntable *turntable);
/* TS yaw for elapsed time (rotation period 9000 ms, 40 ms frames). */
double qa_app_turntable_yaw(double initial_yaw, double elapsed_milliseconds);
#endif
