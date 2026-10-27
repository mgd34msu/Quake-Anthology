/* Port of src/content/q1/missionpacks/monsters/helpers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_HELPERS_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_HELPERS_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

void qa_q1mpm_number(qa_q1mpm_mission_monster *monster, const char *key, double value);
bool qa_q1mpm_drop_to_floor(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gib(qa_q1mpm_mission_monster *monster, const char *head,
                  const char *const *gibs, size_t gib_count, const char *sound);
bool qa_q1mpm_eye(qa_q1mpm_game *game, qa_actor_id actor, qa_vec3 *out);
/* TS radiusActors: fills caller buffer (reversed observation order). */
size_t qa_q1mpm_radius_actors(qa_q1mpm_game *game, qa_vec3 origin, double radius,
                              qa_actor_id *out, size_t capacity);
void qa_q1mpm_eel_zap(qa_q1mpm_mission_monster *monster);
qa_q1mpm_actor *qa_q1mpm_missile(qa_q1mpm_game *game, qa_actor_id owner, const char *classname,
                                 const char *model, qa_vec3 origin, qa_vec3 velocity,
                                 const char *touch, double seconds);
void qa_q1mpm_empty_pain(void);
extern const qa_bounds qa_q1mpm_human_bounds;
extern const qa_bounds qa_q1mpm_hull_bounds;
void qa_q1mpm_set_point_bounds(qa_q1mpm_mission_monster *monster);

#endif
