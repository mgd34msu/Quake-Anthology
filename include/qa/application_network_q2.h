#ifndef QA_APPLICATION_NETWORK_Q2_H
#define QA_APPLICATION_NETWORK_Q2_H

#include "qa/application_native_q2_presentation.h"
#include "qa/network_q2_session.h"
#include "qa/network_q2_unicast.h"

typedef struct qa_application_network_q2 qa_application_network_q2;
typedef struct qa_application_network_q2_host {
    qa_application_native_q2_presentation source;
    qa_net_protocol_id protocol;
    qa_cvars *cvars;
    qa_vfs *content;
    uint32_t client_slots, entity_slots;
} qa_application_network_q2_host;
typedef struct qa_application_network_q2_client_slot {
    qa_actor_id actor;
    qa_actor_owner source_owner;
    uint32_t source_slot;
    bool occupied, reserved, connected;
} qa_application_network_q2_client_slot;
typedef struct qa_application_network_q2_recipient_view {
    qa_actor_id actor;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint64_t connection_epoch;
    uint8_t remote_index;
} qa_application_network_q2_recipient_view;
typedef struct qa_application_network_q2_resource_view {
    qa_actor_owner provider;
    const char *instance, *path, *wire_path;
    const qa_vfs *view;
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
    bool missing;
    qa_bytes wire_bytes;
} qa_application_network_q2_resource_view;
typedef struct qa_application_network_q2_metadata {
    qa_actor_owner source_owner;
    qa_application_native_q2_source_kind kind;
    qa_q2_edition edition;
    qa_net_protocol_id protocol;
    uint32_t client_slots, entity_slots;
    int32_t server_count;
    qa_sha256_digest identity, map_identity;
    const char *instance, *map;
    qa_clock_config clock_config;
    qa_clock_state clock;
    uint64_t server_time_ns, publication_generation, map_revision;
    bool archival, materials_bound, materials_capability;
} qa_application_network_q2_metadata;

/* Host discovery and binding require the actual physical ENTITIES GAME. A
 * local seat or selected CHARACTER is not a substitute for source admission. */
bool qa_application_network_q2_host_source(qa_application *, qa_net_protocol_id,
    qa_application_network_q2_host *, qa_error *);
bool qa_application_network_q2_create(qa_application *, qa_net_protocol_id,
    int32_t server_count, qa_application_network_q2 **, qa_error *);
/* Supplied once by the actual Network peer's negotiated Source claim. */
bool qa_application_network_q2_material_capability(qa_application_network_q2 *, bool, qa_error *);
void qa_application_network_q2_destroy(qa_application_network_q2 *);
bool qa_application_network_q2_player(qa_application_network_q2 *, qa_actor_id,
    qa_network_q2_player *, qa_error *);
bool qa_application_network_q2_slot(qa_application_network_q2 *, uint32_t source_slot,
    qa_application_network_q2_client_slot *, qa_error *);

/* The Network owner supplies its authenticated physical seat actors in wire
 * order. Results borrow immutable storage until the next publication call;
 * transport adapters clone or encode them synchronously. */
bool qa_application_network_q2_game_state(qa_application_network_q2 *,
    const qa_actor_id *, size_t seats, qa_q2_game_state *, qa_error *);
/* Does not rebuild or invalidate a previously borrowed entity/frame span. */
bool qa_application_network_q2_configs(qa_application_network_q2 *,
    const qa_q2_config_entry **, size_t *, qa_error *);
bool qa_application_network_q2_frame(qa_application_network_q2 *,
    const qa_actor_id *, size_t seats, qa_q2_wire_frame *, qa_error *);
bool qa_application_network_q2_motion(qa_application_network_q2 *,
    const qa_q2_source_motion **, qa_error *);
bool qa_application_network_q2_status(qa_application_network_q2 *,
    qa_q2_status *, qa_error *);
bool qa_application_network_q2_download_source(qa_application_network_q2 *,
    qa_network_q2_download_source *, qa_error *);
size_t qa_application_network_q2_resource_count(const qa_application_network_q2 *);
bool qa_application_network_q2_resource_read(const qa_application_network_q2 *, size_t,
    qa_application_network_q2_resource_view *, qa_error *);
