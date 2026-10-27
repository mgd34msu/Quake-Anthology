/* Port of src/content/q1/missionpacks/world/rogue-teams.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_TEAMS_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_TEAMS_H
#include "qa/content/q1/missionpacks/types.h"

struct qa_q1mp_world_hooks;
typedef struct qa_q1mp_rogue_teams { qa_q1mp_game *game; const struct qa_q1mp_world_hooks *hooks; } qa_q1mp_rogue_teams;
void qa_q1mp_rogue_teams_init(qa_q1mp_rogue_teams *teams, qa_q1mp_game *game, const struct qa_q1mp_world_hooks *hooks);
void qa_q1mp_teams_frame(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
void qa_q1mp_teams_player_spawned(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
void qa_q1mp_teams_drop_carried_flag(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
bool qa_q1mp_teams_impulse(qa_q1mp_rogue_teams *teams, qa_actor_id actor, double impulse);
double qa_q1mp_teams_team(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
qa_q1mp_actor *qa_q1mp_teams_select_spawn(qa_q1mp_rogue_teams *teams, qa_actor_id actor);
void qa_q1mp_teams_confirmed_damage(qa_q1mp_rogue_teams *teams, qa_actor_id target, qa_q1mp_opt_actor attacker);
void qa_q1mp_teams_player_died(qa_q1mp_rogue_teams *teams, qa_actor_id actor, qa_q1mp_opt_actor attacker);

#endif
