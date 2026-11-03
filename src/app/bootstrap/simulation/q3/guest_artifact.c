/* Port of src/app/bootstrap/simulation/q3/guest-artifact.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/compatibility.h"
#include "qa/foundation/support.h"

static bool same_ref(qa_provider_reference a, qa_provider_reference b) {
  return a.content && b.content && strcmp(a.content, b.content) == 0
      && a.provider && b.provider && strcmp(a.provider, b.provider) == 0;
}
void qa_app_assert_q3_guest_recipe(qa_executable_recipe recipe, const qa_app_q3_game_execution *execution) {
  qa_provider_reference owner = recipe.map.entities;
  bool timing_q3 = false, timing_found = false;
  /* recipe.timing.find(timing => timing.provider === owner.provider)?.clock.kind */
  for (size_t i = 0; i < recipe.timing_count && !timing_found; i++)
    if (recipe.timing[i].provider && owner.provider && strcmp(recipe.timing[i].provider, owner.provider) == 0) {
      timing_found = true; timing_q3 = recipe.timing[i].clock.kind == QA_CLOCK_Q3;
    }
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
static qa_save_reader child(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
/* The content q3 mirror of the bytecode artifact (primary_profile.c does the same). */
static void equipment_view_init(qa_app_q3_equipment_artifact_view *view, const qa_resolved_qvm_artifact *artifact) {
  memset(view, 0, sizeof *view);
  qa_q3_equipment_qvm_artifact *result = &view->artifact;
  result->module = artifact->module;
  result->role = artifact->role;
  result->abi_profile = artifact->has_abi_profile ? artifact->abi_profile : NULL;
  if (artifact->known != NULL) { view->known.product = artifact->known->product; result->known = &view->known; }
  result->image.initialized_data = artifact->image.initialized_data;
  result->image.initialized_data_length = artifact->image.initialized_length;
  result->image.allocated_data_length = (double)artifact->image.allocated_data_length;
  result->image.data_length = (double)artifact->image.data_length;
  result->image.literal_length = (double)artifact->image.literal_length;
  result->image.bss_length = (double)artifact->image.bss_length;
  size_t count = artifact->image.instruction_count;
  view->instructions = calloc(count ? count : 1, sizeof *view->instructions);
  if (!view->instructions) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_qvm_instruction *instruction = &artifact->image.instructions[i];
    view->instructions[i].byte_offset = (double)instruction->byte_offset;
    view->instructions[i].opcode = (double)instruction->opcode;
    view->instructions[i].operand_width = (double)instruction->operand_width;
    view->instructions[i].operand = (double)instruction->operand;
  }
  result->image.instructions = view->instructions;
  result->image.instruction_count = count;
}
static void guest_layout_of(const qa_qvm_item_layout *items, qa_q3_guest_qvm_item_layout *out) {
  memset(out, 0, sizeof *out);
  out->address_is_global = !items->address_is_number;
  out->address = items->address_is_number ? items->address : items->address_global;
  out->count_is_global = !items->count_is_number;
  out->count = items->count_is_number ? items->count : items->count_global;
  out->count_maximum = items->count_maximum;
  out->source_live = items->has_source && items->source && strcmp(items->source, "live") == 0;
  out->stride = items->stride;
  out->field_class_name = items->field_class_name; out->field_pickup_name = items->field_pickup_name;
  out->field_type = items->field_type; out->field_tag = items->field_tag;
  out->weapon_type = items->weapon_type; out->ammo_type = items->ammo_type;
}
static bool mounts_open(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length) {
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_length = opened.byte_length;
  return true;
}
void qa_app_prepare_q3_game(qa_app_q3_game_execution execution, qa_content_mounted *mounts, qa_app_prepared_q3_game *out) {
  const qa_resolved_resource_reference *reference = execution.artifact;
  if (execution.api.kind != QA_GAME_API_Q3 || execution.api.as.q3.kind != QA_Q3_API_QAGAME
      || !reference || !ieq(reference->requested_path, "vm/qagame.qvm"))
    qa_range_error("Selected Q3 server artifact must provide a supported qagame ABI");
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, reference, &length);
  if (!bytes) qa_range_error("Selected Q3 server artifact is unreadable");
  qa_qvm_compatibility_reading declaration;
  qa_qvm_module_path_digest module = {reference->requested_path, reference->digest};
  if (!qa_qvm_read_compatibility_declaration(mounts, module, "qagame", &declaration)) qa_range_error("Out of memory");
  qa_qvm_abi_profile abi_profile = declaration.declaration.profile;
  if (execution.api.as.q3.version != (abi_profile && strcmp(abi_profile, "q3-modern") == 0 ? 8 : 7))
    qa_range_error("Selected QVM ABI differs from its saved or resolved recipe");
  const qa_mount_identity *identity = reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE && reference->provenance.archive_mount
      ? &reference->provenance.archive_mount->identity : reference->provenance.loose_mount ? &reference->provenance.loose_mount->identity : NULL;
  char revision[512];
  snprintf(revision, sizeof revision, "%s:%lld", identity ? identity->id : "", identity ? (long long)identity->generation : 0);
  memset(out, 0, sizeof *out);
  out->kind = "qvm";
  out->execution = execution; out->resource = reference;
  qa_qvm_resolve_artifact_options resolve; memset(&resolve, 0, sizeof resolve);
  resolve.module.id = execution.owner.provider; resolve.module.artifact_path = reference->requested_path;
  resolve.module.digest = reference->digest; resolve.module.revision = revision;
  resolve.role = "qagame"; resolve.bytes = bytes; resolve.length = length;
  resolve.has_abi_profile = true; resolve.abi_profile = abi_profile;
  if (!qa_qvm_resolve_artifact(resolve, &out->artifact)) qa_range_error("Out of memory");
  free(bytes);
  /* The resolved artifact borrows the revision text: keep it with the prepared game. */
  out->artifact.module.revision = strdup(revision);
  if (out->artifact.is_typescript) qa_range_error("Selected Q3 guest artifact did not resolve to bytecode");
  out->module = out->artifact.module;
  equipment_view_init(&out->equipment, &out->artifact);
  bool private_inventory = false;
  if (declaration.declaration.primary != NULL) {
    qa_save_reader primary;
    if (!qa_save_reader_init(&primary, declaration.declaration.primary, "qvm.primary")) qa_range_error("Out of memory");
    qa_save_reader items = child(&primary, "items");
    out->items = qa_qvm_parse_item_layout(&items); out->has_items = true;
    qa_save_reader_destroy(&items);
    qa_save_reader inventory = child(&primary, "inventory"), storage = child(&inventory, "storage");
    private_inventory = storage.value != NULL;
    qa_save_reader_destroy(&storage); qa_save_reader_destroy(&inventory);
    qa_save_reader_destroy(&primary);
  }
  qa_q3_guest_qvm_item_layout layout;
  if (out->has_items) guest_layout_of(&out->items, &layout);
  qa_q3_guest_mounts guest_mounts = {mounts, mounts_open};
  out->weapons = qa_q3_guest_weapons(&out->equipment.artifact, guest_mounts, out->has_items ? &layout : NULL, private_inventory);
  if (declaration.declaration.primary == NULL) {
    qa_qvm_builtin_primary_profile(&out->artifact, out->weapons.values, out->weapons.count, &out->primary);
  } else if (declaration.resource == NULL || !out->has_items) {
    qa_save_reader primary;
    if (!qa_save_reader_init(&primary, declaration.declaration.primary, "qvm.primary")) qa_range_error("Out of memory");
    qa_save_reader_fail(&primary, "primary interface lost its declaration resource");
  } else {
    qa_save_reader primary;
    if (!qa_save_reader_init(&primary, declaration.declaration.primary, "qvm.primary")) qa_range_error("Out of memory");
    bool live = out->items.has_source && out->items.source && strcmp(out->items.source, "live") == 0;
    qa_qvm_catalog_entry *catalog = calloc(out->weapons.count ? out->weapons.count : 1, sizeof *catalog);
    if (!catalog) qa_range_error("Out of memory");
    for (size_t i = 0; i < out->weapons.count; i++) { catalog[i].weapon = (int64_t)out->weapons.values[i].weapon; catalog[i].item = out->weapons.values[i].item; }
    qa_qvm_read_primary_profile(&primary, &out->artifact, live ? NULL : catalog, live ? 0 : out->weapons.count, !live,
        &out->items, declaration.resource, &out->primary);
    free(catalog);
    qa_save_reader_destroy(&primary);
  }
  qa_qvm_compatibility_reading_destroy(&declaration);
}
void qa_app_prepare_q3_native_game(const qa_app_q3_game_execution *execution, qa_content_mounted *mounts,
                                   qa_app_prepared_q3_game *out) {
  memset(out, 0, sizeof *out);
  qa_app_prepare_q3_native(execution, mounts, &out->image);
  if (strcmp(out->image.image.profile, "quake-live-elf") == 0) {
    qa_app_prepared_q3_native_destroy(&out->image);
    qa_raise("Error", 0, 0, "%s",
             "Quake Live table-interface modules are not served by the server game: their 64-bit entity and client records "
             "are not located through G_LOCATE_GAME_DATA, and no Quake Live binary exists here to verify the layouts");
  }
  out->kind = "native";
  out->execution = *execution;
  out->module = out->image.image.module;
  out->resource = execution->artifact;
  out->weapons.values = calloc(QA_Q3_BASE_GUEST_WEAPON_COUNT, sizeof *out->weapons.values);
  if (!out->weapons.values) qa_range_error("Out of memory");
  memcpy(out->weapons.values, qa_q3_base_guest_weapons, QA_Q3_BASE_GUEST_WEAPON_COUNT * sizeof *out->weapons.values);
  out->weapons.count = QA_Q3_BASE_GUEST_WEAPON_COUNT;
  /* product undefined; primary { declaration, input, weapons, inventory, pickups, combat } all null (zeroed). */
}
void qa_app_prepared_q3_game_destroy(qa_app_prepared_q3_game *game) {
  if (!game) return;
  if (game->kind != NULL && strcmp(game->kind, "native") == 0) {
    qa_q3_guest_weapon_list_destroy(&game->weapons);
    qa_app_prepared_q3_native_destroy(&game->image);
    memset(game, 0, sizeof *game);
    return;
  }
  qa_q3_guest_weapon_list_destroy(&game->weapons);
  qa_qvm_primary_profile_free(&game->primary);
  free((void *)game->artifact.module.revision);
  qa_resolved_qvm_artifact_destroy(&game->artifact);
  free(game->equipment.instructions);
  memset(game, 0, sizeof *game);
}
const qa_q3_equipment_qvm_artifact *qa_app_prepared_q3_equipment_artifact(const qa_app_prepared_q3_game *prepared) {
  return prepared->kind != NULL && strcmp(prepared->kind, "native") == 0 ? NULL : &prepared->equipment.artifact;
}
bool qa_app_prepared_q3_guest_layout(const qa_app_prepared_q3_game *prepared, qa_q3_guest_qvm_item_layout *out) {
  if (!prepared->has_items) return false;
  guest_layout_of(&prepared->items, out);
  return true;
}
const qa_qvm_input_definition *qa_app_prepared_q3_primary_input(const qa_app_prepared_q3_game *prepared) { return prepared->primary.input; }
const qa_qvm_primary_weapon_profile *qa_app_prepared_q3_primary_weapons(const qa_app_prepared_q3_game *prepared) { return prepared->primary.weapons; }
const qa_qvm_primary_combat_profile *qa_app_prepared_q3_primary_combat(const qa_app_prepared_q3_game *prepared) { return prepared->primary.combat; }
const qa_qvm_inventory_profile *qa_app_prepared_q3_primary_inventory(const qa_app_prepared_q3_game *prepared) { return prepared->primary.inventory; }
const qa_qvm_pickup_profile *qa_app_prepared_q3_primary_pickups(const qa_app_prepared_q3_game *prepared) { return prepared->primary.pickups; }
const char *qa_app_prepared_q3_primary_declaration_digest(const qa_app_prepared_q3_game *prepared) {
  return prepared->primary.declaration ? prepared->primary.declaration->digest : NULL;
}
