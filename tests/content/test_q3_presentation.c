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
#include "qa/core/math.h"

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
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_BASE_PRODUCT_BASEQ3, false), "sarge") == 0);
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_BASE_PRODUCT_BASEQ3, true), "sarge") == 0);
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_BASE_PRODUCT_MISSIONPACK, false), "sarge") == 0);
  CHECK(strcmp(qa_q3_custom_sound_fallback(QA_Q3_BASE_PRODUCT_MISSIONPACK, true), "james") == 0);
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

/* ---- presentation core (state.ts / retail-snapshot.ts / entities.ts) ---- */

static bool vec_eq(qa_vec3 value, double x, double y, double z) {
  return value.x == x && value.y == y && value.z == z;
}

static void test_core_state_and_retail(void) {
  /* presentation.test.ts: snapshot copies retain mission fields. */
  qa_q3_wire_snapshot input;
  memset(&input, 0, sizeof input);
  qa_q3_player_state_record_init(&input.player_state, QA_Q3_PRODUCT_MISSIONPACK, 0, 13, 3);
  qa_q3_player_state_slot_set(&input.player_state, 2, 7, 17);
  qa_q3_player_state_slot_set(&input.player_state, 5, 13, 6);
  qa_q3_player_state_slot_set(&input.player_state, 0, 1, 68);
  qa_q3_player_state_slot_set(&input.player_state, 1, 1, 40);
  qa_q3_player_state_slot_set(&input.player_state, 4, 14, 3000);
  qa_q3_player_state_slot_set(&input.player_state, 3, 8, 11);
  input.player_state.origin = qa_math_vec3(1, 2, 3);
  input.player_state.jumppad_frame = 50;
  input.player_state.entity_event_sequence = 9;
  qa_q3_entity_state_record entity;
  qa_q3_entity_state_init(&entity);
  entity.number = 37;
  entity.generic1 = 14;
  entity.origin = qa_math_vec3(4, 5, 6);
  input.message_number = 5;
  input.server_time = 1500;
  input.delta_number = 4;
  input.server_command_number = 3;
  input.parse_entities_number = 12;
  input.area_mask_len = 32;
  input.entities = &entity;
  input.entity_count = 1;
  qa_q3_pres_retail_snapshot output;
  CHECK(qa_q3_pres_retail_snapshot_from_wire(&output, &input));
  qa_q3_player_state_slot_set(&input.player_state, 2, 7, 0);
  qa_q3_player_state_slot_set(&input.player_state, 0, 1, 0);
  input.player_state.origin = qa_math_vec3(0, 0, 0);
  entity.origin = qa_math_vec3(0, 0, 0);
  input.area_mask[0] = 255;
  CHECK(qa_q3_base_player_state_record_product(&output.player_state) == QA_Q3_BASE_PRODUCT_MISSIONPACK);
  CHECK(qa_q3_base_player_state_slots_get(&output.player_state.stats, 7) == 17);
  CHECK(qa_q3_base_player_state_slots_get(&output.player_state.ammo, 13) == 6);
  CHECK(qa_q3_base_player_state_slots_get(&output.player_state.events, 1) == 68);
  CHECK(qa_q3_base_player_state_slots_get(&output.player_state.event_parms, 1) == 40);
  CHECK(qa_q3_base_player_state_slots_get(&output.player_state.powerups, 14) == 3000);
  CHECK(qa_q3_base_player_state_slots_get(&output.player_state.persistant, 8) == 11);
  CHECK(output.player_state.jumppad_frame == 50 && output.player_state.entity_event_sequence == 9);
  CHECK(vec_eq(qa_q3_base_player_state_record_origin(&output.player_state), 1, 2, 3));
  CHECK(output.entity_count == 1 && vec_eq(output.entities[0].origin, 4, 5, 6));
  CHECK(output.area_mask[0] == 0);
  qa_q3_pres_retail_snapshot_destroy(&output);

  /* presentation.test.ts: seat state and mover adjustment stay independent. */
  qa_q3_pres_client_game_state first, second;
  CHECK(qa_q3_pres_client_game_state_init(&first, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 10));
  CHECK(qa_q3_pres_client_game_state_init(&second, QA_Q3_BASE_PRODUCT_MISSIONPACK, 1, 20));
  qa_q3_base_entity_state *mover =
      &qa_q3_pres_client_game_state_entity_at(&first, 5)->current_state;
  mover->e_type = QA_Q3_BASE_ET_MOVER;
  mover->pos.type = QA_Q3_BASE_TR_LINEAR;
  mover->pos.time = 1000;
  mover->pos.base = qa_math_vec3(0, 0, 0);
  mover->pos.delta = qa_math_vec3(100, -20, 10);
  qa_vec3 point = qa_math_vec3(1, 2, 3);
  CHECK(vec_eq(qa_q3_pres_adjust_position_for_mover(&first, point, 5, 1000, 1500), 51, -8, 8));
  CHECK(vec_eq(qa_q3_pres_adjust_position_for_mover(&second, point, 5, 1000, 1500), 1, 2, 3));
  CHECK(vec_eq(qa_q3_pres_client_game_state_entity_at(&second, 5)->current_state.pos.delta, 0, 0, 0));
  qa_q3_pres_client_game_state_destroy(&first);
  qa_q3_pres_client_game_state_destroy(&second);

  /* Constructor + entityAt RangeErrors (state.ts). */
  CHECK_ABORTS({
    qa_q3_pres_client_game_state bad;
    (void)qa_q3_pres_client_game_state_init(&bad, QA_Q3_BASE_PRODUCT_BASEQ3, 64, 0);
  });
  CHECK_ABORTS({
    qa_q3_pres_client_game_state bad;
    (void)qa_q3_pres_client_game_state_init(&bad, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 2147483648.0);
  });
  CHECK_ABORTS({
    qa_q3_pres_client_game_state state;
    (void)qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0);
    (void)qa_q3_pres_client_game_state_entity_at(&state, 1024);
  });
}

