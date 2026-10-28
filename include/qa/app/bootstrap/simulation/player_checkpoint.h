/* Port of src/app/bootstrap/simulation/player-checkpoint.ts.
 * captureMovementPlayer returns an owned qa_save_value tree (the TS
 * plain checkpoint object). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PLAYER_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_PLAYER_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/content/q1/base/travel.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q3/foundation/character.h"
#include "qa/persistence/value.h"
typedef qa_actor_id (*qa_app_sim_actor_reference_fn)(void *ctx, qa_saved_actor_id saved);
/* Owned save tree for the hit; caller frees. */
qa_save_value *qa_app_sim_save_hit(qa_trace_hit hit);
qa_trace_hit qa_app_sim_read_hit(const qa_save_reader *reader, qa_app_sim_actor_reference_fn reference, void *reference_ctx);
qa_animation_state qa_app_sim_read_animation(const qa_save_reader *reader);
/* Readonly view of the MovementPlayer fields capture reads. */
qa_save_value *qa_app_sim_capture_movement_player(const qa_app_sim_movement_player *player);
typedef struct qa_app_sim_saved_movement_player {
  bool has_character; qa_provider_reference character_definition;
  bool has_character_appearance; qa_provider_reference character_appearance;
  bool has_body_shape_base; qa_bounds body_shape_base;
  bool fixed_pose_active;
  bool has_arsenal_intent; qa_arsenal_intent arsenal_intent;
  bool has_netquake_command; qa_q1_user_command netquake_command;
  bool flight;
  qa_saved_actor_id actor; double client_slot;
  qa_movement_state state; qa_arsenal_state arsenal; qa_actor_animation_state animation;
  qa_vec3 view_angles, command_angles; double view_height; qa_bounds bounds; qa_trace_hit ground;
  double water_level, water_type; bool intermission;
  double gravity_multiplier, world_gravity, buttons, previous_buttons, last_sequence, last_weapon_seconds;
  bool has_cutscene; qa_vec3 cutscene_origin, cutscene_angles, cutscene_view_offset;
  qa_inventory_entry *ammo; /* owned copy of arsenal ammo; caller frees */
} qa_app_sim_saved_movement_player;
qa_app_sim_saved_movement_player qa_app_sim_read_movement_player(const qa_save_reader *reader, qa_app_sim_actor_reference_fn reference, void *reference_ctx);
qa_q3_character_checkpoint qa_app_sim_read_q3_character(const qa_save_reader *reader);
/* inventory/extensions malloc'd; free with qa_q1_travel_state_destroy. */
qa_q1_travel_state qa_app_sim_read_q1_travel(const qa_save_reader *reader);
qa_q2_base_player_view qa_app_sim_read_q2_view(const qa_save_reader *reader);
#endif
