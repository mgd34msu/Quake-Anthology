#ifndef QA_APPLICATION_NATIVE_Q3_CONSOLE_H
#define QA_APPLICATION_NATIVE_Q3_CONSOLE_H

#include "internal.h"

/* map_path is the actual incoming map, before public map metadata is replaced.
 * Engine defaults and genuine startup current/latch values share this owner;
 * empty package cvars are not evidence that pure metadata was prepared. */
bool application_native_q3_console_create(application_provider *, const char *map_path,
    qa_error *);
bool application_native_q3_console_destroy(application_provider *, qa_error *);
bool application_native_q3_console_idle(const application_provider *);
bool application_native_q3_console_borrow(application_provider *, qa_error *);
void application_native_q3_console_release(application_provider *);
bool application_native_q3_console_at(application_provider *, qa_console **,
                                       qa_cvars **, qa_command_context *);
qa_cvars *application_native_q3_console_registry(const application_provider *);
qa_cvar_handle application_native_q3_console_no_areas_handle(const application_provider *);
typedef enum application_native_q3_rankings_control {
    APPLICATION_Q3_RANKINGS_ENABLE,
    APPLICATION_Q3_RANKINGS_ACTIVE,
    APPLICATION_Q3_RANKINGS_GAME_TYPE,
    APPLICATION_Q3_RANKINGS_FRAGLIMIT,
    APPLICATION_Q3_RANKINGS_TIMELIMIT,
    APPLICATION_Q3_RANKINGS_CONTROL_COUNT
} application_native_q3_rankings_control;
qa_cvar_handle application_native_q3_console_rankings_handle(const application_provider *,
    application_native_q3_rankings_control);
bool application_native_q3_console_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q3_console_restore(application_provider *, qa_bytes, qa_error *);
bool application_native_q3_console_settings_bound(const application_provider *);
void application_native_q3_console_settings_commit(application_provider *);
qa_cvars *application_native_q3_cvar_owner(const application_provider *, const char *);
typedef struct application_native_q3_source_command_scope {
    struct application_native_q3_source_command_scope *previous;
    application_provider *provider;
    qa_q3_game *game;
    const qa_launch_instance *launch;
    qa_actor_id actor;
    uint32_t slot;
    uint64_t publication_generation,command_generation,map_revision;
} application_native_q3_source_command_scope;
bool application_native_q3_source_command_begin(application_provider *,qa_actor_id,
    const qa_command_invocation *,application_native_q3_source_command_scope *,qa_error *);
bool application_native_q3_source_command_entered(const application_provider *);
bool application_native_q3_source_command_actor_current(const application_provider *,qa_actor_id);
bool application_native_q3_source_command_end(application_native_q3_source_command_scope *,qa_error *);
typedef struct application_native_q3_source_drop_scope {
    struct application_native_q3_source_drop_scope *previous;
    application_provider *provider;
    qa_q3_game *game;
    const qa_launch_instance *launch;
    qa_actor_id actor;
    uint32_t slot;
    uint64_t publication_generation, command_generation, map_revision;
    char *reason;
    bool disconnected;
} application_native_q3_source_drop_scope;
bool application_native_q3_source_entered(const application_provider *);
bool application_native_q3_source_drop_begin(application_provider *,uint32_t,qa_actor_id,
    application_native_q3_source_drop_scope *,qa_error *);
bool application_native_q3_source_drop_disconnected(application_native_q3_source_drop_scope *,qa_error *);
bool application_native_q3_source_drop_end(application_native_q3_source_drop_scope *,qa_error *);

#endif
