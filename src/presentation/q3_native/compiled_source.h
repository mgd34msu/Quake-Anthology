#ifndef QA_Q3_NATIVE_COMPILED_SOURCE_H
#define QA_Q3_NATIVE_COMPILED_SOURCE_H

#include "qa/application.h"
#include "qa/q3_presentation.h"
#include "qa/source_save.h"

typedef struct q3n_compiled_source q3n_compiled_source;
typedef struct q3n_compiled_source_rebind_ticket q3n_compiled_source_rebind_ticket;
/* The receiver owns decoded compiled GAME rows and its reached CLIENT
 * dictionary. This receipt borrows that owner; it owns no GAME or transport. */
typedef struct q3n_compiled_source_basis {
    qa_application *application;
    qa_session *session;
    qa_actor_registry *registry;
    qa_actor_owner provider;
    uint64_t receiver;
    const char *instance;
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    qa_q3_product product;
    uint64_t publication, map_revision, serial;
    qa_actor_id viewer;
    uint32_t seat, physical_seat;
    int32_t client_number, time, game_type, max_clients, level_start_time;
    int32_t initial_command, reached_command;
    uint32_t snapshot_bit;
    bool initialized;
} q3n_compiled_source_basis;
typedef struct q3n_compiled_source_view {
    const q3n_compiled_source *owner;
    q3n_compiled_source_basis basis;
} q3n_compiled_source_view;
typedef struct q3n_compiled_source_options {
    void *context;
    bool (*read)(void *, q3n_compiled_source_basis *, qa_error *);
    bool (*current)(void *, const q3n_compiled_source_basis *);
    bool (*configstring)(void *, uint32_t, const char **, uint64_t *, qa_error *);
    bool (*idle)(void *);
    bool (*client_actor)(void *, uint32_t, qa_actor_id *, bool *, qa_error *);
    /* Pure actual wire-history provenance. Retired cached actors remain
     * observations; this grants no live source or gameplay authority. */
    bool (*actor_known)(void *, qa_actor_id);
    bool (*checkpoint_read)(void *, q3n_compiled_source_basis *, qa_error *);
    bool (*checkpoint_current)(void *, const q3n_compiled_source_basis *);
} q3n_compiled_source_options;
bool q3n_compiled_source_create(const q3n_compiled_source_options *, q3n_compiled_source **, qa_error *);
bool q3n_compiled_source_destroy(q3n_compiled_source **, qa_error *);
bool q3n_compiled_source_read(const q3n_compiled_source *, q3n_compiled_source_view *, qa_error *);
bool q3n_compiled_source_current(const q3n_compiled_source_view *);
/* Retained terminal observations permit exact retired wire actors only for
 * pure construction and codecs. They grant no frame or resource operation. */
bool q3n_compiled_source_checkpoint_read(const q3n_compiled_source *, q3n_compiled_source_view *, qa_error *);
bool q3n_compiled_source_checkpoint_current(const q3n_compiled_source_view *);
/* The actual receiver calls this after committing a real round bit toggle.
 * Only the snapshot bit and full viewer may change in the constructor tuple. */
bool q3n_compiled_source_rebind(q3n_compiled_source *, const q3n_compiled_source_view *, qa_error *);
bool q3n_compiled_source_rebind_prepare(q3n_compiled_source *, const q3n_compiled_source_view *,
    const q3n_compiled_source_basis *, q3n_compiled_source_rebind_ticket **, qa_error *);
bool q3n_compiled_source_rebind_ready(const q3n_compiled_source_rebind_ticket *);
bool q3n_compiled_source_rebind_checkpoint_current(const q3n_compiled_source_rebind_ticket *);
bool q3n_compiled_source_rebind_context_is(const q3n_compiled_source_rebind_ticket *,
    const q3n_compiled_source *, const qa_command_context *, const qa_command_context *);
bool q3n_compiled_source_rebind_checkpoint_context_is(const q3n_compiled_source_rebind_ticket *,
    const q3n_compiled_source *, const qa_command_context *, const qa_command_context *);
void q3n_compiled_source_rebind_commit(q3n_compiled_source_rebind_ticket **);
void q3n_compiled_source_rebind_abort(q3n_compiled_source_rebind_ticket **);
bool q3n_compiled_source_configstring(const q3n_compiled_source *, uint32_t,
    const char **, uint64_t *, qa_error *);
bool q3n_compiled_source_client_actor(const q3n_compiled_source *, uint32_t, qa_actor_id *, bool *, qa_error *);
bool q3n_compiled_source_actor_known(const q3n_compiled_source_view *, qa_actor_id);
/* Runtime codecs serialize the real receiver, not this stateless borrowed
 * adapter. Its constructor identity is requalified after pure owner import. */
bool q3n_compiled_source_idle(const q3n_compiled_source *);

#endif
