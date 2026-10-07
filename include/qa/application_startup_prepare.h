#ifndef QA_APPLICATION_STARTUP_PREPARE_H
#define QA_APPLICATION_STARTUP_PREPARE_H
#include "qa/application.h"
#include "qa/game_q1.h"
#include "qa/game_q2.h"
#include "qa/game_q3.h"
#include "qa/application_language.h"
#include "qa/console_cvars_prepare.h"

typedef struct qa_settings_store qa_settings_store;
/* A borrowed Source view of the application-owned console and cvar table.
 * CLIENT scopes retain their receiver descriptor and authored seat separately
 * from GAME; the command records the view's actual constructor identity. */
typedef struct qa_application_startup_source {
    const qa_launch_instance *descriptor;
    qa_application_console_scope scope;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    uint64_t declaration_owner;
} qa_application_startup_source;

/* The platform retains its real configuration, input and archive owners here.
 * GAME and CLIENT views borrow the common console and canonical table.
 * The options hook pointer and its context must outlive the application. */
typedef struct qa_application_startup_hooks {
    void *context;
    bool (*prepare_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, void **phase, qa_error *);
    bool (*advance_source)(void *, void *phase, qa_console *, bool *complete, qa_error *);
    bool (*read_script)(void *, void *phase, const qa_command_context *,
        const char *, qa_bytes *, void **lease, qa_error *);
    void (*release_script)(void *, void *phase, void *lease);
    void (*script_complete)(void *, void *phase, const qa_command_context *, const char *, bool);
    bool (*allow_command)(void *, void *phase, const qa_command_invocation *);
    bool (*prepare_candidate)(void *, qa_application *, const qa_launch_snapshot *, qa_error *);
    bool (*release_source)(void *, void *phase, qa_error *);
    /* Phase cleanup precedes GAME construction. Persistent configuration
     * authorities observe the actual candidate outcome separately. */
    void (*finish_candidate)(void *, qa_application *, const qa_launch_snapshot *, bool published);
    bool (*preinit_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, qa_error *);
    /* Bind decoded authorities only, after the actual source registry import. */
    bool (*restore_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, qa_error *);
    bool (*retire_source)(void *, qa_application *, const qa_application_startup_source *, qa_error *);
    qa_cvars *(*cvar_owner)(void *, qa_application *, qa_console *,
        const qa_command_context *, const char *);
    /* Pure initial local admission borrows the completed physical seat view
     * and canonical view preference; no command context is synthesized. */
    bool (*local_userinfo)(void *,qa_application *,const qa_launch_choices *,
        const qa_launch_seat *,qa_cvars **,const qa_cvar_view **field_of_view,
        bool *found,qa_error *);
    bool (*visible_cvars)(void *, qa_application *, qa_console *,
        const qa_command_context *, size_t, qa_cvars **);
    bool (*read_source_script)(void *, qa_application *, qa_console *,
        const qa_command_context *, const char *, qa_bytes *, void **lease, qa_error *);
    void (*release_source_script)(void *, qa_application *, const qa_cvars *, void *lease);
    /* Entered detached teardown releases physical client/read leases before
     * GAME readiness and Shutdown. The configuration owner remains live. */
    bool (*begin_retire_source)(void *, qa_application *, const qa_application_startup_source *, qa_error *);
    bool (*configuration_store)(void *, qa_application *,
        const qa_application_startup_source *, qa_settings_store *, qa_error *);
    /* Borrow the compatible published physical program before its candidate
     * cfg prefix. No command capture, effects or registry transfer occurs. */
    bool (*program_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, qa_application_startup_source *, bool *found, qa_error *);
    bool (*startup_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, bool *primary, qa_error *);
    /* Advance real retained input/settings releases before final candidate
     * preflight. Incomplete work retains the candidate and all source phases. */
    bool (*advance_candidate)(void *, qa_application *, const qa_launch_snapshot *,
        bool *complete, qa_error *);
    /* Hosted CLIENT roles release their old GAME leases before binding the
     * replacement Source view to the common console. */
    bool (*retire_hosted_configuration)(void *, qa_application *,
        const qa_application_startup_source *, qa_error *);
    bool (*bind_hosted_configuration)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, const qa_application_startup_source *,
        qa_cvars **, qa_error *);
    void (*publish_hosted_configuration)(void *, qa_application *,
        const qa_application_startup_source *);
    /* Purely borrow language tickets actually held by this candidate's
     * retained settings owner. This does not authorize source destruction. */
    bool (*candidate_languages)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_language_ticket *const **, size_t *, qa_error *);
    bool (*abort_candidate)(void *, qa_application *, const qa_launch_snapshot *, qa_error *);
    /* Final resources are admitted after source validation. If candidate_values
     * is supplied, its null result admits the ordinary resource-free path.
     * Partial prepare and entered cleanup failures retain the actual owner. */
    bool (*prepare_publication)(void *, qa_application *, const qa_launch_snapshot *, void **, qa_error *);
    bool (*ready_publication)(void *, qa_application *, const qa_launch_snapshot *, void *, qa_error *);
    bool (*owned_publication_ready)(void *, const qa_application *, const qa_launch_snapshot *, const void *);
    void (*consume_publication)(void *, qa_application *, const qa_launch_snapshot *, void *);
    bool (*finish_publication)(void *, qa_application *, const qa_launch_snapshot *, void **, bool *complete, qa_error *);
    bool (*abort_publication)(void *, qa_application *, const qa_launch_snapshot *, void **, qa_error *);
    /* Source initialization can change selected held rows. The ENGINE-only
     * bootstrap uses this same final release pass after images and initial
     * release, without source validation. Settle effects before resource leases. */
    bool (*advance_validated_candidate)(void *, qa_application *, const qa_launch_snapshot *,
        bool *complete, qa_error *);
    /* Receive the actual postvalidation declaration owner while the retained
     * descriptor, scope, console and Source view remain unchanged. */
    bool (*refresh_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, qa_error *);
    /* Borrow the candidate's canonical edit. Its creator retains publication
     * and cancellation ownership; Source views leave before either operation. */
    bool (*candidate_values)(void *, const qa_application *, const qa_launch_snapshot *,
        qa_cvars_edit **, qa_error *);
    /* Pure proof of this candidate's entered failed input history, retained
     * complete physical coverage, native ticket and returned parents. Dormant
     * captured rows become shutdown metadata only under the ENGINE loan. */
    bool (*candidate_retirement_ready)(void *, const qa_application *, const qa_launch_snapshot *,
        const qa_cvars_edit *, qa_error *);
    /* Resolve the real retained scalar ticket for a captured command or an
     * exact lexical entered cvar operation. Neither admits handler dispatch;
     * a rejected view must not fall back to live values. */
    bool (*cvar_edit)(void *, qa_application *, qa_console *, const qa_command_context *,
        qa_cvars *, qa_cvars_edit **, qa_error *);
    /* Advance the real isolated image-settings programme and touched archive
     * before any source configuration or initial startup-variable replay. */
    bool (*advance_images)(void *, qa_application *, const qa_launch_snapshot *,
        bool *complete, qa_error *);
    /* Admit the actual canonical ENGINE before any private source cfg. A null
     * candidate names only the application's real source-free bootstrap. */
    bool (*prepare_root)(void *, qa_application *, const qa_launch_snapshot *,
        qa_console *, qa_cvars *, const qa_command_context *, qa_error *);
    /* The physical QW GAME appends its obituary before this reached writer.
     * The backend owns the session-lived fraglogfile handle independently of
     * replaced map consoles. Buffers alone never certify enabled logging. */
    void (*qw_logfrag_write)(void *, qa_application *, const qa_application_startup_source *, const char *);
    bool (*qw_logfrag_enabled)(void *, qa_application *, const qa_application_startup_source *, bool *, qa_error *);
    /* Exact registered common services may consume the unchanged Source
     * invocation after its GAME declined it. No text forwarding or reparse. */
    bool (*source_common_command)(void *, qa_application *, const qa_application_startup_source *,
        const qa_command_invocation *, bool *, qa_error *);
    /* Flood policy reads local wall time or the entered remote action receipt,
     * independently of the paused/scaled GAME clock. */
    bool (*source_command_realtime)(void *,qa_application *,const qa_application_startup_source *,
        const qa_command_invocation *,bool remote,uint64_t *,qa_error *);
    /* The actual Source file holder supplies media and changes COM_Gamedir.
     * NULL directory queries; a reached switch reports its literal effects. */
    bool (*source_files)(void *,qa_application *,const qa_application_startup_source *,
        qa_launch_source_files *,const char **native_directory,qa_error *);
    bool (*source_gamedir)(void *,qa_application *,const qa_application_startup_source *,
        const qa_command_invocation *,const char *,bool *changed,qa_error *);
} qa_application_startup_hooks;

