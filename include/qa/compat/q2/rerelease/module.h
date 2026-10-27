/* Port of src/compat/q2/rerelease/module.ts. GPL-2.0-or-later.
 * The Pmove entry binding uses the runner mirror's bind_entry
 * (native-mod-entries bypass reduced to an accepts predicate).
 * Async loading becomes a synchronous next-frame callback. */
#ifndef QA_COMPAT_Q2_RERELEASE_MODULE_H
#define QA_COMPAT_Q2_RERELEASE_MODULE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/world_profile.h"
#include "qa/contracts/movement.h"
#include "qa/movement/body_shape.h"

typedef struct qa_compat_q2_rerelease_import_call {
  const char *api; /* "game" | "cgame" */
  const char *name;
  qa_guest_call_context context;
  const qa_guest_call_value *arguments; size_t argument_count;
} qa_compat_q2_rerelease_import_call;
typedef struct qa_compat_q2_rerelease_guest_module qa_compat_q2_rerelease_guest_module;
typedef struct qa_compat_q2_rerelease_module_options {
  bool has_image_base; qa_guest_address image_base;
  const qa_compat_q2_rerelease_primary_world_profile *world_profile; /* optional */
  qa_compat_q2_runner *runner;
  qa_guest_address get_game_api; qa_guest_address get_cgame_api;
  void *invoke_import_ctx;
  qa_guest_call_result (*invoke_import)(void *ctx, qa_compat_q2_rerelease_import_call call);
  void *actor_ctx;
  bool (*actor_at_slot)(void *ctx, double slot, qa_actor_id *out);
  double instruction_budget; bool has_instruction_budget;
  double loading_instruction_budget; bool has_loading_budget;
  double serialization_instruction_budget; bool has_serialization_budget;
  double frame_milliseconds; bool has_frame_milliseconds;
} qa_compat_q2_rerelease_module_options;

/* TS MissingRereleaseImport message into a caller buffer, and throw form. */
void qa_compat_q2_rerelease_missing_import_message(const qa_compat_q2_rerelease_import_call *call, char *out, size_t capacity);
noreturn void qa_compat_q2_rerelease_throw_missing_import(const qa_compat_q2_rerelease_import_call *call);
qa_guest_call_value qa_compat_q2_rerelease_guest_pointer(bool has, qa_guest_address value);
qa_guest_call_value qa_compat_q2_rerelease_guest_int(double value);
qa_guest_call_value qa_compat_q2_rerelease_guest_bool(bool value);
/* False is the TS null. */
bool qa_compat_q2_rerelease_result_pointer(qa_guest_call_result result, qa_guest_address *out);

/* NULL only on OOM. */
qa_compat_q2_rerelease_guest_module *qa_compat_q2_rerelease_guest_module_create(const qa_compat_q2_rerelease_module_options *options);
void qa_compat_q2_rerelease_guest_module_destroy(qa_compat_q2_rerelease_guest_module *m);
const qa_compat_q2_rerelease_module_options *qa_compat_q2_rerelease_guest_module_get_options(const qa_compat_q2_rerelease_guest_module *m);
qa_compat_q2_memory *qa_compat_q2_rerelease_guest_module_get_memory(const qa_compat_q2_rerelease_guest_module *m);
/* NULL is the TS null worldProfile. */
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_guest_module_get_world_profile(const qa_compat_q2_rerelease_guest_module *m);
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_guest_module_require_world_profile(qa_compat_q2_rerelease_guest_module *m);
qa_guest_address qa_compat_q2_rerelease_guest_module_get_game_import_address(const qa_compat_q2_rerelease_guest_module *m);
qa_guest_address qa_compat_q2_rerelease_guest_module_get_cgame_import_address(const qa_compat_q2_rerelease_guest_module *m);
qa_guest_call_result qa_compat_q2_rerelease_guest_module_invoke(qa_compat_q2_rerelease_guest_module *m, qa_guest_address target,
                                                                qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *arguments,
                                                                size_t count, const qa_raw_entity_view *self, const qa_raw_entity_view *other);
