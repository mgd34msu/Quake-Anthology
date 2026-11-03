/* Port of src/app/bootstrap/simulation/native-mod.ts. TS async members run
 * synchronously; see the header for ownership. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q2_localization.h"
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/compat/q2/native_mod_provider.h"
#include "qa/foundation/support.h"
#include "qa/guest/pe/format.h"
#include "qa/network/common/session.h"
#include "qa/text/localization_resources.h"
#include "qa/world/session/mod_commands.h"

typedef struct prepared_ctx {
  qa_app_sim_prepare_native_mod_options options;
  char *instance;
  qa_provider_reference source;
  qa_module_identity modules[1];
  qa_provider_checkpoint_ref providers[1];
  qa_resolved_resource_reference artifact;
  char *artifact_id, *artifact_path, *artifact_digest;
  bool rerelease;
  qa_app_sim_prepared_classic_guest classic;
  qa_app_sim_prepared_rerelease_guest rerelease_guest;
  /* prepareMountedNativeMod */
  qa_content_mounted *mounts;
  bool has_localization; qa_localization_table localization;
  char **scripts; size_t script_count;
} prepared_ctx;

typedef struct runtime_ctx {
  prepared_ctx *p;
  const qa_mod_host_services *services;
  qa_compat_q2_native_mod_provider *provider;
  qa_compat_q2_native_mod_host *host;
  qa_mod_command_port *commands;
  qa_resource_scope *resources;
  void *init_ctx; void (*next_frame)(void *); void (*assert_current)(void *);
  qa_provider_checkpoint saved; qa_provider_checkpoint saved_rows[1];
} runtime_ctx;

