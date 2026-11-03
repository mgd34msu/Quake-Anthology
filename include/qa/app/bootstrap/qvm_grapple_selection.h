/* Port of src/app/bootstrap/qvm-grapple-selection.ts. The landed
 * equipment grapple profile reads a mirror artifact shape; adapting
 * the resolved artifact to it is the host's profile_for callback.
 * Mount-plan opening is also a host callback (TS MountPlanOpener).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_QVM_GRAPPLE_SELECTION_H
#define QA_APP_BOOTSTRAP_QVM_GRAPPLE_SELECTION_H
#include <stdbool.h>
#include "qa/compat/qvm/artifacts.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/qvm_grapple.h"
/* TS QvmGrappleSelection: GrappleSelection with mechanic "q3-qvm". */
typedef qa_grapple_selection qa_app_qvm_grapple_selection;
/* TS PreparedQvmGrapple. Artifact/mounts owned by the result. */
typedef struct qa_app_prepared_qvm_grapple {
  qa_app_qvm_grapple_selection selection;
  qa_qvm_grapple_definition profile; /* owned lists; selection.profile points here */
  qa_resolved_qvm_artifact artifact;
  qa_content_mounted *mounts; /* owned unless borrowed_mounts */
  char *module_id;             /* owned backing */
  /* prepareApplicationQvmGrapple: mounts are the content's forContent mounts
   * (borrowed), and the profile's module strings are owned copies. */
  bool borrowed_mounts;
  char *artifact_path, *digest, *revision;
} qa_app_prepared_qvm_grapple;
void qa_app_prepared_qvm_grapple_destroy(qa_app_prepared_qvm_grapple *prepared);
typedef struct qa_app_qvm_grapple_host {
  void *ctx;
  /* TS MountPlanOpener over catalog mounts for content: owned. */
  qa_content_mounted *(*open_plan)(void *ctx, const qa_content_catalog *catalog, qa_content_id content);
  /* TS q3GrappleProfile(artifact): false = TS null. */
  bool (*profile_for)(void *ctx, const qa_resolved_qvm_artifact *artifact, qa_qvm_grapple_definition *out);
} qa_app_qvm_grapple_host;
/* Returns false for the TS null. selection_out borrows profile_out. */
bool qa_app_application_qvm_grapple_selection(const qa_content_catalog *catalog, qa_content_id content,
    const qa_app_qvm_grapple_host *host, qa_app_qvm_grapple_selection *selection_out,
    qa_qvm_grapple_definition *profile_out, char **module_id_out);
typedef qa_content_mounted *(*qa_app_for_content_fn)(void *ctx, qa_content_id content);
/* TS q3GrappleProfile(artifact) over a resolved QVM artifact; false = TS null.
 * The profile's module strings are malloc'd into *artifact_path, *digest and
 * *revision (the caller frees them). */
bool qa_app_qvm_grapple_profile(const qa_resolved_qvm_artifact *artifact, qa_qvm_grapple_definition *out,
    char **artifact_path, char **digest, char **revision);
/* TS prepareApplicationQvmGrapple(selection, forContent). The prepared grapple
 * borrows the forContent mounts and the selection (keep both alive while it
 * lives). Raises Error "Selected grapple differs from its installed source
 * executable or declaration". */
void qa_app_prepare_application_qvm_grapple(qa_app_qvm_grapple_selection selection,
    qa_app_for_content_fn for_content, void *for_content_ctx, qa_app_prepared_qvm_grapple *out);
#endif
