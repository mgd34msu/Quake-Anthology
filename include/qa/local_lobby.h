#ifndef QA_LOCAL_LOBBY_H
#define QA_LOCAL_LOBBY_H

#include "qa/network_unified.h"

typedef struct qa_lobbies qa_lobbies;
typedef struct qa_lobby qa_lobby;
typedef struct qa_lobby_session qa_lobby_session;
typedef struct qa_lobby_id {
    uint64_t owner, serial;
} qa_lobby_id;
typedef struct qa_local_account {
    const char *id, *name;
} qa_local_account;
typedef struct qa_lobby_member {
    qa_local_account account;
    uint32_t seats;
    bool ready;
} qa_lobby_member;
typedef enum qa_lobby_phase { QA_LOBBY_OPEN, QA_LOBBY_STARTING, QA_LOBBY_PLAYING } qa_lobby_phase;
typedef struct qa_lobby_wire {
    qa_net_protocol_id protocol;
    qa_sha256_digest composition;
    const char *snapshot_schema; /* Required only for the unified wire. */
} qa_lobby_wire;
typedef struct qa_lobby_selection {
    /* Already admitted full composition, including the snapshot schema. */
    const qa_unified_composition *composition;
    const char *snapshot_schema;
} qa_lobby_selection;
typedef struct qa_lobby_view {
    qa_lobby_id id;
    const char *owner, *name;
    uint32_t capacity;
    qa_lobby_selection selection;
    const qa_lobby_member *members;
    size_t member_count;
    uint64_t match_generation;
    qa_lobby_phase phase;
    /* Valid only while playing; cleared when returning to the open room. */
    qa_net_address endpoint;
    qa_lobby_wire wire;
} qa_lobby_view;

/* One application thread owns the local service. The caller supplies a unique
 * nonzero service identity, distinct from actor/world/connection identities.
 * Rooms and membership outlive launched worlds. Account authentication belongs
 * to the selected account provider, not this room registry. */
bool qa_lobbies_create(uint64_t owner, qa_lobbies **, qa_error *);
void qa_lobbies_destroy(qa_lobbies *);
size_t qa_lobbies_count(const qa_lobbies *);
const qa_lobby *qa_lobbies_at(const qa_lobbies *, size_t);
const qa_lobby *qa_lobbies_find(const qa_lobbies *, qa_lobby_id);
/* Borrowed snapshots survive until a registry mutation. Retain explicitly to
 * preserve an immutable snapshot across transitions or service destruction.
 * Room definitions, composition bytes and strings are shared between versions. */
const qa_lobby_view *qa_lobby_read(const qa_lobby *);
void qa_lobby_retain(const qa_lobby *);
void qa_lobby_release(const qa_lobby *);
bool qa_lobbies_host(qa_lobbies *, qa_local_account, const char *name, uint32_t capacity,
                     const qa_lobby_selection *, uint32_t seats, const qa_lobby **, qa_error *);
bool qa_lobbies_join(qa_lobbies *, qa_lobby_id, qa_local_account, uint32_t seats, const qa_lobby **,
                     qa_error *);
bool qa_lobbies_ready(qa_lobbies *, qa_lobby_id, const char *account, bool, qa_error *);
bool qa_lobbies_start(qa_lobbies *, qa_lobby_id, const char *owner, const qa_lobby **, qa_error *);
bool qa_lobbies_publish(qa_lobbies *, qa_lobby_id, const char *owner, uint64_t generation,
                        const qa_net_address *, const qa_lobby_wire *, const qa_lobby **,
                        qa_error *);
bool qa_lobbies_complete(qa_lobbies *, qa_lobby_id, const char *owner, uint64_t generation,
                         const qa_lobby **, qa_error *);
bool qa_lobbies_leave(qa_lobbies *, qa_lobby_id, const char *account, qa_error *);

typedef struct qa_lobby_transitions {
    void *context;
    bool (*host)(void *, const qa_lobby_view *, qa_net_address *, qa_lobby_wire *, qa_error *);
    bool (*join)(void *, const qa_lobby_view *, qa_error *);
    bool (*leave)(void *, const qa_lobby_view *, qa_error *);
    bool (*completed)(void *, const qa_lobby_view *, qa_error *);
} qa_lobby_transitions;
/* Call from the application transition queue. Callbacks complete synchronously,
 * may mutate the shared registry, and must not reenter or destroy this session.
 * The service outlives sessions; retained room views outlive callback changes.
 * A failed host callback owns its partial-launch cleanup. After host success,
 * failed publication attempts both room reset and host teardown. */
bool qa_lobby_session_create(qa_lobbies *, qa_local_account, const qa_lobby_transitions *,
                             qa_lobby_session **, qa_error *);
const qa_lobby *qa_lobby_session_current(const qa_lobby_session *);
bool qa_lobby_session_host(qa_lobby_session *, const char *, uint32_t, const qa_lobby_selection *,
                           uint32_t seats, qa_error *);
bool qa_lobby_session_join(qa_lobby_session *, qa_lobby_id, uint32_t seats, qa_error *);
bool qa_lobby_session_ready(qa_lobby_session *, bool, qa_error *);
bool qa_lobby_session_start(qa_lobby_session *, qa_error *);
bool qa_lobby_session_poll(qa_lobby_session *, qa_error *);
bool qa_lobby_session_complete(qa_lobby_session *, qa_error *);
bool qa_lobby_session_leave(qa_lobby_session *, qa_error *);
/* Attempts leave, then frees the session even on callback failure. */
bool qa_lobby_session_close(qa_lobby_session *, qa_error *);

#endif
