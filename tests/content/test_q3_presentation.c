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
  printf("q3-presentation: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
