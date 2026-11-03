/* Port of src/app/bootstrap/simulation/quakec-mod.ts. Mount reads
 * are synchronous (PORTING.md). The prepared mod's ctx owns the
 * loaded program; free with qa_app_sim_prepared_quakec_mod_free.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_MOD_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_MOD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qc/mod_provider.h"
#include "qa/content/mounts.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/mods.h"
#include "qa/world/session/mods.h"
typedef struct qa_app_sim_prepare_quakec_mod_options {
  qa_mod_description description;
  qa_mod_callback_declaration declaration;
  qa_content_digest declaration_digest;
  const uint8_t *program; size_t program_length;
  const qa_compat_qc_mod_media *media; /* TS resources (optional) */
  void *read_script_ctx;
  char *(*read_script)(void *ctx, const char *name); /* malloc'd or NULL */
} qa_app_sim_prepare_quakec_mod_options;
typedef struct qa_app_sim_quakec_mod_mounts {
  void *ctx;
  /* mounts.open: false when TS null; bytes malloc'd. */
  bool (*open)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length, const char **out_digest);
  /* prepareQuakeCResources(program, mounts). */
  qa_compat_qc_mod_media (*prepare_resources)(void *ctx, qa_compat_qc_program *program);
} qa_app_sim_quakec_mod_mounts;
qa_prepared_mod qa_app_sim_prepare_mounted_quakec_mod(qa_app_sim_prepare_quakec_mod_options options, qa_app_sim_quakec_mod_mounts mounts);
/* TS prepareMountedQuakeCMod({ description, declaration, declarationDigest, mounts }) over
 * MountedContent: the program, prepareQuakeCResources and readScript come from mounts,
 * which must outlive the prepared mod (it owns the program bytes and resources).
 * Raises the TS Error when the program differs from its declaration. */
qa_prepared_mod qa_app_sim_prepare_mounted_quakec_mod_content(qa_mod_description description,
    const qa_mod_callback_declaration *declaration, qa_content_digest declaration_digest, qa_content_mounted *mounts);
qa_prepared_mod qa_app_sim_prepare_quakec_mod(qa_app_sim_prepare_quakec_mod_options options);
void qa_app_sim_prepared_quakec_mod_free(qa_prepared_mod *prepared);
#endif
