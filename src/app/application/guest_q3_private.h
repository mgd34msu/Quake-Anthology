#ifndef QA_APPLICATION_GUEST_Q3_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_PRIVATE_H

#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

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
    uint32_t big_configstring_index;
    size_t big_configstring_length;
    bool big_configstring_active;
    bool allocated, connected, begun, bot, has_snapshot, pending_system_info;
    bool pending_bot, pending_retirement, disconnect_pending, disconnect_started;
    bool roster_attached;
} q3g_client;
typedef struct q3g_artifact {
    struct q3g_artifact *next;
    char *path;
    qa_qvm_role kind;
    qa_qvm_abi abi;
    qa_qvm_image *image;
    qa_native_module *module;
    qa_native_declaration *declaration;
    qa_buffer primary;
    bool qvm;
} q3g_artifact;
typedef struct q3g_role {
    struct q3g_role *next;
    struct application_q3_guest *engine;
    qa_qvm_role kind;
    qa_qvm_abi abi;
    uint32_t seat, client;
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
    bool committed, activation_failed;
} q3g_role;
struct application_q3_guest {
    application_provider *provider;
    qa_world *world;
    q3g_role *roles, *game;
    q3g_artifact *artifacts;
    qa_q3_product product;
    qa_q3_gamestate gamestate;
    q3g_client clients[64];
    uint32_t seats[64];
    qa_command_tokens arguments;
    char *entity_text;
    int32_t milliseconds;
    unsigned calls;
    uint64_t role_sequence;
    bool map_ready, draining_clients, restore_pending;
};

struct application_q3_guest *q3g_engine(application_provider *);
bool q3g_call(q3g_role *, int32_t command, const int32_t *, size_t, int32_t *, qa_error *);
bool q3g_role_create(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
                      const char *path, bool primary, q3g_role **, qa_error *);
bool q3g_role_create_restored(struct application_q3_guest *, qa_qvm_role, uint32_t seat,
                               const char *path, bool primary, uint64_t service_sequence,
                               qa_string_id service_owner, q3g_role **, qa_error *);
bool q3g_role_destroy(q3g_role *, qa_error *);
bool q3g_role_activate(q3g_role *, qa_error *);
bool q3g_role_shutdown(q3g_role *, qa_error *);
void q3g_game_aliases(struct application_q3_guest *, q3g_role *);
bool q3g_role_restart(q3g_role *, q3g_role **, qa_error *);
void q3g_server_bind(q3g_role *, qa_q3_host_options *);
bool q3g_client_bind(q3g_role *, qa_q3_host_options *, qa_error *);
bool q3g_arguments(void *, qa_native_host_command_view *, qa_error *);
char *q3g_copy_text(const char *, qa_error *);
void q3g_clients_clear(struct application_q3_guest *);
bool application_guest_q3_create_empty(qa_application *, application_provider *, qa_world *,
    const qa_product *, const qa_launch_choices *, bool restoring, qa_error *);
bool q3g_client_effect(q3g_role *, qa_application_q3_client_effect,
                        const char *, qa_error *);

#endif
