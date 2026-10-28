/* Port of src/app/bootstrap/simulation/q3/guest-artifact.ts.
 * Async becomes synchronous. Compatibility/primary-profile/item-layout
 * reads are host seams: landed compat/qvm headers expose divergent
 * artifact types (qa_resolved_qvm_artifact vs qa_qvmy_artifact),
 * reconciled by the lossless qa_qvm_artifact_bridge
 * (compat/qvm/artifacts.h) where a qa_qvmy_artifact view is needed;
 * the resolved artifact + primary profile still travel as opaque
 * owned handles from the wiring layer. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_ARTIFACT_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_ARTIFACT_H
#include <stdint.h>
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/content/q3/guest_items.h"

typedef qa_resolved_execution_module qa_app_q3_game_execution;
typedef struct qa_app_prepared_q3_game {
  qa_app_q3_game_execution execution;
  void *artifact; /* owned resolved QVM artifact handle */
  const qa_resolved_resource_reference *resource; /* borrowed */
  qa_q3_guest_weapon_list weapons;
  bool has_items;
  void *items;   /* owned item-layout handle */
  void *primary; /* owned primary-profile handle */
} qa_app_prepared_q3_game;
void qa_app_prepared_q3_game_destroy(qa_app_prepared_q3_game *game,
    void (*free_handle)(void *ctx, void *handle), void *free_ctx);

void qa_app_assert_q3_guest_recipe(qa_executable_recipe recipe, const qa_app_q3_game_execution *execution);

typedef struct qa_app_q3_game_prepare_host {
  void *ctx;
  /* mounts.read(reference): malloc'd bytes. */
  uint8_t *(*read_reference)(void *ctx, const qa_resolved_resource_reference *reference, size_t *out_length);
  /* Declaration read: abi profile string, primary presence, resource. */
  const char *(*compatibility_profile)(void *ctx, const qa_resolved_resource_reference *reference,
      bool *out_has_primary, bool *out_has_resource, bool *out_private_inventory);
  /* resolveQvmArtifact: owned handle + bytecode flag. revision "id:generation". */
  void *(*resolve_artifact)(void *ctx, const char *abi_profile, const uint8_t *bytes, size_t length,
      qa_provider_id module_id, const char *artifact_path, qa_content_digest digest, const char *revision,
      bool *out_is_bytecode);
  void *(*parse_item_layout)(void *ctx); /* NULL when no primary */
  qa_q3_guest_weapon_list (*guest_weapons)(void *ctx, void *artifact, void *items, bool private_inventory);
  bool (*item_source_is_live)(void *ctx, void *items);
  void *(*builtin_primary)(void *ctx, void *artifact, const qa_q3_guest_weapon *weapons, size_t weapon_count);
  void *(*read_primary)(void *ctx, void *artifact, const qa_q3_guest_weapon *weapons, size_t weapon_count,
      bool has_weapons, void *items, bool has_resource);
  void (*fail_primary)(void *ctx); /* MUST abort: primary.fail(...) */
} qa_app_q3_game_prepare_host;

qa_app_prepared_q3_game qa_app_prepare_q3_game(qa_app_q3_game_execution execution, qa_app_q3_game_prepare_host host);
#endif
