/* Port of src/app/bootstrap/simulation/classic-guest-player.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/state.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static double stat_at(qa_q2_player_state s, int64_t i) { return qa_q2_read_element_f64(s.view.stats.values, s.view.stats.count, i); }
qa_app_sim_player_view qa_app_sim_classic_guest_player_view(qa_q2_player_state state) {
  qa_app_sim_player_view out = {0};
  out.origin.x = state.movement.origin_eighths.values[0] / 8 + state.view.view_offset.x;
  out.origin.y = state.movement.origin_eighths.values[1] / 8 + state.view.view_offset.y;
  out.origin.z = state.movement.origin_eighths.values[2] / 8;
  out.view_height = state.view.view_offset.z;
  out.angles = state.view.view_angles;
  out.has_kick_angles = true; out.kick_angles = state.view.kick_angles;
  out.has_field_of_view = true; out.field_of_view = state.view.fov;
  out.has_blend = true; out.blend = state.blend;
  return out;
}
static void display_name(const char *value, char *out, size_t size) {
  size_t o = 0; bool word_start = true;
  for (size_t i = 0; value[i] != '\0' && o + 1 < size; i++) {
    char c = value[i] == '_' || value[i] == '-' ? ' ' : value[i];
    if (word_start && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    word_start = !(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    out[o++] = c;
  }
  out[o] = '\0';
}
qa_app_sim_player_ui qa_app_sim_classic_guest_player_ui(qa_q2_player_state state,
    const qa_app_sim_configstring *configstrings, size_t configstring_count,
    qa_provider_reference source, const qa_q2w_definition *definitions, size_t definition_count,
    qa_weapon_hud_status *weapon_status_scratch, char *label_scratch, size_t label_scratch_size) {
  if (definitions == NULL) { definitions = qa_q2w_base_weapons; definition_count = qa_q2w_base_weapon_count; }
  qa_q2_protocol_identity proto = {"q2-classic", 34, 0, 0};
  qa_app_q2_layout layout = qa_app_q2_application_layout(proto);
  const char *model = NULL;
  if (state.view.gun_index != 0) {
    double want = layout.models + state.view.gun_index;
    for (size_t i = 0; i < configstring_count; i++) if (configstrings[i].index == want) model = configstrings[i].value;
  }
  const qa_q2w_definition *weapon = NULL;
  if (model != NULL) for (size_t i = 0; i < definition_count; i++)
    if (definitions[i].view_model != NULL && strcmp(definitions[i].view_model, model) == 0) weapon = &definitions[i];
  double armor = stat_at(state, 5), ammo = stat_at(state, 3);
  qa_app_sim_player_ui out = {0};
  out.health = stat_at(state, 1);
  out.armor.powered.kind = "none";
  if (armor == 0) { out.armor.regular.kind = QA_REGULAR_ARMOR_NONE; }
  else { out.armor.regular.kind = QA_REGULAR_ARMOR_Q2; out.armor.regular.points = armor;
    out.armor.regular.normal_protection = 0; out.armor.regular.energy_protection = 0; out.armor.regular.item = "q2:remote-armor"; }
  out.arsenal_warning = "none";
  out.active_weapon = weapon != NULL ? weapon->item : NULL;
  if (weapon != NULL && weapon->ammo != NULL) { out.has_ammo = true; out.ammo_item = weapon->ammo; out.ammo_count = ammo; }
  if (weapon != NULL && weapon_status_scratch != NULL) {
    qa_weapon_hud_status st = {0}; st.source = source; st.item = weapon->item;
    const char *label = qa_q2_base_weapon_display_name(weapon->item);
    if (label == NULL) { display_name(weapon->name, label_scratch, label_scratch_size); label = label_scratch; }
    st.label = label;
    if (weapon->ammo == NULL) st.ammo_is_finite = false;
    else { st.ammo_is_finite = true; st.ammo_item = weapon->ammo; st.ammo_count = ammo;
      st.has_ammo_to_start = ammo >= weapon->quantity; st.low = ammo <= weapon->warning; }
    *weapon_status_scratch = st; out.weapon_status = weapon_status_scratch;
  }
  return out;
}
qa_q2_user_command qa_app_sim_classic_guest_local_command(const char *source_kind,
    qa_user_command command, qa_q2_player_state state) {
  if (is(source_kind, "remote-client")) qa_range_error("Native remote commands must enter ClientThink through their wire endpoint");
  if (command.kind != QA_USER_COMMAND_Q2) qa_range_error("An API 3 native player requires classic Quake II movement commands");
  qa_q2_user_command out = command.as.q2;
  for (int i = 0; i < 3; i++) {
    /* (angle - delta) & 65535: ToInt32 of the number difference */
    out.angle_shorts.values[i] = (double)(qa_js_to_int32(out.angle_shorts.values[i] - state.movement.delta_angle_shorts.values[i]) & 65535);
  }
  return out;
}
