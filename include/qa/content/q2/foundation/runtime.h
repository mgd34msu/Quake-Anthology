/* Port of src/content/q2/foundation/runtime.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_RUNTIME_H
#define QA_CONTENT_Q2_FOUNDATION_RUNTIME_H
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/contracts/content.h"
typedef struct qa_q2_spawn_report {
  double authored;
  const qa_q2_spawn_fields *const *inhibited; size_t inhibited_count;
  const qa_q2_spawn_fields *const *removed_by_source; size_t removed_count;
  qa_q2_entity *const *spawned; size_t spawned_count;
  qa_q2_entity *const *unsupported; size_t unsupported_count;
  const qa_owned_actor *replaced; size_t replaced_count;
} qa_q2_spawn_report;
/* TS MonsterDefinitionReference mirror (TS owner: src/contracts/content.ts landed shape used where available). */
typedef struct qa_q2_monster_admission {
  void *ctx;
  bool (*resolve)(void *ctx, const char *classname, const qa_q2_spawn_fields *source, qa_monster_definition_reference *out);
  void (*spawn)(void *ctx, qa_owned_actor actor, const qa_q2_spawn_fields *source, qa_monster_definition_reference def);
} qa_q2_monster_admission;
typedef qa_q2_entity_services qa_q2_foundation;
void qa_q2_foundation_set_monster_admission(qa_q2_foundation *f, const qa_q2_monster_admission *a);
/* keep NULL = all. Report storage owned by foundation until next load. */
qa_q2_spawn_report qa_q2_foundation_load(qa_q2_foundation *f, const char *source, const double *keep, size_t keep_count);
#endif
