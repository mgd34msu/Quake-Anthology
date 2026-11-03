/* Port of src/app/bootstrap/simulation/classic-guest-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/classic/world_profile.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"

typedef enum world_phase { CREATED, INITIALIZED, RUNNING, TRANSFERRED, CLOSED } world_phase;
/* The source, services and command context every world of one guest shares (TS retainedSource/retainedServices/commandContext). */
typedef struct retained {
  int references;
  qa_app_sim_classic_guest_source *source;
  qa_app_sim_classic_guest_services *services;
  bool has_command; char **arguments; size_t argument_count; char *args;
  qa_app_sim_guest_command fallback; /* the options' command leaf */
  qa_compat_q2_classic_world_profile world_profile; bool has_world_profile;
} retained;
typedef struct retirement { double slot; qa_mod_client_identity identity; } retirement;
struct qa_app_sim_classic_guest_world {
  retained *shared;
  world_phase phase;
  bool busy;
  qa_compat_q2_native_input_binding *input;
  qa_compat_q2_native_input_services input_services;
  retirement *retirements; size_t retirement_count, retirement_capacity;
  qa_app_sim_classic_guest_client *clients; size_t client_count, client_capacity;
  qa_q2_entity_state *states; size_t state_capacity;
  char *connect_userinfo;
};
typedef qa_app_sim_classic_guest_world world_t;

static char *dup_text(const char *text) { size_t n = strlen(text); char *p = malloc(n + 1); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n + 1); return p; }
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static qa_compat_q2_classic_guest_host *host_of(world_t *w) { return qa_app_sim_classic_guest_source_host(w->shared->source); }
static qa_compat_q2_classic_edicts *edicts_of(world_t *w) { return qa_compat_q2_classic_guest_host_get_edicts(host_of(w)); }
static double budget_of(world_t *w) { return qa_compat_q2_classic_guest_host_get_options(host_of(w))->instruction_budget; }
static const qa_app_sim_classic_guest_services_options *service_options(world_t *w) { return qa_app_sim_classic_guest_services_options_of(w->shared->services); }
static void require_ownership(const world_t *w) { if (w->phase == TRANSFERRED) fail("Native world ownership was transferred"); }

/* TS fitClassicUserinfo(text, 512): the NUL-terminated prefix, at most 511 characters. */
static char *fit_userinfo(const char *text) { size_t n = strlen(text); if (n > 511) n = 511; char *out = malloc(n + 1); if (out == NULL) qa_range_error("Out of memory"); memcpy(out, text, n); out[n] = '\0'; return out; }

/* --- clients (TS Map insertion order) --- */
static qa_app_sim_classic_guest_client *find_client(world_t *w, double slot) {
  for (size_t i = 0; i < w->client_count; i++) if (w->clients[i].slot == slot) return &w->clients[i];
  return NULL;
}
static void set_client(world_t *w, double slot, const char *phase, const char *userinfo) {
  qa_app_sim_classic_guest_client *client = find_client(w, slot);
  char *copy = dup_text(userinfo);
  if (client == NULL) {
    if (w->client_count == w->client_capacity) {
      size_t next = w->client_capacity == 0 ? 4 : w->client_capacity * 2;
      void *grown = realloc(w->clients, next * sizeof *w->clients); if (grown == NULL) qa_range_error("Out of memory");
      w->clients = grown; w->client_capacity = next;
    }
    client = &w->clients[w->client_count++]; client->slot = slot; client->userinfo = NULL;
  }
  free(client->userinfo); client->userinfo = copy; client->phase = phase;
}
static void delete_client(world_t *w, double slot) {
  qa_app_sim_classic_guest_client *client = find_client(w, slot); if (client == NULL) return;
  size_t index = (size_t)(client - w->clients); free(client->userinfo);
  memmove(client, client + 1, (w->client_count - index - 1) * sizeof *client); w->client_count--;
}
static void clear_clients(world_t *w) { for (size_t i = 0; i < w->client_count; i++) free(w->clients[i].userinfo); w->client_count = 0; }
static qa_app_sim_classic_guest_client *require_client(world_t *w, double slot, const char *phase) {
  qa_app_sim_classic_guest_client *client = find_client(w, slot);
  if (client == NULL || (phase != NULL && strcmp(client->phase, phase) != 0))
    qa_raise("Error", 0, 0, "Native client %s is not %s", QA_JS_NUMBER(slot), phase != NULL ? phase : "connected");
  return client;
}

