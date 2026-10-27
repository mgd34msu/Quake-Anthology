/* Port of src/content/q1/addons/ctf/state.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_STATE_H
#define QA_CONTENT_Q1_ADDONS_CTF_STATE_H
#include "qa/content/q1/addons/ctf/types.h"
struct qa_q1ad_ctf_state {
  qa_q1ad_context *context;
  qa_q1ad_ctf_services services;
  qa_shared_grapple_control *shared_grapple;
  qa_q1ad_threewave_grapple grapple;
  qa_q1ad_threewave_weapon grapple_weapon;
  void *factory_ctx;
  qa_q1ad_grapple_factory grapple_factory;
  qa_q1ad_weapon_factory weapon_factory;
};
double qa_q1ad_ctf_team_number(const char *team);
const char *qa_q1ad_ctf_number_team(double value);
const char *qa_q1ad_ctf_opposite(const char *team);
const char *qa_q1ad_ctf_rune_item(const char *rune, char buffer[64]);
void qa_q1ad_ctf_state_init(qa_q1ad_ctf_state *state, qa_q1ad_context *context,
                            qa_q1ad_ctf_services services, qa_shared_grapple_control *shared);
bool qa_q1ad_ctf_native_grapple_enabled(const qa_q1ad_ctf_state *state);
void qa_q1ad_ctf_grapple_state(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1ad_grapple_pose *out);
bool qa_q1ad_ctf_grapple_pulling(qa_q1ad_ctf_state *state, qa_actor_id actor);
double qa_q1ad_ctf_teamplay(qa_q1ad_ctf_state *state);
bool qa_q1ad_ctf_start_map(qa_q1ad_ctf_state *state);
qa_q1ad_actor *qa_q1ad_ctf_world(qa_q1ad_ctf_state *state);
qa_owned_actor qa_q1ad_ctf_owner(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1ad_body qa_q1ad_ctf_body(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_write_body(qa_q1ad_ctf_state *state, qa_actor_id actor, const qa_q1ad_body_patch *patch);
double qa_q1ad_ctf_number(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *name);
void qa_q1ad_ctf_set(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *name, double value);
const char *qa_q1ad_ctf_team(qa_q1ad_ctf_state *state, qa_actor_id actor);
const char *qa_q1ad_ctf_last_team(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1ad_actor *qa_q1ad_ctf_flag(qa_q1ad_ctf_state *state, const char *team);
const char *qa_q1ad_ctf_flag_team(qa_q1ad_actor *flag);
qa_q1ad_actor *qa_q1ad_ctf_carried(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1ad_actor *qa_q1ad_ctf_hook(qa_q1ad_ctf_state *state, qa_actor_id actor);
const char *qa_q1ad_ctf_rune(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_grant(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_item_id item, double count, double capacity);
void qa_q1ad_ctf_announce(qa_q1ad_ctf_state *state, const char *key, const qa_actor_id *actor, const char *extra);
void qa_q1ad_ctf_update(qa_q1ad_ctf_state *state, const qa_actor_id *player);
#endif
