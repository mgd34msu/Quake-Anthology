#ifndef QA_APPLICATION_H
#define QA_APPLICATION_H

#include "qa/builtin.h"
#include "qa/campaign.h"
#include "qa/game_q1_maps.h"
#include "qa/game_q2_entities.h"
#include "qa/game_q2_player.h"
#include "qa/game_q3_map.h"
#include "qa/launch.h"
#include "qa/input.h"
#include "qa/movement.h"
#include "qa/native_host.h"
#include "qa/player_progress.h"
#include "qa/q3_host.h"
#include "qa/rankings.h"
#include "qa/targets.h"

typedef struct qa_application qa_application;

typedef struct qa_application_map_request {
    qa_product_id geometry, presentation;
    const char *map, *spawn_point;
    bool new_unit, carry_players;
} qa_application_map_request;
typedef struct qa_application_map_view {
    qa_product_id geometry, presentation;
    const char *name;
    qa_resource *resource;
    uint64_t revision;
} qa_application_map_view;
typedef struct qa_application_visual_view {
    qa_actor_id actor;
    qa_actor_owner provider, character;
    qa_product_id content, character_content;
    qa_game_family family;
    qa_body_state body;
    const char *models[4], *skin_path;
    int32_t frame, old_frame, skin, legs_animation, torso_animation;
    uint64_t effects;
    uint32_t render_flags, source_flags, powerups, inline_model, q1_effects;
    float alpha, scale;
    bool visible, has_inline_model;
} qa_application_visual_view;
typedef struct qa_application_presentation_view {
    qa_actor_owner hud, menu;
    bool source_hud, source_menu, source_world;
} qa_application_presentation_view;

typedef enum qa_application_guest_menu {
    QA_APPLICATION_GUEST_MENU_NONE,
    QA_APPLICATION_GUEST_MENU_MAIN,
    QA_APPLICATION_GUEST_MENU_INGAME
} qa_application_guest_menu;
typedef struct qa_application_travel_request {
    qa_actor_owner provider;
    qa_actor_id cause;
    qa_product_id geometry;
    const char *expression;
    const qa_q2_landmark *landmark;
    bool new_unit, carry_players, complete_campaign;
} qa_application_travel_request;
typedef struct qa_application_travel_view {
    qa_travel_target target;
    qa_actor_owner provider;
    qa_actor_id cause;
    qa_product_id geometry;
    uint64_t revision;
    bool carry_players, complete_campaign, has_landmark;
    qa_q2_landmark landmark;
} qa_application_travel_view;

typedef struct qa_application_motion_view {
    qa_actor_id actor;
    qa_body_state body;
    qa_vec3 view_angles, angular_kick;
    uint64_t hold_until_ns, revision;
    qa_builtin_motion_reason reason;
    bool force_view_angles, apply_angular_kick;
} qa_application_motion_view;

typedef struct qa_application_shader_remap_view {
    qa_string_id original, replacement;
    uint64_t time_ns;
} qa_application_shader_remap_view;
typedef struct qa_application_q2_visual_view {
    qa_actor_id actor;
    qa_q2_visual visual;
    uint64_t revision;
} qa_application_q2_visual_view;
typedef struct qa_application_q2_map_event {
    qa_actor_owner provider;
    uint64_t time_ns;
    qa_q2_map_event event;
} qa_application_q2_map_event;
typedef struct qa_application_q3_map_event {
    qa_actor_owner provider;
    uint64_t time_ns;
    qa_q3_map_event event;
} qa_application_q3_map_event;
typedef struct qa_application_q2_player_event {
    qa_actor_owner provider;
    uint64_t time_ns;
    qa_q2_player_event event;
} qa_application_q2_player_event;
typedef struct qa_application_protocol_reference {
    size_t offset;
    qa_actor_id actor;
    bool packed_sound;
} qa_application_protocol_reference;
typedef struct qa_application_protocol_event {
    qa_actor_owner provider;
    qa_clock_kind dialect;
    uint64_t time_ns;
    qa_actor_id recipient;
    qa_vec3 origin;
    qa_bytes payload;
    const qa_application_protocol_reference *references;
    size_t reference_count;
    int32_t destination;
    bool reliable, multicast, signon;
} qa_application_protocol_event;

/* One application-owned control continuation follows a live actor generation.
 * Its movement family comes from the current MOVEMENT role binding; callers do
 * not select or duplicate a family state. The shared world remains the body,
 * collision and link authority. */
