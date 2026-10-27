/* Port of src/content/q2/base/player/checkpoint.ts. GPL-2.0-or-later.
 * The state capture/restore helpers are the structuredClone/Object
 * .assign conversions the TS performs inline in players/character. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_CHECKPOINT_H
#define QA_CONTENT_Q2_BASE_PLAYER_CHECKPOINT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/contracts/session.h"

/** TS Q2PlayerStateCheckpoint (state with a saved chase target).
 * Arrays in a captured value are malloc'd copies; free them with
 * qa_q2_base_player_state_checkpoint_clear. The mutable identity
 * strings are copied (TS structuredClone); the remaining strings are
 * static spellings. */
typedef struct qa_q2_base_player_state_checkpoint {
  double slot, entered_at;
  bool use_q2_weapons, use_q2_inventory;
  qa_inventory_entry *spawn_inventory; size_t spawn_inventory_count;
  char userinfo[512], name[16], skin[512];
  const char *gender;
  double fov;
  const char *hand;
  bool spectator, requested_spectator, connected, dead, gibbed, noclip, god, notarget;
  double score, ping, respawn_time, air_finished, next_drown_time, drown_damage;
  double old_water_level, breather_sound, pain_debounce;
  double damage_blood, damage_armor, damage_power_armor, damage_knockback;
  qa_vec3 damage_from, damage_blend;
  double damage_alpha, bonus_alpha, damage_pitch, damage_roll, damage_time;
  double power_armor_time, fall_time, fall_value;
  bool landmark_free_fall;
  double landmark_noise_time;
  qa_vec3 old_velocity, old_view_angles;
  double killer_yaw, buttons, latched_buttons;
  bool weapon_thunk;
  double bob_time, bob_move;
  const char *event;
  double animation_priority, animation_end;
  bool animation_duck, animation_run;
  const char *loop_sound;
  qa_item_id selected_item;
  bool show_scores, show_inventory, show_help;
  bool has_chase_target; qa_saved_actor_id chase_target;
  bool has_coop_respawn; qa_q2_base_player_carry coop_respawn;
  double *flood_times; size_t flood_time_count;
  double flood_lock_until;
} qa_q2_base_player_state_checkpoint;

void qa_q2_base_player_state_checkpoint_capture(const qa_q2_base_player_state *state, qa_q2_base_player_state_checkpoint *out);
void qa_q2_base_player_state_checkpoint_clear(qa_q2_base_player_state_checkpoint *checkpoint);
/* Restores every field except chase/coop arrays' ownership transfer:
 * arrays are deep-copied into the state. resolve maps the saved
 * chase target (called only when has_chase_target). */
void qa_q2_base_player_state_checkpoint_restore(qa_q2_base_player_state *state,
    const qa_q2_base_player_state_checkpoint *checkpoint,
    qa_actor_id (*resolve)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx);

/** TS Q2PlayerIntermissionCheckpoint landmark (saved player). */
typedef struct qa_q2_base_player_saved_landmark {
  qa_saved_actor_id player;
  const char *name;
  qa_vec3 relative_origin, relative_velocity, relative_view_angles;
} qa_q2_base_player_saved_landmark;
/** TS Q2PlayerIntermissionCheckpoint union on kind. */
typedef struct qa_q2_base_player_intermission_checkpoint {
  const char *kind; /* "playing"|"intermission" */
  const char *map; double started; bool exit;
  bool has_landmark; qa_q2_base_player_saved_landmark landmark;
} qa_q2_base_player_intermission_checkpoint;

typedef struct qa_q2_base_player_checkpoint_entry {
  qa_saved_actor_id actor;
  qa_q2_base_player_state_checkpoint state;
} qa_q2_base_player_checkpoint_entry;

/** TS Q2PlayersCheckpoint. The players array is malloc'd by capture. */
typedef struct qa_q2_base_players_checkpoint {
  double version; /* 1 */
  double corpse_index, death_animation, pain_animation;
  qa_q2_base_player_rules rules;
  qa_q2_base_player_intermission_checkpoint intermission;
  qa_q2_base_player_checkpoint_entry *players; size_t player_count;
} qa_q2_base_players_checkpoint;

/** TS character checkpoint entity Pick<>. */
typedef struct qa_q2_base_character_entity_checkpoint {
  const char *model, *model2, *model3, *model4;
  double skin, frame, old_frame, scale, effects, render_flags, flags;
  double server_flags, view_height, max_health;
  const char *sound;
  bool visible;
} qa_q2_base_character_entity_checkpoint;

/** TS Q2CharacterCheckpoint. */
typedef struct qa_q2_base_character_checkpoint {
  double version; /* 1 */
  double pain_index, death_index;
  qa_q2_base_player_state_checkpoint state;
  qa_q2_base_player_rules rules;
  qa_q2_base_character_entity_checkpoint entity;
  bool has_last_attack;
  qa_q2_attack_checkpoint last_attack;
} qa_q2_base_character_checkpoint;

#endif
