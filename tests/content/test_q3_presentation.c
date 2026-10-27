/*
 * Tests for the ported src/content/q3/presentation leaf modules
 * (character-resources, refdef, ref-entity, model-access,
 * cgame-body-profile, collision/movement/scene hosts).
 * Cases port tests/audio/q3-custom-sound.test.ts and the refdef /
 * ref-entity behavior asserted across the TS suite; TypeScript
 * `toThrow(RangeError)` cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q3/presentation.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static void test_character_resources(void) {
  /* tests/audio/q3-custom-sound.test.ts: native base and Team Arena policy. */
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_PRODUCT_BASEQ3, false), "sarge") == 0);
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_PRODUCT_BASEQ3, true), "sarge") == 0);
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_PRODUCT_MISSIONPACK, false), "sarge") == 0);
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_PRODUCT_MISSIONPACK, true), "james") == 0);
  CHECK(strcmp(qa_q3_character_sounds.select_sound, "sound/weapons/change.wav") == 0);
  CHECK(strcmp(qa_q3_character_sounds.gib_sound, "sound/player/gibsplt1.wav") == 0);
  CHECK(strcmp(qa_q3_character_sounds.jump_pad_sound, "sound/world/jumppad.wav") == 0);
  CHECK(qa_q3_footstep_path_count == 7);
  CHECK(strcmp(qa_q3_footstep_paths[0].key, "normal") == 0 && strcmp(qa_q3_footstep_paths[0].path, "step") == 0);
  CHECK(strcmp(qa_q3_footstep_paths[6].key, "metal") == 0 && strcmp(qa_q3_footstep_paths[6].path, "clank") == 0);
}

static qa_q3_pres_render_row row_of(const char *text) {
  qa_q3_pres_render_row row;
  memset(&row, 0, sizeof row);
  row.length = strlen(text);
  memcpy(row.bytes, text, row.length < 32 ? row.length : 32);
  return row;
}

static void test_refdef(void) {
  qa_q3_pres_refdef refdef = qa_q3_pres_create_refdef();
  CHECK(refdef.x == 0 && refdef.width == 0 && refdef.fov_x == 0 && refdef.time == 0);
  CHECK(refdef.render_flags == 0 && refdef.area_mask_length == 32);
  CHECK(refdef.view_origin.x == 0 && refdef.view_axis.vectors[2].z == 0);
  for (int i = 0; i < 8; i++) CHECK(refdef.text.rows[i].length == 0);
  refdef.width = 640;
  refdef.height = 480;
  refdef.fov_x = 90;
  refdef.area_mask[3] = 7;
  refdef.text.rows[0] = row_of("hello");
  qa_q3_pres_refdef copy = qa_q3_pres_copy_refdef(&refdef);
  refdef.area_mask[3] = 0;
  refdef.text.rows[0].bytes[0] = 'x';
  refdef.view_origin.x = 9;
  CHECK(copy.width == 640 && copy.fov_x == 90);
  CHECK(copy.area_mask[3] == 7); /* copy owns its arrays */
  CHECK(copy.text.rows[0].bytes[0] == 'h' && copy.text.rows[0].length == 5);
  CHECK(copy.view_origin.x == 0);
  /* A full 32-byte row is valid only with a NUL inside it. */
  qa_q3_pres_render_row rows[8];
  for (int i = 0; i < 8; i++) rows[i] = row_of("");
  memset(rows[1].bytes, 'a', 32);
  rows[1].length = 32;
  rows[1].bytes[31] = 0;
  qa_q3_pres_render_text out;
  qa_q3_pres_copy_render_text(rows, 8, &out);
  CHECK(out.rows[1].length == 32 && out.rows[1].bytes[0] == 'a');
  CHECK_ABORTS({ qa_q3_pres_render_text o; qa_q3_pres_copy_render_text(rows, 7, &o); });
  CHECK_ABORTS({
    qa_q3_pres_render_row r[8];
    for (int i = 0; i < 8; i++) r[i] = row_of("");
    r[0].length = 33;
    qa_q3_pres_render_text o;
    qa_q3_pres_copy_render_text(r, 8, &o);
  });
  CHECK_ABORTS({
    qa_q3_pres_render_row r[8];
    for (int i = 0; i < 8; i++) r[i] = row_of("");
    memset(r[2].bytes, 'b', 32);
    r[2].length = 32; /* no NUL within 32 bytes */
    qa_q3_pres_render_text o;
    qa_q3_pres_copy_render_text(r, 8, &o);
  });
  CHECK_ABORTS({
    qa_q3_pres_refdef bad = qa_q3_pres_create_refdef();
    bad.area_mask_length = 31;
    (void)qa_q3_pres_copy_refdef(&bad);
  });
}

