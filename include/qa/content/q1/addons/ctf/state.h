/* Port of src/content/q1/addons/ctf/state.ts (CtfState). The struct also
 * carries the Q1Ctf registrations (index.ts), which the game borrows.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_STATE_H
#define QA_CONTENT_Q1_ADDONS_CTF_STATE_H
#include "qa/content/q1/addons/ctf/types.h"
#include "qa/content/q1/equipment/threewave_weapon.h"

typedef struct qa_q1ad_ctf_state {
  qa_q1ad_context *context;
  qa_q1ad_ctf_services services;
  const qa_shared_grapple_control *shared_grapple; /* NULL = TS null; borrowed */
  qa_q1_threewave_grapple *grapple;                /* NULL = TS null; owned */
  qa_q1_threewave_weapon *grapple_weapon;          /* NULL = TS null; owned */
  int64_t grapple_release;                         /* ThreewaveGrapple host.actors.onRelease */
  /* Registrations borrowed by the game (registerWeapon/WeaponRules/PickupRules/PlayerExtension). */
  qa_q1_weapon_definition grapple_definition;
  qa_q1_weapon_rules rune_weapons;
  qa_q1_pickup_rules weapon_pickups;
  qa_q1_player_extension spawn_parameters;
} qa_q1ad_ctf_state;

double qa_q1ad_ctf_team_number(const char *team);
const char *qa_q1ad_ctf_number_team(double value);
const char *qa_q1ad_ctf_opposite(const char *team);
qa_item_id qa_q1ad_ctf_rune_item(const char *rune);
/* new CtfState(context, services, sharedGrapple). False only on OOM. */
bool qa_q1ad_ctf_state_init(qa_q1ad_ctf_state *state, qa_q1ad_context *context,
                            qa_q1ad_ctf_services services, const qa_shared_grapple_control *shared);
void qa_q1ad_ctf_state_free(qa_q1ad_ctf_state *state);
bool qa_q1ad_ctf_native_grapple_enabled(const qa_q1ad_ctf_state *state);
qa_q1_threewave_state *qa_q1ad_ctf_grapple_state(qa_q1ad_ctf_state *state, qa_actor_id actor);
bool qa_q1ad_ctf_grapple_pulling(qa_q1ad_ctf_state *state, qa_actor_id actor);
double qa_q1ad_ctf_teamplay(qa_q1ad_ctf_state *state);
bool qa_q1ad_ctf_start_map(qa_q1ad_ctf_state *state);
qa_q1_actor *qa_q1ad_ctf_world(qa_q1ad_ctf_state *state);
qa_owned_actor qa_q1ad_ctf_owner(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1_body qa_q1ad_ctf_body(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_write_body(qa_q1ad_ctf_state *state, qa_actor_id actor, const qa_q1_body_patch *patch);
double qa_q1ad_ctf_number(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *name);
void qa_q1ad_ctf_set(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *name, double value);
const char *qa_q1ad_ctf_team(qa_q1ad_ctf_state *state, qa_actor_id actor);
const char *qa_q1ad_ctf_last_team(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1_actor *qa_q1ad_ctf_flag(qa_q1ad_ctf_state *state, const char *team);
const char *qa_q1ad_ctf_flag_team(const qa_q1_actor *flag);
qa_q1_actor *qa_q1ad_ctf_carried(qa_q1ad_ctf_state *state, qa_actor_id actor);
qa_q1_actor *qa_q1ad_ctf_hook(qa_q1ad_ctf_state *state, qa_actor_id actor);
const char *qa_q1ad_ctf_rune(qa_q1ad_ctf_state *state, qa_actor_id actor);
/* TS grant(actor, item, count, capacity = 1). */
void qa_q1ad_ctf_grant(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_item_id item, double count, double capacity);
/* TS announce(key, actor?, extra = ""): actor NULL = undefined. */
void qa_q1ad_ctf_announce(qa_q1ad_ctf_state *state, const char *key, const qa_actor_id *actor, const char *extra);
/* TS update(player?): player NULL = every player. */
void qa_q1ad_ctf_update(qa_q1ad_ctf_state *state, const qa_actor_id *player);
#endif
