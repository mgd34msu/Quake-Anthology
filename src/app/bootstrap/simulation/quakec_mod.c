/* Port of src/app/bootstrap/simulation/quakec-mod.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_mod.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/foundation/support.h"
typedef struct prepared_ctx {
  qa_app_sim_prepare_quakec_mod_options options;
  qa_compat_qc_program program;
  qa_module_identity module;
  qa_module_identity modules[1];
  char *instance_provider;
} prepared_ctx;
typedef struct runtime_ctx {
  prepared_ctx *prepared;
  qa_compat_qc_mod_provider provider;
  qa_app_sim_source_random *random;
  qa_compat_qc_mod_random mod_random;
  qa_compat_qc_mod_media media;
  qa_mod_runtime runtime;
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
  for (size_t i = 0; i < state.words.count && i < 31; i++) s.words[i] = (int32_t)state.words.values[i];
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
static qa_mod_runtime initialize_mod(void *ctx, qa_mod_initialization *context) {
  prepared_ctx *p = ctx;
  if (context->services == NULL) qa_range_error("QuakeC gameplay mods require destination world services");
  qa_mod_host_services *services = (qa_mod_host_services *)context->services;
  runtime_ctx *r = calloc(1, sizeof *r);
  if (!r) qa_range_error("Out of memory");
  r->prepared = p;
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
  if (!context->restoring) qa_compat_qc_mod_provider_initialize(&r->provider);
  r->runtime.ctx = r;
  r->runtime.close = rt_close;
  r->runtime.activate = rt_activate;
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
void qa_app_sim_prepared_quakec_mod_free(qa_prepared_mod *prepared) {
  prepared_ctx *p = prepared->ctx;
  if (!p) return;
  free(p->instance_provider);
  free(p);
  prepared->ctx = NULL;
}
