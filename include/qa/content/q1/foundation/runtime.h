/*
 * Port of src/content/q1/foundation/runtime.ts. GPL-2.0-or-later.
 * Q1Foundation is Q1EntityServices plus map admission; in C the same
 * qa_q1_services value is used with the qa_q1_foundation_* functions.
 * monsterAdmission is a fn+ctx mirror (TS src/contracts/content.ts
 * MonsterDefinitionReference summarized by provider+classname).
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_RUNTIME_H
#define QA_CONTENT_Q1_FOUNDATION_RUNTIME_H

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/formats/q1_map/types.h"

typedef struct qa_q1_spawn_note { double ordinal; const char *classname; const char *reason; } qa_q1_spawn_note;
typedef struct qa_q1_spawn_report {
  qa_q1_presentation *spawned; size_t spawned_count;
  qa_q1_spawn_note *inhibited; size_t inhibited_count;
  qa_q1_spawn_note *compiler_only; size_t compiler_only_count;
  qa_q1_spawn_note *foreign; size_t foreign_count;
  void *storage;
} qa_q1_spawn_report;
void qa_q1_spawn_report_free(qa_q1_spawn_report *report);

typedef struct qa_q1_monster_admission {
  void *ctx;
  /* resolve: false = TS null; out strings borrowed. */
  bool (*resolve)(void *ctx, const char *classname, const qa_q1_entity *source,
                  const char **out_provider, const char **out_classname);
  void (*spawn)(void *ctx, qa_owned_actor actor, const qa_q1_entity *source, double ordinal,
                const char *provider, const char *classname);
} qa_q1_monster_admission;

void qa_q1_foundation_set_monster_admission(qa_q1_services *game, const qa_q1_monster_admission *admission);
const qa_q1_monster_admission *qa_q1_foundation_monster_admission(qa_q1_services *game);
/* keep == NULL spawns all ordinals. False only on OOM (report zeroed). */
bool qa_q1_foundation_spawn_entities(qa_q1_services *game, const qa_q1_entity *entity_list, size_t entity_count,
                                     const qa_bounds *model_bounds, size_t model_count, const char *map_name,
                                     const double *keep, size_t keep_count, qa_q1_spawn_report *out);
bool qa_q1_foundation_spawn_map(qa_q1_services *game, const qa_q1_map *map, qa_q1_spawn_report *out);

#endif
