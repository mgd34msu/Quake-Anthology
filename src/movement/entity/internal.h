#ifndef QA_ENTITY_PHYSICS_INTERNAL_H
#define QA_ENTITY_PHYSICS_INTERNAL_H

#include "qa/physics.h"
#include <math.h>
#include <string.h>

#define PH_MONSTER_MASK UINT32_C(0x02020003)

bool ph_live(const qa_physics *, qa_actor_id);
int ph_read(qa_physics *, qa_actor_id, qa_body_state *, qa_physics_properties *, qa_error *);
bool ph_write(qa_physics *, qa_actor_id, const qa_body_state *, qa_error *);
bool ph_properties(qa_physics *, qa_actor_id, const qa_physics_properties *, qa_error *);
bool ph_link(qa_physics *, qa_actor_id, bool triggers, qa_error *);
bool ph_trace(qa_physics *, qa_actor_id, const qa_physics_properties *,
               qa_vec3 start, qa_vec3 end, const qa_bounds *, uint32_t mask,
               qa_q1_move_kind, const qa_actor_id *, size_t,
               qa_trace_result *, qa_error *);
bool ph_body_trace(qa_physics *, qa_actor_id, const qa_body_state *,
                    const qa_physics_properties *, qa_vec3, qa_vec3, bool exact,
                    const qa_actor_id *, size_t, qa_trace_result *, qa_error *);
bool ph_contents(qa_physics *, qa_actor_id, const qa_physics_properties *,
                  qa_vec3, qa_point_contents *, qa_error *);
bool ph_event(qa_physics *, qa_actor_id, qa_physics_event_kind, qa_vec3, qa_error *);
qa_actor_id ph_hit(const qa_physics *, const qa_trace_result *);
qa_vec3 ph_normal(const qa_trace_result *);
void ph_axes(qa_vec3, qa_vec3 *, qa_vec3 *, qa_vec3 *);
bool ph_ground(qa_physics *, qa_actor_id, qa_actor_id ground, qa_error *);
bool ph_test_position(qa_physics *, qa_actor_id, bool *blocked, qa_error *);
static inline bool ph_moving(qa_vec3 v) { return v.x != 0 || v.y != 0 || v.z != 0; }
static inline bool ph_equal_vec(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static inline qa_actor_id ph_none(void) { return (qa_actor_id){0}; }
static inline bool ph_wet(qa_collision_family family, int32_t contents) {
    return family == QA_COLLISION_Q1 ?
        ((contents <= -3 && contents >= -5) || (contents <= -9 && contents >= -14)) :
        (contents & 56) != 0;
}

#endif
