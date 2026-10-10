#ifndef QA_FRONTEND_REMOTE_UNIFIED_H
#define QA_FRONTEND_REMOTE_UNIFIED_H

#include "qa/frontend.h"
#include "qa/executable_recipe.h"
#include "qa/network_unified_session.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_components.h"
#include "qa/persistence_content.h"
#include "qa/audio.h"
#include "qa/scene.h"
#include "qa/input.h"
#include "qa/source_frame_time.h"

typedef struct frontend_remote_unified frontend_remote_unified;
typedef struct frontend_unified_recipient_clock {
    double milliseconds,source_elapsed_ms;
    uint64_t begin_generation,physical_frame,wall_time_ns,wall_elapsed_ns;
} frontend_unified_recipient_clock;
typedef enum frontend_unified_frame_preparation {
    FRONTEND_UNIFIED_FRAME_WAIT,
    FRONTEND_UNIFIED_FRAME_READY,
    FRONTEND_UNIFIED_FRAME_OBSOLETE
} frontend_unified_frame_preparation;
typedef struct frontend_remote_unified_domain {
    qa_application *application;
    qa_network_runtime *runtime;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint32_t physical_seat;
    qa_catalog *catalog;
    qa_resource_pool *resources;
    qa_console *console;
    qa_cvars *cvars;
    const qa_source_frame_time_binding *frame_time;
    qa_command_context command_context;
} frontend_remote_unified_domain;

/* These are the actual consumer owners. Preparation may retain an incomplete
 * child; its checked close must succeed before the recipe can be released. */
typedef struct frontend_remote_unified_consumers {
    void *context;
    bool (*prepare)(void *, frontend_remote_unified *, qa_executable_recipe *, bool *, qa_error *);
    bool (*offer_publish)(void *, frontend_remote_unified *, qa_executable_recipe *, qa_error *);
    bool (*offer_ready)(void *, frontend_remote_unified *, qa_executable_recipe *, qa_error *);
    bool (*control)(void *, frontend_remote_unified *, const qa_unified_document *, qa_error *);
    bool (*events_decode)(void *, qa_bytes, qa_unified_held **, bool *ready, qa_error *);
    bool (*frame)(void *, frontend_remote_unified *, const qa_unified_document *,
        frontend_unified_frame_preparation *, qa_error *);
    bool (*publish)(void *, frontend_remote_unified *, const qa_unified_document *, qa_error *);
    bool (*input)(void *, frontend_remote_unified *, const qa_usercmd *, double command_time_ms, qa_error *);
    bool (*physical_ready)(void *, frontend_remote_unified *, uint64_t *completed_sequence,
        bool *sample_needed, qa_error *);
    bool (*physical_input)(void *, frontend_remote_unified *, const qa_seat_input_sample *,
        uint64_t sequence, double source_elapsed_ms, qa_error *);
    bool (*begin_frame)(void *,frontend_remote_unified *,uint64_t wall_now_ns,
        uint64_t wall_elapsed_ns,qa_error *);
    bool (*clock_read)(void *,const frontend_remote_unified *,frontend_unified_recipient_clock *,qa_error *);
    bool (*sample)(void *, frontend_remote_unified *, uint64_t, qa_error *);
    bool (*draw)(void *, frontend_remote_unified *, float, qa_audio_listener *, qa_error *);
    bool (*idle)(void *, const frontend_remote_unified *);
    /* Returned callback custody with exact retained receive-stage tokens.
     * Ordinary input/draw admission continues to use idle. */
    bool (*checkpoint_returned)(void *, const frontend_remote_unified *);
    bool (*close)(void *, frontend_remote_unified *, qa_error *);
    bool (*content_visit)(void *, const frontend_remote_unified *,
        const qa_application_content_visitor *, qa_error *);
    void (*dispose)(void *);
} frontend_remote_unified_consumers;
typedef struct frontend_remote_unified_options {
    frontend_remote_unified_domain domain;
    void *context;
    bool (*current)(void *, const frontend_remote_unified_domain *, qa_error *);
    bool (*userinfo)(void *, const frontend_remote_unified_domain *, const char **, qa_error *);
    bool (*disconnected)(void *, const frontend_remote_unified_domain *, const char *, qa_error *);
    bool (*retirement)(void *,const frontend_remote_unified_domain *,qa_error *);
    bool (*command_text)(void *, const frontend_remote_unified_domain *, const char *, qa_error *);
    bool (*source_command)(void *,const frontend_remote_unified_domain *,const char *instance,
        uint64_t publication,uint64_t map_revision,
        const qa_command_context *,const qa_command_tokens *,qa_error *);
    bool (*transport_restart)(void *,const frontend_remote_unified_domain *,uint64_t runtime_epoch,qa_error *);
    frontend_remote_unified_consumers consumers;
    uint32_t identity_capacity;
} frontend_remote_unified_options;

bool frontend_remote_unified_create(qa_frontend *, const frontend_remote_unified_options *,
    frontend_remote_unified **, qa_error *);
