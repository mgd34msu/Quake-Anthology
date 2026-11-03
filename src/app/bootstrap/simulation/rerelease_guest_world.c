/* Port of src/app/bootstrap/simulation/rerelease-guest-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/public_state.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

typedef enum world_phase { CREATED, INITIALIZED, RUNNING, TRANSFERRED, CLOSED } world_phase;
typedef struct retained {
  int references;
  qa_app_sim_rerelease_guest_source *source;
  qa_app_sim_rerelease_guest_services *services;
  bool has_command; char **arguments; size_t argument_count; char *args;
  qa_app_sim_guest_command fallback;
  void *clock_ctx; bool (*now)(void *, double *); bool (*counter)(void *, int64_t *);
} retained;
typedef struct retirement { double slot; qa_mod_client_identity identity; } retirement;
struct qa_app_sim_rerelease_guest_world {
  retained *shared; world_phase phase; bool busy; double frame;
  qa_compat_q2_native_input_binding *input; qa_compat_q2_native_input_services input_services;
  retirement *retirements; size_t retirement_count, retirement_capacity;
  qa_app_sim_classic_guest_client *clients; size_t client_count, client_capacity;
  qa_q2_rerelease_entity_state *states; size_t state_capacity;
  char *connect_userinfo;
};
typedef qa_app_sim_rerelease_guest_world world_t;

static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static char *dup_text(const char *text) { size_t n = strlen(text); char *p = malloc(n + 1); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n + 1); return p; }
static qa_compat_q2_rerelease_q2_guest_host *host_of(world_t *w) { return qa_app_sim_rerelease_guest_source_host(w->shared->source); }
static qa_compat_q2_rerelease_guest_module *module_of(world_t *w) { return qa_compat_q2_rerelease_q2_guest_host_get_module(host_of(w)); }
static const qa_app_sim_rerelease_guest_services_options *service_options(world_t *w) { return qa_app_sim_rerelease_guest_services_options_of(w->shared->services); }
static void require_ownership(const world_t *w) { if (w->phase == TRANSFERRED || w->phase == CLOSED) fail("Native rerelease world is retired"); }
static void require_running(world_t *w) { require_ownership(w); if (w->phase != RUNNING) fail("Native world has not spawned"); }

char *qa_app_sim_fit_rerelease_text(const char *text, size_t capacity) {
  /* UTF-16 slicing in TS can split a surrogate pair; the C bytes are cut at code point boundaries. */
  size_t n = strlen(text);
  if (n < capacity) return dup_text(text);
  size_t end = capacity == 0 ? 0 : capacity - 1;
  while (end > 0 && ((unsigned char)text[end] & 0xc0) == 0x80) end--;
  char *out = malloc(end + 1); if (out == NULL) qa_range_error("Out of memory"); memcpy(out, text, end); out[end] = '\0'; return out;
}

/* --- clients --- */
static qa_app_sim_classic_guest_client *find_client(world_t *w, double slot) { for (size_t i = 0; i < w->client_count; i++) if (w->clients[i].slot == slot) return &w->clients[i]; return NULL; }
static void set_client(world_t *w, double slot, const char *phase, const char *userinfo) {
  qa_app_sim_classic_guest_client *client = find_client(w, slot); char *copy = dup_text(userinfo);
  if (client == NULL) {
    if (w->client_count == w->client_capacity) { size_t next = w->client_capacity == 0 ? 4 : w->client_capacity * 2; void *g = realloc(w->clients, next * sizeof *w->clients); if (g == NULL) qa_range_error("Out of memory"); w->clients = g; w->client_capacity = next; }
    client = &w->clients[w->client_count++]; client->slot = slot; client->userinfo = NULL;
  }
  free(client->userinfo); client->userinfo = copy; client->phase = phase;
}
static void delete_client(world_t *w, double slot) {
  qa_app_sim_classic_guest_client *client = find_client(w, slot); if (client == NULL) return;
  size_t index = (size_t)(client - w->clients); free(client->userinfo); memmove(client, client + 1, (w->client_count - index - 1) * sizeof *client); w->client_count--;
}
static void clear_clients(world_t *w) { for (size_t i = 0; i < w->client_count; i++) free(w->clients[i].userinfo); w->client_count = 0; }
static qa_app_sim_classic_guest_client *require_client(world_t *w, double slot, const char *phase) {
  qa_app_sim_classic_guest_client *client = find_client(w, slot);
  if (client == NULL || (phase != NULL && strcmp(client->phase, phase) != 0)) qa_raise("Error", 0, 0, "Native client %s is not %s", QA_JS_NUMBER(slot), phase != NULL ? phase : "connected");
  return client;
}

