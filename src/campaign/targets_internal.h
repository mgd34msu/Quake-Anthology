#ifndef QA_TARGETS_INTERNAL_H
#define QA_TARGETS_INTERNAL_H
#include "qa/targets.h"
#include "qa/arena.h"

typedef struct target_index {
    qa_actor_id actor;
    qa_string_id name;
    uint32_t order;
} target_index;
typedef struct authored_index {
    uint32_t order, slot;
} authored_index;
typedef struct target_monster {
    qa_target_binding native;
    qa_monster_mission mission;
    qa_authored_monster authored;
} target_monster;
struct qa_targets {
    qa_target_options options;
    qa_target_binding *bindings;
    target_monster **monsters;
    void *monster_context;
    bool (*monster_resolve)(void *, qa_actor_owner, qa_monster_mission *, qa_error *);
    uint64_t *binding_serial;
    target_index *index;
    authored_index *authored;
    qa_arena scratch;
    size_t count, authored_count, capacity, depth;
    uint64_t actor_revision;
    uint64_t next_binding_serial;
    bool dirty;
};
#endif
