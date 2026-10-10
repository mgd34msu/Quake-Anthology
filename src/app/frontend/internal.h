#ifndef QA_FRONTEND_INTERNAL_H
#define QA_FRONTEND_INTERNAL_H
#include "qa/frontend.h"
#include "world_scratch.h"
#include "qa/application_visual_visibility.h"
#include "legacy_render_policy.h"
#include "qa/audio.h"
#include "qa/console_draw.h"
#include "qa/console_io.h"
#include "qa/console_seat.h"
#include "qa/hud.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/hud_wheel.h"
#include "qa/input_platform.h"
#include "qa/material.h"
#include "qa/render_cpu.h"
#include "qa/render_gl.h"
#include "qa/q3_presentation.h"
#include "qa/q3_key.h"
#include "qa/application_q3_client.h"
#include "../../presentation/q3_native/player_state.h"
#include "qa/persistence_content.h"
#include "qa/tools.h"
#include "qa/http.h"
#include "qa/llm.h"
#include "qa/recovery.h"
#include "qa/native_runtime.h"
#include "qa/native_resource_inventory.h"
#include "qa/save.h"
#include <stdlib.h>
#include <string.h>

static inline bool frontend_profiler_end(qa_profiler *profiler, bool ok, qa_error *error)
{
    qa_error cleanup = {0};
    bool retired = qa_profiler_pop(profiler, &cleanup);
    if (ok && !retired && error) *error = cleanup;
    return ok && retired;
}

enum { FRONTEND_HOME = 1, FRONTEND_LIBRARY, FRONTEND_MODS, FRONTEND_SETTINGS,
    FRONTEND_RANKINGS, FRONTEND_ASSISTANCE, FRONTEND_BINDINGS, FRONTEND_PLAYER_SOURCES,
    FRONTEND_OPTIONS = 210, FRONTEND_CONTENT_LIBRARY, FRONTEND_DISPLAY, FRONTEND_SOUND,
    FRONTEND_CONTROLS, FRONTEND_ALL_OPTIONS, FRONTEND_GRAPHICS, FRONTEND_NETWORK_OPTIONS,
    FRONTEND_LANGUAGE, FRONTEND_BINDINGS_CONFLICT, FRONTEND_BINDINGS_RESET, FRONTEND_LOAD,
    FRONTEND_SAVE, FRONTEND_SAVE_NAME, FRONTEND_SAVE_OVERWRITE, FRONTEND_GYRO,
    FRONTEND_AUDIO_OPTIONS, FRONTEND_GAMEPLAY_RESET, FRONTEND_MATCH, FRONTEND_Q1_HELP };
