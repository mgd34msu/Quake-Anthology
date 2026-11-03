/* Port of src/content/q1/base/animation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_ANIMATION_H
#define QA_CONTENT_Q1_BASE_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/foundation/entity_services.h"

/* TS MonsterAi string union. */
typedef enum qa_q1_monster_ai {
  QA_Q1_AI_STAND, QA_Q1_AI_WALK, QA_Q1_AI_RUN, QA_Q1_AI_CHARGE_SIDE,
  QA_Q1_AI_MELEE_SIDE, QA_Q1_AI_CHARGE, QA_Q1_AI_MELEE, QA_Q1_AI_PAINFORWARD,
  QA_Q1_AI_TURN, QA_Q1_AI_FACE, QA_Q1_AI_PAIN, QA_Q1_AI_FORWARD
} qa_q1_monster_ai;
const char *qa_q1_monster_ai_name(qa_q1_monster_ai mode);
bool qa_q1_monster_ai_parse(const char *name, qa_q1_monster_ai *out);

/* TS MonsterOperation union on kind. Unused variant fields stay zero. */
typedef enum qa_q1_monster_operation_kind {
  QA_Q1_OP_AI, QA_Q1_OP_SOUND, QA_Q1_OP_SOLID, QA_Q1_OP_LIGHTSTYLE, QA_Q1_OP_ACTION
} qa_q1_monster_operation_kind;
typedef struct qa_q1_monster_operation {
  qa_q1_monster_operation_kind kind;
  qa_q1_monster_ai mode; double distance; /* ai */
  const char *path; qa_q1_sound_channel channel; double attenuation; /* sound */
  const char *comparison; /* "greater"|"less" */
  bool has_chance; double chance; /* sound: TS chance|null */
  const char *solid; /* solid: "none" */
  const char *pattern; /* lightstyle */
  const char *name; /* action */
} qa_q1_monster_operation;

/* TS MonsterFrame. The frame's map key is carried in frame tables. */
typedef struct qa_q1_monster_frame {
  double frame; const char *next;
  const qa_q1_monster_operation *operations; size_t operation_count;
} qa_q1_monster_frame;

/* A TS ReadonlyMap<string, MonsterFrame> as an ordered table. */
typedef struct qa_q1_monster_frame_entry {
  const char *name; qa_q1_monster_frame frame;
} qa_q1_monster_frame_entry;
typedef struct qa_q1_monster_frame_table {
  const qa_q1_monster_frame_entry *entries; size_t count;
} qa_q1_monster_frame_table;
const qa_q1_monster_frame *qa_q1_monster_frame_table_get(const qa_q1_monster_frame_table *table, const char *name);
bool qa_q1_monster_frame_table_has(const qa_q1_monster_frame_table *table, const char *name);

#endif
