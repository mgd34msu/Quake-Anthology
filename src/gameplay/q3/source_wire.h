#ifndef QA_Q3_SOURCE_WIRE_H
#define QA_Q3_SOURCE_WIRE_H

#include "qa/game_q3_wire.h"
#include "qa/source_save.h"

typedef struct q3_wire_state q3_wire_state;

/* Source s fields without a body or trajectory authority. Player pos/apos are
 * retained separately at BG conversion; other trajectories read their owners. */
typedef struct q3_wire_entity_source {
    /* Only entities without an existing projectile/item/mover/corpse trajectory
     * use position. Non-mover angular trajectories also have this source owner. */
    qa_trajectory position, angular;
    int32_t type, flags, time, time2;
    qa_vec3 authored_origin, origin2, authored_angles, angles2;
    int32_t other_entity, other_entity2, ground_entity, constant_light;
    int32_t loop_sound, model, model2, client, frame, solid;
    int32_t event, event_parameter, powerups, weapon, legs, torso, generic1;
    int32_t single_client;
    bool free_after_event, unlink_after_event;
    /* Explicit arena assignment owns think/nextthink until G_FreeEntity or an
     * actual thinker assignment. G_InitGentity preserves this raw row state. */
    int32_t arena_think, arena_nextthink;
} q3_wire_entity_source;

bool q3_wire_create(qa_q3_game *, qa_error *);
void q3_wire_destroy(qa_q3_game *);
void q3_wire_reset(qa_q3_game *);
void q3_wire_bind(qa_q3_game *, uint32_t source_slot, qa_actor_id);
void q3_wire_release(qa_q3_game *, uint32_t source_slot, qa_actor_id);
q3_wire_entity_source *q3_wire_entity(qa_q3_game *, qa_actor_id);
q3_wire_entity_source *q3_wire_entity_slot(qa_q3_game *, uint32_t);
/* The ordinary constructor commits this after all real source field writes.
 * Baselines, bot observations and snapshots reject an unfinished constructor. */
bool q3_wire_entity_ready(qa_q3_game *, qa_actor_id, qa_error *);
/* CopyToBodyQue copies the authored player s, including its published snapped
 * trajectory. The caller installs this trajectory in the genuine corpse owner. */
bool q3_wire_copy_body(qa_q3_game *, qa_actor_id player, qa_actor_id corpse,
                       qa_trajectory *position, uint32_t *source_flags, qa_error *);
void q3_wire_client_clear(qa_q3_game *, uint32_t source_slot);
/* ClientSpawn clears source PM/stats while retaining the persistant fields. */
void q3_wire_client_spawn_clear(qa_q3_game *, uint32_t source_slot);
/* Selected-source copyFrom preserves every canonical/special ammo authority. */
void q3_wire_selected_client_clear(qa_q3_game *, uint32_t source_slot);
/* Native copyFrom writes these existing source stats/persistant owners while
 * preserving this client's own shared inventory and body authorities. */
void q3_wire_client_follow_copy(qa_q3_game *, uint32_t source_slot, const qa_q3_player *);
bool q3_wire_damage(qa_q3_game *, const qa_damage_outcome *, qa_error *);
/* Call from the actual G_AddEvent/TempEntity producers, before their report
 * sink. Predictable player events use the existing native two-entry ring. */
bool q3_wire_add_event(qa_q3_game *, qa_actor_id, int32_t event, int32_t parameter,
                       qa_error *);
bool q3_wire_temp_entity(qa_q3_game *, qa_vec3 origin, int32_t event,
                         qa_actor_id *, qa_error *);
qa_q3_entity *q3_wire_temporary(qa_q3_game *, qa_actor_id);
typedef enum q3_wire_event_status {
    Q3_WIRE_EVENT_INACTIVE, Q3_WIRE_EVENT_ACTIVE,
    Q3_WIRE_EVENT_WAITING, Q3_WIRE_EVENT_FREED
} q3_wire_event_status;
bool q3_wire_expire_events(qa_q3_game *, qa_actor_id, q3_wire_event_status *, qa_error *);
/* ClientThink writes entity.eventTime when the real predictable ring changes,
 * independently of ps.externalEventTime. */
bool q3_wire_event_time(qa_q3_game *, qa_actor_id, int32_t time_ms, qa_error *);
/* Capture and prepare use the enclosing session's real actor remapping. The
 * prepared owner is committed only after its physical source rows qualify. */
bool q3_wire_capture(const qa_q3_game *, qa_buffer *, qa_error *);
bool q3_wire_prepare(qa_q3_game *, qa_bytes, q3_wire_state **, qa_error *);
bool q3_wire_validate(const qa_q3_game *, const q3_wire_state *, qa_error *);
bool q3_wire_validate_saved(const qa_q3_game *, const q3_wire_state *,
                            const qa_q3_checkpoint *, bool world_links, qa_error *);
void q3_wire_discard(q3_wire_state *);
void q3_wire_commit(qa_q3_game *, q3_wire_state *);

#endif
