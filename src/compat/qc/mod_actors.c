/* Port of src/compat/qc/mod-actors.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_actors.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/world/actors/callbacks.h"
typedef qa_compat_qc_mod_actors A;
static bool owned_eq(qa_owned_actor a, qa_owned_actor b) { return qa_same_owned_actor(a, b); }
static int64_t owned_slot(A *a, qa_owned_actor actor, bool *found) {
  for (size_t i = 0; i < a->owned_count; i++) if (owned_eq(a->owned_actors[i], actor)) { *found = true; return a->owned_slots[i]; }
  *found = false; return 0;
}
static void owned_set(A *a, qa_owned_actor actor, int64_t slot) {
  bool f; (void)owned_slot(a, actor, &f);
  if (f) { for (size_t i = 0; i < a->owned_count; i++) if (owned_eq(a->owned_actors[i], actor)) a->owned_slots[i] = slot; return; }
  if (a->owned_count == a->owned_capacity) { size_t c = a->owned_capacity ? a->owned_capacity*2 : 8;
    a->owned_actors = realloc(a->owned_actors, c*sizeof *a->owned_actors); a->owned_slots = realloc(a->owned_slots, c*sizeof *a->owned_slots);
    if (!a->owned_actors || !a->owned_slots) { qa_range_error("mod actor allocation failed"); } a->owned_capacity = c; }
  a->owned_actors[a->owned_count] = actor; a->owned_slots[a->owned_count++] = slot;
}
static void owned_delete(A *a, qa_owned_actor actor) {
  for (size_t i = 0; i < a->owned_count; i++) if (owned_eq(a->owned_actors[i], actor)) {
    memmove(&a->owned_actors[i], &a->owned_actors[i+1], (a->owned_count-i-1)*sizeof *a->owned_actors);
    memmove(&a->owned_slots[i], &a->owned_slots[i+1], (a->owned_count-i-1)*sizeof *a->owned_slots); a->owned_count--; return; }
}
static void think_call(void *ctx, qa_owned_actor actor, qa_frame_context frame) {
  A *a = ctx;
  if (!a->options.services->callbacks) qa_range_error("Scheduled mod actors require shared callbacks");
  qa_actor_callback_table_think(a->options.services->callbacks, actor, frame);
}
static bool sched_source_slot(void *ctx, qa_actor_id actor, int64_t *out) {
  A *a = ctx; qa_actor_source_result r = qa_session_actor_registry_source_of(a->options.services->actors, actor);
  if (!r.has_value) { return false; } *out = r.slot; return true;
}
static qa_think_callback_result sched_resolve(void *ctx, qa_provider_id provider, qa_callback_id callback) {
  (void)provider; A *a = ctx; qa_think_callback_result r; memset(&r, 0, sizeof r);
  if (strcmp(callback, "quakec:mod-think") == 0) { r.has_value = true; r.value = (qa_think_callback){a, think_call}; }
  return r;
}
static double w_count(void *ctx){ A *a=ctx; return a->storage_base.count(a->storage_base.ctx); }
static void w_set_count(void *ctx, double c){ A *a=ctx; a->storage_base.set_count(a->storage_base.ctx,c); }
static qa_source_slot_state w_read(void *ctx, int64_t s){ A *a=ctx; return a->storage_base.read(a->storage_base.ctx,s); }
static void w_clear(void *ctx, int64_t s, qa_source_time n){ A *a=ctx; a->storage_base.clear_freed(a->storage_base.ctx,s,n); }
static bool w_can(void *ctx, int64_t s){ A *a=ctx; return a->storage_base.can_free(a->storage_base.ctx,s); }
static void storage_initialize(void *ctx, int64_t slot, qa_owned_actor actor) {
  A *a = ctx;
  a->storage_base.initialize(a->storage_base.ctx, slot, actor);
  a->options.remember(a->options.ctx, actor.id, slot);
  qa_shared_body_table_bind(a->options.services->bodies, actor, a->options.body(a->options.ctx, slot));
  qa_compat_qc_mod_actors_admit(a, actor, slot);
}
static qa_source_time slots_now(void *ctx) {
  A *a = ctx; qa_source_time now = a->options.services->time(a->options.services->ctx);
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, now.kind == QA_SOURCE_TIME_SECONDS ? now.value : now.value / 1000};
}
static void slots_unlink(void *ctx, qa_owned_actor actor) { A *a = ctx; qa_shared_body_table_unlink(a->options.services->bodies, actor); }
static void slots_exhausted(void *ctx, int64_t slot) { (void)ctx; (void)slot; qa_range_error("Gameplay mod source edicts exhausted"); }
static void remove_fn(void *ctx, qa_compat_qc_machine *m) {
  A *a = ctx;
  if (m->entities != a->options.machine->entities) { qa_compat_qc_machine_fail(m, "actor builtin belongs to another machine"); }
  int64_t slot = qa_compat_qc_entity_memory_slot(m->entities, qa_compat_qc_machine_arg_int(m, 0));
  qa_owned_actor at;
  if (!qa_source_actor_slots_at(a->slots, slot, &at)) { qa_compat_qc_machine_fail(m, "Mod remove requires its own actor; foreign source removal needs its owner continuation"); }
  /* inner remove is bindings remove (stored before wrap) */
  a->bindings_state.slots = a->slots;
  qa_owned_actor actor = at;
  if (!qa_source_actor_slots_free(a->slots, actor)) {
    a->slots_options.storage.clear_freed(a->slots_options.storage.ctx, slot, a->slots_options.now(a->slots_options.ctx));
  }
}
static void on_release(void *ctx, qa_owned_actor actor) {
  A *a = ctx; bool found; int64_t slot = owned_slot(a, actor, &found);
  if (!found) return;
  a->storage_base.clear_freed(a->storage_base.ctx, slot, a->slots_options.now(a->slots_options.ctx));
  owned_delete(a, actor); qa_frame_scheduler_cancel(a->scheduler, actor);
  a->options.retired(a->options.ctx, actor);
}
bool qa_compat_qc_mod_actors_init(A *a, qa_compat_qc_mod_actor_options o) {
  memset(a, 0, sizeof *a); a->options = o; a->self_ref = a;
  qa_compat_qc_machine *machine = o.machine; qa_mod_host_services *services = o.services;
  qa_clock_profile clock; memset(&clock, 0, sizeof clock);
  if (machine->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD) { clock.kind = QA_CLOCK_Q1_QUAKEWORLD; clock.maximum_command_milliseconds = 100; }
  else { clock.kind = QA_CLOCK_Q1_NETQUAKE; clock.minimum_frame_seconds = 0; clock.maximum_frame_seconds = 0.1; }
  a->clock_entry = (qa_scheduler_clock){o.provider, clock};
  qa_actor_registry actors_contract = qa_session_actor_registry_as_contract(services->actors);
  qa_scheduler_options so; memset(&so, 0, sizeof so);
  so.actors = actors_contract;
  so.ordering = (qa_frame_ordering){QA_FRAME_ORDERING_NATIVE, clock, NULL, 0};
  so.clocks = &a->clock_entry; so.clock_count = 1; so.ctx = a;
  so.source_slot = sched_source_slot; so.resolve = sched_resolve;
  a->scheduler = qa_frame_scheduler_create(so);
  if (!a->scheduler) return false;
  a->storage_base = qa_compat_qc_create_source_slot_storage(machine->program, machine->entities,
    (qa_compat_qc_edict_metadata_layout){0, machine->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? 100 : 92}, &a->storage_state);
  /* storage_base ctx is &storage_state; wrapped storage ctx is A */
  qa_source_slot_storage wrapped = {a, w_count, w_set_count, w_read, storage_initialize, w_clear, w_can};
  /* other wrapped fns need ctx=storage_state: keep base struct but dispatch via A wrappers below */
  a->slots_options = (qa_source_actor_slots_options){o.provider, (double)machine->entities->capacity,
    qa_quake_edict_lifetime((double)(o.first_dynamic_slot ? o.first_dynamic_slot : 1), NULL),
    wrapped, a, slots_now, slots_unlink, slots_exhausted};
  a->slots = qa_source_actor_slots_create(services->actors, a->slots_options);
  if (!a->slots) { qa_frame_scheduler_destroy(a->scheduler); return false; }
  qa_compat_qc_actor_bindings bindings = qa_compat_qc_create_actor_bindings(machine->entities, services->actors, a->slots, a->slots_options, &a->bindings_state);
  a->spawn = bindings.host_entries[0].fn;
  a->remove = (qa_compat_qc_builtin){a, remove_fn};
  a->release_id = qa_session_actor_registry_on_release(services->actors, on_release, a);
  return true;
}
void qa_compat_qc_mod_actors_destroy(A *a) {
  if (a->scheduler) qa_frame_scheduler_destroy(a->scheduler);
  qa_source_actor_slots_destroy(a->slots);
  free(a->owned_actors); free(a->owned_slots);
  for (size_t i = 0; i < a->cb_count; i++) free(a->cb_storage[i]);
  free(a->cb_storage); memset(a, 0, sizeof *a);
}
typedef struct qa_compat_qc_mod_actor_cb CbCtx;
static CbCtx *cb_for(A *a, qa_owned_actor actor, int64_t slot) {
  for (size_t i = 0; i < a->cb_count; i++) if (owned_eq(a->cb_storage[i]->actor, actor)) { a->cb_storage[i]->slot = slot; return a->cb_storage[i]; }
  if (a->cb_count == a->cb_capacity) { size_t c = a->cb_capacity ? a->cb_capacity*2 : 8; a->cb_storage = realloc(a->cb_storage, c*sizeof *a->cb_storage); if (!a->cb_storage) qa_range_error("mod actor allocation failed"); a->cb_capacity = c; }
  CbCtx *cell = malloc(sizeof *cell); if (!cell) qa_range_error("mod actor allocation failed");
  *cell = (CbCtx){a, actor, slot}; a->cb_storage[a->cb_count++] = cell; return cell;
}
static void cb_touch(void *ctx, qa_touch_contact contact) { CbCtx *c = ctx; c->a->options.touch(c->a->options.ctx, c->actor, contact.other); }
static void cb_use(void *ctx, qa_owned_actor actor, const qa_actor_id *other, const qa_actor_id *activator) { CbCtx *c = ctx; c->a->options.use(c->a->options.ctx, actor, other, activator); }
static void cb_pain(void *ctx, qa_pain_reaction r) { CbCtx *c = ctx; c->a->options.pain(c->a->options.ctx, r); }
static void cb_die(void *ctx, qa_death_reaction r) { CbCtx *c = ctx; c->a->options.die(c->a->options.ctx, r); }
static void cb_think(void *ctx, qa_owned_actor actor, qa_frame_context frame) {
  CbCtx *c = ctx; A *a = c->a;
  if (a->options.think < 0 || a->options.nextthink < 0) qa_range_error("Mod think needs explicit think/nextthink field mappings");
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(a->options.machine->entities, c->slot);
  int32_t function_index = qa_compat_qc_words_int(fields, a->options.think);
  qa_compat_qc_words_set_float(fields, a->options.nextthink, 0);
  if (function_index != 0) a->options.invoke(a->options.ctx, actor, function_index, frame);
}
void qa_compat_qc_mod_actors_admit(A *a, qa_owned_actor actor, int64_t slot) {
  owned_set(a, actor, slot);
  a->options.admitted(a->options.ctx, actor, slot);
  if (a->options.services->callbacks) {
    CbCtx *c = cb_for(a, actor, slot);
    qa_actor_callbacks callbacks = {c, cb_think, cb_touch, cb_use, cb_pain, cb_die};
    qa_actor_callback_table_bind(a->options.services->callbacks, actor, callbacks);
  }
}
void qa_compat_qc_mod_actors_schedule(A *a, qa_owned_actor actor) {
  bool found; int64_t slot = owned_slot(a, actor, &found);
  if (!found) qa_range_error("Mod scheduling requires its own source actor");
  if (a->options.nextthink < 0) qa_range_error("Mod scheduling requires a nextthink binding");
  double due = qa_compat_qc_words_float(qa_compat_qc_entity_memory_at(a->options.machine->entities, slot), a->options.nextthink);
  if (due <= 0) { qa_frame_scheduler_cancel(a->scheduler, actor); return; }
  qa_think_timing timing; memset(&timing, 0, sizeof timing);
  timing.due = (qa_source_time){QA_SOURCE_TIME_SECONDS, due}; timing.boundary = QA_THINK_DURING_PHYSICS;
  timing.order = (qa_invocation_order){a->options.provider, actor.id, 0};
  qa_frame_scheduler_schedule(a->scheduler, actor, "quakec:mod-think", timing);
}
typedef struct { A *a; qa_actor_id actor; qa_frame_context frame; } ThinkRun;
static void think_run(void *ctx) { ThinkRun *t = ctx; qa_frame_scheduler_run(t->a->scheduler, t->actor, t->frame, QA_THINK_DURING_PHYSICS); }
void qa_compat_qc_mod_actors_advance(A *a, qa_frame_context frame) {
  double tv = frame.time.kind == QA_SOURCE_TIME_SECONDS ? frame.time.value : frame.time.value / 1000;
  double ev = frame.elapsed.kind == QA_SOURCE_TIME_SECONDS ? frame.elapsed.value : frame.elapsed.value / 1000;
  qa_frame_context source = frame;
  source.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, tv - ev};
  source.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, ev};
  int64_t first_dynamic = a->options.first_dynamic_slot ? a->options.first_dynamic_slot : 1;
  for (int64_t slot = 1; slot < a->options.machine->entities->count; slot++) {
    if (slot < first_dynamic && a->options.client_frame && a->options.client_frame(a->options.ctx, slot, source)) continue;
    qa_owned_actor actor; if (!qa_source_actor_slots_at(a->slots, slot, &actor)) continue;
    ThinkRun run = {a, actor.id, source};
    a->options.step(a->options.ctx, actor, source, &run, think_run);
  }
}
void qa_compat_qc_mod_actors_restored(A *a) {
  size_t count = 0; const qa_owned_actor *owned = qa_session_actor_registry_owned_by(a->options.services->actors, a->options.provider, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_source_result src = qa_session_actor_registry_source_of(a->options.services->actors, owned[i].id);
    if (!src.has_value || strcmp(src.provider, a->options.provider) != 0) qa_range_error("Saved mod actor lost its source slot");
    qa_shared_body_table_rebind(a->options.services->bodies, owned[i], a->options.body(a->options.ctx, src.slot));
    qa_compat_qc_mod_actors_admit(a, owned[i], src.slot);
    if (a->options.nextthink >= 0) qa_compat_qc_mod_actors_schedule(a, owned[i]);
  }
}
void qa_compat_qc_mod_actors_close(A *a) {
  if (a->closed) return;
  a->closed = true;
  qa_session_actor_registry_off_release(a->options.services->actors, a->release_id);
  qa_frame_scheduler_close(a->scheduler);
  /* TS collects release errors into AggregateError; release aborts on error in C. */
  for (size_t i = 0; i < a->owned_count; i++) if (qa_session_actor_registry_is_live(a->options.services->actors, a->owned_actors[i].id)) qa_session_actor_registry_release(a->options.services->actors, a->owned_actors[i]);
  a->owned_count = 0;
}
