#ifndef QA_APPLICATION_GUEST_NATIVE_Q2_PRIVATE_H
#define QA_APPLICATION_GUEST_NATIVE_Q2_PRIVATE_H
#include "internal.h"
#include "native_process_owner.h"
#include "qa/binary.h"
#include "qa/json.h"
#include "qa/lightstyles.h"
#include "qa/hud_q2.h"
#include "qa/q2_config_changes.h"
#include "qa/id_index.h"
#include "qa/network_q2_messages.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct qa_application_network_q2_recipient_view;
struct qa_network_runtime;
struct qa_q2_unicast_claim;

typedef struct application_native_q2_client {
    qa_actor_id actor;
    uint32_t seat;
    bool reserved, connected, begun, bot, disconnect_started;
    bool denied;
    bool userinfo_present;
    char userinfo[2048];
    char layout[1024];
    int16_t inventory[256];
    uint64_t layout_revision, inventory_revision;
    qa_q2_wire_fog protocol_fog;
    qa_actor_id protocol_fog_actor;
    struct application_native_q2 *inventory_engine;
    uint32_t inventory_slot;
    qa_inventory_lease inventory_lease, inventory_definitions;
    bool inventory_bound, inventory_prepared;
} application_native_q2_client;

typedef enum application_native_q2_loading_phase {
    APPLICATION_NATIVE_Q2_LOADING_NONE,
    APPLICATION_NATIVE_Q2_LOADING_HOST,
    APPLICATION_NATIVE_Q2_LOADING_INITIALIZE
} application_native_q2_loading_phase;

struct application_native_q2 {
    const struct application_native_q2_input_stage *input_stage;
    const qa_usercmd *input_command;
    const struct application_native_callback_inputs *raw_inputs;
    bool input_arsenal, input_arsenal_committed, input_arsenal_reached;
    const qa_json_document *raw_input_document;
    bool raw_input_capable;
    const struct application_control_external_stage *movement_stage;
    struct application_native_q2_baseline *baseline;
    application_provider *provider;
    qa_world *world;
    qa_native_profile profile;
    qa_native_declaration *declaration;
    struct application_native_q2_callbacks *callbacks;
    struct application_native_q2_stages *stages;
    struct application_native_q2_source_actors *source_actors;
    struct application_native_q2_source_invocation *source_invocation;
    struct application_native_q2_source_storage *source_storage;
    size_t source_retirement_sequence;
    struct application_native_q2_publication *publication;
    struct application_native_q2_wire_engine *wire_engine;
    struct application_native_q2_visibility *visibility;
    application_native_process_owner process;
    struct application_native_q2_inventory *primary_inventory;
    struct application_native_q2_inventory_rows *inventory_rows;
    struct application_native_q2_inventory_scanner *inventory_scanner;
    struct application_native_q2_primary_pickups *primary_pickups;
    struct application_native_q2_attack *source_attack;
    struct application_native_q2_prediction_layout {
        uint32_t client_pointer, client_bytes;
        uint32_t state, pending, shots, grenade, blew_up, end, priority, duck, run;
        uint8_t pointer_bytes, boolean_bytes;
    } prediction;
    struct application_native_q2_combat *source_combat;
    struct application_q2_control *source_control;
    qa_native_host_engine_services platform;
    qa_native_host_q2_application_fn application;
    void *application_context;
    qa_cvars *cvars;
    struct { qa_cvar_handle coop, dmflags; } combat_cvars;
    qa_cvar_handle appearance_cvar;
    qa_console *console;
    qa_command_context command_context;
    qa_command_tokens arguments;
    qa_actor_id world_actor;
    qa_actor_definition definition;
    application_native_q2_client clients[257];
    qa_actor_index client_index;
    qa_q2_messages *event_decoder;
    qa_net_protocol_id protocol;
    qa_hud_q2_names hud_names;
    qa_q2_player_media player_media[256];
    qa_id_index media_probes;
    const qa_vfs *media_files;
    uint64_t media_generation;
    qa_source_frame frame;
    const qa_clock_state *initialization_clock;
    qa_string_id map_name, spawn_point;
    char *entity_text;
    application_native_q2_loading_phase loading_phase;
    bool map_admission_prepared;
    unsigned calls;
    uint32_t current_client, disconnect_client;
    uint64_t current_command_sequence;
    qa_q2_config_changes config_changes;
    uint64_t hud_config_revision;
    qa_lightstyles lightstyles;
    struct qa_network_runtime *network_recipient_runtime;
    void *network_recipient_context;
    bool (*network_recipient)(void *, qa_actor_id,
        struct qa_application_network_q2_recipient_view *, bool *, qa_error *);
    bool (*network_unicast)(void *, const struct qa_q2_unicast_claim *, bool, bool *, qa_error *);
    size_t network_recipient_users;
    struct application_q2_recipient_binding *network_recipient_binding;
    qa_actor_owner hud_source_owner;
    struct {
        qa_native_host_q2_hud_state state;
        qa_native_host_q2_wheel_state wheel;
        qa_actor_id actor;
        qa_actor_owner source;
        uint64_t frame, time_ns, config_revision;
        uint32_t wheel_ammo_mask, wheel_powerup_mask;
        bool present, wheel_ready;
    } hud_observation;
    bool prepared, initialized, map_ready, shutting_down, activation_failed;
    bool host_constructing;
    /* The immutable restore image owns this span through persistence_finish. */
    qa_bytes restore_record;
    qa_error activation_error;
};

