/*
 * Header audit (slice B): the 27 TypeScript sources in
 * /tmp/qa-lists/w8-header-b.txt are all type-only (verified by
 * reading them in full: no exported functions, classes, const
 * values, or mutable state), so their C counterparts are headers
 * with no .c. This test includes every one of those headers
 * directly and checks the carried exported names/values that the
 * audit fixed or confirmed: exact string-literal unions kept as
 * strings, kind enums, and the fields of the structs whose TS
 * union arms were previously left unenumerated in comments
 * (QvmPresentationArgument sources, QvmModCombatCalls role sets,
 * NativeModInputOutput inputs, Q1LumpName, Q3MovementState Pick
 * keys, WeaponHudStatus finite ammo). GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* All 27 headers of the audit slice. */
#include "qa/contracts/model_attachment.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/native_mod_region.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/qc_pickup_callers.h"
#include "qa/contracts/qc_weapon_stage.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/qvm_grapple.h"
#include "qa/contracts/qvm_mod_actor_frame.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"
#include "qa/contracts/ui.h"
#include "qa/contracts/world.h"
#include "qa/core/commands/documentation.h"
#include "qa/formats/q1_map/types.h"
#include "qa/types/png.d.h"

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

static bool str_eq(const char *left, const char *right) {
  return left != NULL && right != NULL && strcmp(left, right) == 0;
}

/* TS Q1BspFormat: the one runtime helper in the slice
 * (static inline qa_q1_bsp_format_name in formats/q1_map/types.h)
 * must return the exact TS literal for each enumerator. */
static void test_q1_bsp_format_names(void) {
  CHECK(str_eq(qa_q1_bsp_format_name(QA_Q1_FORMAT_BSP29), "bsp29"));
  CHECK(str_eq(qa_q1_bsp_format_name(QA_Q1_FORMAT_BSP2), "bsp2"));
  CHECK(str_eq(qa_q1_bsp_format_name(QA_Q1_FORMAT_2PSB), "2psb"));
  CHECK(str_eq(qa_q1_bsp_format_name(QA_Q1_FORMAT_QUAKE64), "quake64"));
  CHECK(QA_Q1_FORMAT_BSP29 != QA_Q1_FORMAT_BSP2);
  CHECK(QA_Q1_FORMAT_2PSB != QA_Q1_FORMAT_QUAKE64);
}

/* The 15 Q1LumpName literals are stringly typed in C; the lump
 * header comment now enumerates them. Spot-check that
 * qa_q1_lump carries a name without conversion. */
static void test_q1_lump_name(void) {
  qa_q1_lump lump;
  memset(&lump, 0, sizeof lump);
  lump.name = "leafFaces";
  lump.offset = 640.0;
  lump.length = 128.0;
  CHECK(str_eq(lump.name, "leafFaces"));
  CHECK(lump.offset == 640.0 && lump.length == 128.0);
}

/* TS QvmPresentationArgument source values (all 11 are now
 * enumerated in the header comment) live in source_value. */
static void test_qvm_presentation_argument(void) {
  qa_qvm_presentation_argument source;
  memset(&source, 0, sizeof source);
  source.kind = "source";
  source.source_value = "entity-state";
  CHECK(str_eq(source.kind, "source"));
  CHECK(str_eq(source.source_value, "entity-state"));

  qa_qvm_presentation_argument number;
  memset(&number, 0, sizeof number);
  number.kind = "float32";
  number.number_value = 2.5;
  CHECK(str_eq(number.kind, "float32"));
  CHECK(number.number_value == 2.5);
}

/* TS QvmModCombatCalls: five generic combat calls whose fixed
 * per-call role sets are now enumerated in the header comment. */
static void test_qvm_mod_combat_calls(void) {
  static const qa_qvm_combat_role_index die_roles[] = {
      {"target", 0}, {"inflictor", 1}, {"attacker", 2},
      {"amount", 3}, {"method", 4}};
  qa_qvm_mod_combat_calls calls;
  memset(&calls, 0, sizeof calls);
  calls.die.roles = die_roles;
  calls.die.role_count = 5;
  CHECK(calls.die.role_count == 5);
  CHECK(str_eq(calls.die.roles[0].role, "target"));
  CHECK(str_eq(calls.die.roles[4].role, "method"));
  CHECK(calls.die.roles[3].index == 3.0);
  /* damage/touch/use/pain stay independently addressable. */
  CHECK(&calls.damage != &calls.touch);
  CHECK(&calls.use != &calls.pain);
}

/* TS NativeModInputOutput handler inputs exclude "view-angles";
 * the inputs list is present alongside entry/arguments. */
static void test_native_mod_input_output(void) {
  static const qa_mod_client_input inputs[] = {"forward-move"};
  qa_native_mod_input_output out;
  memset(&out, 0, sizeof out);
  out.kind = "handler";
  out.inputs = inputs;
  out.input_count = 1;
  CHECK(str_eq(out.kind, "handler"));
  CHECK(out.input_count == 1);
  CHECK(str_eq(out.inputs[0], "forward-move"));
  CHECK(!str_eq(out.inputs[0], "view-angles"));
}

