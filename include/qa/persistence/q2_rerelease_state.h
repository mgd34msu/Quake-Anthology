/*
 * Port of src/persistence/q2-rerelease-state.ts. Rerelease fog, player, module/campaign, and Q64 checkpoints.
 * Fog/player struct names carry a _settings/_saved_ infix because
 * qa_q2_height_fog and qa_q2_rerelease_player_state already name
 * different contract shapes (render.h, protocol.h).
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_RERELEASE_STATE_H
#define QA_PERSISTENCE_Q2_RERELEASE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_fog_settings {
  double density;
  qa_vec3 color;
  double sky_factor;
} qa_q2_fog_settings;

typedef struct qa_q2_height_fog_settings {
  qa_vec3 start_color;
  double start_distance;
  qa_vec3 end_color;
  double end_distance;
  double falloff;
  double density;
} qa_q2_height_fog_settings;

typedef struct qa_q2_fog_state {
  qa_q2_fog_settings fog;
  qa_q2_height_fog_settings height_fog;
} qa_q2_fog_state;

typedef struct qa_q2_pending_landmark {
  const char *name;
  qa_vec3 relative_origin;
  qa_vec3 relative_velocity;
  qa_vec3 relative_view_angles;
} qa_q2_pending_landmark;

typedef struct qa_q2_rerelease_saved_player {
  bool spawned;
  double game_help1_changed;
  double game_help2_changed;
  double help_changed;
  double help_time;
  double seat;
  const char *social_id;
  double auto_switch;
  double auto_shield;
  const char *dogtag;
  double slime_debounce;
  double wind_sound_time;
  double animation_time;
  double flash_time;
  double flashes;
  double last_damage_until;
  double last_firing_until;
  double lives;
  double invisibility_until;
  double invisibility_fade_until;
  const char *coop_respawn_state;
  bool flashlight;
  qa_q2_fog_state fog;
  qa_q2_fog_state wanted_fog;
  double fog_transition;
  bool bob_skip;
  double impact_delta;
  bool on_ladder;
  double grapple_released_until;
  bool grapple_attached;
  bool awaiting_respawn;
  double respawn_timeout;
  bool has_pending_landmark;
  qa_q2_pending_landmark pending_landmark;
  qa_vec3 slow_view_angles;
  double quake_time;
  qa_vec3 help_location;
  const char *help_image;
  qa_vec3 *help_points; size_t help_points_count;
  double help_index;
  double help_draw_time;
  double help_marker_until;
} qa_q2_rerelease_saved_player;

typedef struct qa_q2_rerelease_options {
  bool coop_squad_respawn;
  bool coop_instanced_items;
  bool coop_lives;
  double coop_num_lives;
  bool deathmatch_force_respawn;
  bool deathmatch_no_fall_damage;
  bool deathmatch_allow_exit;
  bool deathmatch_spawn_farthest;
  double deathmatch_force_respawn_time;
  bool coop_player_collision;
  double auto_save_minimum_time;
} qa_q2_rerelease_options;

typedef struct qa_q2_intermission_camera {
  qa_vec3 origin;
  qa_vec3 angles;
} qa_q2_intermission_camera;

typedef struct qa_q2_rerelease_player_entry {
  qa_saved_actor_id actor;
  qa_q2_rerelease_saved_player state;
} qa_q2_rerelease_player_entry;

typedef struct qa_q2_squad_spawn {
  qa_saved_actor_id actor;
  qa_vec3 origin;
  qa_vec3 angles;
} qa_q2_squad_spawn;

typedef struct qa_q2_rerelease_players_checkpoint {
  double version;
  qa_q2_rerelease_options options;
  double coop_restart_time;
  double intermission_flags;
  bool has_intermission_fade_until;
  double intermission_fade_until;
  bool has_intermission_camera;
  qa_q2_intermission_camera intermission_camera;
  bool intermission_camera_set;
  bool deadly_kill_box;
  qa_q2_rerelease_player_entry *players; size_t players_count;
  qa_q2_squad_spawn *squad_spawns; size_t squad_spawns_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_rerelease_players_checkpoint;

typedef struct qa_q2_rerelease_sky {
  const char *name;
  double rotation;
  bool auto_rotate;
  qa_vec3 axis;
} qa_q2_rerelease_sky;

typedef struct qa_q2_rerelease_poi {
  qa_saved_actor_id actor;
  qa_vec3 origin;
  const char *image;
  bool has_dynamic;
  qa_saved_actor_id dynamic;
} qa_q2_rerelease_poi;

typedef struct qa_q2_rerelease_goals {
  const char *goals;
  double goal_number;
} qa_q2_rerelease_goals;

typedef struct qa_q2_campaign_mission {
  const char *primary;
  const char *secondary;
  double primary_changes;
  double secondary_changes;
} qa_q2_campaign_mission;

typedef struct qa_q2_campaign_level {
  const char *map;
  const char *name;
  double visit_order;
  double total_secrets;
  double found_secrets;
  double total_monsters;
  double killed_monsters;
  double time;
} qa_q2_campaign_level;

typedef struct qa_q2_campaign {
  qa_q2_campaign_mission mission;
  double cross_unit_flags;
  const char * *visited_maps; size_t visited_maps_count;
  qa_q2_campaign_level *levels; size_t levels_count;
} qa_q2_campaign;

typedef struct qa_q2_q64_eye_state {
  qa_vec3 neutral_angles;
  qa_vec3 eye_position;
  double vision_cone;
} qa_q2_q64_eye_state;

typedef struct qa_q2_q64_eye_entry {
  qa_saved_actor_id actor;
  qa_q2_q64_eye_state state;
} qa_q2_q64_eye_entry;

typedef struct qa_q2_q64_camera_state {
  double remaining;
  double distance;
  double speed;
  qa_vec3 angles;
} qa_q2_q64_camera_state;

typedef struct qa_q2_q64_camera_entry {
  qa_saved_actor_id actor;
  qa_q2_q64_camera_state state;
} qa_q2_q64_camera_entry;

typedef struct qa_q2_q64_dummy_state {
  double fade_remaining;
  double fade_duration;
  bool fading;
} qa_q2_q64_dummy_state;

typedef struct qa_q2_q64_dummy_entry {
  qa_saved_actor_id actor;
  qa_q2_q64_dummy_state state;
} qa_q2_q64_dummy_entry;

typedef struct qa_q2_rerelease_q64_checkpoint {
  qa_q2_q64_eye_entry *eyes; size_t eyes_count;
  qa_q2_q64_camera_entry *cameras; size_t cameras_count;
  qa_q2_q64_dummy_entry *dummies; size_t dummies_count;
} qa_q2_rerelease_q64_checkpoint;

typedef struct qa_q2_light_entry {
  qa_saved_actor_id actor;
  bool active;
} qa_q2_light_entry;

typedef struct qa_q2_health_bar {
  qa_saved_actor_id controller;
  qa_saved_actor_id target;
  bool has_dead_until;
  double dead_until;
} qa_q2_health_bar;

typedef struct qa_q2_health_bar_opt { bool has; qa_q2_health_bar value; } qa_q2_health_bar_opt;

typedef struct qa_q2_health_target_entry {
  qa_saved_actor_id controller;
  qa_saved_actor_id target;
} qa_q2_health_target_entry;

typedef struct qa_q2_picked_up_by_entry {
  qa_saved_actor_id actor;
  double *slots; size_t slots_count;
} qa_q2_picked_up_by_entry;

typedef struct qa_q2_trigger_sound_time {
  qa_saved_actor_id actor;
  double time;
} qa_q2_trigger_sound_time;

typedef struct qa_q2_rerelease_module_checkpoint {
  double version;
  qa_q2_fog_state world_fog;
  const char *story;
  qa_q2_rerelease_sky sky;
  bool has_poi;
  qa_q2_rerelease_poi poi;
  double poi_stage;
  double last_auto_save;
  qa_q2_rerelease_goals goals;
  qa_q2_campaign campaign;
  qa_q2_rerelease_q64_checkpoint q64;
  qa_q2_light_entry *lights; size_t lights_count;
  qa_q2_health_bar_opt *health_bars; size_t health_bars_count;
  qa_q2_health_target_entry *health_targets; size_t health_targets_count;
  qa_q2_picked_up_by_entry *picked_up_by; size_t picked_up_by_count;
  qa_q2_trigger_sound_time *trigger_sound_times; size_t trigger_sound_times_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_rerelease_module_checkpoint;

qa_q2_fog_state qa_read_q2_fog_state(const qa_save_reader *reader);
qa_q2_rerelease_players_checkpoint qa_read_q2_rerelease_players_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_rerelease_players_checkpoint(const qa_q2_rerelease_players_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_rerelease_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_players_checkpoint *out);
qa_q2_rerelease_q64_checkpoint qa_read_q2_rerelease_q64_checkpoint(const qa_save_reader *reader);
qa_q2_rerelease_module_checkpoint qa_read_q2_rerelease_module_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_rerelease_module_checkpoint(const qa_q2_rerelease_module_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_rerelease_module_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_module_checkpoint *out);

#endif
