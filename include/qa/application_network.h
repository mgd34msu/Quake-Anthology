#ifndef QA_APPLICATION_NETWORK_H
#define QA_APPLICATION_NETWORK_H
#include "qa/application.h"
#include "qa/application_players.h"
#include "qa/network_runtime.h"
#include "qa/network_q1_nq.h"

#define QA_NETWORK_COMMAND_OWNER UINT64_C(0x71616e6574770001)
bool qa_application_network_command_owner_bound(const qa_application *);

/* These adapters borrow the application. Seat authority is always the
 * canonical roster, and travel resolves its freshly published actor IDs. */
bool qa_application_network_controlled(qa_application *, qa_net_client_id,
    qa_net_seat_id, qa_actor_id, qa_movement_kind, qa_bytes arsenal, qa_error *);
bool qa_application_network_command(qa_application *, const qa_network_command *, qa_error *);
bool qa_application_network_q3_command(qa_application *,
    const qa_network_q3_source_command *, qa_error *);
/* Publish the actual first source usercmd before canonical ClientBegin. This
 * seed is not a transport command acknowledgement or a second Think call. */
bool qa_application_network_q3_enter(qa_application *, qa_net_client_id,
    qa_net_seat_id, const qa_q3_usercmd *, qa_error *);
bool qa_application_network_resolve(qa_application *, const qa_net_client *,
    uint32_t actor_slot, uint32_t actor_generation, qa_net_seat_id *,
    qa_unified_controlled_actor *, qa_error *);
bool qa_application_network_detach(qa_application *, const qa_net_client *, qa_error *);

typedef struct qa_application_network_player {
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    uint32_t application_seat, client_slot, source_slot;
    bool retiring, deferred, source_begin_pending;
} qa_application_network_player;
/* Reads the actual remote roster, including a retained retirement. It neither
 * admits a player nor calls its source owner. The cursor is a physical row. */
bool qa_application_network_player_next(const qa_application *, size_t *cursor,
    qa_application_network_player *);

/* Original Q1 source observations use an admitted classic QuakeC gameplay
 * owner and its real borrowed client/owned edict/precache namespace. These
 * normal source reads may refresh canonical client projections. Builtin and mixed gameplay
 * sources need a complete native wire producer and fail this admission. */
bool qa_application_network_q1_source(qa_application *, qa_actor_id,
    qa_actor_owner *, uint32_t *source_slot, qa_net_protocol_id *, qa_error *);
/* Real source high-water edict extent and reserved client extent. Readonly;
 * no edict refresh or source callback is performed. */
bool qa_application_network_q1_extents(qa_application *, qa_actor_id,
    uint32_t *client_slots, uint32_t *entity_slots, qa_error *);
qa_cvars *qa_application_network_q1_cvars(qa_application *, qa_actor_id, qa_error *);
bool qa_application_network_q1_entity(qa_application *, qa_actor_id source_player,
    qa_actor_id entity, qa_q1_entity *, qa_error *);
/* Iterate actual source edicts in source-slot order. Cursor starts at zero;
 * present distinguishes the completed inventory from failed source admission. */
bool qa_application_network_q1_entity_next(qa_application *, qa_actor_id source_player,
    uint32_t *cursor, bool *present, qa_actor_id *, qa_q1_entity *, qa_error *);
bool qa_application_network_q1_eye(qa_application *, qa_actor_id source_player,
    qa_vec3 *, qa_error *);
/* Bounds are the real source absmin/absmax after canonical borrowed-body
 * refresh. has_model requires both its model index and retained model name. */
bool qa_application_network_q1_bounds(qa_application *, qa_actor_id source_player,
    qa_actor_id entity, qa_bounds *, bool *has_model, qa_error *);
/* Names borrow the source until mutation; output arrays are caller-owned. */
bool qa_application_network_q1_precache(qa_application *, qa_actor_id,
    bool models, const char *names[255], size_t *count, qa_error *);
typedef struct qa_application_network_q1_world {
    qa_net_protocol_id protocol;
    uint32_t max_clients;
    bool standard_quake, deathmatch;
    float seconds;
    const char *map, *level, *lightstyles[64];
    int32_t total_secrets, total_monsters, found_secrets, killed_monsters;
} qa_application_network_q1_world;
bool qa_application_network_q1_world_read(qa_application *, qa_actor_id,
    qa_application_network_q1_world *, qa_error *);
bool qa_application_network_q1_clientdata(qa_application *, qa_actor_id,
    qa_q1_clientdata *, qa_error *);
typedef struct qa_application_network_q1_status_player {
    qa_actor_id actor;
    uint32_t source_slot;
    const char *name;
    int32_t frags;
    float source_frags;
    uint8_t colors;
    bool spawned;
} qa_application_network_q1_status_player;
/* Connected source clients, including local clients, in physical slot order.
 * Names borrow actual QC strings until source mutation. */
