/*
 * Port of src/persistence/q2-players.ts. Player state, carry, rules, and character checkpoints. Armor and
 * inventory readers are local copies of save-image.ts readArmor/
 * readInventoryEntry (that module is not ported yet).
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_PLAYERS_H
#define QA_PERSISTENCE_Q2_PLAYERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_player_landmark {
  qa_saved_actor_id player;
  const char *name;
  qa_vec3 relative_origin, relative_velocity, relative_view_angles;
} qa_q2_player_landmark;
/* TS Q2PlayerIntermissionCheckpoint union on "kind". */
typedef struct qa_q2_player_intermission_checkpoint {
  const char *kind; /* "playing" | "intermission" */
  const char *map; double started; bool exit; /* intermission */
  bool has_landmark; qa_q2_player_landmark landmark; /* intermission, nullable */
} qa_q2_player_intermission_checkpoint;

typedef struct qa_q2_player_carry {
  double health;
  double maximum_health;
  qa_armor_state armor;
  qa_inventory_entry *inventory; size_t inventory_count;
  const char *weapon;
  const char *selected_item;
  double score;
  double flags;
  double power_cubes;
} qa_q2_player_carry;

typedef struct qa_q2_player_rules {
  const char *password;
  const char *spectator_password;
  double max_spectators;
  bool cheats;
  double time_limit_minutes;
  double frag_limit;
  const char * *map_list; size_t map_list_count;
  bool map_list_shuffle;
  const char *next_map;
  const char *spawn_point;
  double flood_messages;
  double flood_seconds;
  double flood_wait_seconds;
  double roll_speed;
  double roll_angle;
  double run_pitch;
  double run_roll;
  double bob_up;
  double bob_pitch;
  double bob_roll;
  qa_vec3 gun_offset;
} qa_q2_player_rules;

typedef struct qa_q2_player_state_checkpoint {
  double slot;
  double entered_at;
  bool use_q2_weapons;
  bool use_q2_inventory;
  qa_inventory_entry *spawn_inventory; size_t spawn_inventory_count;
  const char *userinfo;
  const char *name;
  const char *skin;
  const char *gender;
  double fov;
  const char *hand;
  bool spectator;
  bool requested_spectator;
  bool connected;
  bool dead;
  bool gibbed;
  bool noclip;
  bool god;
  bool notarget;
  double score;
  double ping;
  double respawn_time;
  double air_finished;
  double next_drown_time;
  double drown_damage;
  double old_water_level;
  double breather_sound;
  double pain_debounce;
  double damage_blood;
  double damage_armor;
  double damage_power_armor;
  double damage_knockback;
  qa_vec3 damage_from;
  qa_vec3 damage_blend;
  double damage_alpha;
  double bonus_alpha;
  double damage_pitch;
  double damage_roll;
  double damage_time;
  double power_armor_time;
  double fall_time;
  double fall_value;
  bool landmark_free_fall;
  double landmark_noise_time;
  qa_vec3 old_velocity;
  qa_vec3 old_view_angles;
  double killer_yaw;
  double buttons;
  double latched_buttons;
  bool weapon_thunk;
  double bob_time;
  double bob_move;
  const char *event;
  double animation_priority;
  double animation_end;
  bool animation_duck;
  bool animation_run;
  const char *loop_sound;
  const char *selected_item;
  bool show_scores;
  bool show_inventory;
  bool show_help;
  bool has_chase_target;
  qa_saved_actor_id chase_target;
  bool has_coop_respawn;
  qa_q2_player_carry coop_respawn;
  double *flood_times; size_t flood_times_count;
  double flood_lock_until;
} qa_q2_player_state_checkpoint;

typedef struct qa_q2_player_entry {
  qa_saved_actor_id actor;
  qa_q2_player_state_checkpoint state;
} qa_q2_player_entry;

typedef struct qa_q2_players_checkpoint {
  double version;
  double corpse_index;
  double death_animation;
  double pain_animation;
  qa_q2_player_rules rules;
  qa_q2_player_intermission_checkpoint intermission;
  qa_q2_player_entry *players; size_t players_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_players_checkpoint;

typedef struct qa_q2_character_entity {
  const char *model;
  const char *model2;
  const char *model3;
  const char *model4;
  double skin;
  double frame;
  double old_frame;
  double scale;
  double effects;
  double render_flags;
  double flags;
  double server_flags;
  double view_height;
  double max_health;
  const char *sound;
  bool visible;
} qa_q2_character_entity;

typedef struct qa_q2_character_checkpoint {
  double version;
  double pain_index;
  double death_index;
  qa_q2_player_state_checkpoint state;
  qa_q2_player_rules rules;
  bool has_last_attack;
  qa_q2_attack_checkpoint last_attack;
  qa_q2_character_entity entity;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_character_checkpoint;

qa_q2_player_state_checkpoint qa_read_q2_player_state_checkpoint(const qa_save_reader *reader);
qa_q2_players_checkpoint qa_read_q2_players_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_players_checkpoint(const qa_q2_players_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_players_checkpoint *out);
qa_q2_character_checkpoint qa_read_q2_character_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_character_checkpoint(const qa_q2_character_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_character_checkpoint(const uint8_t *bytes, size_t length, qa_q2_character_checkpoint *out);

#endif
