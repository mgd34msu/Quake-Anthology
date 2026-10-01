#ifndef QA_APPLICATION_EQUIPMENT_RUNTIME_H
#define QA_APPLICATION_EQUIPMENT_RUNTIME_H

#include "internal.h"
#include "guest_q3_gear.h"
#include "qa/save.h"
#include "equipment_events.h"

typedef struct application_equipment_runtime application_equipment_runtime;
typedef enum application_equipment_source_event_kind {
    APPLICATION_EQUIPMENT_CONFIGSTRING,
    APPLICATION_EQUIPMENT_SERVER_COMMAND
} application_equipment_source_event_kind;
typedef struct application_equipment_source_event {
    application_equipment_source_event_kind kind;
    qa_actor_owner provider, selected_provider;
    qa_string_id service_owner;
    qa_actor_id recipient;
    uint64_t time_ns;
    int32_t index;
    const char *text;
} application_equipment_source_event;
typedef struct application_equipment_runtime_options {
    qa_application *application;
    const qa_launch_snapshot *snapshot;
    application_provider *const *providers;
    size_t provider_count;
    application_provider *world_source;
    qa_builtin_services services;
    qa_bytes entity_text;
} application_equipment_runtime_options;
typedef struct application_equipment_runtime_source {
    qa_actor_owner selected_owner, gear_owner;
    qa_item_id weapon_item;
    qa_string_id service_owner;
    const qa_launch_instance *descriptor;
    const qa_resource *artifact;
    const qa_vfs_acquisition *acquisition;
    qa_vfs *content;
    const application_q3_grapple_definition *definition;
    application_q3_gear *gear;
} application_equipment_runtime_source;

/* saved_roster is the genuine nested runtime payload, decoded before private
 * executors are constructed. Restore never runs filtering, defaults or Init.
 * Initialize *out to NULL. A failed constructor sets it only when genuine
 * cleanup refused; that owner and its providers/world must remain retained
 * until checked destruction succeeds. Once bound, the equipment controller
 * consumes the runtime on successful checked destruction. */
bool application_equipment_runtime_create(const application_equipment_runtime_options *,
    qa_bytes saved_roster, application_equipment_runtime **, qa_error *);
bool application_equipment_runtime_saved(qa_session *, const qa_save_image *,
    qa_bytes *, qa_error *);
bool application_equipment_runtime_idle(const application_equipment_runtime *);
bool application_equipment_runtime_destroy(application_equipment_runtime *, qa_error *);
void application_equipment_runtime_bind(application_equipment_runtime *, qa_equipment_options *);
/* Genuine participant callbacks and clocks enter the same session admission
 * as the selected providers. These functions execute no GAME callbacks. */
bool application_equipment_runtime_prepare_components(application_equipment_runtime *, qa_error *);
bool application_equipment_runtime_validate_components(application_equipment_runtime *, qa_error *);
bool application_equipment_runtime_commit_components(application_equipment_runtime *, qa_error *);
void application_equipment_runtime_abort_components(application_equipment_runtime *);
size_t application_equipment_runtime_component_count(const application_equipment_runtime *);
bool application_equipment_runtime_component_at(const application_equipment_runtime *, size_t,
    const qa_component **, qa_error *);
size_t application_equipment_runtime_source_count(const application_equipment_runtime *);
bool application_equipment_runtime_source_at(const application_equipment_runtime *, size_t,
    application_equipment_runtime_source *, qa_error *);
bool application_equipment_runtime_owner_current(const application_equipment_runtime *, qa_actor_owner);
bool application_equipment_runtime_actor_released(application_equipment_runtime *,
    qa_actor_record, qa_error *);
application_equipment_events *application_equipment_runtime_events(const application_equipment_runtime *);
bool application_equipment_runtime_event_current(const application_equipment_runtime *,
    const qa_application_equipment_event *);

#endif
