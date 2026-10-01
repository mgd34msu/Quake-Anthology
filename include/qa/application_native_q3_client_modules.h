#ifndef QA_APPLICATION_NATIVE_Q3_CLIENT_MODULES_H
#define QA_APPLICATION_NATIVE_Q3_CLIENT_MODULES_H

#include "qa/application_q3_factory.h"
#include "qa/application_q3_equipment_source.h"
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
bool qa_application_native_q3_client_modules_loading_screen(application_native_q3_client_modules *,
    bool *drawn, qa_error *);
bool qa_application_native_q3_client_modules_artifact_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_application_q3_role_artifact *, qa_error *);
/* Pure identity access for actual frontend media/world restoration. The
 * attached physical parent retains the module host; no source entry occurs. */
bool qa_application_native_q3_client_modules_host_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_q3_host **, qa_q3_host_client_context *, qa_error *);
/* Pure proof for retained host callbacks during their actual source entry,
 * including entered Shutdown. Retirement never reopens media readiness. */
bool qa_application_native_q3_client_modules_host_entered(const application_native_q3_client_modules *,
    qa_qvm_role, const qa_q3_host *, uint64_t service_owner);
bool qa_application_native_q3_client_modules_receipt_read(const application_native_q3_client_modules *,
    qa_qvm_role, qa_application_q3_role_receipt *, qa_error *);
bool qa_application_native_q3_client_modules_receipt_current(const application_native_q3_client_modules *,
    const qa_application_q3_role_receipt *);
bool qa_application_native_q3_client_modules_equipment_requests(const application_native_q3_client_modules *,
    bool *hud, bool *view, qa_error *);

bool qa_application_native_q3_client_modules_content_visit(const application_native_q3_client_modules *,
    const qa_application_content_visitor *, qa_error *);
bool qa_application_native_q3_client_modules_checkpoint(const application_native_q3_client_modules *,
    qa_buffer *, qa_error *);
/* Complete source records are decoded and qualified before host construction.
 * Imports actual RAM/host/function continuations without source Init or reads. */
bool qa_application_native_q3_client_modules_restore(qa_application *,
    const qa_application_native_q3_client_modules_options *, qa_bytes,
    application_native_q3_client_modules **, qa_error *);
bool qa_application_native_q3_client_modules_finish_restore(application_native_q3_client_modules *, qa_error *);

#endif
