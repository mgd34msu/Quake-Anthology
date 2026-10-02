#ifndef QA_APPLICATION_UNIFIED_EVENTS_H
#define QA_APPLICATION_UNIFIED_EVENTS_H

#include "network_unified.h"

typedef enum application_event_queue {
    APPLICATION_EVENT_BUILTIN, APPLICATION_EVENT_Q2_MAP,
    APPLICATION_EVENT_Q3_MAP, APPLICATION_EVENT_Q2_PLAYER,
    APPLICATION_EVENT_PROTOCOL
} application_event_queue;

/* References the actual payload owner. Sequence is allocated by emit, before
 * any consumer groups events by family or receiver. */
typedef struct application_event_journal_record {
    uint64_t sequence;
    application_event_queue queue;
    size_t index;
    qa_source_frame frame;
    bool has_frame;
} application_event_journal_record;

typedef struct application_unified_event_record {
    qa_bytes presentation, simulation;
    uint64_t order, presentation_sequence, simulation_sequence, time_ns, simulation_time_ns;
    qa_clock_kind clock, presentation_clock;
    qa_actor_id recipient, simulation_recipient;
    qa_saved_actor_id recipient_saved, simulation_recipient_saved;
    qa_net_client_id client;
    qa_actor_owner provider;
    qa_string_id content;
    int32_t source_entity;
    bool has_source_entity;
    bool link_presentation;
    /* Native receipt: payload ActorIds use imported checkpoint history after
     * decode. The original pair stays in the continuation until projection. */
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

typedef struct application_unified_persistent_event {
    application_unified_event_record event;
    qa_buffer key, payload;
} application_unified_persistent_event;

bool application_unified_persistent_key(qa_application *,
    const application_unified_event_record *, qa_buffer *, bool *remove, qa_error *);
void application_unified_persistent_dispose(qa_application *);
bool application_unified_persistent_retire(qa_application *, qa_actor_owner,
    qa_actor_id recipient, qa_error *);
bool application_unified_event_owner_retire(qa_application *, qa_actor_owner,
    const qa_source_frame *actual_primary_clock, qa_error *);

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

/* Payloads are real SourcePresentationEvent / SimulationEventPayload values,
 * without their sequence/time envelopes. The actual emitter supplies the
 * source slot and recipient; absent payloads do not allocate that stream's
 * sequence. JSON is copied before returning. */
bool application_unified_event_emit(qa_application *, qa_actor_owner,
    qa_bytes presentation, qa_bytes simulation, qa_actor_id recipient,
    qa_actor_id simulation_recipient, uint64_t time_ns, int32_t source_entity,
    bool has_source_entity, bool link_presentation, qa_error *);
bool application_unified_event_payload_valid(qa_bytes, bool presentation, bool link_presentation, qa_error *);
bool application_unified_event_actors_valid(qa_application *, const application_unified_event_record *, qa_error *);
bool application_unified_event_recipient(qa_application *, const application_unified_event_record *,
    bool simulation, qa_actor_id *, qa_error *);
bool application_unified_damage_emit(qa_application *, const qa_damage_outcome *, qa_error *);

bool application_event_journal_reserve(qa_application *, qa_error *);
void application_event_journal_append(qa_application *, application_event_queue,
    size_t index, qa_actor_owner);

typedef struct application_unified_events {
    qa_unified_document *simulation;
    qa_unified_document *world_text;
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
