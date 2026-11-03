/* Port of src/app/bootstrap/character-turntable.ts. Loads a Q1/Q2/Q3 player
 * model from mounted content into a flat textured mesh and rasterizes it in
 * software into a scene image the menu draws.
 * C accommodations:
 *  - TS async loads are synchronous. CharacterTurntable.request runs the
 *    load inline where TS starts it and lets a later tick observe the result;
 *    the scene, key and timing fields end in the same state.
 *  - The TS WeakMap<MountedContent, shader catalog> is a module cache keyed
 *    by the mounted instance; qa_app_turntable_forget_mounts drops an entry
 *    and must be called before that instance is closed (the turntable does
 *    this for the mounts it opens).
 *  - Scenes are reference counted: the TS scene map and the current scene
 *    share them the way GC references would.
 *  - Rendered frames handed to SceneImageRegistry.update are borrowed by the
 *    queued operation; the turntable keeps the last
 *    QA_APP_TURNTABLE_FRAME_RING frames alive.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CHARACTER_TURNTABLE_H
#define QA_APP_BOOTSTRAP_CHARACTER_TURNTABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/render/scene/resources.h"
#include "qa/text/draw2d.h"
#define QA_APP_TURNTABLE_WIDTH 288
#define QA_APP_TURNTABLE_HEIGHT 384
#define QA_APP_TURNTABLE_FRAME_MILLISECONDS 40
#define QA_APP_TURNTABLE_ROTATION_MILLISECONDS 9000
#define QA_APP_TURNTABLE_FRAME_RING 8
/* TS CharacterTurntableRequest (StartupCharacterPreview). */
typedef struct qa_app_turntable_request {
  qa_content_id product;
  qa_game_family family;
  const char *model;
  const char *skin;
  double top_color, bottom_color;
} qa_app_turntable_request;
/* TS TurntableTexture; pixels owned by the scene. alpha_test is the TS
 * RenderState alphaTest string ("none" | "gt0" | "lt128" | "ge128"). */
typedef struct qa_app_turntable_texture {
  double width, height;
  uint8_t *pixels; size_t pixel_length;
  const char *alpha_test;
} qa_app_turntable_texture;
typedef struct qa_app_turntable_vertex {
  qa_vec3 position, normal;
  qa_vec2 tex_coord;
  double texture;
} qa_app_turntable_vertex;
/* TS CharacterTurntableScene. Arrays owned; free with
 * qa_app_character_turntable_scene_release. */
typedef struct qa_app_character_turntable_scene {
  qa_app_turntable_vertex *vertices; size_t vertex_count;
  double *indices; size_t index_count;
  qa_app_turntable_texture *textures; size_t texture_count;
  qa_vec3 center;
  double radius, half_height, floor_z, initial_yaw;
  size_t references;
} qa_app_character_turntable_scene;
/* TS loadCharacterTurntableScene. Raises (qa_raise) with the TS message on
 * failure; returns a scene with one reference. */
qa_app_character_turntable_scene *qa_app_load_character_turntable_scene(qa_content_mounted *mounts,
    const qa_app_turntable_request *request);
void qa_app_character_turntable_scene_retain(qa_app_character_turntable_scene *scene);
void qa_app_character_turntable_scene_release(qa_app_character_turntable_scene *scene);
/* Drops the Q3 shader catalog cached for these mounts. */
void qa_app_turntable_forget_mounts(const qa_content_mounted *mounts);
/* TS renderCharacterTurntable. Raises RangeError "Invalid turntable
 * dimensions". out->pixels.data is malloc'd (caller frees). */
void qa_app_render_character_turntable(const qa_app_character_turntable_scene *scene, double yaw_radians,
    double width, double height, qa_image_level *out);
/* TS CharacterTurntableOptions. catalog() may raise (TS throw); the turntable
 * then keeps showing what it had. print receives each line. Every callback
 * gets its own ctx. */
typedef struct qa_app_character_turntable_options {
  void *catalog_ctx; qa_content_catalog *(*catalog)(void *ctx);
  qa_scene_image_registry *images;
  void *print_ctx; void (*print)(void *ctx, const char *text);
} qa_app_character_turntable_options;
typedef struct qa_app_character_turntable qa_app_character_turntable;
extern const char qa_app_character_turntable_resource[];
qa_app_character_turntable *qa_app_character_turntable_create(qa_app_character_turntable_options options);
/* close + free. */
void qa_app_character_turntable_destroy(qa_app_character_turntable *turntable);
/* TS request(preview, nowMilliseconds): returns the viewer ResourceId. */
const char *qa_app_character_turntable_request(qa_app_character_turntable *turntable,
    const qa_app_turntable_request *preview, double now_milliseconds);
/* TS picture(resource): NULL is TS null. */
const qa_image_picture *qa_app_character_turntable_picture(const qa_app_character_turntable *turntable,
    const char *resource);
void qa_app_character_turntable_close(qa_app_character_turntable *turntable);
#endif