static char *xstrdup(const char *text) { size_t n = strlen(text) + 1; char *p = malloc(n); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n); return p; }
static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* --- runtime --- */
static bool p_project(void *ctx, qa_raw_entity_view record, qa_owned_actor *out) { return qa_compat_q2_native_mod_provider_project(((runtime_ctx *)ctx)->provider, record, out); }
static bool p_actor_at(void *ctx, double slot, qa_actor_id *out) { return qa_compat_q2_native_mod_provider_actor_at(((runtime_ctx *)ctx)->provider, slot, out); }
static bool p_slot_of(void *ctx, qa_actor_id actor, double *out) { return qa_compat_q2_native_mod_provider_slot_of(((runtime_ctx *)ctx)->provider, actor, out); }
static bool p_accepts_client(void *ctx, double slot) { return qa_compat_q2_native_mod_provider_accepts_client(((runtime_ctx *)ctx)->provider, slot); }
static qa_guest_address p_address(void *ctx, qa_actor_id actor) { return qa_compat_q2_native_mod_provider_address(((runtime_ctx *)ctx)->provider, actor); }
static qa_guest_call_result p_import_boundary(void *ctx, const char *name, const qa_guest_call_value *values, size_t count, void *invoke_ctx,
                                              qa_guest_call_result (*invoke)(void *, const qa_guest_call_value *, size_t)) {
  return qa_compat_q2_native_mod_provider_import_boundary(((runtime_ctx *)ctx)->provider, name, values, count, invoke_ctx, invoke);
}
static char *host_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  runtime_ctx *r = ctx; return r->p->options.localize(r->p->options.ctx, key, arguments, count);
}
static void host_next_frame(void *ctx) { runtime_ctx *r = ctx; r->next_frame(r->init_ctx); }
static bool command_invoke(void *ctx, qa_command_invocation *command) { return qa_compat_q2_native_mod_provider_invoke_command(((runtime_ctx *)ctx)->provider, command); }
static const char *command_read_script(void *ctx, const char *name) { runtime_ctx *r = ctx; return r->p->options.read_script(r->p->options.ctx, name); }
static qa_mod_command_port *bind_commands(void *ctx, qa_cvar_registry *cvars) {
  runtime_ctx *r = ctx; prepared_ctx *p = r->p;
  qa_mod_command_binding binding; memset(&binding, 0, sizeof binding);
  binding.ctx = r; binding.selection = p->options.description.selection; binding.module = p->modules[0]; binding.cvars = cvars;
  binding.invoke = command_invoke; if (p->options.read_script != NULL) binding.read_script = command_read_script;
  r->commands = qa_mod_commands_bind(r->services->commands, binding, r->resources);
  return r->commands;
}
static void runtime_free(void *ctx) {
  runtime_ctx *r = ctx;
  qa_compat_q2_native_mod_provider_destroy(r->provider);
  qa_app_sim_native_mod_host_destroy(r->host);
  /* r->commands is borrowed: the mod scope (resources) owns the command port, as in TS. */
  free((void *)r->saved.bytes.bytes); free(r);
}
static void provider_close(void *ctx) { qa_compat_q2_native_mod_provider_close(((runtime_ctx *)ctx)->provider); }
static void r_activate(void *ctx) { qa_compat_q2_native_mod_provider_activate_protection(((runtime_ctx *)ctx)->provider); }
static qa_source_time callback_time(void *ctx) { runtime_ctx *r = ctx; return r->services->time(r->services->ctx); }
static bool callback_execute(void *ctx, const void *callback, const qa_app_sim_mod_callback_inputs *inputs, double *out) {
  runtime_ctx *r = ctx; qa_compat_q2_native_pickup_input rows[QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY];
  for (size_t i = 0; i < inputs->count; i++) { rows[i].name = inputs->names[i]; rows[i].value = inputs->values[i]; }
  qa_compat_q2_native_pickup_inputs view = {rows, inputs->count};
  return qa_compat_q2_native_mod_provider_invoke(r->provider, &((const qa_native_mod_callback *)callback)->call, view, out);
}
static void r_register(void *ctx, qa_mod_registrations *registrations) {
  runtime_ctx *r = ctx; const qa_native_mod_declaration *d = r->p->options.declaration;
  qa_app_sim_mod_callback_host host = {r, callback_time, callback_execute};
  qa_app_sim_register_mod_callbacks(d->callbacks, d->callback_count, sizeof *d->callbacks, offsetof(qa_native_mod_callback, binding), registrations, host);
}
static qa_mod_private_checkpoint r_checkpoint(void *ctx) {
  runtime_ctx *r = ctx; qa_provider_checkpoint record = qa_compat_q2_native_mod_provider_checkpoint(r->provider);
  free((void *)r->saved.bytes.bytes); r->saved = record; r->saved_rows[0] = record;
  qa_mod_private_checkpoint state; memset(&state, 0, sizeof state); state.providers = r->saved_rows; state.provider_count = 1; return state;
}
static void r_restore(void *ctx, qa_mod_private_checkpoint state) {
  runtime_ctx *r = ctx; if (state.provider_count == 0) qa_raise("Error", 0, 0, "Missing native gameplay mod checkpoint");
  qa_compat_q2_native_mod_provider_restore(r->provider, &state.providers[0]);
}
static bool r_client_presentation(void *ctx, qa_mod_client_presentation_source *out) { return qa_compat_q2_native_mod_provider_client_presentation(((runtime_ctx *)ctx)->provider, out); }
static void r_advance(void *ctx, qa_frame_context frame) { qa_compat_q2_native_mod_provider_advance(((runtime_ctx *)ctx)->provider, frame); }
static const qa_app_simulation_presentation *r_presentations(void *ctx, size_t *n) { return qa_compat_q2_native_mod_provider_presentations(((runtime_ctx *)ctx)->provider, n); }
static const qa_app_simulation_presentation *r_appearance_overrides(void *ctx, size_t *n) { return qa_compat_q2_native_mod_provider_appearance_overrides(((runtime_ctx *)ctx)->provider, n); }
static void r_close(void *ctx) { qa_compat_q2_native_mod_provider_close(((runtime_ctx *)ctx)->provider); }

