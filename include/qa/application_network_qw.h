#ifndef QA_APPLICATION_NETWORK_QW_H
#define QA_APPLICATION_NETWORK_QW_H
#include "qa/application.h"
#include "qa/network_q1_qw.h"
#include "qa/network_runtime.h"

/* Authenticate selected control separately from the literal source commands. */
bool qa_application_network_qw_commands(qa_application *,
    const qa_network_command_group *, qa_error *);

/* Borrow the actual canonical player's retained source userinfo. This pure
 * read may run inside its source callback. An absent owner is NOT_FOUND; a
 * retained empty source string is valid. No field dictionary is synthesized. */
bool qa_application_network_qw_userinfo_read(qa_application *, qa_actor_id,
    const char **, qa_error *);

typedef struct qa_application_network_qw_source {
    qa_actor_owner owner;
    uint32_t entity_count;
    uint64_t source_time_ns, completed_time_ns;
    qa_cvars *cvars;
} qa_application_network_qw_source;
/* The actual primary QW source retains 32 physical rows, ordered resources,
 * a completed QW frame and the matching player/stat/event producers. */
bool qa_application_network_qw_source_read(qa_application *,
    qa_application_network_qw_source *, qa_error *);

typedef struct qa_application_network_qw_world {
    qa_application_network_qw_source source;
    qa_net_protocol_id protocol;
    uint32_t max_clients;
    const char *game_directory, *map, *level, *lightstyles[64];
    qa_qw_movevars movement;
    qa_bytes map_bytes;
} qa_application_network_qw_world;
/* Read the actual primary source without selecting a player. Text and map
 * bytes borrow their real owners until application mutation. The map bytes
 * supply the original checksum producer; no checksum is inferred from a name. */
bool qa_application_network_qw_world_read(qa_application *,
    qa_application_network_qw_world *, qa_error *);
/* Borrow the primary source's retained stamped signon emissions. */
bool qa_application_network_qw_signon_count(qa_application *, size_t *, qa_error *);
bool qa_application_network_qw_signon_at(qa_application *, size_t,
    qa_application_protocol_event *, qa_error *);
/* Clear the actual reserved source row at host.spawn, before genuine Begin.
 * This invokes no guest callback and preserves the full physical admission. */
bool qa_application_network_qw_prepare(qa_application *, qa_actor_id, qa_error *);

/* Actual source Math.trunc values precede low-byte wire conversion. Presence
 * and delta decisions must retain these values, including values above 255. */
typedef struct qa_application_network_qw_entity {
    uint32_t number;
    double model, frame, colormap, skin, effects;
    float origin[3], angles[3];
} qa_application_network_qw_entity;

typedef struct qa_application_network_qw_client {
    qa_actor_id actor;
    qa_actor_id spectator_track;
    uint32_t source_slot;
    bool begun, spectator;
    qa_application_network_qw_entity entity;
    float velocity[3], view_offset[3], minimum[3], health, frags;
    double weapon_frame;
    uint16_t stat_mask;
    double stats[16];
    qa_movement_command command;
    uint64_t command_time_ns;
    bool command_present;
} qa_application_network_qw_client;
/* Full canonical generations map through genuine physical source rows,
 * independently of the selected Character owner. Source shared fields retain their native or QC owners; no game callback or raw command is invented. */
bool qa_application_network_qw_client_read(qa_application *, qa_actor_id,
    qa_application_network_qw_client *, qa_error *);
/* Connected physical rows, including local or borrowed source clients. */
bool qa_application_network_qw_client_next(qa_application *, uint32_t *cursor,
    bool *present, qa_application_network_qw_client *, qa_error *);
/* Source-eye fat PVS against the target's retained source link envelope. */
bool qa_application_network_qw_visible(qa_application *, qa_actor_id viewer,
    qa_actor_id target, bool *, qa_error *);
bool qa_application_network_qw_receives(qa_application *, qa_actor_id,
    const qa_application_protocol_event *, bool *, qa_error *);
/* Actual source actions run at the installed completed-frame command cut. */
bool qa_application_network_qw_kill(qa_application *, qa_actor_id, bool *killed, qa_error *);
bool qa_application_network_qw_pause(qa_application *, qa_actor_id,
    qa_buffer *announcement, bool *changed, qa_error *);
bool qa_application_network_qw_ptrack(qa_application *, qa_actor_id,
    bool target_supplied, int32_t client_slot, qa_error *);
bool qa_application_network_qw_userinfo(qa_application *, qa_actor_id,
    const char *, qa_error *);
bool qa_application_network_qw_flush(qa_application *, qa_error *);
qa_vfs *qa_application_network_qw_content(qa_application *, qa_error *);
/* Source-owned dynamic edicts with a real model, in physical slot order.
 * Start cursor at zero. Completed inventory returns present=false. */
bool qa_application_network_qw_entity_next(qa_application *, uint32_t *cursor,
    bool *present, qa_actor_id *, qa_application_network_qw_entity *, qa_error *);
/* Names borrow the actual indexed source precaches until source mutation. */
bool qa_application_network_qw_precache(qa_application *, bool models,
    const char *names[255], size_t *count, qa_error *);

#endif
