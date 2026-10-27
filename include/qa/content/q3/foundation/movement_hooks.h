/* Port of src/content/q3/foundation/movement-hooks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_MOVEMENT_HOOKS_H
#define QA_CONTENT_Q3_FOUNDATION_MOVEMENT_HOOKS_H
#include <stdbool.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/identity.h"
#include "qa/movement/q3/types.h"
typedef struct qa_q3_arsenal_runtime_access {
  void *ctx;
  qa_q3_arsenal_runtime_state (*read)(void *ctx, qa_owned_actor actor, const char *execution);
  void (*write)(void *ctx, qa_owned_actor actor, const char *execution, qa_q3_arsenal_runtime_state state);
  bool (*gauntlet_hit)(void *ctx, qa_q3_hook_context context);
} qa_q3_arsenal_runtime_access;
qa_q3_arsenal_controls qa_q3_source_arsenal_controls(qa_q3_hook_context context);
/* Returned hooks hold a malloc'd holder in ctx; free with the destroy helper. */
qa_q3_movement_hooks qa_q3_create_source_movement_hooks(qa_q3_arsenal_runtime_access runtime);
void qa_q3_source_movement_hooks_destroy(qa_q3_movement_hooks *hooks);
#endif