/* --- input retirements --- */
static void retire(void *ctx, double slot, qa_mod_client_identity identity) {
  world_t *w = ctx;
  qa_compat_q2_classic_edicts_retire_input_client(edicts_of(w), slot);
  for (size_t i = 0; i < w->retirement_count; i++) if (w->retirements[i].slot == slot) { w->retirements[i].identity = identity; return; }
  if (w->retirement_count == w->retirement_capacity) {
    size_t next = w->retirement_capacity == 0 ? 4 : w->retirement_capacity * 2;
    void *grown = realloc(w->retirements, next * sizeof *w->retirements); if (grown == NULL) qa_range_error("Out of memory");
    w->retirements = grown; w->retirement_capacity = next;
  }
  w->retirements[w->retirement_count].slot = slot; w->retirements[w->retirement_count++].identity = identity;
}
static void complete_retirement(world_t *w, size_t index) {
  retirement entry = w->retirements[index];
  memmove(&w->retirements[index], &w->retirements[index + 1], (w->retirement_count - index - 1) * sizeof *w->retirements); w->retirement_count--;
  delete_client(w, entry.slot);
  w->input_services.retired(w->input_services.ctx, entry.identity);
}
static void finish_input_retirements(world_t *w) {
  while (w->retirement_count > 0) {
    double slot = w->retirements[0].slot; qa_compat_q2_classic_guest_host *host = host_of(w);
    if (qa_compat_q2_view_i32(qa_compat_q2_classic_edicts_at(edicts_of(w), slot).bytes.bytes, 88) != 0) qa_compat_q2_classic_guest_host_client_event(host, "ClientDisconnect", slot);
    qa_compat_q2_classic_edicts_release_client(edicts_of(w), slot); qa_compat_q2_classic_edicts_finish_input_retirement(edicts_of(w), slot);
    complete_retirement(w, 0);
  }
}
static void discard_input_retirements(world_t *w) { while (w->retirement_count > 0) complete_retirement(w, 0); }
void qa_app_sim_classic_guest_world_bind_input(world_t *w, qa_compat_q2_native_input_services services) {
  if (w->input != NULL) { qa_compat_q2_native_input_binding_close(w->input); qa_compat_q2_native_input_binding_destroy(w->input); w->input = NULL; }
  w->input_services = services;
  w->input = qa_compat_q2_native_input_binding_create(qa_compat_q2_native_input_source_classic(host_of(w), w, retire), services);
}
static void close_input(world_t *w) {
  if (w->input != NULL) { qa_compat_q2_native_input_binding_close(w->input); qa_compat_q2_native_input_binding_destroy(w->input); w->input = NULL; }
}

/* --- command context --- */
static const char *const *context_command(void *ctx, size_t *out_count, const char **out_args) {
  retained *r = ctx;
  if (r->has_command) { *out_count = r->argument_count; *out_args = r->args; return (const char *const *)r->arguments; }
  return r->fallback.command(r->fallback.ctx, out_count, out_args);
}
static void clear_command(retained *r) {
  for (size_t i = 0; i < r->argument_count; i++) free(r->arguments[i]);
  free(r->arguments); free(r->args); r->arguments = NULL; r->argument_count = 0; r->args = NULL; r->has_command = false;
}
static void set_command(retained *r, const char *const *arguments, size_t count, const char *args) {
  clear_command(r);
  r->arguments = calloc(count == 0 ? 1 : count, sizeof *r->arguments); if (r->arguments == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) r->arguments[i] = dup_text(arguments[i]);
  r->argument_count = count; r->args = dup_text(args); r->has_command = true;
}

