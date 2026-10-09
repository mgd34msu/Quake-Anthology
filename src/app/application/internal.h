#ifndef QA_APPLICATION_INTERNAL_H
#define QA_APPLICATION_INTERNAL_H

#include "qa/arena.h"
#include "qa/application.h"
#include "qa/console_cvars_prepare.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/campaign_q1.h"
#include "qa/campaign_q1_sources.h"
#include "qa/equipment.h"
#include "qa/game_q1.h"
#include "qa/game_q1_maps.h"
#include "qa/game_q2.h"
#include "qa/game_q2_entities.h"
#include "qa/game_q2_items.h"
#include "qa/game_q2_monsters.h"
#include "qa/game_q2_player.h"
#include "qa/game_q3.h"
#include "qa/game_q3_map.h"
#include "qa/modes.h"
#include "qa/native_host.h"
#include "qa/q3_host.h"
#include "qa/qc.h"
#include "qa/qc_host.h"
#include "qa/qvm.h"
#include "qa/persistence_content.h"
#include "qa/save.h"
#include "qa/source_frame_time.h"
#include "qa/ui_preferences.h"
#include "unified_events.h"
#include "event_stream.h"

typedef enum application_operation {
    APPLICATION_IDLE,
    APPLICATION_DISCOVERING,
    APPLICATION_CONFIGURING,
    APPLICATION_PERSISTING,
    APPLICATION_ADVANCING,
    APPLICATION_DESTROYING
} application_operation;

typedef struct qa_combat_policy_admission qa_combat_policy_admission;
typedef struct qa_world_geometry_admission qa_world_geometry_admission;
struct application_native_client_role;
struct application_q3_mod_operations;
struct application_q3_components;

typedef enum application_provider_kind {
    APPLICATION_PROVIDER_Q1,
    APPLICATION_PROVIDER_Q2,
    APPLICATION_PROVIDER_Q3,
    APPLICATION_PROVIDER_QC,
    APPLICATION_PROVIDER_QVM,
    APPLICATION_PROVIDER_NATIVE
} application_provider_kind;

typedef struct application_provider {
    struct application_q2_recipient_binding *q2_recipient_binding;
    struct application_bots_npc *bots_npc;
    struct application_native_client_role *native_client_roles;
    struct application_native_q1_console *native_q1_console;
    struct application_native_q1_wire *native_q1_wire;
    /* Borrowed from native_restore_image between constructor and inverse. */
    qa_bytes native_q1_restore_game, native_q1_restore_npc;
    struct application_native_q2_console *native_q2_console;
    struct application_native_q3_console *native_q3_console;
    struct application_native_q3_remote_role *native_q3_remote_roles;
    struct application_native_q3_settings *native_q3_settings;
    struct application_native_q3_ipfilters *native_q3_ipfilters;
    struct application_native_q3_wire *native_q3_wire;
    struct application_q2_wire_capture *q2_wire_capture;
    struct application_q3_wire_capture *q3_wire_capture;
    struct application_native_q3_votes *native_q3_votes;
    struct application_native_q3_team_status *native_q3_team_status;
    struct qa_application *application;
    application_provider_kind kind;
    const qa_launch_instance *launch;
    qa_launch_instance_lease *launch_lease;
    const qa_product *product;
    qa_catalog *product_catalog;
    qa_actor_owner owner;
    qa_source_frame event_retirement_frame;
    bool event_retirement_frame_present;
    bool event_activation_deferred, event_activation_bound;
    qa_component component;
    qa_source_frame_time_binding frame_time;
    qa_cvar_handle sv_novis, q2_maxclients, q2_airaccelerate;
    qa_combat_policy policy;
    qa_q1_game_operation q1_lifetime;
    bool constructed;
    bool client_only_owned;
    bool attached;
    bool component_attached;
    bool policy_attached;
    bool close_pending;
    size_t hosted_video_leases;
    bool map_bound;
    struct application_provider *next_close;
    struct application_provider *previous_live, *next_live;
    qa_q1_level *q1_level;
    qa_q1_campaign_source *q1_campaign;
    uint32_t q1_server_flags;
    uint32_t q2_server_flags;
    union {
        qa_q1_game *q1;
        qa_q2_game *q2;
        qa_q3_game *q3;
        struct {
            qa_qc_program *program;
            qa_qc_instance *instance;
            qa_qc_game *game;
            struct application_qc_state *engine;
            struct application_qc_profile *qualified;
        } qc;
        struct {
            qa_q3_host *host;
            qa_qvm_image *image;
            qa_qvm *machine;
            struct application_q3_guest *engine;
        } qvm;
        struct {
            qa_q3_host *q3_host;
            qa_native_module *module;
            qa_native_host *host;
            struct application_q3_guest *engine;
            struct application_native_q2 *q2_engine;
        } native;
    } state;
} application_provider;