typedef struct qa_application_control_view {
    qa_actor_id actor;
    qa_movement_state state;
    qa_movement_profile profile;
    qa_bounds bounds;
    qa_movement_ground ground;
    qa_vec3 view_angles, command_angles, view_offset;
    uint64_t command_sequence;
    uint32_t buttons, previous_buttons;
    int32_t water_level, water_type;
    float view_height, gravity_multiplier;
    bool flight, cutscene;
} qa_application_control_view;

typedef struct qa_application_camera_view {
    qa_actor_id actor;
    qa_vec3 origin, angles, view_offset;
    float view_height;
    bool cutscene;
} qa_application_camera_view;

typedef enum qa_application_state {
    QA_APPLICATION_READY,
    QA_APPLICATION_RUNNING,
    QA_APPLICATION_STOPPING,
    QA_APPLICATION_FAULTED
} qa_application_state;

/* The platform/client owner fills borrowed input, scene, audio and connected
 * client services over the application's core defaults. Preparation must not
 * execute a guest or mutate the active world. Context outlives retained hosts. */
typedef bool (*qa_application_q3_services_fn)(void *, qa_application *,
                                             qa_actor_owner, qa_qvm_role, uint32_t,
                                             qa_q3_host_options *, qa_error *);

typedef enum qa_application_q3_client_effect {
    QA_APPLICATION_Q3_SYSTEM_INFO,
    QA_APPLICATION_Q3_MAP_RESTART,
    QA_APPLICATION_Q3_LEVEL_SHOT,
    QA_APPLICATION_Q3_DISCONNECT
} qa_application_q3_client_effect;
typedef bool (*qa_application_q3_client_effect_fn)(void *, qa_application *,
                                                   qa_actor_owner, uint32_t,
                                                   qa_application_q3_client_effect,
                                                   const char *, qa_error *);
typedef bool (*qa_application_world_hook_fn)(void *, qa_application *, qa_error *);
/* The platform fills a separate, zeroed presentation service view. Core
 * wrappers retain canonical world ownership and call decorators with this
 * view's context. A returned frontend lease is owned by the guest engine. */
typedef bool (*qa_application_native_q2_services_fn)(void *, qa_application *,
    qa_actor_owner, qa_native_profile, qa_native_host_engine_services *,
    qa_native_host_q2_application_fn *, void **application_context, qa_error *);

typedef struct qa_application_options {
    const char *content_root;
    const char *user_root;
    uint64_t catalog_generation;
    uint32_t actor_capacity;
    uint32_t component_capacity;
    /* Borrowed backend context outlives the application; NULL retains the
     * documented unavailable service rather than selecting a new backend. */
    const qa_ranking_provider *ranking_provider;
    const qa_native_runner_config *native_runner;
    void *guest_context;
    qa_application_q3_services_fn q3_services;
    qa_application_q3_client_effect_fn q3_client_effect;
    qa_application_native_q2_services_fn native_q2_services;
    qa_application_world_hook_fn world_change_ready;
    qa_application_world_hook_fn before_world_change;
    qa_application_world_hook_fn world_retired;
    void (*console_print)(void *, const qa_command_context *, const char *);
    bool discover_mods;
    bool mixed_source_order;
} qa_application_options;

void qa_application_options_default(qa_application_options *);
bool qa_application_create(const qa_application_options *, qa_application **,
                           qa_error *);
/* Destruction requires an idle application. Success consumes the public
 * handle. Retained launch snapshots may keep detached provider state and its
 * borrowed application services alive until their final release; callers must
 * not use the application again after a successful call. */
bool qa_application_destroy(qa_application *, qa_error *);
/* Retire all source callbacks while platform services are still available.
 * Success retains the shared authorities and console for platform teardown.
 * Failure retains this application; retry after outstanding owners release. */
bool qa_application_retire_sources(qa_application *, qa_error *);

qa_application_state qa_application_get_state(const qa_application *);
/* Borrowed fault detail. NULL means the application has not faulted. */
const qa_error *qa_application_error(const qa_application *);
void qa_application_request_stop(qa_application *);
bool qa_application_should_stop(const qa_application *);

/* All returned owners are borrowed. The application is the only mutable
 * lifecycle owner; subsystem adapters use these to bind typed services. */
