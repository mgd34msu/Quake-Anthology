/* Port of src/content/q3/base/game/shader-remaps.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SHADER_REMAPS_H
#define QA_CONTENT_Q3_BASE_GAME_SHADER_REMAPS_H
#include <stddef.h>
#include "qa/persistence/value.h"
typedef struct qa_q3_shader_remap { char *old_name, *new_name; double time_offset; } qa_q3_shader_remap;
typedef struct qa_q3_shader_remap_registry {
  qa_q3_shader_remap *remaps; size_t count, cap;
  void *print_ctx; void (*print)(void *ctx, const char *message);
} qa_q3_shader_remap_registry;
void qa_q3_shader_remap_registry_init(qa_q3_shader_remap_registry *reg, void *print_ctx, void (*print)(void *ctx, const char *message));
void qa_q3_shader_remap_registry_destroy(qa_q3_shader_remap_registry *reg);
void qa_q3_shader_remap_registry_add(qa_q3_shader_remap_registry *reg, const char *old_name, const char *new_name, double time_offset);
/* Malloc'd (NULL=OOM). */
char *qa_q3_shader_remap_registry_build_config(qa_q3_shader_remap_registry *reg);
/* Capture returns malloc'd array copy; free with destroy helper. */
typedef struct qa_q3_shader_remap_state { qa_q3_shader_remap *remaps; size_t count; } qa_q3_shader_remap_state;
qa_q3_shader_remap_state qa_q3_shader_remap_registry_capture(qa_q3_shader_remap_registry *reg);
void qa_q3_shader_remap_state_destroy(qa_q3_shader_remap_state *state);
void qa_q3_shader_remap_registry_restore(qa_q3_shader_remap_registry *reg, const qa_save_value *value);
#endif
