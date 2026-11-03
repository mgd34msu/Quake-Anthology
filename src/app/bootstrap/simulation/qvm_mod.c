/* Port of src/app/bootstrap/simulation/qvm-mod.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/qvm_mod.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
#include "qa/compat/qvm/mod_presentation.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mod_commands.h"
#include "qa/world/session/mod_files.h"
#include "qa/world/session/resources.h"

typedef struct prepared_ctx {
  qa_app_sim_prepare_qvm_mod_options options;
  uint8_t *program, *presentation_program; /* owned copies of the option bytes */
  char *instance_provider;
  qa_module_identity module;
  qa_module_identity modules[1];
  qa_resolved_qvm_artifact artifact;
  bool has_presentation_artifact;
  qa_resolved_qvm_artifact presentation_artifact;
} prepared_ctx;

typedef struct runtime_ctx {
  prepared_ctx *p;
  const qa_mod_host_services *services;
  qa_qvm_mod_provider provider;
  bool provider_ready;
  qa_content_mounted *borrowed_mounts; /* borrowModFileMounts result, closed by the scope */
  qa_mod_command_port *commands;
  char *script; /* the last readScript text (the command port borrows it) */
  void *init_ctx; void (*assert_current)(void *ctx);
  qa_guest_checkpoint saved_rows[1];
} runtime_ctx;

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static const qa_qvm_presentation_base *presentation_base(const qa_qvm_mod_presentation_declaration *declaration) {
  return declaration->is_scene ? &declaration->as.scene->base : &declaration->as.player_events->base;
}
/* declaration.presentation?.hud. */
static const qa_qvm_presentation_hud *declared_hud(const qa_qvm_mod_callback_declaration *declaration) {
  return declaration->presentation == NULL ? NULL : presentation_base(declaration->presentation)->hud;
}

/* ---- validateState ---- */
static void validate_state(void *ctx, qa_mod_private_checkpoint state) {
  prepared_ctx *p = ctx;
  if (state.guest_count != 1 || state.provider_count != 0 || state.guests[0].kind != QA_GUEST_CHECKPOINT_QVM)
    qa_raise("Error", 0, 0, "Missing QVM gameplay mod checkpoint");
  qa_qvm_validate_mod_checkpoint(&p->artifact, p->options.declaration, &state.guests[0]);
}

