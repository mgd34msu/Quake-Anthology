/* Port of src/content/q1/missionpacks/world/rogue-teams.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_TEAMS_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_TEAMS_H
#include <stdint.h>

#include "qa/content/q1/missionpacks/types.h"

typedef struct qa_q1mp_world_hooks qa_q1mp_world_hooks;
/* TS RogueTeams. states is the TS Map<OwnedActor, Q1Actor>. hooks is
 * borrowed; the struct must not move after init (the game borrows the
 * extension and effects, and the callbacks receive the struct). */
typedef struct qa_q1mp_team_state_entry { qa_owned_actor actor; qa_q1_actor *state; } qa_q1mp_team_state_entry;
typedef struct qa_q1mp_rogue_teams {
  qa_q1_services *game; const qa_q1mp_world_hooks *hooks;
  qa_q1mp_team_state_entry *states; size_t state_count, state_capacity;
  qa_q1_player_extension extension;
  int64_t release_subscription;
} qa_q1mp_rogue_teams;
void qa_q1mp_rogue_teams_init(qa_q1mp_rogue_teams *teams, qa_q1_services *game, const qa_q1mp_world_hooks *hooks);
void qa_q1mp_rogue_teams_destroy(qa_q1mp_rogue_teams *teams);
void qa_q1mp_teams_frame(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
void qa_q1mp_teams_player_spawned(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
void qa_q1mp_teams_drop_carried_flag(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
bool qa_q1mp_teams_impulse(qa_q1mp_rogue_teams *teams, qa_actor_id actor, double impulse);
/* TS suicide: false refuses the suicide. */
bool qa_q1mp_teams_suicide(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
/* actor NULL = TS null. */
double qa_q1mp_teams_team(qa_q1mp_rogue_teams *teams, const qa_actor_id *actor);
/* NULL = TS undefined. */
qa_q1_actor *qa_q1mp_teams_select_spawn(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
void qa_q1mp_teams_confirmed_damage(qa_q1mp_rogue_teams *teams, qa_actor_id target, const qa_actor_id *attacker);
void qa_q1mp_teams_player_died(qa_q1mp_rogue_teams *teams, qa_actor_id actor, const qa_actor_id *attacker);

#endif
