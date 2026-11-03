/* Port of src/content/q1/base/map-entities.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_MAP_ENTITIES_H
#define QA_CONTENT_Q1_BASE_MAP_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/base/provider.h"

extern const char *const QA_Q1_REMAINING_MAP_CLASSNAMES[];
extern const size_t QA_Q1_REMAINING_MAP_CLASSNAME_COUNT;

void qa_q1_make_static(qa_q1_services *game, qa_q1_actor *entity);
/* velocity NULL = TS default {0,0,15}. */
qa_q1_actor *qa_q1_spawn_bubble(qa_q1_services *game, qa_vec3 origin, const qa_vec3 *velocity, bool split);
void qa_q1_register_map_callbacks(qa_q1_services *game);
void qa_q1_spawn_remaining_map_actor(qa_q1_base *base, qa_q1_actor *entity);

#endif
