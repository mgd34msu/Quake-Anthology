#ifndef QA_Q3_NATIVE_ENTITY_H
#define QA_Q3_NATIVE_ENTITY_H

#include "body.h"
#include "player_fx.h"

/* A real per-seat centity continuation, qualified by physical GAME row and
 * full canonical actor generation. Authoritative S/PS stay in the observer;
 * these fields belong exclusively to native cgame presentation. */
typedef struct q3n_entity {
    qa_actor_id actor;
    uint32_t physical;
    uint64_t client_media_revision;
    bool valid, event_only_fired, teleport_bit, loop_stopped;
    int32_t previous_event, snapshot_time, trail_time, dust_trail_time;
    int32_t misc_time, muzzle_flash_time;
    qa_vec3 lerp_origin, lerp_angles;
    q3n_player_pose player;
    q3n_player_fx_state player_fx;
    float barrel_angle;
    int32_t barrel_time;
    bool barrel_spinning, lightning_firing, railgun_flash;
    qa_vec3 rail_impact;
} q3n_entity;

#endif