typedef struct application_provider_admission {
    application_provider *provider;
    application_provider *previous_activation;
    qa_component_admission *component;
    qa_combat_policy_admission *policy;
    bool constructed;
} application_provider_admission;

typedef struct application_motion_record {
    qa_actor_id actor;
    qa_body_state body;
    qa_vec3 view_angles, angular_kick;
    uint64_t hold_until_ns, revision;
    qa_builtin_motion_reason reason;
    bool active, force_view_angles, apply_angular_kick;
} application_motion_record;

typedef struct application_actor_routes {
    qa_actor_id actor;
    qa_actor_owner owner;
    qa_actor_definition definition;
    application_provider *source;
    application_provider *providers[QA_ROLE_COUNT];
    const qa_launch_instance *instances[QA_ROLE_COUNT];
    bool ready, resolving;
} application_actor_routes;

typedef struct application_control_record {
    struct qa_application *application;
    application_provider *cutscene_character;
    qa_actor_id actor;
    qa_movement_state state;
    qa_movement_profile profile;
    qa_application_movement_numeric numeric;
    qa_application_movement_numeric prediction_numeric;
    qa_movement_result result;
    qa_bounds standing_bounds, bounds;
    qa_movement_ground ground;
    qa_vec3 view_angles, command_angles, view_offset, saved_view_offset;
    qa_vec3 q2r_pml_origin;
    uint64_t command_sequence, command_angle_revision;
    uint32_t buttons, previous_buttons;
    int32_t water_level, water_type;
    float view_height, gravity_multiplier;
    qa_movement_mode player_mode;
    int32_t saved_mode;
    bool active, moving, retired, flight, cutscene, saved_damageable;
    bool saved_mode_valid, command_seen, guest_mode_valid;
    bool player_mode_set;
    int32_t guest_mode;
} application_control_record;

typedef struct application_shader_remap {
    qa_string_id original, replacement;
    uint64_t time_ns;
} application_shader_remap;

typedef struct application_q2_visual_record {
    qa_actor_id actor;
    qa_q2_visual visual;
    uint64_t revision;
    bool active;
} application_q2_visual_record;

typedef struct application_publication {
    struct application_publication *failed_next;
    bool failed_retained;
    const qa_launch_snapshot *previous;
    const qa_launch_snapshot *candidate;
    qa_cvars_edit *values;
    bool owns_values;
    void *resources;
    bool resources_consumed;
    bool source_retirement_started;
    bool sources_retired;
    bool gameplay_prepared;
    bool native_map_cut;
    uint32_t source_max_clients;
    bool source_capacity_prepared;
    application_provider **next;
    size_t next_count;
    application_provider **removed;
    size_t removed_count;
    application_provider_admission *admissions;
    size_t admission_count;
    qa_resource *map_resource;
    struct qa_map_sidecars *map_sidecars;
    qa_bsp_view map;
    qa_collision_geometry *geometry;
    qa_world *initial_world;
    qa_world_geometry_admission *geometry_admission;
    qa_entities entities;
    qa_modes *modes;
    qa_equipment *equipment;
    struct application_equipment_runtime *equipment_runtime;
    qa_bytes equipment_runtime_saved;
    struct application_q3_components *components;
    qa_mode_id *mode_ids;
    size_t mode_count;
    qa_mode_id primary_mode;
    application_provider *map_provider;
    struct application_player_travel *players;
    bool travel;
    bool restoring;
    bool physics_initialized;
    bool entities_parsed;
    bool published;
    struct application_supplies *supplies;
    struct application_startup_program_roster *programs;
} application_publication;

typedef struct application_campaign_travel {
    qa_campaign_location source, destination;
    qa_application_travel_view request;
    qa_campaign_visit *visit;
    struct application_player_travel *players;
} application_campaign_travel;

typedef struct application_feature_report {
    char site[96];
    qa_error error;
} application_feature_report;

