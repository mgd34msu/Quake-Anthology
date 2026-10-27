/* Port of src/content/q2/missionpacks/monsters/spawn.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_SPAWN_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_SPAWN_H

#include "qa/content/q2/foundation/monsters/index.h"

/* False first result is TS null (out untouched). */
bool qa_q2mpm_find_rogue_spawn_point(qa_q2f_game *game, qa_vec3 start, qa_bounds bounds, double max_move_up,
                                     qa_vec3 *out);
bool qa_q2mpm_check_rogue_spawn_point(qa_q2f_game *game, qa_vec3 origin, qa_bounds bounds);
bool qa_q2mpm_check_rogue_ground_spawn_point(qa_q2f_game *game, qa_vec3 origin, qa_bounds bounds, double height,
                                             double gravity);
qa_q2f_entity *qa_q2mpm_create_rogue_monster(qa_q2m_monsters *monsters, qa_q2f_game *game, qa_vec3 origin,
                                             qa_vec3 angles, const char *classname);
/* NULL is TS null. */
qa_q2f_entity *qa_q2mpm_create_rogue_ground_monster(qa_q2m_monsters *monsters, qa_q2f_game *game, qa_vec3 origin,
                                                    qa_vec3 angles, qa_bounds bounds, const char *classname,
                                                    double height);
qa_q2f_callback_definitions qa_q2mpm_rogue_spawn_callbacks(void);
qa_q2f_entity *qa_q2mpm_rogue_spawn_grow(qa_q2f_game *game, qa_vec3 origin, double size);

#endif
