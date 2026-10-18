#ifndef QA_APPLICATION_GUEST_Q3_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_PRIVATE_H

#include "internal.h"
#include "q3_command_tokens.h"
#include "q3_source_receipts.h"
#include "unified_q3_events.h"
#include "native_process_owner.h"
#include "guest_q3_equipment_profile.h"
#include "guest_q3_movement_profile.h"
#include "guest_q3_source_pose.h"
#include "guest_q3_input_profile.h"
#include "guest_q3_equipment.h"
#include "guest_q3_grapple_profile.h"
#include "guest_q3_fire.h"
#include "guest_q3_combat_profile.h"
#include "guest_q3_pickups_profile.h"
#include "guest_q3_body.h"
#include "guest_q3_collision_profile.h"
#include "guest_q3_weapon_models.h"
#include "qa/application_q3_client.h"
#include "qa/catalog_write.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>


typedef struct q3g_client {
    qa_actor_id actor;
    q3g_fire_continuation fire;
    char *userinfo;
    char *retirement_reason;
    qa_q3_usercmd command;
    qa_q3_usercmd commands[QA_Q3_USERCMDS];
    int32_t command_sequence, snapshot_sequence, consumed_server_command, server_id;
    uint64_t entered_ns, shared_command_sequence;
    bool shared_command_seen;
    application_q3_command_cursor server_commands;
    qa_q3_gamestate *gamestate;
    qa_arena snapshot_storage;
    qa_q3_snapshot_slot snapshots[QA_Q3_PACKET_BACKUP];
    application_q3_source_receipts source_receipts;
    float sensitivity;
    int32_t weapon;
    qa_q3_big_configstring big_configstring;
    bool allocated, connected, begun, bot, has_snapshot, pending_system_info;
    bool pending_bot, pending_retirement, disconnect_pending, disconnect_started;
    bool roster_attached, carry_pending, reserved;
} q3g_client;
typedef struct q3g_artifact {
    struct q3g_artifact *next;
    char *path;
    qa_qvm_role kind;
    qa_qvm_abi abi;
    qa_qvm_image *image;
    qa_native_module *module;
    qa_native_declaration *declaration;
    qa_resource *resource;
    qa_vfs_acquisition acquisition;
    qa_resource *items_resource;
    qa_vfs_acquisition items_acquisition;
    qa_resource *body_resource;
    qa_vfs_acquisition body_acquisition;
    application_q3_body_profile body_profile;
    qa_q3_host_collision_profile collision_profile;
    qa_buffer collision_scene;
    qa_resource *weapon_models_resource;
    qa_vfs_acquisition weapon_models_acquisition;
    application_q3_weapon_models_profile weapon_models_profile;
    qa_launch_instance_lease *descriptor;
    qa_vfs *view;
    qa_buffer primary, equipment_presentation;
    application_q3_equipment_profile equipment_profile;
    application_q3_grapple_profile *grapple_profile;
    application_q3_combat_profile *combat_profile;
    bool qvm;
} q3g_artifact;
typedef struct q3g_role {
    struct q3g_role *next;
    struct application_q3_guest *engine;
    qa_qvm_role kind;
    qa_qvm_abi abi;
    uint32_t seat, client;
    qa_actor_owner source_owner;
    struct application_native_q3_wire_client_lease *native_client;
    application_provider *client_source;
    struct application_q3_guest *client_engine;
    uint64_t service_sequence;
    qa_string_id service_owner;
    qa_q3_host *host;
    qa_catalog_write_resolver *write_resolver;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_native_module *module;
    qa_native_host *native;
    qa_native_host_q3_options native_options;
    application_native_process_owner process;
    qa_error activation_error;
    qa_native_declaration *declaration;
    q3g_artifact *artifact;
    const qa_launch_instance *descriptor;
    application_q3_movement_profile movement;
    application_q3_source_pose_profile pose_profile;
    application_guest_input_profile input_profile;
    struct application_guest_input *input;
    struct application_guest_projection *projection;
    struct application_q3_catalog *catalog;
    struct application_q3_weapons *weapons;
    struct application_q3_weapon_models *weapon_models;
    struct application_q3_weapons_services *weapon_services;
    struct application_q3_combat *combat;
    application_q3_pickup_profile pickup_profile;
    struct application_q3_pickups *pickups;
    application_q3_equipment *equipment;
    application_q3_body *body;
    qa_application_q3_body_services body_services;
    qa_application_q3_client_context draw_source;
    int32_t draw_arguments[3];
    int32_t init_arguments[3];
    uint8_t init_argument_count;
    bool draw_entry;
    char *path;
    qa_command_tokens arguments;
    qa_arena argument_storage[2];
    unsigned argument_storage_slot;
    unsigned entry_depth;
    struct q3g_spawn_scope *spawn_scope;
    application_q3_command_storage command_storage;
    qa_q3_host_common_services common;
    qa_q3_host_server_services server;
    qa_q3_host_client_services client_services;
    qa_q3_host_collision_services collision_services;
    bool input_keys[256];
    bool initialized, retired, ready, primary, local_client, arguments_scoped;
    bool committed, activation_failed, shutdown_entry;
    bool init_succeeded, source_cleared;
} q3g_role;
typedef enum q3g_round_phase {
    Q3G_ROUND_NONE, Q3G_ROUND_RETIRING, Q3G_ROUND_RESETTING,
    Q3G_ROUND_SETTLING, Q3G_ROUND_FAILED
} q3g_round_phase;
typedef struct q3g_round {
    q3g_round_phase phase;
    uint64_t carried, roster_carried, reconnected;
    uint64_t start_ns, last_frame;
    uint32_t completed_frames;
    bool source_entry, shutdown_completed;
    qa_error failure;
} q3g_round;
struct application_q3_guest {
    application_provider *provider;
    qa_world *world;
    q3g_role *roles, *game;
    qa_id_index role_index;
    size_t owned_role_count;
    q3g_role *constructing_role;
    q3g_role *initializing_role;
    q3g_role *entered_role;
    qa_q3_host_options *constructing_services;
    qa_application_q3_equipment_services *constructing_equipment_services;
    qa_bytes restored_client_cvars;
    qa_cvars *restored_client_registry;
    qa_qvm_role restored_client_role;
    uint32_t restored_client_seat;
    const char *restored_client_source_instance;
    uint32_t restored_client_source_seat;
    q3g_artifact *artifacts;
    struct application_guest_q3_console *console;
    struct application_guest_q3_client_console *client_preparation;
    qa_q3_product product;
    qa_q3_model_names model_names;
    qa_q3_gamestate gamestate;
    q3g_client clients[64];
    qa_actor_index client_actors;
    uint32_t seats[64];
    qa_command_tokens arguments;
    application_q3_command_storage client_commands[64];
    size_t command_depth;
    char *entity_text;
    struct q3g_restore *restoration;
    int32_t milliseconds, random_seed;
    int32_t loaded_game_type, loaded_max_clients;
    uint8_t local_snapshot_server_bit;
    q3g_round round;
    unsigned calls;
    size_t client_leases;
    size_t video_leases;
    uint64_t role_sequence;
    qa_launch_instance_lease *client_descriptor;
    const qa_launch_instance *client_candidate;
    uint64_t client_generation, connection_epoch;
    bool map_ready, loaded_compatibility, draining_clients, restore_pending, startup_restart, handoff_ready;
};