static void test_core_client_info_and_config(void) {
  qa_q3_pres_client_info info;
  qa_q3_pres_client_info_init(&info);
  qa_q3_animation animations[QA_Q3_TOTAL_ANIMATION_COUNT];
  bool present[QA_Q3_TOTAL_ANIMATION_COUNT];
  for (size_t i = 0; i < QA_Q3_TOTAL_ANIMATION_COUNT; i++) {
    memset(&animations[i], 0, sizeof animations[i]);
    animations[i].first_frame = (double)i;
    present[i] = true;
  }
  present[QA_Q3_ANIM_TORSO_NEGATIVE + 1] = false;
  qa_q3_pres_client_info_set_animations(&info, animations, present, QA_Q3_TOTAL_ANIMATION_COUNT);
  CHECK(info.animations[3].first_frame == 3);
  CHECK(info.animations[QA_Q3_ANIM_TORSO_NEGATIVE + 1].first_frame == 0);
  CHECK(qa_q3_pres_set_string(&info.name, "Sarge"));
  qa_q3_pres_client_info copy;
  qa_q3_pres_client_info_init(&copy);
  CHECK(qa_q3_pres_client_info_copy_from(&copy, &info));
  CHECK(copy.name != NULL && strcmp(copy.name, "Sarge") == 0 && copy.animations[4].first_frame == 4);
  qa_q3_pres_client_info_destroy(&copy);
  qa_q3_pres_client_info_destroy(&info);
  CHECK_ABORTS({
    qa_q3_pres_client_info bad;
    qa_q3_pres_client_info_init(&bad);
    qa_q3_animation one;
    memset(&one, 0, sizeof one);
    qa_q3_pres_client_info_set_animations(&bad, &one, NULL, 1);
  });
  CHECK_ABORTS({
    qa_q3_pres_client_info bad;
    qa_q3_pres_client_info_init(&bad);
    qa_q3_animation cells[QA_Q3_TOTAL_ANIMATION_COUNT];
    bool missing[QA_Q3_TOTAL_ANIMATION_COUNT];
    for (size_t i = 0; i < QA_Q3_TOTAL_ANIMATION_COUNT; i++) missing[i] = true;
    missing[2] = false;
    memset(cells, 0, sizeof cells);
    qa_q3_pres_client_info_set_animations(&bad, cells, missing, QA_Q3_TOTAL_ANIMATION_COUNT);
  });

  /* config.ts cvarTable: 89 base rows, 101 missionpack rows. */
  size_t count = 0;
  const qa_q3_pres_cvar_definition *base = qa_q3_pres_cvar_table(QA_Q3_BASE_PRODUCT_BASEQ3, &count);
  CHECK(count == 89);
  const qa_q3_pres_cvar_definition *mission =
      qa_q3_pres_cvar_table(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
  CHECK(count == 101);
  const qa_q3_pres_cvar_definition *base_defer = NULL, *mission_defer = NULL;
  for (size_t i = 0; i < 89; i++) if (base[i].symbol == QA_Q3_PRES_VM_CG_DEFER_PLAYERS) base_defer = &base[i];
  for (size_t i = 0; i < 101; i++) {
    if (mission[i].symbol == QA_Q3_PRES_VM_CG_DEFER_PLAYERS) mission_defer = &mission[i];
  }
  CHECK(base_defer != NULL && strcmp(base_defer->default_value, "1") == 0);
  CHECK(mission_defer != NULL && strcmp(mission_defer->default_value, "0") == 0);
  CHECK(strcmp(qa_q3_pres_client_vm_cvar_symbol_name(QA_Q3_PRES_VM_CG_DEFER_PLAYERS), "cg_deferPlayers") == 0);
}

static void test_core_prediction_and_pool(void) {
  /* ClientCommandHistory CMD_BACKUP window (prediction.ts). */
  qa_q3_pres_client_command_history history;
  qa_q3_pres_client_command_history_init(&history);
  qa_q3_base_user_command command;
  memset(&command, 0, sizeof command);
  command.server_time = 100;
  CHECK(qa_q3_pres_client_command_history_append(&history, command) == 1);
  qa_q3_base_user_command read_command;
  CHECK(qa_q3_pres_client_command_history_read(&history, 1, &read_command));
  CHECK(read_command.server_time == 100);
  CHECK(!qa_q3_pres_client_command_history_read(&history, 1 - 64, &read_command));
  CHECK_ABORTS({
    qa_q3_pres_client_command_history h;
    qa_q3_pres_client_command_history_init(&h);
    qa_q3_base_user_command out;
    (void)qa_q3_pres_client_command_history_read(&h, 2, &out);
  });
  CHECK_ABORTS({
    qa_q3_pres_client_command_history h;
    qa_q3_pres_client_command_history_init(&h);
    qa_q3_base_user_command out;
    (void)qa_q3_pres_client_command_history_read(&h, 0.5, &out);
  });

  /* LocalEntityPool order/recycle + validation (local-entities.ts). */
  qa_q3_pres_local_entity_pool *pool = NULL;
  CHECK(qa_q3_pres_local_entity_pool_create(&pool, QA_Q3_BASE_PRODUCT_BASEQ3));
  qa_q3_pres_ref_entity sprite = qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_create_sprite_entity());
  qa_q3_pres_local_entity *a =
      qa_q3_pres_local_entity_pool_allocate(pool, QA_Q3_PRES_LE_SCALE_FADE, sprite);
  qa_q3_pres_local_entity *b =
      qa_q3_pres_local_entity_pool_allocate(pool, QA_Q3_PRES_LE_SCALE_FADE, sprite);
  CHECK(qa_q3_pres_local_entity_pool_active_count(pool) == 2);
  qa_q3_pres_local_entity **active = NULL;
  size_t active_count = 0;
  CHECK(qa_q3_pres_local_entity_pool_active_entities(pool, &active, &active_count));
  CHECK(active_count == 2 && active[0] == b && active[1] == a);
  free(active);
  qa_q3_pres_local_entity_pool_free(pool, a);
  CHECK(!qa_q3_pres_local_entity_pool_is_active(pool, a));
  CHECK(qa_q3_pres_local_entity_pool_is_active(pool, b));
  qa_q3_pres_local_entity_pool_destroy(pool);
  CHECK_ABORTS({
    qa_q3_pres_local_entity_pool *p = NULL;
    (void)qa_q3_pres_local_entity_pool_create(&p, QA_Q3_BASE_PRODUCT_BASEQ3);
    qa_q3_pres_ref_entity model =
        qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity_default());
    (void)qa_q3_pres_local_entity_pool_allocate(p, QA_Q3_PRES_LE_KAMIKAZE, model);
  });
  CHECK_ABORTS({
    qa_q3_pres_local_entity_pool *p = NULL;
    (void)qa_q3_pres_local_entity_pool_create(&p, QA_Q3_BASE_PRODUCT_MISSIONPACK);
    qa_q3_pres_ref_entity model =
        qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity_default());
    (void)qa_q3_pres_local_entity_pool_allocate(p, QA_Q3_PRES_LE_SCALE_FADE, model);
  });

  /* placeString (events.ts). */
  char *place = qa_q3_pres_place_string(1);
  CHECK(place != NULL && strcmp(place, "^41st^7") == 0);
  free(place);
  place = qa_q3_pres_place_string(2);
  CHECK(place != NULL && strcmp(place, "^12nd^7") == 0);
  free(place);
  place = qa_q3_pres_place_string(0x4000 | 4);
  CHECK(place != NULL && strcmp(place, "Tied for 4th") == 0);
  free(place);
  place = qa_q3_pres_place_string(11);
  CHECK(place != NULL && strcmp(place, "11th") == 0);
  free(place);
  place = qa_q3_pres_place_string(23);
  CHECK(place != NULL && strcmp(place, "23rd") == 0);
  free(place);
}

/* ---- events.ts: weapon preparation order (event-weapon-preparation.test.ts) ---- */

typedef struct event_probe {
  char calls[8][32];
  size_t call_count;
  qa_q3_pres_client_info info;
  qa_q3_game_random random;
} event_probe;
static void probe_call(event_probe *probe, const char *name) {
  snprintf(probe->calls[probe->call_count], 32, "%.31s", name);
  probe->call_count++;
}
static void probe_present(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position,
    qa_q3_pres_event_source_fn source, void *source_ctx) {
  (void)ctx;
  (void)entity;
  (void)position;
  source(source_ctx);
}
static void probe_prepare(void *ctx, double weapon) {
  char name[32];
  snprintf(name, sizeof name, "prepare:%d", (int)weapon);
  probe_call(ctx, name);
}
static void probe_noop_entity(void *ctx, qa_q3_pres_client_entity *entity) { (void)ctx; (void)entity; }
static void probe_out_of_ammo(void *ctx) { probe_call(ctx, "outOfAmmo"); }
static void probe_fire(void *ctx, qa_q3_pres_client_entity *entity) { (void)entity; probe_call(ctx, "fire"); }
static void probe_hit_player(void *ctx, double w, qa_vec3 o, qa_vec3 d, double n) {
  (void)w; (void)o; (void)d; (void)n; probe_call(ctx, "missileHitPlayer");
}
static void probe_hit_wall(void *ctx, double w, double c, qa_vec3 o, qa_vec3 d,
    qa_q3_pres_impact_sound_mirror s) {
  (void)w; (void)c; (void)o; (void)d; (void)s; probe_call(ctx, "missileHitWall");
}
static void probe_rail(void *ctx, double c, qa_vec3 a, qa_vec3 b) {
  (void)c; (void)a; (void)b; probe_call(ctx, "railTrail");
}
static void probe_bullet(void *ctx, qa_vec3 e, double s, qa_q3_pres_bullet_hit_mirror h) {
  (void)e; (void)s; (void)h; probe_call(ctx, "bullet");
}
static void probe_shotgun(void *ctx, qa_q3_base_entity_state *s) { (void)s; probe_call(ctx, "shotgun"); }
static qa_q3_pres_local_entity *probe_smoke(void *ctx, qa_q3_pres_smoke_puff_options_mirror o) {
  (void)o;
  probe_call(ctx, "unexpectedSmoke");
  return NULL;
}
static qa_q3_pres_local_entity *probe_spawn(void *ctx, qa_vec3 o) {
  (void)o;
  probe_call(ctx, "unexpectedSpawn");
  return NULL;
}
static void probe_gib(void *ctx, qa_vec3 o) { (void)ctx; (void)o; }
static void probe_plum(void *ctx, double c, qa_vec3 o, double s) { (void)ctx; (void)c; (void)o; (void)s; }
static qa_q3_pres_client_info *probe_client_info(void *ctx, double number) {
  (void)number;
  return &((event_probe *)ctx)->info;
}
static const char *probe_null_name(void *ctx, double number) { (void)ctx; (void)number; return NULL; }
static const char *probe_empty_string(void *ctx, double index) { (void)ctx; (void)index; return ""; }
static qa_pcm_sound *probe_null_sound2(void *ctx, double n, const char *name) {
  (void)ctx; (void)n; (void)name; return NULL;
}
static qa_pcm_sound *probe_null_sound3(void *ctx, const char *path, bool compressed) {
  (void)ctx; (void)path; (void)compressed; return NULL;
}
static void probe_start_sound(void *ctx, bool h, qa_vec3 o, double e, double c, qa_pcm_sound *s) {
  (void)ctx; (void)h; (void)o; (void)e; (void)c; (void)s;
}
static void probe_stop_loop(void *ctx, double e) { (void)ctx; (void)e; }
static void probe_buffered(void *ctx, qa_pcm_sound *s) { (void)ctx; (void)s; }
static void probe_print(void *ctx, const char *m) { (void)ctx; (void)m; }
static void probe_center(void *ctx, const char *m, double y, double w) { (void)ctx; (void)m; (void)y; (void)w; }

