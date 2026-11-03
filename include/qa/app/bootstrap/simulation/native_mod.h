/* Port of src/app/bootstrap/simulation/native-mod.ts: a selected native
 * gameplay mod as a PreparedMod whose runtime is a NativeModProvider over
 * createNativeModHost. TS async members are synchronous (docs/PORTING.md).
 *
 * Ownership: the prepared mod owns a copy of the program bytes, the
 * instance provider id and the identity arrays; free it with
 * qa_app_sim_prepared_native_mod_free after every runtime it initialized is
 * closed. options.declaration and options.description are borrowed and must
 * outlive the prepared mod. An initialized runtime is owned by the
 * initialization's resource scope (TS resources.own(provider)): closing the
 * scope closes the provider and frees the runtime, host and command port.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/mounts.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/world/session/mods.h"

/* TS PrepareNativeModOptions. artifact is copied. read_script NULL is TS
 * undefined; its string is borrowed until the prepared mod is freed.
 * localize returns a malloc'd string. */
typedef struct qa_app_sim_prepare_native_mod_options {
  qa_mod_description description;
  const qa_native_mod_declaration *declaration;
  qa_content_digest declaration_digest;
  const uint8_t *program; size_t program_length;
  const qa_resolved_resource_reference *artifact;
  void *ctx;
  const char *(*read_script)(void *ctx, const char *name);
  char *(*localize)(void *ctx, const char *key, const char *const *arguments, size_t count);
} qa_app_sim_prepare_native_mod_options;

/* TS prepareNativeMod. Aborts as the TS throws. */
qa_prepared_mod qa_app_sim_prepare_native_mod(const qa_app_sim_prepare_native_mod_options *options);
/* TS prepareMountedNativeMod: the program, scripts and the server's Q2
 * rerelease localization come from mounts, which must outlive the prepared mod. */
qa_prepared_mod qa_app_sim_prepare_mounted_native_mod(qa_mod_description description, const qa_native_mod_declaration *declaration,
                                                      qa_content_digest declaration_digest, qa_content_mounted *mounts);
void qa_app_sim_prepared_native_mod_free(qa_prepared_mod *prepared);
#endif
