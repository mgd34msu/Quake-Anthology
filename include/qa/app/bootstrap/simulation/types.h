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
#include "qa/contracts/held_weapon.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/qvm_grapple.h"
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
/* TS SimulationTravel.players[].selectedArsenal ({ kind: "q1" | "q2" | "q3", ... }); the shared simulation defines it. */
typedef struct qa_app_sim_selected_arsenal_travel qa_app_sim_selected_arsenal_travel;
/* TS NativeQ2Travel (native-q2-travel.h). */
typedef struct qa_app_sim_native_q2_travel qa_app_sim_native_q2_travel;
/* TS PreparedQvmGrapple (qvm-grapple-selection.h) and WindowsCapabilities (guest/runtime/windows/contracts.h). */
typedef struct qa_app_prepared_qvm_grapple qa_app_prepared_qvm_grapple;
typedef struct qa_windows_capabilities qa_windows_capabilities;
/* TS Q3GuestRuntimeOptions["common"]["realTime"]: (output?: QvmCalendar) => number. */
typedef struct qa_qvm_calendar qa_qvm_calendar;
typedef struct qa_app_sim_real_time { void *ctx; double (*real_time)(void *ctx, bool has_output, qa_qvm_calendar *out_calendar); } qa_app_sim_real_time;
typedef struct qa_app_simulation_travel {
  const char *spawn_point;
  const char *source_kind; /* "q1"|"netquake"|"quakeworld"|"q2"|"shared" */
  double flags; double skill; double server_flags;
  const void *lmctf; const void *rerelease;
  bool has_landmark; double landmark_client_slot; const char *landmark_name;
  qa_vec3 landmark_relative_origin, landmark_relative_velocity, landmark_relative_view_angles;
  const qa_app_sim_travel_player *players; size_t player_count;
  const struct qa_app_sim_quakec_source_travel *quakec; /* source kind "netquake"|"quakeworld": QuakeCSource.captureTravel() */
} qa_app_simulation_travel;
struct qa_app_sim_travel_player {
  qa_client_id client;
  bool has_weapon_slot; qa_provider_id weapon_provider; qa_item_id weapon_item;
  const void *hand_grenades; const qa_app_sim_selected_arsenal_travel *selected_arsenal; const char *selected_arsenal_kind;
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

/* TS SimulationOptions.q3Guest: Pick<Q3GuestRuntimeOptions, "writable" | "common">
 * & { prepared, gameDirectory, gameType?, print }. All members are borrowed. */
struct qa_app_prepared_q3_game;
struct qa_user_file_store;
struct qa_qvm_common_services;
typedef struct qa_app_sim_q3_guest_options {
  const struct qa_app_prepared_q3_game *prepared;
  const char *game_directory;
  bool has_game_type; double game_type;
  void *print_ctx; void (*print)(void *ctx, const char *text);
  struct qa_user_file_store *writable;
  /* Pick<QvmCommonServices, "milliseconds" | "realTime" | "commands">: the
   * other members are the guest's own (role, cvars, print, arguments). */
  const struct qa_qvm_common_services *common;
} qa_app_sim_q3_guest_options;

typedef struct qa_app_simulation_options {
  const qa_app_prepared_qvm_grapple *prepared_qvm_grapple;
  const void *const *prepared_mods; size_t prepared_mod_count;
  const void *mod_commands; const void *mod_files;
  const void *enabled_mods; const void *mod_travel;
  void *nav_ctx; const void *(*prepare_rerelease_navigation)(void *ctx, qa_app_sim_shared_simulation *sim);
  const qa_app_sim_real_time *weapon_behavior_real_time; const qa_windows_capabilities *weapon_behavior_clock;
  const void *const *weapon_behaviors; size_t weapon_behavior_count;
  bool dedicated;
  void *prompt_ctx; bool (*prompt_supported)(void *ctx, qa_client_id client);
  const void *prepared_quakec;
  bool has_q2_guest; qa_app_sim_native_q2_guest_options q2_guest;
  const qa_app_sim_native_q2_travel *native_q2_travel;
  bool original_save_candidate;
  const char *start_items;
  bool has_player_colors; double player_color_top, player_color_bottom;
  const char *initial_spawn_point; const char *q2_next_server;
  const qa_app_sim_q3_guest_options *q3_guest; /* NULL = undefined */
  qa_identity_owner identity;
  qa_executable_recipe recipe;
  const void *world; const void *mounts;
  double skill; /* 0..3 */
  const char *mode; /* "singleplayer"|"coop"|"deathmatch" */
  double seed; double max_clients;
  bool has_travel; qa_app_simulation_travel travel;
  void *identity_ctx; bool (*player_identity)(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id);
  const struct qa_app_q3_source_session_carry *q3_session; /* NULL = undefined */
  const void *server_profile; void *source_registry;
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
/* TS SimulationPresentation.q3GrappleCable. */
typedef struct qa_app_sim_q3_grapple_cable {
  qa_actor_id owner; qa_vec3 owner_origin, owner_angles; double view_height;
  bool offhand, attached; const char *flight, *pull, *hold; double segment_length;
} qa_app_sim_q3_grapple_cable;
/* TS SimulationPresentation. Optional object members are NULL when absent
 * (borrowed from the producer). */
typedef struct qa_app_simulation_presentation {
  const qa_held_weapon_declaration *held_weapon; bool native_held_weapon;
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
  const qa_qvm_grapple_view_anchor *model_anchor; /* QvmGrappleDefinition["presentation"]["viewAnchor"] */
  const qa_app_sim_q3_grapple_cable *q3_grapple_cable;
  qa_vec3 angles; double scale;
  bool has_alpha; double alpha;
  bool visible; bool view_weapon;
  bool has_q3_weapon; double q3_weapon_time_ms; double q3_weapon_torso_animation;
  bool has_q3_weapon_last_fire; double q3_weapon_last_fire_ms; bool q3_weapon_firing;
  double q3_weapon_horizontal_speed; double q3_weapon_bob_cycle; double q3_weapon_weapon;
} qa_app_simulation_presentation;

/* TS Q3CharacterPresentationEvent: Q3CharacterEvent with a plain actor id
 * (payload of a "q3-character" presentation event). */
typedef struct qa_app_sim_q3_character_presentation_event {
  qa_actor_id actor; double sequence, time_milliseconds, event, parameter;
} qa_app_sim_q3_character_presentation_event;

/* The payload types of SourcePresentationEvent that other modules own. They
 * are declared where TS declares them; consumers include those headers:
 * Q1Event content/q1/foundation/types.h, Q1CompositionEvent
 * content/composition/q1/types.h, Q1IntermissionResult content/q1/base/rules.h,
 * Q2PresentationEvent content/q2/foundation/host.h, Q2WeaponEvent
 * content/q2/foundation/weapons/types.h, Q2CompositionEvent
 * content/composition/q2/types.h, Q2RereleaseEvent content/q2/rerelease/types.h,
 * Q2PlayerEvent content/q2/base/player/types.h, Q3SourceEvent
 * app/bootstrap/simulation/q3/host.h. */
typedef struct qa_q1_event qa_q1_event;
typedef struct qa_comp_q1_event qa_comp_q1_event;
typedef struct qa_q1_intermission_result qa_q1_intermission_result;
typedef struct qa_q2_presentation_event qa_q2_presentation_event;
typedef struct qa_q2w_event qa_q2w_event;
typedef struct qa_q2_composition_event qa_q2_composition_event;
typedef struct qa_q2_rerelease_event qa_q2_rerelease_event;
typedef struct qa_q2_player_event qa_q2_player_event;
typedef struct qa_app_q3_source_event qa_app_q3_source_event;

/* The inline object payloads of SourcePresentationEvent (types.ts). kind
 * strings are the TS literals. */
typedef struct qa_app_sim_presentation_owner_event {
  const char *kind; /* "retired"|"refreshed" */
  qa_presentation_owner owner;
} qa_app_sim_presentation_owner_event;
typedef struct qa_app_sim_debug_graph_event {
  const char *kind; /* "sample" */
  double value, color;
} qa_app_sim_debug_graph_event;
typedef struct qa_app_sim_q1_sky_event {
  const char *kind; /* "skybox" */
  const char *name;
} qa_app_sim_q1_sky_event;
/* TS Q1ClientMetadataEvent: value is text for "name"|"social"|"player-info"
 * and number for "colors"|"frags"|"ping". */
typedef struct qa_app_sim_q1_client_metadata_event {
  const char *kind;
  double slot;
  const char *text; /* string kinds */
  double value;     /* number kinds */
} qa_app_sim_q1_client_metadata_event;
typedef struct qa_app_sim_q1_session_event {
  const char *kind; /* "level-completed"|"back-to-lobby" */
} qa_app_sim_q1_session_event;
typedef struct qa_app_sim_q1_fog_event {
  const char *kind; /* "transition" */
  bool has_player; qa_actor_id player; /* false is TS null */
  qa_q1_fog_transition transition;
  double sky_factor;
} qa_app_sim_q1_fog_event;
typedef struct qa_app_sim_music_event {
  const char *kind; /* "cd-track"|"pause" */
  double track;     /* cd-track */
  bool paused;      /* pause */
} qa_app_sim_music_event;
/* TS view-reset carries these at the top level, beside kind; C keeps them in
 * the payload union like every other kind. */
typedef struct qa_app_sim_view_reset_event {
  const char *reason; /* "spawn"|"teleport"|"freeze"|"source" */
  qa_actor_id actor;
  qa_vec3 angles;
} qa_app_sim_view_reset_event;

/* TS SourcePresentationEvent: a union on kind ("presentation-owner",
 * "debug-graph", "q1", "q1-sky", "q1-client", "q1-session", "q1-fog", "music",
 * "q1-composition", "q1-level", "q2", "q2-weapon", "view-reset",
 * "q2-composition", "q2-rerelease", "q2-player", "q3-character", "q3-source").
 * Exactly the union member named after kind applies (hyphens become
 * underscores). Payloads owned by other modules are held by pointer; a value
 * built by a producer borrows them for the call, and an owned copy
 * (qa_app_sim_presentation_event_copy) points into its own storage. */
#define QA_APP_SIM_SOURCE_PRESENTATION_EVENT_MEMBERS                          \
  const char *kind;                                                         \
  union {                                                                   \
    qa_app_sim_presentation_owner_event presentation_owner;                 \
    qa_app_sim_debug_graph_event debug_graph;                               \
    const qa_q1_event *q1;                                                  \
    qa_app_sim_q1_sky_event q1_sky;                                         \
    qa_app_sim_q1_client_metadata_event q1_client;                          \
    qa_app_sim_q1_session_event q1_session;                                 \
    qa_app_sim_q1_fog_event q1_fog;                                         \
    qa_app_sim_music_event music;                                           \
    const qa_comp_q1_event *q1_composition;                                 \
    const qa_q1_intermission_result *q1_level;                              \
    const qa_q2_presentation_event *q2;                                     \
    const qa_q2w_event *q2_weapon;                                          \
    qa_app_sim_view_reset_event view_reset;                                 \
    const qa_q2_composition_event *q2_composition;                          \
    const qa_q2_rerelease_event *q2_rerelease;                              \
    const qa_q2_player_event *q2_player;                                    \
    qa_app_sim_q3_character_presentation_event q3_character;                \
    const qa_app_q3_source_event *q3_source;                                \
  };
typedef struct qa_app_sim_source_presentation_event {
  QA_APP_SIM_SOURCE_PRESENTATION_EVENT_MEMBERS
} qa_app_sim_source_presentation_event;

/* TS SimulationPresentationEvent = SourcePresentationEvent & {owner?,
 * recipient?, sequence, content, seconds, sourceEntity?}. One type serves as
 * a borrowed view (storage NULL: every pointer belongs to the producer, for
 * the call) and as an owned value (storage set: every string, array and
 * payload is a deep copy held by storage). Stores keep owned values. */
typedef struct qa_app_sim_presentation_event {
  QA_APP_SIM_SOURCE_PRESENTATION_EVENT_MEMBERS
  bool has_owner; qa_presentation_owner owner;
  bool has_recipient; qa_actor_id recipient;
  double sequence; qa_content_id content; double seconds;
  bool has_source_entity; double source_entity; /* false is TS null/undefined */
  void *storage; /* owned block, or NULL for a borrowed view */
} qa_app_sim_presentation_event;

/* Kind validation: true when kind is one of the TS SourcePresentationEvent
 * kinds above. */
bool qa_app_sim_source_presentation_kind_known(const char *kind);
/* The SourcePresentationEvent part of a presentation event (a view sharing
 * the event's pointers). */
qa_app_sim_source_presentation_event qa_app_sim_presentation_event_source(const qa_app_sim_presentation_event *event);
/* A borrowed presentation event made of a source event and the stamp fields
 * TS emitOwned spreads over it; storage NULL. */
qa_app_sim_presentation_event qa_app_sim_presentation_event_view(const qa_app_sim_source_presentation_event *source);
/* Deep copy: *out becomes an owned value holding its own copy of every
 * string, array and payload reachable from source (identity tokens such as
 * qa_session_id.token are kept, as TS shares them). kind must be a known
 * kind (RangeError otherwise). Returns false only on OOM, leaving *out
 * zeroed. */
bool qa_app_sim_presentation_event_copy(qa_app_sim_presentation_event *out, const qa_app_sim_presentation_event *source);
/* Releases an owned value's storage and zeroes it; a no-op on views. */
void qa_app_sim_presentation_event_release(qa_app_sim_presentation_event *event);

/* Element types of the access results below, declared by their TS owners:
 * DebugLine debug/shapes.h, WorldText text/world.h, Q3CharacterView
 * content/q3/foundation/presentation.h, ActiveModClientPresentation
 * world/session/mod_client_presentation.h (which includes this header). */
typedef struct qa_debug_line qa_debug_line;
typedef struct qa_world_text qa_world_text;
typedef struct qa_q3_character_view qa_q3_character_view;
typedef struct qa_active_mod_client_presentation qa_active_mod_client_presentation;
typedef struct qa_app_sim_debug_shape_access {
  void *ctx;
  const qa_debug_line *(*lines)(void *ctx, size_t *out_count);
  double (*line_width)(void *ctx);
} qa_app_sim_debug_shape_access;
typedef struct qa_app_sim_presentation_access {
  void *ctx;
  /* Optional (NULL is the TS absent member). */
  const qa_active_mod_client_presentation *(*mod_client_presentation_sources)(void *ctx, size_t *out_count);
  const qa_world_text *(*world_text)(void *ctx, size_t *out_count);
  qa_app_sim_player_ui (*player_ui)(void *ctx, qa_actor_id actor);
  qa_app_simulation_travel (*capture_travel)(void *ctx, const char *spawn_point);
  qa_app_sim_player_admission (*admit_travel)(void *ctx, qa_client_id client, qa_app_simulation_travel travel);
  const qa_q3_character_view *(*character_views)(void *ctx, size_t *out_count);
  void (*player_command)(void *ctx, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
  const qa_transition_intent *(*take_transitions)(void *ctx, size_t *out_count);
  const qa_app_simulation_presentation *(*presentations)(void *ctx, size_t *out_count);
  const qa_app_sim_presentation_event *(*drain_presentation_events)(void *ctx, size_t *out_count);
  void (*register_resource)(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource);
  qa_app_sim_player_view (*player_view)(void *ctx, qa_actor_id actor);
  qa_app_sim_player_admission (*admit_player)(void *ctx, qa_client_id client);
} qa_app_sim_presentation_access;
#endif
