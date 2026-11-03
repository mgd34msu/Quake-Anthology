/* Port of src/content/q2/multiplayer/ctf/grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_GRAPPLE_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_GRAPPLE_H
#include "qa/content/q2/equipment/ctf_grapple.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/contracts/world.h"
typedef struct qa_q2_ctf_grapple qa_q2_ctf_grapple;
/* shared NULL = TS null. */
qa_q2_ctf_grapple *qa_q2_ctf_grapple_module_create(qa_q2_ctf_context *context, qa_shared_grapple_control *shared);
void qa_q2_ctf_grapple_module_destroy(qa_q2_ctf_grapple *grapple);
qa_q2_ctf_grapple_equipment *qa_q2_ctf_grapple_module_equipment(qa_q2_ctf_grapple *grapple);
bool qa_q2_ctf_grapple_module_native_enabled(qa_q2_ctf_grapple *grapple);
qa_shared_grapple_control *qa_q2_ctf_grapple_module_shared(qa_q2_ctf_grapple *grapple);
/* TS states/state/delete over equipment or inactive map. */
qa_ctf_grapple_state qa_q2_ctf_grapple_module_state(qa_q2_ctf_grapple *grapple, qa_actor_id actor);
qa_ctf_grapple_state *qa_q2_ctf_grapple_module_state_ref(qa_q2_ctf_grapple *grapple, qa_actor_id actor);
bool qa_q2_ctf_grapple_module_state_has(qa_q2_ctf_grapple *grapple, qa_actor_id actor);
void qa_q2_ctf_grapple_module_state_set(qa_q2_ctf_grapple *grapple, qa_actor_id actor, qa_ctf_grapple_state state);
void qa_q2_ctf_grapple_module_state_delete(qa_q2_ctf_grapple *grapple, qa_actor_id actor);
/* TS states.clear(): the landed equipment exposes no map clear, so
 * this only clears the inactive map (see grapple.c note). */
void qa_q2_ctf_grapple_module_states_clear(qa_q2_ctf_grapple *grapple);
qa_q2_callback_definitions qa_q2_ctf_grapple_module_callbacks(qa_q2_ctf_grapple *grapple);
void qa_q2_ctf_grapple_module_reset(qa_q2_ctf_grapple *grapple, qa_owned_actor player, qa_q2_game_services *game);
void qa_q2_ctf_grapple_module_offhand(qa_q2_ctf_grapple *grapple, qa_q2_entity *player, qa_q2_game_services *game, bool pressed);
void qa_q2_ctf_grapple_module_touch(qa_q2_ctf_grapple *grapple, qa_q2_entity *hook, qa_q2_game_services *game, qa_touch_contact contact);
/* TS defaults damage 10, speed 650, effects 0 (qa_..._default). */
bool qa_q2_ctf_grapple_module_fire_grapple(qa_q2_ctf_grapple *grapple, qa_q2_entity *player, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, double damage, double speed, double effects);
bool qa_q2_ctf_grapple_module_fire_grapple_default(qa_q2_ctf_grapple *grapple, qa_q2_entity *player, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction);
/* binding: "weapon-slot" | "offhand". */
void qa_q2_ctf_grapple_module_player_frame(qa_q2_ctf_grapple *grapple, qa_q2_entity *player, qa_q2_game_services *game, const char *binding);
void qa_q2_ctf_grapple_module_command(qa_q2_ctf_grapple *grapple, qa_q2_entity *player, bool pressed);
void qa_q2_ctf_grapple_module_register(qa_q2_ctf_grapple *grapple);
void qa_q2_ctf_grapple_module_fire(qa_q2_ctf_grapple *grapple, qa_q2w_context *context);
#endif