static void test_core_event_weapon_preparation(void) {
  qa_q3_pres_client_game_state state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  qa_q3_wire_snapshot input;
  memset(&input, 0, sizeof input);
  qa_q3_player_state_record_init(&input.player_state, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  input.message_number = 1;
  input.server_time = 1000;
  input.area_mask_len = 32;
  qa_q3_pres_retail_snapshot snap;
  CHECK(qa_q3_pres_retail_snapshot_from_wire(&snap, &input));
  state.snap = &snap;
  state.time = 1000;
  event_probe probe;
  memset(&probe, 0, sizeof probe);
  qa_q3_pres_client_info_init(&probe.info);
  qa_q3_pres_client_event_host host;
  memset(&host, 0, sizeof host);
  host.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  host.options.game_type = QA_Q3_BASE_GT_FFA;
  host.random = &probe.random;
  host.entities_ctx = &probe;
  host.set_entity_sound_position = probe_noop_entity;
  host.beam = probe_noop_entity;
  host.weapons.ctx = &probe;
  host.weapons.out_of_ammo_change = probe_out_of_ammo;
  host.weapons.fire_weapon = probe_fire;
  host.weapons.missile_hit_player = probe_hit_player;
  host.weapons.missile_hit_wall = probe_hit_wall;
  host.weapons.rail_trail = probe_rail;
  host.weapons.bullet = probe_bullet;
  host.weapons.shotgun_fire = probe_shotgun;
  host.effects.ctx = &probe;
  host.effects.smoke_puff = probe_smoke;
  host.effects.spawn_effect = probe_spawn;
  host.effects.gib_player = probe_gib;
  host.effects.score_plum = probe_plum;
  host.ctx = &probe;
  host.present_event = probe_present;
  host.prepare_weapon = probe_prepare;
  host.client_info = probe_client_info;
  host.client_info_ctx = &probe;
  host.player_name = probe_null_name;
  host.sound_config_string = probe_empty_string;
  host.custom_sound = probe_null_sound2;
  host.register_sound = probe_null_sound3;
  host.start_sound = probe_start_sound;
  host.stop_looping_sound = probe_stop_loop;
  host.add_buffered_sound = probe_buffered;
  host.print = probe_print;
  host.center_print = probe_center;
  qa_q3_pres_client_event_runtime events;
  qa_q3_pres_client_event_runtime_init(&events, &state, host);

  qa_q3_pres_client_entity *rail = qa_q3_pres_client_game_state_entity_at(&state, 5);
  rail->previous_event = 0;
  rail->current_state.e_type = QA_Q3_BASE_ET_EVENTS + QA_Q3_EV_RAILTRAIL;
  rail->current_state.event_parm = 255;
  rail->current_state.client_num = 0;
  rail->current_state.pos.type = QA_Q3_BASE_TR_STATIONARY;
  rail->current_state.pos.time = 1000;
  rail->current_state.pos.base = qa_math_vec3(200, 0, 0);
  rail->current_state.origin2 = qa_math_vec3(0, 0, 0);
  qa_q3_pres_client_event_runtime_check_events(&events, rail);
  char expected_rail[32];
  snprintf(expected_rail, sizeof expected_rail, "prepare:%d", (int)QA_Q3_WP_RAILGUN);
  CHECK(probe.call_count == 2 && strcmp(probe.calls[0], expected_rail) == 0 &&
        strcmp(probe.calls[1], "railTrail") == 0);

  probe.call_count = 0;
  qa_q3_pres_client_entity *missile = qa_q3_pres_client_game_state_entity_at(&state, 6);
  missile->previous_event = 0;
  missile->current_state.e_type = QA_Q3_BASE_ET_EVENTS + QA_Q3_EV_MISSILE_MISS;
  missile->current_state.weapon = QA_Q3_WP_ROCKET_LAUNCHER;
  missile->current_state.event_parm = 0;
  missile->current_state.pos.type = QA_Q3_BASE_TR_STATIONARY;
  missile->current_state.pos.time = 1000;
  missile->current_state.pos.base = qa_math_vec3(10, 0, 0);
  qa_q3_pres_client_event_runtime_check_events(&events, missile);
  char expected_rocket[32];
  snprintf(expected_rocket, sizeof expected_rocket, "prepare:%d", (int)QA_Q3_WP_ROCKET_LAUNCHER);
  CHECK(probe.call_count == 2 && strcmp(probe.calls[0], expected_rocket) == 0 &&
        strcmp(probe.calls[1], "missileHitWall") == 0);
  qa_q3_pres_client_info_destroy(&probe.info);
  qa_q3_pres_retail_snapshot_destroy(&snap);
  state.snap = NULL;
  qa_q3_pres_client_game_state_destroy(&state);
}

/* ---- snapshots.ts / resources.ts / server-commands.ts / player-state.ts / media.ts ---- */

typedef struct snap_probe {
  int executed, respawns, checks, transitions, lagometer;
  double last_sequence;
  char warnings[256];
} snap_probe;
static qa_q3_pres_snapshot_current snap_current(void *ctx) {
  (void)ctx;
  qa_q3_pres_snapshot_current current = {2, 2000};
  return current;
}
static bool snap_read(void *ctx, double number, qa_q3_wire_snapshot *out) {
  (void)ctx;
  if (number != 1 && number != 2) return false;
  memset(out, 0, sizeof *out);
  qa_q3_player_state_record_init(&out->player_state, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  out->message_number = (int64_t)number;
  out->server_time = number == 1 ? 1000 : 2000;
  out->server_command_number = (int64_t)number;
  out->area_mask_len = 32;
  out->entities = malloc(sizeof *out->entities);
  if (out->entities == NULL) return false;
  out->entity_count = 1;
  qa_q3_entity_state_init(&out->entities[0]);
  out->entities[0].number = 7;
  out->entities[0].origin = qa_math_vec3(number, 0, 0);
  return true;
}
static void snap_execute(void *ctx, double sequence) {
  snap_probe *probe = ctx;
  probe->executed++;
  probe->last_sequence = sequence;
}
static void snap_respawn(void *ctx) { ((snap_probe *)ctx)->respawns++; }
static void snap_reset_entity(void *ctx, qa_q3_pres_client_entity *e) { (void)ctx; (void)e; }
static void snap_check(void *ctx, qa_q3_pres_client_entity *e) { (void)e; ((snap_probe *)ctx)->checks++; }
static void snap_transition_ps(void *ctx, qa_q3_base_source_player_state *c,
    const qa_q3_base_source_player_state *p) {
  (void)c; (void)p; ((snap_probe *)ctx)->transitions++;
}
static void snap_lagometer(void *ctx, const qa_q3_wire_snapshot *s) {
  (void)s;
  ((snap_probe *)ctx)->lagometer++;
}
static void snap_warn(void *ctx, const char *message) {
  snprintf(((snap_probe *)ctx)->warnings, 256, "%.250s", message);
}

static void test_core_snapshots(void) {
  qa_q3_pres_client_game_state state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  snap_probe probe;
  memset(&probe, 0, sizeof probe);
  qa_q3_pres_snapshot_host host;
  memset(&host, 0, sizeof host);
  host.source.ctx = &probe;
  host.source.current = snap_current;
  host.source.read = snap_read;
  host.ctx = &probe;
  host.execute_server_commands = snap_execute;
  host.respawn = snap_respawn;
  host.reset_player_entity = snap_reset_entity;
  host.check_events = snap_check;
  host.transition_player_state = snap_transition_ps;
  host.lagometer_snapshot = snap_lagometer;
  host.warn = snap_warn;
  qa_q3_pres_snapshot_runtime runtime;
  qa_q3_pres_snapshot_runtime_init(&runtime, &state, host);
  state.time = 1500;
  CHECK(qa_q3_pres_snapshot_runtime_process_snapshots(&runtime));
  CHECK(state.snap != NULL && state.snap->server_time == 1000);
  CHECK(state.next_snap != NULL && state.next_snap->server_time == 2000);
  CHECK(probe.executed == 1 && probe.last_sequence == 1 && probe.respawns == 1);
  qa_q3_pres_client_entity *entity = qa_q3_pres_client_game_state_entity_at(&state, 7);
  CHECK(entity->current_valid && vec_eq(entity->current_state.origin, 1, 0, 0));
  CHECK(vec_eq(entity->next_state.origin, 2, 0, 0));
  state.time = 2500;
  CHECK(qa_q3_pres_snapshot_runtime_process_snapshots(&runtime));
  CHECK(state.snap != NULL && state.snap->server_time == 2000 && state.next_snap == NULL);
  CHECK(vec_eq(entity->current_state.origin, 2, 0, 0));
  CHECK(probe.executed == 2 && probe.last_sequence == 2);
  qa_q3_pres_snapshot_runtime_destroy(&runtime);
  CHECK(state.snap == NULL && state.next_snap == NULL);
  qa_q3_pres_client_game_state_destroy(&state);
}

typedef struct resource_probe {
  qa_material_picture picture;
  qa_q3_pres_scene_model model;
  qa_q3_pres_scene_skin skin;
  qa_q3_pres_scene_skin_surface surface;
} resource_probe;
static bool res_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  resource_probe *probe = ctx;
  (void)path;
  *out = probe->model;
  return true;
}
static bool res_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) {
  resource_probe *probe = ctx;
  (void)path;
  *out = &probe->skin;
  return true;
}
static bool res_shader(void *ctx, const char *path, bool mip, const qa_material_picture **out) {
  resource_probe *probe = ctx;
  (void)path;
  (void)mip;
  *out = &probe->picture;
  return true;
}
static bool res_world(void *ctx, const char *path, qa_q3_pres_world_scene *out) {
  (void)ctx;
  (void)path;
  memset(out, 0, sizeof *out);
  return true;
}
static void res_noop_scene(void *ctx) { (void)ctx; }
static void res_noop_ref(void *ctx, const qa_q3_pres_admitted_ref_entity *e) { (void)ctx; (void)e; }
static void res_noop_poly(void *ctx, const qa_q3_pres_ref_poly *p) { (void)ctx; (void)p; }
static void res_noop_light(void *ctx, qa_q3_pres_dynamic_light l) { (void)ctx; (void)l; }
static void res_noop_render(void *ctx, const qa_q3_pres_refdef *r) { (void)ctx; (void)r; }
static void res_remap(void *ctx, const char *a, const char *b, const char *c) {
  (void)ctx; (void)a; (void)b; (void)c;
}
static qa_q3_pres_q3_renderer_resources *make_resources(resource_probe *probe) {
  qa_q3_pres_resource_host host;
  memset(&host, 0, sizeof host);
  host.scene.ctx = probe;
  host.scene.clear_scene = res_noop_scene;
  host.scene.add_ref_entity = res_noop_ref;
  host.scene.add_poly = res_noop_poly;
  host.scene.add_light = res_noop_light;
  host.scene.render_scene = res_noop_render;
  host.zero_picture.name = "zero";
  host.ctx = probe;
  host.model = res_model;
  host.skin = res_skin;
  host.shader = res_shader;
  host.world = res_world;
  host.remap_shader = res_remap;
  qa_q3_pres_q3_renderer_resources *resources = NULL;
  (void)qa_q3_pres_q3_renderer_resources_init(&resources, host, NULL, QA_Q3_PRES_HANDLES_CLIENT);
  return resources;
}

