#ifndef QA_MONSTER_MISSION_H
#define QA_MONSTER_MISSION_H

#include "qa/session.h"

typedef struct qa_monster_combat_route {
    qa_actor_id goal;
    bool stand_ground;
} qa_monster_combat_route;

/* The authored owner keeps mission state independently of the selected monster
 * implementation. A lookup copies this borrowed binding for one synchronous call. */
typedef struct qa_monster_mission {
    qa_actor_owner owner;
    void *context;
    bool ambush;
    bool (*spawned)(void *, qa_actor_id, qa_error *);
    bool (*started)(void *, qa_actor_id, qa_error *);
    bool (*killed)(void *, qa_actor_id, qa_actor_id attacker, qa_error *);
    bool (*route)(void *, qa_actor_id, qa_actor_id *goal, qa_error *);
    bool (*use)(void *, qa_actor_id, qa_actor_id activator, bool *handled, qa_error *);
    bool (*combat_route)(void *, qa_actor_id, qa_monster_combat_route *, qa_error *);
    bool (*found_target)(void *, qa_actor_id, qa_error *);
} qa_monster_mission;

typedef struct qa_monster_missions {
    void *context;
    /* Nonmutating lookup; false means this actor has no authored mission. */
    bool (*lookup)(void *, qa_actor_id, qa_monster_mission *);
} qa_monster_missions;

#endif
