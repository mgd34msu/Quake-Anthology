/* Port of src/content/q1/base/animation.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/animation.h"

#include <string.h>

static const char *const AI_NAMES[] = {
  "stand", "walk", "run", "charge_side", "melee_side", "charge",
  "melee", "painforward", "turn", "face", "pain", "forward"
};
const char *qa_q1_monster_ai_name(qa_q1_monster_ai mode) { return AI_NAMES[mode]; }
bool qa_q1_monster_ai_parse(const char *name, qa_q1_monster_ai *out) {
  for (size_t i = 0; i < sizeof AI_NAMES / sizeof *AI_NAMES; i++) {
    if (strcmp(name, AI_NAMES[i]) == 0) { *out = (qa_q1_monster_ai)i; return true; }
  }
  return false;
}
const qa_q1_monster_frame *qa_q1_monster_frame_table_get(const qa_q1_monster_frame_table *table, const char *name) {
  if (table == NULL) return NULL;
  for (size_t i = 0; i < table->count; i++)
    if (strcmp(table->entries[i].name, name) == 0) return &table->entries[i].frame;
  return NULL;
}
bool qa_q1_monster_frame_table_has(const qa_q1_monster_frame_table *table, const char *name) {
  return qa_q1_monster_frame_table_get(table, name) != NULL;
}
