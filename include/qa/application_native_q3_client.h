#ifndef QA_APPLICATION_NATIVE_Q3_CLIENT_H
#define QA_APPLICATION_NATIVE_Q3_CLIENT_H

#include "qa/application_native_q3_presentation.h"
#include "qa/application_q3_client.h"
#include "qa/application_character_selection.h"
#include "qa/application_native_q3_wire.h"

typedef struct qa_native_q3_client_service qa_native_q3_client_service;

/* Called with the resolved physical GAME, before CGAME constructors. The caller
 * owns the returned services and selection and releases each exactly once. */
typedef struct qa_native_q3_client_services {
    qa_application_q3_client_context client;
    uint64_t publication_generation, map_revision;
    /* Numeric receipts/origins are absent when no real command/transport
     * claim exists. The input owner remains the genuine seat service. */
    qa_input_seat *input;
    /* Borrowed genuine physical GAME reader. The consumed context owns this
     * exact lease; release(context) releases it after all role observers. */
    qa_native_q3_wire_reader *wire_reader;
    uint64_t input_receipt, source_client_origin;
    qa_command_context reliable_origin, console_origin;
    void *context;
    /* Identity/idle/status callbacks inspect state only and never retire or
     * mutate the owner while the service is qualifying a retained view. */
    bool (*current)(void *, const struct qa_native_q3_client_services *);
    bool (*idle)(void *);
    bool (*reliable)(void *, const qa_command_context *, const char *, qa_error *);
    bool (*console)(void *, const qa_command_context *, const char *, qa_error *);
    bool (*reload_client_info)(void *, uint32_t, const char *, qa_error *);
    bool (*status_visible)(void *);
    bool (*command_values)(void *, int32_t weapon, float sensitivity, qa_error *);
    void (*release)(void *);
} qa_native_q3_client_services;
typedef bool (*qa_native_q3_client_preinit_fn)(void *, qa_application *,
    const qa_application_native_q3_presentation *, uint32_t seat,
    qa_native_q3_client_services *, qa_native_q3_character_selection *, qa_error *);

typedef struct qa_native_q3_client_cvar {
    char value[256];
    float number;
    int32_t integer;
    uint64_t modification_count;
} qa_native_q3_client_cvar;
typedef struct qa_native_q3_client_basis {
    qa_application *application;
    qa_session *session;
    const qa_q3_game *source_game;
    const qa_launch_instance *source_launch;
    qa_vfs *content;
    qa_actor_owner source_owner, receiver;
    qa_actor_id viewing_actor;
    qa_product_id content_product;
    qa_q3_product product;
    uint32_t seat, physical_client;
    uint64_t publication_generation, map_revision;
} qa_native_q3_client_basis;
/* Pure installed constructor identity. Does not observe source clocks and
 * admits enclosing PERSISTING capture/import after real source installation. */
bool qa_native_q3_client_basis_read(const qa_native_q3_client_service *,
    qa_native_q3_client_basis *, qa_error *);
/* Qualify an already installed native source lease before service import. The
 * actual frontend has rebound all owner pointers/namespaces at this point. */
bool qa_native_q3_client_source_basis_read(qa_application *,
    const qa_native_q3_client_services *, qa_native_q3_client_basis *, qa_error *);

/* Constructor consumes the actual retained service/declaration leases only on
 * success. It creates CGAME configuration directly, without a QVM/host. */
bool qa_native_q3_client_service_create(qa_application *,
    const qa_application_native_q3_presentation *, qa_native_q3_client_services *,
    qa_native_q3_character_selection *, qa_native_q3_client_service **, qa_error *);
bool qa_native_q3_client_service_destroy(qa_native_q3_client_service *, qa_error *);
bool qa_native_q3_client_service_current(const qa_native_q3_client_service *);
bool qa_native_q3_client_service_idle(const qa_native_q3_client_service *);
bool qa_native_q3_client_service_retire_ready(const qa_native_q3_client_service *, qa_error *);
/* Called only after the actual native CGAME Init has completed. */
bool qa_native_q3_client_initialized(qa_native_q3_client_service *, qa_error *);
bool qa_native_q3_client_context_read(qa_native_q3_client_service *,
    qa_application_q3_client_context *, qa_error *);
/* Borrowed owner inventory for the enclosing frontend graph codec. Callback
 * addresses and pointers are resolved by that owner, never written as bytes. */
const qa_native_q3_client_services *qa_native_q3_client_services_read(
    const qa_native_q3_client_service *);
const qa_native_q3_character_selection *qa_native_q3_client_character(
    const qa_native_q3_client_service *);
/* Register is an observable constructor stage. Restore never calls it. */
bool qa_native_q3_client_register(qa_native_q3_client_service *, qa_error *);
/* Genuine ApplicationQ3Client constructor pre-Init and frame refresh stages.
 * Settings/SystemInfo writes target this seat's engine registry; CG_UpdateCvars
 * remains the separate private-cache refresh stage. */
bool qa_native_q3_client_prepare(qa_native_q3_client_service *, qa_error *);
bool qa_native_q3_client_refresh(qa_native_q3_client_service *, qa_error *);
/* Actual CL SystemInfo effect, after CS1 reaches this reader and before the
 * reliable acknowledgment. Refresh engine values/time only; CG_UpdateCvars
 * retains its separate private-cache stage. */
bool qa_native_q3_client_system_info(qa_native_q3_client_service *, qa_error *);
bool qa_native_q3_client_frame_time(qa_native_q3_client_service *, double supplied_ms,
    double *source_ms, qa_error *);
bool qa_native_q3_client_userinfo_initialize(qa_native_q3_client_service *,
    const char *actual_name, qa_error *);
bool qa_native_q3_client_update(qa_native_q3_client_service *, qa_error *);
bool qa_native_q3_client_force_model_change(qa_native_q3_client_service *, qa_error *);
/* symbol is the donor C global name, e.g. cg_zoomFov or cg_gun_x. Read and
 * direct numeric writes use CGAME private cache, not the engine registry. */
bool qa_native_q3_client_cvar_read(const qa_native_q3_client_service *, const char *symbol,
    qa_native_q3_client_cvar *, qa_error *);
bool qa_native_q3_client_cvar_number(qa_native_q3_client_service *, const char *symbol,
    float, qa_error *);
bool qa_native_q3_client_cvar_integer(qa_native_q3_client_service *, const char *symbol,
    int32_t, qa_error *);
bool qa_native_q3_client_reliable(qa_native_q3_client_service *, const char *, qa_error *);
bool qa_native_q3_client_console(qa_native_q3_client_service *, const char *, qa_error *);
bool qa_native_q3_client_command_values(qa_native_q3_client_service *, int32_t weapon,
    float sensitivity, qa_error *);
bool qa_native_q3_client_set_timescale(qa_native_q3_client_service *, float, qa_error *);

/* Pure configuration continuation; the enclosing frontend codec separately
 * saves actual cvar registries, input receipts, service namespace and retained
 * declaration/content references. Import binds those already restored owners
 * and does not register cvars, reload media or emit input/commands. */
bool qa_native_q3_client_checkpoint(const qa_native_q3_client_service *, qa_buffer *, qa_error *);
bool qa_native_q3_client_restore(qa_application *,
    const qa_native_q3_client_basis *, qa_native_q3_client_services *,
    qa_native_q3_character_selection *, qa_bytes, qa_native_q3_client_service **, qa_error *);

#endif