struct qa_application {
    struct qa_application_client_preparation *client_preparation;
    application_publication *failed_publications;
    struct application_startup_flow *startup_flow;
    application_publication *startup_publication;
    application_provider *startup_preinit_provider;
    application_provider *startup_retiring_provider;
    struct application_startup_program *startup_program_owners;
    struct qa_application_engine_shutdown *engine_shutdown;
    application_provider *engine_shutdown_provider;
    const struct qa_application_startup_hooks *startup_hooks;
    bool dedicated;
    struct application_startup *startup;
    qa_q3_product_policy q3_product;
    const qa_q3_product_policy *q3_product_preparing;
    qa_cvars *cvars;
    qa_cvar_handle bot_minplayers;
    qa_ui_preference_handles ui_preference_handles;
    qa_console *console;
    const qa_native_runtime_config *native_runtime_config;
    qa_native_runtime *native_runtime;
    char *native_bootstrap;
    qa_native_process_resource_policy native_process_policy;
    void *guest_context;
    void *prompt_context;
    bool (*prompt_supported)(void *, qa_actor_id, bool *, qa_error *);
    qa_application_q3_services_fn q3_services;
    qa_application_q3_client_prepare_fn q3_client_prepare;
    qa_application_q3_component_scene_prepare_fn q3_component_scene_prepare;
    qa_application_q3_component_client_drop_fn q3_component_client_drop;
    qa_application_q3_client_registry_reference_fn q3_client_registry_reference;
    qa_application_q3_client_effect_fn q3_client_effect;
    qa_application_q3_campaign_command_fn q3_campaign_command;
    const struct qa_application_q3_round_services *q3_round_services;
    qa_application_ranking_effect_fn ranking_effect;
    char *ranking_game_key;
    struct application_rankings *ranked_source;
    qa_application_native_q2_services_fn native_q2_services;
    qa_application_model_admission_fn model_admission;
    qa_application_world_hook_fn world_change_ready;
    qa_application_world_hook_fn before_world_change;
    qa_application_world_hook_fn world_retired;
    void (*console_print)(void *, const qa_command_context *, const char *);
    qa_command_fallback console_forward;
    struct application_map_state *map_state;
    qa_campaign_unit *campaign_unit;
    application_campaign_travel *campaign_travel;
    struct application_player_roster *players;
    struct application_bots *bots;
    struct application_match_intents *match_intents;
    struct application_q3_world_restart *q3_world_restart;
    struct application_q1_original_save *q1_original_save;
    struct application_q2_original_save *q2_original_save;
    struct application_q3_campaign_launch *q3_campaign_launch;
    bool frame_preparing;
    bool source_shutdown_admitted;
    struct application_q1_signon *q1_signon;
    struct application_portals *portals;
    bool map_force_reload;
    char *content_root;
    char **install_roots;
    size_t install_root_count;
    char *user_root;
    qa_resource_pool *resources;
    qa_fs_root *user_files;
    qa_player_progress *progress;
    qa_rankings *rankings;
    qa_catalog *catalog;
    qa_application_content_graph *content_graph;
    qa_application_content_graph *capture_content_graph;
    qa_save_purpose capture_purpose;
    const struct qa_application_native_resource_refs *native_restore_resources;
    const struct qa_save_image *native_restore_image;
    /* Borrowed only while rebuilding a visited level from its current unit. */
    struct qa_application *native_restore_current;
    qa_session *session;
    qa_configuration *configuration;
    qa_world *world;
    struct qa_application_acoustics *acoustics;
    qa_combat *combat;
    qa_inventory *inventory;
    struct application_q3_mod_operations *mod_operations;
    qa_pickups *pickups;
    qa_targets *targets;
    qa_modes *modes;
    qa_mode_id *mode_ids;
    size_t mode_count;
    qa_equipment *equipment;
    struct application_equipment_runtime *equipment_runtime;
    struct application_q3_components *components;
    qa_physics *physics;
    qa_resource *map_resource;
    struct qa_map_sidecars *map_sidecars;
    qa_collision_geometry *geometry;
    application_provider **providers;
    size_t provider_count;
    application_provider *pending_close;
    application_provider *live_providers;
    size_t provider_states;
    qa_event_ring *event_ring;
    struct application_q2_audience_scratch *event_q2_capture;
    application_event_write *event_write;
    uint64_t event_local_cursor, event_peer_cursor;
    uint64_t protocol_events_generation;
    uint64_t simulation_event_sequence;
    uint64_t presentation_event_sequence;
    application_unified_persistent_event *unified_persistent;
    size_t unified_persistent_count, unified_persistent_capacity;
    uint64_t unified_persistent_revision;
    application_unified_event_owner *unified_event_owners;
    size_t unified_event_owner_count, unified_event_owner_capacity;
    uint64_t unified_event_owner_generation;
    application_unified_event_resource *unified_event_resources;
    size_t unified_event_resource_count, unified_event_resource_capacity;
    application_unified_event_registration *unified_event_registrations;
    size_t unified_event_registration_count, unified_event_registration_capacity;
    uint64_t unified_event_registration_revision;
    application_unified_world_text *unified_world_text;
    size_t unified_world_text_count, unified_world_text_capacity;
    uint64_t unified_world_text_revision, unified_world_text_map;
    application_motion_record *motion;
    uint32_t motion_capacity;
    application_actor_routes *actor_routes;
    uint32_t actor_route_capacity;
    const qa_launch_snapshot *actor_route_snapshot;
    application_control_record *controls;
    struct application_control_frames *control_frames;
    struct application_native_q2_scratch *native_baselines;
    qa_fs_root *baseline_write_root;
    uint32_t control_capacity;
    application_q2_visual_record *q2_visuals;
    uint32_t q2_visual_capacity;
    application_shader_remap *shader_remaps;
    size_t shader_remap_count, shader_remap_capacity;
    qa_builtin_random random;
    qa_string_id current_map;
    const qa_launch_snapshot *routing_snapshot;
    application_provider **routing_providers;
    size_t routing_provider_count;
    qa_mode_id primary_mode;
    uint64_t catalog_generation;
    uint64_t publication_generation;
    uint64_t command_generation, map_revision;
    uint64_t frame_revision;
    uint64_t snapshot_mutation;
    qa_product_id map_geometry, map_presentation;
    bool map_view_ready;
    bool q1_paused;
    bool q3_round_active;
    application_operation operation;
    qa_application_state state;
    bool discover_mods;
    bool physics_ready;
    bool primary_mode_ready;
    bool publication_started;
    bool destroy_requested;
    bool finalizing;
    qa_error publication_error;
    struct application_supplies *supplies;
    application_feature_report feature_reports[16];
    size_t feature_report_count;
    bool feature_report_overflow;
};

