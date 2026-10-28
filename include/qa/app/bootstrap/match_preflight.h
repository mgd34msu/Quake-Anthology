/* Port of src/app/bootstrap/match-preflight.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MATCH_PREFLIGHT_H
#define QA_APP_BOOTSTRAP_MATCH_PREFLIGHT_H
#include <stddef.h>
#include "qa/content/catalog/index.h"
#include "qa/content/q3/team_arena/foreign_objectives.h"
#include "qa/contracts/content.h"
/* TS Pick<LoadedApplicationContent, "catalog"|"recipe"> plus the
 * world's kind/entities (content.ts is another bootstrap slice). */
typedef struct qa_app_preflight_content {
  const qa_content_catalog *catalog; /* borrowed */
  const qa_executable_recipe *recipe; /* borrowed */
  qa_q3_ta_world_kind world_kind;
  const char *world_entities; /* borrowed entity text */
} qa_app_preflight_content;
typedef struct qa_app_source_value { const char *name; const char *value; } qa_app_source_value;
/* Aborts with the TS Error text when the match is unavailable.
 * mode is Pick<ApplicationOptions,"mode">. */
void qa_app_preflight_application_match(qa_app_preflight_content content, const char *mode,
    const qa_app_source_value *source_values, size_t source_value_count);
#endif
