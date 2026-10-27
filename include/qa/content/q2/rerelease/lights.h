/* Port of src/content/q2/rerelease/lights.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_LIGHTS_H
#define QA_CONTENT_Q2_RERELEASE_LIGHTS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
double qa_q2_rerelease_color(const char *value);
typedef struct qa_q2rr_light_entry { qa_actor_id actor; bool active; } qa_q2rr_light_entry;
typedef struct qa_q2_rerelease_lights {
  qa_q2_rerelease_hooks hooks;
  qa_q2rr_light_entry *active; size_t count, cap;
  qa_q2_use use; qa_q2_think think, flicker;
} qa_q2_rerelease_lights;
void qa_q2_rerelease_lights_init(qa_q2_rerelease_lights *l, qa_q2_rerelease_hooks hooks);
void qa_q2_rerelease_lights_destroy(qa_q2_rerelease_lights *l);
bool qa_q2_rerelease_lights_spawn(qa_q2_rerelease_lights *l, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_rerelease_lights_get_active(qa_q2_rerelease_lights *l, qa_actor_id a, bool *out);
bool qa_q2_rerelease_lights_set_active(qa_q2_rerelease_lights *l, qa_actor_id a, bool v);
void qa_q2_rerelease_lights_delete(qa_q2_rerelease_lights *l, qa_actor_id a);
void qa_q2_rerelease_lights_clear(qa_q2_rerelease_lights *l);
qa_q2_callback_definitions qa_q2_rerelease_lights_callbacks(qa_q2_rerelease_lights *l);
qa_q2_spawn_module qa_q2_rerelease_lights_as_spawn(qa_q2_rerelease_lights *l);
#endif