/* --- input retirements --- */
static void retire(void *ctx, double slot, qa_mod_client_identity identity) {
  world_t *w = ctx; qa_compat_q2_rerelease_q2_guest_host_retire_input_client(host_of(w), slot);
  for (size_t i = 0; i < w->retirement_count; i++) if (w->retirements[i].slot == slot) { w->retirements[i].identity = identity; return; }
  if (w->retirement_count == w->retirement_capacity) { size_t next = w->retirement_capacity == 0 ? 4 : w->retirement_capacity * 2; void *g = realloc(w->retirements, next * sizeof *w->retirements); if (g == NULL) qa_range_error("Out of memory"); w->retirements = g; w->retirement_capacity = next; }
  w->retirements[w->retirement_count].slot = slot; w->retirements[w->retirement_count++].identity = identity;
}
static void complete_retirement(world_t *w) {
  retirement entry = w->retirements[0];
  memmove(&w->retirements[0], &w->retirements[1], (w->retirement_count - 1) * sizeof *w->retirements); w->retirement_count--;
  delete_client(w, entry.slot); w->input_services.retired(w->input_services.ctx, entry.identity);
}
static void finish_input_retirements(world_t *w) {
  while (w->retirement_count > 0) {
    double slot = w->retirements[0].slot; qa_compat_q2_rerelease_q2_guest_host *host = host_of(w);
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(module_of(w));
    qa_compat_q2_rerelease_public_edict *record = qa_compat_q2_rerelease_public_edict_create(qa_compat_q2_rerelease_guest_module_get_memory(module_of(w)), table.at_slot(table.ctx, slot));
    bool in_use = qa_compat_q2_rerelease_public_edict_byte(record, "inuse") != 0; qa_compat_q2_rerelease_public_edict_destroy(record);
    if (in_use) qa_compat_q2_rerelease_q2_guest_host_client_disconnect(host, slot);
    qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(host, slot); qa_compat_q2_rerelease_q2_guest_host_finish_input_retirement(host, slot);
    complete_retirement(w);
  }
}
static void discard_input_retirements(world_t *w) { while (w->retirement_count > 0) complete_retirement(w); }
static void close_input(world_t *w) { if (w->input != NULL) { qa_compat_q2_native_input_binding_close(w->input); qa_compat_q2_native_input_binding_destroy(w->input); w->input = NULL; } }
void qa_app_sim_rerelease_guest_world_bind_input(world_t *w, qa_compat_q2_native_input_services services) {
  close_input(w); w->input_services = services;
  w->input = qa_compat_q2_native_input_binding_create(qa_compat_q2_native_input_source_rerelease(module_of(w), w, retire), services);
}

/* --- command context --- */
static const char *const *context_command(void *ctx, size_t *out_count, const char **out_args) {
  retained *r = ctx; if (r->has_command) { *out_count = r->argument_count; *out_args = r->args; return (const char *const *)r->arguments; }
  return r->fallback.command(r->fallback.ctx, out_count, out_args);
}
static void clear_command(void *arg) {
  retained *r = arg; for (size_t i = 0; i < r->argument_count; i++) free(r->arguments[i]);
  free(r->arguments); free(r->args); r->arguments = NULL; r->argument_count = 0; r->args = NULL; r->has_command = false;
}
static void set_command(retained *r, const char *const *arguments, size_t count, const char *args) {
  clear_command(r); r->arguments = calloc(count == 0 ? 1 : count, sizeof *r->arguments); if (r->arguments == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) r->arguments[i] = dup_text(arguments[i]);
  r->argument_count = count; r->args = dup_text(args); r->has_command = true;
}

