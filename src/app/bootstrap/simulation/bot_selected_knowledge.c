/* Port of src/app/bootstrap/simulation/bot-selected-knowledge.ts:
 * maxRange/priority staging; botlib weapon parsing stays with the
 * landed WeaponAi behind bot assets. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_selected_knowledge.h"
#include <math.h>
double qa_app_sim_q3_knowledge_max_range(double weapon) { return weapon == 1 ? 60 : weapon == 6 ? 768 : 4096; }
double qa_app_sim_q3_knowledge_priority(double damage, double reload_seconds) {
  double d = reload_seconds > 0.05 ? reload_seconds : 0.05;
  return damage / d;
}
