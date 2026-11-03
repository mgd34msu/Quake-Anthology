/* Port of src/content/composition/q1-expansion-arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1_expansion_arsenal.h"

#include <stddef.h>
#include <string.h>

#include "qa/content/q1/addons/items/commands.h"
#include "qa/content/q1/addons/items/index.h"
#include "qa/content/q1/missionpacks/selection.h"

const char *qa_comp_selected_q1_program_name(qa_comp_selected_q1_program program) {
  static const char *const NAMES[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3"};
  return NAMES[program];
}

/* TS text => services.emit({ kind: "developer-message", text }). */
static void emit_developer(void *ctx, const char *text) {
  qa_comp_selected_q1_mission_weapons *weapons = ctx;
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_DEVELOPER_MESSAGE;
  event.text = text;
  weapons->services.emit(weapons->services.ctx, &event);
}

void qa_comp_register_selected_q1_mission_weapons(qa_comp_selected_q1_mission_weapons *out,
                                                  qa_q1_services *game,
                                                  qa_comp_selected_q1_program program,
                                                  qa_q1ad_services services) {
  memset(out, 0, sizeof *out);
  out->game = game;
  out->program = program;
  out->services = services;
  if (program == QA_COMP_SELECTED_Q1_HIPNOTIC) {
    qa_q1mp_register_hipnotic_weapons(game);
  } else if (program == QA_COMP_SELECTED_Q1_ROGUE) {
    (void)qa_q1mp_register_mission_pack_arsenal(game, "rogue", &out->arsenal);
  } else if (program == QA_COMP_SELECTED_Q1_MG3) {
    qa_q1_base_options options;
    memset(&options, 0, sizeof options);
    options.has_official_campaign = true;
    options.official_campaign = false;
    out->base = qa_q1_base_create(game, &options);
    out->context = qa_q1ad_context_create(out->base, QA_Q1AD_MG3, services);
    out->context->mg3_items = qa_q1ad_register_mg3_items(out->context); /* the context frees it */
  }
}

void qa_comp_selected_q1_mission_weapons_destroy(qa_comp_selected_q1_mission_weapons *weapons) {
  if (weapons->program == QA_COMP_SELECTED_Q1_ROGUE) qa_q1mp_mission_pack_arsenal_destroy(&weapons->arsenal);
  if (weapons->context != NULL) qa_q1ad_context_destroy(weapons->context);
  if (weapons->base != NULL) qa_q1_base_destroy(weapons->base);
  memset(weapons, 0, sizeof *weapons);
}

bool qa_comp_selected_q1_mission_weapons_impulse(qa_comp_selected_q1_mission_weapons *weapons,
                                                 qa_q1_player_state *player, double value) {
  if (weapons->program == QA_COMP_SELECTED_Q1_HIPNOTIC)
    return qa_q1mp_mission_weapon_impulse(weapons->game, player, "hipnotic", value);
  if (weapons->program == QA_COMP_SELECTED_Q1_ROGUE)
    return qa_q1mp_arsenal_impulse(&weapons->arsenal, player->actor.id, value);
  if (weapons->program == QA_COMP_SELECTED_Q1_MG3)
    return qa_q1ad_handle_mg3_item_impulse(weapons->context, player->actor.id, value, emit_developer, weapons) ||
           qa_comp_q1_weapon_impulse(weapons->game, player, value);
  return qa_comp_q1_weapon_impulse(weapons->game, player, value);
}

void qa_comp_selected_q1_mission_weapons_frame(qa_comp_selected_q1_mission_weapons *weapons, double elapsed_seconds) {
  if (weapons->program == QA_COMP_SELECTED_Q1_MG3) qa_q1ad_frame(weapons->context, elapsed_seconds);
}

void qa_comp_selected_q1_mission_weapons_prepare_pickup(qa_comp_selected_q1_mission_weapons *weapons,
                                                        qa_q1_player_state *player) {
  if (weapons->program == QA_COMP_SELECTED_Q1_ROGUE) qa_q1mp_players_enable_combos(&weapons->arsenal.players, player);
}
