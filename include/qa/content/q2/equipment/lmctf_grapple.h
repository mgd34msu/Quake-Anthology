/* Port of src/content/q2/equipment/lmctf-grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_EQUIPMENT_LMCTF_GRAPPLE_H
#define QA_CONTENT_Q2_EQUIPMENT_LMCTF_GRAPPLE_H
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/foundation/callbacks.h"
typedef struct qa_lmctf_grapple_policy {
  void *ctx;
  bool (*can_attach)(void *ctx, qa_actor_id owner, qa_actor_id target, qa_q2_game_services *g);
  bool (*can_damage)(void *ctx, qa_actor_id target);
  bool (*player_hit)(void *ctx, qa_actor_id target, qa_q2_game_services *g);
} qa_lmctf_grapple_policy;
typedef struct qa_lmctf_grapple_equipment qa_lmctf_grapple_equipment;
/* policy fns NULL = TS defaults; released NULL = no-op. */
qa_lmctf_grapple_equipment *qa_lmctf_grapple_create(qa_grapple_hooks hooks, qa_lmctf_grapple_policy policy,
  void (*released)(void *ctx, qa_actor_id a), void *released_ctx);
void qa_lmctf_grapple_destroy(qa_lmctf_grapple_equipment *e);
qa_grapple_hooks qa_lmctf_grapple_hooks(qa_lmctf_grapple_equipment *e);
void qa_lmctf_grapple_bind(qa_lmctf_grapple_equipment *e, qa_q2_game_services *g);
qa_lmctf_grapple_state *qa_lmctf_grapple_state_of(qa_lmctf_grapple_equipment *e, qa_actor_id a);
qa_q2_callback_definitions qa_lmctf_grapple_callbacks(qa_lmctf_grapple_equipment *e);
void qa_lmctf_grapple_abort(qa_lmctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g);
void qa_lmctf_grapple_fire(qa_lmctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g);
double qa_lmctf_grapple_gravity_scale(qa_lmctf_grapple_equipment *e, qa_actor_id a);
/* TS readonly states Map: get (NULL is undefined), ordered iteration and clear.
 * Map.set is the creating state() accessor followed by assignment. */
qa_lmctf_grapple_state *qa_lmctf_grapple_find_state(qa_lmctf_grapple_equipment *e, qa_actor_id a);
size_t qa_lmctf_grapple_state_count(const qa_lmctf_grapple_equipment *e);
qa_lmctf_grapple_state *qa_lmctf_grapple_state_at(qa_lmctf_grapple_equipment *e, size_t index, qa_actor_id *out_actor);
void qa_lmctf_grapple_clear_states(qa_lmctf_grapple_equipment *e);
#endif
