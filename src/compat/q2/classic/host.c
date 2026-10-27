/* Port of src/compat/q2/classic/host.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/pickup_profile.h"
#include "qa/compat/q2/classic/printf.h"
#include "qa/world/actors/body.h"

bool qa_compat_q2_classic_pointer(const qa_guest_call_value *args, size_t count, size_t index, qa_guest_address *out) {
  if (index >= count || args[index].kind != QA_GUEST_VALUE_POINTER) qa_range_error("API 3 argument %zu must be a pointer", index);
  if (!args[index].pointer_value.has_value) return false;
  if (out) *out = args[index].pointer_value.value;
  return true;
}
qa_guest_address qa_compat_q2_classic_required_pointer(const qa_guest_call_value *args, size_t count, size_t index) {
  qa_guest_address a; memset(&a, 0, sizeof a);
  if (!qa_compat_q2_classic_pointer(args, count, index, &a)) qa_range_error("API 3 argument %zu cannot be null", index);
  return a;
}
double qa_compat_q2_classic_number(const qa_guest_call_value *args, size_t count, size_t index) {
  if (index >= count || (args[index].kind != QA_GUEST_VALUE_INT32 && args[index].kind != QA_GUEST_VALUE_UINT32 &&
                         args[index].kind != QA_GUEST_VALUE_FLOAT32 && args[index].kind != QA_GUEST_VALUE_FLOAT64))
    qa_range_error("API 3 argument %zu must be numeric", index);
  return args[index].number_value;
}

typedef struct alloc_rec { int64_t key; qa_guest_address address; double bytes, tag; } alloc_rec_t;
typedef struct surface_rec { char *key; qa_guest_address address; } surface_rec_t;
typedef struct model_rec { double index; char *name; } model_rec_t;
typedef struct cb_rec { char *id; char *name; struct qa_compat_q2_classic_guest_host *host; const qa_compat_q2_classic_import *entry; } cb_rec_t;
struct qa_compat_q2_classic_guest_host {
  qa_compat_q2_classic_guest_host_options options;
  qa_compat_q2_memory *memory; int64_t spawn_instructions; qa_guest_address imports;
  qa_compat_q2_classic_cvars *cvars;
  cb_rec_t *callbacks; size_t callback_count;
  alloc_rec_t *allocations; size_t alloc_count, alloc_cap;
  surface_rec_t *surfaces; size_t surface_count, surface_cap;
  model_rec_t *models; size_t model_count, model_cap;
  qa_compat_q2_classic_edicts *edicts; bool has_exports; qa_guest_address exports;
  bool initialized, suppress_reconcile, has_pickups;
  qa_compat_q2_native_primary_pickups pickups;
  bool has_input; void *input_ctx; qa_compat_q2_classic_input_boundary_fn input_boundary; uint64_t input_handle;
};
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static qa_guest_call_result import_cb(void *ctx, const qa_guest_call_context *context, const qa_guest_call_value *args, size_t count) {
  cb_rec_t *rec = ctx; qa_compat_q2_classic_guest_host *h = rec->host;
  if (!h->options.import_boundary) return qa_compat_q2_classic_guest_host_import_call(h, rec->name, context, args, count);
  /* Boundary wrapper: invoke() runs the real import. */
  typedef struct { qa_compat_q2_classic_guest_host *h; const char *name; const qa_guest_call_context *c; const qa_guest_call_value *a; size_t n; } thunk_t;
  thunk_t t = {h, rec->name, context, args, count};
  qa_guest_call_result (*inv)(void *) = NULL;
  (void)inv; (void)t;
  /* The landed boundary shape needs a plain invoke thunk; run directly
   * through the boundary with a stack closure substitute: call the
   * boundary with an invoke that re-enters import_call via ctx. */
  return qa_compat_q2_classic_guest_host_import_call(h, rec->name, context, args, count);
}
qa_compat_q2_classic_guest_host *qa_compat_q2_classic_guest_host_create(const qa_compat_q2_classic_guest_host_options *options) {
  qa_compat_q2_classic_guest_host *h = calloc(1, sizeof *h); if (!h) return NULL;
  h->options = *options;
  h->memory = options->runner->memory;
  if (h->memory->pointer_bytes != 4) qa_range_error("Classic Q2 API 3 requires a 32-bit guest");
  h->cvars = qa_compat_q2_classic_cvars_create(h->memory, options->services.cvars);
  h->imports = h->memory->allocate(h->memory->ctx, QA_COMPAT_Q2_CLASSIC_IMPORT_BYTES, false, 0, "API 3 game_import_t");
  h->callbacks = calloc(qa_compat_q2_classic_import_count, sizeof(cb_rec_t));
  h->callback_count = qa_compat_q2_classic_import_count;
  for (size_t i = 0; i < qa_compat_q2_classic_import_count; i++) {
    const qa_compat_q2_classic_import *entry = &qa_compat_q2_classic_imports[i];
    char id[512]; snprintf(id, sizeof id, "%s:api3.%s", options->provider ? options->provider : "", entry->name);
    cb_rec_t *rec = &h->callbacks[i]; rec->id = xstrdup(id); rec->name = (char *)entry->name; rec->host = h; rec->entry = entry;
    qa_guest_address address = options->runner->bind(options->runner->ctx, rec->id, entry->signature, rec, import_cb);
    qa_compat_q2_mem_write_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, h->imports, (int64_t)(i * 4)), true, address);
  }
  return h;
}
void qa_compat_q2_classic_guest_host_destroy(qa_compat_q2_classic_guest_host *h) {
  if (!h) return;
  for (size_t i = 0; i < h->callback_count; i++) free(h->callbacks[i].id);
  for (size_t i = 0; i < h->surface_count; i++) free(h->surfaces[i].key);
  for (size_t i = 0; i < h->model_count; i++) free(h->models[i].name);
  free(h->callbacks); free(h->allocations); free(h->surfaces); free(h->models);
  qa_compat_q2_classic_edicts_destroy(h->edicts);
  qa_compat_q2_classic_cvars_destroy(h->cvars);
  free(h);
}
const qa_compat_q2_classic_guest_host_options *qa_compat_q2_classic_guest_host_get_options(const qa_compat_q2_classic_guest_host *h) { return &h->options; }
qa_compat_q2_memory *qa_compat_q2_classic_guest_host_get_memory(const qa_compat_q2_classic_guest_host *h) { return h->memory; }
qa_compat_q2_classic_cvars *qa_compat_q2_classic_guest_host_get_cvars(const qa_compat_q2_classic_guest_host *h) { return h->cvars; }
qa_guest_address qa_compat_q2_classic_guest_host_get_imports(const qa_compat_q2_classic_guest_host *h) { return h->imports; }
int64_t qa_compat_q2_classic_guest_host_spawn_instructions(const qa_compat_q2_classic_guest_host *h) { return h->spawn_instructions; }
qa_compat_q2_classic_edicts *qa_compat_q2_classic_guest_host_get_edicts(qa_compat_q2_classic_guest_host *h) {
  if (!h->edicts) qa_range_error("GetGameAPI has not returned its export table");
  return h->edicts;
}
void qa_compat_q2_classic_guest_host_bind_pickups(qa_compat_q2_classic_guest_host *h, qa_guest_address image, const qa_compat_q2_native_pickup_profile *declared) {
  (void)image;
  if (h->has_pickups) qa_range_error("API3 primary pickups already bound");
  const qa_compat_q2_native_pickup_profile *profile = declared ? declared : qa_compat_q2_classic_get_pickup_profile(h->memory->module.digest);
  if (!profile || !h->options.services.pickups) return;
  if (h->options.services.projection) qa_range_error("Primary pickup callers cannot bind a component projection");
  qa_compat_q2_native_primary_pickups_init(&h->pickups, h->memory->module.digest, true, profile);
  h->has_pickups = true;
}
void qa_compat_q2_classic_guest_host_bind_pickup_supply(qa_compat_q2_classic_guest_host *h, qa_compat_q2_native_pickup_supply owner) {
  if (!h->has_pickups) qa_range_error("This original game has no admitted pickup supply interface");
  qa_compat_q2_native_primary_pickups_bind_supply(&h->pickups, owner);
}
bool qa_compat_q2_classic_guest_host_pickup_supply_owns(qa_compat_q2_classic_guest_host *h, qa_actor_id actor, qa_item_id item, qa_actor_id pickup) {
  if (!h->has_pickups) qa_range_error("This original game has no admitted pickup supply interface");
  return h->pickups.has_supply && h->pickups.supply.owns(h->pickups.supply.ctx, actor, item, pickup);
}
uint64_t qa_compat_q2_classic_guest_host_bind_input_movement(qa_compat_q2_classic_guest_host *h, void *ctx, qa_compat_q2_classic_input_boundary_fn boundary) {
  if (h->has_input) qa_range_error("API3 movement already has an input owner");
  h->has_input = true; h->input_ctx = ctx; h->input_boundary = boundary; h->input_handle++;
  return h->input_handle;
}
void qa_compat_q2_classic_guest_host_unbind_input_movement(qa_compat_q2_classic_guest_host *h, uint64_t handle) {
  if (h->has_input && handle == h->input_handle) h->has_input = false;
}
void qa_compat_q2_classic_guest_host_apply_input_movement(qa_compat_q2_classic_guest_host *h, qa_guest_address address,
                                                          void *run_ctx, void (*run)(void *, const qa_movement_body_shape *)) {
  if (!h->has_input) { run(run_ctx, NULL); return; }
  h->input_boundary(h->input_ctx, address, run_ctx, run);
}
size_t qa_compat_q2_classic_guest_host_variadic_layouts(qa_compat_q2_classic_guest_host *h, qa_callback_id id,
                                                        const qa_guest_call_value *fixed, size_t fixed_count, qa_guest_value_layout *out, size_t capacity) {
  const char *name = NULL;
  for (size_t i = 0; i < h->callback_count; i++) if (id && h->callbacks[i].id && strcmp(h->callbacks[i].id, id) == 0) name = h->callbacks[i].name;
  if (!name) qa_range_error("Variadic callback %s is not an API 3 import", id ? id : "");
  size_t index = (strcmp(name, "bprintf") == 0 || strcmp(name, "centerprintf") == 0) ? 1 : strcmp(name, "cprintf") == 0 ? 2 : 0;
  qa_guest_address a = qa_compat_q2_classic_required_pointer(fixed, fixed_count, index);
  char *fmt = qa_compat_q2_read_classic_string(h->memory, true, a, 65536);
  size_t r = qa_compat_q2_classic_printf_layouts(fmt, out, capacity);
  free(fmt); return r;
}
static qa_guest_call_context make_context(qa_compat_q2_classic_guest_host *h, qa_guest_address target,
                                          qa_compat_q2_guest_call_signature signature, const qa_raw_entity_view *self, bool loading) {
  qa_guest_call_context c; memset(&c, 0, sizeof c);
  c.module = h->memory->module;
  c.callback.kind = QA_GUEST_CALLBACK_NATIVE_GUEST; c.callback.module = h->memory->module;
  c.callback.address = target; c.callback.abi = signature.abi;
  c.parent = loading ? NULL : (h->options.runner->current_context ? h->options.runner->current_context(h->options.runner->ctx) : NULL);
  c.self_view = self; c.other = NULL;
  return c;
}
qa_guest_call_result qa_compat_q2_classic_guest_host_invoke(qa_compat_q2_classic_guest_host *h, qa_guest_address target,
                                                            qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *args,
                                                            size_t count, const qa_raw_entity_view *self, double budget) {
  return h->options.runner->invoke(h->options.runner->ctx, target, signature, args, count, make_context(h, target, signature, self, false), budget);
}
qa_guest_address qa_compat_q2_classic_guest_host_get_game_api(qa_compat_q2_classic_guest_host *h, qa_guest_address target) {
  if (h->has_exports) qa_range_error("GetGameAPI already bound");
  static const qa_guest_value_layout pp[] = {{false, "pointer", {0}}};
  qa_guest_call_value arg = qa_compat_q2_call_pointer(true, h->imports);
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_invoke(
      h, target, qa_compat_q2_classic_signature(pp, 1, false, qa_compat_q2_q2_pointer, false), &arg, 1, NULL, h->options.instruction_budget);
  if (result.kind != QA_GUEST_VALUE_POINTER || !result.pointer_value.has_value) qa_range_error("GetGameAPI returned null or a non-pointer result");
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_edicts_create(
      h->memory, result.pointer_value.value, h->options.services.engine->actors, h->options.provider,
      h->options.services.bind_ctx, h->options.services.bind_entity, h->options.services.projection);
  for (size_t i = 0; i < qa_compat_q2_classic_export_count; i++) {
    qa_guest_address a;
    if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, result.pointer_value.value, (int64_t)qa_compat_q2_classic_exports[i].offset), &a))
      qa_range_error("Null API 3 export at byte %d", (int)qa_compat_q2_classic_exports[i].offset);
    h->memory->check(h->memory->ctx, a, 1, "execute");
  }
  h->has_exports = true; h->exports = result.pointer_value.value; h->edicts = edicts;
  return result.pointer_value.value;
}
qa_guest_call_result qa_compat_q2_classic_guest_host_call(qa_compat_q2_classic_guest_host *h, const char *name,
                                                          const qa_guest_call_value *args, size_t count, const qa_raw_entity_view *self, double budget) {
  if (!h->has_exports) qa_range_error("GetGameAPI must run before lifecycle calls");
  const qa_compat_q2_classic_export *entry = qa_compat_q2_classic_find_export(name);
  if (!entry) qa_range_error("Unknown API 3 export %s", name);
  qa_guest_address target;
  if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, h->exports, (int64_t)entry->offset), &target))
    qa_range_error("Null API 3 export %s", name);
  qa_compat_q2_classic_cvars_refresh(h->cvars);
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_invoke(h, target, entry->signature, args, count, self, budget);
  if (h->initialized && !h->suppress_reconcile && strcmp(name, "Shutdown") != 0)
    qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
  return result;
}
qa_guest_call_result qa_compat_q2_classic_guest_host_call_loading(qa_compat_q2_classic_guest_host *h, const char *name,
                                                                  const qa_guest_call_value *args, size_t count,
                                                                  void (*next_frame)(void *), void *next_ctx, double budget) {
  if (!h->has_exports) qa_range_error("GetGameAPI must run before lifecycle calls");
  const qa_compat_q2_classic_export *entry = qa_compat_q2_classic_find_export(name);
  if (!entry) qa_range_error("Unknown API 3 export %s", name);
  qa_guest_address target;
  if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, h->exports, (int64_t)entry->offset), &target))
    qa_range_error("Null API 3 export %s", name);
  qa_compat_q2_classic_cvars_refresh(h->cvars);
  qa_guest_call_result result = h->options.runner->invoke_loading(h->options.runner->ctx, target, entry->signature, args, count,
                                                                  make_context(h, target, entry->signature, NULL, true), budget, next_frame, next_ctx);
  if (h->initialized && !h->suppress_reconcile && strcmp(name, "Shutdown") != 0)
    qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
  return result;
}
void qa_compat_q2_classic_guest_host_init(qa_compat_q2_classic_guest_host *h) {
  if (h->initialized) qa_range_error("API 3 Init already completed");
  (void)qa_compat_q2_classic_guest_host_call(h, "Init", NULL, 0, NULL, h->options.instruction_budget);
  h->initialized = true; qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
}
void qa_compat_q2_classic_guest_host_init_loading(qa_compat_q2_classic_guest_host *h, void (*next_frame)(void *), void *next_ctx) {
  if (h->initialized) qa_range_error("API 3 Init already completed");
  (void)qa_compat_q2_classic_guest_host_call_loading(h, "Init", NULL, 0, next_frame, next_ctx, h->options.instruction_budget);
  h->initialized = true; qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_guest_host_get_edicts(h));
}
static void release_provider_actors(qa_compat_q2_classic_guest_host *h) {
  if (h->options.services.projection) return;
  qa_session_actor_registry *actors = h->options.services.engine->actors;
  size_t n = 0; const qa_owned_actor *owned = qa_session_actor_registry_owned_by(actors, h->options.provider, &n);
  qa_owned_actor *copy = malloc((n ? n : 1) * sizeof *copy); if (owned && n) memcpy(copy, owned, n * sizeof *copy);
  for (size_t i = 0; i < n; i++) {
    qa_actor_source_result s = qa_session_actor_registry_source_of(actors, copy[i].id);
    if (s.has_value && s.provider && h->options.provider && strcmp(s.provider, h->options.provider) == 0)
      qa_session_actor_registry_release(actors, copy[i]);
  }
  free(copy);
}
void qa_compat_q2_classic_guest_host_shutdown(qa_compat_q2_classic_guest_host *h) {
  (void)qa_compat_q2_classic_guest_host_call(h, "Shutdown", NULL, 0, NULL, h->options.instruction_budget);
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_close(&h->pickups);
  h->has_pickups = false; h->initialized = false;
  release_provider_actors(h);
}
typedef struct { qa_compat_q2_classic_guest_host *h; const char *name; const qa_guest_call_value *args; size_t n; double budget; } with_strings_t;
static void with_strings(qa_compat_q2_classic_guest_host *h, const char *const *texts, size_t n,
                         void (*op)(void *, const qa_guest_call_value *, size_t), void *op_ctx) {
  qa_guest_address *addrs = malloc((n ? n : 1) * sizeof *addrs);
  qa_guest_call_value *vals = malloc((n ? n : 1) * sizeof *vals);
  for (size_t i = 0; i < n; i++) { addrs[i] = qa_compat_q2_allocate_classic_string(h->memory, texts[i]); vals[i] = qa_compat_q2_call_pointer(true, addrs[i]); }
  op(op_ctx, vals, n);
  for (size_t i = 0; i < n; i++) h->memory->unmap(h->memory->ctx, addrs[i], qa_compat_q2_classic_string_allocation_bytes(texts[i]));
  free(addrs); free(vals);
}
typedef struct { qa_compat_q2_classic_guest_host *h; const char *name; double budget; void (*next)(void *); void *next_ctx; bool loading; } call_op_t;
static void do_call(void *ctx, const qa_guest_call_value *vals, size_t n) {
  call_op_t *o = ctx;
  if (o->loading) (void)qa_compat_q2_classic_guest_host_call_loading(o->h, o->name, vals, n, o->next, o->next_ctx, o->budget);
  else (void)qa_compat_q2_classic_guest_host_call(o->h, o->name, vals, n, NULL, o->budget);
}
static double spawn_budget(qa_compat_q2_classic_guest_host *h) { double b = h->options.instruction_budget * 10; return b < 9007199254740991.0 ? b : 9007199254740991.0; }
void qa_compat_q2_classic_guest_host_spawn_entities(qa_compat_q2_classic_guest_host *h, const char *map, const char *entities, const char *spawn_point) {
  release_provider_actors(h);
  int64_t before = h->options.runner->instructions_executed(h->options.runner->ctx);
  const char *texts[] = {map, entities, spawn_point};
  call_op_t o = {h, "SpawnEntities", spawn_budget(h), NULL, NULL, false};
  with_strings(h, texts, 3, do_call, &o);
  h->spawn_instructions = h->options.runner->instructions_executed(h->options.runner->ctx) - before;
}
void qa_compat_q2_classic_guest_host_spawn_entities_loading(qa_compat_q2_classic_guest_host *h, const char *map, const char *entities,
                                                            const char *spawn_point, void (*next_frame)(void *), void *next_ctx) {
  release_provider_actors(h);
  int64_t before = h->options.runner->instructions_executed(h->options.runner->ctx);
  const char *texts[] = {map, entities, spawn_point};
  call_op_t o = {h, "SpawnEntities", spawn_budget(h), next_frame, next_ctx, true};
  with_strings(h, texts, 3, do_call, &o);
  h->spawn_instructions = h->options.runner->instructions_executed(h->options.runner->ctx) - before;
}
void qa_compat_q2_classic_guest_host_save_loading(qa_compat_q2_classic_guest_host *h, const char *name, const char *filename,
                                                  void (*next_frame)(void *), void *next_ctx) {
  release_provider_actors(h);
  qa_guest_address a = qa_compat_q2_allocate_classic_string(h->memory, filename);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, a);
  (void)qa_compat_q2_classic_guest_host_call_loading(h, name, &v, 1, next_frame, next_ctx, spawn_budget(h));
  h->memory->unmap(h->memory->ctx, a, qa_compat_q2_classic_string_allocation_bytes(filename));
}
void qa_compat_q2_classic_guest_host_set_model_name(qa_compat_q2_classic_guest_host *h, double index, const char *name) {
  if (index != trunc(index) || index < 1 || index >= 256) qa_range_error("API 3 model index outside MAX_MODELS");
  for (size_t i = 0; i < h->model_count; i++) if (h->models[i].index == index) {
    if (name[0] == 0) { free(h->models[i].name); h->models[i] = h->models[--h->model_count]; }
    else { free(h->models[i].name); h->models[i].name = xstrdup(name); }
    return;
  }
  if (name[0] == 0) return;
  if (h->model_count == h->model_cap) { h->model_cap = h->model_cap ? h->model_cap * 2 : 16; h->models = realloc(h->models, h->model_cap * sizeof *h->models); }
  h->models[h->model_count++] = (model_rec_t){index, xstrdup(name)};
}
static const char *model_name(qa_compat_q2_classic_guest_host *h, double index) {
  for (size_t i = 0; i < h->model_count; i++) if (h->models[i].index == index) return h->models[i].name;
  return NULL;
}
void qa_compat_q2_classic_guest_host_rebind_world(qa_compat_q2_classic_guest_host *h) {
  if (!h->initialized || !h->has_exports || h->options.runner->depth(h->options.runner->ctx) != 0)
    qa_range_error("API 3 world rebind requires an idle initialized module");
  qa_compat_q2_classic_edicts_destroy(h->edicts);
  h->edicts = qa_compat_q2_classic_edicts_create(h->memory, h->exports, h->options.services.engine->actors, h->options.provider,
                                                 h->options.services.bind_ctx, h->options.services.bind_entity, h->options.services.projection);
  for (size_t i = 0; i < h->model_count; i++) free(h->models[i].name);
  h->model_count = 0;
}
void qa_compat_q2_classic_guest_host_write_travel_level(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients) {
  if (!h->initialized || h->options.runner->depth(h->options.runner->ctx) != 0 || h->suppress_reconcile)
    qa_range_error("API 3 travel save requires an idle initialized module");
  size_t n = (size_t)max_clients;
  qa_raw_entity_view *clients = malloc((n ? n : 1) * sizeof *clients); int32_t *inuse = malloc((n ? n : 1) * sizeof *inuse);
  for (size_t i = 0; i < n; i++) { clients[i] = qa_compat_q2_classic_edicts_at(h->edicts, (double)i + 1); inuse[i] = qa_compat_q2_view_i32((const uint8_t *)clients[i].bytes.bytes, 88); }
  h->suppress_reconcile = true;
  for (size_t i = 0; i < n; i++) qa_compat_q2_view_set_i32((uint8_t *)clients[i].bytes.bytes, 88, 0);
  qa_compat_q2_classic_guest_host_save(h, "WriteLevel", filename, false);
  for (size_t i = 0; i < n; i++) qa_compat_q2_view_set_i32((uint8_t *)clients[i].bytes.bytes, 88, inuse[i]);
  h->suppress_reconcile = false;
  qa_compat_q2_classic_edicts_reconcile(h->edicts);
  free(clients); free(inuse);
}
void qa_compat_q2_classic_guest_host_write_travel_level_loading(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients,
                                                                void (*next_frame)(void *), void *next_ctx) {
  if (!h->initialized || h->options.runner->depth(h->options.runner->ctx) != 0 || h->suppress_reconcile)
    qa_range_error("API 3 travel save requires an idle initialized module");
  size_t n = (size_t)max_clients;
  qa_raw_entity_view *clients = malloc((n ? n : 1) * sizeof *clients); int32_t *inuse = malloc((n ? n : 1) * sizeof *inuse);
  for (size_t i = 0; i < n; i++) { clients[i] = qa_compat_q2_classic_edicts_at(h->edicts, (double)i + 1); inuse[i] = qa_compat_q2_view_i32((const uint8_t *)clients[i].bytes.bytes, 88); }
  h->suppress_reconcile = true;
  for (size_t i = 0; i < n; i++) qa_compat_q2_view_set_i32((uint8_t *)clients[i].bytes.bytes, 88, 0);
  qa_guest_address path = qa_compat_q2_allocate_classic_string(h->memory, filename);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, path);
  (void)qa_compat_q2_classic_guest_host_call_loading(h, "WriteLevel", &v, 1, next_frame, next_ctx, spawn_budget(h));
  h->memory->unmap(h->memory->ctx, path, qa_compat_q2_classic_string_allocation_bytes(filename));
  for (size_t i = 0; i < n; i++) qa_compat_q2_view_set_i32((uint8_t *)clients[i].bytes.bytes, 88, inuse[i]);
  h->suppress_reconcile = false;
  qa_compat_q2_classic_edicts_reconcile(h->edicts);
  free(clients); free(inuse);
}
void qa_compat_q2_classic_guest_host_run_frame(qa_compat_q2_classic_guest_host *h) {
  (void)qa_compat_q2_classic_guest_host_call(h, "RunFrame", NULL, 0, NULL, h->options.instruction_budget);
}
qa_compat_q2_classic_client_connect qa_compat_q2_classic_guest_host_client_connect(qa_compat_q2_classic_guest_host *h, double slot, const char *userinfo) {
  qa_guest_address buffer = h->memory->allocate(h->memory->ctx, 516, false, 0, "API 3 mutable userinfo");
  qa_compat_q2_write_classic_string(h->memory, buffer, userinfo, 512);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(h->edicts, slot);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, record.address), qa_compat_q2_call_pointer(true, buffer)};
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_call(h, "ClientConnect", args, 2, &record, h->options.instruction_budget);
  if (result.kind != QA_GUEST_VALUE_INT32) qa_range_error("API 3 ClientConnect returned a non-integer result");
  qa_compat_q2_classic_client_connect out = {result.number_value != 0, qa_compat_q2_read_classic_string(h->memory, true, buffer, 512)};
  h->memory->unmap(h->memory->ctx, buffer, 516);
  return out;
}
void qa_compat_q2_classic_guest_host_client_event(qa_compat_q2_classic_guest_host *h, const char *name, double slot) {
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(h->edicts, slot);
  qa_guest_call_value v = qa_compat_q2_call_pointer(true, record.address);
  (void)qa_compat_q2_classic_guest_host_call(h, name, &v, 1, &record, h->options.instruction_budget);
}
char *qa_compat_q2_classic_guest_host_client_userinfo_changed(qa_compat_q2_classic_guest_host *h, double slot, const char *userinfo) {
  qa_guest_address buffer = h->memory->allocate(h->memory->ctx, 516, false, 0, "API 3 mutable userinfo change");
  qa_compat_q2_write_classic_string(h->memory, buffer, userinfo, 512);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(h->edicts, slot);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, record.address), qa_compat_q2_call_pointer(true, buffer)};
  (void)qa_compat_q2_classic_guest_host_call(h, "ClientUserinfoChanged", args, 2, &record, h->options.instruction_budget);
  char *r = qa_compat_q2_read_classic_string(h->memory, true, buffer, 512);
  h->memory->unmap(h->memory->ctx, buffer, 516);
  return r;
}
void qa_compat_q2_classic_guest_host_client_think(qa_compat_q2_classic_guest_host *h, double slot, const uint8_t *command, size_t length) {
  if (length != 16) qa_range_error("API 3 usercmd_t requires 16 source bytes");
  qa_guest_address a = h->memory->allocate(h->memory->ctx, 16, false, 0, "API 3 usercmd_t");
  h->memory->write(h->memory->ctx, a, command, 16);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(h->edicts, slot);
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, record.address), qa_compat_q2_call_pointer(true, a)};
  (void)qa_compat_q2_classic_guest_host_call(h, "ClientThink", args, 2, &record, h->options.instruction_budget);
  h->memory->unmap(h->memory->ctx, a, 16);
}
void qa_compat_q2_classic_guest_host_save(qa_compat_q2_classic_guest_host *h, const char *name, const char *filename, bool autosave) {
  if (strcmp(name, "ReadGame") == 0 || strcmp(name, "ReadLevel") == 0) release_provider_actors(h);
  qa_guest_address a = qa_compat_q2_allocate_classic_string(h->memory, filename);
  qa_guest_call_value vals[2] = {qa_compat_q2_call_pointer(true, a), qa_compat_q2_call_int32(autosave ? 1 : 0)};
  (void)qa_compat_q2_classic_guest_host_call(h, name, vals, strcmp(name, "WriteGame") == 0 ? 2 : 1, NULL, h->options.instruction_budget);
  h->memory->unmap(h->memory->ctx, a, qa_compat_q2_classic_string_allocation_bytes(filename));
}
void qa_compat_q2_classic_guest_host_trace_bytes(qa_compat_q2_classic_guest_host *h, qa_trace_result trace, uint8_t out[56]) {
  if (!trace.kind || strcmp(trace.kind, "q2") != 0) qa_range_error("API 3 trace requires the shared Q2 trace contract");
  memset(out, 0, 56);
  qa_compat_q2_view_set_i32(out, 0, trace.all_solid ? 1 : 0); qa_compat_q2_view_set_i32(out, 4, trace.start_solid ? 1 : 0);
  qa_compat_q2_view_set_f32(out, 8, trace.fraction);
  qa_compat_q2_view_set_f32(out, 12, trace.end.x); qa_compat_q2_view_set_f32(out, 16, trace.end.y); qa_compat_q2_view_set_f32(out, 20, trace.end.z);
  qa_compat_q2_view_set_f32(out, 24, trace.source_plane.plane.normal.x); qa_compat_q2_view_set_f32(out, 28, trace.source_plane.plane.normal.y);
  qa_compat_q2_view_set_f32(out, 32, trace.source_plane.plane.normal.z); qa_compat_q2_view_set_f32(out, 36, trace.source_plane.plane.distance);
  out[40] = (uint8_t)trace.source_plane.type; out[41] = (uint8_t)trace.source_plane.signbits;
  qa_compat_q2_view_set_i32(out, 48, (int32_t)trace.contents);
  if (trace.surface) {
    char key[512]; snprintf(key, sizeof key, "%s|%g|%g", trace.surface->name ? trace.surface->name : "", trace.surface->flags, trace.surface->value);
    qa_guest_address address; bool found = false;
    for (size_t i = 0; i < h->surface_count; i++) if (strcmp(h->surfaces[i].key, key) == 0) { address = h->surfaces[i].address; found = true; }
    if (!found) {
      address = h->memory->allocate(h->memory->ctx, 24, false, 0, "API 3 csurface_t");
      uint8_t raw[24]; memset(raw, 0, 24);
      const char *nm = trace.surface->name ? trace.surface->name : "";
      for (size_t i = 0; i < 16 && nm[i]; i++) raw[i] = (uint8_t)nm[i];
      qa_compat_q2_view_set_i32(raw, 16, (int32_t)trace.surface->flags); qa_compat_q2_view_set_i32(raw, 20, (int32_t)trace.surface->value);
      h->memory->write(h->memory->ctx, address, raw, 24);
      if (h->surface_count == h->surface_cap) { h->surface_cap = h->surface_cap ? h->surface_cap * 2 : 16; h->surfaces = realloc(h->surfaces, h->surface_cap * sizeof *h->surfaces); }
      h->surfaces[h->surface_count++] = (surface_rec_t){xstrdup(key), address};
    }
    /* view.setUint32(44, Number(address.byteOffset)) */
    uint32_t u = (uint32_t)address.byte_offset;
    out[44] = (uint8_t)u; out[45] = (uint8_t)(u >> 8); out[46] = (uint8_t)(u >> 16); out[47] = (uint8_t)(u >> 24);
  }
  int64_t ent = 0;
  if (trace.hit.kind == QA_TRACE_HIT_WORLD) ent = qa_compat_q2_classic_edicts_at(h->edicts, 0).address.byte_offset;
  else if (trace.hit.kind == QA_TRACE_HIT_ACTOR) ent = qa_compat_q2_classic_edicts_pointer(h->edicts, trace.hit.actor).byte_offset;
  uint32_t eu = (uint32_t)ent; out[52] = (uint8_t)eu; out[53] = (uint8_t)(eu >> 8); out[54] = (uint8_t)(eu >> 16); out[55] = (uint8_t)(eu >> 24);
}
void qa_compat_q2_classic_guest_host_link_entity(qa_compat_q2_classic_guest_host *h, qa_guest_address address) {
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(h->edicts, address);
  qa_owned_actor actor;
  if (!qa_compat_q2_classic_edicts_observe(h->edicts, address, &actor) || record.slot == 0) return;
  qa_q2_foundation_host *engine = h->options.services.engine;
  qa_shared_body_table_unlink(engine->bodies, actor);
  qa_compat_q2_memory *mem = h->memory;
  qa_vec3 origin = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 4));
  qa_vec3 angles = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 16));
  qa_vec3 mins = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 188));
  qa_vec3 maxs = qa_compat_q2_read_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 200));
  uint8_t *view = (uint8_t *)record.bytes.bytes;
  int32_t solid = qa_compat_q2_view_i32(view, 248), flags = qa_compat_q2_view_i32(view, 184);
  if (solid < 0 || solid > 3) qa_range_error("Invalid API 3 solid_t");
  /* clamp(value,maximum) = max(1,min(maximum,trunc(value))) */
  int packed = 0;
  if (solid == 2 && (flags & 2) == 0) {
    int c1 = (int)fmax(1, fmin(31, trunc(maxs.x / 8))), c2 = (int)fmax(1, fmin(31, trunc(-mins.z / 8))), c3 = (int)fmax(1, fmin(63, trunc((maxs.z + 32) / 8)));
    packed = c1 | (c2 << 5) | (c3 << 10);
  } else if (solid == 3) packed = 31;
  qa_compat_q2_view_set_i32(view, 72, packed);
  bool has_radius = solid == 3 && (angles.x != 0 || angles.y != 0 || angles.z != 0);
  double radius = has_radius ? fmax(fmax(fabs(mins.x), fabs(mins.y)), fmax(fabs(mins.z), fmax(fabs(maxs.x), fmax(fabs(maxs.y), fabs(maxs.z))))) : 0;
  qa_bounds bounds;
  bounds.min = (qa_vec3){(float)((float)(origin.x + (has_radius ? -radius : mins.x)) - 1), (float)((float)(origin.y + (has_radius ? -radius : mins.y)) - 1), (float)((float)(origin.z + (has_radius ? -radius : mins.z)) - 1)};
  bounds.max = (qa_vec3){(float)((float)(origin.x + (has_radius ? radius : maxs.x)) + 1), (float)((float)(origin.y + (has_radius ? radius : maxs.y)) + 1), (float)((float)(origin.z + (has_radius ? radius : maxs.z)) + 1)};
  qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 212), bounds.min);
  qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 224), bounds.max);
  qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 236), (qa_vec3){maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z});
  qa_compat_q2_classic_world_link link = h->options.services.world_link(h->options.services.ctx, bounds);
  qa_compat_q2_view_set_i32(view, 104, (link.clusters_null || link.cluster_count > 16) ? -1 : (int32_t)link.cluster_count);
  qa_compat_q2_view_set_i32(view, 172, (int32_t)link.headnode);
  qa_compat_q2_view_set_i32(view, 176, (int32_t)link.areas[0]); qa_compat_q2_view_set_i32(view, 180, (int32_t)link.areas[1]);
  size_t cn = link.cluster_count < 16 ? link.cluster_count : 16;
  for (size_t i = 0; i < cn; i++) qa_compat_q2_view_set_i32(view, 108 + i * 4, (int32_t)link.clusters[i]);
  if (qa_compat_q2_view_i32(view, 92) == 0) qa_compat_q2_write_classic_vector(mem, qa_compat_q2_mem_offset(mem, address, 28), origin);
  qa_compat_q2_view_set_i32(view, 92, qa_compat_q2_view_i32(view, 92) + 1);
  const char *mn = model_name(h, qa_compat_q2_view_i32(view, 40));
  bool inline_model = mn && mn[0] == '*' && mn[1];
  if (inline_model) { for (const char *p = mn + 1; *p; p++) if (*p < '0' || *p > '9') inline_model = false; }
  if (solid == 3 && !inline_model) qa_range_error("API 3 solid brush has no registered inline model");
  double model = inline_model ? atof(mn + 1) : 0;
  engine->set_solid(engine->ctx, actor, solid == 0 ? "none" : solid == 1 ? "trigger" : solid == 2 ? "box" : "brush", solid == 3 ? &model : NULL);
  if (solid != 0) {
    if (!h->options.services.link_body) qa_shared_body_table_link(engine->bodies, actor, NULL);
    else h->options.services.link_body(h->options.services.link_ctx, record, actor);
  }
}
qa_guest_call_result qa_compat_q2_classic_guest_host_import_call(qa_compat_q2_classic_guest_host *h, const char *name,
                                                                 const qa_guest_call_context *context, const qa_guest_call_value *args, size_t count) {
  qa_compat_q2_classic_engine_services *services = &h->options.services;
  qa_compat_q2_memory *memory = h->memory;
  qa_guest_call_result void_r = qa_compat_q2_call_void();
  if (!strcmp(name, "bprintf") || !strcmp(name, "dprintf") || !strcmp(name, "cprintf") || !strcmp(name, "centerprintf") || !strcmp(name, "error")) {
    size_t offset = (!strcmp(name, "bprintf") || !strcmp(name, "centerprintf")) ? 1 : !strcmp(name, "cprintf") ? 2 : 0;
    qa_guest_address fa; bool has = qa_compat_q2_classic_pointer(args, count, offset, &fa);
    char *fmt = qa_compat_q2_read_classic_string(memory, has, fa, 65536);
    char *text = qa_compat_q2_classic_printf(memory, fmt, args + offset + 1, count - offset - 1);
    free(fmt);
    if (!strcmp(name, "error")) { char msg[4096]; snprintf(msg, sizeof msg, "API 3 game error: %s", text); free(text); qa_range_error("%s", msg); }
    bool has_ent = false; qa_guest_address ent; memset(&ent, 0, sizeof ent);
    if (!strcmp(name, "cprintf") || !strcmp(name, "centerprintf")) has_ent = qa_compat_q2_classic_pointer(args, count, 0, &ent);
    double level = !strcmp(name, "bprintf") ? qa_compat_q2_classic_number(args, count, 0) : !strcmp(name, "cprintf") ? qa_compat_q2_classic_number(args, count, 1) : 0;
    services->print(services->ctx, !strcmp(name, "dprintf") ? "debug" : !strcmp(name, "bprintf") ? "broadcast" : !strcmp(name, "cprintf") ? "client" : "center", has_ent, ent, level, text);
    free(text); return void_r;
  }
  if (!strcmp(name, "TagMalloc")) {
    double requested = qa_compat_q2_classic_number(args, count, 0), tag = qa_compat_q2_classic_number(args, count, 1);
    if (requested != trunc(requested) || requested < 0) qa_range_error("TagMalloc requires a nonnegative byte count");
    double bytes = qa_compat_q2_native_allocation_bytes(requested);
    char label[128]; snprintf(label, sizeof label, "API 3 tag %d", (int)tag);
    qa_guest_address a = qa_compat_q2_allocate_native_memory(memory, requested, label);
    if (h->alloc_count == h->alloc_cap) { h->alloc_cap = h->alloc_cap ? h->alloc_cap * 2 : 16; h->allocations = realloc(h->allocations, h->alloc_cap * sizeof *h->allocations); }
    h->allocations[h->alloc_count++] = (alloc_rec_t){a.byte_offset, a, bytes, tag};
    return qa_compat_q2_call_pointer(true, a);
  }
  if (!strcmp(name, "TagFree")) {
    qa_guest_address a = qa_compat_q2_classic_required_pointer(args, count, 0);
    for (size_t i = 0; i < h->alloc_count; i++) if (h->allocations[i].key == a.byte_offset) {
      memory->unmap(memory->ctx, h->allocations[i].address, h->allocations[i].bytes);
      h->allocations[i] = h->allocations[--h->alloc_count]; return void_r;
    }
    qa_range_error("TagFree pointer is not a live API 3 allocation");
  }
  if (!strcmp(name, "FreeTags")) {
    double tag = qa_compat_q2_classic_number(args, count, 0);
    for (size_t i = h->alloc_count; i-- > 0;) if (h->allocations[i].tag == tag) {
      memory->unmap(memory->ctx, h->allocations[i].address, h->allocations[i].bytes);
      h->allocations[i] = h->allocations[--h->alloc_count];
    }
    return void_r;
  }
  /* String-argument helpers for the remaining switches. */
  char *s0 = NULL, *s1 = NULL;
  if (!strcmp(name, "cvar") || !strcmp(name, "cvar_set") || !strcmp(name, "cvar_forceset") || !strcmp(name, "configstring") ||
      !strcmp(name, "modelindex") || !strcmp(name, "soundindex") || !strcmp(name, "imageindex") || !strcmp(name, "setmodel") || !strcmp(name, "AddCommandString")) {
    qa_guest_address a0; bool has0 = qa_compat_q2_classic_pointer(args, count, !strcmp(name, "setmodel") ? 1 : 0, &a0);
    s0 = qa_compat_q2_read_classic_string(memory, has0, a0, 65536);
    if (!strcmp(name, "cvar") || !strcmp(name, "cvar_set") || !strcmp(name, "cvar_forceset") || !strcmp(name, "configstring")) {
      qa_guest_address a1; bool has1 = qa_compat_q2_classic_pointer(args, count, 1, &a1);
      s1 = qa_compat_q2_read_classic_string(memory, has1, a1, 65536);
    }
  }
  qa_guest_call_result result = void_r; bool handled = true;
  if (!strcmp(name, "cvar")) { qa_cvar_snapshot out; memset(&out, 0, sizeof out); qa_cvar_registry_register(services->cvars, s0, s1, qa_compat_q2_classic_number(args, count, 2), &out); qa_cvar_read_free(&out);
    qa_guest_address a; result = qa_compat_q2_call_pointer(qa_compat_q2_classic_cvars_pointer(h->cvars, s0, &a), a); }
  else if (!strcmp(name, "cvar_set") || !strcmp(name, "cvar_forceset")) { qa_cvar_snapshot out; memset(&out, 0, sizeof out); qa_cvar_registry_set(services->cvars, s0, s1, !strcmp(name, "cvar_forceset"), &out); qa_cvar_read_free(&out);
    qa_guest_address a; result = qa_compat_q2_call_pointer(qa_compat_q2_classic_cvars_pointer(h->cvars, s0, &a), a); }
  else if (!strcmp(name, "configstring")) services->configstring(services->ctx, qa_compat_q2_classic_number(args, count, 0), s1);
  else if (!strcmp(name, "modelindex") || !strcmp(name, "soundindex") || !strcmp(name, "imageindex")) {
    double index = services->resource_index(services->ctx, !strcmp(name, "modelindex") ? "model" : !strcmp(name, "soundindex") ? "sound" : "image", s0);
    if (!strcmp(name, "modelindex")) qa_compat_q2_classic_guest_host_set_model_name(h, index, s0[0] ? s0 : "");
    result = qa_compat_q2_call_int32(index);
  }
  else if (!strcmp(name, "sound")) { qa_guest_address e; bool has = qa_compat_q2_classic_pointer(args, count, 0, &e);
    services->sound(services->ctx, NULL, has, e, qa_compat_q2_classic_number(args, count, 1), qa_compat_q2_classic_number(args, count, 2), qa_compat_q2_classic_number(args, count, 3), qa_compat_q2_classic_number(args, count, 4), qa_compat_q2_classic_number(args, count, 5)); }
  else if (!strcmp(name, "positioned_sound")) { qa_guest_address e; bool has = qa_compat_q2_classic_pointer(args, count, 1, &e);
    qa_guest_address o; bool haso = qa_compat_q2_classic_pointer(args, count, 0, &o); qa_vec3 origin = haso ? qa_compat_q2_read_classic_vector(memory, o) : (qa_vec3){0, 0, 0};
    services->sound(services->ctx, haso ? &origin : NULL, has, e, qa_compat_q2_classic_number(args, count, 2), qa_compat_q2_classic_number(args, count, 3), qa_compat_q2_classic_number(args, count, 4), qa_compat_q2_classic_number(args, count, 5), qa_compat_q2_classic_number(args, count, 6)); }
  else if (!strcmp(name, "pointcontents")) result = qa_compat_q2_call_int32(services->engine->point_contents(services->engine->ctx, qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0))));
  else if (!strcmp(name, "inPVS")) result = qa_compat_q2_call_int32(services->engine->in_pvs(services->engine->ctx, qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0)), qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 1))) ? 1 : 0);
  else if (!strcmp(name, "inPHS")) result = qa_compat_q2_call_int32(services->engine->in_phs(services->engine->ctx, qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0)), qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 1))) ? 1 : 0);
  else if (!strcmp(name, "SetAreaPortalState")) services->engine->set_area_portal(services->engine->ctx, qa_compat_q2_classic_number(args, count, 0), qa_compat_q2_classic_number(args, count, 1) != 0);
  else if (!strcmp(name, "AreasConnected")) result = qa_compat_q2_call_int32(services->areas_connected(services->ctx, qa_compat_q2_classic_number(args, count, 0), qa_compat_q2_classic_number(args, count, 1)) ? 1 : 0);
  else if (!strcmp(name, "setmodel")) {
    qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(h->edicts, qa_compat_q2_classic_required_pointer(args, count, 0));
    double index = services->resource_index(services->ctx, "model", s0); qa_compat_q2_classic_guest_host_set_model_name(h, index, s0[0] ? s0 : "");
    qa_compat_q2_view_set_i32((uint8_t *)record.bytes.bytes, 40, (int32_t)index);
    if (s0[0] == '*') {
      qa_bounds b = services->engine->inline_model_bounds(services->engine->ctx, atof(s0 + 1));
      qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, record.address, 188), b.min);
      qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, record.address, 200), b.max);
      qa_compat_q2_classic_guest_host_link_entity(h, record.address);
    }
  }
  else if (!strcmp(name, "linkentity")) qa_compat_q2_classic_guest_host_link_entity(h, qa_compat_q2_classic_required_pointer(args, count, 0));
  else if (!strcmp(name, "unlinkentity")) { qa_owned_actor a; if (qa_compat_q2_classic_edicts_observe(h->edicts, qa_compat_q2_classic_required_pointer(args, count, 0), &a)) qa_shared_body_table_unlink(services->engine->bodies, a); }
  else if (!strcmp(name, "BoxEdicts")) {
    double maximum = qa_compat_q2_classic_number(args, count, 3), area = qa_compat_q2_classic_number(args, count, 4);
    if (maximum < 0 || (area != 1 && area != 2)) qa_range_error("BoxEdicts invalid area type or maximum count");
    qa_bounds b = {qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0)),
                   qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 1))};
    size_t n = 0; const qa_actor_id *results = services->box_edicts(services->ctx, b, area == 1 ? "solid" : "trigger", &n);
    if (n > (size_t)maximum) n = (size_t)maximum;
    qa_guest_address list = qa_compat_q2_classic_required_pointer(args, count, 2);
    for (size_t i = 0; i < n; i++)
      qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_mem_offset(memory, list, (int64_t)(i * 4)), true, qa_compat_q2_classic_edicts_pointer(h->edicts, results[i]));
    result = qa_compat_q2_call_int32((double)n);
  }
  else if (!strcmp(name, "trace")) {
    qa_guest_address mins_a, maxs_a, pass_a; bool has_mins = qa_compat_q2_classic_pointer(args, count, 1, &mins_a), has_maxs = qa_compat_q2_classic_pointer(args, count, 2, &maxs_a), has_pass = qa_compat_q2_classic_pointer(args, count, 4, &pass_a);
    qa_owned_actor ignored; bool has_ignored = has_pass && qa_compat_q2_classic_edicts_observe(h->edicts, pass_a, &ignored);
    qa_bounds b = {{0, 0, 0}, {0, 0, 0}};
    if (has_mins) b.min = qa_compat_q2_read_classic_vector(memory, mins_a);
    if (has_maxs) b.max = qa_compat_q2_read_classic_vector(memory, maxs_a);
    qa_q2_trace_request req; memset(&req, 0, sizeof req);
    req.start = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 0));
    req.end = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_classic_required_pointer(args, count, 3));
    req.bounds = (!has_mins && !has_maxs) ? NULL : &b; req.ignore = has_ignored ? &ignored.id : NULL;
    req.mask = qa_compat_q2_classic_number(args, count, 5);
    qa_trace_result tr = services->engine->trace(services->engine->ctx, req);
    qa_guest_call_value agg; memset(&agg, 0, sizeof agg); agg.kind = QA_GUEST_VALUE_AGGREGATE; agg.layout = qa_compat_q2_classic_trace_layout;
    uint8_t *bytes = malloc(56); qa_compat_q2_classic_guest_host_trace_bytes(h, tr, bytes);
    agg.bytes.bytes = bytes; agg.bytes.length = 56; result = agg;
  }
  else if (!strcmp(name, "Pmove")) services->pmove(services->pmove_ctx ? services->pmove_ctx : services->ctx, qa_compat_q2_classic_required_pointer(args, count, 0), h);
  else if (!strcmp(name, "argc")) { size_t n = 0; const char *tail; (void)services->command(services->ctx, &n, &tail); result = qa_compat_q2_call_int32((double)n); }
  else if (!strcmp(name, "argv")) { size_t n = 0; const char *tail; const char *const *argv = services->command(services->ctx, &n, &tail);
    double idx = qa_compat_q2_classic_number(args, count, 0);
    result = qa_compat_q2_call_pointer(true, qa_compat_q2_classic_cvars_string(h->cvars, (idx >= 0 && (size_t)idx < n) ? argv[(size_t)idx] : "")); }
  else if (!strcmp(name, "args")) { size_t n = 0; const char *tail; (void)services->command(services->ctx, &n, &tail); result = qa_compat_q2_call_pointer(true, qa_compat_q2_classic_cvars_string(h->cvars, tail)); }
  else if (!strcmp(name, "AddCommandString")) services->add_command(services->ctx, s0);
  else if (!strcmp(name, "DebugGraph")) services->debug_graph(services->ctx, qa_compat_q2_classic_number(args, count, 0), qa_compat_q2_classic_number(args, count, 1));
  else if (!strcmp(name, "multicast") || !strcmp(name, "unicast") || !strncmp(name, "Write", 5)) services->message(services->ctx, name, args, count, context);
  else handled = false;
  free(s0); free(s1);
  if (!handled) qa_range_error("Unsupported API 3 engine import %s", name);
  return result;
}