qa_resource_pool *qa_application_resources(qa_application *);
qa_catalog *qa_application_catalog(qa_application *);
qa_session *qa_application_session(qa_application *);
qa_world *qa_application_world(qa_application *);
/* Only published map metadata is visible, including during source INIT.
 * Retirement hides it before geometry replacement. Names/resources borrow the
 * application until mutation; retain the resource for longer frontend use. */
bool qa_application_map_read(const qa_application *, qa_application_map_view *);
bool qa_application_visual_read(qa_application *, qa_actor_id,
                                  qa_application_visual_view *, qa_error *);
bool qa_application_weapon_read(qa_application *, qa_actor_id, qa_item_id *, qa_error *);
bool qa_application_present(qa_application *, uint32_t seat,
                             uint32_t real_milliseconds,
                             uint32_t client_milliseconds, qa_error *);
bool qa_application_presentation_read(qa_application *, uint32_t seat,
                                       qa_application_presentation_view *);
bool qa_application_guest_menu_set(qa_application *, uint32_t seat,
                                     qa_application_guest_menu, bool *handled,
                                     qa_error *);
bool qa_application_guest_source_actor(qa_application *, qa_actor_owner,
                                        uint32_t seat, int32_t source_number,
                                        qa_actor_id *, qa_error *);
bool qa_application_guest_input(qa_application *, uint32_t seat,
                                 const qa_input_event *, bool *consumed, qa_error *);
bool qa_application_capture_command_context(qa_application *, const qa_command_context *,
                                              qa_command_context *, qa_error *);
bool qa_application_command_context_active(const qa_application *, const qa_command_context *);
qa_vfs *qa_application_context_files(qa_application *, const qa_command_context *, qa_mount_id *);
bool qa_application_source_command(qa_application *, const qa_command_invocation *, qa_error *);
bool qa_application_actor_command(qa_application *, qa_actor_id, const char *, qa_error *);
qa_combat *qa_application_combat(qa_application *);
qa_inventory *qa_application_inventory(qa_application *);
qa_pickups *qa_application_pickups(qa_application *);
qa_targets *qa_application_targets(qa_application *);
qa_player_progress *qa_application_player_progress(qa_application *);
qa_rankings *qa_application_rankings(qa_application *);
qa_cvars *qa_application_cvars(qa_application *);
qa_console *qa_application_console(qa_application *);
/* Qualify all callback owners before the caller's no-fail publication phase.
 * Applying the new borrowed context invokes no service callback. */
bool qa_application_guest_context_rebind_ready(const qa_application *, const qa_scene_frame *, qa_error *);
void qa_application_guest_context_rebind(qa_application *, void *, qa_scene_frame *);
typedef enum qa_application_console_kind {
    QA_APPLICATION_CONSOLE_ENGINE,
    QA_APPLICATION_CONSOLE_QC,
    QA_APPLICATION_CONSOLE_NATIVE_Q2,
    QA_APPLICATION_CONSOLE_Q3_GAME,
    QA_APPLICATION_CONSOLE_Q3_CGAME,
    QA_APPLICATION_CONSOLE_Q3_UI
} qa_application_console_kind;
typedef struct qa_application_console_scope {
    qa_actor_owner provider;
    qa_application_console_kind kind;
    uint32_t seat;
} qa_application_console_scope;
/* Shared console aliases use the smallest provider instance/role/seat key. */
bool qa_application_console_scope_read(const qa_application *, const qa_console *,
                                        qa_application_console_scope *);
/* Instance text is borrowed until its attached provider retires. */
const char *qa_application_provider_instance(const qa_application *, qa_actor_owner);
bool qa_application_provider_owner(const qa_application *, const char *, qa_actor_owner *);
bool qa_application_provider_gravity(const qa_application *, qa_actor_owner, float *);
bool qa_application_q1_fog_read(qa_application *, qa_actor_id, qa_q1_fog_state *);
bool qa_application_q1_fog_owner(qa_application *, qa_actor_owner *);
bool qa_application_q1_monster_counts(const qa_application *, uint32_t seat,
    uint32_t *total, uint32_t *killed);
size_t qa_application_console_count(const qa_application *);
qa_console *qa_application_console_at(qa_application *, size_t, qa_actor_owner *);
const qa_launch_snapshot *qa_application_launch(const qa_application *);
uint64_t qa_application_configuration_generation(const qa_application *);
/* Shared movement/network owners consume the latest committed discontinuity.
 * False means this actor generation has no retained change. */
