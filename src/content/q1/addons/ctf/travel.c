/* Port of src/content/q1/addons/ctf/travel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/travel.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
/* Replace state's inventory with owned copies of entries. */
static void set_inventory(qa_q1_travel_state *state, const qa_inventory_entry *entries, size_t count) {
  qa_inventory_entry *inventory = malloc((count != 0 ? count : 1) * sizeof *inventory);
  qa_item_id *items = malloc((count != 0 ? count : 1) * sizeof *items);
  if (inventory == NULL || items == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    inventory[i] = entries[i];
    items[i] = dup_text(entries[i].item);
    inventory[i].item = items[i];
  }
  state->inventory = inventory;
  state->inventory_items = items;
  state->inventory_count = count;
}
static void copy_extensions(qa_q1_travel_state *state, const qa_q1_travel_extension *extensions, size_t count) {
  state->extensions = malloc((count != 0 ? count : 1) * sizeof *state->extensions);
  if (state->extensions == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    state->extensions[i].id = dup_text(extensions[i].id);
    state->extensions[i].length = extensions[i].length;
    state->extensions[i].bytes = malloc(extensions[i].length != 0 ? extensions[i].length : 1);
    if (state->extensions[i].bytes == NULL) qa_range_error("out of memory");
    if (extensions[i].length != 0) memcpy(state->extensions[i].bytes, extensions[i].bytes, extensions[i].length);
  }
  state->extension_count = count;
}

/** CTF SetNewParms always starts at 100, including rerelease nightmare co-op. */
qa_q1_travel_state qa_q1ad_new_q1_ctf_travel(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  qa_q1_travel_state base = qa_q1_new_travel(&game->options);
  bool lobby = qa_q1ad_ctf_start_map(state) && (game->world != NULL ? qa_q1_actor_number(game->world, "ctf.pregameOver") : 0) == 0;
  size_t count = base.inventory_count + 1;
  qa_inventory_entry *entries = malloc(count * sizeof *entries);
  if (entries == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < base.inventory_count; i++) {
    entries[i] = base.inventory[i];
    if (strcmp(entries[i].item, "q1:weapon/shotgun") == 0) entries[i].count = lobby ? 0 : 1;
    else if (strcmp(entries[i].item, "q1:ammo/shells") == 0) entries[i].count = lobby ? 0 : 40;
  }
  bool grapple = qa_q1ad_ctf_native_grapple_enabled(state) && !lobby &&
                 (qa_js_to_int32(qa_q1ad_ctf_teamplay(state)) & QA_Q1AD_CTF_DISABLE_GRAPPLE) == 0;
  entries[base.inventory_count] = (qa_inventory_entry){.item = "q1:ctf/weapon/grapple", .count = grapple ? 1 : 0, .capacity = 1};
  qa_q1_travel_state result = {.health = 100, .max_health = 100, .weapon = lobby ? "axe" : "shotgun", .armor = base.armor};
  set_inventory(&result, entries, count);
  free(entries);
  copy_extensions(&result, NULL, 0);
  if (lobby) result.armor.regular = (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_NONE};
  else result.armor.regular = (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q1, .points = 50, .absorption = 0.3,
                                                       .item = "q1:item_armor1"};
  qa_q1_travel_state_destroy(&base);
  return result;
}

/** SetChangeParms resets equipment even for living players; parm10/14/15 still travel. */
qa_q1_travel_state qa_q1ad_capture_q1_ctf_travel(qa_q1ad_ctf_state *state, qa_owned_actor actor) {
  qa_q1_travel_state saved = qa_q1_capture_travel(state->context->game, actor, NULL, NULL, (qa_q1_travel_policy){0});
  qa_q1_travel_state result = qa_q1ad_new_q1_ctf_travel(state);
  free(result.extensions);
  result.extensions = saved.extensions; /* moved: saved no longer owns them */
  result.extension_count = saved.extension_count;
  saved.extensions = NULL;
  saved.extension_count = 0;
  qa_q1_travel_state_destroy(&saved);
  return result;
}

qa_q1_travel_state qa_q1ad_decode_q1_ctf_travel(qa_q1ad_ctf_state *state, const qa_q1_travel_state *travel) {
  qa_q1_travel_state result;
  if (qa_q1ad_ctf_start_map(state)) {
    result = qa_q1ad_new_q1_ctf_travel(state);
    free(result.extensions);
  } else {
    result = *travel;
    set_inventory(&result, travel->inventory, travel->inventory_count);
  }
  copy_extensions(&result, travel->extensions, travel->extension_count);
  return result;
}

uint8_t *qa_q1ad_ctf_capture_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, size_t *out_length) {
  double last_team = qa_q1_health(state->context->game, actor) > 0 &&
                             (qa_q1ad_ctf_start_map(state) || state->services.observer(state->services.ctx, actor))
                         ? -1
                         : qa_q1ad_ctf_number(state, actor, "lastteam");
  double status = qa_q1ad_ctf_number(state, actor, "status");
  double access = qa_q1ad_ctf_number(state, actor, "access");
  qa_save_value *root = qa_save_value_object();
  qa_save_value *a = qa_save_value_number(last_team);
  qa_save_value *b = qa_save_value_number(status);
  qa_save_value *c = qa_save_value_number(access);
  if (root == NULL || a == NULL || b == NULL || c == NULL) {
    qa_save_value_free(root); qa_save_value_free(a); qa_save_value_free(b); qa_save_value_free(c);
    return NULL;
  }
  /* set takes ownership of the child on success only. */
  qa_save_value *children[3] = {a, b, c};
  static const char *const KEYS[3] = {"lastTeam", "status", "access"};
  for (int i = 0; i < 3; i++)
    if (!qa_save_value_set(root, KEYS[i], children[i])) {
      for (int j = i; j < 3; j++) qa_save_value_free(children[j]);
      qa_save_value_free(root);
      return NULL;
    }
  uint8_t *bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return bytes;
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_save_reader_fail(reader, "out of memory");
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
void qa_q1ad_ctf_restore_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, const uint8_t *bytes, size_t length) {
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q1:ctf:travel")) qa_save_reader_fail(&reader, "out of memory");
  double team = field_number(&reader, "lastTeam");
  qa_q1ad_ctf_set(state, actor, "lastteam", qa_q1ad_ctf_start_map(state) ? 1 : team);
  qa_q1ad_ctf_set(state, actor, "status", field_number(&reader, "status"));
  qa_q1ad_ctf_set(state, actor, "access", field_number(&reader, "access"));
  const char *color = qa_q1ad_ctf_number_team(team);
  if (!qa_q1ad_ctf_start_map(state) && color != NULL) {
    qa_gameplay_authority_set_traits(state->context->game->host.combat, qa_q1ad_ctf_owner(state, actor),
                                     (qa_combat_trait_changes){.has_team = true, .team = color});
    state->services.colors(state->services.ctx, actor, team - 1, team - 1);
  }
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
