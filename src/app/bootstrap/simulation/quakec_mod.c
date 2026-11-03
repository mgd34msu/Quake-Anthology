/* Port of src/app/bootstrap/simulation/quakec-mod.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_mod.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
#include "qa/compat/qc/source_call.h"
#include "qa/world/session/mod_commands.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
typedef struct prepared_ctx {
  qa_app_sim_prepare_quakec_mod_options options;
  qa_compat_qc_program program;
  qa_module_identity module;
  qa_module_identity modules[1];
  char *instance_provider;
  /* prepareMountedQuakeCMod over MountedContent: owned program bytes, resources and media. */
  uint8_t *owned_program;
  bool has_resources; qa_app_sim_quakec_resources resources;
  qa_compat_qc_mod_media_resource *media_rows;
  qa_compat_qc_mod_media media;
} prepared_ctx;
typedef struct runtime_ctx {
  prepared_ctx *prepared;
  qa_compat_qc_mod_provider provider;
  qa_app_sim_source_random *random;
  qa_compat_qc_mod_random mod_random;
  qa_compat_qc_mod_media media;
  qa_mod_runtime runtime;
  /* TS initialize's context and services, which register() reads. */
  qa_mod_host_services *services;
  void *init_ctx;
  void (*assert_current)(void *ctx);
  /* The command binding borrows its names; readScript's last text stays until the next read. */
  const char **command_names;
  char *script;
} runtime_ctx;
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static double rnd_next_integer(void *ctx) { return qa_app_sim_source_random_next_integer(ctx); }
static double rnd_next_unit(void *ctx) { return qa_app_sim_source_random_next_unit(ctx); }
static qa_random_state rnd_checkpoint(void *ctx) {
  qa_app_sim_source_random_state s = qa_app_sim_source_random_checkpoint(ctx);
  qa_random_state out; memset(&out, 0, sizeof out);
  out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
  static double words[31];
  for (int i = 0; i < 31; i++) words[i] = s.words[i];
  out.words = (qa_double_view){words, 31};
  out.front = s.front; out.rear = s.rear; out.draws = s.draws;
  return out;
}
static void rnd_restore(void *ctx, qa_random_state state) {
  if (state.kind != QA_RANDOM_STATE_GLIBC_RANDOM) qa_range_error("Invalid QuakeC mod random state");
  qa_app_sim_source_random_state s; memset(&s, 0, sizeof s);
  s.kind = "glibc-random";
  for (size_t i = 0; i < state.words.count && i < 31; i++) s.words[i] = qa_js_to_int32(state.words.values[i]);
  s.front = (int)state.front; s.rear = (int)state.rear; s.draws = (int64_t)state.draws;
  qa_app_sim_source_random_restore(ctx, s);
}
static void validate_state(void *ctx, qa_mod_private_checkpoint state) {
  prepared_ctx *p = ctx;
  const qa_guest_checkpoint *guest = state.guest_count ? &state.guests[0] : NULL;
  if (state.guest_count != 1 || state.provider_count != 0 || guest == NULL
      || guest->kind != QA_GUEST_CHECKPOINT_QUAKEC || !is(guest->module.id, p->module.id)
      || !is(guest->module.digest, p->program.digest)
      || guest->quakec_api.kind != p->program.api.kind
      || guest->globals.length != p->program.initial_globals_length
      || guest->function_index != 0 || guest->call_stack_count != 0 || guest->locals.length != 0
      || !is(guest->host_state.format, "quakec:host-v1"))
    qa_range_error("Incompatible QuakeC gameplay mod checkpoint");
}
static void rt_close(void *ctx) {
  runtime_ctx *r = ctx;
  qa_compat_qc_mod_provider_close(&r->provider);
  qa_compat_qc_mod_provider_destroy(&r->provider);
  qa_app_sim_source_random_destroy(r->random);
}
/* The scope frees the runtime after it closes the source (as qvm_mod.c's runtime_free). */
static void runtime_free(void *ctx) {
  runtime_ctx *r = ctx;
  free(r->command_names);
  free(r->script);
  free(r);
}
static void rt_activate(void *ctx) { qa_compat_qc_mod_provider_activate_match(&((runtime_ctx *)ctx)->provider); }
static void rt_advance(void *ctx, qa_frame_context frame) { qa_compat_qc_mod_provider_advance(&((runtime_ctx *)ctx)->provider, frame); }
static qa_mod_private_checkpoint rt_checkpoint(void *ctx) {
  runtime_ctx *r = ctx;
  qa_mod_private_checkpoint out; memset(&out, 0, sizeof out);
  const qa_guest_checkpoint *guest = qa_compat_qc_mod_provider_checkpoint(&r->provider);
  static qa_guest_checkpoint storage;
  if (guest) { storage = *guest; out.guests = &storage; out.guest_count = 1; }
  return out;
}
static void rt_restore(void *ctx, qa_mod_private_checkpoint state) {
  runtime_ctx *r = ctx;
  if (state.guest_count == 0 || state.guests[0].kind != QA_GUEST_CHECKPOINT_QUAKEC)
    qa_range_error("Missing QuakeC mod checkpoint");
  qa_compat_qc_mod_provider_restore(&r->provider, &state.guests[0]);
}
/* register(registrations): registerModCallbacks(declaration.callbacks, registrations,
 * () => services.time(), (callback, inputs) => { context.assertCurrent(); return source.invoke(callback, inputs); }). */
