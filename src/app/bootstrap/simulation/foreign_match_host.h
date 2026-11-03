/* Internal header of the foreign match rules (TS foreign-match-host.ts): the simulation helpers TagRules,
 * FlagMatchRules, DeathballRules and HordeRules share, ForeignMatchSpawns (deathmatch spawn origins, farthest spawn)
 * and the balanced team assignment. Defined in foreign_match_host.c. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_MATCH_HOST_H
#define QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_MATCH_HOST_H
#include <stdnoreturn.h>
#include "shared_simulation_internal.h"
#include "qa/content/q3/base/records.h"
#include "qa/formats/q3_map/entities.h"

noreturn void qa_fm_fail(const char *format, ...);
void *qa_fm_alloc(size_t count, size_t size);
qa_shared_body_table *qa_fm_bodies(qa_app_shared_simulation *sim);
double qa_fm_time(const qa_app_shared_simulation *sim);
/* players() copied (the caller frees). */
qa_actor_id *qa_fm_players(qa_app_shared_simulation *sim, size_t *out_count);
bool qa_fm_includes(const qa_actor_id *actors, size_t count, qa_actor_id actor);
double qa_fm_health(qa_app_shared_simulation *sim, qa_actor_id actor);
const char *qa_fm_family_name(const char *provider);
qa_q3_game_entity *qa_fm_q3_native(qa_app_shared_simulation *sim, qa_actor_id actor);
/* TS mapFamily(host) !== "q2". */
bool qa_fm_family_is_not_q2_map(const qa_app_shared_simulation *sim);
bool qa_fm_deathmatch(const qa_app_shared_simulation *sim);
qa_owned_actor qa_fm_spawn_trigger(qa_app_shared_simulation *sim, const char *definition, qa_vec3 origin, qa_bounds bounds,
    void (*touch)(void *ctx, qa_touch_contact contact));
void qa_fm_detached_model(qa_app_shared_simulation *sim, qa_owned_actor actor, const char *path);
/* TS entityOrigin(entity); false = TS null. */
bool qa_fm_entity_origin(const qa_q3_entity *entity, qa_vec3 *out);
/* TS ForeignMatchSpawns.origins(). */
const qa_vec3 *qa_fm_tag_spawn_origins(qa_app_shared_simulation *sim, size_t *out_count);
/* TS balancedTeam over each unassigned player. */
void qa_fm_assign_teams(qa_app_shared_simulation *sim, qa_ss_map *teams, const qa_actor_id *players, size_t player_count);
#endif