/* --- lifecycle --- */
static world_t *new_world(retained *shared) {
  world_t *w = calloc(1, sizeof *w); if (w == NULL) qa_range_error("Out of memory");
  w->shared = shared; shared->references++; return w;
}
typedef struct services_build { const qa_app_sim_classic_guest_world_options *options; retained *shared; } services_build;
static void build_services(void *ctx, qa_compat_q2_memory *memory, qa_compat_q2_classic_engine_services *out) {
  services_build *b = ctx; const qa_app_sim_prepared_classic_guest *prepared = b->options->prepared;
  qa_app_sim_classic_guest_services_options options = b->options->services;
  options.has_primary_world = true;
  if (prepared->has_primary) options.primary_world = prepared->primary.profile.world;
  else {
    b->shared->has_world_profile = qa_compat_q2_classic_get_primary_world_profile(((const qa_resolved_resource_reference *)prepared->execution.artifact)->digest, &b->shared->world_profile);
    options.primary_world = b->shared->has_world_profile ? &b->shared->world_profile : NULL;
  }
  if (prepared->has_primary) options.pickup_profile = prepared->primary.profile.pickups;
  b->shared->fallback = options.command;
  options.command.ctx = b->shared; options.command.command = context_command;
  b->shared->services = qa_app_sim_classic_guest_services_create(memory, &options);
  if (b->shared->services == NULL) qa_range_error("Out of memory");
  *out = qa_app_sim_classic_guest_services_engine(b->shared->services);
}
world_t *qa_app_sim_classic_guest_world_create(const qa_app_sim_classic_guest_world_options *options) {
  retained *shared = calloc(1, sizeof *shared); if (shared == NULL) qa_range_error("Out of memory");
  services_build build = {options, shared};
  qa_app_sim_classic_guest_source_options source; memset(&source, 0, sizeof source);
  source.capabilities = options->capabilities; source.has_instruction_budget = options->has_instruction_budget; source.instruction_budget = options->instruction_budget;
  source.services_ctx = &build; source.services = build_services;
  shared->source = qa_app_sim_classic_guest_source_create(options->prepared, &source);
  if (shared->services == NULL) { qa_app_sim_classic_guest_source_discard(shared->source); fail("Native services were not constructed"); }
  qa_guest_address image = qa_app_sim_classic_guest_source_image_base(shared->source);
  qa_app_sim_classic_guest_services_bind_host(shared->services, qa_app_sim_classic_guest_source_host(shared->source), &image);
  return new_world(shared);
}
static void release_shared(retained *r) {
  if (--r->references > 0) return;
  qa_app_sim_classic_guest_source_destroy(r->source);
  qa_app_sim_classic_guest_services_destroy(r->services);
  clear_command(r); free(r);
}
void qa_app_sim_classic_guest_world_destroy(world_t *w) {
  if (w == NULL) return;
  close_input(w); clear_clients(w);
  free(w->clients); free(w->retirements); free(w->states); free(w->connect_userinfo);
  release_shared(w->shared); free(w);
}
bool qa_app_sim_classic_guest_world_is_retired(const world_t *w) { return w->phase == TRANSFERRED || w->phase == CLOSED; }
qa_app_sim_classic_guest_source *qa_app_sim_classic_guest_world_source(world_t *w) { require_ownership(w); return w->shared->source; }
qa_app_sim_classic_guest_services *qa_app_sim_classic_guest_world_services(world_t *w) { require_ownership(w); return w->shared->services; }
qa_module_identity qa_app_sim_classic_guest_world_module(world_t *w) { return qa_app_sim_classic_guest_source_memory(qa_app_sim_classic_guest_world_source(w))->module; }
const qa_app_sim_classic_guest_client *qa_app_sim_classic_guest_world_clients(world_t *w, size_t *out_count) { require_ownership(w); *out_count = w->client_count; return w->clients; }

/* TS operation(run): one external call at a time; input retirements finish after it. */
static void clear_busy(void *arg) { ((world_t *)arg)->busy = false; }
typedef struct operation_scope { world_t *w; qa_cleanup node; } operation_scope;
static void begin_operation(world_t *w, operation_scope *scope) {
  require_ownership(w);
  if (w->phase == CLOSED) fail("Native world is closed");
  if (w->busy) fail("External native world operation is already active");
  w->busy = true; scope->w = w; qa_cleanup_push(&scope->node, clear_busy, w);
}
static void end_operation(operation_scope *scope) { finish_input_retirements(scope->w); qa_cleanup_pop(&scope->node); scope->w->busy = false; }
static void require_running(world_t *w) { if (w->phase != RUNNING) fail("Native world has not spawned"); }

