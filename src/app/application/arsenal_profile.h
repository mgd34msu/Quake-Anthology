#ifndef QA_APPLICATION_ARSENAL_PROFILE_H
#define QA_APPLICATION_ARSENAL_PROFILE_H
#include "qa/application_startup_prepare.h"

typedef struct application_arsenal_profile {
    qa_game_family family;
    union {
        qa_q1_program q1;
        qa_q2_options q2;
        qa_q3_product q3;
    } source;
} application_arsenal_profile;

/* Product declarations for the selected default or authored-seat arsenal.
 * Draft metadata describes the boot catalog independently of execution kind;
 * a running provider's registered items supersede these declarations. */
bool application_draft_arsenal_profile(const qa_launch_draft *, qa_launch_scope,
    application_arsenal_profile *, bool *found, qa_error *);
/* Native boot defaults additionally require the retained physical provider. */
bool application_startup_arsenal_profile(qa_application *, const qa_launch_snapshot *,
    qa_launch_scope, application_arsenal_profile *, qa_actor_owner *, bool *found, qa_error *);
#endif
