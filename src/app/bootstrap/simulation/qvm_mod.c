/* Port of src/app/bootstrap/simulation/qvm-mod.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/qvm_mod.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct prepared_ctx {
  qa_app_sim_prepare_qvm_mod_options options;
  qa_module_identity module;
  qa_module_identity modules[1];
  char *instance_provider;
  qa_resolved_qvm_artifact artifact;
  bool has_presentation_artifact;
  qa_resolved_qvm_artifact presentation_artifact;
} prepared_ctx;
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void validate_state(void *ctx, qa_mod_private_checkpoint state) {
  prepared_ctx *p = ctx;
  if (state.guest_count != 1 || state.provider_count != 0 || state.guests[0].kind != QA_GUEST_CHECKPOINT_QVM)
    qa_range_error("Missing QVM gameplay mod checkpoint");
  if (p->options.validate_checkpoint)
    p->options.validate_checkpoint(p->options.host_ctx, &p->artifact, &p->options.declaration, &state.guests[0]);
}
static qa_mod_runtime initialize_mod(void *ctx, qa_mod_initialization *context) {
  prepared_ctx *p = ctx;
  if (context->services == NULL) qa_range_error("QVM gameplay mods require destination world services");
  return p->options.create_runtime(p->options.host_ctx, &p->artifact, &p->options.declaration, context);
}
qa_prepared_mod qa_app_sim_prepare_qvm_mod(qa_app_sim_prepare_qvm_mod_options options) {
  prepared_ctx *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->options = options;
  p->instance_provider = qa_mod_instance_provider(options.description.selection);
  p->module = (qa_module_identity){p->instance_provider, options.declaration.program.path,
      options.declaration.program.digest, options.declaration_digest};
  p->modules[0] = p->module;
  qa_qvm_resolve_artifact_options resolve; memset(&resolve, 0, sizeof resolve);
  resolve.module = p->module; resolve.role = "qagame";
  resolve.bytes = options.program; resolve.length = options.program_length;
  resolve.has_abi_profile = options.declaration.abi_profile != NULL;
  resolve.abi_profile = options.declaration.abi_profile;
  if (!qa_qvm_resolve_artifact(resolve, &p->artifact)) qa_range_error("Out of memory");
  if (p->artifact.is_typescript) qa_range_error("Gameplay mod callbacks require their authored QVM executable");
  if (options.validate) options.validate(options.host_ctx, &p->artifact, &options.declaration);
  qa_prepared_mod out; memset(&out, 0, sizeof out);
  out.ctx = p;
  out.description = options.description;
  out.identity.selection = options.description.selection;
  out.identity.source = options.description.source;
  out.identity.declaration_digest = options.declaration_digest;
  out.identity.modules = p->modules; out.identity.module_count = 1;
  const qa_qvm_mod_presentation_declaration *presentation = options.declaration.presentation;
  if (presentation != NULL) {
    if (options.presentation_program == NULL)
      qa_range_error("Declared QVM mod presentation requires its authored cgame bytes");
    /* gameplay abiProfile lives on the presentation declaration's
     * gameplay member in TS; landed declaration carries cgame only. */
    qa_module_identity cgame_module = p->module;
    qa_qvm_resolve_artifact_options pres; memset(&pres, 0, sizeof pres);
    pres.module = cgame_module; pres.role = "cgame";
    pres.bytes = options.presentation_program; pres.length = options.presentation_length;
    pres.has_abi_profile = options.declaration.abi_profile != NULL;
    pres.abi_profile = options.declaration.abi_profile;
    if (!qa_qvm_resolve_artifact(pres, &p->presentation_artifact)) qa_range_error("Out of memory");
    p->has_presentation_artifact = true;
    if (p->presentation_artifact.is_typescript)
      qa_range_error("Mod presentation requires its authored QVM executable");
    out.has_presentation = true;
    out.presentation.artifact = &p->presentation_artifact;
    out.presentation.source = p->module;
    out.presentation.declaration = *presentation;
  } else if (options.presentation_program != NULL) {
    qa_range_error("QVM mod presentation bytes require a declaration");
  }
  out.validate_state = validate_state;
  out.initialize = initialize_mod;
  return out;
}
qa_prepared_mod qa_app_sim_prepare_mounted_qvm_mod(qa_app_sim_prepare_qvm_mod_options options, qa_app_sim_qvm_mod_mounts mounts) {
  uint8_t *bytes = NULL; size_t length = 0; const char *digest = NULL;
  if (!mounts.open(mounts.ctx, options.declaration.program.path, &bytes, &length, &digest) || !is(digest, options.declaration.program.digest)) {
    free(bytes);
    qa_range_error("Selected QVM mod differs from its resolved artifact");
  }
  options.program = bytes; options.program_length = length;
  /* presentation cgame bytes are opened by the caller when declared
   * (landed declaration carries no cgame path member). */
  return qa_app_sim_prepare_qvm_mod(options);
}
void qa_app_sim_prepared_qvm_mod_free(qa_prepared_mod *prepared) {
  prepared_ctx *p = prepared->ctx;
  if (!p) return;
  qa_resolved_qvm_artifact_destroy(&p->artifact);
  if (p->has_presentation_artifact) qa_resolved_qvm_artifact_destroy(&p->presentation_artifact);
  free(p->instance_provider);
  free(p);
  prepared->ctx = NULL;
}
