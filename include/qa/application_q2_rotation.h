#ifndef QA_APPLICATION_Q2_ROTATION_H
#define QA_APPLICATION_Q2_ROTATION_H
#include "qa/application.h"

typedef struct qa_application_q2_rotation_view {
    const qa_application *application;
    qa_actor_owner owner;
    const qa_launch_instance *descriptor;
    qa_console *console;
    qa_cvars *cvars;
    qa_application_console_scope scope;
    qa_command_context command;
    const char *desired_maps, *effective_maps;
    const char *latched_maps;
    uint64_t maps_revision, shuffle_revision;
    uint64_t configuration_generation;
    bool rerelease, prepared, desired_shuffle, effective_shuffle;
} qa_application_q2_rotation_view;

/* Strings borrow the actual physical source until its next scalar edit. */
bool qa_application_q2_rotation_read(qa_application *, qa_actor_owner,
    qa_application_q2_rotation_view *, qa_error *);
bool qa_application_q2_rotation_current(const qa_application_q2_rotation_view *);
/* One genuine Source scalar edit, through its managed physical console.
 * Setting maps resets its order; changing only shuffle preserves that order. */
bool qa_application_q2_rotation_maps(qa_application *, const qa_application_q2_rotation_view *,
    const char *, qa_error *);
bool qa_application_q2_rotation_shuffle(qa_application *, const qa_application_q2_rotation_view *,
    bool, qa_error *);
#endif