/* ---- the ModRuntime initialize() returns ---- */
static void runtime_free(void *ctx) {
  runtime_ctx *r = ctx;
  if (r->provider_ready) qa_qvm_mod_provider_destroy(&r->provider);
  /* r->commands is borrowed: the mod scope (resources) owns the command port, as in TS. */
  free(r->script);
  free(r);
}
static void provider_close(void *ctx) { qa_qvm_mod_provider_close(&((runtime_ctx *)ctx)->provider); }
static void mounts_close(void *ctx) {
  runtime_ctx *r = ctx;
  qa_content_mounted_close(r->borrowed_mounts);
  qa_content_mounted_destroy(r->borrowed_mounts);
  r->borrowed_mounts = NULL;
}
static void provider_assert_current(void *ctx) {
  runtime_ctx *r = ctx;
  r->assert_current(r->init_ctx);
}
static bool command_invoke(void *ctx, qa_command_invocation *command) {
  return qa_qvm_mod_provider_console_command(&((runtime_ctx *)ctx)->provider, command);
}
static const char *command_read_script(void *ctx, const char *name) {
  runtime_ctx *r = ctx;
  free(r->script);
  r->script = qa_qvm_mod_provider_read_script(&r->provider, name);
  return r->script;
}
static qa_source_time callback_time(void *ctx) {
  runtime_ctx *r = ctx;
  return r->services->time(r->services->ctx);
}
static bool callback_execute(void *ctx, const void *callback, const qa_app_sim_mod_callback_inputs *inputs, double *out) {
  runtime_ctx *r = ctx;
  qa_qvm_mod_provider_input rows[QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY];
  for (size_t i = 0; i < inputs->count; i++) { rows[i].name = inputs->names[i]; rows[i].value = inputs->values[i]; }
  *out = qa_qvm_mod_provider_invoke(&r->provider, &((const qa_qvm_mod_callback *)callback)->call, rows, inputs->count);
  return true;
}
static void r_register(void *ctx, qa_mod_registrations *registrations) {
  runtime_ctx *r = ctx;
  const qa_qvm_mod_callback_declaration *declaration = r->p->options.declaration;
  qa_app_sim_mod_callback_host host = { r, callback_time, callback_execute };
  qa_app_sim_register_mod_callbacks(declaration->callbacks, declaration->callback_count, sizeof *declaration->callbacks,
      offsetof(qa_qvm_mod_callback, binding), registrations, host);
}
static void r_activate(void *ctx) { qa_qvm_mod_provider_activate_protection(&((runtime_ctx *)ctx)->provider); }
static void r_advance(void *ctx, qa_frame_context frame) { qa_qvm_mod_provider_advance(&((runtime_ctx *)ctx)->provider, frame); }
static const qa_app_simulation_presentation *r_presentations(void *ctx, size_t *out_count) {
  return qa_qvm_mod_provider_presentations(&((runtime_ctx *)ctx)->provider, out_count);
}
static qa_mod_private_checkpoint r_checkpoint(void *ctx) {
  runtime_ctx *r = ctx;
  r->saved_rows[0] = *qa_qvm_mod_provider_checkpoint(&r->provider);
  qa_mod_private_checkpoint state;
  memset(&state, 0, sizeof state);
  state.guests = r->saved_rows; state.guest_count = 1;
  return state;
}
static void r_restore(void *ctx, qa_mod_private_checkpoint state) {
  runtime_ctx *r = ctx;
  if (state.guest_count == 0 || state.guests[0].kind != QA_GUEST_CHECKPOINT_QVM) qa_raise("Error", 0, 0, "Missing QVM mod checkpoint");
  qa_qvm_mod_provider_restore(&r->provider, &state.guests[0]);
}
static void r_close(void *ctx) { qa_qvm_mod_provider_close(&((runtime_ctx *)ctx)->provider); }
static bool r_qvm_presentation(void *ctx, qa_mod_qvm_presentation_source *out) {
  *out = ((runtime_ctx *)ctx)->provider.presentation_source;
  return true;
}
/* The clientPresentation source over source.presentationSource. */
static bool client_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  runtime_ctx *r = ctx;
  qa_mod_qvm_presentation_source *source = &r->provider.presentation_source;
  if (source->context(source->ctx, actor) == NULL) return false;
  memset(out, 0, sizeof *out);
  out->kind = QA_MOD_CLIENT_PRESENTATION_QVM;
  out->qvm_hud_mode = declared_hud(r->p->options.declaration)->mode;
  return true;
}
static void client_assert_current(void *ctx) {
  qa_mod_qvm_presentation_source *source = &((runtime_ctx *)ctx)->provider.presentation_source;
  source->assert_current(source->ctx);
}
static bool r_client_presentation(void *ctx, qa_mod_client_presentation_source *out) {
  runtime_ctx *r = ctx;
  memset(out, 0, sizeof *out);
  out->ctx = r;
  out->generation = r->provider.presentation_source.generation;
  out->frame = client_frame;
  out->assert_current = client_assert_current;
  return true;
}