typedef struct qa_frontend_tools qa_frontend_tools;
typedef struct qa_frontend_network qa_frontend_network;
bool frontend_network_content_visit(const qa_frontend *,const qa_application *,const qa_application_content_visitor *,qa_error *);
bool frontend_source_client_registry_reference(void *,const qa_cvars *,const char **source_instance,uint32_t *,bool *,qa_error *);
typedef struct frontend_source frontend_source;
typedef struct frontend_remap frontend_remap;
typedef struct frontend_visual_owner frontend_visual_owner;
typedef struct frontend_native_q2 frontend_native_q2;
typedef struct frontend_native_q3 frontend_native_q3;
typedef struct frontend_remote_q3 frontend_remote_q3;
typedef struct frontend_remote_q2 frontend_remote_q2;
typedef struct frontend_qc_rerelease frontend_qc_rerelease;
bool frontend_native_q2_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
bool frontend_native_q2_callbacks_idle(const qa_frontend *);
void frontend_native_q2_rebind(qa_frontend *, qa_frontend *);
typedef struct frontend_event_state frontend_event_state;
typedef struct frontend_particle_state frontend_particle_state;
typedef struct frontend_capture frontend_capture;
typedef struct frontend_resource_inventory frontend_resource_inventory;
typedef struct frontend_save_commands frontend_save_commands;
typedef struct frontend_campaign frontend_campaign;
typedef struct frontend_ui_features frontend_ui_features;
typedef struct frontend_settings_menu frontend_settings_menu;
typedef struct frontend_save_menu frontend_save_menu;
typedef struct frontend_content_library_menu frontend_content_library_menu;
typedef struct frontend_keys frontend_keys;
typedef struct frontend_restart frontend_restart;
typedef struct frontend_input_settings frontend_input_settings;
typedef struct frontend_input_shutdown frontend_input_shutdown;
typedef struct frontend_shutdown frontend_shutdown;
typedef struct qa_application_engine_shutdown qa_application_engine_shutdown;
typedef struct frontend_equipment frontend_equipment;
typedef struct frontend_equipment_q3 frontend_equipment_q3;
typedef struct frontend_equipment_gear frontend_equipment_gear;
typedef struct frontend_selected_character frontend_selected_character;
typedef struct frontend_selected_effects frontend_selected_effects;
typedef struct frontend_config_store frontend_config_store;
typedef struct frontend_client_registry frontend_client_registry;
typedef struct frontend_client_registry_import frontend_client_registry_import;
typedef struct frontend_cinematic frontend_cinematic;
typedef struct frontend_system_cinematic frontend_system_cinematic;
typedef struct frontend_music_sources frontend_music_sources;
typedef struct frontend_view_settings frontend_view_settings;
typedef struct frontend_equipment_events frontend_equipment_events;
typedef struct qa_application_q3_round_cut qa_application_q3_round_cut;
typedef struct frontend_audio_identity { qa_actor_id actor; uint64_t id; bool retired; } frontend_audio_identity;
void frontend_audio_engine_options(qa_frontend *,qa_audio_engine_options *);
bool frontend_audio_engine_options_ready(const qa_frontend *);
bool frontend_startup_replay(qa_frontend *,qa_error *);
bool frontend_startup_queued(const qa_frontend *);
void frontend_source_audio_stopped(qa_frontend *);
bool frontend_source_audio_view(const qa_frontend *,const qa_audio_asset *,qa_vfs **);
typedef struct frontend_q1_view_motion {
    double seconds, bob_seconds, face_until;
    float bob, old_z, damage_time, damage_roll, damage_pitch;
    int32_t damage_percent, bonus_percent;
    qa_vec3 damage_color;
    bool initialized;
} frontend_q1_view_motion;
typedef struct frontend_q1_view_pose {
    qa_vec3 origin, angles, gun_origin, gun_angles;
} frontend_q1_view_pose;
typedef struct frontend_pause_cvars {
    qa_cvars *cvars;
    qa_cvar_handle cl_paused, sv_paused;
} frontend_pause_cvars;
typedef struct frontend_seat {
    struct qa_frontend *frontend;
    frontend_pause_cvars pause_cvars;
    uint32_t id;
    qa_input_seat *input;
    qa_seat_console *console;
    qa_ui *ui;
    qa_ui_id command_menu;
    bool command_game_menu;
    qa_command_context command_menu_context;
    bool player_sources_registered;
    qa_hud *hud;
    qa_hud_wheel *wheel;
    qa_hud_wheel_item *wheel_items;
    qa_item_definition *wheel_definitions;
    size_t wheel_capacity;
    char *wheel_labels;
    size_t wheel_label_capacity;
    qa_ui_library *library;
    struct frontend_startup_selection *startup_selection;
    struct frontend_startup_arena *startup_arena;
    struct frontend_host_menu *host_menu;
    char *server_profile_path;
    struct frontend_content_library_services *library_services;
    qa_ui_mods *mods;
    struct frontend_startup_server_browser *server_browser;
    struct frontend_startup_rotation *rotation_menu;
    struct frontend_startup_downloads *downloads_menu;
    struct frontend_source_prompt *source_prompt;
    struct frontend_q1_help *q1_help;
    qa_ui_rankings *rankings;
    qa_ui_llm *assistance;
    frontend_save_menu *save_menu;
    frontend_content_library_menu *content_library;
    frontend_settings_menu *settings_menu;
    char *clipboard;
    qa_font_selection fonts;
    qa_input_command_builder builder;
    qa_actor_id actor;
    uint64_t sequence, command_angle_revision;
    uint64_t client_clock_ns, client_frame_ns;
    qa_ui_control controls[96];
    qa_ui_row *binding_rows;
    char *binding_labels, *binding_command;
    size_t binding_capacity, binding_label_capacity, selected_binding;
    qa_physical_input pending_binding;
    bool binding_conflict;
    char binding_query[321];
    char *binding_selected;
    bool binding_reassign;
    qa_physical_input binding_previous;
    char binding_status[256];
    qa_display_backend menu_renderer;
    char menu_width[5], menu_height[5];
    bool menu_display_initialized;
    uint32_t menu_controller_seat;
    size_t selected_setting;
    const char **player_source_titles;
    qa_product_id *player_source_products;
    size_t player_source_capacity, player_source_count;
    char player_source_labels[QA_INPUT_LOCAL_SEATS][3][32];
    char setting_value[1024];
    frontend_q1_view_motion q1_view_motion;
    frontend_q1_view_pose q1_view_pose;
    qa_vec4 q1_blend;
    qa_actor_id q1_view_actor;
    bool q1_view_ready, q1_chase;
    q3n_damage_feedback q3_damage;
    qa_actor_id q3_damage_actor;
    qa_actor_owner q3_damage_provider;
    uint64_t q3_damage_map;
    uint32_t q3_damage_spawn;
    int32_t q3_damage_event, q3_damage_time;
    qa_actor_id q2_actor;
    qa_q2_player_view q2_view;
    qa_application_native_q2_hud q2_hud;
    qa_actor_owner q2_hud_font_provider;
    qa_font_library *q2_hud_fonts;
    const qa_font *q2_hud_classic;
    bool q2_hud_active;
    qa_hud_value q1_monsters;
    char q1_monster_label[80];
    char *q2_help_text[2];
    const char *q2_help_lines[2];
    qa_hud_score *q2_scores;
    char *q2_score_names;
    size_t q2_score_count;
    bool q2_view_ready, q2_help, q2_inventory;
    bool chat_team;
} frontend_seat;
typedef struct frontend_engine_cvar_handles {
    uint64_t view_identity;
    qa_cvar_handle timedemo, com_maxfps, r_maxfps;
    qa_cvar_handle cl_avidemo, cl_forceavidemo, timescale;
    qa_cvar_handle s_volume, scr_centertime, s_geometry_acoustics;
    qa_cvar_handle cl_rerelease_effects;
    qa_cvar_handle gl_farclip, r_gamma, con_notifytime, r_drawentities;
    qa_cvar_handle filterban, public_server, dedicated;
    qa_hud_cvar_handles hud;
    qa_cvar_handle cg_gunX, cg_gunY, cg_gunZ, cg_gun_frame;
    qa_cvar_handle r_maxpolys, r_maxpolyverts, r_textureMode, r_drawBuffer;
    qa_cvar_handle r_nobind, r_uifullscreen, r_detailtextures, r_vertexLight;
    qa_cvar_handle r_ignoreFastPath, r_allowExtensions, r_ext_multitexture, r_ext_texture_env_add;
    qa_cvar_handle r_znear, r_lodscale, r_lodbias, r_lodCurveError;
    qa_cvar_handle r_railCoreWidth, r_railWidth, r_railSegmentLength, r_drawworld;
    qa_cvar_handle r_nocull, r_novis, r_nocurves, r_facePlaneCull;
    qa_cvar_handle r_lockpvs, r_noportals, r_portalOnly, r_fastsky;
    qa_cvar_handle r_dynamiclight, r_ambientScale, r_directedScale, r_norefresh;
    qa_cvar_handle r_showcluster, r_debugSort, r_showtris, r_shownormals;
    qa_cvar_handle r_showsky, r_offsetfactor, r_offsetunits, r_lightmap;
    qa_cvar_handle r_skipBackEnd, r_clear, r_subdivisions, r_mapOverBrightBits;
    qa_cvar_handle r_fullbright, r_finish, r_showImages, r_speeds;
    qa_cvar_handle r_measureOverdraw, r_shadows;
    qa_cvar_handle r_ignorehwgamma, r_intensity, r_overBrightBits;
    qa_cvar_handle r_picmip, r_roundImagesDown, r_simpleMipMaps;
    qa_cvar_handle r_colorMipLevels, r_texturebits, r_ext_compressed_textures;
    qa_cvar_handle r_primitives, r_ext_compiled_vertex_array;
    qa_cvar_handle resource_policy[15];
    frontend_legacy_cvar_handles legacy;
} frontend_engine_cvar_handles;
struct qa_frontend {
    qa_frontend_options options;
    qa_frontend_tools *tools;
    qa_frontend_network *network;
    struct frontend_network_server_lease *network_server_leases;
    frontend_source *sources;
    frontend_remap *remaps;
    frontend_visual_owner *visuals;
    frontend_native_q2 *native_q2;
    frontend_native_q3 *native_q3;
    struct frontend_video_guests *video_guests;
    frontend_remote_q3 *remote_q3;
    struct frontend_remote_q3_initial *initial_resources;
    frontend_remote_q2 *remote_q2;
    struct frontend_remote_q1 *remote_q1;
    struct frontend_remote_unified *remote_unified;
    struct frontend_client_source *client_sources;
    struct frontend_component_scene *component_scenes;
    struct frontend_component_scene_restore *component_scene_restores;
    frontend_qc_rerelease *qc_rerelease;
    qa_native_runtime *native_runtime;
    qa_native_resource_inventory *native_resource_inventory_pending;
    qa_save_image *save_image_pending;
    struct frontend_renderer_materials *renderer_materials;
    struct frontend_renderer_worlds *renderer_worlds;
    struct frontend_renderer_registries *renderer_registries;
    frontend_event_state *events;
    frontend_particle_state *particles;
    qa_application_q3_round_cut *round;
    frontend_capture *capture;
    frontend_resource_inventory *resource_inventory;
    struct frontend_live_resource_policy *live_resource_policy;
    frontend_save_commands *save_commands;
    frontend_campaign *campaign;
    frontend_music_sources *music_sources;
    frontend_view_settings *view_settings;
    struct frontend_material_movies *material_movie_owners;
    struct frontend_root_resources *root_resources, *root_resources_pending;
    struct frontend_q1_sky *q1_sky;
    struct frontend_qc_messages *qc_messages;
    struct frontend_q3_color *source_color;
    struct qa_q3_cinematic_handles *source_cinematics;
    struct frontend_cinematic_roles *cinematic_roles;
    frontend_view_settings *view_restore_pending;
    struct frontend_global_settings_storage *global_settings_storage;
    frontend_ui_features *ui_features;
    frontend_keys *keys;
    frontend_restart *restart;
    frontend_input_settings *input_settings;
    struct frontend_settings_devices *settings_devices;
    struct frontend_startup_launch *startup_launch;
    qa_lobbies *lobbies;
    struct frontend_local_lobby *local_lobby;
    bool lobbies_owned;
    char *network_connect_text;
    struct frontend_demo_dispatch *demos;
    frontend_input_shutdown *input_shutdown;
    frontend_shutdown *shutdown;
    struct frontend_constructor *constructor;
    qa_launch_draft *player_source_draft;
    const qa_launch_snapshot *player_source_publication;
    uint64_t player_source_generation;
    qa_actor_id player_source_actor;
    uint32_t player_source_physical, player_source_logical;
    qa_actor_owner server_stop_owner;
    uint64_t server_stop_generation;
    bool server_stopped, server_stop_follow_map;
    qa_application_engine_shutdown *engine_shutdown;
    frontend_equipment *equipment;
    frontend_equipment_q3 *equipment_q3;
    frontend_equipment_gear *equipment_gear;
    frontend_selected_character *selected_characters;
    frontend_selected_effects *selected_effects;
    frontend_config_store *config_store;
    frontend_client_registry *client_registries;
    frontend_client_registry_import *client_registry_import;
    frontend_cinematic *cinematic;
    frontend_system_cinematic *system_cinematics;
    frontend_equipment_events *gear_events;
    qa_catalog *input_catalog;
    uint64_t next_source_id;
    bool source_restoring;
    bool archive_enabled, archive_saved;
    frontend_audio_identity *audio_ids;
    size_t audio_id_count, audio_id_capacity;
    uint64_t next_audio_id;
    qa_application *application;
    frontend_engine_cvar_handles engine_cvars;
    frontend_pause_cvars pause_cvars;
    qa_display *display;
    qa_cpu_renderer *cpu;
    qa_gl_renderer *gl;
    qa_input_platform *input;
    qa_input_console *input_commands;
    qa_dedicated_console *terminal;
    qa_platform_events *platform_events;
    qa_vfs *ui_mounts, *mounts;
    qa_vfs *input_config;
    qa_product_id input_product;
    char *default_user_root;
    qa_scene_resources *ui_images, *images;
    qa_scene_image *console_background;
    qa_ui_art menu_art;
    qa_font_library *fonts;
    const qa_font *classic, *primary;
    qa_material_order *order;
    qa_material_library *materials;
    qa_scene_world *scene_world;
    frontend_world_scratch *world_scratch;
    qa_application_visual_visibility *(*visual_visibility)[2];
    qa_resource *map_resource;
    qa_scene_frame frame;
    qa_audio_engine *audio;
    qa_audio_device *device;
    qa_audio_output_format audio_output_format;
    qa_audio_bank *sounds;
    frontend_seat *seats;
    uint64_t time_ns, wall_time_ns, frame_number, configuration, map_revision;
    uint64_t recipient_begin_generation;
    uint64_t silent_audio_remainder;
    char *map_name;
    unsigned sdl_subsystems;
    uint32_t width, height;
    qa_display_info observed_display;
    bool stepping, preparing;
    void *native_output_context;
    void (*native_print)(void *, const qa_native_host_print *);
    bool (*native_clipboard)(void *, const char *, qa_error *);
};
void frontend_engine_cvars_bind(qa_frontend *);
bool frontend_fail(qa_error *, qa_status, const char *);
bool frontend_save_image_release(qa_frontend *,qa_save_image **,qa_error *);
bool frontend_clipboard_write(qa_frontend *, const char *, qa_error *);
bool frontend_tools_create_diagnostics(qa_frontend *, qa_vfs *, qa_error *);
bool frontend_protocol(const char *, qa_net_protocol_id *, qa_error *);
bool frontend_launch(qa_frontend *, qa_error *);
qa_mode_kind frontend_local_mode(qa_game_family, unsigned local_count, qa_mode_kind);
bool frontend_launch_overlay(qa_launch_draft *, const char *, uint64_t, const char *,
    qa_launch_scope, qa_error *);
