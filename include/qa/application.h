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
#include "qa/native_process_resources.h"
#include "qa/player_progress.h"
#include "qa/q3_host.h"
#include "qa/rankings.h"
#include "qa/application_rankings.h"
#include "qa/targets.h"
#include "qa/q3_product_policy.h"
#include "qa/network_q1.h"
#include "qa/session.h"
#include "qa/scene.h"

enum { QA_APPLICATION_RESOURCE_KEY_CAPACITY = sizeof("resource:unified:") + 64 };

typedef struct qa_application qa_application;
struct qa_save_image;
struct qa_application_q3_round_services;
struct qa_application_startup_hooks;
typedef struct qa_application_q3_equipment_services qa_application_q3_equipment_services;
typedef struct qa_application_q3_body_services qa_application_q3_body_services;

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
typedef struct qa_application_q2_flare_view {
    const char *image;
    qa_vec3 color, rim_color;
    float fade_start, fade_end;
    bool present, has_rim_color, lock_angle;
} qa_application_q2_flare_view;
typedef struct qa_application_visual_view {
    qa_actor_id actor;
    qa_actor_owner provider, character;
    qa_product_id content, character_content;
    qa_game_family family;
    qa_body_state body;
    qa_vec3 previous_origin;
    const char *models[4], *skin_path;
    const qa_resource *model_resources[4];
    /* Borrowed acquired-source receipts share the retained precache lifetime. */
    const qa_vfs_acquisition *model_openings[4];
    int32_t colormap;
    uint8_t player_colors;
    bool has_player_colors;
    int32_t frame, old_frame, skin, legs_animation, torso_animation;
    uint64_t effects;
    uint32_t render_flags, source_flags, powerups, inline_model, q1_effects;
    float alpha, scale;
    bool visible, has_inline_model, model_beam;
    qa_q3_entity source_entity;
    int32_t source_number, source_client;
    bool has_source_entity;
    qa_application_q2_flare_view q2_flare;
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
    /* Historical transport seats captured at the actual PRINT emission. */
    const struct qa_application_network_q2_recipient_view *recipients;
    size_t recipient_count;
} qa_application_q2_player_event;
typedef struct qa_application_protocol_reference {
    size_t offset;
    qa_actor_id actor;
    bool packed_sound;
} qa_application_protocol_reference;
typedef struct qa_application_protocol_resource_reference {
    /* Ordinal in this packet's decoded record stream, not a journal event ID. */
    size_t record_ordinal;
    qa_native_host_resource_kind kind;
    uint32_t source_index;
    const char *name;
    /* Prior real Source precache registration. Empty retains an unresolved
     * Source spelling, including sexed sounds and known missing resources. */
    char resource_key[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    uint64_t resource_custody; /* Exact retained opening within this immutable key. */
} qa_application_protocol_resource_reference;
typedef struct qa_application_protocol_event {
    qa_actor_owner provider;
    qa_clock_kind dialect;
    uint64_t time_ns;
    qa_actor_id recipient;
    qa_vec3 origin;
    qa_bytes payload;
    const qa_application_protocol_reference *references;
    size_t reference_count;
    const qa_application_protocol_resource_reference *resources;
    size_t resource_count;
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

/* A copied selected-player seed for private client prediction. Provider IDs
 * identify the actual admitted roles. No command, source call or world write
 * is performed; the prediction owner supplies its own scratch and clock. */
typedef enum qa_application_numeric_rounding {
    QA_APPLICATION_ROUND_NEAREST,
    QA_APPLICATION_ROUND_DOWN,
    QA_APPLICATION_ROUND_UP,
    QA_APPLICATION_ROUND_ZERO
} qa_application_numeric_rounding;

/* Facts from the native C movement constructor. External Source execution
 * leaves its recipe absent; the independent copied prediction kernel retains
 * its own C recipe. This describes storage and evaluation, not each operation. */
typedef struct qa_application_movement_numeric {
    qa_string_id id;
    uint32_t radix, scalar_mantissa_bits, double_mantissa_bits;
    int32_t evaluation_method;
    qa_application_numeric_rounding rounding;
    bool native_c, qw_origin_binary64;
} qa_application_movement_numeric;

typedef struct qa_application_control_prediction_configuration {
    qa_actor_owner movement, character, arsenal;
    qa_actor_owner profile_id;
    qa_clock_config clock;
    qa_application_movement_numeric numeric; /* Actual Source movement executor. */
    qa_application_movement_numeric prediction_numeric; /* Independently copied C movement kernel. */
    qa_movement_input input;
    qa_vec3 q2r_pml_origin;
    qa_vec3 view_angles, command_angles;
    qa_movement_ground ground;
    float view_height;
    int32_t water_level, water_type;
    bool q3_character, q3_arsenal;
    bool native_q3_character, native_q3_arsenal;
    float fractional_weapon_ms;
    uint32_t external_weapon_slot;
    int32_t requested_weapon;
    bool has_client_view_offset;
    qa_vec3 client_view_offset;
} qa_application_control_prediction_configuration;

typedef struct qa_application_camera_view {
    qa_actor_id actor;
    qa_vec3 origin, angles, view_offset;
    float view_height;
    bool cutscene, has_client_view_offset;
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

typedef struct qa_application_q3_client_preparation {
    const qa_launch_instance *receiver_descriptor, *game_descriptor;
    qa_catalog *receiver_catalog;
    const qa_product *receiver_product;
    qa_actor_owner receiver, source_owner;
    qa_qvm_role role;
    uint32_t seat, source_client;
    qa_catalog *source_catalog;
    const qa_product *source_product;
    qa_console *source_console;
    qa_cvars *source_cvars;
    const qa_q3_product_policy *product_policy;
    /* The actual private client imports, before the host copies them. */
    qa_q3_host_options *services;
    qa_application_q3_equipment_services *equipment_services;
    qa_application_q3_body_services *body_services;
    bool restoring;
    qa_bytes restored_cvars;
    qa_qvm_role cvars_role;
    uint32_t cvars_seat;
} qa_application_q3_client_preparation;
typedef bool (*qa_application_q3_client_prepare_fn)(void *, qa_application *,
    const qa_application_q3_client_preparation *, qa_error *);
struct application_q3_component_scene_preparation;
typedef bool (*qa_application_q3_component_scene_prepare_fn)(void *,
    const struct application_q3_component_scene_preparation *,qa_error *);
typedef bool (*qa_application_q3_component_client_drop_fn)(void *,qa_actor_owner,
    qa_actor_id,const char *,qa_error *);
/* Qualifies a physical shared registry by its retained source constructor.
 * found=false leaves genuine provider-private registries with their owner. */
typedef bool (*qa_application_q3_client_registry_reference_fn)(void *, const qa_cvars *,
    const char **source_instance, uint32_t *authored_seat, bool *found, qa_error *);

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
typedef bool (*qa_application_q3_campaign_command_fn)(void *, qa_application *,
    const qa_command_invocation *, bool *handled, qa_error *);
/* The platform fills a separate, zeroed presentation service view. Core
 * wrappers retain canonical world ownership and call decorators with this
 * view's context. A returned frontend lease is owned by the guest engine. */
typedef bool (*qa_application_native_q2_services_fn)(void *, qa_application *,
    qa_actor_owner, qa_native_profile, qa_native_host_engine_services *,
    qa_native_host_q2_application_fn *, void **application_context, qa_error *);

typedef struct qa_application_model_admission_request {
    qa_actor_owner provider;
    qa_game_family family;
    const char *request;
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
    const qa_vfs *view;
    bool has_player_colors;
    uint8_t player_colors;
} qa_application_model_admission_request;
typedef struct qa_application_model_admission {
    qa_scene_image_options images;
    qa_bytes palette_rgb;
    qa_scene_palette_source palette_source;
    const qa_vfs *palette_view;
} qa_application_model_admission;
/* The real BODY constructor owns these borrowed receipts until its next
 * mutation. The caller must retain/copy them before another callback. */
typedef bool (*qa_application_model_admission_fn)(void *, qa_application *,
    const qa_application_model_admission_request *, qa_application_model_admission *, qa_error *);

typedef struct qa_application_options {
    const char *const *startup_commands;
    size_t startup_command_count;
    const char *initial_product_key;
    /* Pure isolated baseline construction imports this retained owner. */
    const qa_q3_product_policy *q3_product_policy;
    const char *content_root;
    const char *const *install_roots;
    size_t install_root_count;
    const char *user_root;
    /* Actual input configuration directory, independent of content roots and
     * GAME selection. The application retains this native directory owner. */
    qa_fs_root *player_profile_root;
    uint64_t catalog_generation;
    uint32_t actor_capacity;
    uint32_t component_capacity;
    /* Borrowed backend context outlives the application; NULL retains the
     * documented unavailable service rather than selecting a new backend. */
    const qa_ranking_provider *ranking_provider;
    const char *ranking_game_key;
    qa_application_ranking_effect_fn ranking_effect;
    const qa_native_runner_config *native_runner;
    qa_native_runtime *native_runtime;
    const char *native_bootstrap;
    qa_native_process_resource_policy native_process_policy;
    void *guest_context;
    qa_application_q3_services_fn q3_services;
    qa_application_q3_client_prepare_fn q3_client_prepare;
    qa_application_q3_component_scene_prepare_fn q3_component_scene_prepare;
    qa_application_q3_component_client_drop_fn q3_component_client_drop;
    qa_application_q3_client_registry_reference_fn q3_client_registry_reference;
    qa_application_q3_client_effect_fn q3_client_effect;
    qa_application_q3_campaign_command_fn q3_campaign_command;
    const struct qa_application_q3_round_services *q3_round_services;
    qa_application_native_q2_services_fn native_q2_services;
    qa_application_model_admission_fn model_admission;
    qa_application_world_hook_fn world_change_ready;
    qa_application_world_hook_fn before_world_change;
    qa_application_world_hook_fn world_retired;
    void (*console_print)(void *, const qa_command_context *, const char *);
    bool discover_mods;
    bool mixed_source_order;
    /* Actual host mode when a Source has no dedicated cvar (QW GAME). */
    bool dedicated;
    const struct qa_application_startup_hooks *startup_hooks;
    /* Actual connected recipient capability, borrowed with its backend.
     * An absent callback retains the source's unsupported-prompt behavior. */
    void *prompt_context;
    bool (*prompt_supported)(void *, qa_actor_id, bool *supported, qa_error *);
} qa_application_options;

void qa_application_options_default(qa_application_options *);
/* handled=false means no platform constructor is installed. */
bool qa_application_model_admit(qa_application *, const qa_application_model_admission_request *,
    qa_application_model_admission *, bool *handled, qa_error *);
/* Failure normally leaves *out NULL. If checked cleanup rejects retirement,
 * *out retains the genuine partial owner: keep its borrowed option services
 * alive and retry qa_application_destroy before releasing them. */
bool qa_application_create(const qa_application_options *, qa_application **,
                           qa_error *);
/* Original startup ordinal, consumed by its actual published early source. */
bool qa_application_startup_command_seeded(const qa_application *, size_t ordinal);
size_t qa_application_startup_command_count(const qa_application *);
const char *qa_application_startup_command(const qa_application *, size_t ordinal);
bool qa_application_startup_command_pending(const qa_application *, size_t ordinal);
bool qa_application_startup_command_complete(qa_application *, size_t ordinal, qa_error *);
/* Destruction requires an idle application. Success consumes the public
 * handle. Retained launch snapshots may keep detached provider state and its
 * borrowed application services alive until their final release; callers must
 * not use the application again after a successful call. */
bool qa_application_destroy(qa_application *, qa_error *);
/* Retire all source callbacks while platform services are still available.
 * Success retains the shared authorities and console for platform teardown.
 * Failure retains this application; retry after outstanding owners release. */
bool qa_application_retire_sources(qa_application *, qa_error *);
/* Retire the actual Q2 primary server at a returned source boundary, retaining
 * the engine, configuration owner and frontend services for another launch. */
bool qa_application_stop_server(qa_application *, qa_actor_owner, qa_error *);
/* Return to the startup menu while retaining the engine and its installed
 * content, settings and platform services. */
bool qa_application_end_game(qa_application *, qa_error *);
/* A queued post-shutdown route retains its original selected launch until
 * the existing map owner has consumed that transient restart continuation. */
bool qa_application_server_restart_pending(const qa_application *);

qa_application_state qa_application_get_state(const qa_application *);
/* Borrowed fault detail. NULL means the application has not faulted. */
const qa_error *qa_application_error(const qa_application *);
void qa_application_request_stop(qa_application *);
bool qa_application_should_stop(const qa_application *);
/* The actual Quake source pause holds shared simulation and source clocks.
 * Host presentation and transport clocks remain owned by the frontend. */
bool qa_application_q1_paused(const qa_application *);
/* The actual driver reports one completed outer frame after its ordinary
 * output work and before draining map/restart intents. Source simulation or
 * restart settlement steps do not publish this revision. */
bool qa_application_complete_frame(qa_application *, qa_error *);
/* Consume pending client drops after command and transport callbacks return,
 * before sampling host slots. This does not advance simulation or Source time. */
bool qa_application_clients_drain(qa_application *, qa_error *);
/* Drain due source intents at the actual idle driver boundary, before any
 * controls, source stepping, or presentation owners begin their next frame. */
bool qa_application_prepare_frame(qa_application *, qa_error *);

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
/* Actual admitted map opening, including during real candidate routing. */
bool qa_application_map_origin_read(const qa_application *, qa_launch_resource_origin *);
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
/* Borrow actual live/constructor provider content independently of console
 * command generations. The provider retains this view until retirement. */
qa_vfs *qa_application_provider_files(qa_application *,qa_actor_owner);
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
    QA_APPLICATION_CONSOLE_Q3_UI,
    QA_APPLICATION_CONSOLE_Q1_GAME,
    QA_APPLICATION_CONSOLE_Q2_GAME,
    QA_APPLICATION_CONSOLE_CLIENT
} qa_application_console_kind;
typedef struct qa_application_console_scope {
    qa_actor_owner provider;
    qa_application_console_kind kind;
    uint32_t seat;
} qa_application_console_scope;
bool qa_application_startup_command_queue(qa_application *, size_t, qa_console *,
    const qa_command_context *, qa_error *);
bool qa_application_startup_command_queued_console(qa_application *, size_t,
    qa_console **, qa_error *);
bool qa_application_startup_console_queued(const qa_application *, const qa_console *);
/* Shared console aliases use the smallest provider instance/role/seat key. */
bool qa_application_console_scope_read(const qa_application *, const qa_console *,
                                        qa_application_console_scope *);
/* Instance text is borrowed until its attached provider retires. */
const char *qa_application_provider_instance(const qa_application *, qa_actor_owner);
/* Resolve the actual actor/configured actor/seat/default selection in this
 * snapshot using the application's live roster. The result borrows snapshot
 * custody; this query never constructs, attaches or enters its execution. */
const qa_launch_instance *qa_application_selected_instance(qa_application *,
    const qa_launch_snapshot *, qa_actor_id, qa_launch_role, const char *selector);
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
 * generation. Commands submitted outside a source turn are retained for its
 * actual admitted frame. A guest's nested locomotion command executes in its
 * active source turn. Callbacks may retire the actor; a failure after movement
 * starts faults the application because committed effects are never replayed. */
bool qa_application_control_admit(qa_application *, qa_actor_id,
                                  qa_vec3 view_angles, qa_error *);
bool qa_application_control_move(qa_application *, qa_actor_id,
                                 const qa_movement_command *, qa_error *);
/* A QuakeWorld packet's commands retain one packet sequence and their source
 * order. The next packet must advance that sequence. */
bool qa_application_control_commands(qa_application *, qa_actor_id,
    const qa_movement_command *, size_t count, qa_error *);
bool qa_application_control_qw_commands(qa_application *, qa_actor_id,
    const qa_movement_command *, size_t count, qa_error *);
/* Retain the physical NetQuake command until its genuine source actor turn. */
bool qa_application_control_nq_command(qa_application *, qa_actor_id,
    uint64_t tick_sequence, const qa_q1_command *, qa_error *);
/* Retain literal physical Q2 input independently of selected movement. */
bool qa_application_control_q2_command(qa_application *, qa_actor_id,
    uint64_t transport_sequence, const qa_movement_command *, qa_error *);
struct qa_unified_input;
struct qa_unified_movement;
bool qa_application_control_project_unified(const struct qa_unified_movement *,
    const qa_movement_state *, uint64_t sequence, qa_movement_command *, qa_error *);
bool qa_application_control_unified_command(qa_application *, qa_actor_id,
    const struct qa_unified_input *, qa_error *);
/* Preserve the received Q3 words independently of the transport sequence and
 * the actor's selected movement profile. */
bool qa_application_control_q3_command(qa_application *, qa_actor_id,
    uint64_t transport_sequence, const qa_q3_usercmd *, qa_error *);
/* Read the actual local CGAME values and selected Q3 arsenal request. A
 * different selected arsenal keeps the genuine CGAME sensitivity. Without a
 * CGAME or Q3 arsenal, present is false and the values remain unchanged. */
bool qa_application_q3_input_values_read(qa_application *, uint32_t seat, qa_actor_id,
    uint8_t *weapon, float *sensitivity, bool *present, qa_error *);
bool qa_application_control_read(const qa_application *, qa_actor_id,
                                 qa_application_control_view *);
/* Actual selected command recipient: physical Source-client admission precedes
 * actor execution ownership, including mixed character/movement selections. */
bool qa_application_control_source_read(qa_application *, qa_actor_id,
    qa_actor_owner *, const qa_cvars **, qa_error *);
bool qa_application_control_prediction_read(qa_application *, qa_actor_id,
    qa_application_control_prediction_configuration *, qa_error *);
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
uint64_t qa_application_protocol_events_generation(const qa_application *);
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
/* Consume retained match map intentions only after the frontend has drained
 * source events. Finish applies ordered post-map assignments after publication. */
bool qa_application_prepare_match_travel(qa_application *, qa_error *);
bool qa_application_finish_match_travel(qa_application *, uint64_t revision, qa_error *);
bool qa_application_travel_read(const qa_application *, qa_application_travel_view *);
/* The application owns departed world state and the genuine player carry.
 * A cached destination is imported by the same ordinary save reader. */
bool qa_application_campaign_depart(qa_application *, uint64_t revision, bool *needed, qa_error *);
bool qa_application_campaign_stage(qa_application *, const struct qa_save_image *, qa_error *);
qa_bytes qa_application_campaign_restore(const qa_application *);
bool qa_application_campaign_reenter(qa_application *candidate, qa_application *previous, qa_error *);
bool qa_application_commit_travel(qa_application *, uint64_t revision, qa_error *);
/* The retained MAP request completes only when its actual world publishes. */
bool qa_application_travel_publication_read(const qa_application *, uint64_t *revision);
bool qa_application_finish_travel_publication(qa_application *, uint64_t revision, qa_error *);
bool qa_application_complete_travel(qa_application *, uint64_t revision, qa_error *);
qa_string_id qa_application_nextserver(const qa_application *);
typedef struct qa_application_save_request {
    uint64_t world_generation, revision;
    bool fresh_entry, authored;
} qa_application_save_request;
/* Observed only after actual map/player publication and Source callbacks return. */
bool qa_application_save_request_read(const qa_application *, qa_application_save_request *);
bool qa_application_save_request_complete(qa_application *, const qa_application_save_request *, qa_error *);
bool qa_application_player_actor(const qa_application *, uint32_t seat, qa_actor_id *);
size_t qa_application_player_count(const qa_application *);

/* Elapsed monotonic duration is supplied by the outer platform loop or replay.
 * The application never reads a hidden wall clock for simulation. */
bool qa_application_advance(qa_application *, uint64_t elapsed_ns, qa_error *);

#endif
