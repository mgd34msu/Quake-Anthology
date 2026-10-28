/* Port of src/app/bootstrap/network/unified-components.ts.
 * ModIdentity has no landed writer: identities carry their wire
 * qa_save_value tree alongside the parsed form (read fills both;
 * write clones the tree). Player-state slot storage (64 doubles)
 * rides beside each qa_q3_player_state as the landed record codec
 * requires. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_COMPONENTS_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_COMPONENTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/unified_types.h"
#include "qa/compat/qvm/mod_presentation_checkpoint.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/presentation.h"
#include "qa/network/q3/game_state.h"
#include "qa/persistence/value.h"
typedef struct qa_app_unified_component_identity {
  qa_presentation_owner owner;
  qa_mod_identity identity;
  const qa_save_value *identity_wire; /* borrowed on write; owned clone after read */
  double generation;
  qa_qvm_abi_profile abi;    /* "q3-modern"|"q3-1.16n-base" */
  const char *runtime;       /* "qvm-scene"|"qvm-player-events" */
} qa_app_unified_component_identity;
typedef struct qa_app_unified_snapshot {
  double server_time; qa_q3_player_state player_state; double player_slots[64];
} qa_app_unified_snapshot;
typedef struct qa_app_unified_component_publication {
  qa_app_unified_component_identity identity;
  qa_actor_id viewer;
  bool has_client_number; double client_number;
  double game_state_revision;
  const qa_q3_source_game_state_record *game_state; /* borrowed */
  qa_app_unified_snapshot snapshot;
  bool has_weapon_presented; bool weapon_presented;
  const qa_qvm_scene_context *scene; /* borrowed, nullable */
  const qa_qvm_scene_actor *bindings; size_t binding_count;
} qa_app_unified_component_publication;
typedef struct qa_app_unified_component_state {
  qa_app_unified_component_identity identity;
  double game_state_revision;
  bool has_game_state; qa_q3_source_game_state_record game_state; /* owned when set */
  double command_base;
  qa_qvm_cp_scene_command *commands; size_t command_count; /* owned */
} qa_app_unified_component_state;
typedef struct qa_app_unified_component_frame_scene {
  double revision; qa_qvmy_source_snapshot snapshot;
} qa_app_unified_component_frame_scene;
typedef struct qa_app_unified_component_frame {
  qa_presentation_owner owner; double generation; qa_qvm_abi_profile abi;
  qa_actor_id viewer; double client_number, game_state_revision;
  qa_app_unified_snapshot snapshot; bool weapon_presented;
  qa_qvm_scene_actor *bindings; size_t binding_count; /* owned after read */
  bool has_scene; qa_app_unified_component_frame_scene scene;
} qa_app_unified_component_frame;
struct qa_app_unified_native_state;
struct qa_app_unified_native_frame;
typedef struct qa_app_unified_component_update {
  double revision;
  qa_app_unified_component_state *sources; size_t source_count;
  struct qa_app_unified_native_state *native; size_t native_count;
} qa_app_unified_component_update;
typedef struct qa_app_unified_component_frames {
  double revision;
  qa_app_unified_component_frame *sources; size_t source_count;
  bool has_native; struct qa_app_unified_native_frame *native; size_t native_count;
} qa_app_unified_component_frames;
qa_presentation_owner qa_app_unified_read_component_owner(const qa_save_reader *reader);
/* Owned wire trees; NULL only on OOM. */
qa_save_value *qa_app_unified_write_component_update(const qa_app_unified_component_update *update);
qa_app_unified_component_update qa_app_unified_read_component_update(const qa_save_reader *reader);
void qa_app_unified_component_update_free(qa_app_unified_component_update *update);
qa_save_value *qa_app_unified_write_component_frames(const qa_app_unified_component_frames *frames);
qa_app_unified_component_frames qa_app_unified_read_component_frames(
    const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
void qa_app_unified_component_frames_free(qa_app_unified_component_frames *frames);
#endif
