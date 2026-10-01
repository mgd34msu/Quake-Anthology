#ifndef QA_APPLICATION_STARTUP_PREPARE_H
#define QA_APPLICATION_STARTUP_PREPARE_H
#include "qa/application.h"

/* The platform retains its real configuration, input and archive owners here.
 * The console and registry are the selected provider's eventual GAME owners.
 * The options hook pointer and its context must outlive the application. */
typedef struct qa_application_startup_hooks {
    void *context;
    bool (*prepare_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_launch_instance *, qa_console *, qa_cvars *,
        const qa_command_context *, void **phase, qa_error *);
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
        const qa_launch_instance *, qa_console *, qa_cvars *, const qa_command_context *, qa_error *);
    /* Bind decoded authorities only, after the actual source registry import. */
    bool (*restore_source)(void *, qa_application *, const qa_launch_snapshot *,
        const qa_launch_instance *, qa_console *, qa_cvars *, qa_error *);
    bool (*retire_source)(void *, qa_application *, const qa_launch_instance *,
        qa_console *, qa_cvars *, qa_error *);
    qa_cvars *(*cvar_owner)(void *, qa_application *, qa_console *,
        const qa_command_context *, const char *);
    bool (*visible_cvars)(void *, qa_application *, qa_console *,
        const qa_command_context *, size_t, qa_cvars **);
} qa_application_startup_hooks;

/* READY remains READY across a source wait. No GAME Init or world publication
 * occurs until all retained configuration phases have completed. */
bool qa_application_startup_pending(const qa_application *);
bool qa_application_startup_advance(qa_application *, bool *complete, qa_error *);
bool qa_application_startup_abort(qa_application *, qa_error *);
const qa_launch_snapshot *qa_application_startup_candidate(const qa_application *);
bool qa_application_startup_source_read(qa_application *, const qa_launch_snapshot *,
    const qa_launch_instance *, qa_console **, qa_cvars **, qa_command_context *, qa_error *);
bool qa_application_startup_replay_variables(qa_application *, qa_console *, qa_error *);
#endif
