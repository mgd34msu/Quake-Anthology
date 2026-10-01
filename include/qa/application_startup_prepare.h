#ifndef QA_APPLICATION_STARTUP_PREPARE_H
#define QA_APPLICATION_STARTUP_PREPARE_H
#include "qa/application.h"
#include "qa/game_q1.h"
#include "qa/game_q2.h"
#include "qa/game_q3.h"
#include "qa/application_language.h"

typedef struct qa_settings_store qa_settings_store;
/* A borrowed physical configuration authority. CLIENT scopes retain their
 * receiver descriptor and authored seat independently of GAME. A heap is
 * transferred only through its actual factory's checked ownership operation. */
typedef struct qa_application_startup_source {
    const qa_launch_instance *descriptor;
    qa_application_console_scope scope;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    uint64_t declaration_owner;
} qa_application_startup_source;

/* The platform retains its real configuration, input and archive owners here.
 * Consoles and registries are the selected physical GAME or CLIENT owners.
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
    bool (*visible_cvars)(void *, qa_application *, qa_console *,
        const qa_command_context *, size_t, qa_cvars **);
    bool (*read_source_script)(void *, qa_application *, qa_console *,
        const qa_command_context *, const char *, qa_bytes *, void **lease, qa_error *);
    void (*release_source_script)(void *, qa_application *, qa_console *, void *lease);
    /* Entered detached teardown releases physical client/read leases before
     * GAME readiness and Shutdown. The configuration owner remains live. */
    bool (*begin_retire_source)(void *, qa_application *, const qa_application_startup_source *, qa_error *);
    bool (*carry_source_variables)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, bool *carried, qa_error *);
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
    /* Hosted CLIENT roles have released their old GAME leases. The retained
     * physical console remains closed until its completed new heap is bound. */
    bool (*retire_hosted_configuration)(void *, qa_application *,
        const qa_application_startup_source *, qa_error *);
    bool (*bind_hosted_configuration)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_startup_source *, const qa_application_startup_source *,
        qa_cvars **, qa_error *);
    void (*publish_hosted_configuration)(void *, qa_application *,
        const qa_application_startup_source *);
    /* Borrow only language tickets actually held by this candidate's
     * retained settings owner. This does not authorize source destruction. */
    bool (*candidate_languages)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_application_language_ticket *const **, size_t *, qa_error *);
    bool (*abort_candidate)(void *, qa_application *, const qa_launch_snapshot *, qa_error *);
} qa_application_startup_hooks;

/* READY remains READY across a source wait. No GAME Init or world publication
 * occurs until all retained configuration phases have completed. */
bool qa_application_startup_pending(const qa_application *);
bool qa_application_startup_advance(qa_application *, bool *complete, qa_error *);
bool qa_application_startup_abort(qa_application *, qa_error *);
/* True only inside entered retirement of this exact physical source or its
 * released hosted CLIENT child. Historical stamps alone are insufficient. */
bool qa_application_startup_source_retiring(const qa_application *, const qa_console *,
    const qa_command_context *);
/* Borrow canonical ENGINE only through this actual physical source. A
 * detached heap additionally requires its entered provider shutdown loan. */
bool qa_application_startup_source_engine_cvars(const qa_application *,
    const qa_application_startup_source *, qa_cvars **, qa_error *);
const qa_launch_snapshot *qa_application_startup_candidate(const qa_application *);
bool qa_application_startup_source_read(qa_application *, const qa_launch_snapshot *,
    const qa_launch_instance *, qa_console **, qa_cvars **, qa_command_context *, qa_error *);
bool qa_application_startup_replay_variables(qa_application *, qa_console *,
    const qa_command_context *, qa_error *);
bool qa_application_startup_console_primary(qa_application *, qa_console *, bool *primary, qa_error *);
bool qa_application_startup_q3_safe_mode(qa_application *, const qa_launch_instance *,
    qa_console *, bool *safe, qa_error *);
bool qa_application_startup_q2_arsenal_options(qa_application *, const qa_launch_snapshot *,
    qa_launch_scope, qa_q2_options *, bool *found, qa_error *);
bool qa_application_startup_q1_arsenal_program(qa_application *, const qa_launch_snapshot *,
    qa_launch_scope, qa_q1_program *, qa_actor_owner *, bool *found, qa_error *);
bool qa_application_startup_q3_arsenal_product(qa_application *, const qa_launch_snapshot *,
    qa_launch_scope, qa_q3_product *, qa_actor_owner *, bool *found, qa_error *);
#endif