static inline void application_snapshot_mutated(qa_application *application)
{
    ++application->snapshot_mutation;
}

bool application_actor_released(void *, qa_session *, qa_actor_record,
                                qa_error *);
qa_combat_hooks application_combat_hooks(qa_application *);
bool application_force_death(void *, const qa_damage_request *, qa_error *);
bool application_source_force_death(qa_application *, const qa_damage_request *,
    int32_t final_health, qa_error *);
bool application_native_cheats_enabled(void *);
bool application_native_console_motion(void *, qa_actor_id, bool, qa_error *);
qa_command_result application_native_engine_fly(qa_application *, const qa_command_invocation *,
    const qa_command_context *, qa_error *);
bool application_native_q1_console_cheat(void *, qa_actor_id, const char *, bool *, qa_error *);
bool application_native_q3_console_print(void *, const char *, qa_error *);
bool application_native_grant_arsenal(void *, qa_actor_id, bool, bool *, qa_error *);
bool application_native_give_item(void *, qa_actor_id, size_t, const char *const *,
                                  bool *, qa_error *);
bool application_native_suicide(void *, qa_actor_id, qa_error *);
bool application_native_q3_award(void *, qa_actor_id, qa_q3_source_award, qa_error *);
bool application_native_horde_spawn_monster(void *, qa_mode_id, qa_string_id,
    qa_vec3, qa_vec3, qa_actor_id, qa_actor_id *, qa_error *);
bool application_native_horde_spawn_loot(void *, qa_mode_id,
    const qa_mode_loot_spawn *, qa_actor_id *, qa_error *);
bool application_native_horde_grant_loot(void *, qa_actor_id, qa_actor_id,
    bool *, qa_error *);
bool application_native_horde_head(void *, qa_actor_id, bool, qa_error *);
bool application_native_horde_alpha(void *, qa_actor_id, float, qa_error *);
float application_native_horde_random(void *, qa_mode_id);
bool application_native_horde_point(void *, qa_mode_id, qa_actor_id, qa_vec3 *,
    qa_vec3 *, qa_string_id *, uint32_t *, qa_error *);
