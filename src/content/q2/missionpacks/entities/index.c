/* Port of src/content/q2/missionpacks/entities/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/entities/index.h"

#include <string.h>

void qa_q2mp_entities_init(qa_q2mp_entities *entities, qa_q2mp_pack pack, qa_q2mp_entity_hooks hooks) {
  memset(entities, 0, sizeof *entities);
  entities->pack = pack;
  if (pack == QA_Q2MP_PACK_XATRIX) qa_q2mp_xatrix_entities_init(&entities->xatrix, hooks);
  else qa_q2mp_rogue_entities_init(&entities->rogue, hooks);
  entities->has_movers = pack == QA_Q2MP_PACK_ROGUE;
  if (entities->has_movers) qa_q2mp_rogue_movers_init(&entities->movers, hooks);
}
qa_q2_callback_definitions qa_q2mp_entities_callbacks(qa_q2mp_entities *entities) {
  qa_q2_callback_definitions source = entities->pack == QA_Q2MP_PACK_XATRIX
      ? qa_q2mp_xatrix_entities_callbacks(&entities->xatrix)
      : qa_q2mp_rogue_entities_callbacks(&entities->rogue);
  qa_q2_callback_definitions movers;
  memset(&movers, 0, sizeof movers);
  if (entities->has_movers) movers = qa_q2mp_rogue_movers_callbacks(&entities->movers);
  size_t i = 0;
  for (size_t j = 0; j < source.think_count; j++) entities->cb_think[i++] = source.think[j];
  for (size_t j = 0; j < movers.think_count; j++) entities->cb_think[i++] = movers.think[j];
  entities->cb_think_count = i;
  i = 0;
  for (size_t j = 0; j < source.use_count; j++) entities->cb_use[i++] = source.use[j];
  for (size_t j = 0; j < movers.use_count; j++) entities->cb_use[i++] = movers.use[j];
  entities->cb_use_count = i;
  i = 0;
  for (size_t j = 0; j < source.touch_count; j++) entities->cb_touch[i++] = source.touch[j];
  for (size_t j = 0; j < movers.touch_count; j++) entities->cb_touch[i++] = movers.touch[j];
  entities->cb_touch_count = i;
  i = 0;
  for (size_t j = 0; j < source.die_count; j++) entities->cb_die[i++] = source.die[j];
  for (size_t j = 0; j < movers.die_count; j++) entities->cb_die[i++] = movers.die[j];
  entities->cb_die_count = i;
  i = 0;
  for (size_t j = 0; j < source.blocked_count; j++) entities->cb_blocked[i++] = source.blocked[j];
  for (size_t j = 0; j < movers.blocked_count; j++) entities->cb_blocked[i++] = movers.blocked[j];
  entities->cb_blocked_count = i;
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = entities->cb_think; defs.think_count = entities->cb_think_count;
  defs.use = entities->cb_use; defs.use_count = entities->cb_use_count;
  defs.touch = entities->cb_touch; defs.touch_count = entities->cb_touch_count;
  defs.die = entities->cb_die; defs.die_count = entities->cb_die_count;
  defs.blocked = entities->cb_blocked; defs.blocked_count = entities->cb_blocked_count;
  return defs;
}
bool qa_q2mp_entities_spawn(qa_q2mp_entities *entities, qa_q2_entity *entity, qa_q2_game_services *game) {
  bool spawned = entities->pack == QA_Q2MP_PACK_XATRIX
      ? qa_q2mp_xatrix_entities_spawn(&entities->xatrix, entity, game)
      : qa_q2mp_rogue_entities_spawn(&entities->rogue, entity, game);
  return spawned || (entities->has_movers && qa_q2mp_rogue_movers_spawn(&entities->movers, entity, game));
}
qa_q2mp_rogue_entities_checkpoint qa_q2mp_entities_capture(qa_q2mp_entities *entities) {
  if (entities->pack == QA_Q2MP_PACK_ROGUE) return qa_q2mp_rogue_entities_capture(&entities->rogue);
  qa_q2mp_rogue_entities_checkpoint cp = {0};
  return cp;
}
void qa_q2mp_entities_restore(qa_q2mp_entities *entities, qa_q2mp_rogue_entities_checkpoint state) {
  if (entities->pack == QA_Q2MP_PACK_ROGUE) qa_q2mp_rogue_entities_restore(&entities->rogue, state);
}