qa_guest_address qa_compat_q2_rerelease_guest_module_bind_game(qa_compat_q2_rerelease_guest_module *m);
qa_guest_address qa_compat_q2_rerelease_guest_module_bind_cgame(qa_compat_q2_rerelease_guest_module *m);
qa_guest_call_result qa_compat_q2_rerelease_guest_module_call_game(qa_compat_q2_rerelease_guest_module *m, const char *name,
                                                                   const qa_guest_call_value *arguments, size_t count,
                                                                   const qa_raw_entity_view *self, const qa_raw_entity_view *other);
qa_guest_call_result qa_compat_q2_rerelease_guest_module_call_game_loading(qa_compat_q2_rerelease_guest_module *m, const char *name,
                                                                           const qa_guest_call_value *arguments, size_t count,
                                                                           void (*next_frame)(void *), void *next_ctx);
qa_guest_call_result qa_compat_q2_rerelease_guest_module_call_cgame(qa_compat_q2_rerelease_guest_module *m, const char *name,
                                                                    const qa_guest_call_value *arguments, size_t count);
void qa_compat_q2_rerelease_guest_module_pre_init(qa_compat_q2_rerelease_guest_module *m);
void qa_compat_q2_rerelease_guest_module_init(qa_compat_q2_rerelease_guest_module *m);
void qa_compat_q2_rerelease_guest_module_prep_frame(qa_compat_q2_rerelease_guest_module *m);
void qa_compat_q2_rerelease_guest_module_run_frame(qa_compat_q2_rerelease_guest_module *m, bool main_loop);
typedef struct qa_compat_q2_rerelease_client_connect { bool accepted; char *userinfo; } qa_compat_q2_rerelease_client_connect;
qa_compat_q2_rerelease_client_connect qa_compat_q2_rerelease_guest_module_client_connect(
    qa_compat_q2_rerelease_guest_module *m, double slot, const char *userinfo, const char *social_id, bool is_bot);
void qa_compat_q2_rerelease_guest_module_client_begin(qa_compat_q2_rerelease_guest_module *m, double slot);
typedef void (*qa_compat_q2_rerelease_input_boundary_fn)(void *ctx, qa_guest_address address, void *run_ctx, void (*run)(void *, const qa_movement_body_shape *));
uint64_t qa_compat_q2_rerelease_guest_module_bind_input_movement(qa_compat_q2_rerelease_guest_module *m, void *ctx,
                                                                 qa_compat_q2_rerelease_input_boundary_fn boundary,
                                                                 void *active_ctx, bool (*active)(void *));
void qa_compat_q2_rerelease_guest_module_unbind_input_movement(qa_compat_q2_rerelease_guest_module *m, uint64_t handle);
void qa_compat_q2_rerelease_guest_module_client_think(qa_compat_q2_rerelease_guest_module *m, double slot,
                                                      qa_q2_rerelease_user_command command, const qa_equipment_movement *equipment);
void qa_compat_q2_rerelease_guest_module_client_disconnect(qa_compat_q2_rerelease_guest_module *m, double slot);
void qa_compat_q2_rerelease_guest_module_spawn_entities(qa_compat_q2_rerelease_guest_module *m, const char *map, const char *entities, const char *spawnpoint);
qa_guest_address qa_compat_q2_rerelease_guest_module_string(qa_compat_q2_rerelease_guest_module *m, const char *text);
qa_raw_entity_table qa_compat_q2_rerelease_guest_module_entities(qa_compat_q2_rerelease_guest_module *m);
void qa_compat_q2_rerelease_guest_module_draw_hud(qa_compat_q2_rerelease_guest_module *m, double split, qa_guest_address server_data,
                                                  const double viewport[4], const double safe_area[4], double scale, double player_number, qa_guest_address player_state);
#endif
