/* Port of src/app/bootstrap/simulation/arsenal-intent.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_q3_arsenal_controls qa_app_sim_resolve_q3_arsenal_controls(qa_arsenal_state arsenal,
    const qa_arsenal_intent *intent, qa_movement_command command, const char *product) {
  if (!is(arsenal.state.kind, "q3")) qa_range_error("Q3 controls require the selected Q3 arsenal");
  double requested = command.kind == QA_USER_COMMAND_Q3 ? command.as.q3.weapon : arsenal.state.source_weapon;
  if (intent != NULL) {
    if (intent->provider == NULL || arsenal.provider == NULL || strcmp(intent->provider, arsenal.provider) != 0)
      qa_range_error("Arsenal command belongs to a different provider");
    requested = arsenal.state.source_weapon;
    if (intent->weapon != NULL) {
      const qa_q3_weapon_item *weapon = NULL;
      for (size_t i = 0; i < (size_t)QA_Q3_WEAPON_ITEM_COUNT; i++)
        if (qa_q3_weapon_items[i].item != NULL && strcmp(qa_q3_weapon_items[i].item, intent->weapon) == 0) { weapon = &qa_q3_weapon_items[i]; break; }
      /* Peers choose this string; a weapon outside the product is no request. */
      if (weapon != NULL && (!is(product, "baseq3") || weapon->weapon <= 10)) requested = weapon->weapon;
    }
  }
  double buttons = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.buttons
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.buttons
      : command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.buttons
      : command.kind == QA_USER_COMMAND_Q2_RERELEASE ? command.as.q2_rerelease.buttons : command.as.q3.buttons;
  long b = qa_js_to_int32(buttons); /* buttons & mask */
  qa_q3_arsenal_controls out;
  out.attack = (b & 1) != 0;
  out.use_holdable = intent != NULL ? intent->use_holdable : (command.kind == QA_USER_COMMAND_Q3 && (b & 4) != 0);
  out.requested_weapon = requested;
  return out;
}