bool application_native_horde_manager(void *, qa_mode_id, qa_actor_id,
    qa_string_id *, qa_actor_id *, qa_error *);
bool application_native_horde_restart(void *, qa_mode_id, uint32_t, qa_error *);
bool application_native_mode_select_weapon(void *, qa_actor_id, qa_item_id, qa_error *);
application_provider *application_mode_provider(qa_application *, qa_mode_id);
const qa_launch_mode *application_mode_choice(const qa_application *, size_t);
bool application_match_mode_source_owned(const application_provider *);
bool application_native_mode_emit(void *, qa_mode_id, const qa_builtin_event *, qa_error *);
bool application_native_mode_q3_clock(void *, qa_mode_id, int32_t *, qa_error *);
bool application_native_mode_q3_warmup_restart(void *, qa_mode_id, qa_error *);
bool application_native_q3_settings_register(application_provider *, qa_error *);
bool application_native_q3_settings_install(application_provider *, qa_error *);
bool application_native_q3_settings_reconnect(application_provider *, qa_error *);
bool application_native_mode_map_allowed(void *, qa_mode_id, qa_string_id);
bool application_native_mode_rogue_runes_claim(void *, qa_mode_id, bool *, qa_error *);
bool application_native_mode_rogue_runes_read(void *, qa_mode_id, qa_actor_id *, bool *);
bool application_native_mode_use_item(void *, qa_actor_id, qa_item_id, qa_error *);
bool application_native_mode_select_grapple(void *, qa_actor_id, qa_error *);
bool application_native_mode_character_frame(void *, qa_actor_id, int32_t *);
bool application_native_mode_body_armor(void *, qa_mode_id, qa_actor_id, qa_error *);
bool application_native_mode_quad(void *, qa_mode_id, qa_actor_id, qa_game_family, uint64_t, qa_error *);
bool application_native_mode_team_equipment(void *, qa_actor_id, qa_item_id *, uint64_t *, qa_error *);
bool application_native_mode_drop_arsenal(void *, qa_actor_id, bool, qa_error *);
bool application_native_mode_visible(void *, qa_actor_id, qa_actor_id, bool);
qa_builtin_services application_builtin_services(qa_application *, qa_world *,
                                                 qa_physics *);
qa_target_options application_target_options(qa_application *);
qa_physics_services application_physics_services(qa_application *);
void application_map_dispose(qa_application *);
void application_map_publication_dispose(application_publication *);
bool application_players_advance(qa_application *, qa_error *);
bool application_player_source_actor(const qa_application *, qa_actor_id, qa_actor_id *);
bool application_bots_publish(qa_application *, const qa_launch_choices *,
                               const qa_bsp_view *, const qa_entities *, qa_error *);
bool application_bots_prepare(qa_application *, const qa_launch_choices *,
                               const qa_bsp_view *, const qa_entities *, qa_error *);
bool application_bots_native_q3_initialize(application_provider *, qa_error *);
bool application_bots_native_q3_connect(application_provider *, qa_actor_id,
    bool restart, bool *accepted, qa_error *);
bool application_native_q3_match_bots_end(application_provider *, qa_error *);
bool application_bots_test_aas(application_provider *, qa_vec3, qa_error *);
qa_bot_runtime *application_bots_runtime(qa_application *);
bool application_bots_frame_at(qa_application *, const qa_source_frame *, size_t,
                                uint64_t host_ns, qa_error *);
bool application_bots_actor_released(qa_application *, qa_actor_record, qa_error *);
bool application_bots_destroy(qa_application *, qa_error *);
bool application_bots_can_destroy(const qa_application *);
bool application_native_q3_mode_frame_owned(qa_application *, qa_mode_id);
bool application_emit_q2_player(application_provider *, const qa_q2_player_event *, qa_error *);
bool application_emit_protocol(application_provider *, const qa_application_protocol_event *, qa_error *);
bool application_map_server_command(application_provider *, qa_string_id, qa_error *);
bool application_guest_spawn_map(application_provider *, const qa_bsp_view *,
                                  const qa_entities *, qa_string_id, qa_string_id, qa_error *);
bool application_construct_qc(qa_application *, application_provider *, qa_world *,
                               const qa_product *, const qa_launch_choices *, qa_error *);
bool application_qc_spawn_map(application_provider *, const qa_bsp_view *, const qa_entities *,
                              qa_string_id, qa_string_id, qa_error *);
bool application_qc_initialize_map(application_provider *, const qa_bsp_view *,
                                   const qa_entities *, qa_string_id, qa_string_id, qa_error *);
