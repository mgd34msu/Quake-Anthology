/* Port of src/app/bootstrap/simulation/types.ts (types only).
 * Unlanded complex collaborators (ApplicationWorld, prepared guests,
 * navigation, travel payloads) are opaque pointers; landed contract
 * types are used directly. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_TYPES_H
#define QA_APP_BOOTSTRAP_SIMULATION_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/flare.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/ui.h"
#include "qa/materials/legacy_fog.h"

typedef struct qa_app_sim_shared_simulation qa_app_sim_shared_simulation;
typedef struct qa_app_sim_cvar_name_value { const char *name; const char *value; } qa_app_sim_cvar_name_value;

typedef struct qa_app_sim_native_q2_guest_options {
  void *capabilities; /* WindowsCapabilities (opaque) */
  void *print_ctx; void (*print)(void *ctx, const char *text);
  void *cmd_ctx; void (*add_command)(void *ctx, const char *text);
  void *graph_ctx; void (*debug_graph)(void *ctx, double value, double color);
  const char *edition; /* "classic"|"rerelease" */
  const void *prepared; /* PreparedClassicGuest|PreparedRereleaseGuest */
  const void *localize; const void *clipboard; const void *semantic_bindings;
} qa_app_sim_native_q2_guest_options;

typedef struct qa_app_sim_travel_player qa_app_sim_travel_player;
typedef struct qa_app_simulation_travel {
  const char *spawn_point;
  const char *source_kind; /* "q1"|"netquake"|"quakeworld"|"q2"|"shared" */
  double flags; double skill; double server_flags;
  const void *lmctf; const void *rerelease;
  bool has_landmark; double landmark_client_slot; const char *landmark_name;
  qa_vec3 landmark_relative_origin, landmark_relative_velocity, landmark_relative_view_angles;
  const qa_app_sim_travel_player *players; size_t player_count;
} qa_app_simulation_travel;
struct qa_app_sim_travel_player {
  qa_client_id client;
  bool has_weapon_slot; qa_provider_id weapon_provider; qa_item_id weapon_item;
  const void *hand_grenades; const void *selected_arsenal; const char *selected_arsenal_kind;
  const char *state_kind; const void *carry;
  double health; qa_armor_state armor;
  const qa_inventory_entry *inventory; size_t inventory_count;
};
typedef struct qa_app_sim_quakec_client_travel {
  qa_client_id client; const double *parameters; size_t parameter_count;
  const qa_app_sim_cvar_name_value *userinfo; size_t userinfo_count;
  const char *role; /* "player"|"spectator"|NULL */
} qa_app_sim_quakec_client_travel;
typedef struct qa_app_sim_quakec_source_travel {
  const char *kind; /* "netquake"|"quakeworld" */
  double server_flags;
  const qa_app_sim_cvar_name_value *cvars; size_t cvar_count;
  const qa_app_sim_quakec_client_travel *clients; size_t client_count;
} qa_app_sim_quakec_source_travel;

typedef struct qa_app_simulation_options {
  const void *prepared_qvm_grapple;
  const void *const *prepared_mods; size_t prepared_mod_count;
  const void *mod_commands; const void *mod_files;
  const void *enabled_mods; const void *mod_travel;
  void *nav_ctx; const void *(*prepare_rerelease_navigation)(void *ctx, qa_app_sim_shared_simulation *sim);
  const void *weapon_behavior_real_time; const void *weapon_behavior_clock;
  const void *const *weapon_behaviors; size_t weapon_behavior_count;
  bool dedicated;
  void *prompt_ctx; bool (*prompt_supported)(void *ctx, qa_client_id client);
  const void *prepared_quakec;
  bool has_q2_guest; qa_app_sim_native_q2_guest_options q2_guest;
  const void *native_q2_travel;
  bool original_save_candidate;
  const char *start_items;
  bool has_player_colors; double player_color_top, player_color_bottom;
  const char *initial_spawn_point; const char *q2_next_server;
  const void *q3_guest;
  qa_identity_owner identity;
  qa_executable_recipe recipe;
  const void *world; const void *mounts;
  double skill; /* 0..3 */
  const char *mode; /* "singleplayer"|"coop"|"deathmatch" */
  double seed; double max_clients;
  bool has_travel; qa_app_simulation_travel travel;
  void *identity_ctx; bool (*player_identity)(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id);
  const void *q3_session; const void *server_profile; void *source_registry;
  const void *source_archive; size_t source_archive_count;
  const qa_app_sim_cvar_name_value *q2_cvars; size_t q2_cvar_count;
  const qa_app_sim_cvar_name_value *q3_cvars; size_t q3_cvar_count;
  bool has_initial_source_milliseconds; double initial_source_milliseconds;
  const void *monster_navigation;
  const qa_save_image *restore;
  const qa_client_id *restored_clients; size_t restored_client_count;
} qa_app_simulation_options;

