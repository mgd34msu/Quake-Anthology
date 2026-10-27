/* Port of src/content/q2/equipment/ctf-grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_EQUIPMENT_CTF_GRAPPLE_H
#define QA_CONTENT_Q2_EQUIPMENT_CTF_GRAPPLE_H
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/foundation/callbacks.h"
typedef struct qa_ctf_grapple_settings { double fly_speed, pull_speed, damage; bool players_collide; } qa_ctf_grapple_settings;
typedef struct qa_q2_ctf_grapple_equipment qa_q2_ctf_grapple_equipment;
/* can_damage/settings NULL = TS defaults. */
qa_q2_ctf_grapple_equipment *qa_q2_ctf_grapple_create(qa_grapple_hooks hooks,
  bool (*can_damage)(void *ctx, qa_actor_id owner, qa_actor_id target), void *can_ctx,
  qa_ctf_grapple_settings (*settings)(void *ctx, qa_actor_id a, qa_q2_game_services *g), void *settings_ctx);
void qa_q2_ctf_grapple_destroy(qa_q2_ctf_grapple_equipment *e);
qa_grapple_hooks qa_q2_ctf_grapple_hooks(qa_q2_ctf_grapple_equipment *e);
void qa_q2_ctf_grapple_bind(qa_q2_ctf_grapple_equipment *e, qa_q2_game_services *g);
qa_ctf_grapple_state *qa_q2_ctf_grapple_state(qa_q2_ctf_grapple_equipment *e, qa_actor_id a);
qa_q2_callback_definitions qa_q2_ctf_grapple_callbacks(qa_q2_ctf_grapple_equipment *e);
void qa_q2_ctf_grapple_sound(qa_q2_ctf_grapple_equipment *e, qa_actor_id entity, qa_actor_id owner, qa_q2_game_services *g, const char *file, bool reliable);
void qa_q2_ctf_grapple_reset(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g);
void qa_q2_ctf_grapple_touch(qa_q2_ctf_grapple_equipment *e, qa_q2_entity *hook, qa_q2_game_services *g, qa_touch_contact contact);
void qa_q2_ctf_grapple_offhand(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g, bool pressed);
void qa_q2_ctf_grapple_fire_from_pose(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g);
bool qa_q2_ctf_grapple_fire(qa_q2_ctf_grapple_equipment *e, qa_actor_id owner, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir, double damage, double speed, double effects);
void qa_q2_ctf_grapple_pull(qa_q2_ctf_grapple_equipment *e, qa_q2_entity *hook, qa_q2_game_services *g, bool damage_pulse);
void qa_q2_ctf_grapple_player_frame(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g, bool damage_pulse);
#endif