/* --- construction --- */
static world_t *new_world(retained *shared) { world_t *w = calloc(1, sizeof *w); if (w == NULL) qa_range_error("Out of memory"); w->shared = shared; shared->references++; return w; }
static qa_compat_q2_rerelease_core_services bind_memory(void *ctx, qa_compat_q2_memory *memory) { return qa_app_sim_rerelease_guest_services_bind_memory(((retained *)ctx)->services, memory); }
static bool clock_now(void *ctx, double *out) { retained *r = ctx; return r->now(r->clock_ctx, out); }
static bool clock_counter(void *ctx, int64_t *out) { retained *r = ctx; return r->counter(r->clock_ctx, out); }
world_t *qa_app_sim_rerelease_guest_world_create(const qa_app_sim_rerelease_guest_world_options *options) {
  retained *shared = calloc(1, sizeof *shared); if (shared == NULL) qa_range_error("Out of memory");
  qa_app_sim_rerelease_guest_services_options services = options->services;
  shared->fallback = services.base.command; services.base.command.ctx = shared; services.base.command.command = context_command;
  shared->services = qa_app_sim_rerelease_guest_services_create(&services); if (shared->services == NULL) qa_range_error("Out of memory");
  const qa_windows_capabilities *clock = &options->capabilities;
  if (clock->now_milliseconds == NULL || clock->performance_counter == NULL || !clock->has_performance_frequency) fail("Native rerelease game requires the host clock capabilities");
  shared->clock_ctx = clock->ctx; shared->now = clock->now_milliseconds; shared->counter = clock->performance_counter;
  qa_app_sim_rerelease_guest_source_options source; memset(&source, 0, sizeof source);
  qa_app_sim_rerelease_guest_services_host_options(shared->services, &source.host);
  source.pickups = source.host.has_pickups ? source.host.pickup_admission : NULL; source.host.has_pickups = false; source.host.pickup_admission = NULL;
  source.clock_ctx = shared; source.now_milliseconds = clock_now; source.performance_counter = clock_counter; source.performance_frequency = clock->performance_frequency;
  if (options->has_instruction_budget) { source.host.module_options.has_instruction_budget = true; source.host.module_options.instruction_budget = options->instruction_budget; }
  source.services_ctx = shared; source.services = bind_memory;
  shared->source = qa_app_sim_rerelease_guest_source_create(options->prepared, &source);
  qa_app_sim_rerelease_guest_services_bind_host(shared->services, qa_app_sim_rerelease_guest_source_host(shared->source));
  return new_world(shared);
}
static void release_shared(retained *r) {
  if (--r->references > 0) return;
  qa_app_sim_rerelease_guest_source_destroy(r->source); qa_app_sim_rerelease_guest_services_destroy(r->services); clear_command(r); free(r);
}
void qa_app_sim_rerelease_guest_world_destroy(world_t *w) {
  if (w == NULL) return;
  close_input(w); clear_clients(w); free(w->clients); free(w->retirements); free(w->states); free(w->connect_userinfo);
  release_shared(w->shared); free(w);
}
bool qa_app_sim_rerelease_guest_world_is_retired(const world_t *w) { return w->phase == TRANSFERRED || w->phase == CLOSED; }
qa_app_sim_rerelease_guest_source *qa_app_sim_rerelease_guest_world_source(world_t *w) { require_ownership(w); return w->shared->source; }
qa_app_sim_rerelease_guest_services *qa_app_sim_rerelease_guest_world_services(world_t *w) { require_ownership(w); return w->shared->services; }
qa_module_identity qa_app_sim_rerelease_guest_world_module(world_t *w) { return qa_app_sim_rerelease_guest_source_memory(qa_app_sim_rerelease_guest_world_source(w))->module; }
const qa_app_sim_classic_guest_client *qa_app_sim_rerelease_guest_world_clients(world_t *w, size_t *out_count) { require_ownership(w); *out_count = w->client_count; return w->clients; }

static void clear_busy(void *arg) { ((world_t *)arg)->busy = false; }
typedef struct scope_t { world_t *w; qa_cleanup node; } scope_t;
static void begin(world_t *w, scope_t *scope) {
  require_ownership(w); if (w->busy) fail("External native world operation is already active");
  w->busy = true; scope->w = w; qa_cleanup_push(&scope->node, clear_busy, w);
}
static void end(scope_t *scope) { finish_input_retirements(scope->w); qa_cleanup_pop(&scope->node); scope->w->busy = false; }

