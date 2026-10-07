#ifndef QA_FRONTEND_STARTUP_SELECTION_H
#define QA_FRONTEND_STARTUP_SELECTION_H

#include "qa/ui_library.h"

typedef struct frontend_startup_selection frontend_startup_selection;

bool frontend_startup_selection_create(frontend_startup_selection **, qa_error *);
void frontend_startup_selection_destroy(frontend_startup_selection *);
bool frontend_startup_selection_complete(qa_ui_library *, qa_error *);
bool frontend_startup_monsters_select(qa_launch_draft *, const char *source, qa_error *);
bool frontend_startup_selection_choices(void *, qa_ui_library *, qa_ui_library_field,
    const char *classname, const qa_ui_library_choice **, size_t *, const char **selected, qa_error *);
bool frontend_startup_selection_select(void *, qa_ui_library *, qa_ui_library_field,
    const char *classname, const char *choice, qa_error *);
bool frontend_startup_selection_roster(void *, qa_ui_library *,
    const qa_ui_library_roster_row **, size_t *, const char **source_label, qa_error *);
bool frontend_startup_selection_weapon_bindings(void *, qa_ui_library *,
    const qa_input_weapon_binding **, size_t *, qa_error *);

#endif
