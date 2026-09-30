#ifndef QA_APPLICATION_GUEST_NATIVE_Q2_PRIVATE_H
#define QA_APPLICATION_GUEST_NATIVE_Q2_PRIVATE_H
#include "internal.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct application_native_q2_client {
    qa_actor_id actor;
    uint32_t seat;
    bool reserved, connected, begun, bot, disconnect_started;
    char layout[1024];
    int16_t inventory[256];
    struct application_native_q2 *inventory_engine;
    uint32_t inventory_slot;
    qa_inventory_lease inventory_lease;
    bool inventory_bound, inventory_prepared;
} application_native_q2_client;

struct application_native_q2 {
    application_provider *provider;
    qa_world *world;
    qa_native_profile profile;
    qa_native_declaration *declaration;
    struct application_native_q2_inventory *primary_inventory;
    struct application_native_q2_attack *source_attack;
    struct application_native_q2_combat *source_combat;
    qa_native_host_engine_services platform;
    qa_native_host_q2_application_fn application;
    void *application_context;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
    qa_command_tokens arguments;
    qa_actor_id world_actor;
    qa_actor_definition definition;
    application_native_q2_client clients[257];
    char **configstrings;
    uint32_t configstring_count, resource_base[3], resource_limit[3];
    qa_source_frame frame;
    qa_string_id map_name, spawn_point;
    char *entity_text;
    unsigned calls;
    uint32_t current_client;
    uint64_t current_command_sequence;
    uint64_t config_revision, hud_config_revision;
    qa_actor_owner hud_source_owner;
    bool initialized, map_ready, shutting_down;
};

bool application_construct_native_q2(qa_application *, application_provider *, qa_world *,
                                       const qa_product *, const qa_launch_choices *, qa_error *);
bool application_native_q2_spawn_map(application_provider *, const qa_bsp_view *,
                                      const qa_entities *, qa_string_id, qa_string_id, qa_error *);
bool application_native_q2_retire_map(application_provider *, qa_error *);
bool application_native_q2_deconstruct(application_provider *, qa_error *);
bool application_native_q2_idle(const application_provider *);
bool application_native_q2_activate(struct application_native_q2 *, qa_error *);
bool application_native_q2_client_admit(application_provider *, uint32_t, qa_actor_id,
    const char *, const char *, bool, bool *, qa_error *);
bool application_native_q2_client_begin(application_provider *, uint32_t, qa_error *);
bool application_native_q2_client_userinfo(application_provider *, uint32_t, const char *, qa_error *);
bool application_native_q2_client_disconnect(application_provider *, uint32_t, qa_error *);
bool application_native_q2_client_think(application_provider *, uint32_t, qa_bytes, qa_error *);
bool application_native_q2_console_command(application_provider *, qa_actor_id, const char *,
                                            bool *, qa_error *);
qa_native_host_engine_services application_native_q2_services(struct application_native_q2 *);
qa_native_host_movement_services application_native_q2_movement_services(struct application_native_q2 *);
bool application_native_q2_move(application_provider *, qa_actor_id,
    const qa_movement_command *, bool *, qa_error *);
bool application_native_q2_draw_hud(application_provider *, uint32_t, uint32_t, qa_error *);
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
bool application_native_q2_restore_engine(void *, qa_bytes, qa_error *);
bool application_native_q2_inventory_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_inventory_admit(struct application_native_q2 *, uint32_t, qa_error *);
bool application_native_q2_inventory_detach(struct application_native_q2 *, uint32_t, qa_error *);
bool application_native_q2_inventory_binding(application_provider *, qa_actor_id, uint64_t,
    qa_inventory_binding *, qa_error *);
bool application_native_q2_inventory_finish(application_provider *, qa_error *);
bool application_native_q2_prepare_restore(application_provider *, qa_error *);
bool application_native_q2_restore_finish(application_provider *, qa_error *);
bool application_native_q2_inventory_close(struct application_native_q2 *, qa_error *);
#endif