static void initialize(world_t *w) {
  if (w->phase != CREATED) fail("Native Init requires a fresh world");
  qa_compat_q2_rerelease_q2_guest_host_pre_init(host_of(w)); qa_app_sim_rerelease_guest_source_init(w->shared->source); w->phase = INITIALIZED;
}
void qa_app_sim_rerelease_guest_world_init(world_t *w) { scope_t s; begin(w, &s); initialize(w); end(&s); }
void qa_app_sim_rerelease_guest_world_init_loading(world_t *w, void (*next_frame)(void *), void *next_ctx) {
  scope_t s; begin(w, &s);
  if (w->phase != CREATED) fail("Native Init requires a fresh world");
  qa_app_sim_rerelease_guest_source_init_loading(w->shared->source, next_frame, next_ctx); w->phase = INITIALIZED;
  end(&s);
}
static void run_frame(world_t *w, bool main_loop) {
  qa_app_sim_rerelease_guest_services_begin_frame(w->shared->services, ++w->frame);
  qa_compat_q2_rerelease_q2_guest_host_prep_frame(host_of(w)); qa_compat_q2_rerelease_q2_guest_host_run_frame(host_of(w), main_loop);
  qa_app_sim_rerelease_guest_services_publish_entities(w->shared->services);
}
static void spawn_map(world_t *w, qa_app_sim_classic_guest_map map) {
  if (w->phase != INITIALIZED) fail("Native SpawnEntities requires an initialized candidate");
  w->frame = 0; qa_compat_q2_rerelease_q2_guest_host_spawn_entities(host_of(w), map.map, map.entities, map.spawn_point != NULL ? map.spawn_point : "");
  run_frame(w, false); run_frame(w, false); qa_app_sim_rerelease_guest_services_complete_spawn(w->shared->services); w->phase = RUNNING;
}
static void spawn_map_loading(world_t *w, qa_app_sim_classic_guest_map map, void (*next_frame)(void *), void *next_ctx) {
  if (w->phase != INITIALIZED) fail("Native SpawnEntities requires an initialized candidate");
  w->frame = 0; qa_compat_q2_rerelease_q2_guest_host_spawn_entities_loading(host_of(w), map.map, map.entities, map.spawn_point != NULL ? map.spawn_point : "", next_frame, next_ctx);
  for (int frame = 0; frame < 2; frame++) {
    qa_app_sim_rerelease_guest_services_begin_frame(w->shared->services, ++w->frame);
    qa_compat_q2_rerelease_q2_guest_host_run_frame_loading(host_of(w), false, next_frame, next_ctx);
    qa_app_sim_rerelease_guest_services_publish_entities(w->shared->services);
  }
  qa_app_sim_rerelease_guest_services_complete_spawn(w->shared->services); w->phase = RUNNING;
}
void qa_app_sim_rerelease_guest_world_spawn(world_t *w, const char *map, const char *entities, const char *spawn_point) {
  scope_t s; begin(w, &s); qa_app_sim_classic_guest_map m = {map, entities, spawn_point}; spawn_map(w, m); end(&s);
}
void qa_app_sim_rerelease_guest_world_spawn_loading(world_t *w, const char *map, const char *entities, void (*next_frame)(void *), void *next_ctx, const char *spawn_point) {
  scope_t s; begin(w, &s); qa_app_sim_classic_guest_map m = {map, entities, spawn_point}; spawn_map_loading(w, m, next_frame, next_ctx); end(&s);
}

typedef struct travel_call { world_t *from, *next; qa_app_sim_classic_guest_map map; const qa_app_sim_rerelease_guest_map_services *binding;
  const qa_app_sim_rerelease_guest_revisit *revisit; void (*next_frame)(void *); void *next_ctx; qa_app_sim_classic_guest_client *clients; size_t client_count; } travel_call;
