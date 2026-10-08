#ifndef QA_APPLICATION_UNIFIED_EVENTS_H
#define QA_APPLICATION_UNIFIED_EVENTS_H

#include "network_unified.h"
#include "qa/unified_frame_events.h"
#include "event_pages.h"

typedef struct application_unified_event_record {
    qa_unified_presentation_payload *presentation;
    qa_unified_simulation_payload *simulation;
    uint64_t order, presentation_sequence, simulation_sequence, time_ns, simulation_time_ns;
    qa_clock_kind clock, presentation_clock;
    qa_game_family family;
    qa_actor_id recipient, simulation_recipient;
    qa_saved_actor_id recipient_saved, simulation_recipient_saved;
    qa_net_client_id client;
    qa_actor_owner provider;
    qa_string_id content;
    int32_t source_entity;
    bool has_source_entity;
    bool link_presentation;
    /* Imported ActorIds retain checkpoint history through saved continuation
     * validation, then restore finish rebinds them once to current actors. */
    bool payload_checkpoint;
    uint64_t owner_generation;
    /* Emission-time GAME rules receipt: 0 absent, 1 classic, 2 rerelease.
     * Historical rows never rediscover it from a later selected product. */
    uint8_t q2_source_profile;
    uint64_t q2_source_interval_ns;
} application_unified_event_record;

typedef struct application_unified_event_owner {
    qa_actor_owner provider;
    qa_string_id content;
    uint64_t generation;
    bool active;
} application_unified_event_owner;
struct application_provider;
bool application_unified_event_owner_bind(qa_application *, struct application_provider *,
    bool restoring, qa_error *);
bool application_unified_event_owner_bound_is(const qa_application *, const struct application_provider *);
bool application_unified_event_owner_prepare(qa_application *, struct application_provider *,
    struct application_provider *previous, qa_error *);
bool application_unified_event_owner_publish(qa_application *, struct application_provider *,
    struct application_provider *previous, qa_error *);
bool application_unified_event_component_owner_bind(qa_application *, qa_actor_owner,
    bool restoring, qa_error *);

typedef struct application_unified_event_source {
    qa_actor_owner owner;
    const qa_launch_instance *descriptor;
    const qa_product *product;
    qa_vfs *content;
    qa_clock_kind clock;
    bool component;
} application_unified_event_source;
bool application_unified_event_source_read(qa_application *, qa_actor_owner,
    application_unified_event_source *, qa_error *);

typedef struct application_persistent_key {
    uint64_t generation, selector, actor_registry, actor_generation;
    uint64_t recipient_registry, recipient_generation;
    uint32_t provider, domain, actor_slot, recipient_slot;
    int32_t channel;
    qa_string_id resource;
} application_persistent_key;

typedef struct application_unified_persistent_event {
    application_unified_event_record event;
    application_persistent_key key;
    application_event_lease *lease;
} application_unified_persistent_event;

bool application_unified_persistent_key(qa_application *,
    const application_unified_event_record *, application_persistent_key *, bool *remove, qa_error *);
bool application_unified_persistent_key_equal(const application_persistent_key *,
    const application_persistent_key *);
void application_unified_persistent_dispose(qa_application *);
bool application_unified_persistent_retire(qa_application *, qa_actor_owner,
    qa_actor_id recipient, qa_error *);
bool application_unified_event_owner_retire(qa_application *, qa_actor_owner,
    const qa_source_frame *actual_primary_clock, qa_error *);
bool application_unified_events_restore_finish(qa_application *, qa_error *);

typedef struct application_unified_event_resource_custody {
    qa_resource *resource;
    qa_resource_pool *pool;
    qa_vfs *view;
    qa_vfs_acquisition opening;
    uint64_t saved_pool, saved_resource, saved_view;
} application_unified_event_resource_custody;

typedef struct application_unified_event_resource {
    qa_actor_owner provider;
    qa_string_id content, path;
    qa_resource *resource;
    qa_resource_pool *pool;
    qa_vfs *view;
    qa_vfs_acquisition opening;
    qa_launch_instance_lease *descriptor;
    qa_buffer key;
    char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    uint64_t saved_pool, saved_resource, saved_view;
    application_unified_event_resource_custody *custodies;
    size_t custody_count, custody_capacity;
} application_unified_event_resource;

