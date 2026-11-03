/* Port of src/content/q1/missionpacks/monsters/tables/invis_sw.ts. Generated mechanically from TS via bun JSON dump; see header. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/tables/invis_sw.h"

static const qa_q1_monster_operation ops_0[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0.0}
};
static const qa_q1_monster_operation ops_1[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "invis_sw:sword_run1"}
};
static const qa_q1_monster_operation ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14.0}
};
static const qa_q1_monster_operation ops_9[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "invis_sw:sword_atk1"}
};
static const qa_q1_monster_operation ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14.0}
};
static const qa_q1_monster_operation ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14.0}
};
static const qa_q1_monster_operation ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14.0}
};
static const qa_q1_monster_operation ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0.0}
};
static const qa_q1_monster_operation ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0.0}
};
static const qa_q1_monster_operation ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0.0}
};
static const qa_q1_monster_operation ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14.0}
};
static const qa_q1_monster_operation ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14.0}
};
static const qa_q1_monster_operation ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14.0}
};
static const qa_q1_monster_operation ops_21[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"}
};
static const qa_q1_monster_operation ops_25[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "invis_sw:sword_die7"}
};
static const qa_q1_monster_operation ops_31[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"}
};
static const qa_q1_monster_operation ops_35[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "invis_sw:sword_die7"}
};
const qa_q1_monster_frame_entry qa_q1mpm_tables_invis_sw_entries[] = {
  {"sword_stand1", {0.0, "sword_stand1", ops_0, 1}},
  {"sword_run1", {1.0, "sword_run2", ops_1, 1}},
  {"sword_run2", {2.0, "sword_run3", ops_2, 1}},
  {"sword_run3", {3.0, "sword_run4", ops_3, 1}},
  {"sword_run4", {4.0, "sword_run5", ops_4, 1}},
  {"sword_run5", {5.0, "sword_run6", ops_5, 1}},
  {"sword_run6", {6.0, "sword_run7", ops_6, 1}},
  {"sword_run7", {7.0, "sword_run8", ops_7, 1}},
  {"sword_run8", {8.0, "sword_run1", ops_8, 1}},
  {"sword_atk1", {9.0, "sword_atk2", ops_9, 1}},
  {"sword_atk2", {10.0, "sword_atk3", ops_10, 1}},
  {"sword_atk3", {11.0, "sword_atk4", ops_11, 1}},
  {"sword_atk4", {12.0, "sword_atk5", ops_12, 1}},
  {"sword_atk5", {13.0, "sword_atk6", ops_13, 1}},
  {"sword_atk6", {14.0, "sword_atk7", ops_14, 1}},
  {"sword_atk7", {15.0, "sword_atk8", ops_15, 1}},
  {"sword_atk8", {16.0, "sword_atk9", ops_16, 1}},
  {"sword_atk9", {17.0, "sword_atk10", ops_17, 1}},
  {"sword_atk10", {18.0, "sword_run1", ops_18, 1}},
  {"sword_die1", {19.0, "sword_die2", NULL, 0}},
  {"sword_die2", {20.0, "sword_die3", NULL, 0}},
  {"sword_die3", {21.0, "sword_die4", ops_21, 1}},
  {"sword_die4", {22.0, "sword_die5", NULL, 0}},
  {"sword_die5", {23.0, "sword_die6", NULL, 0}},
  {"sword_die6", {24.0, "sword_die7", NULL, 0}},
  {"sword_die7", {25.0, "sword_die8", ops_25, 1}},
  {"sword_die8", {26.0, "sword_die9", NULL, 0}},
  {"sword_die9", {27.0, "sword_die10", NULL, 0}},
  {"sword_die10", {28.0, "sword_die10", NULL, 0}},
  {"sword_dieb1", {29.0, "sword_dieb2", NULL, 0}},
  {"sword_dieb2", {30.0, "sword_dieb3", NULL, 0}},
  {"sword_dieb3", {31.0, "sword_dieb4", ops_31, 1}},
  {"sword_dieb4", {32.0, "sword_dieb5", NULL, 0}},
  {"sword_dieb5", {33.0, "sword_dieb6", NULL, 0}},
  {"sword_dieb6", {34.0, "sword_dieb7", NULL, 0}},
  {"sword_dieb7", {35.0, "sword_dieb8", ops_35, 1}},
  {"sword_dieb8", {36.0, "sword_dieb9", NULL, 0}},
  {"sword_dieb9", {37.0, "sword_dieb10", NULL, 0}},
  {"sword_dieb10", {38.0, "sword_dieb11", NULL, 0}},
  {"sword_dieb11", {39.0, "sword_dieb11", NULL, 0}}
};
const qa_q1_monster_frame_table qa_q1mpm_tables_invis_sw_frames = {qa_q1mpm_tables_invis_sw_entries, 40};
