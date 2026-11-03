/* Port of src/content/q1/addons/monsters/bosses/frames/orb.ts. Generated mechanically from the TS
 * frame table via bun JSON dump; do not hand-edit entries. */
#include "qa/content/q1/addons/monsters/bosses/frames/orb.h"

static const qa_q1_monster_operation ops_0[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_stand1"},
};
static const qa_q1_monster_operation ops_1[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_walk1"},
};
static const qa_q1_monster_operation ops_2[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_side1"},
};
static const qa_q1_monster_operation ops_3[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_run1"},
};
static const qa_q1_monster_operation ops_4[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_fast1"},
};
static const qa_q1_monster_operation ops_6[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_fast3"},
};
static const qa_q1_monster_operation ops_7[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_fast4"},
};
static const qa_q1_monster_operation ops_8[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_fast5"},
};
static const qa_q1_monster_operation ops_9[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_pain1"},
};
static const qa_q1_monster_operation ops_10[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_pain2"},
};
static const qa_q1_monster_operation ops_13[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_death1"},
};
static const qa_q1_monster_operation ops_15[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_death3"},
};
static const qa_q1_monster_operation ops_16[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "orb:orb_death4"},
};
static const qa_q1_monster_frame_entry entries[] = {
  {"orb_stand1", {0, "orb_stand1", ops_0, 1}},
  {"orb_walk1", {0, "orb_walk1", ops_1, 1}},
  {"orb_side1", {0, "orb_side1", ops_2, 1}},
  {"orb_run1", {0, "orb_run1", ops_3, 1}},
  {"orb_fast1", {0, "orb_fast2", ops_4, 1}},
  {"orb_fast2", {1, "orb_fast3", NULL, 0}},
  {"orb_fast3", {2, "orb_fast4", ops_6, 1}},
  {"orb_fast4", {0, "orb_fast5", ops_7, 1}},
  {"orb_fast5", {2, "orb_run1", ops_8, 1}},
  {"orb_pain1", {2, "orb_pain2", ops_9, 1}},
  {"orb_pain2", {2, "orb_pain3", ops_10, 1}},
  {"orb_pain3", {2, "orb_pain4", NULL, 0}},
  {"orb_pain4", {0, "orb_run1", NULL, 0}},
  {"orb_death1", {1, "orb_death2", ops_13, 1}},
  {"orb_death2", {2, "orb_death3", NULL, 0}},
  {"orb_death3", {2, "orb_death4", ops_15, 1}},
  {"orb_death4", {2, "orb_death4", ops_16, 1}},
};
const qa_q1_monster_frame_table qa_q1am_orb_frames = {entries, sizeof entries / sizeof entries[0]};