/* READY remains READY across a source wait. No GAME Init or world publication
 * occurs until all retained configuration phases have completed. */
bool qa_application_startup_pending(const qa_application *);
bool qa_application_startup_advance(qa_application *, bool *complete, qa_error *);
bool qa_application_startup_abort(qa_application *, qa_error *);
/* Release original-save staging after startup cancellation has completed.
 * Import advancement and native-to-QC reconfiguration keep this custody. */
bool qa_application_startup_import_abort(qa_application *,qa_error *);
/* Retain source-free ENGINE configuration without a synthetic launch. */
bool qa_application_startup_bootstrap(qa_application *, qa_error *);
/* Drive only the retained images programme before first output construction.
 * Native settings and resource publication remain in the ordinary driver. */
bool qa_application_startup_bootstrap_images_advance(qa_application *, bool *complete, qa_error *);
bool qa_application_startup_bootstrap_images_ready(const qa_application *);
bool qa_application_startup_root_phase(const qa_application *, const qa_launch_snapshot *);
/* Only the entered definition registration of an actual candidate source
 * borrows the retained ENGINE root before physical source configuration. */
bool qa_application_startup_root_definition_phase(const qa_application *, const qa_launch_snapshot *);
/* Borrow the actual retained canonical tuple through preparation, consumption
 * and checked cleanup. Execution requires the separate root phase proof. */