bool frontend_player_source_select(qa_frontend *, uint32_t physical_seat,
    qa_launch_role, const qa_product *, qa_error *);
bool frontend_player_sources_drain(qa_frontend *, qa_error *);
void frontend_player_sources_discard(qa_frontend *);
const qa_product *frontend_product_selection(qa_catalog *,const char *);
const qa_product *frontend_product_current(const qa_frontend *);
bool frontend_present(qa_frontend *, qa_error *);
bool frontend_frame_cancel(qa_frontend *, qa_error *);
bool frontend_display_ready(qa_frontend *, bool *ready, qa_error *);
bool frontend_frame_present(qa_frontend *, qa_error *);
bool frontend_scene_sync(qa_frontend *, qa_error *);
bool frontend_shader_remap(qa_frontend *, const char *, const char *, float, qa_error *);
bool frontend_shader_sync(qa_frontend *, qa_error *);
bool frontend_shader_retire(qa_frontend *, qa_error *);
void frontend_shader_destroy(qa_frontend *);
bool frontend_material_remaps(qa_frontend *, qa_material_library *, qa_error *);
bool frontend_source_remap(qa_frontend *, const char *, const char *, float, qa_error *);
bool frontend_visuals_remap(qa_frontend *, const char *, const char *, float, qa_error *);
bool frontend_resources(qa_frontend *, qa_error *);
bool frontend_seats_create(qa_frontend *, qa_error *);
bool frontend_seats_create_range(qa_frontend *, unsigned first, qa_error *);
bool frontend_seats_destroy_range(qa_frontend *, unsigned first, unsigned last, qa_error *);
bool frontend_seats_destroy(qa_frontend *, qa_error *);
bool frontend_commands(qa_frontend *, qa_error *);
/* Physical input/audio slots follow the retained published seat array order.
 * An absent publication/row has no admitted application seat identity. */
