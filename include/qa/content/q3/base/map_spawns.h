/* Port of src/content/q3/base/map-spawns.ts. GPL-2.0-or-later.
 * The TS ReadonlyMap becomes a deduplicated handler entry array
 * (later registrations replace earlier ones, like Map.set). */
#ifndef QA_CONTENT_Q3_BASE_MAP_SPAWNS_H
#define QA_CONTENT_Q3_BASE_MAP_SPAWNS_H
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/misc_spawn.h"
#include "qa/content/q3/base/game/mover_spawn.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/targets.h"
#include "qa/content/q3/base/game/triggers.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/team.h"

/* TS Q3SpawnHandlersHost. */
typedef struct qa_q3_base_spawn_handlers_host {
  qa_q3_base_product product;
  qa_q3_misc_spawn_host misc;
  qa_q3_mover_spawn_host movers;
  qa_q3_trigger_host triggers;
  qa_q3_target_runtime *targets;
  qa_q3_ta_team_runtime *team;
} qa_q3_base_spawn_handlers_host;

/* Fills out/out_cap and returns the total handler count (which may
 * exceed out_cap, mirroring the landed sub-fillers). */
size_t qa_q3_base_create_spawn_handlers(qa_q3_base_spawn_handlers_host host,
    qa_q3_spawn_handler_entry *out, size_t out_cap);

/* TS findQ3EntityTeams result. */
typedef struct qa_q3_base_entity_teams { double teams, entities; } qa_q3_base_entity_teams;
qa_q3_base_entity_teams qa_q3_base_find_entity_teams(qa_q3_entity_pool *pool);
#endif
