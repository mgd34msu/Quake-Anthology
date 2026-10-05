#ifndef QA_FRONTEND_STARTUP_ARENA_H
#define QA_FRONTEND_STARTUP_ARENA_H

#include "qa/frontend.h"
#include "qa/ui_library.h"
#include "qa/application_q3_campaign.h"

typedef struct frontend_startup_arena frontend_startup_arena;

bool frontend_startup_arena_create(qa_frontend *, frontend_startup_arena **, qa_error *);
void frontend_startup_arena_destroy(frontend_startup_arena *);
bool frontend_startup_arena_prepare(void *, qa_ui_library *, qa_error *);
bool frontend_startup_arena_arenas(void *, qa_ui_library *,
    const qa_base_arena_catalog **, const qa_arena_progress **, qa_error *);
bool frontend_startup_arena_choices(void *, qa_ui_library *, qa_ui_library_field,
    const char *classname, const qa_ui_library_choice **, size_t *, const char **selected, qa_error *);
bool frontend_startup_arena_select(void *, qa_ui_library *, qa_ui_library_field,
    const char *classname, const char *choice, qa_error *);
/* Mutates the caller's pending draft. The returned settings borrow this owner
 * until its next launch or destruction; campaign_start copies them. */
bool frontend_startup_arena_launch(frontend_startup_arena *, qa_launch_draft *,
    const char *arena_map, int32_t bot_skill, qa_error *);
bool frontend_startup_arena_settings(const frontend_startup_arena *,
    const qa_application_q3_setting **, size_t *, qa_error *);
bool frontend_startup_arena_client_settings(const frontend_startup_arena *,
    const qa_application_q3_setting **, size_t *, qa_error *);
/* Called only for the retained queued Team Arena candidate, after its actual
 * scripts/archive/defaults and before GAME consumes the planned Source rows. */
bool frontend_startup_arena_prepare_settings(qa_cvars *server, qa_cvars *client,
    bool first_source, const qa_application_q3_setting *client_rows, size_t client_count, qa_error *);

#endif
