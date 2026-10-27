/* Port of src/content/q1/addons/monsters/ordinary/index.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_INDEX_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_ordinary_monster {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
  qa_q1am_source source;
  qa_q1am_frame extra_storage[9];
  /* Per-monster merged table storage (TS builds a fresh Map). */
  const qa_q1am_frame *merged_ptrs[16];
  qa_q1am_frame merged_storage[16];
  qa_q1am_frame_table merged_table;
  struct qa_q1am_ordinary_monster *next;
} qa_q1am_ordinary_monster;

void qa_q1am_ordinary_monster_init(qa_q1am_ordinary_monster *monster, qa_q1am_context *context,
                                   qa_q1am_actor *entity, bool mg3);
qa_q1am_monster *qa_q1am_ordinary_addon_monster(qa_q1am_context *context, qa_q1am_actor *entity);
/* Returns the registry head (TS ReadonlyMap values in insertion order). */
qa_q1am_ordinary_monster *qa_q1am_register_ordinary_addon_monsters(qa_q1am_context *context);

#endif
