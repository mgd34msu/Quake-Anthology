/* Port of src/app/bootstrap/simulation/qvm-mod.ts. Preparation
 * resolves the landed qa_resolved_qvm_artifact and, with
 * validate/validate_checkpoint NULL, runs qa_qvm_validate_mod /
 * qa_qvm_validate_mod_checkpoint over it directly. Provider
 * construction stays caller-supplied. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_MOD_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_MOD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/mod_provider.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/world/session/mods.h"
typedef struct qa_app_sim_prepare_qvm_mod_options {
  qa_mod_description description;
  qa_qvm_mod_callback_declaration declaration;
  qa_content_digest declaration_digest;
  const uint8_t *program; size_t program_length;
  const uint8_t *presentation_program; size_t presentation_length; /* NULL absent */
  const void *mounts; /* MountedContent (opaque) */
  void *host_ctx;
  /* validateQvmMod + validateQvmModPresentation over the bridged artifact. */
  void (*validate)(void *ctx, const qa_resolved_qvm_artifact *artifact, const qa_qvm_mod_callback_declaration *declaration);
  void (*validate_checkpoint)(void *ctx, const qa_resolved_qvm_artifact *artifact, const qa_qvm_mod_callback_declaration *declaration, const qa_guest_checkpoint *guest);
  /* QvmModProvider construction + lifecycle (initialize). */
  qa_mod_runtime (*create_runtime)(void *ctx, const qa_resolved_qvm_artifact *artifact,
      const qa_qvm_mod_callback_declaration *declaration, qa_mod_initialization *context);
} qa_app_sim_prepare_qvm_mod_options;
typedef struct qa_app_sim_qvm_mod_mounts {
  void *ctx;
  bool (*open)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length, const char **out_digest);
} qa_app_sim_qvm_mod_mounts;
qa_prepared_mod qa_app_sim_prepare_mounted_qvm_mod(qa_app_sim_prepare_qvm_mod_options options, qa_app_sim_qvm_mod_mounts mounts);
qa_prepared_mod qa_app_sim_prepare_qvm_mod(qa_app_sim_prepare_qvm_mod_options options);
void qa_app_sim_prepared_qvm_mod_free(qa_prepared_mod *prepared);
#endif
