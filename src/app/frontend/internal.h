#ifndef QA_FRONTEND_INTERNAL_H
#define QA_FRONTEND_INTERNAL_H
#include "qa/frontend.h"
#include "qa/audio.h"
#include "qa/console_draw.h"
#include "qa/console_io.h"
#include "qa/console_seat.h"
#include "qa/hud.h"
#include "qa/hud_wheel.h"
#include "qa/input_platform.h"
#include "qa/material.h"
#include "qa/render_cpu.h"
#include "qa/render_gl.h"
#include "qa/q3_presentation.h"
#include "qa/tools.h"
#include "qa/http.h"
#include "qa/llm.h"
#include <stdlib.h>
#include <string.h>

#define FRONTEND_OWNER UINT64_C(0x716166726f6e7401)
enum { FRONTEND_HOME = 1, FRONTEND_LIBRARY, FRONTEND_MODS, FRONTEND_SETTINGS, FRONTEND_RANKINGS, FRONTEND_ASSISTANCE, FRONTEND_BINDINGS };
typedef struct qa_frontend_tools qa_frontend_tools;
typedef struct qa_frontend_network qa_frontend_network;
typedef struct frontend_source frontend_source;
typedef struct frontend_remap frontend_remap;
typedef struct frontend_visual_owner frontend_visual_owner;
typedef struct frontend_native_q2 frontend_native_q2;
typedef struct frontend_audio_identity { qa_actor_id actor; uint64_t id; } frontend_audio_identity;
typedef struct frontend_seat {
    struct qa_frontend *frontend;
    uint32_t id;
    qa_input_seat *input;
    qa_seat_console *console;
    qa_ui *ui;
    qa_hud *hud;
    qa_hud_wheel *wheel;
    qa_hud_wheel_item *wheel_items;
    qa_item_definition *wheel_definitions;
    size_t wheel_capacity;
    char *wheel_labels;
    size_t wheel_label_capacity;
    qa_ui_library *library;
    qa_ui_mods *mods;
    qa_ui_rankings *rankings;
    qa_ui_llm *assistance;
    char *clipboard;
    qa_font_selection fonts;
    qa_input_command_builder builder;
    qa_actor_id actor;
    uint64_t sequence;
    qa_ui_control controls[24];
    qa_ui_row *binding_rows;
    char *binding_labels, *binding_command;
    size_t binding_capacity, binding_label_capacity, selected_binding;
    qa_physical_input pending_binding;
    bool binding_conflict;
    char binding_status[256];
    qa_ui_row *settings_rows;
    size_t settings_capacity, selected_setting;
    char setting_value[1024];
    uint64_t settings_revision;
    qa_actor_id q2_actor;
    qa_q2_player_view q2_view;
    qa_hud_value q2_vitals[3];
    qa_hud_timer q2_timer;
    qa_item_id q2_timer_item;
    char *q2_timer_label, *q2_help_text[2];
    const char *q2_help_lines[2];
    qa_hud_score *q2_scores;
    char *q2_score_names;
    size_t q2_score_count;
    bool q2_view_ready, q2_help, q2_inventory;
    bool scores, chat_team;
} frontend_seat;
struct qa_frontend {
    qa_frontend_options options;
    qa_frontend_tools *tools;
    qa_frontend_network *network;
    frontend_source *sources;
    frontend_remap *remaps;
    frontend_visual_owner *visuals;
    frontend_native_q2 *native_q2;
    uint64_t next_source_id;
    frontend_audio_identity *audio_ids;
    size_t audio_id_count, audio_id_capacity;
    uint64_t next_audio_id;
    qa_application *application;
    qa_display *display;
    qa_cpu_renderer *cpu;
    qa_gl_renderer *gl;
    qa_input_platform *input;
    qa_input_console *input_commands;
    qa_dedicated_console *terminal;
    qa_vfs *ui_mounts, *mounts;
    qa_scene_resources *ui_images, *images;
    qa_scene_image *console_background;
    qa_font_library *fonts;
    const qa_font *classic, *primary;
    qa_material_order *order;
    qa_material_library *materials;
    qa_scene_world *scene_world;
    qa_resource *map_resource;
    qa_scene_frame frame;
    qa_audio_engine *audio;
    qa_audio_device *device;
    qa_audio_bank *sounds;
    frontend_seat seats[QA_INPUT_LOCAL_SEATS];
    uint64_t time_ns, frame_number, configuration, map_revision;
    uint64_t silent_audio_remainder;
    char *map_name;
    unsigned sdl_subsystems;
    uint32_t width, height;
    bool stepping;
};
bool frontend_fail(qa_error *, qa_status, const char *);
bool frontend_protocol(const char *, qa_net_protocol_id *, qa_error *);
bool frontend_launch(qa_frontend *, qa_error *);
bool frontend_present(qa_frontend *, qa_error *);
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
bool frontend_seats_destroy(qa_frontend *, qa_error *);
bool frontend_commands(qa_frontend *, qa_error *);
bool frontend_events(qa_frontend *, qa_error *);
bool frontend_player_events(qa_frontend *, qa_error *);
void frontend_player_retire(frontend_seat *);
void frontend_print(void *, const char *);
void frontend_console_print(void *, const qa_command_context *, const char *);
bool frontend_menu_open(frontend_seat *, qa_ui_id, qa_error *);
bool frontend_game_menu(frontend_seat *, qa_error *);
bool frontend_wheel_create(frontend_seat *, qa_error *);
bool frontend_bindings_create(frontend_seat *, qa_error *);
bool frontend_binding_capture(void *, uint32_t, qa_physical_input, qa_error *);
void frontend_binding_cancel(void *, uint32_t);
void frontend_bindings_destroy(frontend_seat *);
void frontend_wheel_command(void *, qa_input_seat *, bool, bool);
uint64_t frontend_audio_actor(qa_frontend *, qa_actor_id, qa_error *);
bool frontend_tools_create(qa_frontend *, qa_error *);
bool frontend_tools_before_world_change(qa_frontend *, qa_error *);
bool frontend_tools_world_change_ready(qa_frontend *, qa_error *);
bool frontend_tools_sync(qa_frontend *, qa_error *);
bool frontend_tools_capture_clock(qa_frontend *, uint64_t, uint64_t *, qa_error *);
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
const qa_scene_resources *frontend_visual_images_at(qa_frontend *, size_t);
const qa_scene_resources *frontend_native_q2_images_at(qa_frontend *, size_t);
bool frontend_network_create(qa_frontend *, qa_error *);
bool frontend_network_destroy(qa_frontend *, qa_error *);
bool frontend_network_pump(qa_frontend *, qa_error *);
bool frontend_network_command(qa_frontend *, uint32_t, qa_actor_id, const qa_movement_command *, qa_error *);
bool frontend_network_publish(qa_frontend *, qa_error *);
bool frontend_network_world_change_ready(qa_frontend *, qa_error *);
bool frontend_network_source_services(qa_frontend *, qa_q3_host_options *, qa_error *);
bool frontend_source_services(void *, qa_application *, qa_actor_owner, qa_qvm_role, uint32_t, qa_q3_host_options *, qa_error *);
bool frontend_source_frame(qa_frontend *, uint32_t, qa_scene_rect, qa_error *);
bool frontend_source_retire_world(qa_frontend *, qa_error *);
bool frontend_source_publish_world(qa_frontend *, qa_error *);
bool frontend_source_listener(qa_frontend *, uint32_t, qa_audio_listener *);
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
#endif
