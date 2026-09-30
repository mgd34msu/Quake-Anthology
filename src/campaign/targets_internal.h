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
struct qa_targets {
    qa_target_options options;
    qa_target_binding *bindings;
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