struct application_q3_guest *q3g_engine(const application_provider *);
qa_id_index_cursor q3g_role_lookup_begin(const struct application_q3_guest *, qa_qvm_role, uint32_t seat);
q3g_role *q3g_role_lookup_next(const struct application_q3_guest *, qa_id_index_cursor *);
void q3g_roles_reindex(struct application_q3_guest *);
void q3g_role_publish(q3g_role **position, q3g_role *, q3g_role *next);
bool q3g_command_begin(struct application_q3_guest *,const char *,const char *const *,size_t,
    qa_command_tokens *,qa_error *);
void q3g_command_end(struct application_q3_guest *,qa_command_tokens);
bool application_q3_guest_console_vector(application_provider *,const char *const *,size_t,
    bool *,qa_error *);
bool q3g_world_begin(struct application_q3_guest *, qa_error *);
bool application_q3_player_entity_text(qa_bytes, qa_buffer *, qa_error *);
qa_qvm_role q3g_primary_role(const char *);
bool q3g_source_idle(const struct application_q3_guest *);
bool q3g_call(q3g_role *, int32_t command, const int32_t *, size_t, int32_t *, qa_error *);
bool application_q3_guest_respawn(application_provider *, qa_actor_id, qa_error *);
void application_q3_guest_spawn_point(q3g_role *, qa_actor_id, qa_actor_id,
    const qa_body_state *);
