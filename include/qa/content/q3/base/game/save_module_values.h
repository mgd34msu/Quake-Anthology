/* Port of src/content/q3/base/game/save-module-values.ts. GPL-2.0-or-later.
 * CvarSnapshot mirror: TS owner core/cvars (landed elsewhere; local shape). */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_MODULE_VALUES_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_MODULE_VALUES_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/persistence/value.h"
typedef struct qa_q3_cvar_snapshot_mirror {
  char *name, *value, *reset_value, *latched_value; double flags; bool modified; double modification_count, numeric_value, integer_value;
} qa_q3_cvar_snapshot_mirror;
typedef struct qa_q3_module_cvar_capture {
  const char *name, *value, *reset_value, *latched_value; double flags; bool modified; double modification_count, numeric_value, integer_value;
} qa_q3_module_cvar_capture;
qa_q3_game_entity *qa_q3_read_module_entity(const qa_save_reader *reader, qa_q3_entity_pool *pool);
qa_q3_module_cvar_capture qa_q3_capture_module_cvar(qa_q3_cvar_snapshot_mirror value);
qa_q3_cvar_snapshot_mirror qa_q3_read_module_cvar(const qa_save_reader *reader);
void qa_q3_cvar_snapshot_mirror_destroy(qa_q3_cvar_snapshot_mirror *value);
#endif