bool frontend_seat_launch_id_read(const qa_frontend *,uint32_t ordinal,uint32_t *);
bool frontend_seat_ordinal_read(const qa_frontend *,uint32_t launch_seat,uint32_t *);
bool frontend_seat_actor_read(const qa_frontend *,uint32_t ordinal,qa_actor_id *);
bool frontend_command_seat_read(const qa_frontend *,const qa_command_context *,uint32_t *);
bool frontend_seat_context_ready(void *,uint32_t,const qa_command_context *,qa_error *);
bool frontend_events(qa_frontend *, qa_error *);
bool frontend_events_flush(qa_frontend *, qa_error *);
bool frontend_map_events(qa_frontend *, qa_error *);
bool frontend_event_world(qa_frontend *, unsigned, qa_scene_world_input *, qa_error *);
bool frontend_event_sound(qa_frontend *, const qa_builtin_event *, qa_error *);
bool frontend_event_audio(qa_frontend *, qa_error *);
bool frontend_event_debug(qa_frontend *, const qa_scene_view *, qa_error *);
bool frontend_event_retire_checked(qa_frontend *,qa_error *);
bool frontend_event_images(qa_frontend *, qa_actor_owner, qa_game_family, qa_scene_resources **, qa_error *);
bool frontend_particle_events(qa_frontend *, qa_error *);
bool frontend_particle_advance(qa_frontend *, qa_error *);
bool frontend_particle_prepare(qa_frontend *,qa_error *);
void frontend_particle_retire(qa_frontend *);
bool frontend_player_events(qa_frontend *, qa_error *);
void frontend_player_retire(frontend_seat *);
void frontend_print(void *, const char *);
void frontend_console_print(void *, const qa_command_context *, const char *);
qa_command_result frontend_console_forward(void *,const qa_command_invocation *,qa_error *);
bool frontend_menu_open(frontend_seat *, qa_ui_id, qa_error *);
bool frontend_game_menu(frontend_seat *, qa_error *);
bool frontend_wheel_create(frontend_seat *, qa_error *);
bool frontend_bindings_create(frontend_seat *, qa_error *);
bool frontend_binding_capture(void *, uint32_t, qa_physical_input, qa_error *);
void frontend_binding_cancel(void *, uint32_t);
void frontend_bindings_destroy(frontend_seat *);
void frontend_wheel_command(void *, qa_input_seat *, bool, bool);
uint64_t frontend_audio_actor(qa_frontend *, qa_actor_id, qa_error *);
bool frontend_audio_actor_position(qa_frontend *, qa_actor_id, uint64_t,
    const qa_body_state *, qa_vec3 *, qa_error *);