bool application_qc_deconstruct(application_provider *, qa_error *);
bool application_qc_actor_released(application_provider *, qa_actor_record, qa_error *);
bool application_qc_physics_read(application_provider *, qa_actor_id, qa_physics_properties *);
bool application_qc_physics_write(application_provider *, qa_actor_id,
                                  const qa_physics_properties *, qa_error *);
bool application_qc_touch(application_provider *, const qa_touch_contact *, qa_error *);
bool application_qc_blocked(application_provider *, qa_actor_id, qa_actor_id, qa_error *);
bool application_qc_pusher_think(application_provider *, qa_actor_id,
                                 const qa_source_frame *, qa_error *);
bool application_qc_water_transition(application_provider *, qa_actor_id, qa_error *);
bool application_qc_actor_traits(application_provider *, qa_actor_id, qa_builtin_actor_traits *);
bool application_qc_bind_player(application_provider *, uint32_t, uint32_t,
                                qa_actor_id, const char *, bool, bool,
                                bool primary_character, qa_error *);
bool application_qc_player_map_ready(application_provider *, bool primary_character,
                                      bool source_map_owned, qa_error *);
bool application_qc_player_roster_ready(application_provider *,
                                        const qa_launch_choices *, qa_error *);
bool application_qc_change_parms(application_provider *, qa_error *);
bool application_qc_client_userinfo(application_provider *, qa_actor_id, qa_error *);
bool application_qc_disconnect_player(application_provider *, qa_actor_id, qa_error *);
bool application_qc_player_command(application_provider *, qa_actor_id,
                                    const qa_movement_command *, qa_error *);
bool application_qc_qualify(application_provider *, qa_error *);
void application_qc_release_qualification(application_provider *);
bool application_qc_input(application_provider *, qa_actor_id,
                           qa_movement_command *, bool before,
                           bool movement_slice, uint64_t elapsed_ns, qa_error *);
bool application_qc_input_abort(application_provider *, qa_actor_id,
                                bool movement_slice, qa_error *);
bool application_qc_input_idle(const application_provider *);
bool application_qc_console_command(application_provider *, qa_actor_id, const char *,
                                      bool client_command, bool *handled, qa_error *);
qa_console *application_qc_console(application_provider *);
bool application_q3_guest_actor_client(application_provider *, qa_actor_id, uint32_t *);
bool application_guest_weapon_read(application_provider *, qa_actor_id, qa_item_id *, qa_error *);
bool application_guest_inventory_restore_finish(application_provider *, qa_error *);
bool application_native_q2_prepare_restore(application_provider *, qa_error *);
bool application_native_q2_restore_finish(application_provider *, qa_error *);
bool application_guest_bots_admit(application_provider *, qa_error *);
bool application_guest_clients_drain(application_provider *, qa_error *);
bool application_guest_console_at(application_provider *, size_t, qa_console **,
                                    qa_cvars **, qa_command_context *);

bool application_guest_frontend_rebind_ready(application_provider *, const qa_scene_frame *,
                                               void *, qa_error *);
void application_guest_frontend_rebind(application_provider *, qa_scene_frame *, void *, void *);
qa_command_result application_command_fallback(void *, const qa_command_invocation *, qa_error *);
bool application_command_capture(void *, const qa_command_context *, qa_command_context *, qa_error *);
bool application_command_active(void *, const qa_command_context *);
bool application_construct_q3_guest(qa_application *, application_provider *, qa_world *,
                                     const qa_product *, const qa_launch_choices *, qa_error *);
bool application_q3_guest_spawn_map(application_provider *, const qa_bsp_view *, const qa_entities *,
                                    qa_string_id, qa_string_id, qa_error *);
bool application_q3_guest_deconstruct(application_provider *, qa_error *);
bool application_q3_guest_actor_released(application_provider *, qa_actor_record, qa_error *);
bool application_q3_guest_client_connect(application_provider *, uint32_t,
                                         qa_actor_id, const char *, bool, bool, bool *, qa_error *);
bool application_q3_guest_client_begin(application_provider *, uint32_t, qa_error *);
bool application_q3_guest_client_userinfo(application_provider *, uint32_t,
                                          const char *, qa_error *);
bool application_q3_guest_client_disconnect(application_provider *, uint32_t, qa_error *);
bool application_q3_guest_client_think(application_provider *, uint32_t,
                                      const qa_q3_usercmd *, qa_error *);