bool q3g_projection_after_free(q3g_role *, uint64_t pointer, qa_error *);
bool q3g_projection_actor_released(q3g_role *, qa_actor_record, qa_error *);
bool q3g_role_create(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
                      const char *path, bool primary, q3g_role **, qa_error *);
bool q3g_role_create_client(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
    const char *path, bool primary, application_provider *actual_game, q3g_role **, qa_error *);
bool q3g_role_create_restored(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
                               const char *path, bool primary, uint64_t service_sequence,
                               qa_string_id service_owner, application_provider *actual_game,
                               q3g_role **, qa_error *);
bool q3g_role_destroy(q3g_role *, qa_error *);
bool q3g_role_activate(q3g_role *, qa_error *);
bool q3g_role_shutdown(q3g_role *, bool restart, qa_error *);
bool q3g_role_shutdown_source(q3g_role *, bool restart, qa_error *);
bool q3g_role_consume(q3g_role *, qa_error *);
bool q3g_role_catalog_refresh(q3g_role *, qa_error *);
bool application_guest_q3_collision_bind(q3g_role *, qa_error *);
void q3g_game_aliases(struct application_q3_guest *, q3g_role *);
bool q3g_role_restart(q3g_role *, q3g_role **, qa_error *);
qa_command_result q3g_declared_command(void *,const qa_command_invocation *,qa_error *);
void q3g_server_bind(q3g_role *, qa_q3_host_options *);
bool q3g_output(application_provider *, const qa_unified_q3_event *, qa_event_lease *, qa_error *);
bool q3g_client_bind(q3g_role *, qa_q3_host_options *, qa_error *);
bool application_q3_guest_input_values_read(application_provider *, uint32_t seat,
    qa_actor_id, uint8_t *weapon, float *sensitivity, qa_error *);
bool q3g_arsenal_client_admit(application_provider *, uint32_t, qa_error *);
application_provider *q3g_native_game_source(qa_application *);
application_provider *q3g_game_source(qa_application *);
bool q3g_selected_client_seat(const application_provider *, const qa_launch_choices *,
    qa_qvm_role, size_t);
bool q3g_arguments(void *, qa_native_host_command_view *, qa_error *);
bool q3g_compatibility(const qa_launch_instance *, const char *, const qa_qvm_image *,
    qa_qvm_role, bool primary, qa_qvm_compatibility *, qa_error *);
bool application_q3_guest_services_descriptor(qa_application *, application_provider *,
    const qa_launch_instance *, qa_qvm_role, uint32_t, uint64_t, qa_q3_host_options *, qa_error *);
char *q3g_copy_text(const char *, qa_error *);
bool q3g_client_actor_set(struct application_q3_guest *, uint32_t, qa_actor_id, qa_error *);
void q3g_client_actor_clear(struct application_q3_guest *, uint32_t);
void q3g_clients_clear(struct application_q3_guest *);
bool q3g_clients_prepare(struct application_q3_guest *, qa_error *);
void q3g_clients_destroy(struct application_q3_guest *);
bool application_guest_q3_create_empty(qa_application *, application_provider *, qa_world *,
    const qa_product *, const qa_launch_choices *, bool restoring, qa_error *);
void application_guest_q3_save_clear(struct application_q3_guest *);
bool q3g_client_effect(q3g_role *, qa_application_q3_client_effect,
                        const char *, qa_error *);
bool q3g_set_configstring(q3g_role *, uint32_t, const char *, qa_error *);
bool q3g_publish_information(q3g_role *, bool, qa_error *);
bool q3g_round_fail(struct application_q3_guest *, const qa_error *, qa_error *);
bool q3g_round_call(q3g_role *, int32_t, const int32_t *, size_t, int32_t *, qa_error *);

#endif
