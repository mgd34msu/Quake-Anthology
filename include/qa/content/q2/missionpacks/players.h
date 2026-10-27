/* Port of src/content/q2/missionpacks/players.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_PLAYERS_H
#define QA_CONTENT_Q2_MISSIONPACKS_PLAYERS_H
#include <stdbool.h>
#include "qa/content/q2/foundation/weapons/types.h"

/* TS Q2RoguePlayerSpawns (stateless). */
typedef struct qa_q2mp_rogue_player_spawns { int unused; } qa_q2mp_rogue_player_spawns;
void qa_q2mp_rogue_player_spawns_init(qa_q2mp_rogue_player_spawns *spawns);
bool qa_q2mp_rogue_player_spawns_spawn(qa_q2mp_rogue_player_spawns *s, qa_q2f_entity *entity, qa_q2f_game *game);
/* False return is TS null. */
typedef struct qa_q2mp_spawn_point { qa_vec3 origin, angles; } qa_q2mp_spawn_point;
bool qa_q2mp_rogue_player_spawns_select_spawn(qa_q2mp_rogue_player_spawns *s, qa_q2f_game *game,
                                              qa_q2mp_spawn_point *out);
#endif