bool application_q3_guest_client_command(application_provider *, uint32_t,
                                         const char *, qa_error *);
bool application_q3_guest_client_command_vector(application_provider *, qa_actor_id,
    const char *const *, size_t, qa_error *);
bool application_q3_guest_selected_respawn(application_provider *, qa_actor_id, qa_error *);
bool application_q3_native_deathmatch_destination(application_provider *, qa_actor_id,
    qa_vec3 *, qa_vec3 *, qa_error *);
bool application_q3_guest_publish_snapshot(application_provider *, uint32_t,
                                           const qa_q3_snapshot *, int32_t, qa_error *);
bool application_q3_guest_role_loading(const application_provider *, qa_qvm_role, uint32_t);
bool application_q3_guest_role_add(application_provider *, qa_qvm_role, uint32_t,
                                   const char *, qa_error *);
bool application_q3_guest_role_initialize(application_provider *, qa_qvm_role,
                                          uint32_t, int32_t, int32_t, int32_t, bool, qa_error *);
bool application_q3_guest_role_call(application_provider *, qa_qvm_role, uint32_t,
                                    int32_t, const int32_t *, size_t, int32_t *, qa_error *);
bool application_q3_guest_retire_map(application_provider *, qa_error *);
bool application_q3_guest_idle(const application_provider *);
bool application_q3_guest_client_gamestate(application_provider *, uint32_t,
                                           const qa_q3_gamestate **, qa_error *);
const qa_q3_gamestate *application_q3_guest_server_gamestate(application_provider *);
bool application_q3_guest_console_command(application_provider *, const char *,
                                          bool *, qa_error *);
bool application_q3_guest_role_command(application_provider *, qa_qvm_role,
                                       uint32_t, int32_t, const char *, bool *, qa_error *);
bool application_guests_idle(const qa_application *);
struct qa_application_language_ticket;
bool application_guests_languages_idle(const qa_application *,
    const struct qa_application_language_ticket *const *, size_t);
bool application_q3_guest_services(qa_application *, application_provider *, qa_qvm_role,
                                    uint32_t, uint64_t service_owner, qa_q3_host_options *, qa_error *);
bool application_q3_guest_native_options(qa_application *, application_provider *,
                                          qa_qvm_role, uint32_t,
                                          qa_native_host_instance_options *, qa_error *);
bool application_console_create(qa_application *, qa_error *);
void application_console_print(void *,const qa_command_context *,const char *);
bool application_emit(void *, const qa_builtin_event *, qa_error *);
bool application_q1_music_cue(qa_application *, bool fresh, qa_error *);
bool application_record_motion_change(qa_application *, qa_actor_id,
                                      const qa_builtin_motion_change *,
                                      qa_error *);
bool application_emit_q2_map(application_provider *, const qa_q2_map_event *,
                             qa_error *);
bool application_emit_q3_map(application_provider *, const qa_q3_map_event *,
                             qa_error *);
bool application_record_achievement(application_provider *, qa_actor_id,
                                    qa_string_id, qa_error *);
bool application_record_level(application_provider *, qa_string_id,
                              qa_error *);
bool application_provider_prepare(qa_application *, const qa_launch_instance *,
                                  application_provider **, qa_error *);
bool application_instance_configuration(void *, const qa_launch_instance *,
                                        const qa_launch_choices *,
                                        qa_buffer *, qa_error *);
qa_q1_program application_q1_program(const char *);
bool application_provider_construct(qa_application *, application_provider *,
                                    qa_world *, qa_catalog *, const qa_product *,
                                    const qa_launch_choices *, qa_error *);
struct qa_save_record;
bool application_provider_construct_q3_restored(qa_application *, application_provider *,
    qa_world *, qa_catalog *, const qa_product *, const qa_launch_choices *,
    const struct qa_save_record *, qa_error *);
bool application_provider_deconstruct(application_provider *, qa_error *);
bool application_provider_actor_released(application_provider *, qa_actor_record,
                                         qa_error *);
bool application_provider_close(qa_application *, application_provider *, qa_error *);
void application_provider_release(qa_application *, application_provider *);
bool application_drain_provider_closes(qa_application *, qa_error *);
bool application_finalize(qa_application *, qa_error *);
bool application_acoustics_idle(const qa_application *);
bool application_control_ensure(qa_application *, qa_actor_id, qa_vec3,
                                application_control_record **, qa_error *);