static qa_mod_runtime initialize_mod(void *ctx, qa_mod_initialization *context) {
  prepared_ctx *p = ctx; const qa_mod_host_services *services = context->services;
  if (services == NULL || services->native == NULL) qa_raise("Error", 0, 0, "Native gameplay mods require the destination's native engine context");
  const qa_app_sim_native_mod_host_context *native = services->native;
  runtime_ctx *r = calloc(1, sizeof *r); if (r == NULL) qa_range_error("Out of memory");
  r->p = p; r->services = services; r->resources = context->resources; r->init_ctx = context->ctx; r->next_frame = context->next_frame; r->assert_current = context->assert_current;
  r->provider = qa_compat_q2_native_mod_provider_create(p->options.declaration, services, p->instance, native->map_path, context->ctx, context->assert_current);
  /* The scope frees the runtime after it closes the provider (and the command port bound below). */
  qa_resource_scope_defer(context->resources, runtime_free, r);
  qa_session_resource resource = {r, provider_close}; (void)qa_resource_scope_own(context->resources, resource);
  qa_compat_q2_native_mod_provider_reserve_protection(r->provider);
  qa_app_sim_native_mod_host_options options; memset(&options, 0, sizeof options);
  if (p->rerelease) options.rerelease = &p->rerelease_guest; else options.classic = &p->classic;
  options.declaration = p->options.declaration; options.source = p->source; options.context = native; options.services = services;
  options.projection.ctx = r; options.projection.project = p_project; options.projection.actor_at = p_actor_at; options.projection.slot_of = p_slot_of;
  options.projection.accepts_client = p_accepts_client; options.projection.address = p_address; options.projection.import_boundary = p_import_boundary;
  options.ctx = r; options.localize = host_localize; options.next_frame = host_next_frame;
  if (services->commands != NULL) options.bind_commands = bind_commands;
  r->host = qa_app_sim_create_native_mod_host(&options);
  qa_compat_q2_native_mod_provider_attach(r->provider, r->host);
  qa_compat_q2_native_mod_provider_initialize(r->provider, context->restoring);
  context->assert_current(context->ctx);
  qa_mod_runtime runtime; memset(&runtime, 0, sizeof runtime);
  runtime.ctx = r; runtime.activate = r_activate; runtime.register_ = r_register; runtime.checkpoint = r_checkpoint; runtime.restore = r_restore;
  runtime.client_presentation = r_client_presentation; runtime.advance = r_advance; runtime.presentations = r_presentations;
  runtime.appearance_overrides = r_appearance_overrides; runtime.close = r_close;
  return runtime;
}
static void validate_state(void *ctx, qa_mod_private_checkpoint state) {
  prepared_ctx *p = ctx;
  if (state.guest_count != 0 || state.provider_count != 1) qa_raise("Error", 0, 0, "Missing native gameplay mod checkpoint");
  qa_compat_q2_validate_native_mod_checkpoint(&state.providers[0], p->modules[0], p->options.declaration);
}

/* --- prepareNativeMod --- */
static bool abi_matches(const qa_native_mod_declaration *declaration, const uint8_t *program, size_t length) {
  qa_pe_file pe; qa_parse_pe(&pe, program, length);
  bool classic = declaration->target.api.kind == QA_Q2_GAME_API_CLASSIC;
  bool matches = (same(pe.abi.kind, "windows-i386") && classic) || (same(pe.abi.kind, "windows-x86-64") && !classic);
  qa_pe_file_free(&pe); return matches;
}
static bool program_digest_matches(const uint8_t *program, size_t length, const char *digest) {
  char hex[65]; qa_net_sha256_hex(program, length, hex); char text[80]; snprintf(text, sizeof text, "sha256:%s", hex);
  return same(text, digest);
}
qa_prepared_mod qa_app_sim_prepare_native_mod(const qa_app_sim_prepare_native_mod_options *options) {
  const qa_native_mod_declaration *declaration = options->declaration;
  qa_compat_q2_validate_native_mod_declaration(declaration);
  if (!same(options->artifact->digest, declaration->program.digest) || !same(options->artifact->requested_path, declaration->program.path)
      || !program_digest_matches(options->program, options->program_length, declaration->program.digest)
      || !abi_matches(declaration, options->program, options->program_length)) qa_raise("Error", 0, 0, "Native mod artifact or ABI differs from its declaration");
  prepared_ctx *p = calloc(1, sizeof *p); if (p == NULL) qa_range_error("Out of memory");
  p->options = *options;
  p->instance = qa_mod_instance_provider(options->description.selection); if (p->instance == NULL) qa_range_error("Out of memory");
  p->source = options->description.source; p->source.provider = p->instance;
  p->modules[0].id = p->instance; p->modules[0].artifact_path = declaration->program.path;
  p->modules[0].digest = declaration->program.digest; p->modules[0].revision = declaration->program.digest;
  p->providers[0].provider = p->instance; p->providers[0].schema = "native:mod"; p->providers[0].version = 1;
  p->artifact = *options->artifact;
  p->artifact_id = xstrdup(options->artifact->id); p->artifact_path = xstrdup(options->artifact->requested_path); p->artifact_digest = xstrdup(options->artifact->digest);
  p->artifact.id = p->artifact_id; p->artifact.requested_path = p->artifact_path; p->artifact.digest = p->artifact_digest;
  qa_resolved_execution_module execution; memset(&execution, 0, sizeof execution);
  execution.kind = QA_EXECUTION_MODULE_NATIVE; execution.owner = p->source; execution.role = "server-game";
  execution.api.kind = QA_GAME_API_Q2_GAME; execution.api.as.q2_game = declaration->target.api; execution.profile = declaration->target.abi; execution.artifact = &p->artifact;
  uint8_t *bytes = malloc(options->program_length == 0 ? 1 : options->program_length); if (bytes == NULL) qa_range_error("Out of memory");
  memcpy(bytes, options->program, options->program_length);
  p->rerelease = declaration->target.api.kind != QA_Q2_GAME_API_CLASSIC;
  if (p->rerelease) { p->rerelease_guest.edition = "rerelease"; p->rerelease_guest.execution = execution; p->rerelease_guest.bytes = bytes; p->rerelease_guest.length = options->program_length; }
  else { p->classic.edition = "classic"; p->classic.execution = execution; p->classic.bytes = bytes; p->classic.length = options->program_length; }
  qa_prepared_mod out; memset(&out, 0, sizeof out);
  out.ctx = p; out.description = options->description;
  const qa_native_mod_client_presentation *presentation = declaration->client_presentation;
  if (presentation != NULL) {
    out.has_client_presentation = true;
    out.client_presentation.hud = same(presentation->hud, "none") ? "none" : same(presentation->hud, "layout-overlay") ? "overlay" : "replace";
    out.client_presentation.view = !same(presentation->view, "none");
  }
  out.module_checkpoint = "provider";
  out.identity.selection = options->description.selection; out.identity.source = options->description.source;
  out.identity.declaration_digest = options->declaration_digest;
  out.identity.modules = p->modules; out.identity.module_count = 1; out.identity.providers = p->providers; out.identity.provider_count = 1;
  out.validate_state = validate_state; out.initialize = initialize_mod;
  return out;
}

