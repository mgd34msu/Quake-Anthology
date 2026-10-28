/* Port of src/compat/qvm/game-weapons.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_weapons.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/foundation/support.h"

typedef enum effect_kind {
  EFFECT_DAMAGE_FACTOR,
  EFFECT_TELEPORT,
  EFFECT_OBJECTIVES
} effect_kind;

typedef struct evaluation_frame {
  qa_actor_id actor;
  effect_kind kind;
  bool entered;
  bool has_result;
  double result;
} evaluation_frame;

struct qa_qvm_primary_weapons {
  qa_qvm_game *game; /* borrowed */
  qa_qvm_primary_weapon_profile profile;
  qa_qvm_primary_weapon_services services;
  qa_qvm_weapon_dispatcher_runtime dispatcher;
  qa_qvm_equipment_movement *equipment; /* owned, nullable */
  bool closed;
  double scratch;
  /* Removal ids in TS CleanupStack push order (equipment uses the
   * equipment flag below); unbound in reverse on close. */
  size_t removal_ids[4];
  size_t removal_count;
  bool has_equipment_removal;
  evaluation_frame *evaluations;
  size_t evaluation_count;
  size_t evaluation_capacity;
  void *eval_hooks[2]; /* owned eval_ctx allocations */
};

static bool is_integer(double value) { return isfinite(value) && trunc(value) == value; }
static bool is_safe_integer(double value) {
  return is_integer(value) && value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

/* TS memory.dataView(address, 4) int32/float32 access. */
static int32_t mem_i32(qa_qvm_primary_weapons *w, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(w->game->module.memory, address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static void mem_set_i32(qa_qvm_primary_weapons *w, int64_t address, int32_t value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(w->game->module.memory, address, 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, value, true);
}
static float mem_f32(qa_qvm_primary_weapons *w, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(w->game->module.memory, address, 4, &view);
  return qa_qvm_writable_view_get_float32(&view, 0, true);
}

static int32_t call_word(const qa_qvmy_function_call *call, int64_t byte_offset) {
  if (byte_offset < 0 || byte_offset > (int64_t)call->words_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = call->words + byte_offset;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}
static int32_t proceed(qa_qvmy_function_call *call) {
  return strcmp(call->execution, "asynchronous") == 0 ? call->proceed_async(call->call_ctx)
                                                      : call->proceed(call->call_ctx);
}

static bool live(qa_qvm_primary_weapons *w, qa_actor_id actor) {
  double slot = 0;
  if (w->closed || !w->services.slot(w->services.ctx, actor, &slot)) return false;
  qa_actor_id current;
  return w->services.actor(w->services.ctx, slot, &current) && qa_same_actor(current, actor);
}

static int64_t pointer_of(qa_qvm_primary_weapons *w, qa_actor_id actor) {
  double slot = 0;
  bool has_slot = w->services.slot(w->services.ctx, actor, &slot);
  qa_qvm_game_data_state located = qa_qvm_game_data_checkpoint(&w->game->data);
  if (!live(w, actor) || !has_slot)
    qa_range_error("Primary QVM weapon actor is no longer current");
  if (located.entity_stride != w->profile.entity_stride ||
      located.client_stride != w->profile.client_stride)
    qa_range_error("Primary QVM weapon records differ from their qualified layout");
  qa_qvm_writable_view bytes;
  qa_qvm_game_data_entity_bytes(&w->game->data, slot, &bytes);
  int32_t pointer =
      qa_qvm_writable_view_get_int32(&bytes, (int64_t)w->profile.client_pointer, true);
  qa_qvm_game_data_public_player_bytes(&w->game->data, slot, &bytes);
  if (pointer != located.clients_word + slot * located.client_stride)
    qa_range_error("Primary QVM actor does not own its original player state");
  return pointer;
}

static bool actor_of(qa_qvm_primary_weapons *w, const qa_qvm_weapon_actor *source,
                     qa_qvmy_function_call *call, qa_actor_id *out) {
  if (w->closed) return false;
  const qa_qvm_mod_input_pointer *reference = &source->pointer;
  int64_t pointer;
  if (reference->is_global) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(call->guest, (int64_t)reference->address, 4, &view);
    pointer = qa_qvm_writable_view_get_int32(&view, 0, true);
  } else {
    pointer = call_word(call, (int64_t)reference->index * 4);
  }
  for (size_t i = 0; i < reference->indirections.count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(call->guest, pointer + (int64_t)reference->indirections.values[i],
                            4, &view);
    pointer = qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  pointer += (int64_t)reference->offset;
  qa_qvm_game_data_state located = qa_qvm_game_data_checkpoint(&w->game->data);
  double slot = ((double)pointer - located.clients_word) / located.client_stride;
  if (strcmp(source->record, "client") != 0 || !is_integer(slot) || slot < 0)
    qa_range_error("Primary weapon dispatcher has no located source client");
  if (!w->services.actor(w->services.ctx, slot, out)) return false;
  if (pointer_of(w, *out) != pointer)
    qa_range_error("Primary weapon dispatcher has a different source actor");
  return true;
}

bool qa_qvm_primary_weapons_get_match(const qa_qvm_primary_weapons *weapons,
                                      qa_source_primary_match *out) {
  if (!weapons->profile.has_match) return false;
  *out = weapons->profile.match;
  return true;
}

double qa_qvm_primary_weapons_score(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  if (!weapons->profile.has_match)
    qa_range_error("Original QVM score storage has no declaration");
  return mem_i32(weapons, pointer_of(weapons, actor) + (int64_t)weapons->profile.match.score);
}
void qa_qvm_primary_weapons_set_score(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                      double score) {
  if (!weapons->profile.has_match)
    qa_range_error("Original QVM score storage has no declaration");
  mem_set_i32(weapons, pointer_of(weapons, actor) + (int64_t)weapons->profile.match.score,
              (int32_t)qa_source_score(score));
}

int32_t qa_qvm_primary_weapons_equipment_movement(qa_qvm_primary_weapons *weapons,
                                                  qa_qvmy_function_call *call, const char *kind,
                                                  qa_qvm_primary_weapon_run_fn run, void *run_ctx) {
  return weapons->equipment == NULL
             ? run(run_ctx)
             : qa_qvm_equipment_movement_movement(weapons->equipment, call, kind, run, run_ctx);
}

/* --- drop hook --- */
typedef struct drop_saved { int64_t address; int32_t value; } drop_saved;
typedef struct drop_ctx {
  qa_qvm_primary_weapons *weapons;
  qa_qvmy_function_call *call;
  qa_actor_id actor;
  int32_t entity;
  drop_saved *saved;
  size_t saved_count;
} drop_ctx;

static int32_t drop_view_get(drop_ctx *d, int64_t address) {
  /* TS new DataView(call.memory.buffer, byteOffset + address, 4). */
  if (address < 0 || address > (int64_t)d->call->memory_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = d->call->memory + address;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}
static void drop_view_set(drop_ctx *d, int64_t address, int32_t value) {
  if (address < 0 || address > (int64_t)d->call->memory_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint8_t *p = (uint8_t *)d->call->memory + address;
  uint32_t bits = (uint32_t)value;
  p[0] = (uint8_t)bits;
  p[1] = (uint8_t)(bits >> 8);
  p[2] = (uint8_t)(bits >> 16);
  p[3] = (uint8_t)(bits >> 24);
}
static void drop_restore(drop_ctx *d) {
  size_t count = d->saved_count;
  drop_saved *previous = d->saved;
  d->saved = NULL;
  d->saved_count = 0;
  if (live(d->weapons, d->actor))
    for (size_t i = 0; i < count; i++) drop_view_set(d, previous[i].address, previous[i].value);
  free(previous);
}
static const char *drop_region_run(void *ctx, void *control_ctx) {
  (void)control_ctx;
  drop_ctx *d = ctx;
  qa_qvm_primary_weapons *w = d->weapons;
  const qa_qvm_primary_weapon_drop *profile = &w->profile.drop;
  qa_qvm_primary_weapon_drop_result projection;
  if (w->services.drop(w->services.ctx, d->actor, &projection)) {
    bool known = false;
    for (size_t i = 0; i < w->profile.stage.selection.value_count; i++)
      if (w->profile.stage.selection.values[i].value == projection.weapon) known = true;
    if ((projection.weapon != 0 && !known) || !is_integer(projection.ammo) ||
        projection.ammo < -2147483648.0 || projection.ammo > 2147483647.0)
      qa_range_error("Selected death drop has no original weapon or int32 ammo counter");
    if (profile->ammo_is_inventory && !projection.has_ammo_words)
      qa_range_error("Original drop requires its declared inventory projection");
    size_t extra = profile->ammo_is_inventory ? projection.ammo_word_count : 1;
    size_t count = 1 + extra;
    drop_saved *writes = malloc(count * sizeof *writes);
    if (writes == NULL) qa_range_error("QVM memory allocation failed");
    writes[0].address = (int64_t)d->entity + (int64_t)profile->weapon;
    writes[0].value = (int32_t)projection.weapon;
    if (profile->ammo_is_inventory) {
      for (size_t i = 0; i < extra; i++) {
        writes[1 + i].address = (int64_t)projection.ammo_words[i].address;
        writes[1 + i].value = (int32_t)projection.ammo_words[i].value;
      }
    } else {
      writes[1].address = pointer_of(w, d->actor) + (int64_t)profile->ammo +
                          (int64_t)projection.weapon * 4;
      writes[1].value = (int32_t)projection.ammo;
    }
    d->saved = malloc(count * sizeof *d->saved);
    if (d->saved == NULL) qa_range_error("QVM memory allocation failed");
    d->saved_count = count;
    for (size_t i = 0; i < count; i++) {
      d->saved[i].address = writes[i].address;
      d->saved[i].value = drop_view_get(d, writes[i].address);
    }
    for (size_t i = 0; i < count; i++) drop_view_set(d, writes[i].address, writes[i].value);
    free(writes);
  }
  return "execute";
}
static void drop_region_completed(void *ctx, void *control_ctx) {
  (void)control_ctx;
  drop_restore(ctx);
}

static int32_t drop_call(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_primary_weapons *w = ctx;
  const qa_qvm_primary_weapon_drop *profile = &w->profile.drop;
  int32_t entity = call_word(call, (int64_t)profile->argument * 4);
  qa_actor_id actor;
  bool has_actor = w->services.actor(
      w->services.ctx, qa_qvm_game_data_number_from_pointer(&w->game->data, entity), &actor);
  if (!has_actor || !live(w, actor)) return proceed(call);
  drop_ctx d = {w, call, actor, entity, NULL, 0};
  qa_qvmy_region_binding binding = {(int64_t)profile->region.entry,
                                    (int64_t)profile->region.join, &d, drop_region_run,
                                    drop_region_completed};
  call->regions(call->call_ctx, &binding, 1);
  /* TS awaits async results through finally + call.effect(restore);
   * C proceeds synchronously, so restoring here is equivalent. */
  int32_t result = proceed(call);
  drop_restore(&d);
  return result;
}

/* --- give hook --- */
typedef struct give_branch_ctx {
  qa_qvm_primary_weapons *weapons;
  qa_actor_id actor;
  const char *category;
} give_branch_ctx;
static bool give_decide(void *ctx, bool original_taken, void *call_ctx,
                        qa_qvmy_cancel_fn cancel) {
  (void)call_ctx;
  (void)cancel;
  give_branch_ctx *g = ctx;
  if (live(g->weapons, g->actor)) g->weapons->services.give(g->weapons->services.ctx, g->actor, g->category);
  return original_taken;
}
typedef struct give_region_ctx {
  qa_qvm_primary_weapons *weapons;
  qa_actor_id actor;
  qa_qvm_memory *guest;
  qa_qvmy_cancellation_scope cancellation;
} give_region_ctx;
static const char *give_region_run(void *ctx, void *control_ctx) {
  (void)ctx;
  (void)control_ctx;
  return "execute";
}
static void give_region_completed(void *ctx, void *control_ctx) {
  give_region_ctx *g = ctx;
  qa_qvm_region_control *control = control_ctx;
  const qa_qvm_primary_weapon_give_named *named = &g->weapons->profile.give.named;
  if (control->local_word(control->ctx, named->item) == 0 && live(g->weapons, g->actor)) {
    char *name = qa_qvm_memory_read_string(g->guest,
                                           (int64_t)control->local_word(control->ctx, named->name));
    bool given = g->weapons->services.give_item(g->weapons->services.ctx, g->actor, name);
    free(name);
    if (given) {
      qa_qvm_cancellation_scope scope = {g->cancellation.token};
      control->cancel_function(control->ctx, scope);
    }
  }
}
static int32_t give_call(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_primary_weapons *w = ctx;
  const qa_qvm_primary_weapon_give *profile = &w->profile.give;
  int32_t pointer = call_word(call, (int64_t)profile->argument * 4);
  double slot = qa_qvm_game_data_number_from_pointer(&w->game->data, pointer);
  qa_actor_id actor;
  if (!w->services.actor(w->services.ctx, slot, &actor) || !live(w, actor)) return proceed(call);
  (void)pointer_of(w, actor);
  give_branch_ctx weapons_ctx = {w, actor, "weapons"};
  give_branch_ctx ammo_ctx = {w, actor, "ammo"};
  qa_qvmy_branch_binding branches[2] = {
      {(int64_t)profile->weapons, &weapons_ctx, give_decide},
      {(int64_t)profile->ammo, &ammo_ctx, give_decide}};
  call->branches(call->call_ctx, branches, 2);
  give_region_ctx region = {w, actor, call->guest, call->cancellation_scope(call->call_ctx)};
  qa_qvmy_region_binding binding = {(int64_t)profile->named.entry, (int64_t)profile->named.join,
                                    &region, give_region_run, give_region_completed};
  call->regions(call->call_ctx, &binding, 1);
  return proceed(call);
}

/* --- source-effect evaluation --- */
typedef struct eval_ctx {
  qa_qvm_primary_weapons *weapons;
  effect_kind kind;
  evaluation_frame *frame;
  qa_qvmy_cancellation_scope cancellation;
} eval_ctx;
static const char *damage_stop_run(void *ctx, void *control_ctx) {
  eval_ctx *e = ctx;
  qa_qvm_region_control *control = control_ctx;
  e->frame->result = mem_f32(e->weapons, (int64_t)e->weapons->profile.damage_factor.result);
  e->frame->has_result = true;
  qa_qvm_cancellation_scope scope = {e->cancellation.token};
  control->cancel_function(control->ctx, scope);
  /* TS returns control.cancelFunction(...) ("skip"-shaped); the C
   * cancel either longjmps or the run result is unused. */
  return "skip";
}
static int32_t evaluate_call(void *ctx, qa_qvmy_function_call *call) {
  eval_ctx *e = ctx;
  qa_qvm_primary_weapons *w = e->weapons;
  if (w->evaluation_count == 0) return proceed(call);
  evaluation_frame *frame = &w->evaluations[w->evaluation_count - 1];
  if ((frame->kind != e->kind && !(e->kind == EFFECT_TELEPORT && frame->kind == EFFECT_OBJECTIVES)) ||
      frame->entered)
    return proceed(call);
  frame->entered = true;
  double slot = 0;
  bool has_slot = w->services.slot(w->services.ctx, frame->actor, &slot);
  if (!has_slot || !live(w, frame->actor) ||
      qa_qvm_game_data_number_from_pointer(&w->game->data, call_word(call, 0)) != slot)
    qa_range_error("Primary QVM source effect lost its original actor");
  if (e->kind == EFFECT_TELEPORT)
    return call->evaluate_region(call->call_ctx,
                                 frame->kind == EFFECT_OBJECTIVES ? &w->profile.teleport.objectives
                                                                  : &w->profile.teleport.region,
                                 NULL, 0);
  e->frame = frame;
  e->cancellation = call->cancellation_scope(call->call_ctx);
  qa_qvmy_region_binding binding = {(int64_t)w->profile.damage_factor.stop.entry,
                                    (int64_t)w->profile.damage_factor.stop.join, e,
                                    damage_stop_run, NULL};
  call->regions(call->call_ctx, &binding, 1);
  return call->proceed(call->call_ctx);
}

static bool effect(qa_qvm_primary_weapons *w, qa_actor_id actor, effect_kind kind,
                   double *out) {
  double slot = 0;
  if (!w->services.slot(w->services.ctx, actor, &slot) || !live(w, actor))
    qa_range_error("Primary QVM source effect requires its original actor");
  (void)pointer_of(w, actor);
  if (w->evaluation_count == w->evaluation_capacity) {
    size_t capacity = w->evaluation_capacity == 0 ? 4 : w->evaluation_capacity * 2;
    evaluation_frame *grown = realloc(w->evaluations, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    w->evaluations = grown;
    w->evaluation_capacity = capacity;
  }
  evaluation_frame *frame = &w->evaluations[w->evaluation_count++];
  frame->actor = actor;
  frame->kind = kind;
  frame->entered = false;
  frame->has_result = false;
  frame->result = 0;
  double entry = kind == EFFECT_DAMAGE_FACTOR ? w->profile.damage_factor.entry
                                              : w->profile.teleport.entry;
  qa_qvm_game_data_state located = qa_qvm_game_data_checkpoint(&w->game->data);
  int32_t argument = (int32_t)(located.entities_word + slot * located.entity_stride);
  (void)qa_qvm_module_call(&w->game->module, &argument, 1, (int64_t)entry);
  bool has = frame->has_result;
  if (has) *out = frame->result;
  w->evaluation_count--;
  return has;
}

bool qa_qvm_primary_weapons_settled(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  return qa_qvm_weapon_dispatcher_runtime_settled(&weapons->dispatcher, actor);
}
qa_item_id qa_qvm_primary_weapons_active(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  if (!live(weapons, actor)) qa_range_error("Primary QVM weapon actor is no longer current");
  return qa_qvm_weapon_dispatcher_runtime_active(&weapons->dispatcher, actor);
}
void qa_qvm_primary_weapons_drop_objectives(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  double ignored = 0;
  (void)effect(weapons, actor, EFFECT_OBJECTIVES, &ignored);
}
void qa_qvm_primary_weapons_teleport(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  double ignored = 0;
  (void)effect(weapons, actor, EFFECT_TELEPORT, &ignored);
}

void qa_qvm_primary_weapons_spawn_point(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                        qa_vec3 *out_origin, qa_vec3 *out_angles) {
  double slot = 0;
  if (!weapons->services.slot(weapons->services.ctx, actor, &slot) || !live(weapons, actor))
    qa_range_error("Original QVM spawn selection requires its live player");
  (void)pointer_of(weapons, actor);
  qa_qvm_memory *memory = weapons->game->module.memory;
  int64_t scratch = (int64_t)weapons->scratch;
  uint8_t saved[36];
  memcpy(saved, memory->bytes + scratch, 36);
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory, scratch, 36, 0, &view);
  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&weapons->game->data, slot, &state, stats, persistent,
                                     powerups, ammo);
  qa_qvm_writable_view_set_float32(&view, 0, state.origin.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, state.origin.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, state.origin.z, true);
  int32_t words[3] = {(int32_t)scratch, (int32_t)(scratch + 12), (int32_t)(scratch + 24)};
  (void)qa_qvm_module_call(&weapons->game->module, words, 3,
                           (int64_t)weapons->profile.teleport.spawn);
  if (!live(weapons, actor)) {
    qa_qvm_memory_write_bytes(memory, scratch, saved, 36);
    qa_range_error("Original QVM spawn selection retired its player");
  }
  qa_vec3 origin = {qa_qvm_writable_view_get_float32(&view, 12, true),
                    qa_qvm_writable_view_get_float32(&view, 16, true),
                    qa_qvm_writable_view_get_float32(&view, 20, true)};
  qa_vec3 angles = {qa_qvm_writable_view_get_float32(&view, 24, true),
                    qa_qvm_writable_view_get_float32(&view, 28, true),
                    qa_qvm_writable_view_get_float32(&view, 32, true)};
  qa_qvm_memory_write_bytes(memory, scratch, saved, 36);
  *out_origin = origin;
  *out_angles = angles;
}

void qa_qvm_primary_weapons_teleport_state(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                           qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles,
                                           double hold_milliseconds) {
  double slot = 0;
  if (!weapons->services.slot(weapons->services.ctx, actor, &slot) || !live(weapons, actor))
    qa_range_error("Original QVM teleport requires its live player");
  (void)pointer_of(weapons, actor);
  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&weapons->game->data, slot, &state, stats, persistent,
                                     powerups, ammo);
  state.origin = origin;
  state.velocity = velocity;
  state.ground_entity_number = 1023;
  state.flags = (double)((int32_t)state.flags ^ 4);
  state.movement_flags = (double)((int32_t)state.movement_flags | 64);
  state.movement_time_milliseconds = hold_milliseconds;
  qa_qvm_game_data_write_player_state(&weapons->game->data, slot, &state);
  qa_qvm_memory *memory = weapons->game->module.memory;
  int64_t scratch = (int64_t)weapons->scratch;
  uint8_t saved[12];
  memcpy(saved, memory->bytes + scratch, 12);
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory, scratch, 12, 0, &view);
  qa_qvm_writable_view_set_float32(&view, 0, angles.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, angles.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, angles.z, true);
  qa_qvm_game_data_state located = qa_qvm_game_data_checkpoint(&weapons->game->data);
  int32_t words[2] = {(int32_t)(located.entities_word + slot * located.entity_stride),
                      (int32_t)scratch};
  (void)qa_qvm_module_call(&weapons->game->module, words, 2,
                           (int64_t)weapons->profile.teleport.view);
  qa_qvm_memory_write_bytes(memory, scratch, saved, 12);
}

double qa_qvm_primary_weapons_damage_factor(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  int64_t address = (int64_t)weapons->profile.damage_factor.result;
  int32_t previous = mem_i32(weapons, address);
  double result = 0;
  bool has = effect(weapons, actor, EFFECT_DAMAGE_FACTOR, &result);
  mem_set_i32(weapons, address, previous);
  if (!has || !isfinite(result) || result < 0)
    qa_range_error("Original QVM damage factor did not produce a finite result");
  return result;
}

qa_item_id qa_qvm_primary_weapons_equipment_context(qa_qvm_primary_weapons *weapons,
                                                    qa_provider_id provider) {
  qa_item_id item = qa_source_equipment_item(weapons->profile.equipment_contexts,
                                             weapons->profile.equipment_context_count, provider);
  if (item != NULL && strcmp(weapons->profile.stage.selection.field.record, "client") != 0)
    qa_range_error("Equipment %s requires an original client selection field", provider);
  if (item != NULL) {
    bool found = false;
    for (size_t i = 0; i < weapons->profile.stage.selection.value_count; i++)
      if (strcmp(weapons->profile.stage.selection.values[i].item, item) == 0) found = true;
    if (!found)
      qa_range_error("Equipment %s names an unavailable original cadence item %s", provider, item);
  }
  return item;
}

double qa_qvm_primary_weapons_equipment_delay(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                              qa_provider_id provider, double milliseconds) {
  qa_item_id item = qa_qvm_primary_weapons_equipment_context(weapons, provider);
  if (item == NULL) return qa_qvm_primary_weapons_weapon_delay(weapons, actor, milliseconds);
  const qa_qvm_weapon_selection *selection = &weapons->profile.stage.selection;
  const qa_qvm_weapon_selection_value *value = NULL;
  for (size_t i = 0; i < selection->value_count; i++)
    if (strcmp(selection->values[i].item, item) == 0) value = &selection->values[i];
  if (value == NULL || strcmp(selection->field.record, "client") != 0)
    qa_range_error("Equipment cadence has no original client selection field");
  int64_t pointer = pointer_of(weapons, actor);
  int64_t address = pointer + (int64_t)selection->field.offset;
  int32_t previous = mem_i32(weapons, address);
  mem_set_i32(weapons, address, (int32_t)value->value);
  double result = qa_qvm_primary_weapons_weapon_delay(weapons, actor, milliseconds);
  if (live(weapons, actor) && pointer_of(weapons, actor) == pointer)
    mem_set_i32(weapons, address, previous);
  return result;
}

double qa_qvm_primary_weapons_weapon_delay(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                           double milliseconds) {
  if (!is_integer(milliseconds) || milliseconds < 0 || milliseconds > 0x7fffffff)
    qa_range_error("Original QVM weapon delay requires its int32 input");
  int32_t movement = mem_i32(weapons, (int64_t)weapons->profile.delay_player.movement_global);
  if (movement <= 0) qa_range_error("Original QVM delay has no established Pmove context");
  int64_t address = movement + (int64_t)weapons->profile.delay_player.player_offset;
  int32_t previous = mem_i32(weapons, address);
  mem_set_i32(weapons, address, (int32_t)pointer_of(weapons, actor));
  int64_t input = (int64_t)milliseconds;
  int32_t result = qa_qvm_weapon_dispatcher_runtime_evaluate(&weapons->dispatcher, actor,
                                                             &weapons->profile.delay, &input, 1);
  mem_set_i32(weapons, address, previous);
  return result;
}

void qa_qvm_primary_weapons_attack_animation(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                             bool melee) {
  int32_t movement = mem_i32(weapons, (int64_t)weapons->profile.delay_player.movement_global);
  if (movement <= 0) qa_range_error("Original QVM animation has no established Pmove context");
  int64_t address = movement + (int64_t)weapons->profile.delay_player.player_offset;
  int32_t previous = mem_i32(weapons, address);
  mem_set_i32(weapons, address, (int32_t)pointer_of(weapons, actor));
  int32_t argument = (int32_t)(melee ? weapons->profile.torso_animation.melee
                                     : weapons->profile.torso_animation.attack);
  (void)qa_qvm_module_call(&weapons->game->module, &argument, 1,
                           (int64_t)weapons->profile.torso_animation.entry);
  mem_set_i32(weapons, address, previous);
}

double qa_qvm_primary_weapons_water_level(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  double slot = 0;
  if (!weapons->services.slot(weapons->services.ctx, actor, &slot) || !live(weapons, actor))
    qa_range_error("Original QVM water state lost its actor");
  int32_t movement = mem_i32(weapons, (int64_t)weapons->profile.delay_player.movement_global);
  if (movement > 0 &&
      mem_i32(weapons, movement + (int64_t)weapons->profile.delay_player.player_offset) ==
          (int32_t)pointer_of(weapons, actor))
    return mem_i32(weapons, movement + (int64_t)weapons->profile.water_level.movement_offset);
  qa_qvm_writable_view bytes;
  qa_qvm_game_data_entity_bytes(&weapons->game->data, slot, &bytes);
  return qa_qvm_writable_view_get_int32(&bytes, (int64_t)weapons->profile.water_level.entity_offset, true);
}

double qa_qvm_primary_weapons_max_health(qa_qvm_primary_weapons *weapons, qa_actor_id actor) {
  return mem_i32(weapons, pointer_of(weapons, actor) + (int64_t)weapons->profile.max_health);
}
void qa_qvm_primary_weapons_set_max_health(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                           double health) {
  if (!is_integer(health) || health <= 0 || health > 0x7fffffff)
    qa_range_error("Original QVM maximum health must be a positive int32");
  int64_t pointer = pointer_of(weapons, actor);
  mem_set_i32(weapons, pointer + (int64_t)weapons->profile.max_health, (int32_t)health);
  mem_set_i32(weapons, pointer + (int64_t)weapons->profile.persistent_max_health, (int32_t)health);
}

bool qa_qvm_primary_weapons_available(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                      bool attacking) {
  int64_t pointer = pointer_of(weapons, actor);
  const qa_qvm_primary_weapon_availability *policy = &weapons->profile.availability;
  if (mem_i32(weapons, pointer + (int64_t)policy->health) <= 0) return false;
  if (mem_i32(weapons, pointer + (int64_t)policy->team) == (int32_t)policy->spectator_team)
    return false;
  int32_t movement_type = mem_i32(weapons, pointer + (int64_t)policy->movement_type);
  for (size_t i = 0; i < policy->excluded_count; i++)
    if (policy->excluded[i] == movement_type) return false;
  if (attacking && (mem_i32(weapons, pointer + (int64_t)policy->flags) & (int32_t)policy->respawn_flag) != 0)
    return false;
  return true;
}

double qa_qvm_primary_weapons_powerup_until(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                            qa_qvm_primary_powerup powerup) {
  double offset = powerup == QA_QVM_PRIMARY_POWERUP_QUAD     ? weapons->profile.powerups.quad
                  : powerup == QA_QVM_PRIMARY_POWERUP_HASTE ? weapons->profile.powerups.haste
                                                            : weapons->profile.powerups.flight;
  return mem_i32(weapons, pointer_of(weapons, actor) + (int64_t)offset);
}

/* --- dispatcher operations --- */
static bool ops_actor(void *ctx, const qa_qvm_weapon_actor *source, qa_qvmy_function_call *call,
                      qa_actor_id *out) {
  return actor_of(ctx, source, call, out);
}
static int64_t ops_pointer(void *ctx, qa_actor_id actor, const char *record) {
  if (strcmp(record, "client") != 0)
    qa_range_error("Primary QVM weapon field is not a located client record");
  return pointer_of(ctx, actor);
}
static bool ops_live(void *ctx, qa_actor_id actor) { return live(ctx, actor); }
static bool ops_selected(void *ctx, qa_actor_id actor) {
  qa_qvm_primary_weapons *w = ctx;
  return w->services.selected(w->services.ctx, actor);
}
static qa_qvmy_cancellation_scope ops_cancellation(void *ctx, qa_actor_id actor,
                                                   qa_qvmy_function_call *call) {
  (void)ctx;
  (void)actor;
  return call->cancellation_scope(call->call_ctx);
}
static void ops_attempted(void *ctx, qa_actor_id actor, int32_t value) {
  qa_qvm_primary_weapons *w = ctx;
  w->services.attempted(w->services.ctx, actor, value);
}
static void ops_accepted(void *ctx, qa_actor_id actor, int32_t value) {
  qa_qvm_primary_weapons *w = ctx;
  w->services.accepted(w->services.ctx, actor, value);
}
static void ops_completed(void *ctx, qa_actor_id actor, bool reached) {
  qa_qvm_primary_weapons *w = ctx;
  w->services.completed(w->services.ctx, actor, reached);
}
static void prepare_cleanup(void *cleanup_ctx) {
  qa_qvm_equipment_weapon_restore *restore = cleanup_ctx;
  qa_qvm_equipment_weapon_restore_call(restore);
  free(restore);
}
static void ops_prepare(void *ctx, qa_actor_id actor, qa_qvmy_function_call *call,
                        void (**cleanup)(void *cleanup_ctx), void **cleanup_ctx) {
  *cleanup = NULL;
  *cleanup_ctx = NULL;
  qa_qvm_primary_weapons *w = ctx;
  if (w->equipment == NULL) return;
  qa_qvm_equipment_weapon_restore *restore = malloc(sizeof *restore);
  if (restore == NULL) qa_range_error("QVM memory allocation failed");
  if (!qa_qvm_equipment_movement_prepare_weapon(w->equipment, actor, call, restore)) {
    free(restore);
    return;
  }
  *cleanup = prepare_cleanup;
  *cleanup_ctx = restore;
}

/* --- equipment movement services bridge --- */
static bool equipment_actor(void *ctx, double slot, qa_actor_id *out) {
  qa_qvm_primary_weapons *w = ctx;
  return w->services.actor(w->services.ctx, slot, out);
}
static bool equipment_live(void *ctx, qa_actor_id actor) { return live(ctx, actor); }
static bool equipment_motion(void *ctx, qa_actor_id actor, qa_qvm_equipment_motion *out) {
  qa_qvm_primary_weapons *w = ctx;
  return w->services.equipment_movement(w->services.ctx, actor, out);
}

static qa_qvmy_region_instruction *region_instructions(const qa_qvm_image *image) {
  qa_qvmy_region_instruction *list =
      malloc((image->instruction_count == 0 ? 1 : image->instruction_count) * sizeof *list);
  if (list == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < image->instruction_count; i++) {
    list[i].opcode = (int32_t)image->instructions[i].opcode;
    list[i].operand_width = image->instructions[i].operand_width;
    list[i].operand = image->instructions[i].operand;
    list[i].byte_offset = image->instructions[i].byte_offset;
  }
  return list;
}

static size_t bind(qa_qvm_primary_weapons *w, qa_module_identity identity, double entry,
                   qa_qvmy_function_hook_fn hook, void *hook_ctx) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = identity;
  reference.instruction_index = entry;
  size_t id = qa_qvm_module_bind_invocation(&w->game->module, &reference, hook, hook_ctx);
  w->removal_ids[w->removal_count++] = id;
  return id;
}

qa_qvm_primary_weapons *qa_qvm_primary_weapons_create(qa_qvm_game *game,
                                                      const qa_resolved_qvm_artifact *artifact,
                                                      qa_qvm_primary_weapon_profile profile,
                                                      qa_qvm_primary_weapon_services services) {
  const qa_module_identity *identity = &game->module.profile.module;
  if (strcmp(identity->id, profile.module.id) != 0 ||
      strcmp(identity->digest, profile.module.digest) != 0 ||
      strcmp(identity->revision, profile.module.revision) != 0 ||
      strcmp(identity->artifact_path, profile.module.artifact_path) != 0 ||
      strcmp(artifact->module.digest, identity->digest) != 0 ||
      strcmp(qa_qvm_module_get_abi_profile(&game->module), profile.abi_profile) != 0)
    qa_range_error("Primary QVM weapon profile belongs to another executable");
  const qa_qvm_image *image = &artifact->image;
  double torso_entry = profile.torso_animation.entry;
  if (!is_integer(torso_entry) || torso_entry < 0 || torso_entry >= (double)image->instruction_count ||
      image->instructions[(size_t)torso_entry].opcode != QA_QVM_OP_ENTER)
    qa_range_error("Primary QVM torso animation entry is not an original function");
  if (profile.has_match && (!is_safe_integer(profile.match.score) || profile.match.score < 0 ||
                            (int64_t)profile.match.score % 4 != 0 ||
                            profile.match.score + 4 > profile.client_stride))
    qa_range_error("Original QVM score exceeds its declared client record");
  qa_qvmy_region_instruction *list = region_instructions(image);
  qa_qvm_validate_weapon_dispatcher(&profile.stage, list, image->instruction_count);
  (void)qa_qvm_qualify_region(list, image->instruction_count, (int64_t)profile.damage_factor.entry,
                              (int64_t)profile.damage_factor.stop.entry,
                              (int64_t)profile.damage_factor.stop.join);
  (void)qa_qvm_qualify_region_evaluation(list, image->instruction_count,
                                         (int64_t)profile.stage.dispatcher.entry, &profile.delay,
                                         "source");
  (void)qa_qvm_qualify_region_evaluation(list, image->instruction_count,
                                         (int64_t)profile.teleport.entry, &profile.teleport.region,
                                         "source");
  (void)qa_qvm_qualify_region_evaluation(list, image->instruction_count,
                                         (int64_t)profile.teleport.entry, &profile.teleport.objectives,
                                         "source");
  for (size_t i = 0; i < 2; i++) {
    double entry = i == 0 ? profile.teleport.spawn : profile.teleport.view;
    if (!is_integer(entry) || entry < 0 || entry >= (double)image->instruction_count ||
        image->instructions[(size_t)entry].opcode != QA_QVM_OP_ENTER)
      qa_range_error("Original QVM spawn selector is not a function");
  }
  qa_qvm_primary_weapons *w = calloc(1, sizeof *w);
  if (w == NULL) {
    free(list);
    return NULL;
  }
  w->game = game;
  w->profile = profile;
  w->services = services;
  w->scratch = ceil((double)(image->data_length + image->literal_length + image->bss_length) / 16) * 16;
  if (w->scratch + 36 > (double)image->allocated_data_length - 65536)
    qa_range_error("Source player services require scratch outside source data and stack");
  (void)qa_qvm_qualify_region(list, image->instruction_count, (int64_t)profile.give.entry,
                              (int64_t)profile.give.named.entry, (int64_t)profile.give.named.join);
  (void)qa_qvm_qualify_region(list, image->instruction_count, (int64_t)profile.drop.entry,
                              (int64_t)profile.drop.region.entry, (int64_t)profile.drop.region.join);
  free(list);
  for (size_t i = 0; i < 2; i++) {
    double pc = i == 0 ? profile.give.weapons : profile.give.ammo;
    if (pc <= profile.give.entry || !is_integer(pc) || pc < 0 ||
        pc >= (double)image->instruction_count ||
        image->instructions[(size_t)pc].opcode < QA_QVM_OP_EQ ||
        image->instructions[(size_t)pc].opcode > QA_QVM_OP_GEF)
      qa_range_error("Primary QVM give grant lacks its original completion decision");
  }
  qa_qvm_weapon_dispatcher_ops ops;
  memset(&ops, 0, sizeof ops);
  ops.module = &game->module;
  ops.ctx = w;
  ops.actor = ops_actor;
  ops.pointer = ops_pointer;
  ops.live = ops_live;
  ops.selected = ops_selected;
  ops.cancellation = ops_cancellation;
  ops.attempted = ops_attempted;
  ops.accepted = ops_accepted;
  ops.completed = ops_completed;
  ops.prepare = ops_prepare;
  qa_qvm_weapon_dispatcher_runtime_init(&w->dispatcher, &w->profile.stage, &ops);
  if (services.has_equipment_movement) {
    qa_qvm_equipment_movement_services equipment_services = {w, equipment_actor, equipment_live,
                                                             equipment_motion};
    w->equipment = qa_qvm_equipment_movement_create(game, artifact, profile.equipment_movement,
                                                    equipment_services);
    if (w->equipment == NULL) {
      qa_qvm_weapon_dispatcher_runtime_destroy(&w->dispatcher);
      free(w);
      return NULL;
    }
    w->has_equipment_removal = true;
  }
  (void)bind(w, *identity, profile.give.entry, give_call, w);
  (void)bind(w, *identity, profile.drop.entry, drop_call, w);
  /* Per-kind hook contexts outlive the bindings (freed on destroy). */
  eval_ctx *damage = calloc(1, sizeof *damage);
  eval_ctx *teleport = calloc(1, sizeof *teleport);
  if (damage == NULL || teleport == NULL) qa_range_error("QVM memory allocation failed");
  damage->weapons = w;
  damage->kind = EFFECT_DAMAGE_FACTOR;
  teleport->weapons = w;
  teleport->kind = EFFECT_TELEPORT;
  w->eval_hooks[0] = damage;
  w->eval_hooks[1] = teleport;
  (void)bind(w, *identity, profile.damage_factor.entry, evaluate_call, damage);
  (void)bind(w, *identity, profile.teleport.entry, evaluate_call, teleport);
  return w;
}

void qa_qvm_primary_weapons_close(qa_qvm_primary_weapons *weapons) {
  weapons->closed = true;
  /* TS removals.runReverse() (bindings reversed, then equipment),
   * then dispatcher.close(). */
  while (weapons->removal_count > 0)
    qa_qvm_module_unbind(&weapons->game->module, weapons->removal_ids[--weapons->removal_count]);
  if (weapons->has_equipment_removal) {
    qa_qvm_equipment_movement_close(weapons->equipment);
    weapons->has_equipment_removal = false;
  }
  qa_qvm_weapon_dispatcher_runtime_close(&weapons->dispatcher);
}

void qa_qvm_primary_weapons_destroy(qa_qvm_primary_weapons *weapons) {
  if (weapons == NULL) return;
  qa_qvm_primary_weapons_close(weapons);
  qa_qvm_weapon_dispatcher_runtime_destroy(&weapons->dispatcher);
  qa_qvm_equipment_movement_destroy(weapons->equipment);
  free(weapons->eval_hooks[0]);
  free(weapons->eval_hooks[1]);
  free(weapons->evaluations);
  free(weapons);
}