typedef struct application_native_q2_resource_span { uint32_t base, count; } application_native_q2_resource_span;
static inline application_native_q2_resource_span application_native_q2_resource_range(
    const struct application_native_q2 *engine, qa_native_host_resource_kind kind)
{
    const qa_q2_config_layout *layout = &engine->hud_names.layout;
    switch (kind) {
    case QA_NATIVE_HOST_MODEL: return (application_native_q2_resource_span){layout->models, layout->max_models};
    case QA_NATIVE_HOST_SOUND: return (application_native_q2_resource_span){layout->sounds, layout->max_sounds};
    case QA_NATIVE_HOST_IMAGE: return (application_native_q2_resource_span){layout->images, layout->max_images};
    }
    return (application_native_q2_resource_span){0};
}

bool application_native_q2_config_update(struct application_native_q2 *, uint32_t, qa_string_id, qa_error *);
bool application_native_q2_config_clear(struct application_native_q2 *, qa_error *);
bool application_native_q2_config_prepare(struct application_native_q2 *, qa_error *);

bool application_construct_native_q2(qa_application *, application_provider *, qa_world *,
                                       const qa_product *, const qa_launch_choices *, qa_error *);
bool application_native_q2_initialize_supplemental(application_provider *,qa_string_id,qa_string_id,qa_error *);
bool application_native_q2_frames_exit(void *,qa_session *,const qa_source_frame *,size_t,uint64_t,qa_error *);
bool application_guest_native_q2_console_prepare(qa_application *, application_provider *, qa_world *,
    const qa_product *, const qa_launch_choices *, qa_console **, qa_cvars **,
    qa_command_context *, qa_error *);
bool application_guest_native_q2_console_advance(qa_application *, application_provider *, qa_world *,
    const qa_product *, const qa_launch_choices *, size_t, bool *, qa_console **, qa_cvars **,
    qa_command_context *, qa_error *);
bool application_native_q2_loading_cancel(application_provider *, qa_error *);
bool application_native_q2_loading_idle(const application_provider *);
bool application_native_q2_initialize_primary_advance(application_provider *, const qa_bsp_view *,
    qa_string_id, qa_string_id, const qa_clock_state *, size_t, bool *, qa_error *);
bool application_native_q2_entity_text(application_provider *, const qa_bsp_view *, char **, qa_error *);
bool application_native_q2_spawn_map(application_provider *, const qa_bsp_view *,
                                      const qa_entities *, qa_string_id, qa_string_id, qa_error *);
bool application_native_q2_retire_map(application_provider *, qa_error *);
bool application_native_q2_deconstruct(application_provider *, qa_error *);
void application_network_q2_retire_bindings(struct application_native_q2 *);
bool application_native_q2_idle(const application_provider *);
/* Public GAME exports own opaque private gameplay when every selected
 * gameplay role remains with this same original Source. */
bool application_native_q2_whole_source(const struct application_native_q2 *, qa_actor_id);
bool application_native_q2_activate(struct application_native_q2 *, qa_error *);
bool application_native_q2_client_admit(application_provider *, uint32_t, qa_actor_id,
    const char *, const char *, bool, bool *, qa_error *);