uint64_t frontend_audio_retained_event_actor(qa_frontend *, const qa_builtin_event *, qa_error *);
uint64_t frontend_audio_q2_protocol_actor(qa_frontend *, const qa_application_protocol_event *,
    qa_actor_id, qa_error *);
uint64_t frontend_audio_native_q3_actor(frontend_native_q3 *, uint32_t source_number, qa_error *);
bool frontend_tools_create(qa_frontend *, qa_error *);
bool frontend_tools_before_world_change(qa_frontend *, qa_error *);
bool frontend_tools_world_change_ready(qa_frontend *, qa_error *);
bool frontend_tools_console_retire(qa_frontend *, qa_console *, qa_error *);
bool frontend_tools_sync(qa_frontend *, qa_error *);
bool frontend_tools_capture_clock(qa_frontend *,const qa_cvars *,uint64_t,uint64_t *,qa_error *);
bool frontend_tools_pump(qa_frontend *, qa_error *);
bool frontend_tools_camera(qa_frontend *, uint32_t, bool portal, qa_scene_view *, qa_error *);
bool frontend_tools_debug(qa_frontend *, const qa_scene_view *, qa_error *);
bool frontend_tools_after_present(qa_frontend *, qa_error *);
bool frontend_tools_destroy(qa_frontend *, qa_error *);
qa_http *frontend_tools_http(qa_frontend *);
qa_llm *frontend_tools_llm(qa_frontend *);
qa_tools *frontend_tools_owner(qa_frontend *);
qa_q3_presentation_assets *frontend_source_assets(qa_frontend *, const qa_command_context *);
const qa_scene_resources *frontend_source_images_at(qa_frontend *, size_t);
const qa_scene_resources *frontend_event_images_at(qa_frontend *, size_t);
const qa_scene_resources *frontend_visual_images_at(qa_frontend *, size_t);
const qa_scene_resources *frontend_native_q2_images_at(qa_frontend *, size_t);
bool frontend_network_remote(const qa_frontend *);
qa_save_authority frontend_network_save_authority(const qa_frontend *);
bool frontend_network_client_actor(const qa_frontend *, qa_actor_id);
bool frontend_network_client_ready(const qa_frontend *);
uint32_t frontend_network_client_time(const qa_frontend *);
bool frontend_network_client_services(qa_frontend *, qa_application *, qa_actor_owner, qa_qvm_role, uint32_t, qa_q3_host_options *, qa_error *);
bool frontend_network_client_command(qa_frontend *, const char *, qa_error *);
bool frontend_network_create(qa_frontend *, qa_error *);
bool frontend_network_lobby_host_read(const qa_frontend *, bool *, qa_net_address *,
    qa_lobby_wire *, qa_error *);
