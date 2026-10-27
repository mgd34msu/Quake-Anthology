/* Port of src/content/q2/rerelease/monsters/tables/shambler.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/tables/shambler.h"
const qa_q2rm_shambler_frames qa_q2rm_shambler_frames_value = {
  .stand01 = 0,
  .stand02 = 1,
  .stand03 = 2,
  .stand04 = 3,
  .stand05 = 4,
  .stand06 = 5,
  .stand07 = 6,
  .stand08 = 7,
  .stand09 = 8,
  .stand10 = 9,
  .stand11 = 10,
  .stand12 = 11,
  .stand13 = 12,
  .stand14 = 13,
  .stand15 = 14,
  .stand16 = 15,
  .stand17 = 16,
  .walk01 = 17,
  .walk02 = 18,
  .walk03 = 19,
  .walk04 = 20,
  .walk05 = 21,
  .walk06 = 22,
  .walk07 = 23,
  .walk08 = 24,
  .walk09 = 25,
  .walk10 = 26,
  .walk11 = 27,
  .walk12 = 28,
  .run01 = 29,
  .run02 = 30,
  .run03 = 31,
  .run04 = 32,
  .run05 = 33,
  .run06 = 34,
  .smash01 = 35,
  .smash02 = 36,
  .smash03 = 37,
  .smash04 = 38,
  .smash05 = 39,
  .smash06 = 40,
  .smash07 = 41,
  .smash08 = 42,
  .smash09 = 43,
  .smash10 = 44,
  .smash11 = 45,
  .smash12 = 46,
  .swingr01 = 47,
  .swingr02 = 48,
  .swingr03 = 49,
  .swingr04 = 50,
  .swingr05 = 51,
  .swingr06 = 52,
  .swingr07 = 53,
  .swingr08 = 54,
  .swingr09 = 55,
  .swingl01 = 56,
  .swingl02 = 57,
  .swingl03 = 58,
  .swingl04 = 59,
  .swingl05 = 60,
  .swingl06 = 61,
  .swingl07 = 62,
  .swingl08 = 63,
  .swingl09 = 64,
  .magic01 = 65,
  .magic02 = 66,
  .magic03 = 67,
  .magic04 = 68,
  .magic05 = 69,
  .magic06 = 70,
  .magic07 = 71,
  .magic08 = 72,
  .magic09 = 73,
  .magic10 = 74,
  .magic11 = 75,
  .magic12 = 76,
  .pain01 = 77,
  .pain02 = 78,
  .pain03 = 79,
  .pain04 = 80,
  .pain05 = 81,
  .pain06 = 82,
  .death01 = 83,
  .death02 = 84,
  .death03 = 85,
  .death04 = 86,
  .death05 = 87,
  .death06 = 88,
  .death07 = 89,
  .death08 = 90,
  .death09 = 91,
  .death10 = 92,
  .death11 = 93,
};
static const qa_q2m_frame f_shambler_0[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_shambler_1_10[] = {
  {false, "shambler_maybe_idle", 0, false},
};
static const qa_q2m_frame f_shambler_1[] = {
  {QA_Q2M_AI_WALK, NULL, 10, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 9, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 9, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 12, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 13, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 9, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 7, a_shambler_1_10, 1, -1},
  {QA_Q2M_AI_WALK, NULL, 5, NULL, 0, -1},
};
static const qa_q2m_action a_shambler_2_5[] = {
  {false, "shambler_maybe_idle", 0, false},
};
static const qa_q2m_frame f_shambler_2[] = {
  {QA_Q2M_AI_RUN, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 24, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 24, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 20, a_shambler_2_5, 1, -1},
};
static const qa_q2m_frame f_shambler_3[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_shambler_4_0[] = {
  {false, "shambler_windup", 0, false},
};
static const qa_q2m_action a_shambler_4_1[] = {
  {false, "shambler_lightning_update", 0, false},
};
static const qa_q2m_action a_shambler_4_2[] = {
  {false, "shambler_lightning_update", 0, false},
};
static const qa_q2m_action a_shambler_4_3[] = {
  {false, "shambler_lightning_update", 0, false},
};
static const qa_q2m_action a_shambler_4_4[] = {
  {false, "shambler_lightning_update", 0, false},
};
static const qa_q2m_action a_shambler_4_5[] = {
  {false, "ShamblerSaveLoc", 0, false},
};
static const qa_q2m_action a_shambler_4_8[] = {
  {false, "ShamblerCastLightning", 0, false},
};
static const qa_q2m_action a_shambler_4_9[] = {
  {false, "ShamblerCastLightning", 0, false},
};
static const qa_q2m_action a_shambler_4_10[] = {
  {false, "ShamblerCastLightning", 0, false},
};
static const qa_q2m_frame f_shambler_4[] = {
  {QA_Q2M_AI_CHARGE, NULL, 0, a_shambler_4_0, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_shambler_4_1, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_shambler_4_2, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_4_3, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_4_4, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_4_5, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_4_8, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_4_9, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_4_10, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_shambler_5_0[] = {
  {false, "shambler_melee1", 0, false},
};
static const qa_q2m_action a_shambler_5_9[] = {
  {false, "sham_smash10", 0, false},
};
static const qa_q2m_frame f_shambler_5[] = {
  {QA_Q2M_AI_CHARGE, NULL, 2, a_shambler_5_0, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 1, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_shambler_5_9, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 4, NULL, 0, -1},
};
static const qa_q2m_action a_shambler_6_0[] = {
  {false, "shambler_melee1", 0, false},
};
static const qa_q2m_action a_shambler_6_6[] = {
  {false, "ShamClaw", 0, false},
};
static const qa_q2m_action a_shambler_6_8[] = {
  {false, "sham_swingl9", 0, false},
};
static const qa_q2m_frame f_shambler_6[] = {
  {QA_Q2M_AI_CHARGE, NULL, 5, a_shambler_6_0, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 7, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 7, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 9, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, a_shambler_6_6, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 8, a_shambler_6_8, 1, -1},
};
static const qa_q2m_action a_shambler_7_0[] = {
  {false, "shambler_melee2", 0, false},
};
static const qa_q2m_action a_shambler_7_6[] = {
  {false, "ShamClaw", 0, false},
};
static const qa_q2m_action a_shambler_7_8[] = {
  {false, "sham_swingr9", 0, false},
};
static const qa_q2m_frame f_shambler_7[] = {
  {QA_Q2M_AI_CHARGE, NULL, 1, a_shambler_7_0, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 14, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 7, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, a_shambler_7_6, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 8, a_shambler_7_8, 1, -1},
};
static const qa_q2m_action a_shambler_8_2[] = {
  {false, "shambler_shrink", 0, false},
};
static const qa_q2m_frame f_shambler_8[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_shambler_8_2, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
const qa_q2m_move qa_q2rm_shambler_moves[] = {
  {"shambler_move_stand", 0, 16, NULL, 0, f_shambler_0, 17},
  {"shambler_move_walk", 17, 28, NULL, 0, f_shambler_1, 12},
  {"shambler_move_run", 29, 34, NULL, 0, f_shambler_2, 6},
  {"shambler_move_pain", 77, 82, "shambler_run", 0, f_shambler_3, 6},
  {"shambler_attack_magic", 65, 76, "shambler_run", 0, f_shambler_4, 12},
  {"shambler_attack_smash", 35, 46, "shambler_run", 0, f_shambler_5, 12},
  {"shambler_attack_swingl", 56, 64, "shambler_run", 0, f_shambler_6, 9},
  {"shambler_attack_swingr", 47, 55, "shambler_run", 0, f_shambler_7, 9},
  {"shambler_move_death", 83, 93, "shambler_dead", 0, f_shambler_8, 11},
};
const size_t qa_q2rm_shambler_move_count = sizeof qa_q2rm_shambler_moves / sizeof qa_q2rm_shambler_moves[0];