static void test_core_resources(void) {
  resource_probe probe;
  memset(&probe, 0, sizeof probe);
  probe.picture.name = "gfx/test";
  probe.picture.order = 42;
  probe.model = qa_q3_pres_default_model;
  probe.model.kind = QA_Q3_PRES_SCENE_MODEL_INLINE;
  probe.model.path = "*1";
  probe.model.resource.id = "resource:model";
  probe.surface.name = "torso";
  probe.surface.shader = "gfx/skin";
  probe.skin.surfaces = &probe.surface;
  probe.skin.surface_count = 1;
  qa_q3_pres_q3_renderer_resources *resources = make_resources(&probe);
  CHECK(resources != NULL);
  qa_q3_pres_scene_model model;
  CHECK(qa_q3_pres_q3_renderer_resources_register_model(resources, "models/test.md3", &model));
  CHECK(qa_q3_pres_q3_renderer_resources_model_handle(resources, &model) == 1);
  const qa_q3_pres_scene_shader *shader = NULL;
  CHECK(qa_q3_pres_q3_renderer_resources_register_shader(resources, "GFX/Test", &shader));
  CHECK(shader != NULL && qa_q3_pres_q3_renderer_resources_shader_handle(resources, shader) == 1);
  const qa_q3_pres_scene_skin *skin = NULL;
  CHECK(qa_q3_pres_q3_renderer_resources_register_skin(resources, "models/test.skin", &skin));
  CHECK(skin == &probe.skin);
  qa_save_value *checkpoint = qa_q3_pres_q3_renderer_resources_capture_checkpoint(resources);
  CHECK(checkpoint != NULL);
  qa_q3_pres_q3_renderer_resources *restored = make_resources(&probe);
  CHECK(restored != NULL);
  CHECK(qa_q3_pres_q3_renderer_resources_restore_checkpoint(restored, checkpoint));
  qa_q3_pres_scene_model restored_model =
      qa_q3_pres_q3_renderer_resources_model_for_handle(restored, 1);
  CHECK(qa_q3_pres_q3_renderer_resources_model_handle(restored, &restored_model) == 1);
  const qa_q3_pres_scene_shader *restored_shader =
      qa_q3_pres_q3_renderer_resources_shader_for_handle(restored, 1);
  CHECK(restored_shader != NULL && strcmp(restored_shader->name, "gfx/test") == 0);
  qa_save_value_free(checkpoint);
  CHECK_ABORTS({
    qa_q3_pres_scene_model foreign = qa_q3_pres_default_model;
    foreign.kind = QA_Q3_PRES_SCENE_MODEL_INLINE;
    foreign.path = "*99";
    (void)qa_q3_pres_q3_renderer_resources_model_handle(resources, &foreign);
  });
  qa_q3_pres_q3_renderer_resources_destroy(restored);
  qa_q3_pres_q3_renderer_resources_destroy(resources);
}