size_t frontend_remote_unified_count(const qa_frontend *);
frontend_remote_unified *frontend_remote_unified_at(const qa_frontend *, size_t);
bool frontend_remote_unified_bind(frontend_remote_unified *, qa_net_client_id,
    qa_unified_session *, qa_error *);
qa_unified_session_hooks frontend_remote_unified_hooks(frontend_remote_unified *);
bool frontend_remote_unified_current(const frontend_remote_unified *, qa_error *);
bool frontend_remote_unified_retired(const frontend_remote_unified *);
qa_executable_recipe *frontend_remote_unified_recipe(const frontend_remote_unified *);
qa_actor_registry *frontend_remote_unified_registry(const frontend_remote_unified *);
const qa_collision_geometry *frontend_remote_unified_geometry(const frontend_remote_unified *);
const qa_unified_document *frontend_remote_unified_frame(const frontend_remote_unified *);
const qa_unified_document *frontend_remote_unified_frame_prepared(const frontend_remote_unified *);
const frontend_remote_unified_domain *frontend_remote_unified_domain_read(const frontend_remote_unified *);
uint32_t frontend_remote_unified_epoch(const frontend_remote_unified *);
/* Resolves the real received player's configuration against the admitted
 * recipe. During frame preparation it observes that exact incoming frame. */
const qa_recipe_provider *frontend_remote_unified_provider(const frontend_remote_unified *,
    qa_launch_role, const char *selector);
/* The accepted frame remains authoritative for saved input while a newer
 * frame is retained for preparation. */
const qa_recipe_provider *frontend_remote_unified_provider_published(const frontend_remote_unified *,
    qa_launch_role,const char *selector);
/* Creates an identity in this replica's private registry, with no Source
 * execution or physical body. Wire identities remain a separate namespace. */
bool frontend_remote_unified_actor(frontend_remote_unified *, uint32_t wire_slot,
    uint64_t wire_generation, qa_actor_id *, qa_error *);
/* Import reads an existing actual private identity, including retired history.
 * It never creates an actor or grants live Source authority. */
bool frontend_remote_unified_actor_retained(const frontend_remote_unified *,uint32_t,
    uint64_t,qa_actor_id *,qa_error *);
/* Qualify the complete Source identity before resolving its private alias.
 * A zero identity denotes an explicitly absent optional Source actor. */
bool frontend_remote_unified_source_actor(frontend_remote_unified *,
    const qa_unified_frame *, qa_actor_id, bool retained, qa_actor_id *, qa_error *);
bool frontend_remote_unified_actor_present(const frontend_remote_unified *, uint32_t wire_slot,
    uint64_t wire_generation);
bool frontend_remote_unified_actor_published(const frontend_remote_unified *,uint32_t,
    uint64_t);
bool frontend_remote_unified_wire_actor(const frontend_remote_unified *, qa_actor_id,
    qa_saved_actor_id *);
bool frontend_remote_unified_player(const frontend_remote_unified *, qa_actor_id *, uint32_t *source_entity);
/* The input producer supplies Source-timed binary64 values. Prediction runs
 * before this exact receipt is submitted to the genuine channel. */
bool frontend_remote_unified_submit(frontend_remote_unified *, const qa_usercmd *,
    double command_time_ms, qa_error *);
bool frontend_remote_unified_command(frontend_remote_unified *, const char *,
    const char *const *, size_t, qa_error *);
bool frontend_remote_unified_command_text(frontend_remote_unified *,const char *,qa_error *);
bool frontend_remote_unified_source_disconnect(frontend_remote_unified *,const char *,qa_error *);
bool frontend_remote_unified_component_command(frontend_remote_unified *,
    const qa_source_owner *owner, const char *const *, size_t, qa_error *);
bool frontend_remote_unified_sample(qa_frontend *, uint64_t now_ns, qa_error *);
bool frontend_remote_unified_begin_frame(qa_frontend *,uint64_t wall_now_ns,uint64_t wall_elapsed_ns,qa_error *);
bool frontend_remote_unified_clock_read(const frontend_remote_unified *,frontend_unified_recipient_clock *,qa_error *);
bool frontend_remote_unified_draw(qa_frontend *, uint32_t physical_seat, float stereo,
    qa_audio_listener *, bool *rendered, qa_error *);
bool frontend_remote_unified_idle(const qa_frontend *);
bool frontend_remote_unified_checkpoint_returned(const qa_frontend *);
/* Used only after the actual lower candidate/exchange retirement removed
 * callback custody. It does not disconnect a transferred Source player. */
bool frontend_remote_unified_transport_retired(frontend_remote_unified *,
    const qa_unified_session *, qa_error *);
bool frontend_remote_unified_destroy(frontend_remote_unified **, qa_error *);
bool frontend_remote_unified_destroy_all(qa_frontend *, qa_error *);
bool frontend_remote_unified_content_visit(const qa_frontend *,
    const qa_application_content_visitor *, qa_error *);

#endif
