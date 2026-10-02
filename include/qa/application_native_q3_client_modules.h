#ifndef QA_APPLICATION_NATIVE_Q3_CLIENT_MODULES_H
#define QA_APPLICATION_NATIVE_Q3_CLIENT_MODULES_H

#include "qa/application_q3_factory.h"
#include "qa/application_q3_equipment_source.h"
#include "qa/application_q3_body_source.h"
#include "qa/q3_host.h"

typedef struct application_native_q3_client_modules application_native_q3_client_modules;

/* Borrowed only during the actual host constructor. Artifact and profile
 * qualification precede this callback; successful host creation consumes the
 * returned frontend lease. The physical CLIENT console and heap stay shared. */
typedef struct qa_application_native_q3_module_preparation {
    const qa_application_q3_remote_source *source;
    qa_qvm_role role;
    qa_qvm_abi abi;
    uint64_t service_owner;
    const char *path;
    const qa_resource *artifact;
    const qa_vfs_acquisition *acquisition;
    qa_q3_host_options *services;
    qa_application_q3_equipment_services *equipment_services;
    qa_application_q3_body_services *body_services;
    bool restoring;
} qa_application_native_q3_module_preparation;

typedef struct qa_application_native_q3_client_modules_options {
    qa_application_q3_remote_source source;
    /* NULL means no decoded gamestate. A received pure server requires
     * acquired CGAME and UI bytecode; ordinary CGAME remains compiled. */
    const qa_q3_gamestate *gamestate;
    /* Both callbacks and their actual owner context survive checked destroy. */
    void *context;
    bool (*current)(void *, const qa_application_q3_remote_source *,
        const qa_q3_gamestate *, qa_error *);
    bool (*prepare)(void *, const qa_application_native_q3_module_preparation *, qa_error *);
} qa_application_native_q3_client_modules_options;

typedef struct qa_application_native_q3_client_modules_recipe {
    const char *cgame_path, *ui_path, *ui_fallback_path;
    qa_program_kind cgame_runtime, ui_runtime;
    bool pure;
} qa_application_native_q3_client_modules_recipe;
/* SDK policy over the actual current physical source and received SystemInfo.
 * These authored paths are distinct from actual opening/Init receipts. */
bool qa_application_native_q3_client_modules_recipe_read(qa_application *,
    const qa_application_q3_remote_source *, const qa_q3_gamestate *,
    qa_application_native_q3_client_modules_recipe *, qa_error *);
/* Actual reached physical CLIENT argv, shared by its compiled/acquired roles.
 * The view expires when that real parser owner receives its next command. */
bool qa_application_native_q3_client_arguments_read(qa_application *,
    const qa_application_q3_remote_source *, qa_native_host_command_view *, uint64_t *, qa_error *);

/* Failed construction can retain a partially built owner in out. Its physical
 * parent and actual frontend callback contexts must survive checked destroy. */
bool qa_application_native_q3_client_modules_create(qa_application *,
    const qa_application_native_q3_client_modules_options *,
    application_native_q3_client_modules **, qa_error *);
bool qa_application_native_q3_client_modules_destroy(application_native_q3_client_modules **, qa_error *);
bool qa_application_native_q3_client_modules_idle(const application_native_q3_client_modules *);
bool qa_application_native_q3_client_modules_current(const application_native_q3_client_modules *,
    const qa_application_q3_remote_source *);

bool qa_application_native_q3_client_modules_initialize(application_native_q3_client_modules *,
    const qa_application_q3_remote_init *, qa_error *);
/* Initial connecting UI uses the actual constructor connection proof and has
 * no fabricated CGAME counter tuple. Decoded replacements use initialize. */
bool qa_application_native_q3_client_modules_initialize_ui(application_native_q3_client_modules *,
    bool connecting, qa_error *);
bool qa_application_native_q3_client_modules_call(application_native_q3_client_modules *, qa_qvm_role,
    int32_t command, const int32_t *, size_t, int32_t *, qa_error *);
bool qa_application_native_q3_client_modules_console_command(application_native_q3_client_modules *,
    qa_qvm_role, const qa_command_invocation *, int32_t milliseconds, int32_t *, qa_error *);
bool qa_application_native_q3_client_modules_loading_screen(application_native_q3_client_modules *,
    bool *drawn, qa_error *);