bool frontend_network_destroy(qa_frontend *, qa_error *);
bool frontend_network_stop_server(qa_frontend *, bool *, qa_error *);
bool frontend_network_retire_connections(qa_frontend *, bool *, qa_error *);
bool frontend_network_close_client(qa_frontend *,qa_error *);
bool frontend_network_retire_clients(qa_frontend *,qa_error *);
bool frontend_platform_drain(qa_frontend *, bool input_ready, qa_error *);
typedef struct frontend_replay_timing {
    uint64_t wall_ns,time_ns,duration_ns,frame_before,frame_after;
    bool advanced,completed;
} frontend_replay_timing;
typedef struct frontend_replay_command {
    qa_console *console;
    qa_command_context context;
    uint64_t wall_ns,time_ns,frame_number;
} frontend_replay_command;
bool frontend_replay_frame(qa_frontend *,const frontend_replay_timing *,qa_error *);
bool frontend_network_prepare(qa_frontend *, qa_error *);
bool frontend_network_intake(qa_frontend *, qa_platform_events *, uint64_t now_ns, qa_error *);
bool frontend_network_receive_ready(const qa_frontend *);
bool frontend_network_event_ready(const qa_frontend *, const qa_sys_event *);
bool frontend_network_local_input_owned(const qa_frontend *, uint32_t physical);
bool frontend_network_q3_input_owned(const qa_frontend *, uint32_t physical);
bool frontend_network_local_seat(const qa_frontend *, const qa_net_address *,
    qa_net_seat_id *, uint32_t *application_seat);
