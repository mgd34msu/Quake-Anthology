/* Port of src/content/composition/q1-expansion-arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1_expansion_arsenal.h"

#include <stddef.h>

const char *qa_comp_selected_q1_program_name(qa_comp_selected_q1_program program) {
  static const char *const NAMES[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3"};
  return NAMES[program];
}

static void emit_developer(void *ctx, const char *text) {
  qa_comp_selected_q1_mission_weapons *weapons = (qa_comp_selected_q1_mission_weapons *)ctx;
  weapons->services->emit_developer_message(weapons->services->ctx, text);
}

void qa_comp_register_selected_q1_mission_weapons(qa_comp_selected_q1_mission_weapons *out,
                                                  qa_comp_q1_game *game,
                                                  qa_comp_selected_q1_program program,
                                                  const qa_comp_q1_addon_services *services,
                                                  const qa_comp_q1_arsenal_hooks *hooks) {
  *out = (qa_comp_selected_q1_mission_weapons){game, program, services, hooks, NULL, NULL};
  void *ctx = hooks->ctx;
  if (program == QA_COMP_SELECTED_Q1_HIPNOTIC) {
    hooks->register_hipnotic_weapons(ctx, game);
    return;
  }
  if (program == QA_COMP_SELECTED_Q1_ROGUE) {
    out->arsenal = hooks->register_mission_pack_arsenal(ctx, game, "rogue");
    return;
  }
  if (program == QA_COMP_SELECTED_Q1_MG3) {
    void *base = hooks->create_base_provider(ctx, game, false);
    out->context = hooks->new_addon_context(ctx, base, "mg3", services);
    hooks->register_mg3_items(ctx, out->context);
  }
}

bool qa_comp_selected_q1_mission_weapons_impulse(qa_comp_selected_q1_mission_weapons *weapons,
                                                 qa_comp_q1_player_state *player, double value) {
  void *ctx = weapons->hooks->ctx;
  if (weapons->program == QA_COMP_SELECTED_Q1_HIPNOTIC)
    return weapons->hooks->mission_weapon_impulse(ctx, weapons->game, player, "hipnotic", value);
  if (weapons->program == QA_COMP_SELECTED_Q1_ROGUE)
    return weapons->hooks->arsenal_impulse(ctx, weapons->arsenal, player->actor.id, value);
  if (weapons->program == QA_COMP_SELECTED_Q1_MG3)
    return weapons->hooks->mg3_item_impulse(ctx, weapons->context, player->actor.id, value, emit_developer,
                                            weapons) ||
           qa_comp_q1_weapon_impulse(weapons->game, player, value);
  return qa_comp_q1_weapon_impulse(weapons->game, player, value);
}

void qa_comp_selected_q1_mission_weapons_frame(qa_comp_selected_q1_mission_weapons *weapons, double elapsed_seconds) {
  if (weapons->program == QA_COMP_SELECTED_Q1_MG3)
    weapons->hooks->context_frame(weapons->hooks->ctx, weapons->context, elapsed_seconds);
}

void qa_comp_selected_q1_mission_weapons_prepare_pickup(qa_comp_selected_q1_mission_weapons *weapons,
                                                        qa_comp_q1_player_state *player) {
  if (weapons->program == QA_COMP_SELECTED_Q1_ROGUE)
    weapons->hooks->arsenal_enable_combos(weapons->hooks->ctx, weapons->arsenal, player);
}
