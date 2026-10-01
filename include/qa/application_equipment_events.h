#ifndef QA_APPLICATION_EQUIPMENT_EVENTS_H
#define QA_APPLICATION_EQUIPMENT_EVENTS_H

#include "qa/application.h"

typedef enum qa_application_equipment_event_kind {
    QA_APPLICATION_EQUIPMENT_CONFIGSTRING,
    QA_APPLICATION_EQUIPMENT_SERVER_COMMAND
} qa_application_equipment_event_kind;

typedef struct qa_application_equipment_event {
    qa_application_equipment_event_kind kind;
    qa_actor_owner provider, selected_provider;
    qa_string_id service_owner;
    uint64_t time_ns;
    /* CONFIGSTRING uses the private source index. SERVER_COMMAND uses its
     * physical client number; any negative number is source broadcast. */
    int32_t index;
    /* Targeted commands retain the actual borrowed actor at emission. Zero
     * means broadcast or an unbound source client, never physical client zero. */
    qa_actor_id recipient;
    const char *text;
} qa_application_equipment_event;

/* The separate gear runtime owns these pending events, including candidate
 * Init output. They never change primary GAME configstrings. Text borrows the
 * current runtime until queue clear or retirement. The generation identifies
 * its actual consumption boundary and is preserved by the runtime checkpoint. */
size_t qa_application_equipment_event_count(const qa_application *);
uint64_t qa_application_equipment_events_generation(const qa_application *);
/* The first actual private gear namespace qualifies the queue lifetime. Zero
 * means this runtime has no private gear source. This is not a primary owner. */
qa_actor_owner qa_application_equipment_events_owner(const qa_application *);
bool qa_application_equipment_event_source_current(const qa_application *,
    qa_actor_owner provider, qa_actor_owner selected_provider, qa_string_id service_owner);
bool qa_application_equipment_event_at(const qa_application *, size_t,
    qa_application_equipment_event *, qa_error *);

#endif