bool qa_application_native_q3_client_modules_artifact_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_application_q3_role_artifact *, qa_error *);
/* Pure identity access for actual frontend media/world restoration. The
 * attached physical parent retains the module host; no source entry occurs. */
bool qa_application_native_q3_client_modules_host_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_q3_host **, qa_q3_host_client_context *, qa_error *);
/* Retained ownership only: the physical row still owns this module child.
 * This does not admit execution or publish an initialized media receipt. */
bool qa_application_native_q3_client_modules_retained_source_read(const application_native_q3_client_modules *,
    qa_application_q3_remote_source *, qa_error *);
/* Actual role flags under the same physical host proof, including a pending
 * saved import. This is not a completed/current media receipt. */
bool qa_application_native_q3_client_modules_initialization_read(const application_native_q3_client_modules *,
    qa_qvm_role, bool *initialized, bool *succeeded, qa_error *);
/* A completed current owner can have no acquired CGAME. Incomplete roles
 * remain errors; successful absence clears host/context and sets present false. */
bool qa_application_native_q3_client_modules_optional_host_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_q3_host **, qa_q3_host_client_context *, bool *present, qa_error *);
/* Pure proof for retained host callbacks during their actual source entry,
 * including entered Shutdown. Retirement never reopens media readiness. */
bool qa_application_native_q3_client_modules_host_entered(const application_native_q3_client_modules *,
    qa_qvm_role, const qa_q3_host *, uint64_t service_owner);
/* Constructor callbacks precede ready-host publication. This returns only the
 * actual host of the presently entered role and exact reserved namespace. */
bool qa_application_native_q3_client_modules_entered_host_read(const application_native_q3_client_modules *,
    qa_qvm_role, uint64_t service_owner, qa_q3_host **, qa_q3_host_client_context *, qa_error *);
typedef struct qa_application_native_q3_client_draw_entry {
    qa_application_q3_remote_source source;
    qa_q3_host *host;
    qa_q3_host_client_context context;
    uint64_t revision;
    int32_t server_time, stereo_view, demo_playback;
} qa_application_native_q3_client_draw_entry;
/* Only the actual CG3 invocation bracket, before interpreter entry and through
 * returned body/equipment callbacks. This grants no other entered command. */
bool qa_application_native_q3_client_modules_draw_entry_read(const application_native_q3_client_modules *,
    qa_application_native_q3_client_draw_entry *, qa_error *);
bool qa_application_native_q3_client_modules_draw_entry_current(const application_native_q3_client_modules *,
    const qa_application_native_q3_client_draw_entry *);
/* ConsoleCommand borrows its retained lexical snapshot through the entry.
 * Other source callbacks observe the canonical reached parser. */
bool qa_application_native_q3_client_modules_entered_arguments_read(const application_native_q3_client_modules *,
    qa_qvm_role, uint64_t service_owner, qa_native_host_command_view *, uint64_t *revision, qa_error *);
bool qa_application_native_q3_client_modules_receipt_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_application_q3_role_receipt *, qa_error *);
/* A completed current role can remain cold before Init. Only that state
 * clears the receipt and reports absence; entered failure remains an error. */
bool qa_application_native_q3_client_modules_optional_receipt_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_application_q3_role_receipt *, bool *present, qa_error *);
bool qa_application_native_q3_client_modules_receipt_current(const application_native_q3_client_modules *,
    const qa_application_q3_role_receipt *);
bool qa_application_native_q3_client_modules_equipment_requests(const application_native_q3_client_modules *,
    bool *hud, bool *view, qa_error *);

bool qa_application_native_q3_client_modules_content_visit(const application_native_q3_client_modules *,
    const qa_application_content_visitor *, qa_error *);
bool qa_application_native_q3_client_modules_checkpoint(const application_native_q3_client_modules *,
    qa_buffer *, qa_error *);
/* Pure admission of the child's format prefix for an enclosing opaque record.
 * Complete payload validation remains in the actual module restore decoder. */
bool qa_application_native_q3_client_modules_checkpoint_format(qa_bytes, qa_error *);
/* Complete source records are decoded and qualified before host construction.
 * Imports actual RAM/host/function continuations without source Init or reads. */
bool qa_application_native_q3_client_modules_restore(qa_application *,
    const qa_application_native_q3_client_modules_options *, qa_bytes,
    application_native_q3_client_modules **, qa_error *);
bool qa_application_native_q3_client_modules_finish_restore(application_native_q3_client_modules *, qa_error *);

#endif
