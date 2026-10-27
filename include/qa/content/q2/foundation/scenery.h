/* Port of src/content/q2/foundation/scenery.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_SCENERY_H
#define QA_CONTENT_Q2_FOUNDATION_SCENERY_H
#include "qa/content/q2/foundation/host.h"
qa_q2_entity *qa_q2_throw_debris(qa_q2_entity *self, qa_q2_game_services *g, const char *model, double speed, qa_vec3 origin);
bool qa_q2_kill_box(qa_q2_entity *e, qa_q2_game_services *g);
qa_q2_spawn_module qa_q2_create_scenery_module(void);
#endif