bool frontend_network_receive(qa_frontend *, const qa_sys_event *, qa_bytes, bool *consumed, qa_error *);
bool frontend_network_maintenance(qa_frontend *, qa_error *);
bool frontend_network_client_attempts_advance(qa_frontend *, qa_error *);
bool frontend_network_tick(qa_frontend *, uint64_t elapsed_ns, bool retiring_map, bool *source_ready, qa_error *);
bool frontend_network_client_only(const qa_frontend *);
bool frontend_network_client_command_seat(qa_frontend *,uint32_t,const char *,qa_error *);
bool frontend_network_publish(qa_frontend *, qa_error *);
void frontend_network_events_consume(qa_frontend *);
bool frontend_network_world_change_ready(qa_frontend *, qa_error *);
bool frontend_network_fresh_ready(const qa_frontend *,const qa_frontend *,const qa_frontend *,qa_error *);
void frontend_network_publish_fresh(qa_frontend *,qa_frontend *,qa_frontend *);
void frontend_network_transport_exchange(qa_frontend *, qa_frontend *);
bool frontend_network_source_services(qa_frontend *, qa_q3_host_options *, qa_error *);
bool frontend_network_qw_command_realtime(const qa_frontend *,qa_actor_owner,qa_actor_id,uint64_t *,qa_error *);
typedef struct frontend_source_group_view {
    qa_actor_owner owner;
    uint32_t seat,launch_seat;
    uint64_t identity;
    unsigned roles[3];
    const qa_vfs *source_files;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_audio_music *music;
    qa_media_library *movies;
    qa_q3_key *keys;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    const qa_resource *map_resource;
    qa_collision_geometry *geometry;
    qa_scene_world *world;
    bool private_map;
    qa_audio_listener listener;
    bool has_listener, music_attached;
} frontend_source_group_view;
size_t frontend_source_group_count(const qa_frontend *);
bool frontend_source_group_read(const qa_frontend *, size_t, frontend_source_group_view *);
bool frontend_source_group_video_read(const qa_frontend *,size_t,frontend_source_group_view *,
    const struct frontend_video_guests *);