static void commit_rebind(void *ctx) {
  travel_call *t = ctx; retained *shared = t->next->shared;
  qa_app_sim_rerelease_guest_map_services binding = *t->binding;
  shared->fallback = t->binding->base.command; binding.base.command.ctx = shared; binding.base.command.command = context_command;
  qa_app_sim_rerelease_guest_services_rebind_world(shared->services, &binding);
}
static void run_travel(void *ctx) {
  travel_call *t = ctx; world_t *next = t->next; qa_compat_q2_rerelease_q2_guest_host *host = host_of(next);
  qa_compat_q2_rerelease_q2_guest_host_rebind_world(host, t, commit_rebind);
  if (t->next_frame == NULL) spawn_map(next, t->map); else spawn_map_loading(next, t->map, t->next_frame, t->next_ctx);
  if (t->revisit != NULL) {
    next->phase = INITIALIZED; size_t drained; (void)qa_app_sim_rerelease_guest_services_drain_messages(next->shared->services, &drained);
    t->revisit->restore_server_state(t->revisit->ctx, next);
    if (t->next_frame == NULL) qa_compat_q2_rerelease_q2_guest_host_read_save(host, "level", t->revisit->level, "checkpoint");
    else qa_compat_q2_rerelease_q2_guest_host_read_save_loading(host, "level", t->revisit->level, t->next_frame, t->next_ctx, "checkpoint");
    qa_app_sim_rerelease_guest_services_complete_spawn(next->shared->services); next->phase = RUNNING;
  }
  for (size_t i = 0; i < t->client_count; i++) { (void)qa_compat_q2_rerelease_q2_guest_host_reserve_client(host, t->clients[i].slot); set_client(next, t->clients[i].slot, "connected", t->clients[i].userinfo); }
}
static world_t *transfer(world_t *w, qa_app_sim_classic_guest_map map, const qa_app_sim_rerelease_guest_map_services *binding, void (*next_frame)(void *), void *next_ctx,
                         const qa_app_sim_rerelease_guest_revisit *revisit) {
  scope_t s; begin(w, &s); require_running(w);
  qa_app_sim_rerelease_guest_services_validate_map(w->shared->services, binding->base.scene);
  if (revisit != NULL && qa_cvar_registry_variable_value(service_options(w)->base.cvars, "deathmatch") != 0) fail("Native deathmatch does not restore hub levels");
  finish_input_retirements(w);
  travel_call t; memset(&t, 0, sizeof t); t.from = w; t.map = map; t.binding = binding; t.revisit = revisit; t.next_frame = next_frame; t.next_ctx = next_ctx;
  t.clients = w->clients; t.client_count = w->client_count; w->clients = NULL; w->client_count = 0; w->client_capacity = 0;
  world_t *next = new_world(w->shared);
  close_input(w); next->phase = INITIALIZED; w->phase = TRANSFERRED; t.next = next;
  qa_caught_error caught; bool completed = qa_try(run_travel, &t, &caught);
  for (size_t i = 0; i < t.client_count; i++) free(t.clients[i].userinfo);
  free(t.clients);
  qa_cleanup_pop(&s.node); w->busy = false;
  if (!completed) { qa_app_sim_rerelease_guest_world_close(next); qa_rethrow(&caught); }
  return next;
}
world_t *qa_app_sim_rerelease_guest_world_travel(world_t *w, qa_app_sim_classic_guest_map map, const qa_app_sim_rerelease_guest_map_services *binding, const qa_app_sim_rerelease_guest_revisit *revisit) {
  return transfer(w, map, binding, NULL, NULL, revisit);
}
world_t *qa_app_sim_rerelease_guest_world_travel_loading(world_t *w, qa_app_sim_classic_guest_map map, const qa_app_sim_rerelease_guest_map_services *binding,
                                                         void (*next_frame)(void *), void *next_ctx, const qa_app_sim_rerelease_guest_revisit *revisit) {
  return transfer(w, map, binding, next_frame, next_ctx, revisit);
}

