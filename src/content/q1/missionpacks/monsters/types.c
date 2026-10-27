/* Port of src/content/q1/missionpacks/monsters/types.ts (types only;
 * this file carries the MonsterAi names and frame-table lookup).
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/types.h"

#include <string.h>

const char *qa_q1mpm_ai_name(qa_q1mpm_ai mode) {
  switch (mode) {
    case QA_Q1MPM_AI_STAND: return "stand";
    case QA_Q1MPM_AI_WALK: return "walk";
    case QA_Q1MPM_AI_RUN: return "run";
    case QA_Q1MPM_AI_CHARGE_SIDE: return "charge_side";
    case QA_Q1MPM_AI_MELEE_SIDE: return "melee_side";
    case QA_Q1MPM_AI_CHARGE: return "charge";
    case QA_Q1MPM_AI_MELEE: return "melee";
    case QA_Q1MPM_AI_PAINFORWARD: return "painforward";
    case QA_Q1MPM_AI_TURN: return "turn";
    case QA_Q1MPM_AI_FACE: return "face";
    case QA_Q1MPM_AI_PAIN: return "pain";
    case QA_Q1MPM_AI_FORWARD: return "forward";
  }
  return "stand";
}

const qa_q1mpm_frame_entry *qa_q1mpm_frame_table_get(const qa_q1mpm_frame_table *table, const char *name) {
  if (table == NULL || name == NULL) return NULL;
  for (size_t i = 0; i < table->count; i++)
    if (strcmp(table->entries[i].name, name) == 0) return &table->entries[i];
  return NULL;
}

bool qa_q1mpm_frame_table_has(const qa_q1mpm_frame_table *table, const char *name) {
  return qa_q1mpm_frame_table_get(table, name) != NULL;
}