typedef struct media_probe {
  char paths[4][128];
  bool compressed_flags[4];
  size_t register_calls;
  qa_pcm_sound sound;
  bool voice_loaded;
} media_probe;
static bool bank_register(void *ctx, const char *path, bool compressed, qa_pcm_sound **out) {
  media_probe *probe = ctx;
  if (probe->register_calls < 4) {
    snprintf(probe->paths[probe->register_calls], 128, "%.120s", path);
    probe->compressed_flags[probe->register_calls] = compressed;
  }
  probe->register_calls++;
  *out = &probe->sound;
  return true;
}
static double bank_index(void *ctx, qa_pcm_sound *sound) { (void)ctx; (void)sound; return 0; }
static qa_q3_pres_client_info *media_client_info(void *ctx, double index) {
  (void)index;
  return &((qa_q3_pres_client_game_static_state *)ctx)->client_info[0];
}
static bool media_new_info(void *ctx, double n, const char *c) { (void)ctx; (void)n; (void)c; return true; }
static bool media_load_voice(void *ctx) { ((media_probe *)ctx)->voice_loaded = true; return true; }
static bool media_spectators(void *ctx) { (void)ctx; return true; }
static const char *media_config(void *ctx, double index) { (void)ctx; (void)index; return ""; }
static qa_q3_pres_client_media_settings media_settings(void *ctx) {
  (void)ctx;
  qa_q3_pres_client_media_settings settings = {false};
  return settings;
}
static void media_loading_string(void *ctx, const char *t) { (void)ctx; (void)t; }
static void media_loading_index(void *ctx, double i) { (void)ctx; (void)i; }
static void media_clear_scene(void *ctx) { (void)ctx; }
static bool registry_items(void *ctx, double number) { (void)ctx; (void)number; return true; }
static const qa_q3_pres_scene_shader *registry_shader(void *ctx) { (void)ctx; return NULL; }

static void test_core_media_sounds(void) {
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  qa_q3_pres_client_game_static_state_init(&static_state, QA_Q3_BASE_PRODUCT_BASEQ3);
  media_probe probe;
  memset(&probe, 0, sizeof probe);
  resource_probe resource_fixture;
  memset(&resource_fixture, 0, sizeof resource_fixture);
  qa_q3_pres_q3_renderer_resources *resources = make_resources(&resource_fixture);
  CHECK(resources != NULL);
  qa_q3_pres_client_sound_bank bank = {&probe, bank_register, bank_index};
  qa_q3_pres_client_weapon_media_registry_mirror registry;
  memset(&registry, 0, sizeof registry);
  registry.ctx = &probe;
  registry.register_item_visuals = registry_items;
  registry.effects.lightning_shader = registry_shader;
  registry.effects.rocket_explosion_shader = registry_shader;
  qa_q3_pres_client_media media;
  qa_q3_pres_renderer_resources resources_view = qa_q3_pres_q3_renderer_resources_as_resources(resources);
  CHECK(qa_q3_pres_client_media_init(&media, QA_Q3_BASE_PRODUCT_BASEQ3, &static_state, resources_view,
      bank, registry));
  qa_q3_pres_client_media_host host;
  memset(&host, 0, sizeof host);
  host.state = &state;
  host.static_state = &static_state;
  host.clients_ctx = &static_state;
  host.new_client_info = media_new_info;
  host.client_info = media_client_info;
  host.commands_ctx = &probe;
  host.load_voice_chats = media_load_voice;
  host.build_spectator_string = media_spectators;
  host.ctx = &probe;
  host.config_string = media_config;
  host.settings = media_settings;
  host.loading_string = media_loading_string;
  host.loading_item = media_loading_index;
  host.loading_client = media_loading_index;
  host.clear_scene = media_clear_scene;
  CHECK(qa_q3_pres_register_client_sounds(&media, &host));
  CHECK(media.sounds.one_minute_sound == &probe.sound);
  CHECK(strcmp(probe.paths[0], "sound/feedback/1_minute.wav") == 0 && probe.compressed_flags[0]);
  CHECK(media.sounds.capture_award_sound == NULL); /* FFA: no team sounds */
  CHECK(media.footsteps[QA_Q3_PRES_FOOTSTEP_METAL][3] == &probe.sound);
  CHECK(!probe.voice_loaded); /* baseq3 loads no voice chats */
  qa_q3_pres_client_media_destroy(&media);
  qa_q3_pres_q3_renderer_resources_destroy(resources);
  qa_q3_pres_client_game_static_state_destroy(&static_state);
  qa_q3_pres_client_game_state_destroy(&state);
}

/* ---- pres-draw: audio, scene, marks, draw tools, weapons ---- */

typedef struct draw_probe {
  qa_play_sound plays[4];
  qa_loop_sound loops[2];
  int play_count, loop_count, update_count, stop_count, warnings;
  const qa_sound_asset *fake_asset;
  qa_pcm_sound fake_pcm;
} draw_probe;
static bool probe_no_actor(void *ctx, double number, qa_actor_id *out) {
  (void)ctx; (void)number; (void)out;
  return false;
}
static double probe_frame(void *ctx) { (void)ctx; return 1; }
static void probe_play(void *ctx, qa_play_sound sound) {
  draw_probe *probe = ctx;
  probe->plays[probe->play_count++] = sound;
}
static void probe_loop(void *ctx, qa_loop_sound sound) {
  draw_probe *probe = ctx;
  probe->loops[probe->loop_count++] = sound;
}
static void probe_update(void *ctx, qa_actor_id actor, qa_vec3 position) {
  (void)actor; (void)position;
  ((draw_probe *)ctx)->update_count++;
}
static void probe_stop(void *ctx, qa_seat_id seat, qa_actor_id actor) {
  (void)seat; (void)actor;
  ((draw_probe *)ctx)->stop_count++;
}
static const qa_sound_asset *probe_load(void *ctx, const char *path, bool compressed) {
  (void)path; (void)compressed;
  return ((draw_probe *)ctx)->fake_asset;
}

static void test_draw_stack_audio(void) {
  /* audio-stale-actor.test.ts: released actors are silently ignored. */
  draw_probe probe;
  memset(&probe, 0, sizeof probe);
  qa_sound_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.resource = "resource:q3-audio-stale";
  asset.name = "sound/test.wav";
  probe.fake_asset = &asset;
  qa_q3_pres_presentation_sound_bank *bank =
      qa_q3_pres_presentation_sound_bank_create(NULL, NULL, probe_load, &probe);
  CHECK(bank != NULL);
  qa_pcm_sound *pcm = NULL;
  CHECK(qa_q3_pres_presentation_sound_bank_sound(bank, "sound/test.wav", false, &pcm));
  CHECK(pcm == &asset.pcm);
  CHECK(qa_q3_pres_presentation_sound_bank_index_for_sound(bank, pcm) == 1);
  qa_pcm_sound *none = (qa_pcm_sound *)1;
  CHECK(qa_q3_pres_presentation_sound_bank_register_sound(bank, "", false, &none) && none == NULL);
  none = (qa_pcm_sound *)1;
  CHECK(qa_q3_pres_presentation_sound_bank_register_sound(bank, "*sexed", false, &none) && none == NULL);
  CHECK(qa_q3_pres_presentation_sound_bank_sound_at_index(bank, 0) == NULL);
  CHECK(qa_q3_pres_presentation_sound_bank_sound_at_index(bank, 1) == pcm);
  CHECK_ABORTS((void)qa_q3_pres_presentation_sound_bank_sound_at_index(bank, 2));
  CHECK_ABORTS({ qa_pcm_sound *o; (void)qa_q3_pres_presentation_sound_bank_register_sound(bank, NULL, false, &o); });
  qa_q3_pres_presentation_audio audio;
  qa_q3_pres_audio_target target;
  memset(&target, 0, sizeof target);
  target.sounds = bank;
  target.ctx = &probe;
  target.actor = probe_no_actor;
  target.frame_number = probe_frame;
  target.play = probe_play;
  target.loop = probe_loop;
  target.update_actor = probe_update;
  target.stop_loop = probe_stop;
  qa_q3_pres_presentation_audio_init(&audio, target);
  qa_vec3 origin = {1, 2, 3};
  qa_q3_pres_presentation_audio_update_sound_position(&audio, 64, origin);
  CHECK(probe.update_count == 0);
  qa_q3_pres_presentation_audio_stop_looping_sound(&audio, 64);
  CHECK(probe.stop_count == 0);
  qa_q3_pres_presentation_audio_add_loop_sound(&audio, 64, origin, origin, pcm, true);
  CHECK(probe.loop_count == 0);
  qa_q3_pres_presentation_audio_start_sound(&audio, NULL, 64, 0, pcm);
  CHECK(probe.play_count == 0);
  qa_q3_pres_presentation_audio_start_sound(&audio, &origin, 64, 0, pcm);
  CHECK(probe.play_count == 1 && !probe.plays[0].has_actor);
  CHECK(probe.plays[0].origin.kind == QA_SOUND_ORIGIN_FIXED && probe.plays[0].origin.position.x == 1);
  probe.play_count = 0;
  qa_start_sound_options entity_options;
  memset(&entity_options, 0, sizeof entity_options);
  entity_options.entity = 64;
  entity_options.origin.kind = QA_VOICE_ORIGIN_ENTITY;
  entity_options.origin.entity = 64;
  entity_options.volume = 127;
  qa_q3_pres_presentation_audio_start_source_sound(&audio, pcm, entity_options);
  CHECK(probe.play_count == 0);
  entity_options.origin.kind = QA_VOICE_ORIGIN_FIXED;
  entity_options.origin.position = origin;
  qa_q3_pres_presentation_audio_start_source_sound(&audio, pcm, entity_options);
  CHECK(probe.play_count == 1 && !probe.plays[0].has_actor);
  CHECK(fabs(probe.plays[0].volume - 1) < 1e-9);
  qa_q3_pres_presentation_sound_bank_destroy(bank);
}

