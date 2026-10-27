/* Port of src/content/q1/addons/ctf/travel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/travel.h"

#include <string.h>

#include "qa/persistence/value.h"

/** CTF SetNewParms always starts at 100, including rerelease nightmare co-op. */
qa_q1ad_travel qa_q1ad_new_ctf_travel(const qa_q1ad_base_travel *base, qa_q1ad_ctf_state *state) {
  qa_q1ad_travel fresh = base->new_travel(base->ctx, state->context);
  qa_q1ad_actor *world = qa_q1ad_world(state->context);
  bool lobby = qa_q1ad_ctf_start_map(state) &&
      (world != NULL ? qa_q1ad_actor_number(world, "ctf.pregameOver") : 0) == 0;
  for (size_t i = 0; i < fresh.inventory_count; i++) {
    if (strcmp(fresh.inventory[i].item, "q1:weapon/shotgun") == 0)
      fresh.inventory[i].count = lobby ? 0 : 1;
    else if (strcmp(fresh.inventory[i].item, "q1:ammo/shells") == 0)
      fresh.inventory[i].count = lobby ? 0 : 40;
  }
  if (fresh.inventory_count < QA_Q1AD_TRAVEL_ITEMS) {
    bool grapple = qa_q1ad_ctf_native_grapple_enabled(state) && !lobby &&
        ((int32_t)qa_q1ad_ctf_teamplay(state) & QA_Q1AD_CTF_DISABLE_GRAPPLE) == 0;
    fresh.inventory[fresh.inventory_count++] =
        (qa_q1ad_inventory_entry){"q1:ctf/weapon/grapple", grapple ? 1 : 0, 1};
  }
  fresh.health = 100;
  fresh.max_health = 100;
  fresh.weapon = lobby ? "axe" : "shotgun";
  fresh.extension_bytes = NULL;
  fresh.extension_count = 0;
  fresh.armor_regular = lobby ? (qa_q1ad_armor){.kind = "none"}
      : (qa_q1ad_armor){.kind = "q1", .points = 50, .absorption = 0.3, .item = "q1:item_armor1"};
  return fresh;
}

/** SetChangeParms resets equipment even for living players; parm10/14/15 still travel. */
qa_q1ad_travel qa_q1ad_capture_q1_ctf_travel(const qa_q1ad_base_travel *base, qa_q1ad_ctf_state *state, qa_owned_actor actor) {
  qa_q1ad_travel saved = base->capture(base->ctx, state->context, actor);
  qa_q1ad_travel fresh = qa_q1ad_new_ctf_travel(base, state);
  fresh.extension_bytes = saved.extension_bytes;
  fresh.extension_count = saved.extension_count;
  return fresh;
}

qa_q1ad_travel qa_q1ad_decode_ctf_travel(const qa_q1ad_base_travel *base, qa_q1ad_ctf_state *state, const qa_q1ad_travel *travel) {
  if (!qa_q1ad_ctf_start_map(state)) return *travel;
  qa_q1ad_travel fresh = qa_q1ad_new_ctf_travel(base, state);
  fresh.extension_bytes = travel->extension_bytes;
  fresh.extension_count = travel->extension_count;
  return fresh;
}

uint8_t *qa_q1ad_ctf_capture_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, size_t *out_length) {
  bool observer = state->services.observer != NULL && state->services.observer(state->services.ctx, actor);
  double last_team = qa_q1ad_health(state->context, actor) > 0 &&
          (qa_q1ad_ctf_start_map(state) || observer)
      ? -1 : qa_q1ad_ctf_number(state, actor, "lastteam");
  qa_save_value *root = qa_save_value_object();
  if (root == NULL) return NULL;
  qa_save_value *a = qa_save_value_number(last_team);
  qa_save_value *b = qa_save_value_number(qa_q1ad_ctf_number(state, actor, "status"));
  qa_save_value *c = qa_save_value_number(qa_q1ad_ctf_number(state, actor, "access"));
  if (a == NULL || b == NULL || c == NULL || !qa_save_value_set(root, "lastTeam", a) ||
      !qa_save_value_set(root, "status", b) || !qa_save_value_set(root, "access", c)) {
    qa_save_value_free(root); qa_save_value_free(a); qa_save_value_free(b); qa_save_value_free(c);
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
void qa_q1ad_ctf_restore_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, const uint8_t *bytes, size_t length) {
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q1:ctf:travel"))
    qa_save_reader_fail(&reader, "out of memory");
  double team = field_number(&reader, "lastTeam");
  qa_q1ad_ctf_set(state, actor, "lastteam", qa_q1ad_ctf_start_map(state) ? 1 : team);
  qa_q1ad_ctf_set(state, actor, "status", field_number(&reader, "status"));
  qa_q1ad_ctf_set(state, actor, "access", field_number(&reader, "access"));
  const char *color = qa_q1ad_ctf_number_team(team);
  if (!qa_q1ad_ctf_start_map(state) && color != NULL) {
    if (state->context->game.combat_set_team != NULL)
      state->context->game.combat_set_team(state->context->game.ctx, qa_q1ad_ctf_owner(state, actor), color);
    if (state->services.colors != NULL) state->services.colors(state->services.ctx, actor, team - 1, team - 1);
  }
  qa_save_reader_destroy(&reader);
  qa_save_value_free(decoded);
}