static qa_source_time callback_time(void *ctx) {
  runtime_ctx *r = ctx;
  return r->services->time(r->services->ctx);
}
static bool callback_execute(void *ctx, const void *callback, const qa_app_sim_mod_callback_inputs *inputs, double *out) {
  runtime_ctx *r = ctx;
  r->assert_current(r->init_ctx);
  qa_compat_qc_mod_input_entry rows[QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY];
  for (size_t i = 0; i < inputs->count; i++) {
    rows[i].name = inputs->names[i];
    rows[i].value = inputs->values[i];
  }
  *out = qa_compat_qc_mod_provider_invoke(&r->provider, ((const qa_mod_callback *)callback)->call, rows, inputs->count, NULL);
  return true;
}
static void rt_register(void *ctx, qa_mod_registrations *registrations) {
  runtime_ctx *r = ctx;
  const qa_mod_callback_declaration *declaration = &r->prepared->options.declaration;
  qa_app_sim_mod_callback_host host = {r, callback_time, callback_execute};
  qa_app_sim_register_mod_callbacks(declaration->callbacks, declaration->callback_count, sizeof *declaration->callbacks,
                                    offsetof(qa_mod_callback, binding), registrations, host);
}
/* services.commands.bind({ selection, module, cvars, names, invoke, readScript? }, context.resources) */
static bool command_invoke(void *ctx, qa_command_invocation *command) {
  return qa_compat_qc_mod_provider_console_command(&((runtime_ctx *)ctx)->provider, command);
}
static const char *command_read_script(void *ctx, const char *name) {
  runtime_ctx *r = ctx;
  qa_app_sim_prepare_quakec_mod_options *options = &r->prepared->options;
  free(r->script);
  r->script = options->read_script(options->read_script_ctx, name);
  return r->script;
}
static qa_mod_runtime initialize_mod(void *ctx, qa_mod_initialization *context) {
  prepared_ctx *p = ctx;
  if (context->services == NULL) qa_range_error("QuakeC gameplay mods require destination world services");
  qa_mod_host_services *services = (qa_mod_host_services *)context->services;
  runtime_ctx *r = calloc(1, sizeof *r);
  if (!r) qa_range_error("Out of memory");
  r->prepared = p;
  r->services = services;
  r->init_ctx = context->ctx;
  r->assert_current = context->assert_current;
  qa_resource_scope_defer(context->resources, runtime_free, r);
  r->random = qa_app_sim_source_random_create(services->seed, "classic");
  r->mod_random.source.ctx = r->random;
  r->mod_random.source.next_integer = rnd_next_integer;
  r->mod_random.source.next_unit = rnd_next_unit;
  r->mod_random.source.checkpoint = rnd_checkpoint;
  r->mod_random.restore = rnd_restore;
  r->media.content = p->options.description.source.content;
  if (p->options.media) r->media = *p->options.media;
  if (!qa_compat_qc_mod_provider_init(&r->provider, &p->program, p->module, p->options.declaration,
        services, &r->mod_random, &r->media, NULL, NULL, 0))
    qa_range_error("Out of memory");
  /* context.resources.own(source): runtime close owns the provider. */
  if (services->commands != NULL) {
    const qa_mod_callback_declaration *declaration = &p->options.declaration;
    const char **names = calloc(declaration->command_count + 1, sizeof *names);
    if (!names) qa_range_error("Out of memory");
    r->command_names = names;
    for (size_t i = 0; i < declaration->command_count; i++) names[i] = declaration->commands[i].name;
    qa_mod_command_binding binding;
    memset(&binding, 0, sizeof binding);
    binding.ctx = r;
    binding.selection = p->options.description.selection;
    binding.module = p->module;
    binding.cvars = qa_compat_qc_mod_provider_get_cvars(&r->provider);
    binding.names = names;
    binding.name_count = declaration->command_count;
    binding.invoke = command_invoke;
    if (p->options.read_script != NULL) binding.read_script = command_read_script;
    qa_mod_command_port *port = qa_mod_commands_bind(services->commands, binding, context->resources);
    qa_compat_qc_mod_provider_bind_commands(&r->provider, port);
  }
  if (!context->restoring) qa_compat_qc_mod_provider_initialize(&r->provider);
  r->runtime.ctx = r;
  r->runtime.close = rt_close;
  r->runtime.activate = rt_activate;
  r->runtime.register_ = rt_register;
  r->runtime.advance = rt_advance;
  r->runtime.checkpoint = rt_checkpoint;
  r->runtime.restore = rt_restore;
  return r->runtime;
}
qa_prepared_mod qa_app_sim_prepare_quakec_mod(qa_app_sim_prepare_quakec_mod_options options) {
  prepared_ctx *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->options = options;
  if (!qa_compat_qc_load_program(options.program, options.program_length, NULL, NULL, &p->program))
    qa_range_error("Out of memory");
  qa_compat_qc_validate_mod(&p->program, options.declaration);
  p->instance_provider = qa_mod_instance_provider(options.description.selection);
  p->module = (qa_module_identity){p->instance_provider, options.declaration.program.path, p->program.digest, options.declaration_digest};
  p->modules[0] = p->module;
  qa_prepared_mod out; memset(&out, 0, sizeof out);
  out.ctx = p;
  out.description = options.description;
  if (options.declaration.client_presentation) {
    out.has_client_presentation = true;
    out.client_presentation.hud = is(options.declaration.client_presentation->hud, "none") ? "none" : "replace";
    out.client_presentation.view = !is(options.declaration.client_presentation->view, "none");
  }
  out.identity.selection = options.description.selection;
  out.identity.source = options.description.source;
  out.identity.declaration_digest = options.declaration_digest;
  out.identity.modules = p->modules; out.identity.module_count = 1;
  out.validate_state = validate_state;
  out.initialize = initialize_mod;
  return out;
}
qa_prepared_mod qa_app_sim_prepare_mounted_quakec_mod(qa_app_sim_prepare_quakec_mod_options options, qa_app_sim_quakec_mod_mounts mounts) {
  uint8_t *bytes = NULL; size_t length = 0; const char *digest = NULL;
  bool opened = mounts.open(mounts.ctx, options.declaration.program.path, &bytes, &length, &digest);
  if (!opened || !is(digest, options.declaration.program.digest)) {
    free(bytes);
    qa_range_error("Selected mod program differs from its resolved artifact");
  }
  qa_compat_qc_program program;
  if (!qa_compat_qc_load_program(bytes, length, NULL, NULL, &program)) qa_range_error("Out of memory");
  qa_compat_qc_mod_media media = mounts.prepare_resources(mounts.ctx, &program);
  static qa_compat_qc_mod_media media_storage; media_storage = media;
  options.program = bytes; options.program_length = length;
  options.media = &media_storage;
  return qa_app_sim_prepare_quakec_mod(options);
}
/* TS readScript: async name => (await mounts.open(name))?.bytes decoded, or undefined. */
static char *mounted_read_script(void *ctx, const char *name) {
  qa_content_opened_resource script = {0};
  if (!qa_content_mounted_open(ctx, name, NULL, NULL, &script)) return NULL;
  char *text = malloc(script.byte_length + 1);
  if (!text) qa_range_error("Out of memory");
  memcpy(text, script.bytes, script.byte_length);
  text[script.byte_length] = '\0';
  free(script.bytes);
  return text;
}
qa_prepared_mod qa_app_sim_prepare_mounted_quakec_mod_content(qa_mod_description description,
    const qa_mod_callback_declaration *declaration, qa_content_digest declaration_digest, qa_content_mounted *mounts) {
  qa_content_opened_resource artifact = {0};
  if (!qa_content_mounted_open(mounts, declaration->program.path, NULL, NULL, &artifact)
      || !is(artifact.reference->digest, declaration->program.digest)) {
    free(artifact.bytes);
    qa_raise("Error", 0, 0, "Selected mod program differs from its resolved artifact");
  }
  qa_cleanup program_cleanup;
  qa_cleanup_push(&program_cleanup, free, artifact.bytes);
  /* prepareQuakeCResources(loadQcProgram(artifact.bytes), mounts) */
  qa_compat_qc_program program;
  if (!qa_compat_qc_load_program(artifact.bytes, artifact.byte_length, NULL, NULL, &program)) qa_range_error("Out of memory");
  qa_app_sim_quakec_resources resources = qa_app_sim_prepare_quakec_resources(&program, mounts, NULL);
  qa_compat_qc_program_destroy(&program);
  qa_compat_qc_mod_media_resource *rows = calloc(resources.count + 1, sizeof *rows);
  if (!rows) qa_range_error("Out of memory");
  for (size_t i = 0; i < resources.count; i++) {
    rows[i].name = resources.items[i].name;
    rows[i].resource = resources.items[i].resource;
    rows[i].has_bounds = resources.items[i].has_model_bounds;
    rows[i].model_bounds = resources.items[i].model_bounds;
  }
  qa_app_sim_prepare_quakec_mod_options options;
  memset(&options, 0, sizeof options);
  options.description = description;
  options.declaration = *declaration;
  options.declaration_digest = declaration_digest;
  options.program = artifact.bytes;
  options.program_length = artifact.byte_length;
  options.read_script_ctx = mounts;
  options.read_script = mounted_read_script;
  qa_prepared_mod prepared = qa_app_sim_prepare_quakec_mod(options);
  qa_cleanup_pop(&program_cleanup);
  prepared_ctx *p = prepared.ctx;
  p->owned_program = artifact.bytes;
  p->has_resources = true;
  p->resources = resources;
  p->media_rows = rows;
  p->media.content = description.source.content;
  p->media.resources = rows;
  p->media.resource_count = resources.count;
  p->options.media = &p->media;
  return prepared;
}
void qa_app_sim_prepared_quakec_mod_free(qa_prepared_mod *prepared) {
  prepared_ctx *p = prepared->ctx;
  if (!p) return;
  free(p->instance_provider);
  free(p->owned_program);
  free(p->media_rows);
  if (p->has_resources) qa_app_sim_quakec_resources_destroy(&p->resources);
  qa_compat_qc_program_destroy(&p->program);
  free(p);
  prepared->ctx = NULL;
}
