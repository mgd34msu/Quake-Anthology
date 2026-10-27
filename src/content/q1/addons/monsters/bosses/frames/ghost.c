/* Port of src/content/q1/addons/monsters/bosses/frames/ghost.ts. Generated mechanically from the TS
 * frame table via bun JSON dump; do not hand-edit entries. */
#include "qa/content/q1/addons/monsters/bosses/frames/ghost.h"

static const qa_q1am_operation ops_0[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_stand1", 0, false, false, 0},
};
static const qa_q1am_operation ops_4[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_stand5", 0, false, false, 0},
};
static const qa_q1am_operation ops_5[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_run1", 0, false, false, 0},
};
static const qa_q1am_operation ops_10[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_run6", 0, false, false, 0},
};
static const qa_q1am_operation ops_11[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea1", 0, false, false, 0},
};
static const qa_q1am_operation ops_21[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea11", 0, false, false, 0},
};
static const qa_q1am_operation ops_22[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea1", 0, false, false, 0},
};
static const qa_q1am_operation ops_30[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea11", 0, false, false, 0},
};
static const qa_q1am_operation ops_31[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea1", 0, false, false, 0},
};
static const qa_q1am_operation ops_45[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea11", 0, false, false, 0},
};
static const qa_q1am_operation ops_46[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea1", 0, false, false, 0},
};
static const qa_q1am_operation ops_54[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea11", 0, false, false, 0},
};
static const qa_q1am_operation ops_55[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea1", 0, false, false, 0},
};
static const qa_q1am_operation ops_63[] = {
  {QA_Q1AM_OP_ACTION, QA_Q1AM_AI_STAND, 0, NULL, NULL, "ghost:ghost_diea11", 0, false, false, 0},
};
static const qa_q1am_frame entries[] = {
  {"ghost_stand1", 12, "ghost_stand2", ops_0, 1},
  {"ghost_stand2", 13, "ghost_stand3", NULL, 0},
  {"ghost_stand3", 14, "ghost_stand4", NULL, 0},
  {"ghost_stand4", 15, "ghost_stand5", NULL, 0},
  {"ghost_stand5", 16, "ghost_stand1", ops_4, 1},
  {"ghost_run1", 6, "ghost_run2", ops_5, 1},
  {"ghost_run2", 7, "ghost_run3", NULL, 0},
  {"ghost_run3", 8, "ghost_run4", NULL, 0},
  {"ghost_run4", 9, "ghost_run5", NULL, 0},
  {"ghost_run5", 10, "ghost_run6", NULL, 0},
  {"ghost_run6", 11, "ghost_stand1", ops_10, 1},
  {"ghost_diea1", 50, "ghost_diea2", ops_11, 1},
  {"ghost_diea2", 51, "ghost_diea3", NULL, 0},
  {"ghost_diea3", 52, "ghost_diea4", NULL, 0},
  {"ghost_diea4", 53, "ghost_diea5", NULL, 0},
  {"ghost_diea5", 54, "ghost_diea6", NULL, 0},
  {"ghost_diea6", 55, "ghost_diea7", NULL, 0},
  {"ghost_diea7", 56, "ghost_diea8", NULL, 0},
  {"ghost_diea8", 57, "ghost_diea9", NULL, 0},
  {"ghost_diea9", 58, "ghost_diea10", NULL, 0},
  {"ghost_diea10", 59, "ghost_diea11", NULL, 0},
  {"ghost_diea11", 60, "ghost_diea11", ops_21, 1},
  {"ghost_dieb1", 61, "ghost_dieb2", ops_22, 1},
  {"ghost_dieb2", 62, "ghost_dieb3", NULL, 0},
  {"ghost_dieb3", 63, "ghost_dieb4", NULL, 0},
  {"ghost_dieb4", 64, "ghost_dieb5", NULL, 0},
  {"ghost_dieb5", 65, "ghost_dieb6", NULL, 0},
  {"ghost_dieb6", 66, "ghost_dieb7", NULL, 0},
  {"ghost_dieb7", 67, "ghost_dieb8", NULL, 0},
  {"ghost_dieb8", 68, "ghost_dieb9", NULL, 0},
  {"ghost_dieb9", 69, "ghost_dieb9", ops_30, 1},
  {"ghost_diec1", 70, "ghost_diec2", ops_31, 1},
  {"ghost_diec2", 71, "ghost_diec3", NULL, 0},
  {"ghost_diec3", 72, "ghost_diec4", NULL, 0},
  {"ghost_diec4", 73, "ghost_diec5", NULL, 0},
  {"ghost_diec5", 74, "ghost_diec6", NULL, 0},
  {"ghost_diec6", 75, "ghost_diec7", NULL, 0},
  {"ghost_diec7", 76, "ghost_diec8", NULL, 0},
  {"ghost_diec8", 77, "ghost_diec9", NULL, 0},
  {"ghost_diec9", 78, "ghost_diec10", NULL, 0},
  {"ghost_diec10", 79, "ghost_diec11", NULL, 0},
  {"ghost_diec11", 80, "ghost_diec12", NULL, 0},
  {"ghost_diec12", 81, "ghost_diec13", NULL, 0},
  {"ghost_diec13", 82, "ghost_diec14", NULL, 0},
  {"ghost_diec14", 83, "ghost_diec15", NULL, 0},
  {"ghost_diec15", 84, "ghost_diec15", ops_45, 1},
  {"ghost_died1", 85, "ghost_died2", ops_46, 1},
  {"ghost_died2", 86, "ghost_died3", NULL, 0},
  {"ghost_died3", 87, "ghost_died4", NULL, 0},
  {"ghost_died4", 88, "ghost_died5", NULL, 0},
  {"ghost_died5", 89, "ghost_died6", NULL, 0},
  {"ghost_died6", 90, "ghost_died7", NULL, 0},
  {"ghost_died7", 91, "ghost_died8", NULL, 0},
  {"ghost_died8", 92, "ghost_died9", NULL, 0},
  {"ghost_died9", 93, "ghost_died9", ops_54, 1},
  {"ghost_diee1", 94, "ghost_diee2", ops_55, 1},
  {"ghost_diee2", 95, "ghost_diee3", NULL, 0},
  {"ghost_diee3", 96, "ghost_diee4", NULL, 0},
  {"ghost_diee4", 97, "ghost_diee5", NULL, 0},
  {"ghost_diee5", 98, "ghost_diee6", NULL, 0},
  {"ghost_diee6", 99, "ghost_diee7", NULL, 0},
  {"ghost_diee7", 100, "ghost_diee8", NULL, 0},
  {"ghost_diee8", 101, "ghost_diee9", NULL, 0},
  {"ghost_diee9", 93, "ghost_diee9", ops_63, 1},
};
const qa_q1am_frame_table qa_q1am_ghost_frames = {entries, sizeof entries / sizeof entries[0]};