bool frontend_source_identity_allocate(qa_frontend *,uint64_t *,qa_error *);
bool frontend_source_identity_used(const qa_frontend *,uint64_t);
bool frontend_q3_configuration(qa_frontend *,uint8_t out[11332],qa_error *);
bool frontend_source_prepare_scene(qa_frontend *,qa_application *,uint32_t,
    const qa_q3_refdef *,qa_q3_scene_options *,qa_error *);
bool frontend_source_submit_scene(qa_frontend *,uint32_t,qa_actor_owner,
    const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
bool frontend_source_cgame_recipient(const qa_frontend *,uint32_t,qa_actor_owner *,qa_error *);
bool frontend_source_group_q3_ready(const qa_frontend *, size_t,
    const qa_q3_presentation_options *, const qa_q3_presentation_asset_options *, qa_error *);
bool frontend_source_system_info(qa_frontend *, const qa_application_q3_client_context *, const char *, qa_error *);
bool frontend_source_effect(void *, qa_application *, qa_actor_owner, uint32_t,
    qa_application_q3_client_effect, const char *, qa_error *);
bool frontend_source_drain(qa_frontend *, qa_error *);
void frontend_application_options(qa_frontend *, qa_application_options *);
bool frontend_source_services(void *, qa_application *, qa_actor_owner, qa_qvm_role, uint32_t, qa_q3_host_options *, qa_error *);
bool frontend_source_client_prepare(void *,qa_application *,const qa_application_q3_client_preparation *,qa_error *);
bool frontend_source_registry_scope_read(const qa_frontend *,const qa_cvars *,qa_application_console_scope *);
bool frontend_source_role_media_read(const qa_frontend *,qa_actor_owner,qa_qvm_role,uint32_t,uint64_t,qa_vfs **);
bool frontend_source_role_media_current(const qa_frontend *,qa_actor_owner,qa_qvm_role,uint32_t,uint64_t,const qa_vfs *);
bool frontend_source_role_geometry_read(const qa_frontend *,qa_actor_owner,qa_qvm_role,uint32_t,uint64_t,
    const qa_collision_geometry **,qa_trace_scratch **,const qa_resource **,bool *,qa_error *);
bool frontend_network_client_map_read(const qa_frontend *,qa_application *,qa_actor_owner,qa_qvm_role,
    uint32_t,const qa_vfs *,const qa_resource **,bool *,qa_error *);
bool frontend_source_frame(qa_frontend *, uint32_t, qa_scene_rect, qa_error *);
bool frontend_source_present(qa_frontend *,uint32_t physical,uint32_t authored_seat,
    uint32_t real_milliseconds,uint32_t client_milliseconds,bool *hud_drawn,qa_error *);
bool frontend_source_retire_world(qa_frontend *, qa_error *);
bool frontend_source_publish_world(qa_frontend *, qa_error *);
/* Imported scene roots and Q3 backend bindings precede the host borrow exchange;
 * final private guest service qualification follows it. */
bool frontend_source_worlds_rebind_restored(qa_frontend *, qa_error *);
bool frontend_source_publish_music(qa_frontend *,qa_error *);
bool frontend_source_listener(qa_frontend *, uint32_t, qa_audio_listener *);
bool frontend_travel(qa_frontend *, qa_error *);
bool frontend_source_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_source_rebind(qa_frontend *, qa_frontend *);
bool frontend_before_world_change(void *, qa_application *, qa_error *);
bool frontend_world_change_ready(void *, qa_application *, qa_error *);
bool frontend_world_retired(void *, qa_application *, qa_error *);
bool frontend_native_q2_services(void *, qa_application *, qa_actor_owner, qa_native_profile,
    qa_native_host_engine_services *, qa_native_host_q2_application_fn *, void **, qa_error *);
bool frontend_native_q2_frame(qa_frontend *, uint32_t, qa_scene_rect, qa_error *);
bool frontend_native_q2_world_text(qa_frontend *, uint32_t, const qa_scene_view *, qa_error *);
void frontend_native_q2_retire_world(qa_frontend *);
void frontend_visuals_destroy(qa_frontend *);
bool frontend_visuals_submit(qa_frontend *, uint32_t, qa_actor_owner exclude, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
void frontend_camera_axes(qa_vec3, qa_vec3 [3]);
qa_scene_rect frontend_viewport(const qa_frontend *, unsigned);
bool frontend_view_background(qa_frontend *, qa_scene_rect, const qa_scene_view *, qa_error *);
#endif
