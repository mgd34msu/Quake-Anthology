#ifndef QA_BOT_NAVIGATION_INTERNAL_H
#define QA_BOT_NAVIGATION_INTERNAL_H

#include "qa/bot_navigation.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct qa_bot_navigation {
    qa_navigation *runtime;
    qa_world *world;
    qa_actor_id actor;
    qa_bot_navigation_observations observations;
    qa_nav_workspace *workspace;
    qa_nav_route trajectory;
    qa_nav_prediction_result prediction;
    qa_aas_crossing *crossings;
    size_t crossing_capacity;
    uint32_t offset;
    uint32_t *areas, *stack, *next_neighbor;
    qa_nav_adjacency_cursor *adjacency;
    size_t capacity;
    uint32_t *start_times, *goal_times;
    uint8_t *visited;
};
bool bot_nav_scratch(qa_bot_navigation *, size_t, qa_error *);
bool bot_nav_fail(qa_error *, const char *);

#endif
