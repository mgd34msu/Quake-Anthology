#ifndef QA_APPLICATION_NATIVE_Q3_WIRE_STATE_H
#define QA_APPLICATION_NATIVE_Q3_WIRE_STATE_H

#include "internal.h"
#include "qa/network_q3.h"
#include "qa/application_native_q3_wire.h"

bool application_native_q3_wire_create(application_provider *, qa_world *source_world,
    bool restoring, qa_error *);
bool application_native_q3_wire_destroy(application_provider *, qa_error *);
bool application_native_q3_wire_idle(const application_provider *);
bool application_native_q3_wire_destroy_ready(const application_provider *);

/* Engine admission follows the real physical GAME binding. Source gclient,
 * session and movement state remain with their existing gameplay owners. */
bool application_native_q3_wire_connect(application_provider *, uint32_t source_slot,
    qa_actor_id, uint32_t seat_or_none, const char *userinfo, bool bot, qa_error *);
bool application_native_q3_wire_begin(application_provider *, uint32_t source_slot, qa_error *);
bool application_native_q3_wire_disconnect(application_provider *, uint32_t source_slot, qa_error *);
/* Raw engine userinfo can precede Connect, but requires the current full actor
 * binding for that original fixed client. It never creates engine admission. */
bool application_native_q3_wire_userinfo(application_provider *, uint32_t source_slot,
    const char *, qa_error *);
bool application_native_q3_wire_userinfo_read(application_provider *, uint32_t source_slot,
    const char **, qa_error *);
bool application_native_q3_wire_source_userinfo_read(application_provider *, uint32_t source_slot,
    const char **, qa_error *);
bool application_native_q3_wire_command(application_provider *, uint32_t source_slot,
    const qa_q3_usercmd *, qa_error *);
bool application_native_q3_wire_command_seed(application_provider *, uint32_t source_slot,
    const qa_q3_usercmd *, qa_error *);

typedef struct application_native_q3_wire_client_view {
    qa_actor_id actor;
    const char *userinfo;
    qa_q3_usercmd command;
    uint32_t seat;
    uint64_t entered_ns;
    bool begun, bot, command_received;
} application_native_q3_wire_client_view;
bool application_native_q3_wire_client_read(application_provider *, uint32_t,
    application_native_q3_wire_client_view *, bool *present, qa_error *);
bool application_native_q3_wire_client_admission_read(application_provider *, uint32_t,
    application_native_q3_wire_client_view *, bool *present, qa_error *);
bool application_native_q3_wire_drop(application_provider *, uint32_t,
    const char *reason, qa_error *);
bool application_native_q3_wire_drop_read(application_provider *, uint32_t,
    const char **reason, bool *pending, qa_error *);
bool application_native_q3_wire_drop_client_read(application_provider *, uint32_t,
    qa_actor_id *, const char **reason, bool *pending, qa_error *);
bool application_native_q3_wire_drop_transport(application_provider *, uint32_t, qa_error *);

typedef struct application_native_q3_wire_publication {
    int32_t next_message, reliable_sequence, previous_time;
    uint8_t snapshot_bit;
    bool gamestate_needed, snapshot_needed, has_snapshot;
} application_native_q3_wire_publication;
bool application_native_q3_wire_local_publication(application_provider *, uint32_t,
    application_native_q3_wire_publication *, bool *wanted, qa_error *);
bool application_native_q3_bot_snapshot_entity(application_provider *, qa_actor_id,
    int32_t index, int32_t *entity_number, bool *present, qa_error *);

/* Real source observations are copied into the connected transport owner.
 * Neither producer infers source words from canonical actor storage. */
bool application_native_q3_wire_gamestate(application_provider *, uint32_t,
    const qa_q3_gamestate *, qa_error *);
bool application_native_q3_wire_snapshot(application_provider *, uint32_t,
    const qa_q3_snapshot *, int32_t ping, qa_error *);
bool application_native_q3_wire_snapshot_bit(application_provider *, uint8_t *, qa_error *);

typedef struct application_native_q3_wire_client_lease application_native_q3_wire_client_lease;
typedef struct application_native_q3_wire_client_topology {
    application_provider *source;
    qa_actor_owner source_owner, receiver;
    uint32_t seat, source_slot;
} application_native_q3_wire_client_topology;
bool application_native_q3_wire_client_bind(application_provider *, qa_actor_owner receiver,
    uint32_t seat, uint32_t source_slot, qa_command_tokens *actual_role_arguments,
    application_native_q3_wire_client_lease **empty, qa_q3_host_options *, qa_error *);
bool application_native_q3_wire_client_unbind(application_native_q3_wire_client_lease **,
    qa_error *);
bool application_native_q3_wire_client_arguments(application_native_q3_wire_client_lease *,
    qa_native_host_command_view *, qa_error *);
bool application_native_q3_wire_client_command(application_native_q3_wire_client_lease *,
    const char *, qa_error *);
bool application_native_q3_wire_client_effect(application_native_q3_wire_client_lease *,
    qa_application_q3_client_effect, const char *, qa_error *);
bool application_native_q3_wire_client_topology_read(application_native_q3_wire_client_lease *,
    application_native_q3_wire_client_topology *, qa_error *);
bool application_native_q3_wire_client_time(application_native_q3_wire_client_lease *,
    int32_t *, qa_error *);

typedef struct application_native_q3_bot_cycle application_native_q3_bot_cycle;
bool application_native_q3_bot_cycle_begin(application_provider *, const qa_source_frame *,
    uint64_t host_ns, application_native_q3_bot_cycle **empty, qa_error *);
void application_native_q3_bot_cycle_end(application_native_q3_bot_cycle **owned);

bool application_native_q3_wire_round_ready(application_provider *, qa_error *);
bool application_native_q3_wire_round_begin(application_provider *, qa_error *);
bool application_native_q3_wire_round_bind(application_provider *, uint32_t,
    qa_actor_id, qa_error *);
bool application_native_q3_wire_round_finish(application_provider *, qa_error *);
bool application_native_q3_wire_map_begin(application_provider *, qa_error *);
bool application_native_q3_wire_map_finish(application_provider *, qa_error *);

typedef struct application_native_q3_wire_carry application_native_q3_wire_carry;
bool application_native_q3_wire_carry_capture(application_provider *,
    application_native_q3_wire_carry **empty, qa_error *);
bool application_native_q3_wire_carry_import(application_provider *,
    const application_native_q3_wire_carry *, qa_error *);
bool application_native_q3_wire_carry_refresh(application_provider *,
    const application_native_q3_wire_carry *, qa_error *);
bool application_native_q3_wire_carry_finish(application_provider *, qa_error *);
void application_native_q3_wire_carry_dispose(application_native_q3_wire_carry *);

bool application_native_q3_send_command(application_provider *, int32_t source_slot_or_minus_one,
    const char *, qa_error *);
bool application_native_q3_server_command(void *, int32_t, const char *, qa_error *);
bool application_native_q3_configstring_changed(void *, uint32_t, const char *, qa_error *);
bool application_native_q3_bot_console(application_provider *, qa_actor_id,
    char *, size_t, bool *found, qa_error *);

/* Private provider continuation; restore targets an empty candidate and finish
 * qualifies saved physical bindings after the real GAME rows are restored. */
bool application_native_q3_wire_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q3_wire_restore(application_provider *, qa_bytes, qa_error *);
bool application_native_q3_wire_finish(application_provider *, qa_error *);

#endif