static qa_mod_runtime initialize_mod(void *ctx, qa_mod_initialization *context) {
  prepared_ctx *p = ctx;
  if (context->services == NULL) qa_raise("Error", 0, 0, "QVM gameplay mods require destination world services");
  const qa_mod_host_services *services = context->services;
  qa_mod_selection selection = p->options.description.selection;
  qa_user_file_store *writable = services->files == NULL ? NULL : qa_mod_user_files_for(services->files, selection);
  runtime_ctx *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("Out of memory");
  r->p = p; r->services = services; r->init_ctx = context->ctx; r->assert_current = context->assert_current;
  /* The scope frees the runtime after it closes the provider and the borrowed mounts. */
  qa_resource_scope_defer(context->resources, runtime_free, r);
  qa_content_mounted *mounts = p->options.mounts;
  if (writable != NULL) {
    r->borrowed_mounts = qa_borrow_mod_file_mounts(selection, p->options.description.source.content, p->options.mounts, writable);
    if (r->borrowed_mounts == NULL) qa_range_error("Out of memory");
    mounts = r->borrowed_mounts;
    qa_resource_scope_defer(context->resources, mounts_close, r);
  }
  if (!qa_qvm_mod_provider_init(&r->provider, &p->artifact, p->options.declaration, (qa_mod_host_services *)services,
          provider_assert_current, r, p->options.description.source.content, mounts, writable, NULL, NULL))
    qa_range_error("Out of memory");
  r->provider_ready = true;
  qa_session_resource resource = { r, provider_close };
  (void)qa_resource_scope_own(context->resources, resource);
  qa_qvm_mod_provider_reserve_protection(&r->provider);
  if (services->commands != NULL) {
    qa_mod_command_binding binding;
    memset(&binding, 0, sizeof binding);
    binding.ctx = r; binding.selection = selection; binding.module = p->module; binding.cvars = r->provider.cvars;
    binding.invoke = command_invoke; binding.read_script = command_read_script;
    r->commands = qa_mod_commands_bind(services->commands, binding, context->resources);
    qa_qvm_mod_provider_bind_commands(&r->provider, r->commands);
  }
  if (!context->restoring) qa_qvm_mod_provider_initialize(&r->provider);
  context->assert_current(context->ctx);
  qa_mod_runtime runtime;
  memset(&runtime, 0, sizeof runtime);
  runtime.ctx = r;
  if (declared_hud(p->options.declaration) != NULL) runtime.client_presentation = r_client_presentation;
  runtime.qvm_presentation = r_qvm_presentation;
  runtime.activate = r_activate;
  runtime.register_ = r_register;
  runtime.advance = r_advance;
  runtime.presentations = r_presentations;
  runtime.checkpoint = r_checkpoint;
  runtime.restore = r_restore;
  runtime.close = r_close;
  return runtime;
}

/* ---- prepareQvmMod ---- */
static uint8_t *copy_bytes(const uint8_t *bytes, size_t length) {
  uint8_t *copy = malloc(length == 0 ? 1 : length);
  if (copy == NULL) qa_range_error("Out of memory");
  if (length != 0) memcpy(copy, bytes, length);
  return copy;
}
static void prepared_release(prepared_ctx *p) {
  qa_resolved_qvm_artifact_destroy(&p->artifact);
  if (p->has_presentation_artifact) qa_resolved_qvm_artifact_destroy(&p->presentation_artifact);
  free(p->program); free(p->presentation_program); free(p->instance_provider);
  free(p);
}
static void prepared_release_cleanup(void *ctx) { prepared_release(ctx); }
qa_prepared_mod qa_app_sim_prepare_qvm_mod(qa_app_sim_prepare_qvm_mod_options options) {
  const qa_qvm_mod_callback_declaration *declaration = options.declaration;
  prepared_ctx *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->options = options;
  p->program = copy_bytes(options.program, options.program_length);
  p->options.program = p->program;
  if (options.presentation_program != NULL) {
    p->presentation_program = copy_bytes(options.presentation_program, options.presentation_length);
    p->options.presentation_program = p->presentation_program;
  }
  p->instance_provider = qa_mod_instance_provider(options.description.selection);
  if (p->instance_provider == NULL) qa_range_error("Out of memory");
  p->module = (qa_module_identity){ p->instance_provider, declaration->program.path, declaration->program.digest, options.declaration_digest };
  p->modules[0] = p->module;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, prepared_release_cleanup, p);
  qa_qvm_resolve_artifact_options resolve;
  memset(&resolve, 0, sizeof resolve);
  resolve.module = p->module; resolve.role = "qagame";
  resolve.bytes = p->program; resolve.length = options.program_length;
  resolve.has_abi_profile = declaration->abi_profile != NULL;
  resolve.abi_profile = declaration->abi_profile;
  if (!qa_qvm_resolve_artifact(resolve, &p->artifact)) qa_range_error("Out of memory");
  if (p->artifact.is_typescript) qa_raise("Error", 0, 0, "Gameplay mod callbacks require their authored QVM executable");
  qa_qvm_validate_mod(&p->artifact, declaration);
  qa_prepared_mod out;
  memset(&out, 0, sizeof out);
  out.ctx = p;
  out.description = options.description;
  out.identity.selection = options.description.selection;
  out.identity.source = options.description.source;
  out.identity.declaration_digest = options.declaration_digest;
  out.identity.modules = p->modules; out.identity.module_count = 1;
  if (declaration->presentation != NULL) {
    const qa_qvm_presentation_base *base = presentation_base(declaration->presentation);
    if (options.presentation_program == NULL) qa_raise("Error", 0, 0, "Declared QVM mod presentation requires its authored cgame bytes");
    if (!is(base->gameplay.abi_profile, declaration->abi_profile) && !(base->gameplay.abi_profile == NULL && declaration->abi_profile == NULL))
      qa_raise("Error", 0, 0, "QVM mod presentation differs from its gameplay ABI");
    qa_qvm_resolve_artifact_options presentation;
    memset(&presentation, 0, sizeof presentation);
    presentation.module = p->module;
    presentation.module.artifact_path = base->cgame.path;
    presentation.module.digest = base->cgame.digest;
    presentation.role = "cgame";
    presentation.bytes = p->presentation_program; presentation.length = options.presentation_length;
    presentation.has_abi_profile = base->cgame.abi_profile != NULL;
    presentation.abi_profile = base->cgame.abi_profile;
    if (!qa_qvm_resolve_artifact(presentation, &p->presentation_artifact)) qa_range_error("Out of memory");
    p->has_presentation_artifact = true;
    if (p->presentation_artifact.is_typescript) qa_raise("Error", 0, 0, "Mod presentation requires its authored QVM executable");
    out.has_presentation = true;
    out.presentation.artifact = &p->presentation_artifact;
    out.presentation.source = p->module;
    out.presentation.declaration = *declaration->presentation;
    qa_qvm_validate_mod_presentation(&p->presentation_artifact, p->module, declaration->presentation);
  } else if (options.presentation_program != NULL) qa_raise("Error", 0, 0, "QVM mod presentation bytes require a declaration");
  const qa_qvm_presentation_hud *hud = declared_hud(declaration);
  if (hud != NULL) {
    out.has_client_presentation = true;
    out.client_presentation.hud = is(hud->mode, "overlay") ? "overlay" : "replace";
    out.client_presentation.view = false;
  }
  out.validate_state = validate_state;
  out.initialize = initialize_mod;
  qa_cleanup_pop(&cleanup);
  return out;
}

