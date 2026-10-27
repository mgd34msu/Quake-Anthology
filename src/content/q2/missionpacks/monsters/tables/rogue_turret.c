/* Port of src/content/q2/missionpacks/monsters/tables/rogue-turret.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/tables/rogue_turret.h"
const qa_q2mpm_rogue_turret_frames qa_q2mpm_rogue_turret_frames_value = {
  .stand01 = 0,
  .stand02 = 1,
  .active01 = 2,
  .active02 = 3,
  .active03 = 4,
  .active04 = 5,
  .active05 = 6,
  .active06 = 7,
  .run01 = 8,
  .run02 = 9,
  .pow01 = 10,
  .pow02 = 11,
  .pow03 = 12,
  .pow04 = 13,
  .death01 = 14,
  .death02 = 15,
};
static const qa_q2m_frame f_turret_0[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_turret_1[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_turret_2_0[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_2_1[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_frame f_turret_2[] = {
  {QA_Q2M_AI_WALK, NULL, 0, a_turret_2_0, 1, -1},
  {QA_Q2M_AI_WALK, NULL, 0, a_turret_2_1, 1, -1},
};
static const qa_q2m_action a_turret_3_0[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_3_1[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_frame f_turret_3[] = {
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_3_0, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_3_1, 1, -1},
};
static const qa_q2m_action a_turret_4_0[] = {
  {false, "TurretFire", 0, false},
};
static const qa_q2m_action a_turret_4_1[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_4_2[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_4_3[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_frame f_turret_4[] = {
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_4_0, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_4_1, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_4_2, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_4_3, 1, -1},
};
static const qa_q2m_action a_turret_5_0[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_5_1[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_5_2[] = {
  {false, "TurretAim", 0, false},
};
static const qa_q2m_action a_turret_5_3[] = {
  {false, "TurretFireBlind", 0, false},
};
static const qa_q2m_frame f_turret_5[] = {
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_5_0, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_5_1, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_5_2, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 0, a_turret_5_3, 1, -1},
};
const qa_q2m_move qa_q2mpm_rogue_turret_moves[] = {
  {"turret_move_stand", 0, 1, NULL, 0, f_turret_0, 2},
  {"turret_move_ready_gun", 2, 8, "turret_run", 0, f_turret_1, 7},
  {"turret_move_seek", 8, 9, NULL, 0, f_turret_2, 2},
  {"turret_move_run", 8, 9, "turret_run", 0, f_turret_3, 2},
  {"turret_move_fire", 10, 13, "turret_run", 0, f_turret_4, 4},
  {"turret_move_fire_blind", 10, 13, "turret_run", 0, f_turret_5, 4},
};
const size_t qa_q2mpm_rogue_turret_move_count = sizeof qa_q2mpm_rogue_turret_moves / sizeof qa_q2mpm_rogue_turret_moves[0];