void qa_app_sim_classic_guest_world_init(world_t *w) {
  operation_scope scope; begin_operation(w, &scope);
  if (w->phase != CREATED) fail("Native Init requires a fresh world");
  qa_app_sim_classic_guest_source_init(w->shared->source); w->phase = INITIALIZED;
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_init_loading(world_t *w, void (*next_frame)(void *), void *next_ctx) {
  operation_scope scope; begin_operation(w, &scope);
  if (w->phase != CREATED) fail("Native Init requires a fresh world");
  qa_app_sim_classic_guest_source_init_loading(w->shared->source, next_frame, next_ctx); w->phase = INITIALIZED;
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_spawn(world_t *w, const char *map, const char *entities, const char *spawn_point) {
  operation_scope scope; begin_operation(w, &scope);
  if (w->phase != INITIALIZED) fail("Native SpawnEntities requires an initialized candidate");
  qa_compat_q2_classic_guest_host *host = host_of(w);
  qa_compat_q2_classic_guest_host_spawn_entities(host, map, entities, spawn_point != NULL ? spawn_point : "");
  qa_compat_q2_classic_guest_host_run_frame(host); qa_compat_q2_classic_guest_host_run_frame(host);
  qa_app_sim_classic_guest_services_complete_spawn(w->shared->services); w->phase = RUNNING;
  end_operation(&scope);
}
static void run_frame_loading(world_t *w, void (*next_frame)(void *), void *next_ctx) {
  (void)qa_compat_q2_classic_guest_host_call_loading(host_of(w), "RunFrame", NULL, 0, next_frame, next_ctx, budget_of(w));
}
void qa_app_sim_classic_guest_world_spawn_loading(world_t *w, const char *map, const char *entities, void (*next_frame)(void *), void *next_ctx, const char *spawn_point) {
  require_ownership(w);
  if (w->phase != INITIALIZED || w->busy) fail("Native loading requires an idle initialized world");
  w->busy = true; qa_cleanup node; qa_cleanup_push(&node, clear_busy, w);
  qa_compat_q2_classic_guest_host_spawn_entities_loading(host_of(w), map, entities, spawn_point != NULL ? spawn_point : "", next_frame, next_ctx);
  run_frame_loading(w, next_frame, next_ctx); run_frame_loading(w, next_frame, next_ctx);
  qa_app_sim_classic_guest_services_complete_spawn(w->shared->services); w->phase = RUNNING;
  qa_cleanup_pop(&node); w->busy = false;
}
void qa_app_sim_classic_guest_world_write_travel_level(world_t *w, const char *path) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  qa_compat_q2_classic_guest_host_write_travel_level(host_of(w), path, service_options(w)->max_clients);
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_write_travel_level_loading(world_t *w, const char *path, void (*next_frame)(void *), void *next_ctx) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  qa_compat_q2_classic_guest_host_write_travel_level_loading(host_of(w), path, service_options(w)->max_clients, next_frame, next_ctx);
  end_operation(&scope);
}

typedef struct travel_call { world_t *from, *next; qa_app_sim_classic_guest_map map; const qa_app_sim_classic_guest_map_services *binding; const qa_app_sim_classic_guest_revisit *revisit;
  void (*next_frame)(void *); void *next_ctx; qa_app_sim_classic_guest_client *clients; size_t client_count; } travel_call;
static void run_travel(void *ctx) {
  travel_call *t = ctx; world_t *next = t->next; retained *shared = next->shared;
  qa_compat_q2_classic_guest_host *host = qa_app_sim_classic_guest_source_host(shared->source);
  qa_app_sim_classic_guest_services *services = shared->services; const qa_app_sim_classic_guest_services_options *options = qa_app_sim_classic_guest_services_options_of(services);
  size_t count = 0; const qa_owned_actor *owned = qa_session_actor_registry_owned_by(options->engine->actors, qa_compat_q2_classic_guest_host_get_options(host)->provider, &count);
  qa_owned_actor *copy = calloc(count == 0 ? 1 : count, sizeof *copy); if (copy == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(copy, owned, count * sizeof *copy);
  for (size_t i = 0; i < count; i++) qa_session_actor_registry_release(options->engine->actors, copy[i]);
  free(copy);
  qa_app_sim_classic_guest_map_services binding = *t->binding;
  shared->fallback = t->binding->command; binding.command.ctx = shared; binding.command.command = context_command;
  qa_app_sim_classic_guest_services_rebind_world(services, &binding);
  qa_compat_q2_classic_guest_host_rebind_world(host);
  const char *spawn_point = t->map.spawn_point != NULL ? t->map.spawn_point : "";
  if (t->next_frame == NULL) {
    qa_compat_q2_classic_guest_host_spawn_entities(host, t->map.map, t->map.entities, spawn_point);
    qa_compat_q2_classic_guest_host_run_frame(host); qa_compat_q2_classic_guest_host_run_frame(host);
  } else {
    qa_compat_q2_classic_guest_host_spawn_entities_loading(host, t->map.map, t->map.entities, spawn_point, t->next_frame, t->next_ctx);
    run_frame_loading(next, t->next_frame, t->next_ctx); run_frame_loading(next, t->next_frame, t->next_ctx);
  }
  if (t->revisit != NULL) {
    size_t drained; (void)qa_app_sim_classic_guest_services_drain_messages(services, &drained);
    t->revisit->restore_server_state(t->revisit->ctx, next);
    if (t->next_frame == NULL) {
      qa_compat_q2_classic_guest_host_save(host, "ReadLevel", t->revisit->level_path, false);
      for (int frame = 0; frame < 100; frame++) qa_compat_q2_classic_guest_host_run_frame(host);
    } else {
      qa_compat_q2_classic_guest_host_save_loading(host, "ReadLevel", t->revisit->level_path, t->next_frame, t->next_ctx);
      for (int frame = 0; frame < 100; frame++) { run_frame_loading(next, t->next_frame, t->next_ctx); t->next_frame(t->next_ctx); }
    }
  }
  for (size_t i = 0; i < t->client_count; i++) {
    (void)qa_compat_q2_classic_edicts_retain_client(qa_compat_q2_classic_guest_host_get_edicts(host), t->clients[i].slot);
    set_client(next, t->clients[i].slot, "connected", t->clients[i].userinfo);
  }
  qa_app_sim_classic_guest_services_complete_spawn(services); next->phase = RUNNING;
}
static world_t *travel(world_t *w, qa_app_sim_classic_guest_map map, const qa_app_sim_classic_guest_map_services *binding,
                       void (*next_frame)(void *), void *next_ctx, const qa_app_sim_classic_guest_revisit *revisit) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  qa_app_sim_classic_guest_services_validate_map(w->shared->services, binding);
  if (revisit != NULL && qa_cvar_registry_variable_value(service_options(w)->cvars, "deathmatch") != 0) fail("Original API 3 deathmatch does not restore hub levels");
  finish_input_retirements(w);
  travel_call t; memset(&t, 0, sizeof t);
  t.from = w; t.map = map; t.binding = binding; t.revisit = revisit; t.next_frame = next_frame; t.next_ctx = next_ctx;
  t.client_count = w->client_count; t.clients = w->clients; w->clients = NULL; w->client_count = 0; w->client_capacity = 0;
  world_t *next = new_world(w->shared);
  close_input(w);
  next->phase = INITIALIZED; w->phase = TRANSFERRED;
  t.next = next;
  qa_caught_error caught;
  bool completed = qa_try(run_travel, &t, &caught);
  for (size_t i = 0; i < t.client_count; i++) free(t.clients[i].userinfo);
  free(t.clients);
  if (!completed) {
    qa_app_sim_classic_guest_world_close(next); next->phase = CLOSED; clear_clients(next);
    qa_cleanup_pop(&scope.node); w->busy = false;
    qa_rethrow(&caught);
  }
  /* TS operation's finishInputRetirements runs on the transferred world, which has none. */
  qa_cleanup_pop(&scope.node); w->busy = false;
  return next;
}
world_t *qa_app_sim_classic_guest_world_travel(world_t *w, qa_app_sim_classic_guest_map map, const qa_app_sim_classic_guest_map_services *binding, const qa_app_sim_classic_guest_revisit *revisit) {
  return travel(w, map, binding, NULL, NULL, revisit);
}
world_t *qa_app_sim_classic_guest_world_travel_loading(world_t *w, qa_app_sim_classic_guest_map map, const qa_app_sim_classic_guest_map_services *binding,
                                                       void (*next_frame)(void *), void *next_ctx, const qa_app_sim_classic_guest_revisit *revisit) {
  return travel(w, map, binding, next_frame, next_ctx, revisit);
}

/* --- clients --- */
qa_app_sim_classic_guest_connect qa_app_sim_classic_guest_world_connect(world_t *w, double slot, const char *userinfo) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  if (slot != floor(slot) || slot < 1 || slot > service_options(w)->max_clients || find_client(w, slot) != NULL) qa_range_error("Native client slot is unavailable");
  qa_compat_q2_classic_client_connect result = qa_compat_q2_classic_guest_host_client_connect(host_of(w), slot, userinfo);
  if (result.allowed) { (void)qa_compat_q2_classic_edicts_retain_client(edicts_of(w), slot); set_client(w, slot, "connected", result.userinfo); }
  free(w->connect_userinfo); w->connect_userinfo = result.userinfo;
  end_operation(&scope);
  qa_app_sim_classic_guest_connect out = {result.allowed, w->connect_userinfo}; return out;
}
void qa_app_sim_classic_guest_world_begin(world_t *w, double slot) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  qa_app_sim_classic_guest_client *client = require_client(w, slot, "connected");
  (void)client; qa_compat_q2_classic_guest_host_client_event(host_of(w), "ClientBegin", slot);
  client = find_client(w, slot); if (client != NULL) client->phase = "active";
  end_operation(&scope);
}
qa_app_sim_classic_guest_connect qa_app_sim_classic_guest_world_admit(world_t *w, double slot, const char *userinfo) {
  qa_app_sim_classic_guest_connect result = qa_app_sim_classic_guest_world_connect(w, slot, userinfo);
  if (result.allowed) qa_app_sim_classic_guest_world_begin(w, slot);
  return result;
}
void qa_app_sim_classic_guest_world_disconnect(world_t *w, double slot) {
  operation_scope scope; begin_operation(w, &scope); require_running(w); (void)require_client(w, slot, NULL);
  qa_compat_q2_classic_guest_host_client_event(host_of(w), "ClientDisconnect", slot); qa_compat_q2_classic_edicts_release_client(edicts_of(w), slot); delete_client(w, slot);
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_userinfo(world_t *w, double slot, const char *value) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  qa_app_sim_classic_guest_client *client = require_client(w, slot, NULL); const char *phase = client->phase;
  char *userinfo = qa_compat_q2_classic_guest_host_client_userinfo_changed(host_of(w), slot, value);
  set_client(w, slot, phase, userinfo); free(userinfo);
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_set_userinfo_storage(world_t *w, double slot, const char *value) {
  require_running(w); qa_app_sim_classic_guest_client *client = require_client(w, slot, NULL);
  char *fitted = fit_userinfo(value); set_client(w, slot, client->phase, fitted); free(fitted);
}
static void clear_command_arg(void *arg) { clear_command(arg); }
void qa_app_sim_classic_guest_world_command(world_t *w, double slot, const char *const *arguments, size_t count, const char *args) {
  operation_scope scope; begin_operation(w, &scope); require_running(w); (void)require_client(w, slot, "active");
  set_command(w->shared, arguments, count, args);
  qa_cleanup node; qa_cleanup_push(&node, clear_command_arg, w->shared);
  qa_compat_q2_classic_guest_host_client_event(host_of(w), "ClientCommand", slot);
  qa_cleanup_pop(&node); clear_command(w->shared);
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_server_command(world_t *w, const char *const *arguments, size_t count, const char *args) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  set_command(w->shared, arguments, count, args);
  qa_cleanup node; qa_cleanup_push(&node, clear_command_arg, w->shared);
  (void)qa_compat_q2_classic_guest_host_call(host_of(w), "ServerCommand", NULL, 0, NULL, budget_of(w));
  qa_cleanup_pop(&node); clear_command(w->shared);
  end_operation(&scope);
}
typedef struct think_call { world_t *w; double slot; uint8_t command[16]; } think_call;
static void run_think(void *ctx) { think_call *t = ctx; qa_compat_q2_classic_guest_host_client_think(host_of(t->w), t->slot, t->command, 16); }
void qa_app_sim_classic_guest_world_think(world_t *w, double slot, qa_q2_user_command command, const qa_equipment_movement *movement) {
  operation_scope scope; begin_operation(w, &scope); require_running(w); (void)require_client(w, slot, "active");
  think_call t; t.w = w; t.slot = slot; qa_app_sim_classic_guest_user_command(command, t.command);
  qa_app_sim_classic_guest_services_with_player_movement(w->shared->services, movement, run_think, &t);
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_frame(world_t *w, double milliseconds) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  if (milliseconds != 100) qa_range_error("API 3 RunFrame requires the source 100ms cadence");
  qa_app_sim_classic_guest_services_sync_monster_proxies(w->shared->services);
  qa_compat_q2_classic_guest_host_run_frame(host_of(w));
  qa_app_sim_classic_guest_services_forward_monster_proxy_damage(w->shared->services);
  qa_app_sim_classic_guest_services_publish_entities(w->shared->services);
  end_operation(&scope);
}
double qa_app_sim_classic_guest_world_allocate_monster_proxy(world_t *w) { return qa_app_sim_classic_guest_services_allocate_monster_proxy(qa_app_sim_classic_guest_world_services(w)); }
void qa_app_sim_classic_guest_world_bind_monster_proxy(world_t *w, double slot, qa_owned_actor actor) { qa_app_sim_classic_guest_services_bind_monster_proxy(qa_app_sim_classic_guest_world_services(w), slot, actor); }
bool qa_app_sim_classic_guest_world_actor(world_t *w, double slot, qa_actor_id *out) {
  require_running(w); qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(edicts_of(w), slot);
  qa_actor_id_result actor = record.current_actor(record.ctx); if (actor.has_value) *out = actor.value; return actor.has_value;
}
qa_app_sim_classic_guest_entity_info qa_app_sim_classic_guest_world_entity_info(world_t *w, double slot) {
  require_running(w);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(edicts_of(w), slot); const uint8_t *view = record.bytes.bytes;
  int32_t count = qa_compat_q2_view_i32(view, 104);
  if (count < -1 || count > 16) qa_range_error("API 3 edict has an invalid visibility cluster count");
  qa_compat_q2_memory *memory = qa_app_sim_classic_guest_source_memory(w->shared->source);
  qa_app_sim_classic_guest_entity_info info; memset(&info, 0, sizeof info);
  qa_actor_id_result actor = record.current_actor(record.ctx); info.has_actor = actor.has_value; if (actor.has_value) info.actor = actor.value;
  info.active = qa_compat_q2_view_i32(view, 88) != 0; info.server_flags = qa_compat_q2_view_i32(view, 184);
  info.areas[0] = qa_compat_q2_view_i32(view, 176); info.areas[1] = qa_compat_q2_view_i32(view, 180);
  info.clusters_null = count == -1;
  for (int32_t i = 0; i < count; i++) info.clusters[i] = qa_compat_q2_view_i32(view, 108 + (size_t)i * 4);
  info.cluster_count = count < 0 ? 0 : (size_t)count;
  info.first_cluster = qa_compat_q2_view_i32(view, 108); info.headnode = qa_compat_q2_view_i32(view, 172);
  qa_guest_address owner;
  if (qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record.address, 256), &owner)) { info.has_owner_slot = true; info.owner_slot = qa_compat_q2_classic_edicts_from_pointer(edicts_of(w), owner).slot; }
  return info;
}
const qa_q2_entity_state *qa_app_sim_classic_guest_world_entity_states(world_t *w, size_t *out_count) {
  require_running(w); size_t count = 0; double total = qa_compat_q2_classic_edicts_descriptor(edicts_of(w)).count;
  for (double slot = 1; slot < total; slot++) {
    uint8_t *view = (uint8_t *)qa_compat_q2_classic_edicts_at(edicts_of(w), slot).bytes.bytes;
    if (qa_compat_q2_view_i32(view, 88) != 0 && (qa_compat_q2_view_i32(view, 184) & 1) == 0) {
      if (qa_compat_q2_view_i32(view, 0) != (int32_t)slot) qa_compat_q2_view_set_i32(view, 0, (int32_t)slot);
      if (count == w->state_capacity) {
        size_t next = w->state_capacity == 0 ? 64 : w->state_capacity * 2;
        void *grown = realloc(w->states, next * sizeof *w->states); if (grown == NULL) qa_range_error("Out of memory");
        w->states = grown; w->state_capacity = next;
      }
      w->states[count++] = qa_app_sim_classic_guest_services_entity_state(w->shared->services, slot);
    }
  }
  *out_count = count; return w->states;
}
qa_q2_entity_state qa_app_sim_classic_guest_world_entity_state(world_t *w, double slot) { require_running(w); return qa_app_sim_classic_guest_services_entity_state(w->shared->services, slot); }
double qa_app_sim_classic_guest_world_player_ping(world_t *w, double slot) {
  require_running(w); const uint8_t *client = qa_compat_q2_classic_edicts_client_prefix(edicts_of(w), slot);
  if (client == NULL) fail("Native slot has no public client prefix");
  return qa_compat_q2_view_i32(client, 184);
}
void qa_app_sim_classic_guest_world_set_player_ping(world_t *w, double slot, double ping) {
  operation_scope scope; begin_operation(w, &scope); require_running(w); (void)require_client(w, slot, NULL);
  qa_compat_q2_classic_edicts_set_client_ping(edicts_of(w), slot, ping);
  end_operation(&scope);
}
qa_q2_player_state qa_app_sim_classic_guest_world_player_state(world_t *w, double slot) { require_running(w); return qa_app_sim_classic_guest_services_player_state(w->shared->services, slot); }
qa_app_sim_model_appearance qa_app_sim_classic_guest_world_model_appearance(world_t *w, double slot) { require_running(w); return qa_app_sim_classic_guest_services_model_appearance(w->shared->services, slot); }
const qa_app_sim_classic_guest_message *qa_app_sim_classic_guest_world_raw_messages(world_t *w, size_t *out_count) { return qa_app_sim_classic_guest_services_drain_messages(qa_app_sim_classic_guest_world_services(w), out_count); }
const qa_app_sim_configstring *qa_app_sim_classic_guest_world_configstrings(world_t *w, size_t *out_count) { return qa_app_sim_classic_guest_services_configstrings(qa_app_sim_classic_guest_world_services(w), out_count); }
void qa_app_sim_classic_guest_world_restore_configstrings(world_t *w, const qa_app_sim_configstring *values, size_t count) {
  if (w->phase != INITIALIZED) fail("Native configstring restoration requires the initialized candidate");
  qa_app_sim_classic_guest_services *services = qa_app_sim_classic_guest_world_services(w);
  size_t current = 0; const qa_app_sim_configstring *table = qa_app_sim_classic_guest_services_configstrings(services, &current);
  double *clear = calloc(current == 0 ? 1 : current, sizeof *clear); size_t clear_count = 0; if (clear == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < current; i++) {
    bool kept = false; for (size_t j = 0; j < count && !kept; j++) kept = values[j].index == table[i].index;
    if (!kept) clear[clear_count++] = table[i].index;
  }
  for (size_t i = 0; i < clear_count; i++) qa_app_sim_classic_guest_services_set_configstring(services, clear[i], "");
  free(clear);
  for (size_t i = 0; i < count; i++) qa_app_sim_classic_guest_services_set_configstring(services, values[i].index, values[i].value);
}
void qa_app_sim_classic_guest_world_write_original(world_t *w, const char *game_path, const char *level_path, bool autosave) {
  operation_scope scope; begin_operation(w, &scope); require_running(w);
  qa_compat_q2_classic_guest_host_save(host_of(w), "WriteGame", game_path, autosave); qa_compat_q2_classic_guest_host_save(host_of(w), "WriteLevel", level_path, false);
  end_operation(&scope);
}
static void restore_original(world_t *w, const char *game_path, const char *level_path, qa_app_sim_classic_guest_map map,
                             void (*restore_server_state)(void *), void *restore_ctx, void (*next_frame)(void *), void *next_ctx) {
  operation_scope scope; begin_operation(w, &scope);
  if (w->phase != INITIALIZED) fail("Original API 3 import requires a fresh initialized candidate");
  qa_compat_q2_classic_guest_host *host = host_of(w); const char *spawn_point = map.spawn_point != NULL ? map.spawn_point : "";
  if (next_frame == NULL) {
    qa_compat_q2_classic_guest_host_save(host, "ReadGame", game_path, false);
    qa_compat_q2_classic_guest_host_spawn_entities(host, map.map, map.entities, spawn_point);
    qa_compat_q2_classic_guest_host_run_frame(host); qa_compat_q2_classic_guest_host_run_frame(host);
  } else {
    qa_compat_q2_classic_guest_host_save_loading(host, "ReadGame", game_path, next_frame, next_ctx);
    qa_compat_q2_classic_guest_host_spawn_entities_loading(host, map.map, map.entities, spawn_point, next_frame, next_ctx);
    run_frame_loading(w, next_frame, next_ctx); run_frame_loading(w, next_frame, next_ctx);
  }
  size_t drained; (void)qa_app_sim_classic_guest_services_drain_messages(w->shared->services, &drained);
  restore_server_state(restore_ctx);
  if (next_frame == NULL) qa_compat_q2_classic_guest_host_save(host, "ReadLevel", level_path, false);
  else qa_compat_q2_classic_guest_host_save_loading(host, "ReadLevel", level_path, next_frame, next_ctx);
  qa_app_sim_classic_guest_services_complete_spawn(w->shared->services); w->phase = RUNNING;
  end_operation(&scope);
}
void qa_app_sim_classic_guest_world_restore_original(world_t *w, const char *game_path, const char *level_path, qa_app_sim_classic_guest_map map, void (*restore_server_state)(void *), void *restore_ctx) {
  restore_original(w, game_path, level_path, map, restore_server_state, restore_ctx, NULL, NULL);
}
void qa_app_sim_classic_guest_world_restore_original_loading(world_t *w, const char *game_path, const char *level_path, qa_app_sim_classic_guest_map map,
                                                             void (*restore_server_state)(void *), void *restore_ctx, void (*next_frame)(void *), void *next_ctx) {
  restore_original(w, game_path, level_path, map, restore_server_state, restore_ctx, next_frame, next_ctx);
}
typedef struct retire_call { world_t *w; bool discard; } retire_call;
static void run_retire(void *ctx) {
  retire_call *c = ctx;
  if (c->discard) qa_app_sim_classic_guest_source_discard(c->w->shared->source); else qa_app_sim_classic_guest_source_close(c->w->shared->source);
}
static void retire_world(world_t *w, bool discard) {
  if (qa_app_sim_classic_guest_world_is_retired(w)) return;
  close_input(w);
  operation_scope scope; begin_operation(w, &scope);
  discard_input_retirements(w);
  retire_call call = {w, discard}; qa_caught_error caught;
  bool completed = qa_try(run_retire, &call, &caught);
  w->phase = CLOSED; clear_clients(w);
  if (!completed) { qa_cleanup_pop(&scope.node); w->busy = false; qa_rethrow(&caught); }
  qa_cleanup_pop(&scope.node); w->busy = false;
}
void qa_app_sim_classic_guest_world_close(world_t *w) { retire_world(w, false); }
void qa_app_sim_classic_guest_world_discard(world_t *w) { retire_world(w, true); }

void qa_app_sim_classic_guest_user_command(qa_q2_user_command command, uint8_t out[16]) {
  memset(out, 0, 16);
  out[0] = qa_js_to_uint8(command.milliseconds); out[1] = qa_js_to_uint8(command.buttons);
  for (int i = 0; i < 3; i++) { uint16_t v = (uint16_t)qa_js_to_int16(command.angle_shorts.values[i]); out[2 + i * 2] = (uint8_t)v; out[3 + i * 2] = (uint8_t)(v >> 8); }
  uint16_t forward = (uint16_t)qa_js_to_int16(command.forward_move), side = (uint16_t)qa_js_to_int16(command.side_move), up = (uint16_t)qa_js_to_int16(command.up_move);
  out[8] = (uint8_t)forward; out[9] = (uint8_t)(forward >> 8); out[10] = (uint8_t)side; out[11] = (uint8_t)(side >> 8); out[12] = (uint8_t)up; out[13] = (uint8_t)(up >> 8);
  out[14] = qa_js_to_uint8(command.impulse); out[15] = qa_js_to_uint8(command.light_level);
}