bool application_control_cutscene(qa_application *, qa_actor_id, qa_vec3,
                                  qa_vec3, qa_vec3, qa_error *);
bool application_control_set_angles(qa_application *, qa_actor_id, qa_vec3, qa_error *);
bool application_control_motion_changed(qa_application *, qa_actor_id,
                                        const qa_builtin_motion_change *,
                                        qa_error *);
bool application_control_source_spawn(qa_application *, qa_actor_id,
                                      qa_vec3 view_angles, qa_error *);
bool application_control_spawn_reset(qa_application *, qa_actor_id, bool spectator, qa_error *);
bool application_control_death(qa_application *, qa_actor_id, qa_error *);
bool application_control_gravity(qa_application *, qa_actor_id, float scale, qa_error *);
bool application_control_water_read(const qa_application *, qa_actor_id,
    int32_t *water_type, int32_t *water_level, qa_error *);
bool application_control_numeric_current(qa_application *, qa_actor_id,
    const qa_application_movement_numeric *, qa_error *);
bool application_control_prediction_numeric_current(qa_application *, qa_actor_id,
    const qa_application_movement_numeric *, qa_error *);
bool application_controlled(const qa_application *, qa_actor_id);
bool application_control_intermission(const qa_movement_state *);
bool application_source_intermission_read(application_provider *, bool *, qa_error *);
bool application_control_player_mode(qa_application *, qa_actor_id,
                                      qa_movement_mode, bool spectator, qa_error *);
bool application_control_toggle_motion(qa_application *, qa_actor_id,
    qa_physics_motion, bool spectator, bool *enabled, qa_error *);
bool application_arsenal_source_actor(void *, qa_session *, qa_actor_id,
                                      const qa_source_frame *, qa_error *);
bool application_q2_weapon_input(void *, qa_actor_id,
                                  qa_q2_weapon_input *, qa_error *);
bool application_q2_weapon_selected(void *, qa_actor_id);
bool application_q2_character_weapon(void *, qa_actor_id,
                                      qa_q2_character_weapon *, qa_error *);
bool application_control_physics_read(const qa_application *, qa_actor_id,
                                      qa_physics_properties *);
bool application_control_physics_write(qa_application *, qa_actor_id,
                                       const qa_physics_properties *, qa_error *);
bool application_control_velocity(qa_application *, qa_actor_id, qa_vec3, qa_error *);
application_provider *application_world_provider(qa_application *, qa_launch_role,
                                                  const char *);
application_provider *application_actor_source_provider(qa_application *, qa_actor_id);
application_provider *application_provider_for(qa_application *, qa_actor_id,
                                               qa_launch_role, const char *selector);
void application_actor_routes_clear(qa_application *);
void application_actor_routes_invalidate(qa_application *, qa_actor_id);
void application_actor_routes_bind(qa_application *, qa_actor_id);
bool application_composition_create(qa_application *, qa_error *);
bool application_composition_destroy(qa_application *, qa_error *);
bool application_apply(qa_application *, const qa_launch_draft *, qa_error *);
bool application_q1_pause_set(qa_application *, application_provider *, bool, qa_error *);
uint64_t application_frame_revision(const qa_application *);
bool application_monster_admit(void *, qa_actor_id, const qa_authored_monster *, qa_error *);
bool application_monster_mission(void *, qa_actor_owner, qa_monster_mission *, qa_error *);
bool application_map_prepare(qa_application *, application_publication *, qa_error *);
bool application_map_publish(qa_application *, application_publication *, qa_error *);
bool application_match_prepare(qa_application *, application_publication *, qa_error *);
bool application_match_prepare_modes(qa_application *, application_publication *, qa_error *);
bool application_match_prepare_equipment(qa_application *, application_publication *, qa_error *);
void application_publication_dispose(qa_application *, application_publication *);
bool application_publication_retry_cleanup(qa_application *, qa_error *);
bool application_publication_prepare(qa_application *,
                                     const qa_launch_snapshot *,
                                     const qa_launch_snapshot *, void **,
                                     qa_error *);
void application_publication_rollback(qa_application *, void *);
bool application_publication_retire(qa_application *, qa_error *);
void application_publication_publish(qa_application *,
                                     const qa_launch_snapshot *,
                                     const qa_launch_snapshot *, void *);

bool application_fail(qa_error *, qa_status, const char *);
void application_fault(qa_application *, const qa_error *);

#endif