typedef struct connect_call { world_t *w; double slot; const char *userinfo, *social; bool bot; qa_compat_q2_rerelease_client_connect result; } connect_call;
static void run_connect(void *ctx) { connect_call *c = ctx; c->result = qa_compat_q2_rerelease_q2_guest_host_client_connect(host_of(c->w), c->slot, c->userinfo, c->social, c->bot); }
qa_app_sim_classic_guest_connect qa_app_sim_rerelease_guest_world_connect(world_t *w, double slot, const char *userinfo, const char *social_id, bool is_bot) {
  scope_t s; begin(w, &s); require_running(w);
  if (slot != floor(slot) || slot < 1 || slot > service_options(w)->base.max_clients || find_client(w, slot) != NULL) qa_range_error("Native client slot is unavailable");
  (void)qa_compat_q2_rerelease_q2_guest_host_reserve_client(host_of(w), slot);
  connect_call call = {w, slot, userinfo, social_id != NULL ? social_id : "", is_bot, {false, NULL}}; qa_caught_error caught;
  if (!qa_try(run_connect, &call, &caught)) { qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(host_of(w), slot); qa_rethrow(&caught); }
  if (call.result.accepted) set_client(w, slot, "connected", call.result.userinfo); else qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(host_of(w), slot);
  free(w->connect_userinfo); w->connect_userinfo = call.result.userinfo;
  end(&s);
  qa_app_sim_classic_guest_connect out = {call.result.accepted, w->connect_userinfo}; return out;
}
void qa_app_sim_rerelease_guest_world_begin(world_t *w, double slot) {
  scope_t s; begin(w, &s); require_running(w); (void)require_client(w, slot, "connected");
  qa_compat_q2_rerelease_q2_guest_host_client_begin(host_of(w), slot); find_client(w, slot)->phase = "active";
  end(&s);
}
qa_app_sim_classic_guest_connect qa_app_sim_rerelease_guest_world_admit(world_t *w, double slot, const char *userinfo, const char *social_id, bool is_bot) {
  qa_app_sim_classic_guest_connect result = qa_app_sim_rerelease_guest_world_connect(w, slot, userinfo, social_id, is_bot);
  if (result.allowed) qa_app_sim_rerelease_guest_world_begin(w, slot);
  return result;
}
void qa_app_sim_rerelease_guest_world_disconnect(world_t *w, double slot) {
  scope_t s; begin(w, &s); require_running(w); (void)require_client(w, slot, NULL);
  qa_compat_q2_rerelease_q2_guest_host_client_disconnect(host_of(w), slot); qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(host_of(w), slot); delete_client(w, slot);
  end(&s);
}
typedef struct info_scope { qa_compat_q2_memory *memory; qa_guest_address info; } info_scope;
static void unmap_info(void *arg) { info_scope *i = arg; i->memory->unmap(i->memory->ctx, i->info, 2048); }
void qa_app_sim_rerelease_guest_world_userinfo(world_t *w, double slot, const char *value) {
  scope_t s; begin(w, &s); require_running(w);
  qa_app_sim_classic_guest_client *client = require_client(w, slot, NULL); const char *phase = client->phase;
  qa_compat_q2_rerelease_guest_module *module = module_of(w); qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  char *fitted = qa_app_sim_fit_rerelease_text(value, 2048);
  info_scope info = {memory, memory->allocate(memory->ctx, 2048, false, 0, "Q2 mutable userinfo")};
  qa_cleanup node; qa_cleanup_push(&node, unmap_info, &info);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(module); qa_raw_entity_view entity = table.at_slot(table.ctx, slot);
  memory->write(memory->ctx, info.info, (const uint8_t *)fitted, strlen(fitted)); free(fitted);
  qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(host_of(w)));
  qa_guest_call_value arguments[2] = {qa_compat_q2_rerelease_guest_pointer(true, entity.address), qa_compat_q2_rerelease_guest_pointer(true, info.info)};
  (void)qa_compat_q2_rerelease_guest_module_call_game(module, "ClientUserinfoChanged", arguments, 2, &entity, NULL);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(host_of(w));
  uint8_t output[2048]; memory->copy(memory->ctx, info.info, output, 2048);
  void *end_byte = memchr(output, 0, 2048);
  if (end_byte == NULL) fail("ClientUserinfoChanged returned unterminated userinfo");
  set_client(w, slot, phase, (const char *)output);
  qa_cleanup_pop(&node); unmap_info(&info);
  end(&s);
}
void qa_app_sim_rerelease_guest_world_set_userinfo_storage(world_t *w, double slot, const char *value) {
  require_running(w); qa_app_sim_classic_guest_client *client = require_client(w, slot, NULL);
  char *fitted = qa_app_sim_fit_rerelease_text(value, 2048); set_client(w, slot, client->phase, fitted); free(fitted);
}
void qa_app_sim_rerelease_guest_world_command(world_t *w, double slot, const char *const *arguments, size_t count, const char *args) {
  scope_t s; begin(w, &s); require_running(w); (void)require_client(w, slot, "active");
  qa_compat_q2_rerelease_guest_module *module = module_of(w); qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(module); qa_raw_entity_view entity = table.at_slot(table.ctx, slot);
  set_command(w->shared, arguments, count, args); qa_cleanup node; qa_cleanup_push(&node, clear_command, w->shared);
  qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(host_of(w)));
  qa_guest_call_value argument = qa_compat_q2_rerelease_guest_pointer(true, entity.address);
  (void)qa_compat_q2_rerelease_guest_module_call_game(module, "ClientCommand", &argument, 1, &entity, NULL);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(host_of(w));
  qa_cleanup_pop(&node); clear_command(w->shared);
  end(&s);
}
void qa_app_sim_rerelease_guest_world_server_command(world_t *w, const char *const *arguments, size_t count, const char *args) {
  scope_t s; begin(w, &s); require_running(w);
  set_command(w->shared, arguments, count, args); qa_cleanup node; qa_cleanup_push(&node, clear_command, w->shared);
  qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_q2_guest_host_get_core(host_of(w)));
  (void)qa_compat_q2_rerelease_guest_module_call_game(module_of(w), "ServerCommand", NULL, 0, NULL, NULL);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(host_of(w));
  qa_cleanup_pop(&node); clear_command(w->shared);
  end(&s);
}
void qa_app_sim_rerelease_guest_world_think(world_t *w, double slot, qa_q2_rerelease_user_command command, const qa_equipment_movement *movement) {
  scope_t s; begin(w, &s); require_running(w); (void)require_client(w, slot, "active");
  qa_compat_q2_rerelease_q2_guest_host_client_think(host_of(w), slot, command, movement);
  end(&s);
}
void qa_app_sim_rerelease_guest_world_frame(world_t *w, double milliseconds) {
  scope_t s; begin(w, &s); require_running(w);
  if (milliseconds != service_options(w)->frame_milliseconds) qa_range_error("API 2023 frame differs from its import-table cadence");
  run_frame(w, true);
  end(&s);
}
bool qa_app_sim_rerelease_guest_world_actor(world_t *w, double slot, qa_actor_id *out) {
  require_running(w); qa_app_sim_rerelease_entity_info info = qa_app_sim_rerelease_guest_services_entity_info(w->shared->services, slot);
  if (info.has_actor) *out = info.actor;
  return info.has_actor;
}
qa_app_sim_rerelease_entity_info qa_app_sim_rerelease_guest_world_entity_info(world_t *w, double slot) { require_running(w); return qa_app_sim_rerelease_guest_services_entity_info(w->shared->services, slot); }
const qa_q2_rerelease_entity_state *qa_app_sim_rerelease_guest_world_entity_states(world_t *w, size_t *out_count) {
  require_running(w); size_t count = 0; double total = qa_compat_q2_rerelease_guest_module_entities(module_of(w)).count;
  for (double slot = 1; slot < total; slot++) {
    qa_app_sim_rerelease_entity_info info = qa_app_sim_rerelease_guest_services_entity_info(w->shared->services, slot);
    if (!info.active || ((int64_t)info.server_flags & 1) != 0) continue;
    if (count == w->state_capacity) { size_t next = w->state_capacity == 0 ? 64 : w->state_capacity * 2; void *g = realloc(w->states, next * sizeof *w->states); if (g == NULL) qa_range_error("Out of memory"); w->states = g; w->state_capacity = next; }
    w->states[count++] = qa_app_sim_rerelease_guest_services_entity_state(w->shared->services, slot);
  }
  *out_count = count; return w->states;
}
qa_q2_rerelease_entity_state qa_app_sim_rerelease_guest_world_entity_state(world_t *w, double slot) { require_running(w); return qa_app_sim_rerelease_guest_services_entity_state(w->shared->services, slot); }
qa_q2_rerelease_player_state qa_app_sim_rerelease_guest_world_player_state(world_t *w, double slot) { require_running(w); return qa_app_sim_rerelease_guest_services_player_state(w->shared->services, slot); }
double qa_app_sim_rerelease_guest_world_player_ping(world_t *w, double slot) { require_running(w); return qa_app_sim_rerelease_guest_services_player_ping(w->shared->services, slot); }
void qa_app_sim_rerelease_guest_world_set_player_ping(world_t *w, double slot, double ping) {
  scope_t s; begin(w, &s); require_running(w); (void)require_client(w, slot, NULL); qa_app_sim_rerelease_guest_services_set_player_ping(w->shared->services, slot, ping); end(&s);
}
qa_app_sim_model_appearance qa_app_sim_rerelease_guest_world_model_appearance(world_t *w, double slot) { require_running(w); return qa_app_sim_rerelease_guest_services_model_appearance(w->shared->services, slot); }
const qa_app_sim_rerelease_guest_message *qa_app_sim_rerelease_guest_world_raw_messages(world_t *w, size_t *out_count) { return qa_app_sim_rerelease_guest_services_drain_messages(qa_app_sim_rerelease_guest_world_services(w), out_count); }
const qa_app_sim_configstring *qa_app_sim_rerelease_guest_world_configstrings(world_t *w, size_t *out_count) { return qa_app_sim_rerelease_guest_services_configstrings(qa_app_sim_rerelease_guest_world_services(w), out_count); }
void qa_app_sim_rerelease_guest_world_restore_configstrings(world_t *w, const qa_app_sim_configstring *values, size_t count) {
  if (w->phase != INITIALIZED) fail("Native configstrings require the initialized candidate");
  qa_app_sim_rerelease_guest_services_restore_configstrings(qa_app_sim_rerelease_guest_world_services(w), values, count);
}
qa_app_sim_rerelease_guest_save qa_app_sim_rerelease_guest_world_write_save(world_t *w, bool autosave) {
  scope_t s; begin(w, &s); require_running(w); qa_app_sim_rerelease_guest_save out;
  out.game = qa_compat_q2_rerelease_q2_guest_host_write_save(host_of(w), "game", autosave); out.level = qa_compat_q2_rerelease_q2_guest_host_write_save(host_of(w), "level", false);
  end(&s); return out;
}
qa_app_sim_rerelease_guest_save qa_app_sim_rerelease_guest_world_write_save_loading(world_t *w, bool autosave, void (*next_frame)(void *), void *next_ctx) {
  scope_t s; begin(w, &s); require_running(w); qa_app_sim_rerelease_guest_save out;
  out.game = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(host_of(w), "game", autosave, next_frame, next_ctx);
  out.level = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(host_of(w), "level", false, next_frame, next_ctx);
  end(&s); return out;
}
qa_compat_q2_rerelease_source_save qa_app_sim_rerelease_guest_world_write_travel_level(world_t *w) {
  scope_t s; begin(w, &s); require_running(w); qa_compat_q2_rerelease_source_save out = qa_compat_q2_rerelease_q2_guest_host_write_save(host_of(w), "level", true); end(&s); return out;
}
qa_compat_q2_rerelease_source_save qa_app_sim_rerelease_guest_world_write_travel_level_loading(world_t *w, void (*next_frame)(void *), void *next_ctx) {
  scope_t s; begin(w, &s); require_running(w); qa_compat_q2_rerelease_source_save out = qa_compat_q2_rerelease_q2_guest_host_write_save_loading(host_of(w), "level", true, next_frame, next_ctx); end(&s); return out;
}
static void restore(world_t *w, qa_app_sim_rerelease_guest_save saved, qa_app_sim_classic_guest_map map, void (*restore_server_state)(void *), void *restore_ctx,
                    void (*next_frame)(void *), void *next_ctx) {
  if (w->phase != INITIALIZED) fail("Native import requires a fresh initialized candidate");
  if (next_frame == NULL) { qa_compat_q2_rerelease_q2_guest_host_read_save(host_of(w), "game", saved.game, "checkpoint"); spawn_map(w, map); }
  else { qa_compat_q2_rerelease_q2_guest_host_read_save_loading(host_of(w), "game", saved.game, next_frame, next_ctx, "checkpoint"); spawn_map_loading(w, map, next_frame, next_ctx); }
  w->phase = INITIALIZED; size_t drained; (void)qa_app_sim_rerelease_guest_services_drain_messages(w->shared->services, &drained);
  restore_server_state(restore_ctx);
  if (next_frame == NULL) qa_compat_q2_rerelease_q2_guest_host_read_save(host_of(w), "level", saved.level, "checkpoint");
  else qa_compat_q2_rerelease_q2_guest_host_read_save_loading(host_of(w), "level", saved.level, next_frame, next_ctx, "checkpoint");
  qa_app_sim_rerelease_guest_services_complete_spawn(w->shared->services); w->phase = RUNNING;
}
void qa_app_sim_rerelease_guest_world_read_save(world_t *w, qa_app_sim_rerelease_guest_save saved, qa_app_sim_classic_guest_map map, void (*restore_server_state)(void *), void *restore_ctx) {
  scope_t s; begin(w, &s); restore(w, saved, map, restore_server_state, restore_ctx, NULL, NULL); end(&s);
}
void qa_app_sim_rerelease_guest_world_read_save_loading(world_t *w, qa_app_sim_rerelease_guest_save saved, qa_app_sim_classic_guest_map map, void (*restore_server_state)(void *), void *restore_ctx,
                                                        void (*next_frame)(void *), void *next_ctx) {
  scope_t s; begin(w, &s); restore(w, saved, map, restore_server_state, restore_ctx, next_frame, next_ctx); end(&s);
}
static void run_close(void *ctx) { qa_app_sim_rerelease_guest_source_close(ctx); }
void qa_app_sim_rerelease_guest_world_close(world_t *w) {
  if (qa_app_sim_rerelease_guest_world_is_retired(w)) return;
  close_input(w);
  scope_t s; begin(w, &s); discard_input_retirements(w);
  qa_caught_error caught; bool completed = qa_try(run_close, w->shared->source, &caught);
  w->phase = CLOSED; clear_clients(w); qa_cleanup_pop(&s.node); w->busy = false;
  if (!completed) qa_rethrow(&caught);
}
void qa_app_sim_rerelease_guest_world_discard(world_t *w) { qa_app_sim_rerelease_guest_world_close(w); }
