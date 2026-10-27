/* Port of src/content/q1/addons/monsters/heavy/index.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/index.h"

#include "qa/content/q1/addons/monsters/heavy/lava_man.h"
#include "qa/content/q1/addons/monsters/heavy/projectiles.h"
#include "qa/content/q1/addons/monsters/heavy/rune_knight.h"
#include "qa/content/q1/addons/monsters/heavy/super_shambler.h"

static qa_q1am_heavy_definition lava_definition;
qa_q1am_mg3_heavy *qa_q1am_register_mg3_heavy(qa_q1am_context *context, qa_q1am_mg3_heavy *runtime) {
  qa_q1am_mg3_heavy_init(runtime, context);
  qa_q1am_register_heavy_projectiles(context->game);
  qa_q1am_mg3_heavy_register(runtime, &qa_q1am_super_shambler_definition);
  qa_q1am_mg3_heavy_register(runtime, &qa_q1am_rune_knight_definition);
  qa_q1am_lava_man_definition(runtime, &lava_definition);
  qa_q1am_mg3_heavy_register(runtime, &lava_definition);
  return runtime;
}