/* Pure retained scalar custody. An archival receipt grants no live Source
 * membership even when the previous namespace's literal owner is retained. */
bool qa_application_network_q2_metadata_read(const qa_application_network_q2 *,
    qa_application_network_q2_metadata *, qa_error *);

typedef struct qa_application_network_q2_bindings {
    qa_network_runtime *runtime;
    void *context;
    bool (*input)(void *, qa_net_client_id, qa_net_seat_id,
        const qa_network_q2_player *, const qa_q2_usercmd *, uint64_t, qa_error *);
    bool (*drop)(void *, qa_net_client_id, const char *, qa_error *);
    void *recipient_context;
    bool (*recipient)(void *, qa_actor_id, qa_application_network_q2_recipient_view *,
        bool *present, qa_error *);
    bool (*unicast)(void *, const qa_q2_unicast_claim *, bool remember,
        bool *duplicate, qa_error *);
} qa_application_network_q2_bindings;
/* The existing Network owner supplies its genuine raw Source-input entry and
 * retirement marker. All remaining hooks resolve the real canonical roster
 * and the physical GAME, without admitting or simulating another player. */
bool qa_application_network_q2_hooks(qa_application_network_q2 *,
    const qa_application_network_q2_bindings *, qa_network_q2_server_hooks *, qa_error *);
bool qa_application_network_q2_client_frame(qa_application_network_q2 *,
    qa_net_client_id, qa_q2_wire_frame *, qa_error *);
/* Newly begun physical viewers wait for actual Source visibility decisions
 * when instanced entities exist. This never invokes a GAME callback. */
bool qa_application_network_q2_client_frame_ready(qa_application_network_q2 *,
    qa_net_client_id, bool *, qa_error *);
/* Native GAME imports may ask while the real Source invocation is active.
 * This observes its physical client binding and the Network owner's actual
 * connection group; it does not advance or publish a completed frame. */
bool qa_application_network_q2_recipient(qa_application *, qa_actor_owner source,
    qa_actor_id, qa_application_network_q2_recipient_view *, bool *present, qa_error *);
bool qa_application_network_q2_unicast(qa_application *, qa_actor_owner source,
    qa_actor_id, uint32_t key, bool remember, bool *duplicate, qa_error *);
bool qa_application_network_q2_entity_number(qa_application *, qa_actor_owner source,
    qa_actor_id, uint32_t *, qa_error *);
bool qa_application_network_q2_event_entity(qa_application_network_q2 *, qa_actor_owner emitter,
    qa_actor_id, uint32_t *, qa_error *);
bool qa_application_network_q2_event_resource(qa_application_network_q2 *, qa_actor_owner emitter,
    const qa_application_protocol_resource_reference *, uint32_t *, qa_error *);
bool qa_application_network_q2_event_config(qa_application_network_q2 *, uint16_t wire_index,
    const char **actual_text, qa_error *);
bool qa_application_network_q2_event_layout(qa_application_network_q2 *, qa_actor_id recipient,
    qa_native_profile source_profile, const char *actual_layout, qa_error *);
bool qa_application_network_q2_discovery(qa_application_network_q2 *,
    qa_q2_status *, const char **name, const char **map, qa_error *);
bool qa_application_network_q2_download_server(qa_application_network_q2 *,
    const char **, qa_error *);

/* Resource/config numbering belongs to this actual source publication owner.
 * Cold import requires an empty owner bound to the restored physical GAME. */
bool qa_application_network_q2_capture(qa_application_network_q2 *, qa_buffer *, qa_error *);
bool qa_application_network_q2_restore(qa_application_network_q2 *, qa_bytes, qa_error *);
/* A travel capsule may retain the preceding publication after its GAME has
 * retired. This owner preserves custody only and cannot supply Source hooks. */
bool qa_application_network_q2_capture_retained(qa_application_network_q2 *, qa_buffer *, qa_error *);
bool qa_application_network_q2_restore_retained(qa_application *, qa_bytes,
    qa_application_network_q2 **, qa_error *);

#endif