bool qa_application_network_q1_status(qa_application *, qa_actor_id source_player,
    qa_application_network_q1_status_player players[255], size_t *count, qa_error *);
/* Actual connected source clients selected by the source teamplay/team values.
 * The sender name borrows its QC string until source mutation. */
bool qa_application_network_q1_chat_recipients(qa_application *, qa_actor_id sender,
    bool team_only, const char **name, qa_actor_id recipients[255], size_t *count, qa_error *);
/* Invoke the real ClientKill callback for a healthy spawned nonspectator.
 * Reserved, spectator and already-dead source clients remain unchanged. */
bool qa_application_network_q1_kill(qa_application *, qa_actor_id, qa_error *);
/* Toggle the actual source pause when its optional pausable policy permits.
 * Returns owned terminated announcement text and whether the flag changed.
 * QuakeWorld spectators receive the original denial without changing state. */
bool qa_application_network_q1_pause(qa_application *, qa_actor_id,
    qa_buffer *empty_text, bool *changed, qa_error *);
bool qa_application_network_q1_name(qa_application *, qa_actor_id, const char *, qa_error *);
bool qa_application_network_q1_colors(qa_application *, qa_actor_id,
    int32_t top, int32_t bottom, qa_error *);
typedef struct qa_application_network_q1_feedback {
    bool damage, set_angle;
    uint8_t armor, blood;
    double origin[3];
    float angles[3];
} qa_application_network_q1_feedback;
/* Consume the source damage and fixangle fields once, after complete native
 * wire qualification. Requires a spawned classic NetQuake client. */
bool qa_application_network_q1_consume_feedback(qa_application *, qa_actor_id,
    qa_application_network_q1_feedback *, qa_error *);
bool qa_application_network_q1_baseline(qa_application *, qa_actor_id,
    const qa_q1_entity *, qa_q1_entity *, qa_error *);
/* The original QuakeC donor captures a baseline for every reserved client
 * source slot, including a physical row without a live canonical actor. */
bool qa_application_network_q1_client_baseline(qa_application *, qa_actor_id source_player,
    uint32_t source_slot, qa_q1_entity *, qa_error *);
bool qa_application_network_q1_signon_count(qa_application *, qa_actor_id,
    size_t *, qa_error *);
bool qa_application_network_q1_signon_at(qa_application *, qa_actor_id, size_t,
    qa_application_protocol_event *, qa_error *);

/* A caller owns this observation storage; snapshot.entities points into it.
 * Native primary GAME observations use its typed physical source records and
 * selected movement/mode services. Original hosts use qualified source ABI
 * records. Both retain the actual source world and collision visibility. */
typedef struct qa_application_network_q3_frame {
    qa_q3_snapshot snapshot;
    qa_q3_visible_entities visible;
} qa_application_network_q3_frame;
const qa_q3_gamestate *qa_application_network_q3_gamestate(qa_application *, qa_actor_id);
/* Native physical admission is independent of the canonical character owner.
 * Original hosts retain their qualified source composition requirement. */
bool qa_application_network_q3_source(qa_application *, qa_actor_id,
    uint32_t *source_slot, qa_q3_product *, qa_error *);
/* Pure full-generation physical identity for the actual primary GAME service
 * callback. Does not require an idle frame; retained pending DROP still owns
 * its source row. Another source owner does not match this recipient. */
bool qa_application_network_q3_client_bound(qa_application *, qa_actor_owner,
    qa_actor_id, uint32_t source_slot);
/* Read the actual primary GAME owner without requiring a local player.
 * Candidate source admission may be pending; no source callback runs. */
bool qa_application_network_q3_owner(qa_application *, qa_actor_owner *, qa_q3_product *, qa_error *);
qa_cvars *qa_application_network_q3_host_cvars(qa_application *, qa_actor_owner, qa_error *);
typedef struct qa_application_network_q3_host_slot {
    bool occupied, bot;
} qa_application_network_q3_host_slot;
bool qa_application_network_q3_host_slots(qa_application *, qa_actor_owner,
    qa_application_network_q3_host_slot slots[64], qa_error *);
/* Replace only a genuine physically admitted source bot. Queued Begin is
 * removed before the actual GAME disconnect and canonical slot retirement. */
bool qa_application_network_q3_drop_bot(qa_application *, qa_actor_owner,
    uint32_t source_slot, qa_error *);
/* Copy only complete linked physical baselines at accepted Connect. This
 * calls no source export and does not publish configstrings or a gamestate. */
