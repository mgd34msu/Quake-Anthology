/* Port of src/content/q1/addons/monsters/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/index.h"

#include "qa/content/q1/addons/monsters/ai/path.h"
#include "qa/content/q1/addons/monsters/ai/targets.h"
#include "qa/content/q1/addons/monsters/bosses/final.h"
#include "qa/content/q1/addons/monsters/bosses/ghost.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/orb.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/bosses/sacrifice.h"
#include "qa/content/q1/addons/monsters/bosses/szombie.h"
#include "qa/content/q1/addons/monsters/demodog.h"
#include "qa/content/q1/addons/monsters/heavy/index.h"
#include "qa/content/q1/addons/monsters/infected/index.h"
#include "qa/content/q1/addons/monsters/ordinary/index.h"
#include "qa/content/q1/addons/monsters/startup.h"

void qa_q1am_register_addon_monsters(qa_q1ad_context *context) {
  qa_q1am_register_ordinary_addon_monsters(context);
  if (context->program == QA_Q1AD_MG3) {
    qa_q1am_register_mg3_path_targets(context);
    qa_q1am_register_mg3_demodog(context);
    qa_q1am_register_mg3_infected(context);
    qa_q1am_register_mg3_heavy(context);
    qa_q1am_register_sacrifice(context);
    qa_q1am_register_ghost(context);
    qa_q1am_register_orb(context);
    qa_q1am_register_shub_zombie(context);
    qa_q1am_register_oldnew(context);
    qa_q1am_register_final_boss(context);
  }
}
void qa_q1am_release_addon_monsters(qa_q1_services *game) {
  qa_q1am_release_ordinary_addon_monsters(game);
  qa_q1am_release_mg3_demodog(game);
  qa_q1am_release_mg3_infected(game);
  qa_q1am_release_mg3_heavy(game);
  qa_q1am_release_final_boss(game);
  qa_q1am_release_boss_controllers(game);
  qa_q1am_release_mg3_monster_startup(game);
  qa_q1am_release_mg3_monster_navigation(game);
}
