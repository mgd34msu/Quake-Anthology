#ifndef QA_APPLICATION_SELECTED_EFFECTS_H
#define QA_APPLICATION_SELECTED_EFFECTS_H

#include "qa/application.h"
#include "qa/game_q3_source.h"

typedef enum qa_application_effects_source_kind {
    QA_APPLICATION_EFFECTS_Q1,
    QA_APPLICATION_EFFECTS_Q2,
    QA_APPLICATION_EFFECTS_Q3,
    QA_APPLICATION_EFFECTS_EXTERNAL
} qa_application_effects_source_kind;
typedef enum qa_application_effects_observation {
    QA_APPLICATION_EFFECTS_ROUTED_ACTOR,
    QA_APPLICATION_EFFECTS_PRODUCER
} qa_application_effects_observation;

typedef struct qa_application_selected_effects {
    qa_actor_id actor;
    qa_actor_owner provider, primary;
    qa_session *session;
    const qa_launch_snapshot *publication;
    const qa_launch_instance *launch;
    qa_vfs *content;
    qa_product_id product;
    qa_game_family family;
    qa_application_effects_source_kind kind;
    qa_application_effects_observation observation;
    union {
        const qa_q1_game *q1;
        const qa_q2_game *q2;
        const qa_q3_game *q3;
    } native;
    qa_source_frame source_frame;
    /* Canonical character pools sample the captured ENTITIES world frame;
     * their selected producer clock and event birth times remain independent. */
    qa_application_effects_source_kind primary_kind;
    union {
        const qa_q1_game *q1;
        const qa_q2_game *q2;
        const qa_q3_game *q3;
    } primary_native;
    qa_source_frame primary_frame;
    uint64_t primary_time_ns;
    int32_t sample_time_ms;
    uint64_t publication_generation, map_revision, application_frame;
    uint64_t source_time_ns;
    int32_t q3_time_ms;
    qa_q3_product q3_product;
    int32_t q3_match_start_ms;
} qa_application_selected_effects;

/* This completed-frame borrow proves the actual EFFECTS routing and source
 * clock. External selection proves its metadata only; it grants no event
 * callback, media registration, translation or primary suppression. */
bool qa_application_selected_effects_read(qa_application *, qa_actor_id,
    qa_application_selected_effects *, bool *found, qa_error *);
bool qa_application_selected_effects_current(qa_application *,
    const qa_application_selected_effects *);
/* An already admitted effect group samples its actual constructed producer
 * even after the emitter retires. This grants no actor routing or event. */
bool qa_application_effects_producer_read(qa_application *, qa_actor_owner,
    qa_application_selected_effects *, qa_error *);
/* Pure retained source observation during the genuine aggregate content
 * lease. This grants no live event dispatch, routing or media loading. */
bool qa_application_effects_retained_read(qa_application *, qa_actor_owner,
    qa_application_selected_effects *, qa_error *);

/* A Q3 physical row is optional for a selected source attached to a foreign
 * actor. The binding is returned only from that source's actual slot table. */
bool qa_application_selected_effects_q3_binding(qa_application *,
    const qa_application_selected_effects *, qa_q3_source_binding *,
    bool *found, qa_error *);

typedef struct qa_application_effect_event {
    qa_application_selected_effects source;
    const qa_builtin_event *event;
    size_t ordinal;
    uint64_t queue_generation;
} qa_application_effect_event;

/* A canonical event can outlive its emitter actor. This borrow qualifies the
 * actual retained queue row and its producer, without routing a retired actor
 * or granting a translation to another selected source. */
bool qa_application_effect_event_read(qa_application *, size_t,
    qa_application_effect_event *, qa_error *);
bool qa_application_effect_event_current(qa_application *,
    const qa_application_effect_event *);

#endif