static double mark_clock;
static double probe_mark_clock(void *ctx) { (void)ctx; return mark_clock; }
static bool probe_mark_enabled(void *ctx) { (void)ctx; return true; }
static const qa_q3_pres_scene_shader *probe_no_shader(void *ctx) { (void)ctx; return NULL; }

static void test_draw_stack_marks(void) {
  /* presentation.test.ts marks projection and fade over lifetime. */
  static const qa_vec3 positions[4] = {{-32, -32, 0}, {32, -32, 0}, {-32, 32, 0}, {32, 32, 0}};
  static const double indices[6] = {0, 2, 1, 1, 2, 3};
  qa_q3_pres_mark_surface surface;
  memset(&surface, 0, sizeof surface);
  surface.kind = QA_Q3_PRES_MARK_FACE;
  surface.content_flags = 1;
  surface.plane = (qa_plane){{0, 0, 1}, 0};
  surface.vertices = positions;
  surface.vertex_count = 4;
  surface.indices = indices;
  surface.index_count = 6;
  qa_q3_pres_mark_leaf leaf = {0, 1};
  double leaf_surfaces[1] = {0};
  qa_q3_pres_mark_geometry geometry;
  memset(&geometry, 0, sizeof geometry);
  geometry.leaves = &leaf;
  geometry.leaf_count = 1;
  geometry.leaf_surfaces = leaf_surfaces;
  geometry.leaf_surface_count = 1;
  geometry.surface_count = 1;
  geometry.surfaces = &surface;
  geometry.surfaces_count = 1;
  qa_q3_pres_bsp_mark_projector projector;
  qa_q3_pres_bsp_mark_projector_init(&projector, geometry);
  mark_clock = 100;
  qa_q3_pres_impact_mark_options options = {NULL, probe_mark_clock, probe_mark_enabled, probe_no_shader};
  qa_q3_pres_impact_mark_system *marks = qa_q3_pres_impact_mark_system_create(&projector, options);
  CHECK(marks != NULL);
  qa_q3_pres_scene_shader shader = {"gfx/damage/bullet_mrk"};
  qa_q3_pres_impact_mark_request request = {&shader, {0, 0, 1}, {0, 0, 1}, 0, {1, 1, 1, 1}, true, 8, false};
  qa_q3_pres_ref_poly *polys = NULL;
  size_t poly_count = 99;
  CHECK(qa_q3_pres_impact_mark_system_impact_mark(marks, request, &polys, &poly_count));
  CHECK(poly_count == 0);
  CHECK(qa_q3_pres_impact_mark_system_active_mark_count(marks) == 2);
  CHECK(qa_q3_pres_impact_mark_system_add_marks(marks, &polys, &poly_count));
  CHECK(poly_count == 2);
  bool flat = true;
  for (size_t i = 0; i < poly_count; i++)
    for (size_t v = 0; v < polys[i].vertex_count; v++)
      if (polys[i].vertices[v].position.z != 0) flat = false;
  CHECK(flat);
  for (size_t i = 0; i < poly_count; i++) qa_q3_pres_ref_poly_destroy(&polys[i]);
  free(polys);
  mark_clock = 9600;
  CHECK(qa_q3_pres_impact_mark_system_add_marks(marks, &polys, &poly_count));
  CHECK(poly_count >= 1 && polys[0].vertex_count >= 1 && polys[0].vertices[0].color.w == 127);
  for (size_t i = 0; i < poly_count; i++) qa_q3_pres_ref_poly_destroy(&polys[i]);
  free(polys);
  mark_clock = 10101;
  CHECK(qa_q3_pres_impact_mark_system_add_marks(marks, &polys, &poly_count));
  CHECK(poly_count == 0 && qa_q3_pres_impact_mark_system_active_mark_count(marks) == 0);
  qa_q3_pres_impact_mark_system_destroy(marks);
  CHECK_ABORTS({
    qa_q3_pres_bsp_mark_projector p;
    qa_q3_pres_bsp_mark_projector_init(&p, geometry);
    qa_q3_pres_impact_mark_system *m = qa_q3_pres_impact_mark_system_create(&p, options);
    qa_q3_pres_impact_mark_request bad = request;
    bad.radius = 0;
    qa_q3_pres_ref_poly *o = NULL; size_t c = 0;
    (void)qa_q3_pres_impact_mark_system_impact_mark(m, bad, &o, &c);
  });
}

typedef struct scene_probe { int warnings; qa_q3_pres_presented_scene scenes[3]; int published; } scene_probe;
static const qa_q3_pres_fog_selection *probe_fogs(void *ctx, size_t *count) {
  (void)ctx; *count = 0; return NULL;
}
static void probe_scene_print(void *ctx, const char *text) {
  (void)text;
  ((scene_probe *)ctx)->warnings++;
}
static const qa_actor_id *probe_scene_actor(void *ctx, const qa_q3_pres_ref_model_entity *entity) {
  (void)ctx; (void)entity; return NULL;
}
static void probe_publish(void *ctx, qa_q3_pres_presented_scene *scene) {
  scene_probe *probe = ctx;
  probe->scenes[probe->published++] = *scene;
}

static void test_draw_stack_scene(void) {
  scene_probe probe;
  memset(&probe, 0, sizeof probe);
  qa_q3_pres_scene_target target;
  memset(&target, 0, sizeof target);
  target.viewport = (qa_rect){640, 0, 640, 480};
  target.near_clip = 4;
  target.far_clip = 4096;
  target.ctx = &probe;
  target.fog_selections = probe_fogs;
  target.print = probe_scene_print;
  target.actor = probe_scene_actor;
  target.publish = probe_publish;
  qa_q3_pres_scene_recorder *scene = qa_q3_pres_scene_recorder_create(target);
  CHECK(scene != NULL);
  qa_q3_pres_scene_model loaded = qa_q3_pres_default_model;
  loaded.kind = QA_Q3_PRES_SCENE_MODEL_LOADED;
  loaded.path = "models/test.md3";
  qa_q3_pres_ref_model_entity parent = qa_q3_pres_create_model_entity(loaded);
  parent.origin = (qa_vec3){10, 20, 30};
  parent.shading.shader_rgba = (qa_vec4){255, 128, 0, 255};
  qa_q3_pres_admitted_ref_entity admitted;
  admitted.is_poly_record = false;
  admitted.entity = qa_q3_pres_ref_entity_from_model(parent);
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  admitted.entity = qa_q3_pres_ref_entity_from_portal(qa_q3_pres_create_portal_entity());
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  admitted.entity = qa_q3_pres_ref_entity_from_beam(qa_q3_pres_create_beam_entity());
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  admitted.entity = qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity_default());
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  qa_q3_pres_ref_sprite_entity sprite = qa_q3_pres_create_sprite_entity();
  sprite.radius = 8;
  admitted.entity = qa_q3_pres_ref_entity_from_sprite(sprite);
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  admitted.entity = qa_q3_pres_ref_entity_from_model(parent);
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  qa_q3_pres_ref_poly_vertex vertex = {{1, 2, 3}, {0, 0}, {255, 255, 255, 255}};
  qa_q3_pres_ref_poly null_poly = {NULL, &vertex, 1};
  qa_q3_pres_scene_recorder_add_poly(scene, &null_poly);
  qa_q3_pres_scene_shader mark_shader = {"gfx/damage/bullet_mrk"};
  qa_q3_pres_ref_poly good_poly = {&mark_shader, &vertex, 1};
  qa_q3_pres_scene_recorder_add_poly(scene, &good_poly);
  CHECK(probe.warnings == 1);
  parent.origin = (qa_vec3){0, 0, 0};
  qa_q3_pres_refdef refdef = qa_q3_pres_create_refdef();
  refdef.width = 640; refdef.height = 480; refdef.fov_x = 90; refdef.fov_y = 73.7398;
  qa_q3_pres_scene_recorder_render_scene(scene, &refdef);
  qa_q3_pres_scene_recorder_render_scene(scene, &refdef);
  qa_q3_pres_scene_recorder_clear_scene(scene);
  admitted.entity = qa_q3_pres_ref_entity_from_sprite(sprite);
  qa_q3_pres_scene_recorder_add_ref_entity(scene, &admitted);
  qa_q3_pres_scene_recorder_render_scene(scene, &refdef);
  CHECK(probe.published == 3);
  qa_q3_pres_presented_scene *first = &probe.scenes[0];
  CHECK(first->content.admission.id.origin == QA_Q3_PRES_SCENE_ORIGIN_NATIVE);
  CHECK(!qa_q3_pres_scene_admission_id_equals(first->content.admission.id, probe.scenes[1].content.admission.id));
  CHECK(first->content.admission.entity_count == 6 && first->content.admission.polygon_count == 1);
  CHECK(first->content.model_count == 2 && first->content.models[0].entity_index == 0 &&
        first->content.models[1].entity_index == 5);
  CHECK(first->content.portal_count == 1 && first->content.portals[0].entity_index == 1);
  CHECK(first->content.special_count == 2);
  CHECK(first->content.effect_count == 2);
  CHECK(first->content.models[0].entity.transform.origin.x == 10);
  CHECK(fabs(first->content.models[0].entity.color.y - 128.0 / 255) < 1e-9);
  CHECK(first->camera.viewport.x == 640);
  CHECK(probe.scenes[2].content.admission.entity_count == 1);
  for (int i = 0; i < 3; i++) qa_q3_pres_presented_scene_destroy(&probe.scenes[i]);
  qa_q3_pres_scene_recorder_destroy(scene);
}

