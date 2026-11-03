/* Port of src/app/bootstrap/simulation/qvm-mod.ts: prepareMountedQvmMod and
 * prepareQvmMod. initialize() constructs the QvmModProvider over the
 * destination's services, binds its commands, and returns the ModRuntime
 * (TS async members are synchronous). The prepared mod borrows description,
 * declaration and mounts; qa_app_sim_prepared_qvm_mod_free releases its own
 * storage (artifacts, program bytes). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_MOD_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_MOD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/mod_provider.h"
#include "qa/content/mounts.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/world/session/mods.h"
/* TS PrepareQvmModOptions. program / presentation_program are copied;
 * presentation_program NULL and mounts NULL are TS undefined. */
typedef struct qa_app_sim_prepare_qvm_mod_options {
  qa_mod_description description;
  const qa_qvm_mod_callback_declaration *declaration;
  qa_content_digest declaration_digest;
  const uint8_t *program; size_t program_length;
  const uint8_t *presentation_program; size_t presentation_length;
  qa_content_mounted *mounts;
} qa_app_sim_prepare_qvm_mod_options;
/* TS prepareMountedQvmMod({ description, declaration, declarationDigest, mounts }). */
qa_prepared_mod qa_app_sim_prepare_mounted_qvm_mod(qa_mod_description description,
    const qa_qvm_mod_callback_declaration *declaration, qa_content_digest declaration_digest, qa_content_mounted *mounts);
qa_prepared_mod qa_app_sim_prepare_qvm_mod(qa_app_sim_prepare_qvm_mod_options options);
void qa_app_sim_prepared_qvm_mod_free(qa_prepared_mod *prepared);
#endif
