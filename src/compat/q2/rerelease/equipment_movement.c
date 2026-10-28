/* Port of src/compat/q2/rerelease/equipment-movement.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/equipment_movement.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/api.h"
#include "qa/foundation/cleanup.h"

static qa_compat_q2_rerelease_guest_module *active_module = NULL;
static void *active_scope = NULL;
static void set_vector(uint8_t *state, const char *name, qa_vec3 value) {
  size_t off = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, name);
  qa_compat_q2_view_set_f32(state, off, value.x); qa_compat_q2_view_set_f32(state, off + 4, value.y); qa_compat_q2_view_set_f32(state, off + 8, value.z);
}
void qa_compat_q2_prepare_rerelease_equipment_movement(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                                       const qa_equipment_movement *equipment) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  uint8_t *state = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_pmove_layout.byte_length);
  size_t flags = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_flags");
  size_t gravity = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.gravity");
  if (equipment->velocity) set_vector(state, "s.velocity", *equipment->velocity);
  qa_compat_q2_view_set_i16(state, gravity, (int32_t)trunc(qa_compat_q2_view_i16(state, gravity) * equipment->gravity_scale));
  qa_compat_q2_view_set_u16(state, flags, equipment->prediction_suppressed ? qa_compat_q2_view_u16(state, flags) | 64 : qa_compat_q2_view_u16(state, flags) & ~64u);
}
typedef struct load_ctx { qa_compat_q2_rerelease_guest_module *module; void *scope; double multiplier, reg; } load_ctx_t;
static void load_observe(void *vctx) {
  load_ctx_t *l = vctx;
  if (active_module != l->module || active_scope != l->scope) return;
  const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(l->module);
  uint8_t *xmm = opts->runner->simd_xmm; size_t off = (size_t)l->reg * 16;
  if (!xmm || off + 4 > opts->runner->simd_xmm_len) return;
  qa_compat_q2_view_set_f32(xmm, off, (float)(qa_compat_q2_view_f32(xmm, off) * l->multiplier));
}
typedef struct cleanup_ctx { qa_compat_q2_runner *runner; uint64_t handle; } cleanup_ctx_t;
static void remove_load(void *vctx) { cleanup_ctx_t *c = vctx; c->runner->remove_entry(c->runner->ctx, c->handle); }
void qa_compat_q2_with_rerelease_equipment_movement(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                                    const qa_equipment_movement *equipment, void *execute_ctx, void (*execute)(void *)) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  uint8_t *state = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_pmove_layout.byte_length);
  size_t flags = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_flags");
  size_t type = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_type");
  int32_t original_type = qa_compat_q2_view_i32(state, type);
  const qa_fixed_movement_pose *pose = equipment ? equipment->pose : NULL;
  double multiplier = equipment && equipment->has_speed_multiplier ? equipment->speed_multiplier : 1;
  qa_compat_q2_rerelease_guest_module *previous = active_module;
  void *previous_scope = active_scope;
  int scope_token = 0;
  qa_cleanup_stack removals; qa_cleanup_stack_init(&removals);
  load_ctx_t *loads = NULL; cleanup_ctx_t *cleanups = NULL;
  active_module = module;
  active_scope = &scope_token;
  if (!isfinite(multiplier) || multiplier <= 0) qa_range_error("Equipment movement speed must be finite and positive");
  if (multiplier != 1) {
    const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
    const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(module);
    qa_guest_address image = qa_compat_q2_mem_offset(memory, opts->get_game_api, -(int64_t)profile->movement.game_api);
    qa_guest_address game = qa_compat_q2_rerelease_guest_module_bind_game(module);
    qa_guest_address movement; bool has = qa_compat_q2_mem_read_pointer(memory,
        qa_compat_q2_mem_offset(memory, game, (int64_t)qa_compat_q2_rerelease_field_offset(qa_compat_q2_rerelease_get_game_export_layout(), "Pmove")), &movement);
    if (!has || movement.byte_offset != qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.pmove).byte_offset)
      qa_range_error("Native movement entry differs from its equipment profile");
    size_t n = profile->movement.speed_load_count;
    loads = calloc(n ? n : 1, sizeof *loads); cleanups = calloc(n ? n : 1, sizeof *cleanups);
    for (size_t i = 0; i < n; i++) {
      qa_guest_address next = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.speed_loads[i].next);
      memory->check(memory->ctx, next, 1, "execute");
      loads[i] = (load_ctx_t){module, &scope_token, multiplier, profile->movement.speed_loads[i].reg};
      uint64_t handle = opts->runner->observe_entry(opts->runner->ctx, next, &loads[i], load_observe);
      cleanups[i] = (cleanup_ctx_t){opts->runner, handle};
      qa_cleanup_stack_add(&removals, remove_load, &cleanups[i]);
    }
  }
  if (pose) { qa_compat_q2_view_set_i32(state, type, 6); set_vector(state, "s.velocity", (qa_vec3){0, 0, 0}); } /* KexPmTypeT.PM_FREEZE */
  execute(execute_ctx);
  if (pose) {
    size_t height = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.viewheight");
    state[height] = (uint8_t)pose->view_height;
    set_vector(state, "mins", pose->bounds.min); set_vector(state, "maxs", pose->bounds.max);
    qa_compat_q2_view_set_u16(state, flags, pose->crouched ? qa_compat_q2_view_u16(state, flags) | 1 : qa_compat_q2_view_u16(state, flags) & ~1u);
  }
  if (pose) qa_compat_q2_view_set_i32(state, type, original_type);
  qa_cleanup_stack_run_reverse(&removals);
  qa_cleanup_stack_destroy(&removals);
  free(loads); free(cleanups);
  active_module = previous;
  active_scope = previous_scope;
}
