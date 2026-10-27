/* Port of src/compat/q2/rerelease/world-text.ts. GPL-2.0-or-later.
 * WorldTextInput (text/world.ts) is mirrored because its landed
 * header pulls the guest-side value set; shape matches q2WorldText. */
#ifndef QA_COMPAT_Q2_RERELEASE_WORLD_TEXT_H
#define QA_COMPAT_Q2_RERELEASE_WORLD_TEXT_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/module.h"

/** Mirror of WorldTextInput (TS owner text/world.ts). */
typedef struct qa_compat_q2_world_text_input {
  qa_vec3 origin; bool has_angles; qa_vec3 angles;
  const char *text; qa_vec4 color; double size; bool depth_test;
} qa_compat_q2_world_text_input;
/** q2WorldText (TS owner content/q2/rerelease/world-text.ts). */
qa_compat_q2_world_text_input qa_compat_q2_q2_world_text(qa_compat_q2_world_text_input input);
typedef struct qa_compat_q2_rerelease_world_text_event { qa_compat_q2_world_text_input text; double lifetime; } qa_compat_q2_rerelease_world_text_event;
typedef struct qa_compat_q2_rerelease_world_text_imports qa_compat_q2_rerelease_world_text_imports;
/* NULL only on OOM. */
qa_compat_q2_rerelease_world_text_imports *qa_compat_q2_rerelease_world_text_imports_create(
    qa_compat_q2_memory *memory, void *emit_ctx, void (*emit)(void *ctx, qa_compat_q2_rerelease_world_text_event event));
void qa_compat_q2_rerelease_world_text_imports_destroy(qa_compat_q2_rerelease_world_text_imports *w);
/* False is the TS undefined (unhandled call). Event text is scratch. */
bool qa_compat_q2_rerelease_world_text_imports_invoke(qa_compat_q2_rerelease_world_text_imports *w, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out);
#endif
