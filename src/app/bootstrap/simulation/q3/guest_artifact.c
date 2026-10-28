/* Port of src/app/bootstrap/simulation/q3/guest-artifact.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

static bool same_ref(qa_provider_reference a, qa_provider_reference b) {
  return a.content && b.content && strcmp(a.content, b.content) == 0
      && a.provider && b.provider && strcmp(a.provider, b.provider) == 0;
}
void qa_app_assert_q3_guest_recipe(qa_executable_recipe recipe, const qa_app_q3_game_execution *execution) {
  qa_provider_reference owner = recipe.map.entities;
  bool timing_q3 = false;
  for (size_t i = 0; i < recipe.timing_count; i++)
    if (recipe.timing[i].provider && owner.provider && strcmp(recipe.timing[i].provider, owner.provider) == 0)
      timing_q3 = recipe.timing[i].clock.kind == QA_CLOCK_Q3;
  if (execution->api.kind != QA_GAME_API_Q3 || execution->api.as.q3.kind != QA_Q3_API_QAGAME
      || !same_ref(execution->owner, owner)
      || recipe.execution_count != 1 || &recipe.execution[0] != execution
      || recipe.weapons.count != 1
      || !same_ref(recipe.engine_behavior, owner) || !same_ref(recipe.combat, owner)
      || !same_ref(recipe.inventory, owner) || !same_ref(recipe.match, owner) || !same_ref(recipe.transition, owner)
      || recipe.enemies.kind == QA_ENEMY_SELECTION_PLAYERS
      || !timing_q3) {
    qa_range_error("Q3 bytecode requires one native Q3 server game, map-defined actors and supported source providers");
  }
}
static bool ieq(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}
qa_app_prepared_q3_game qa_app_prepare_q3_game(qa_app_q3_game_execution execution, qa_app_q3_game_prepare_host host) {
  const qa_resolved_resource_reference *reference = (const qa_resolved_resource_reference *)execution.artifact;
  if (execution.api.kind != QA_GAME_API_Q3 || execution.api.as.q3.kind != QA_Q3_API_QAGAME
      || !reference || !ieq(reference->requested_path, "vm/qagame.qvm"))
    qa_range_error("Selected Q3 server artifact must provide a supported qagame ABI");
  size_t length = 0;
  uint8_t *bytes = host.read_reference(host.ctx, reference, &length);
  bool has_primary = false, has_resource = false, private_inventory = false;
  const char *abi_profile = host.compatibility_profile(host.ctx, reference, &has_primary, &has_resource, &private_inventory);
  if (execution.api.as.q3.version != (abi_profile && strcmp(abi_profile, "q3-modern") == 0 ? 8 : 7))
    qa_range_error("Selected QVM ABI differs from its saved or resolved recipe");
  char revision[256];
  const qa_mount_identity *identity = NULL;
  if (reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE && reference->provenance.archive_mount)
    identity = &reference->provenance.archive_mount->identity;
  else if (reference->provenance.loose_mount) identity = &reference->provenance.loose_mount->identity;
  snprintf(revision, sizeof revision, "%s:%lld", identity ? identity->id : "",
      identity ? (long long)identity->generation : 0);
  bool is_bytecode = false;
  void *artifact = host.resolve_artifact(host.ctx, abi_profile, bytes, length, execution.owner.provider,
      reference->requested_path, reference->digest, revision, &is_bytecode);
  free(bytes);
  if (!is_bytecode) qa_range_error("Selected Q3 guest artifact did not resolve to bytecode");
  void *items = has_primary ? host.parse_item_layout(host.ctx) : NULL;
  qa_q3_guest_weapon_list weapons = host.guest_weapons(host.ctx, artifact, items, private_inventory);
  void *primary = NULL;
  if (!has_primary) {
    primary = host.builtin_primary(host.ctx, artifact, weapons.values, weapons.count);
  } else if (!has_resource || items == NULL) {
    host.fail_primary(host.ctx);
    qa_range_error("primary interface lost its declaration resource");
  } else {
    bool live = host.item_source_is_live(host.ctx, items);
    primary = host.read_primary(host.ctx, artifact, live ? NULL : weapons.values, live ? 0 : weapons.count, !live, items, has_resource);
  }
  qa_app_prepared_q3_game game; memset(&game, 0, sizeof game);
  game.execution = execution; game.artifact = artifact; game.resource = reference;
  game.weapons = weapons; game.has_items = items != NULL; game.items = items; game.primary = primary;
  return game;
}
void qa_app_prepared_q3_game_destroy(qa_app_prepared_q3_game *game,
    void (*free_handle)(void *ctx, void *handle), void *free_ctx) {
  if (!game) return;
  qa_q3_guest_weapon_list_destroy(&game->weapons);
  if (free_handle) {
    free_handle(free_ctx, game->artifact);
    free_handle(free_ctx, game->items);
    free_handle(free_ctx, game->primary);
  }
  memset(game, 0, sizeof *game);
}
