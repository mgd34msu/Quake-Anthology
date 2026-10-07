#ifndef QA_FRONTEND_STARTUP_CONFIG_H
#define QA_FRONTEND_STARTUP_CONFIG_H
#include "qa/console.h"

typedef enum frontend_script_scope {
    FRONTEND_SCRIPT_MOUNTED, FRONTEND_SCRIPT_USER, FRONTEND_SCRIPT_BASE_LOOSE,
    FRONTEND_SCRIPT_GAME_LOOSE, FRONTEND_SCRIPT_LOOSE, FRONTEND_SCRIPT_SEAT
} frontend_script_scope;
typedef struct frontend_startup_config frontend_startup_config;
typedef struct frontend_startup_config_options {
    qa_command_context command;
    bool has_mod, seat_scope, safe_mode;
    void *context;
    bool (*read)(void *, frontend_script_scope, const char *, const qa_command_context *,
                 qa_bytes *, void **lease, qa_error *);
    void (*release)(void *, void *lease);
    bool (*apply_defaults)(void *, qa_error *);
    bool (*apply_archive)(void *, qa_error *);
    bool (*apply_launch)(void *, qa_error *);
    bool (*replay_startup_variables)(void *, qa_error *);
} frontend_startup_config_options;

frontend_startup_config *frontend_startup_config_create(const frontend_startup_config_options *, qa_error *);
/* Own a copy of the genuine image-settings script before source configuration.
 * Its retained prefix borrows the shared aliases and preserves native wait frames.
 * No source defaults, archives, launch options or variables run here. */
frontend_startup_config *frontend_startup_images_create(const qa_command_context *,qa_bytes,qa_error *);
bool frontend_startup_images_command_current(const frontend_startup_config *,const qa_console *,
    const qa_command_context *);
bool frontend_startup_images_completed(const frontend_startup_config *,const qa_console *);
bool frontend_startup_config_destroy(frontend_startup_config *, qa_error *);
/* These hooks belong to the actual retained source. Its enclosing owner still
 * qualifies candidate/publication identity before calling them. */
bool frontend_startup_config_read(frontend_startup_config *, const qa_command_context *,
                                 const char *, qa_bytes *, void **lease, qa_error *);
void frontend_startup_config_release(frontend_startup_config *, void *lease);
void frontend_startup_config_script_complete(frontend_startup_config *,
                                            const qa_command_context *, const char *, bool);
bool frontend_startup_config_restrict_shared(const frontend_startup_config *);
/* Configuration uses a retained prefix of the shared console, preserving the
 * original pending tail. One call is one source frame; wait returns incomplete. */
bool frontend_startup_config_advance(frontend_startup_config *, qa_console *, bool *complete, qa_error *);
#endif
