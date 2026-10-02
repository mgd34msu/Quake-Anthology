#ifndef QA_APPLICATION_VIEW_PREFERENCES_H
#define QA_APPLICATION_VIEW_PREFERENCES_H

#include "qa/application.h"

typedef enum qa_application_view_preference_mode {
    QA_APPLICATION_VIEW_CHANGE,
    QA_APPLICATION_VIEW_RESTORE
} qa_application_view_preference_mode;

/* Read the actual effective Source/Q2 CHARACTER FOV, if that owner exists. */
bool qa_application_player_field_of_view_read(qa_application *, qa_actor_id,
    double *, bool *found, qa_error *);
bool qa_application_player_field_of_view_set(qa_application *, qa_actor_id,
    double, qa_application_view_preference_mode, qa_error *);
/* Apply in published local seat order. Guest QVM GAME owns its own FOV. */
bool qa_application_player_field_of_view_apply(qa_application *, double,
    qa_application_view_preference_mode, qa_error *);

#endif
