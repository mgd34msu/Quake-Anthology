/* Port of src/content/q2/multiplayer/lmctf/grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_GRAPPLE_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_GRAPPLE_H
#include "qa/content/q2/equipment/lmctf_grapple.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
typedef struct qa_lmctf_grapple qa_lmctf_grapple;
qa_lmctf_grapple *qa_lmctf_grapple_module_create(qa_lmctf_context *context, qa_shared_grapple_control *shared);
void qa_lmctf_grapple_module_destroy(qa_lmctf_grapple *grapple);
qa_lmctf_grapple_equipment *qa_lmctf_grapple_module_equipment(qa_lmctf_grapple *grapple);
bool qa_lmctf_grapple_module_native_enabled(qa_lmctf_grapple *grapple);
qa_shared_grapple_control *qa_lmctf_grapple_module_shared(qa_lmctf_grapple *grapple);
qa_lmctf_grapple_state qa_lmctf_grapple_module_state(qa_lmctf_grapple *grapple, qa_actor_id actor);
qa_lmctf_grapple_state *qa_lmctf_grapple_module_state_ref(qa_lmctf_grapple *grapple, qa_actor_id actor);
bool qa_lmctf_grapple_module_state_has(qa_lmctf_grapple *grapple, qa_actor_id actor);
void qa_lmctf_grapple_module_state_set(qa_lmctf_grapple *grapple, qa_actor_id actor, qa_lmctf_grapple_state state);
void qa_lmctf_grapple_module_state_delete(qa_lmctf_grapple *grapple, qa_actor_id actor);
void qa_lmctf_grapple_module_states_clear(qa_lmctf_grapple *grapple);
qa_q2_callback_definitions qa_lmctf_grapple_module_callbacks(qa_lmctf_grapple *grapple);
double qa_lmctf_grapple_module_gravity_scale(qa_lmctf_grapple *grapple, qa_actor_id actor); /* 0 | 1 */
void qa_lmctf_grapple_module_abort(qa_lmctf_grapple *grapple, qa_owned_actor player, qa_q2_game_services *game);
void qa_lmctf_grapple_module_fire(qa_lmctf_grapple *grapple, qa_owned_actor player, qa_q2_game_services *game);
void qa_lmctf_grapple_module_command(qa_lmctf_grapple *grapple, qa_q2_entity *player, qa_q2_game_services *game, bool pressed);
#endif
