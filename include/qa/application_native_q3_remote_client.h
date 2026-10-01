#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_CLIENT_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_CLIENT_H
#include "qa/application_native_q3_client.h"
#include "qa/network.h"
#include "qa/q3_host.h"

typedef struct qa_native_q3_remote_client_service qa_native_q3_remote_client_service;
/* This basis is the received CLIENT domain. It contains no local GAME or
 * GAME wire reader. The frontend retains its private content/map and Network
 * qualifies the actual connection generation and decoded gamestate. */
typedef struct qa_native_q3_remote_client_basis {
    qa_application *application;
    qa_session *session;
    qa_application_q3_client_context client;
    const qa_launch_instance *descriptor;
    qa_vfs *content;
    qa_product_id content_product;
    qa_q3_product product;
    qa_net_client_id connection;
    uint64_t epoch, restart_generation, publication_generation, configuration_generation;
    const qa_resource *map;
    const qa_collision_geometry *geometry;
    const qa_q3_gamestate *gamestate;
    uint32_t physical_client;
    int32_t initial_message, initial_command;
} qa_native_q3_remote_client_basis;
typedef struct qa_native_q3_remote_client_services {
    qa_native_q3_remote_client_basis basis;
    qa_input_seat *input;
    /* Actual Network facade data callbacks; no qa_q3_host is constructed. */
    qa_q3_host_client_services network;
    qa_command_context reliable_origin, console_origin;
    void *context;
    bool (*current)(void *, const qa_native_q3_remote_client_basis *);
    bool (*idle)(void *);
    bool (*reliable)(void *, const qa_command_context *, const char *, qa_error *);
    bool (*console)(void *, const qa_command_context *, const char *, qa_error *);
    bool (*reload_client_info)(void *, uint32_t, const char *, qa_error *);
    bool (*status_visible)(void *);
    /* Failure retains the actual owner and permits checked cleanup retry. */
    bool (*release)(void *, qa_error *);
} qa_native_q3_remote_client_services;
typedef struct qa_native_q3_remote_client_cache {
    const qa_native_q3_remote_client_service *owner;
    uint64_t revision;
    int32_t no_predict, synchronous_clients, predict_items, pmove_fixed, pmove_msec;
    int32_t error_decay_integer, show_miss;
    float error_decay;
} qa_native_q3_remote_client_cache;

bool qa_native_q3_remote_client_create(qa_native_q3_remote_client_services *,
    qa_native_q3_character_selection *, qa_native_q3_remote_client_service **, qa_error *);
bool qa_native_q3_remote_client_destroy(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_current(const qa_native_q3_remote_client_service *);
bool qa_native_q3_remote_client_idle(const qa_native_q3_remote_client_service *);
bool qa_native_q3_remote_client_basis_read(const qa_native_q3_remote_client_service *,
    qa_native_q3_remote_client_basis *, qa_error *);
/* Actual map_restart updates the transport witness without refreshing cache. */
bool qa_native_q3_remote_client_source_bind(qa_native_q3_remote_client_service *,
    const qa_native_q3_remote_client_basis *, qa_error *);
const qa_native_q3_remote_client_services *qa_native_q3_remote_client_services_read(
    const qa_native_q3_remote_client_service *);
const qa_native_q3_character_selection *qa_native_q3_remote_client_character(
    const qa_native_q3_remote_client_service *);
bool qa_native_q3_remote_client_initialized(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_userinfo_initialize(qa_native_q3_remote_client_service *, const char *, qa_error *);
bool qa_native_q3_remote_client_register(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_prepare(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_update(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_system_info(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_force_model_change(qa_native_q3_remote_client_service *, qa_error *);
bool qa_native_q3_remote_client_cvar_read(const qa_native_q3_remote_client_service *, const char *,
    qa_native_q3_client_cvar *, qa_error *);
bool qa_native_q3_remote_client_cvar_number(qa_native_q3_remote_client_service *, const char *, float, qa_error *);
bool qa_native_q3_remote_client_cvar_integer(qa_native_q3_remote_client_service *, const char *, int32_t, qa_error *);
bool qa_native_q3_remote_client_cache_read(const qa_native_q3_remote_client_service *,
    qa_native_q3_remote_client_cache *, qa_error *);
bool qa_native_q3_remote_client_cache_current(const qa_native_q3_remote_client_service *,
    const qa_native_q3_remote_client_cache *);
bool qa_native_q3_remote_client_reliable(qa_native_q3_remote_client_service *, const char *, qa_error *);
bool qa_native_q3_remote_client_console(qa_native_q3_remote_client_service *, const char *, qa_error *);
bool qa_native_q3_remote_client_command_values(qa_native_q3_remote_client_service *, int32_t, float, qa_error *);
bool qa_native_q3_remote_client_set_timescale(qa_native_q3_remote_client_service *, float, qa_error *);
bool qa_native_q3_remote_client_frame_time(qa_native_q3_remote_client_service *, double, double *, qa_error *);
bool qa_native_q3_remote_client_checkpoint(const qa_native_q3_remote_client_service *, qa_buffer *, qa_error *);
bool qa_native_q3_remote_client_restore(qa_native_q3_remote_client_services *,
    qa_native_q3_character_selection *, qa_bytes, qa_native_q3_remote_client_service **, qa_error *);
#endif