bool qa_application_network_q3_host_baselines(qa_application *, qa_actor_owner, qa_q3_gamestate *, qa_error *);
qa_cvars *qa_application_network_q3_cvars(qa_application *, qa_actor_id);
typedef struct qa_application_network_q3_status_player {
    uint32_t slot;
    int32_t score, ping;
    char name[1024];
} qa_application_network_q3_status_player;
/* Names copy the actual native cleaned netname or original guest userinfo. */
bool qa_application_network_q3_host_status(qa_application *, qa_actor_owner,
    qa_application_network_q3_status_player players[64], size_t *count, qa_error *);
/* Actor-independent primary GAME construction policy and mounted content.
 * Capacity is frozen at the admitted source constructor/Init boundary. */
bool qa_application_network_q3_host_capacity(qa_application *, qa_actor_owner,
    uint32_t *, qa_error *);
qa_vfs *qa_application_network_q3_content(qa_application *, qa_actor_owner, qa_error *);
bool qa_application_network_q3_content_product(qa_application *, qa_actor_owner,
    qa_product_id *, qa_error *);
typedef struct qa_application_network_q3_package_view {
    qa_actor_owner owner;
    qa_vfs *content;
    const qa_q3_pak_references *references;
    uint64_t source_generation, read_generation;
    size_t read_count;
} qa_application_network_q3_package_view;
/* Complete physical signon records and authoritative clock from the same
 * qualified GAME. Pure hosting requires the package-reference producer;
 * absent that contract it is rejected before publishing systeminfo. */
bool qa_application_network_q3_signon(qa_application *, qa_actor_id,
    int32_t server_id, int32_t checksum_feed, const qa_application_network_q3_package_view *,
    qa_q3_gamestate *, qa_q3_server_world *, qa_error *);
bool qa_application_network_q3_world(qa_application *, qa_actor_id,
    int32_t server_id, int32_t restarted_server_id, int32_t checksum_feed,
    qa_q3_server_world *, qa_error *);
bool qa_application_network_q3_userinfo(qa_application *, qa_actor_id, const char *, qa_error *);
/* Borrow genuine retained source userinfo before retiring the old admission. */
bool qa_application_network_q3_userinfo_read(qa_application *, qa_actor_id, const char **, qa_error *);
/* Observe the exact retained primary GAME provider's wire world.
 * The source round may have retired every player actor; none is reconstructed
 * or used as a fallback. Requires its completed idle source round boundary. */
bool qa_application_network_q3_round_world(qa_application *, qa_actor_owner source_owner,
    int32_t server_id, int32_t restarted_server_id, int32_t checksum_feed,
    const qa_application_network_q3_package_view *, qa_q3_server_world *, qa_error *);
/* Publish actual server/system configstrings through that source's ordinary
 * reliable owners before and after source reset; does not send a gamestate.
 * Failure after a source write is irreversible; the round owner must fault
 * and retire the failed transaction rather than retry it as an idle cut. */
bool qa_application_network_q3_round_prepare(qa_application *, qa_actor_owner source_owner,
    int32_t server_id, int32_t restarted_server_id, int32_t checksum_feed,
    const qa_application_network_q3_package_view *, qa_q3_server_world *, qa_error *);
bool qa_application_network_q3_snapshot(qa_application *, qa_actor_id,
    int32_t message_number, int32_t server_command_number, uint8_t flags,
    qa_application_network_q3_frame *, qa_error *);
/* Qualified external cgame consumes native client snapshots and owns source
 * prediction. Admission preserves the selected gameplay composition. */
bool qa_application_network_q3_client_source(qa_application *, qa_actor_id,
    qa_actor_owner *, qa_q3_product *, uint32_t *launch_seat, qa_error *);
bool qa_application_network_q3_client_command(qa_application *, qa_actor_owner,
    uint32_t seat, const qa_q3_tokens *, qa_error *);
bool qa_application_network_q3_client_clear(qa_application *, qa_actor_owner,
    uint32_t seat, qa_error *);
/* Connection-owned projection, initialized to zero. Source numbers are kept
 * here rather than in the local GAME source-slot namespace. Bodies contain
 * authoritative snapshot state, remain unlinked, and grant no input authority.
 * Keep current and next presentation snapshots together until the source clock
 * transitions; release before closing the borrowed application. */
typedef struct qa_application_network_q3_projection {
    qa_actor_owner owner;
    qa_actor_definition definition;
    qa_actor_id actors[QA_Q3_ENTITY_WORLD];
} qa_application_network_q3_projection;
bool qa_application_network_q3_client_project(qa_application *, qa_actor_owner,
    qa_application_network_q3_projection *, const qa_q3_snapshot *current,
    const qa_q3_snapshot *next, qa_error *);
bool qa_application_network_q3_client_unproject(qa_application *,
    qa_application_network_q3_projection *, qa_error *);
bool qa_application_network_q3_client_actor(qa_application *,
    const qa_application_network_q3_projection *, uint32_t source,
    qa_actor_id *, bool *present, qa_error *);
#endif
