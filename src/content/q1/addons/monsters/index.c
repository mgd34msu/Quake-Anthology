/* Port of src/content/q1/addons/monsters/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/index.h"

#include <string.h>

#include "qa/content/q1/addons/monsters/ai/targets.h"
#include "qa/content/q1/addons/monsters/bosses/final.h"
#include "qa/content/q1/addons/monsters/bosses/ghost.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/orb.h"
#include "qa/content/q1/addons/monsters/bosses/sacrifice.h"
#include "qa/content/q1/addons/monsters/bosses/szombie.h"
#include "qa/content/q1/addons/monsters/demodog.h"
#include "qa/content/q1/addons/monsters/heavy/index.h"
#include "qa/content/q1/addons/monsters/infected/index.h"
#include "qa/content/q1/addons/monsters/ordinary/index.h"

void qa_q1am_register_addon_monsters(qa_q1am_context *context, qa_q1am_mg3_heavy *heavy_runtime) {
  qa_q1am_register_ordinary_addon_monsters(context);
  if (strcmp(context->program, "mg3") == 0) {
    qa_q1am_register_mg3_path_targets(context);
    qa_q1am_register_mg3_demodog(context);
    qa_q1am_register_mg3_infected(context);
    qa_q1am_register_mg3_heavy(context, heavy_runtime);
    qa_q1am_register_sacrifice(context);
    qa_q1am_register_ghost(context);
    qa_q1am_register_orb(context);
    qa_q1am_register_shub_zombie(context);
    qa_q1am_register_oldnew(context);
    qa_q1am_register_final_boss(context);
  }
}