typedef struct qa_app_sim_player_admission { qa_actor_id actor; double view_height; } qa_app_sim_player_admission;
typedef struct qa_app_sim_player_view {
  bool has_client_view_offset_delta; qa_vec3 client_view_offset_delta;
  bool has_blend; qa_vec4 blend;
  bool has_damage_blend; qa_vec4 damage_blend;
  qa_vec3 origin, angles; double view_height;
  bool has_kick_angles; qa_vec3 kick_angles;
  bool has_field_of_view; double field_of_view;
  bool foreign_character_death;
  bool has_pitch_drift; bool pitch_drift_grounded; double pitch_drift_ideal_pitch; bool pitch_drift_disabled;
} qa_app_sim_player_view;
typedef struct qa_app_sim_player_ui_item {
  qa_item_id id; const char *label; const char *kind; /* "weapon"|"powerup" */
  double source_ordinal; bool owned; bool has_ammo;
  bool has_count; double count; double warning_count;
} qa_app_sim_player_ui_item;
typedef struct qa_app_sim_player_ui {
  bool selected_arsenal; const void *native_inventory;
  const qa_active_powerup_timer *powerups; size_t powerup_count;
  const qa_weapon_hud_status *weapon_status; /* NULL = TS null */
  qa_arsenal_ammo_warning arsenal_warning;
  double health; qa_armor_state armor;
  qa_item_id active_weapon; /* NULL = TS null */
  bool has_ammo; qa_item_id ammo_item; double ammo_count;
  const qa_inventory_entry *inventory; size_t inventory_count;
  const qa_app_sim_player_ui_item *items; size_t item_count;
} qa_app_sim_player_ui;

typedef struct qa_app_sim_presentation_attachment { const char *path; const char *tag; } qa_app_sim_presentation_attachment;
typedef struct qa_app_simulation_presentation {
  const void *held_weapon; bool native_held_weapon;
  qa_item_id weapon_item; bool replaces_body; const char *render_owner;
  bool has_flare; qa_scene_flare flare;
  qa_actor_id actor; qa_content_id content; qa_game_family family;
  const char *path; double frame, old_frame;
  bool has_back_lerp; double back_lerp;
  double skin; bool has_skin_path; const char *skin_path;
  bool has_indexed_skin; qa_indexed_model_skin indexed_skin;
  bool has_player_colors; double player_color_top, player_color_bottom;
  double effects, render_flags; qa_vec3 origin;
  bool has_view_origin; qa_vec3 view_origin;
  bool has_view_angles; qa_vec3 view_angles;
  bool has_previous_origin; qa_vec3 previous_origin;
  bool has_model_beam; double model_beam_segment_length;
  bool has_shader_beam; const char *shader_beam_path; qa_vec3 shader_beam_end; double shader_beam_width;
  const qa_app_sim_presentation_attachment *model_attachments; size_t model_attachment_count;
  const void *model_anchor; const void *q3_grapple_cable;
  qa_vec3 angles; double scale;
  bool has_alpha; double alpha;
  bool visible; bool view_weapon;
  bool has_q3_weapon; double q3_weapon_time_ms; double q3_weapon_torso_animation;
  bool has_q3_weapon_last_fire; double q3_weapon_last_fire_ms; bool q3_weapon_firing;
  double q3_weapon_horizontal_speed; double q3_weapon_bob_cycle; double q3_weapon_weapon;
} qa_app_simulation_presentation;

/* SourcePresentationEvent union on kind; payload carried flat. */
typedef struct qa_app_sim_presentation_event {
  const char *kind; const char *event_kind;
  bool has_owner; qa_presentation_owner owner;
  bool has_recipient; qa_actor_id recipient;
  double sequence; qa_content_id content; double seconds;
  bool has_source_entity; double source_entity;
  double value, color, track, slot; bool paused;
  const char *name; const char *value_text;
  qa_actor_id actor; qa_vec3 angles; const char *reason;
  bool has_player; qa_actor_id player;
  qa_q1_fog_transition transition; double sky_factor;
  const void *event; /* kind-specific landed event payload (borrowed) */
} qa_app_sim_presentation_event;
typedef qa_app_sim_presentation_event qa_app_sim_source_presentation_event;

typedef struct qa_app_sim_debug_shape_access {
  void *ctx;
  const void *(*lines)(void *ctx, size_t *out_count);
  double (*line_width)(void *ctx);
} qa_app_sim_debug_shape_access;
typedef struct qa_app_sim_presentation_access {
  void *ctx;
  const void *(*mod_client_presentation_sources)(void *ctx, size_t *out_count);
  const void *(*world_text)(void *ctx, size_t *out_count);
  qa_app_sim_player_ui (*player_ui)(void *ctx, qa_actor_id actor);
  qa_app_simulation_travel (*capture_travel)(void *ctx, const char *spawn_point);
  qa_app_sim_player_admission (*admit_travel)(void *ctx, qa_client_id client, qa_app_simulation_travel travel);
  const void *(*character_views)(void *ctx, size_t *out_count);
  void (*player_command)(void *ctx, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
  const qa_transition_intent *(*take_transitions)(void *ctx, size_t *out_count);
  const qa_app_simulation_presentation *(*presentations)(void *ctx, size_t *out_count);
  const qa_app_sim_presentation_event *(*drain_presentation_events)(void *ctx, size_t *out_count);
  void (*register_resource)(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource);
  qa_app_sim_player_view (*player_view)(void *ctx, qa_actor_id actor);
  qa_app_sim_player_admission (*admit_player)(void *ctx, qa_client_id client);
} qa_app_sim_presentation_access;
#endif
