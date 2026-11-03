/* Port of src/content/q1/addons/coop-spawns.ts (quakec_{mg1,mg3}/client.qc:
 * info_player_coop checkpoints and COOP_RESPAWN_KEEP_WEAPONS). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_COOP_SPAWNS_H
#define QA_CONTENT_Q1_ADDONS_COOP_SPAWNS_H
#include "qa/content/q1/addons/context.h"
/** A checkpoint trigger activates exactly the points it targets and records the weapons the team holds. */
void qa_q1ad_register_coop_spawns(qa_q1ad_context *context);
/** PutClientInServer: a respawning coop player gets back the weapons recorded on the spot, then the ammo floors. */
void qa_q1ad_restore_coop_weapons(qa_q1ad_context *context, qa_actor_id actor, qa_q1_actor *spot);
#endif
