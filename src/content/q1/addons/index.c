/* Port of src/content/q1/addons/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons.h"

static void spawn_worldspawn_mg3(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.spawn_map_actor != NULL) context->game.spawn_map_actor(context->game.ctx, entity);
  qa_q1ad_set_number(context, entity, "isHordeMode", qa_q1ad_cvar(context, "horde"));
  qa_q1ad_set_number(context, entity, "cheats_allowed", qa_q1ad_cvar(context, "sv_cheats"));
}

qa_q1ad_context *qa_q1ad_register_q1_campaign_addons(qa_q1ad_context *context, qa_q1ad_program program,
                                                  qa_q1ad_services services, qa_q1ad_game game) {
  (void)qa_q1ad_context_init(context, program, services, game);
  if (program == QA_Q1AD_MG3)
    qa_q1ad_register_spawn(context, "worldspawn", false, spawn_worldspawn_mg3);
  qa_q1ad_register_campaign_addons(context);
  qa_q1ad_register_triggers(context);
  qa_q1ad_register_base_triggers(context);
  qa_q1ad_register_field_triggers(context);
  qa_q1ad_register_brushes(context);
  qa_q1ad_register_effects(context);
  qa_q1ad_register_fog(context);
  qa_q1ad_register_lights(context);
  qa_q1ad_register_corpses(context);
  /* registerAddonMonsters(context): addons/monsters/ is outside this slice. */
  if (context->game.register_rule != NULL)
    context->game.register_rule(context->game.ctx, "q1:addon-monsters");
  if (program == QA_Q1AD_MG3) {
    qa_q1ad_register_ropes(context);
    qa_q1ad_register_mg3_items(context);
  }
  return context;
}
