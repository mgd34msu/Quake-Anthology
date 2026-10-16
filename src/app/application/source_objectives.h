#ifndef APPLICATION_SOURCE_OBJECTIVES_H
#define APPLICATION_SOURCE_OBJECTIVES_H

#include "qa/json.h"
#include "qa/modes.h"

typedef struct application_source_objectives application_source_objectives;

/* A source storage identity includes its resolved selector generation. */
typedef struct application_source_objective_location {
    uint64_t address, selector;
    qa_actor_id actor;
} application_source_objective_location;

typedef struct application_source_objective_services {
    void *context;
    bool (*current)(void *, bool callback, qa_error *);
    bool (*normalize)(void *, size_t, double *, qa_error *);
    bool (*locate)(void *, size_t, application_source_objective_location [3], qa_error *);
    bool (*read)(void *, size_t, const application_source_objective_location [3],
        double *, qa_actor_id *, qa_actor_id *, qa_error *);
    bool (*write)(void *, size_t, const application_source_objective_location [3],
        double, qa_actor_id, qa_actor_id, qa_error *);
    bool (*change)(void *, size_t, double, const qa_objective_state *, qa_error *);
} application_source_objective_services;

bool application_source_objectives_create(qa_strings *, qa_actor_owner,
    const qa_json_document *, qa_json_id, const application_source_objective_services *,
    application_source_objectives **, qa_error *);
bool application_source_objectives_activate(application_source_objectives *, qa_modes *, qa_error *);
bool application_source_objectives_suspend(application_source_objectives *, qa_error *);
bool application_source_objectives_refresh(application_source_objectives *, qa_modes *, qa_error *);
bool application_source_objectives_flush(application_source_objectives *, qa_modes *, qa_error *);
bool application_source_objectives_sync(application_source_objectives *, qa_modes *, qa_error *);
bool application_source_objectives_ready(const application_source_objectives *, qa_error *);
bool application_source_objectives_active(const application_source_objectives *);
void application_source_objectives_destroy(application_source_objectives *);

#endif