/* ---- prepareMountedQvmMod ---- */
qa_prepared_mod qa_app_sim_prepare_mounted_qvm_mod(qa_mod_description description,
    const qa_qvm_mod_callback_declaration *declaration, qa_content_digest declaration_digest, qa_content_mounted *mounts) {
  qa_content_opened_resource program;
  memset(&program, 0, sizeof program);
  if (!qa_content_mounted_open(mounts, declaration->program.path, NULL, NULL, &program)
      || !is(program.reference->digest, declaration->program.digest)) {
    free(program.bytes);
    qa_raise("Error", 0, 0, "Selected QVM mod differs from its resolved artifact");
  }
  qa_cleanup program_cleanup;
  qa_cleanup_push(&program_cleanup, free, program.bytes);
  qa_app_sim_prepare_qvm_mod_options options;
  memset(&options, 0, sizeof options);
  options.description = description; options.declaration = declaration; options.declaration_digest = declaration_digest;
  options.program = program.bytes; options.program_length = program.byte_length; options.mounts = mounts;
  qa_content_opened_resource cgame;
  memset(&cgame, 0, sizeof cgame);
  if (declaration->presentation != NULL) {
    const qa_qvm_presentation_program *expected = &presentation_base(declaration->presentation)->cgame;
    if (!qa_content_mounted_open(mounts, expected->path, NULL, NULL, &cgame) || !is(cgame.reference->digest, expected->digest)) {
      free(cgame.bytes);
      qa_raise("Error", 0, 0, "Selected QVM mod presentation differs from its resolved artifact");
    }
    options.presentation_program = cgame.bytes; options.presentation_length = cgame.byte_length;
  }
  qa_cleanup cgame_cleanup;
  qa_cleanup_push(&cgame_cleanup, free, cgame.bytes);
  qa_prepared_mod prepared = qa_app_sim_prepare_qvm_mod(options);
  qa_cleanup_pop(&cgame_cleanup);
  qa_cleanup_pop(&program_cleanup);
  free(cgame.bytes);
  free(program.bytes);
  return prepared;
}
void qa_app_sim_prepared_qvm_mod_free(qa_prepared_mod *prepared) {
  prepared_ctx *p = prepared->ctx;
  if (!p) return;
  prepared_release(p);
  prepared->ctx = NULL;
}
