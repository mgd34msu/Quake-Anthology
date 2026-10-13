#ifndef QA_WORLD_COLLISION_QUERY_H
#define QA_WORLD_COLLISION_QUERY_H

#include "qa/world.h"

typedef struct qa_world_query_row {
    qa_actor_id actor;
    qa_actor_collision collision;
    const qa_entity_body_fields *fields;
} qa_world_query_row;
/* Reads run in membership order after the map query. Rows can participate
 * without an actor. Hit resolution runs only before a replacing result. */
typedef bool (*qa_world_query_row_read_fn)(void *, size_t, qa_entity_pose,
    qa_actor_id pass, qa_world_query_row *, bool *present, qa_error *);
typedef bool (*qa_world_query_row_hit_fn)(void *, size_t, qa_trace_result *, qa_error *);
typedef struct qa_world_borrowed_query {
    qa_collision_geometry *geometry;
    qa_trace_scratch *scratch;
    qa_world_query_rules rules;
    size_t count;
    void *context;
    qa_world_query_row_read_fn read;
    qa_world_query_row_hit_fn hit;
    /* Genuine CG model/temporary-box entry points retain their original
     * validation and transformed-query rules, including foreign maps. */
    bool q3_source;
} qa_world_borrowed_query;

/* Fields, membership and scratch remain borrowed through this synchronous
 * query. Nested calls need independent backing; no actor bodies are changed. */
bool qa_world_trace_borrowed(const qa_world_borrowed_query *, const qa_trace_query *,
    qa_trace_result *, qa_error *);
bool qa_world_point_contents_borrowed(const qa_world_borrowed_query *, const qa_point_query *,
    qa_point_contents *, qa_error *);

#endif
