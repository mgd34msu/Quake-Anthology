/* Port of src/content/q1/base/map-entities.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_MAP_ENTITIES_H
#define QA_CONTENT_Q1_BASE_MAP_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/foundation_types.h"
#include "qa/content/q1/base/provider.h"

extern const char *const QA_Q1_REMAINING_MAP_CLASSNAMES[];
extern const size_t QA_Q1_REMAINING_MAP_CLASSNAME_COUNT;

void qa_q1_make_static(qa_q1b_game *game, qa_q1b_actor *entity);
/* velocity NULL = TS default {0,0,15}. */
qa_q1b_actor *qa_q1_spawn_bubble(qa_q1b_game *game, qa_vec3 origin, const qa_vec3 *velocity, bool split);
void qa_q1_register_map_callbacks(qa_q1b_game *game);
void qa_q1_spawn_remaining_map_actor(qa_q1_base *base, qa_q1b_actor *entity);

#endif
