/* Port of src/app/bootstrap/simulation/rerelease-guest-player.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_player.h"
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/foundation/support.h"
static double read_element(qa_double_view stats, size_t index) { return index < stats.count ? stats.values[index] : 0; }
qa_app_sim_player_view qa_app_sim_rerelease_guest_player_view(qa_q2_rerelease_player_state state) {
  qa_vec3 origin = state.movement.origin, offset = state.view.view_offset;
  qa_app_sim_player_view out; memset(&out, 0, sizeof out);
  out.origin = (qa_vec3){(float)(origin.x + offset.x), (float)(origin.y + offset.y), (float)(origin.z + offset.z)};
  out.view_height = state.movement.view_height;
  out.angles = state.view.view_angles;
  out.has_kick_angles = true; out.kick_angles = state.view.kick_angles;
  out.has_field_of_view = true; out.field_of_view = state.view.fov;
  out.has_blend = true; out.blend = state.screen_blend;
  out.has_damage_blend = true; out.damage_blend = state.damage_blend;
  return out;
}
qa_app_sim_player_ui qa_app_sim_rerelease_guest_player_ui(qa_q2_rerelease_player_state state,
    const qa_app_sim_rerelease_guest_configstring *configstrings, size_t configstring_count, qa_provider_reference source,
    const qa_q2w_definition *definitions, size_t definition_count,
    const qa_weapon_hud_status *(*weapon_status)(void *ctx, const qa_q2w_definition *definition, double ammo, qa_provider_reference source), void *weapon_status_ctx) {
  if (definitions == NULL) { definitions = qa_q2w_base_weapons; definition_count = qa_q2w_base_weapon_count; }
  qa_app_q2_layout layout = qa_app_q2_application_layout((qa_q2_protocol_identity){"q2-rerelease", 1038, 0, 0});
  const char *model = NULL;
  if (state.view.gun_index != 0) {
    double wanted = layout.models + state.view.gun_index;
    for (size_t i = 0; i < configstring_count; i++) if (configstrings[i].index == wanted) model = configstrings[i].value;
  }
  const qa_q2w_definition *weapon = NULL;
  if (model) for (size_t i = 0; i < definition_count; i++)
    if (definitions[i].view_model && strcmp(definitions[i].view_model, model) == 0) { weapon = &definitions[i]; break; }
  double armor = read_element(state.view.stats, 5), ammo = read_element(state.view.stats, 3);
  qa_app_sim_player_ui out; memset(&out, 0, sizeof out);
  out.health = read_element(state.view.stats, 1);
  out.armor.powered.kind = "none";
  if (armor == 0) out.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  else { out.armor.regular.kind = QA_REGULAR_ARMOR_Q2; out.armor.regular.points = armor; out.armor.regular.item = "q2:remote-armor"; }
  out.weapon_status = weapon_status ? weapon_status(weapon_status_ctx, weapon, ammo, source) : NULL;
  out.arsenal_warning = "none";
  out.active_weapon = weapon ? weapon->item : NULL;
  if (weapon && weapon->ammo) { out.has_ammo = true; out.ammo_item = weapon->ammo; out.ammo_count = ammo; }
  return out;
}
qa_q2_rerelease_user_command qa_app_sim_rerelease_guest_local_command(qa_actor_command input, qa_q2_rerelease_player_state state) {
  if (input.source.kind == QA_COMMAND_SOURCE_REMOTE_CLIENT)
    qa_range_error("Native remote commands must enter ClientThink through their wire endpoint");
  if (input.command.kind != QA_USER_COMMAND_Q2_RERELEASE)
    qa_range_error("An API2023 native player requires rerelease Quake II movement commands");
  qa_q2_rerelease_user_command out = input.command.as.q2_rerelease;
  qa_vec3 angles = out.angles, delta = state.movement.delta_angles;
  out.angles = (qa_vec3){(float)(angles.x - delta.x), (float)(angles.y - delta.y), (float)(angles.z - delta.z)};
  return out;
}