typedef struct application_unified_event_registration {
    qa_actor_owner provider;
    qa_string_id path;
    size_t resource;
    qa_native_host_resource_kind kind;
    uint64_t custody;
} application_unified_event_registration;

/* Registration consumes no VFS read. It admits only an actual held opening in
 * the emitting Source's retained content view, and retains that acquisition. */
bool application_unified_event_resource_register(qa_application *, qa_actor_owner,
    const char *requested_path, const qa_resource *, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *);
bool application_unified_event_resource_register_acquired(qa_application *, qa_actor_owner,
    qa_native_host_resource_kind, const char *logical_name, const qa_vfs *, const qa_resource *,
    const qa_vfs_acquisition *, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *);
const qa_resource *application_unified_event_resource_read(const qa_application *, const char *id);
bool application_unified_event_resource_lookup(qa_application *, qa_actor_owner,
    const char *requested_path, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], bool *found, qa_error *);
bool application_unified_event_resource_lookup_kind(qa_application *, qa_actor_owner,
    qa_native_host_resource_kind, const char *, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], bool *found, qa_error *);
bool application_unified_event_resource_lookup_receipt(qa_application *, qa_actor_owner,
    qa_native_host_resource_kind, const char *, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], uint64_t *custody, bool *found, qa_error *);
bool application_unified_event_resource_receipt_read(const qa_application *, const char *id,
    uint64_t custody, const qa_resource **, const qa_vfs **, const qa_vfs_acquisition **, qa_error *);
bool application_unified_event_registration_clear(qa_application *, qa_actor_owner, qa_error *);
void application_unified_events_resources_dispose(qa_application *);
size_t application_unified_event_resource_count(const qa_application *);
const application_unified_event_resource *application_unified_event_resource_at(const qa_application *, size_t);

typedef struct application_unified_world_text {
    qa_string_id content, text;
    qa_actor_owner provider;
    qa_vec3 origin, angles, color;
    float alpha, cell_size;
    double expires;
    uint64_t first_frame;
    bool timed, observed, billboard, depth_test;
} application_unified_world_text;
bool application_unified_world_text_emit(qa_application *, qa_actor_owner,
    const qa_q2_map_event *, qa_error *);

/* Views share the raw event's page lease. Absent views consume no wire sequence. */
bool application_unified_event_emit(qa_application *, qa_actor_owner,
    const qa_unified_presentation_payload *presentation,
    const qa_unified_simulation_payload *simulation, qa_actor_id recipient,
    qa_actor_id simulation_recipient, uint64_t time_ns, int32_t source_entity,
    bool has_source_entity, bool link_presentation, qa_error *);
/* Append to the current raw/import transaction without source revalidation. */
bool application_unified_event_append(qa_application *,
    const application_unified_event_record *, qa_error *);
void application_unified_events_consume(qa_application *, uint64_t next);
void application_unified_events_clear(qa_application *);
bool application_unified_builtin_read(qa_application *, const qa_builtin_event *, qa_unified_builtin_event *, qa_error *);
void application_unified_builtin_read_dispose(qa_unified_builtin_event *);
bool application_unified_world_text_read(qa_application *, const application_unified_source *,
    qa_unified_frame_lease *, qa_unified_frame_visuals *, qa_error *);
bool application_unified_event_actors_valid(qa_application *, const application_unified_event_record *, qa_error *);
bool application_unified_event_recipient(qa_application *, const application_unified_event_record *,
    bool simulation, qa_actor_id *, qa_error *);
bool application_unified_damage_emit(qa_application *, const qa_damage_outcome *, qa_error *);

typedef struct application_unified_events {
    qa_unified_document **controls;
    size_t control_count;
    qa_application *application;
    application_unified_source source;
    qa_net_client_id recipient;
    qa_unified_session_player player;
    uint64_t generation, through;
    size_t count;
    size_t resource_count;
    uint64_t registration_revision;
    uint64_t world_text_revision;
    uint64_t persistent_revision;
} application_unified_events;

bool application_unified_events_read(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *, uint32_t epoch,
    uint64_t after, application_unified_events *, qa_error *);
/* Actual admission reads retained presentation only. It owns no simulation
 * replay or world-text snapshot; through is committed by the caller after
 * retaining every control. */
bool application_unified_events_initial_read(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *, uint32_t epoch,
    application_unified_events *, qa_error *);
bool application_unified_events_current(const application_unified_events *);
void application_unified_events_dispose(application_unified_events *);

#endif
