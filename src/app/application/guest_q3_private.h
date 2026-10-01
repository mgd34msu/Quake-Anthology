#ifndef QA_APPLICATION_GUEST_Q3_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_PRIVATE_H

#include "internal.h"
#include "guest_q3_equipment_profile.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum { Q3G_BIG_INFO_CHARS = 8192 };

typedef struct q3g_snapshot {
    qa_q3_snapshot value;
    qa_q3_entity *entities;
    size_t capacity;
    int32_t ping;
} q3g_snapshot;
typedef struct q3g_client {
    qa_actor_id actor;
    char *userinfo;
    char *retirement_reason;
    qa_q3_usercmd command;
    qa_q3_usercmd commands[QA_Q3_USERCMDS];
    int32_t command_sequence, snapshot_sequence, consumed_server_command, server_id;
    uint64_t entered_ns;
    qa_q3_reliable reliable;
    qa_q3_gamestate *gamestate;
    q3g_snapshot snapshots[QA_Q3_PACKET_BACKUP];
    float sensitivity;
    int32_t weapon;
    char *big_configstring;
    size_t big_configstring_length;
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
    qa_buffer primary, equipment_presentation;
    application_q3_equipment_profile equipment_profile;
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
    uint64_t service_sequence;
    qa_string_id service_owner;
    qa_q3_host *host;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_native_module *module;
    qa_native_host *native;
    qa_native_host_q3_options native_options;
    qa_error activation_error;
    qa_native_declaration *declaration;
    q3g_artifact *artifact;
    struct application_guest_input *input;
    struct application_guest_projection *projection;
    char *path;
    qa_command_tokens arguments;
    qa_q3_host_common_services common;
    qa_q3_host_server_services server;
    qa_q3_host_client_services client_services;
    bool input_keys[256];
    bool initialized, retired, ready, primary, local_client, arguments_scoped;
    bool committed, activation_failed, shutdown_entry;
} q3g_role;
typedef enum q3g_round_phase {
    Q3G_ROUND_NONE, Q3G_ROUND_RETIRING, Q3G_ROUND_RESETTING,
    Q3G_ROUND_SETTLING, Q3G_ROUND_FAILED
} q3g_round_phase;
typedef struct q3g_round {
    q3g_round_phase phase;
    uint64_t carried, roster_carried, queued, reconnected;
    uint64_t start_ns, last_frame;
    uint32_t completed_frames;
    bool source_entry, shutdown_completed;
    qa_error failure;
} q3g_round;
struct application_q3_guest {
    application_provider *provider;
    qa_world *world;
    q3g_role *roles, *game;
    q3g_artifact *artifacts;
    struct application_guest_q3_console *console;
    qa_q3_product product;
    qa_q3_gamestate gamestate;
    q3g_client clients[64];
    uint32_t seats[64];
    qa_command_tokens arguments;
    char *entity_text;
    struct q3g_restore *restoration;
    int32_t milliseconds, random_seed;
    int32_t loaded_game_type, loaded_max_clients;
    uint8_t local_snapshot_server_bit;
    q3g_round round;
    unsigned calls;
    uint64_t role_sequence;
    bool map_ready, loaded_compatibility, draining_clients, restore_pending, startup_restart, handoff_ready;
};

struct application_q3_guest *q3g_engine(application_provider *);
qa_qvm_role q3g_primary_role(const char *);
bool q3g_call(q3g_role *, int32_t command, const int32_t *, size_t, int32_t *, qa_error *);
bool q3g_role_create(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
                      const char *path, bool primary, q3g_role **, qa_error *);
bool q3g_role_create_restored(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
                               const char *path, bool primary, uint64_t service_sequence,
                               qa_string_id service_owner, q3g_role **, qa_error *);
bool q3g_role_destroy(q3g_role *, qa_error *);
bool q3g_role_activate(q3g_role *, qa_error *);
bool q3g_role_shutdown(q3g_role *, bool restart, qa_error *);
bool q3g_role_shutdown_source(q3g_role *, bool restart, qa_error *);
bool q3g_role_consume(q3g_role *, qa_error *);
void q3g_game_aliases(struct application_q3_guest *, q3g_role *);
bool q3g_role_restart(q3g_role *, q3g_role **, qa_error *);
void q3g_server_bind(q3g_role *, qa_q3_host_options *);
bool q3g_client_bind(q3g_role *, qa_q3_host_options *, qa_error *);
application_provider *q3g_native_game_source(qa_application *);
bool q3g_selected_client_seat(const application_provider *, const qa_launch_choices *,
    qa_qvm_role, size_t);
bool q3g_arguments(void *, qa_native_host_command_view *, qa_error *);
char *q3g_copy_text(const char *, qa_error *);
void q3g_clients_clear(struct application_q3_guest *);
bool application_guest_q3_create_empty(qa_application *, application_provider *, qa_world *,
    const qa_product *, const qa_launch_choices *, bool restoring, qa_error *);
void application_guest_q3_save_clear(struct application_q3_guest *);
bool q3g_client_effect(q3g_role *, qa_application_q3_client_effect,
                        const char *, qa_error *);
bool q3g_set_configstring(q3g_role *, uint32_t, const char *, qa_error *);
bool q3g_round_fail(struct application_q3_guest *, const qa_error *, qa_error *);
bool q3g_round_call(q3g_role *, int32_t, const int32_t *, size_t, int32_t *, qa_error *);

#endif
