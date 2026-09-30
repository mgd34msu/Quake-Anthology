#ifndef QA_APPLICATION_NATIVE_MAPS_H
#define QA_APPLICATION_NATIVE_MAPS_H

#include "qa/application.h"
#include "qa/modes.h"

struct application_provider;
typedef struct application_map_assignment {
    char *name, *value;
    uint32_t set_flags;
} application_map_assignment;
typedef struct application_next_map_plan {
    qa_mode_id mode;
    qa_actor_owner owner;
    qa_application_console_scope scope;
    qa_command_context context;
    char *map;
    application_map_assignment *assignments;
    size_t assignment_count, assignments_before_map;
} application_next_map_plan;

/* The path is owned and qualified by this provider's actual content/product. */
bool application_source_map_path(struct application_provider *, const char *,
                                  char **out, qa_error *);
/* Resolve at the execution boundary, after the vote's source delay. No source
 * command, assignment or travel executes here. A zeroed output owns the result;
 * the consumer applies ordered assignments around the queued map transition. */
bool application_native_next_map_plan(qa_application *, qa_mode_id,
                                       application_next_map_plan *, qa_error *);
void application_next_map_plan_free(application_next_map_plan *);
bool application_native_mode_next_map_allowed(void *, qa_mode_id);

#endif