static void test_draw_stack_tools_and_weapons(void) {
  CHECK(qa_q3_pres_draw_strlen("abc") == 3);
  CHECK(qa_q3_pres_draw_strlen("^1a") == 1);
  CHECK(qa_q3_pres_draw_strlen("a^^b") == 2);
  qa_vec4 color;
  CHECK(!qa_q3_pres_fade_color(1000, 0, 500, &color));
  CHECK(qa_q3_pres_fade_color(1400, 1000, 500, &color) && fabs(color.w - 0.5) < 1e-6);
  CHECK(qa_q3_pres_team_color(QA_Q3_BASE_TEAM_RED).x == 1);
  qa_vec4 health = qa_q3_pres_get_color_for_health(100, 0);
  CHECK(health.x == 1 && health.y == 1 && health.z == 1);
  CHECK(qa_q3_pres_get_color_for_health(0, 50).x == 0);
  CHECK(qa_q3_pres_proportional_size_scale(0x10) == 0.75);
  /* Weapon selection cycles owned+stocked weapons, skipping gauntlet. */
  qa_q3_pres_client_game_state state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  qa_q3_wire_snapshot input;
  memset(&input, 0, sizeof input);
  qa_q3_player_state_record_init(&input.player_state, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  input.area_mask_len = 32;
  qa_q3_pres_retail_snapshot snap;
  CHECK(qa_q3_pres_retail_snapshot_from_wire(&snap, &input));
  /* Own machinegun(2)+shotgun(3); ammo only for shotgun. */
  qa_q3_base_player_state_slots_set(&snap.player_state.stats, 2, (1 << 2) | (1 << 3));
  qa_q3_base_player_state_slots_set(&snap.player_state.ammo, 3, 10);
  state.snap = &snap;
  state.weapon_select = 2;
  qa_q3_pres_client_weapon_selection selection;
  qa_q3_pres_client_weapon_selection_init(&selection, &state, NULL, NULL);
  qa_q3_pres_client_weapon_selection_next_weapon(&selection);
  CHECK(state.weapon_select == 3);
  /* Frame audio: buffered sounds play one per 750ms window. */
  draw_probe audio_probe;
  memset(&audio_probe, 0, sizeof audio_probe);
  state.time = 1000;
  qa_q3_pres_client_frame_audio frame_audio;
  qa_q3_pres_client_frame_audio_host frame_host;
  memset(&frame_host, 0, sizeof frame_host);
  qa_q3_pres_client_frame_audio_init(&frame_audio, &state, NULL, frame_host);
  qa_pcm_sound buffered;
  memset(&buffered, 0, sizeof buffered);
  qa_q3_pres_client_frame_audio_add_buffered_sound(&frame_audio, &buffered);
  CHECK(state.sound_buffer_in == 1 && state.sound_buffer[0] == &buffered);
  state.snap = NULL;
  qa_q3_pres_retail_snapshot_destroy(&snap);
  qa_q3_pres_client_game_state_destroy(&state);
}

/* ---- presentation-fill: scoreboard.ts / hud-corners.ts draw logic ---- */
typedef struct fill_probe {
  qa_rect rects[512];
  char names[512][24];
  int count;
  double corner_cvars[7];
  double board_cvars[2];
  char sent[64];
  int sent_count;
  double now_ms;
} fill_probe;
static void fill_set_color(void *ctx, const qa_vec4 *color) { (void)ctx; (void)color; }
static void fill_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv, const qa_picture_asset *picture) {
  (void)uv;
  fill_probe *probe = ctx;
  if (probe->count >= 512) return;
  probe->rects[probe->count] = rect;
  const char *name = picture && picture->kind == QA_PICTURE_MATERIAL ? picture->material.name : NULL;
  snprintf(probe->names[probe->count], sizeof probe->names[0], "%s", name ? name : "");
  probe->count++;
}
static qa_material_picture fill_picture(void *ctx, const qa_q3_pres_scene_shader *shader) {
  (void)ctx;
  qa_material_picture picture;
  memset(&picture, 0, sizeof picture);
  picture.name = shader ? shader->name : "null-shader";
  return picture;
}
static qa_q3_pres_client_draw_icon_settings fill_icon_settings(void *ctx) {
  (void)ctx;
  qa_q3_pres_client_draw_icon_settings settings = {false, false};
  return settings;
}
static qa_cvar_snapshot fill_corner_cvar(void *ctx, qa_q3_pres_hud_corner_cvar name) {
  fill_probe *probe = ctx;
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.integer_value = probe->corner_cvars[name];
  return snapshot;
}
static qa_cvar_snapshot fill_board_cvar(void *ctx, qa_q3_pres_scoreboard_cvar name) {
  fill_probe *probe = ctx;
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.integer_value = probe->board_cvars[name];
  return snapshot;
}
static const char *fill_config(void *ctx, double index) { (void)ctx; (void)index; return ""; }
static double fill_ms(void *ctx) { return ((fill_probe *)ctx)->now_ms; }
static void fill_send(void *ctx, const char *text) {
  fill_probe *probe = ctx;
  snprintf(probe->sent, sizeof probe->sent, "%s", text);
  probe->sent_count++;
}
static void fill_print(void *ctx, const char *text) { (void)ctx; (void)text; }
static bool fill_rect_seen(fill_probe *probe, double x, double y, double w, double h, const char *name) {
  for (int i = 0; i < probe->count; i++)
    if (probe->rects[i].x == x && probe->rects[i].y == y && probe->rects[i].width == w &&
        probe->rects[i].height == h && (name == NULL || strcmp(probe->names[i], name) == 0)) return true;
  return false;
}
static void test_presentation_fill(void) {
  static qa_q3_pres_scene_shader num_shaders[11];
  static char num_names[11][8];
  for (int i = 0; i < 11; i++) { snprintf(num_names[i], 8, "num%d", i); num_shaders[i].name = num_names[i]; }
  static qa_q3_pres_scene_shader sb_score = {"sb-score"}, sb_ping = {"sb-ping"}, sb_time = {"sb-time"},
                                 sb_name = {"sb-name"}, white_shader = {"white"}, charset = {"charset"};
  qa_q3_pres_client_media_graphics graphics;
  memset(&graphics, 0, sizeof graphics);
  for (int i = 0; i < 11; i++) graphics.number_shaders[i] = &num_shaders[i];
  graphics.scoreboard_score = &sb_score;
  graphics.scoreboard_ping = &sb_ping;
  graphics.scoreboard_time = &sb_time;
  graphics.scoreboard_name = &sb_name;

  fill_probe probe;
  memset(&probe, 0, sizeof probe);
  qa_text_draw_sink sink;
  memset(&sink, 0, sizeof sink);
  sink.ctx = &probe;
  sink.target = (qa_rect){0, 0, 640, 480};
  sink.set_color = fill_set_color;
  sink.stretch_pixels = fill_stretch;
  qa_draw2d draw = {sink, QA_COORD_STRETCH_640};
  qa_q3_pres_draw_tools_media media;
  memset(&media, 0, sizeof media);
  media.picture = fill_picture;
  media.charset_shader = &charset;
  media.white_shader = &white_shader;
  qa_q3_pres_client_draw_tools tools;
  qa_q3_pres_client_draw_tools_init(&tools, draw, media);

  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  qa_q3_pres_client_game_static_state_init(&static_state, QA_Q3_BASE_PRODUCT_BASEQ3);
  static_state.maxclients = 64;
  qa_q3_pres_client_draw_icons icons;
  memset(&icons, 0, sizeof icons);
  icons.state = &state;
  icons.tools = &tools;
  icons.media.static_state = &static_state;
  icons.settings = fill_icon_settings;

  /* Snapshot with local client 0. */
  qa_q3_wire_snapshot input;
  memset(&input, 0, sizeof input);
  qa_q3_player_state_record_init(&input.player_state, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  input.area_mask_len = 32;
  qa_q3_pres_retail_snapshot snap;
  CHECK(qa_q3_pres_retail_snapshot_from_wire(&snap, &input));
  state.snap = &snap;

  /* hud-corners drawField: clamps and right-alignment (CHAR 32x48, x+2). */
  qa_q3_pres_client_hud_corners corners;
  qa_q3_pres_hud_corners_host corner_host;
  memset(&corner_host, 0, sizeof corner_host);
  corner_host.ctx = &probe;
  corner_host.read_vm_cvar = fill_corner_cvar;
  corner_host.config_string = fill_config;
  corner_host.milliseconds = fill_ms;
  corner_host.graphics = &graphics;
  qa_q3_pres_client_hud_corners_init(&corners, &state, &static_state, &icons, corner_host);
  probe.count = 0;
  qa_q3_pres_client_hud_corners_draw_field(&corners, 100, 432, 3, 5);
  CHECK(probe.count == 1 && fill_rect_seen(&probe, 166, 432, 32, 48, "num5"));
  probe.count = 0;
  qa_q3_pres_client_hud_corners_draw_field(&corners, 100, 432, 2, -123); /* clamps to -9 */
  CHECK(probe.count == 2 && fill_rect_seen(&probe, 102, 432, 32, 48, "num10") &&
        fill_rect_seen(&probe, 134, 432, 32, 48, "num9"));
  probe.count = 0;
  qa_q3_pres_client_hud_corners_draw_field(&corners, 100, 432, 1, 42); /* clamps to 9 */
  CHECK(probe.count == 1 && fill_rect_seen(&probe, 102, 432, 32, 48, "num9"));
  probe.count = 0;
  qa_q3_pres_client_hud_corners_draw_field(&corners, 100, 432, 0, 5); /* width < 1: nothing */
  CHECK(probe.count == 0);

  /* Upper-right snapshot line draws big chars at y=2. */
  probe.corner_cvars[QA_Q3_PRES_CORNER_CVAR_SNAPSHOT] = 1;
  probe.count = 0;
  qa_q3_pres_client_hud_corners_draw_upper_right(&corners);
  bool at_y2 = false;
  for (int i = 0; i < probe.count; i++) if (probe.rects[i].y == 2) at_y2 = true;
  CHECK(probe.count > 0 && at_y2);
  probe.corner_cvars[QA_Q3_PRES_CORNER_CVAR_SNAPSHOT] = 0;

  /* Scoreboard gates + rows. Board assigned directly (init needs a store). */
  qa_q3_pres_base_scoreboard board;
  memset(&board, 0, sizeof board);
  board.state = &state;
  board.static_state = &static_state;
  board.host.graphics = &graphics;
  board.host.icons = &icons;
  board.host.ctx = &probe;
  board.host.read_vm_cvar = fill_board_cvar;
  board.host.config_string = fill_config;
  board.host.send_client_command = fill_send;
  board.host.print = fill_print;
  state.time = 5000;
  /* paused: false and deferred reset. */
  probe.board_cvars[QA_Q3_PRES_SCOREBOARD_CVAR_PAUSED] = 1;
  state.deferred_player_loading = 7;
  CHECK(!qa_q3_pres_base_scoreboard_draw(&board) && state.deferred_player_loading == 0);
  probe.board_cvars[QA_Q3_PRES_SCOREBOARD_CVAR_PAUSED] = 0;
  /* warmup without +scores: false. */
  state.warmup = 1;
  state.show_scores = false;
  CHECK(!qa_q3_pres_base_scoreboard_draw(&board));
  state.warmup = 0;
  /* fade expired (200ms window): false and killer cleared. */
  state.score_fade_time = 1000;
  state.time = 2000;
  CHECK(qa_q3_pres_set_string(&state.killer_name, "Bob"));
  CHECK(!qa_q3_pres_base_scoreboard_draw(&board) && state.killer_name == NULL);
  /* Happy path: one local free-team row + header pics + deferred++. */
  CHECK(qa_q3_pres_set_string(&static_state.client_info[0].name, "Alice"));
  static_state.client_info[0].info_valid = true;
  static_state.client_info[0].team = QA_Q3_BASE_TEAM_FREE;
  static_state.client_info[0].handicap = 100;
  state.num_scores = 1;
  state.scores[0].client = 0;
  state.scores[0].score = 12;
  state.scores[0].ping = 34;
  state.scores[0].time = 56;
  state.show_scores = true;
  state.deferred_player_loading = 0;
  probe.count = 0;
  CHECK(qa_q3_pres_base_scoreboard_draw(&board));
  CHECK(state.deferred_player_loading == 1);
  CHECK(fill_rect_seen(&probe, 176, 86, 64, 32, "sb-score"));
  CHECK(fill_rect_seen(&probe, 264, 86, 64, 32, "sb-ping"));
  CHECK(fill_rect_seen(&probe, 344, 86, 64, 32, "sb-time"));
  CHECK(fill_rect_seen(&probe, 416, 86, 64, 32, "sb-name"));
  CHECK(fill_rect_seen(&probe, 176, 118, 512, 17, "white")); /* local rank highlight */

  /* Tourney board requests scores every 2s with exactly "score". */
  state.scores_request_time = 0;
  state.time = 5000;
  probe.sent_count = 0;
  probe.sent[0] = '\0';
  CHECK(qa_q3_pres_base_scoreboard_draw_tourney(&board));
  CHECK(probe.sent_count == 1 && strcmp(probe.sent, "score") == 0 && state.scores_request_time == 5000);
  CHECK(qa_q3_pres_base_scoreboard_draw_tourney(&board));
  CHECK(probe.sent_count == 1);

  state.snap = NULL;
  qa_q3_pres_retail_snapshot_destroy(&snap);
  qa_q3_pres_client_game_static_state_destroy(&static_state);
  qa_q3_pres_client_game_state_destroy(&state);
}

int main(void) {
  test_character_resources();
  test_refdef();
  test_ref_entity();
  test_model_access();
  test_cgame_body_profile();
  test_core_state_and_retail();
  test_core_client_info_and_config();
  test_core_prediction_and_pool();
  test_core_event_weapon_preparation();
  test_core_snapshots();
  test_core_resources();
  test_core_media_sounds();
  test_draw_stack_audio();
  test_draw_stack_marks();
  test_draw_stack_scene();
  test_draw_stack_tools_and_weapons();
  test_presentation_fill();
  printf("q3-presentation: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
