/* Port of src/content/q1/addons/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons.h"

#include "qa/content/q1/foundation/spawns.h"
#include "qa/foundation/support.h"

static void spawn_worldspawn_mg3(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_spawn_map_actor(game, entity);
  qa_q1ad_set_number(context, entity, "isHordeMode", context->services.cvar(context->services.ctx, "horde"));
  qa_q1ad_set_number(context, entity, "cheats_allowed", context->services.cvar(context->services.ctx, "sv_cheats"));
}

qa_q1ad_context *qa_q1ad_register_q1_campaign_addons(qa_q1_base *base, qa_q1ad_program program, qa_q1ad_services services) {
  qa_q1ad_context *context = qa_q1ad_context_create(base, program, services);
  if (context == NULL) return NULL;
  if (program == QA_Q1AD_MG3) qa_q1_register_spawn(qa_q1_base_game(base), "worldspawn", spawn_worldspawn_mg3, context);
  qa_q1ad_register_campaign_addons(context);
  qa_q1ad_register_triggers(context);
  qa_q1ad_register_base_triggers(context);
  qa_q1ad_register_field_triggers(context);
  qa_q1ad_register_brushes(context);
  qa_q1ad_register_effects(context);
  qa_q1ad_register_fog(context);
  qa_q1ad_register_lights(context);
  qa_q1ad_register_corpses(context);
  qa_q1am_register_addon_monsters(context);
  context->addon_monsters = true;
  if (program != QA_Q1AD_DOPA) qa_q1ad_register_coop_spawns(context);
  if (program == QA_Q1AD_MG3) {
    qa_q1ad_register_ropes(context);
    context->mg3_items = qa_q1ad_register_mg3_items(context);
    if (context->mg3_items == NULL) qa_range_error("out of memory");
  }
  return context;
}