static void test_ref_entity(void) {
  CHECK(QA_Q3_PRES_RF_MINLIGHT == 1 && QA_Q3_PRES_RF_THIRD_PERSON == 2 && QA_Q3_PRES_RF_FIRST_PERSON == 4);
  CHECK(QA_Q3_PRES_RF_DEPTHHACK == 8 && QA_Q3_PRES_RF_NOSHADOW == 64 && QA_Q3_PRES_RF_LIGHTING_ORIGIN == 128);
  CHECK(QA_Q3_PRES_RF_SHADOW_PLANE == 256 && QA_Q3_PRES_RF_WRAP_FRAMES == 512);
  CHECK(strcmp(qa_q3_pres_default_model.path, "*default") == 0);
  CHECK(qa_q3_pres_default_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  qa_q3_pres_ref_model_entity model = qa_q3_pres_create_model_entity_default();
  CHECK(model.model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  CHECK(model.shading.render_flags == 0 && model.shading.custom_shader == NULL);
  CHECK(model.shading.shader_rgba.w == 0 && model.shading.shader_time == 0);
  CHECK(model.origin.x == 0 && !model.non_normalized_axes && model.custom_skin == NULL);
  CHECK(model.frame == 0 && model.back_lerp == 0 && model.skin_num == 0);
  qa_q3_pres_ref_sprite_entity sprite = qa_q3_pres_create_sprite_entity();
  sprite.radius = 8;
  qa_q3_pres_ref_entity tagged = qa_q3_pres_ref_entity_from_sprite(sprite);
  qa_q3_pres_ref_entity copy = qa_q3_pres_copy_ref_entity(&tagged);
  tagged.as.sprite.origin.x = 5;
  tagged.as.sprite.shading.shader_rgba.x = 9;
  CHECK(copy.kind == QA_Q3_PRES_REF_SPRITE && copy.as.sprite.radius == 8);
  CHECK(copy.as.sprite.origin.x == 0 && copy.as.sprite.shading.shader_rgba.x == 0);
  CHECK(strcmp(qa_q3_pres_ref_entity_kind_name(QA_Q3_PRES_REF_PORTAL), "portal-surface") == 0);
  CHECK(strcmp(qa_q3_pres_ref_entity_kind_name(QA_Q3_PRES_REF_RAIL_CORE), "rail-core") == 0);
  /* RefPoly copy owns its vertices; shader identity is retained. */
  qa_q3_pres_scene_shader shader = {"gfx/damage/bullet_mrk"};
  qa_q3_pres_ref_poly_vertex verts[1] = {{{1, 2, 3}, {0, 0}, {255, 255, 255, 255}}};
  qa_q3_pres_ref_poly poly = {&shader, verts, 1};
  qa_q3_pres_ref_poly poly_copy;
  CHECK(qa_q3_pres_copy_ref_poly(&poly, &poly_copy));
  verts[0].position.x = 99;
  CHECK(poly_copy.shader == &shader);
  CHECK(poly_copy.vertex_count == 1 && poly_copy.vertices[0].position.x == 1);
  CHECK(poly_copy.vertices[0].color.w == 255);
  qa_q3_pres_ref_poly_destroy(&poly_copy);
  CHECK(poly_copy.vertices == NULL && poly_copy.vertex_count == 0);
}

static void test_model_access(void) {
  qa_q3_pres_scene_model fallback = qa_q3_pres_default_model;
  qa_bounds bounds = qa_q3_pres_model_bounds(&fallback);
  CHECK(bounds.min.x == 0 && bounds.max.z == 0);
  qa_q3_pres_model_tag tag;
  CHECK(!qa_q3_pres_lerp_model_tag(&fallback, "tag_torso", 0, 1, 0.75, &tag)); /* non-model is null */
  /* Loaded MD3 with two tag frames: midpoint origin and missing-tag null. */
  qa_q3_mesh_frame frames[2] = {{"f0", {{-1, -2, -3}, {4, 5, 6}}, {0, 0, 0}, 0},
                                {"f1", {{-9, -9, -9}, {9, 9, 9}}, {0, 0, 0}, 0}};
  qa_model_tag row0[1] = {{"tag_torso", {0, 0, 10}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}}};
  qa_model_tag row1[1] = {{"tag_torso", {0, 0, 20}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}}};
  const qa_model_tag *rows[2] = {row0, row1};
  size_t row_counts[2] = {1, 1};
  qa_q3_mesh_model mesh = {"lower", frames, 2, rows, row_counts, 2, NULL, 0};
  qa_q3_pres_scene_model loaded;
  memset(&loaded, 0, sizeof loaded);
  loaded.kind = QA_Q3_PRES_SCENE_MODEL_LOADED;
  loaded.path = "models/players/sarge/lower.md3";
  loaded.model.kind = QA_DECODED_MODEL_Q3_MESH;
  loaded.model.as.q3_mesh = &mesh;
  bounds = qa_q3_pres_model_bounds(&loaded); /* frames[0].bounds */
  CHECK(bounds.min.x == -1 && bounds.max.z == 6);
  CHECK(qa_q3_pres_lerp_model_tag(&loaded, "tag_torso", 0, 1, 0.5, &tag));
  CHECK(tag.origin.z == 15);
  CHECK(tag.axes.vectors[0].x == 1 && tag.axes.vectors[2].z == 1);
  CHECK(!qa_q3_pres_lerp_model_tag(&loaded, "tag_missing", 0, 1, 0.5, &tag));
  CHECK(qa_q3_pres_lerp_model_tag(&loaded, "tag_torso", 0, 99, 1, &tag)); /* clamps to last frame */
  CHECK(tag.origin.z == 20);
  CHECK(!qa_q3_pres_lerp_model_tag(&loaded, "tag_torso", -1, 1, 0.5, &tag));
  /* Inline models report their own bounds. */
  qa_q3_pres_scene_model inlined;
  memset(&inlined, 0, sizeof inlined);
  inlined.kind = QA_Q3_PRES_SCENE_MODEL_INLINE;
  inlined.bounds = (qa_bounds){{1, 2, 3}, {7, 8, 9}};
  bounds = qa_q3_pres_model_bounds(&inlined);
  CHECK(bounds.min.y == 2 && bounds.max.x == 7);
}

static void test_cgame_body_profile(void) {
  qa_qvmy_artifact artifact;
  memset(&artifact, 0, sizeof artifact);
  artifact.role = "game";
  artifact.module.artifact_path = "vm/cgame.qvm";
  artifact.module.digest = "sha256:a4744482c9b93852cc71f4d7ce03b3e4337e5d89844d27d272c2c16d74df07fa";
  qa_qvm_body_submission *rows = NULL;
  size_t count = 0;
  /* Non-cgame roles never reach the mounts. */
  CHECK(qa_q3_pres_read_cgame_body_profile(&artifact, NULL, &rows, &count) == QA_Q3_PRES_BODY_PROFILE_NONE);
  CHECK(rows == NULL && count == 0);
}

int main(void) {
  test_character_resources();
  test_refdef();
  test_ref_entity();
  test_model_access();
  test_cgame_body_profile();
  printf("q3-presentation: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
