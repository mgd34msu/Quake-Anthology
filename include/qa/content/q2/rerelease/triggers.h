/* Port of src/content/q2/rerelease/triggers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_TRIGGERS_H
#define QA_CONTENT_Q2_RERELEASE_TRIGGERS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/players.h"
typedef struct qa_q2rr_sound_time { qa_actor_id actor; double time; } qa_q2rr_sound_time;
typedef struct qa_q2_rerelease_triggers {
  qa_q2_rerelease_players *players;
  qa_q2_rerelease_hooks hooks;
  qa_q2rr_sound_time *sound_times; size_t count, cap;
  qa_q2_use toggle_use, killbox_use, hurt_use, world_gravity_use, sound_fx_use;
  qa_q2_think sound_fx_think, push_active, push_inactive;
  qa_q2_touch push_touch, hurt_touch, gravity_touch, monster_jump_touch;
} qa_q2_rerelease_triggers;
void qa_q2_rerelease_triggers_init(qa_q2_rerelease_triggers *t, qa_q2_rerelease_players *p, qa_q2_rerelease_hooks h);
void qa_q2_rerelease_triggers_destroy(qa_q2_rerelease_triggers *t);
bool qa_q2_rerelease_triggers_spawn(qa_q2_rerelease_triggers *t, qa_q2_entity *e, qa_q2_game_services *g);
qa_q2_callback_definitions qa_q2_rerelease_triggers_callbacks(qa_q2_rerelease_triggers *t);
qa_q2_spawn_module qa_q2_rerelease_triggers_as_spawn(qa_q2_rerelease_triggers *t);
double qa_q2rr_sound_time_get(qa_q2_rerelease_triggers *t, qa_actor_id a); /* 0 = absent (TS ?? 0) */
bool qa_q2rr_sound_time_set(qa_q2_rerelease_triggers *t, qa_actor_id a, double v);
void qa_q2rr_sound_time_delete(qa_q2_rerelease_triggers *t, qa_actor_id a);
void qa_q2rr_sound_time_clear(qa_q2_rerelease_triggers *t);
#endif