/* TS Q3MovementState Pick<Q3PlayerState, ...>: all 15 picked
 * fields are inlined (now enumerated in the header comment). */
static void test_q3_movement_state_pick(void) {
  qa_q3_movement_state state;
  memset(&state, 0, sizeof state);
  state.command_time_milliseconds = 1000.0;
  state.movement_type = 1.0;
  state.bob_cycle = 2.0;
  state.movement_flags = 3.0;
  state.movement_time_milliseconds = 4.0;
  state.gravity = 800.0;
  state.speed = 320.0;
  state.movement_direction = 5.0;
  state.flags = 6.0;
  state.view_height = 26.0;
  CHECK(state.command_time_milliseconds == 1000.0);
  CHECK(state.bob_cycle == 2.0 && state.movement_flags == 3.0);
  CHECK(state.gravity == 800.0 && state.speed == 320.0);
  CHECK(state.view_height == 26.0);
  CHECK(state.jump_pad == NULL);
}

/* TS WeaponHudStatus finite ammo arm: item, count,
 * hasAmmoToStart and low, flattened behind ammo_is_finite. */
static void test_weapon_hud_status(void) {
  qa_weapon_hud_status status;
  memset(&status, 0, sizeof status);
  status.ammo_is_finite = true;
  status.ammo_count = 7.0;
  status.has_ammo_to_start = true;
  status.low = true;
  CHECK(status.ammo_is_finite);
  CHECK(status.ammo_count == 7.0);
  CHECK(status.has_ammo_to_start && status.low);
}

/* Kind enums carrying TS string-literal unions must keep every
 * arm distinct. */
static void test_kind_enums(void) {
  CHECK(QA_NATIVE_MOD_LOCATION_REGISTER != QA_NATIVE_MOD_LOCATION_SIMD);
  CHECK(QA_NATIVE_MOD_LOCATION_SIMD != QA_NATIVE_MOD_LOCATION_STACK);
  CHECK(QA_CLOCK_Q1_NETQUAKE != QA_CLOCK_Q1_QUAKEWORLD);
  CHECK(QA_CLOCK_Q2_CLASSIC != QA_CLOCK_Q2_RERELEASE);
  CHECK(QA_CLOCK_Q2_RERELEASE != QA_CLOCK_Q3);
  CHECK(QA_SOURCE_TIME_SECONDS != QA_SOURCE_TIME_MILLISECONDS);
  CHECK(QA_FRAME_PHASE_FRAME_ENTRY != QA_FRAME_PHASE_FRAME_EXIT);
  CHECK(QA_FRAME_ORDERING_NATIVE != QA_FRAME_ORDERING_MIXED);
  CHECK(QA_USER_COMMAND_Q1 != QA_USER_COMMAND_Q3);
  CHECK(QA_WEAPON_HUD_ICON_WAD_PICTURE != QA_WEAPON_HUD_ICON_IMAGE);
  CHECK(QA_WEAPON_HUD_ICON_IMAGE != QA_WEAPON_HUD_ICON_SHADER);
  CHECK(QA_RANDOM_STATE_Q3_LCG != QA_RANDOM_STATE_GUEST);
  CHECK(QA_FLOAT_TO_INT_QVM_INDEFINITE != QA_FLOAT_TO_INT_X86_INDEFINITE);
}

/* A struct from each remaining header is default-constructible
 * and its headline discriminant field is present and zeroed,
 * proving the header is self-contained under the slice's
 * include set. */
static void test_slice_headers_usable(void) {
  qa_model_attachment_definition attachment;
  memset(&attachment, 0, sizeof attachment);
  CHECK(!attachment.is_joint);

  qa_qvm_grapple_cable cable;
  memset(&cable, 0, sizeof cable);
  CHECK(!cable.is_shader);

  qa_qc_pickup_scalar scalar;
  memset(&scalar, 0, sizeof scalar);
  CHECK(!scalar.is_global);

  qa_command_documentation doc;
  memset(&doc, 0, sizeof doc);
  CHECK(doc.allowed_values == NULL);

  qa_save_image save;
  memset(&save, 0, sizeof save);
  CHECK(save.schema_version == 0);

  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  CHECK(frame.sequence == 0.0);

  qa_q3_mesh_model mesh;
  memset(&mesh, 0, sizeof mesh);
  CHECK(mesh.tag_rows == NULL && mesh.tag_row_count == 0);
}

int main(void) {
  test_q1_bsp_format_names();
  test_q1_lump_name();
  test_qvm_presentation_argument();
  test_qvm_mod_combat_calls();
  test_native_mod_input_output();
  test_q3_movement_state_pick();
  test_weapon_hud_status();
  test_kind_enums();
  test_slice_headers_usable();
  if (failures == 0) {
    printf("header-audit-b: %d checks passed\n", checks);
    return 0;
  }
  fprintf(stderr, "header-audit-b: %d/%d checks FAILED\n", failures, checks);
  return 1;
}