bool application_native_q2_client_begin(application_provider *, uint32_t, qa_error *);
bool application_native_q2_clients_reconnect(application_provider *, qa_error *);
bool application_native_q2_client_userinfo(application_provider *, uint32_t, const char *, qa_error *);
bool application_native_q2_client_disconnect(application_provider *, uint32_t, qa_error *);
bool application_native_q2_actor_disconnect(application_provider *, qa_actor_id, qa_error *);
bool application_native_q2_client_think(application_provider *, uint32_t, qa_bytes, qa_error *);
bool application_native_q2_game_command(application_provider *, const qa_command_invocation *,
    bool *, qa_error *);
bool application_native_q2_console_command(application_provider *, qa_actor_id, const char *,
                                            bool *, qa_error *);
bool application_native_q2_client_command(application_provider *, qa_actor_id,
    const qa_command_invocation *, bool *, qa_error *);
bool application_native_q2_weapon_request(application_provider *, qa_actor_id,
    qa_item_id, bool *admitted, qa_error *);
bool application_native_q2_weapon_accepts(application_provider *, qa_actor_id,
    qa_item_id, bool *admitted, qa_error *);
qa_native_host_engine_services application_native_q2_services(struct application_native_q2 *);
bool application_native_q2_resources_reconnect(struct application_native_q2 *, qa_error *);
qa_native_host_movement_services application_native_q2_movement_services(struct application_native_q2 *);
bool application_native_q2_move(application_provider *, qa_actor_id,
    const qa_usercmd *, bool *, qa_error *);
struct application_control_external_stage;
bool application_native_q2_stage_move(application_provider *, qa_actor_id,
    const qa_usercmd *, const struct application_control_external_stage *, bool *, qa_error *);
bool application_native_q2_draw_hud(application_provider *, uint32_t, uint32_t, qa_error *);
bool application_native_q2_monster_flash_offset(application_provider *, uint16_t, qa_vec3 *, qa_error *);
struct qa_application_native_q2_wheel;
bool application_native_q2_wheel_read(application_provider *, uint32_t, bool,
    struct qa_application_native_q2_wheel *, qa_error *);
bool application_native_q2_hud_text(application_provider *, uint32_t,
    qa_native_q2_hud_text_kind, const char *, bool, qa_error *);
struct application_native_q2 *application_native_q2_hud_source(struct application_native_q2 *,
    uint32_t *, qa_error *);
bool application_native_q2_import(void *, const qa_native_host_q2_application_call *,
    qa_native_value *, qa_error *);
bool application_native_q2_project(void *, qa_native_host *, uint32_t, qa_native_address,
                                    qa_actor_id *, bool *, qa_error *);
bool application_native_q2_address(void *, qa_native_host *, qa_actor_id,
                                    qa_native_address *, bool *, qa_error *);
bool application_native_q2_bind(void *, qa_native_host *, uint32_t, qa_actor_id, qa_error *);
bool application_native_q2_capture_engine(void *, qa_buffer *, qa_error *);
bool application_native_q2_restore_engine(void *, qa_bytes, qa_native_host_restore_phase, qa_error *);
bool application_native_q2_inventory_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_inventory_admit(struct application_native_q2 *, uint32_t, qa_error *);
bool application_native_q2_inventory_detach(struct application_native_q2 *, uint32_t, qa_error *);
bool application_native_q2_inventory_binding(application_provider *, qa_actor_id, uint64_t,
    qa_inventory_binding *, qa_error *);
bool application_native_q2_inventory_finish(application_provider *, qa_error *);
bool application_native_q2_prepare_restore(application_provider *, qa_error *);
bool application_native_q2_restore_finish(application_provider *, qa_error *);
bool application_native_q2_inventory_close(struct application_native_q2 *, qa_error *);
typedef struct application_native_q2_ui_item {
    qa_item_id item;
    char *label;
    int32_t count;
    uint32_t source_index;
} application_native_q2_ui_item;
typedef struct application_native_q2_ui_inventory {
    application_native_q2_ui_item *items;
    size_t count;
    qa_item_id selected;
} application_native_q2_ui_inventory;
bool application_native_q2_inventory_ui_read(application_provider *, qa_actor_id,
    qa_unified_frame_lease *, application_native_q2_ui_inventory *, qa_error *);
uint32_t application_native_q2_client_slot(const struct application_native_q2 *,qa_actor_id);
bool application_native_q2_prediction_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_client_admitted(const struct application_native_q2 *, qa_actor_id);
bool application_native_q2_client_begun(const struct application_native_q2 *, qa_actor_id);
void application_native_q2_client_unbind(struct application_native_q2 *, qa_actor_id);

#endif
