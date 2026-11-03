/* Port of src/app/bootstrap/simulation/q3/guest-artifact.ts. Async becomes
 * synchronous. PreparedQ3Game is one struct tagged by kind: "qvm" (PreparedQ3Qvm:
 * the resolved bytecode artifact, the declared item layout and the
 * QvmPrimaryProfile) or "native" (PreparedQ3NativeGame: the native image, the
 * base weapons and an all-null primary profile). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_ARTIFACT_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_ARTIFACT_H
#include <stdint.h>
#include "qa/app/bootstrap/simulation/q3/native_guest_source.h"
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/item_catalog.h"
#include "qa/compat/qvm/primary_profile.h"
#include "qa/content/mounts.h"
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/content/q3/guest_items.h"

typedef qa_resolved_execution_module qa_app_q3_game_execution;
/* The content q3 slices read the artifact through their own mirror type; the
 * prepared game keeps that view (instructions owned) for its whole life. */
typedef struct qa_app_q3_equipment_artifact_view {
  qa_q3_equipment_qvm_artifact artifact;
  qa_q3_equipment_known_artifact known;
  qa_q3_equipment_qvm_instruction *instructions;
} qa_app_q3_equipment_artifact_view;
/* TS PreparedQ3Game. Everything is owned except execution and resource
 * (borrowed from the recipe). module is the identity every guest call and save
 * is attributed to (artifact.module for qvm, image.image.module for native). */
typedef struct qa_app_prepared_q3_game {
  const char *kind; /* "qvm" | "native" */
  qa_module_identity module;
  qa_app_prepared_q3_native image; /* native */
  qa_app_q3_game_execution execution;
  qa_resolved_qvm_artifact artifact;
  const qa_resolved_resource_reference *resource;
  qa_q3_guest_weapon_list weapons;
  bool has_items; qa_qvm_item_layout items; /* has_items false = TS undefined */
  qa_qvm_primary_profile primary;
  qa_app_q3_equipment_artifact_view equipment;
} qa_app_prepared_q3_game;
void qa_app_prepared_q3_game_destroy(qa_app_prepared_q3_game *game);

void qa_app_assert_q3_guest_recipe(qa_executable_recipe recipe, const qa_app_q3_game_execution *execution);
/* prepareQ3Game(execution, mounts). Errors raise (TS throws); *out is owned. */
void qa_app_prepare_q3_game(qa_app_q3_game_execution execution, qa_content_mounted *mounts, qa_app_prepared_q3_game *out);
/* prepareQ3NativeGame(execution, mounts): a native qagame is a black box to the original-source bindings. execution is
 * borrowed for the prepared game's life. Raises for a Quake Live table-interface module. */
void qa_app_prepare_q3_native_game(const qa_app_q3_game_execution *execution, qa_content_mounted *mounts,
                                   qa_app_prepared_q3_game *out);
/* The bytecode artifact through the content q3 mirror type (borrowed from prepared); NULL for a native module. */
const qa_q3_equipment_qvm_artifact *qa_app_prepared_q3_equipment_artifact(const qa_app_prepared_q3_game *prepared);
/* The declared item layout through the content q3 mirror type; false = undefined. */
bool qa_app_prepared_q3_guest_layout(const qa_app_prepared_q3_game *prepared, qa_q3_guest_qvm_item_layout *out);
/* PreparedQ3Game.primary members (NULL = TS null). */
const qa_qvm_input_definition *qa_app_prepared_q3_primary_input(const qa_app_prepared_q3_game *prepared);
const qa_qvm_primary_weapon_profile *qa_app_prepared_q3_primary_weapons(const qa_app_prepared_q3_game *prepared);
const qa_qvm_primary_combat_profile *qa_app_prepared_q3_primary_combat(const qa_app_prepared_q3_game *prepared);
const qa_qvm_inventory_profile *qa_app_prepared_q3_primary_inventory(const qa_app_prepared_q3_game *prepared);
const qa_qvm_pickup_profile *qa_app_prepared_q3_primary_pickups(const qa_app_prepared_q3_game *prepared);
/* primary.declaration?.digest ?? null. */
const char *qa_app_prepared_q3_primary_declaration_digest(const qa_app_prepared_q3_game *prepared);
#endif