bool qa_application_startup_root_read(const qa_application *, const qa_launch_snapshot *,
    qa_console **, qa_cvars **, qa_command_context *, qa_error *);
/* True only inside entered retirement of this exact physical source or its
 * released hosted CLIENT child. Historical stamps alone are insufficient. */
bool qa_application_startup_source_retiring(const qa_application *, const qa_console *,
    const qa_command_context *);
/* Borrow canonical ENGINE through this actual Source. A detached Source view
 * additionally requires its entered provider shutdown loan. */
bool qa_application_startup_source_engine_cvars(const qa_application *,
    const qa_application_startup_source *, qa_cvars **, qa_error *);
const qa_launch_snapshot *qa_application_startup_candidate(const qa_application *);
/* Actual source cfg or ENGINE-only bootstrap inside settings release or final
 * resource preparation, or after a release returned incomplete. Callers must
 * separately prove their retained resource and input tickets. */
bool qa_application_startup_resource_phase(const qa_application *, const qa_launch_snapshot *);
/* Callback-free association with the actual transaction or ENGINE-only flow
 * and physical owners. This is not child readiness or publication authority. */
bool qa_application_startup_resource_phase_associated(const qa_application *, const qa_launch_snapshot *);
/* Exact synchronous entered resource consumption before source retirement and
 * command-generation change. This does not replace any child readiness proof. */
bool qa_application_startup_publication_consuming(const qa_application *, const qa_launch_snapshot *);
/* Borrow the actual retained previous roster at resource preparation,
 * synchronous consumption or checked resource cleanup before source retirement. */
const qa_launch_snapshot *qa_application_startup_publication_previous(const qa_application *,
    const qa_launch_snapshot *);
/* Exact checked finish callback on the retained entered resource owner. This
 * grants cleanup only, including retry after the configuration has committed. */
bool qa_application_startup_publication_cleanup(const qa_application *, const qa_launch_snapshot *);
/* Returned incomplete release only. A quit request is allowed so captured
 * histories can finish before cancellation; no new source/publication work. */
bool qa_application_startup_release_cleanup_phase(const qa_application *, const qa_launch_snapshot *);
/* The images owner must additionally qualify its separate console/programme
 * and canonical edit. This receipt names its actual linked physical source. */
bool qa_application_startup_images_phase(const qa_application *, const qa_launch_snapshot *,
    const qa_application_startup_source *);
/* Borrow the actual GAME producer for this retained descriptor. Its scope
 * identifies the producer independently of the common console/table. */
bool qa_application_startup_source_read(qa_application *, const qa_launch_snapshot *,
    const qa_launch_instance *, qa_application_startup_source *, qa_error *);
bool qa_application_console_source_at(qa_application *, size_t,
    qa_application_startup_source *, bool *present, qa_error *);
bool qa_application_startup_replay_variables(qa_application *, qa_console *,
    const qa_command_context *, qa_error *);
bool qa_application_startup_source_primary(qa_application *, const qa_application_startup_source *,
    bool *primary, qa_error *);
bool qa_application_startup_q3_safe_mode(qa_application *, const qa_launch_instance *,
    const qa_application_startup_source *, bool *safe, qa_error *);
#endif
