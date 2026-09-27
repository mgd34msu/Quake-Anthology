#ifndef QA_Q2_MONSTER_SPAWN_H
#define QA_Q2_MONSTER_SPAWN_H
#include "qa/game_q2_monsters.h"

bool q2m_check_spawn_point(qa_q2_game *, qa_vec3, qa_bounds, bool *, qa_error *);
bool q2m_rerelease_find_spawn_point(qa_q2_game *, qa_vec3, qa_bounds, bool drop, bool *found,
                                    qa_vec3 *position, qa_error *);
bool q2m_rerelease_check_ground_spawn(qa_q2_game *, qa_vec3, qa_bounds, bool *valid, qa_error *);

#endif