/* --- prepareMountedNativeMod --- */
static uint8_t *read_localization(void *ctx, const char *path, size_t *out_length) {
  qa_content_opened_resource resource; if (!qa_content_mounted_open(ctx, path, NULL, NULL, &resource)) return NULL;
  *out_length = resource.byte_length; return resource.bytes;
}
static const char *mounted_script(void *ctx, const char *name) {
  prepared_ctx *p = ctx; qa_content_opened_resource resource;
  if (!qa_content_mounted_open(p->mounts, name, NULL, NULL, &resource)) return NULL;
  char *text = malloc(resource.byte_length + 1); if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, resource.bytes, resource.byte_length); text[resource.byte_length] = '\0'; free(resource.bytes);
  char **grown = realloc(p->scripts, (p->script_count + 1) * sizeof *grown); if (grown == NULL) qa_range_error("Out of memory");
  p->scripts = grown; p->scripts[p->script_count++] = text; return text;
}
static char *mounted_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  prepared_ctx *p = ctx; char *text = qa_app_q2_localized_text(&p->localization, key, arguments, count); if (text == NULL) qa_range_error("Out of memory");
  return text;
}
qa_prepared_mod qa_app_sim_prepare_mounted_native_mod(qa_mod_description description, const qa_native_mod_declaration *declaration,
                                                      qa_content_digest declaration_digest, qa_content_mounted *mounts) {
  qa_content_opened_resource resource;
  bool opened = qa_content_mounted_open(mounts, declaration->program.path, NULL, NULL, &resource);
  if (!opened || !same(resource.reference->digest, declaration->program.digest)) {
    if (opened) free(resource.bytes);
    qa_raise("Error", 0, 0, "Selected native mod differs from its resolved artifact");
  }
  qa_localization_table localization;
  qa_load_server_localization_resources("english", read_localization, mounts, QA_LOC_Q2_RERELEASE, &localization);
  qa_app_sim_prepare_native_mod_options options; memset(&options, 0, sizeof options);
  options.description = description; options.declaration = declaration; options.declaration_digest = declaration_digest;
  options.program = resource.bytes; options.program_length = resource.byte_length; options.artifact = resource.reference;
  qa_prepared_mod prepared = qa_app_sim_prepare_native_mod(&options);
  free(resource.bytes);
  prepared_ctx *p = prepared.ctx;
  p->mounts = mounts; p->localization = localization; p->has_localization = true;
  p->options.ctx = p; p->options.read_script = mounted_script; p->options.localize = mounted_localize;
  p->options.program = NULL; p->options.program_length = 0; p->options.artifact = &p->artifact;
  return prepared;
}
void qa_app_sim_prepared_native_mod_free(qa_prepared_mod *prepared) {
  prepared_ctx *p = prepared->ctx; if (p == NULL) return;
  if (p->rerelease) qa_app_sim_prepared_rerelease_guest_free(&p->rerelease_guest); else qa_app_sim_prepared_classic_guest_free(&p->classic);
  if (p->has_localization) qa_localization_table_destroy(&p->localization);
  for (size_t i = 0; i < p->script_count; i++) free(p->scripts[i]);
  free(p->scripts); free(p->instance); free(p->artifact_id); free(p->artifact_path); free(p->artifact_digest); free(p);
  prepared->ctx = NULL;
}
