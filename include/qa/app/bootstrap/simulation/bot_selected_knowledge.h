/* Port of src/app/bootstrap/simulation/bot-selected-knowledge.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_SELECTED_KNOWLEDGE_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_SELECTED_KNOWLEDGE_H
#include <stddef.h>
#include "qa/bots/behavior/director.h"
#include "qa/bots/behavior/rerelease/data/knowledge.h"
double qa_app_sim_q3_knowledge_max_range(double weapon);
double qa_app_sim_q3_knowledge_priority(double damage, double reload_seconds);
/* TS BotWeaponT[]: weapons[i].entry points into entries; strings are static
 * (item ids, flag names). Free with qa_app_sim_q3_weapon_knowledge_free. */
typedef struct qa_app_sim_q3_weapon_knowledge {
  qa_bot_weapon_rec *weapons; size_t count;
  qa_weapon_entry *entries;
  const char *(*flags)[3];
} qa_app_sim_q3_weapon_knowledge;
/* TS nativeQ3WeaponKnowledge: parse the selected Q3 source weapon metadata
 * once and retain values, not botlib allocations. Raises "Error" when the
 * metadata is unavailable or a script print reports (TS print throws). */
qa_app_sim_q3_weapon_knowledge qa_app_sim_native_q3_weapon_knowledge(const qa_bot_catalog_files_bots_a *files);
void qa_app_sim_q3_weapon_knowledge_free(qa_app_sim_q3_weapon_knowledge *knowledge);
#endif
