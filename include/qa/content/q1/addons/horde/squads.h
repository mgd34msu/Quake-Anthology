/* Port of src/content/q1/addons/horde/squads.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_HORDE_SQUADS_H
#define QA_CONTENT_Q1_ADDONS_HORDE_SQUADS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
/* TS HordeMonster / HordeSquad / HordeSquadType keep their exact strings. */
typedef struct qa_q1ad_squad_spawn {
  const char *monster;
  qa_vec3 offset;
} qa_q1ad_squad_spawn;
typedef double (*qa_q1ad_random_fn)(void *ctx);
/* TS hordeSquad: writes up to capacity spawns, returns the squad size (at most 3). */
size_t qa_q1ad_horde_squad(const char *name, double skill, qa_q1ad_random_fn random, void *random_ctx,
                           qa_q1ad_squad_spawn *out, size_t capacity);
/* TS chooseHordeSquad: false = TS null. */
bool qa_q1ad_choose_horde_squad(bool army, const char *category, qa_q1ad_random_fn random, void *random_ctx,
                                const char **out_squad, const char **out_type);
#endif