bool qa_application_motion_read(const qa_application *, qa_actor_id,
                                qa_application_motion_view *);
/* Headless applications retain shader remaps until a scene owner can apply
 * them. Entries are borrowed snapshots of interned names. */
size_t qa_application_shader_remap_count(const qa_application *);
bool qa_application_shader_remap_at(const qa_application *, size_t,
                                    qa_application_shader_remap_view *);
bool qa_application_q2_visual_read(const qa_application *, qa_actor_id,
                                   qa_application_q2_visual_view *);

/* Admit an already-live actor with a shared body and combat record. Admission
 * derives its selected movement profile and is idempotent for the same actor
 * generation. Commands execute synchronously through that selected profile;
 * callbacks may retire the actor. A callback failure after movement starts
 * faults the application because committed effects are never replayed. */
bool qa_application_control_admit(qa_application *, qa_actor_id,
                                  qa_vec3 view_angles, qa_error *);
bool qa_application_control_move(qa_application *, qa_actor_id,
                                 const qa_movement_command *, qa_error *);
bool qa_application_control_read(const qa_application *, qa_actor_id,
                                 qa_application_control_view *);
bool qa_application_control_camera(const qa_application *, qa_actor_id,
                                   qa_application_camera_view *);
/* Ends application-owned cinematic suppression without moving the actor.
 * Spawn motion changes also clear it. */
bool qa_application_control_end_cutscene(qa_application *, qa_actor_id,
                                         qa_error *);

/* Gameplay events are copied into one application-owned queue for scene,
 * audio and network consumers. Event arguments returned by event_at remain
 * valid until clear_events or successful application destruction. */
size_t qa_application_event_count(const qa_application *);
bool qa_application_event_at(const qa_application *, size_t,
                             qa_builtin_event *);
size_t qa_application_q2_map_event_count(const qa_application *);
bool qa_application_q2_map_event_at(const qa_application *, size_t,
                                    qa_application_q2_map_event *);
size_t qa_application_q3_map_event_count(const qa_application *);
bool qa_application_q3_map_event_at(const qa_application *, size_t,
                                    qa_application_q3_map_event *);
size_t qa_application_q2_player_event_count(const qa_application *);
bool qa_application_q2_player_event_at(const qa_application *, size_t,
                                       qa_application_q2_player_event *);
size_t qa_application_protocol_event_count(const qa_application *);
bool qa_application_protocol_event_at(const qa_application *, size_t,
                                      qa_application_protocol_event *);
bool qa_application_clear_events(qa_application *, qa_error *);

/* Discovery builds a complete replacement before swapping the catalog.
 * Existing drafts and launch snapshots retain their prior catalog. */
bool qa_application_rediscover(qa_application *, bool discover_mods,
                               qa_error *);

/* Applies a complete draft at a session safe point. Validation failures and
 * reversible preparation leave the active configuration untouched. Once
 * actor retirement or map publication starts, a callback failure faults the
 * application and is never replayed or presented as a rolled-back change. */
bool qa_application_apply(qa_application *, const qa_launch_draft *, qa_error *);

/* Map changes reuse configuration admission and the one shared world. Travel
 * strings are copied on queueing; read views borrow until the route advances.
 * A caller completes non-map targets after presentation or playback ends. */
bool qa_application_load_map(qa_application *, const qa_application_map_request *, qa_error *);
bool qa_application_queue_travel(qa_application *, const qa_application_travel_request *, qa_error *);
/* Queue one literal map path in request.expression. Q2 route separators and
 * unit/spawn markers are ordinary path bytes in this typed map operation. */
bool qa_application_queue_map_travel(qa_application *, const qa_application_travel_request *, qa_error *);
bool qa_application_travel_read(const qa_application *, qa_application_travel_view *);
bool qa_application_commit_travel(qa_application *, uint64_t revision, qa_error *);
bool qa_application_complete_travel(qa_application *, uint64_t revision, qa_error *);
qa_string_id qa_application_nextserver(const qa_application *);
bool qa_application_player_actor(const qa_application *, uint32_t seat, qa_actor_id *);
size_t qa_application_player_count(const qa_application *);

/* Elapsed monotonic duration is supplied by the outer platform loop or replay.
 * The application never reads a hidden wall clock for simulation. */
bool qa_application_advance(qa_application *, uint64_t elapsed_ns, qa_error *);

#endif
