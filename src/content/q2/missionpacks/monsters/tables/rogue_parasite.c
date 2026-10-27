/* Port of src/content/q2/missionpacks/monsters/tables/rogue-parasite.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/tables/rogue_parasite.h"
const qa_q2mpm_rogue_parasite_frames qa_q2mpm_rogue_parasite_frames_value = {
  .break01 = 0,
  .break02 = 1,
  .break03 = 2,
  .break04 = 3,
  .break05 = 4,
  .break06 = 5,
  .break07 = 6,
  .break08 = 7,
  .break09 = 8,
  .break10 = 9,
  .break11 = 10,
  .break12 = 11,
  .break13 = 12,
  .break14 = 13,
  .break15 = 14,
  .break16 = 15,
  .break17 = 16,
  .break18 = 17,
  .break19 = 18,
  .break20 = 19,
  .break21 = 20,
  .break22 = 21,
  .break23 = 22,
  .break24 = 23,
  .break25 = 24,
  .break26 = 25,
  .break27 = 26,
  .break28 = 27,
  .break29 = 28,
  .break30 = 29,
  .break31 = 30,
  .break32 = 31,
  .death101 = 32,
  .death102 = 33,
  .death103 = 34,
  .death104 = 35,
  .death105 = 36,
  .death106 = 37,
  .death107 = 38,
  .drain01 = 39,
  .drain02 = 40,
  .drain03 = 41,
  .drain04 = 42,
  .drain05 = 43,
  .drain06 = 44,
  .drain07 = 45,
  .drain08 = 46,
  .drain09 = 47,
  .drain10 = 48,
  .drain11 = 49,
  .drain12 = 50,
  .drain13 = 51,
  .drain14 = 52,
  .drain15 = 53,
  .drain16 = 54,
  .drain17 = 55,
  .drain18 = 56,
  .pain101 = 57,
  .pain102 = 58,
  .pain103 = 59,
  .pain104 = 60,
  .pain105 = 61,
  .pain106 = 62,
  .pain107 = 63,
  .pain108 = 64,
  .pain109 = 65,
  .pain110 = 66,
  .pain111 = 67,
  .run01 = 68,
  .run02 = 69,
  .run03 = 70,
  .run04 = 71,
  .run05 = 72,
  .run06 = 73,
  .run07 = 74,
  .run08 = 75,
  .run09 = 76,
  .run10 = 77,
  .run11 = 78,
  .run12 = 79,
  .run13 = 80,
  .run14 = 81,
  .run15 = 82,
  .stand01 = 83,
  .stand02 = 84,
  .stand03 = 85,
  .stand04 = 86,
  .stand05 = 87,
  .stand06 = 88,
  .stand07 = 89,
  .stand08 = 90,
  .stand09 = 91,
  .stand10 = 92,
  .stand11 = 93,
  .stand12 = 94,
  .stand13 = 95,
  .stand14 = 96,
  .stand15 = 97,
  .stand16 = 98,
  .stand17 = 99,
  .stand18 = 100,
  .stand19 = 101,
  .stand20 = 102,
  .stand21 = 103,
  .stand22 = 104,
  .stand23 = 105,
  .stand24 = 106,
  .stand25 = 107,
  .stand26 = 108,
  .stand27 = 109,
  .stand28 = 110,
  .stand29 = 111,
  .stand30 = 112,
  .stand31 = 113,
  .stand32 = 114,
  .stand33 = 115,
  .stand34 = 116,
  .stand35 = 117,
  .jump01 = 118,
  .jump02 = 119,
  .jump03 = 120,
  .jump04 = 121,
  .jump05 = 122,
  .jump06 = 123,
  .jump07 = 124,
  .jump08 = 125,
};
static const qa_q2m_frame f_parasite_0[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_1_0[] = {
  {false, "parasite_scratch", 0, false},
};
static const qa_q2m_action a_parasite_1_3[] = {
  {false, "parasite_scratch", 0, false},
};
static const qa_q2m_frame f_parasite_1[] = {
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_1_0, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_1_3, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_2_0[] = {
  {false, "parasite_scratch", 0, false},
};
static const qa_q2m_frame f_parasite_2[] = {
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_2_0, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_3_2[] = {
  {false, "parasite_tap", 0, false},
};
static const qa_q2m_action a_parasite_3_4[] = {
  {false, "parasite_tap", 0, false},
};
static const qa_q2m_action a_parasite_3_8[] = {
  {false, "parasite_tap", 0, false},
};
static const qa_q2m_action a_parasite_3_10[] = {
  {false, "parasite_tap", 0, false},
};
static const qa_q2m_action a_parasite_3_14[] = {
  {false, "parasite_tap", 0, false},
};
static const qa_q2m_action a_parasite_3_16[] = {
  {false, "parasite_tap", 0, false},
};
static const qa_q2m_frame f_parasite_3[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_3_2, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_3_4, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_3_8, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_3_10, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_3_14, 1, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_parasite_3_16, 1, -1},
};
static const qa_q2m_frame f_parasite_4[] = {
  {QA_Q2M_AI_RUN, NULL, 30, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 30, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 22, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 19, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 24, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 28, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 25, NULL, 0, -1},
};
static const qa_q2m_frame f_parasite_5[] = {
  {QA_Q2M_AI_RUN, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 30, NULL, 0, -1},
};
static const qa_q2m_frame f_parasite_6[] = {
  {QA_Q2M_AI_RUN, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 12, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 10, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_parasite_7[] = {
  {QA_Q2M_AI_WALK, NULL, 30, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 30, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 22, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 19, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 24, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 28, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 25, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_8_1[] = {
  {false, "parasite_walk", 0, false},
};
static const qa_q2m_frame f_parasite_8[] = {
  {QA_Q2M_AI_WALK, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 30, a_parasite_8_1, 1, -1},
};
static const qa_q2m_frame f_parasite_9[] = {
  {QA_Q2M_AI_WALK, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 20, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 12, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 10, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_parasite_10[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 16, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -6, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -7, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_11_0[] = {
  {false, "parasite_launch", 0, false},
};
static const qa_q2m_action a_parasite_11_2[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_3[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_4[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_5[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_6[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_7[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_8[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_9[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_10[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_11[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_12[] = {
  {false, "parasite_drain_attack", 0, false},
};
static const qa_q2m_action a_parasite_11_13[] = {
  {false, "parasite_reel_in", 0, false},
};
static const qa_q2m_frame f_parasite_11[] = {
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_0, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 15, a_parasite_11_2, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_3, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_4, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_5, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_6, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, -2, a_parasite_11_7, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, -2, a_parasite_11_8, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, -3, a_parasite_11_9, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, -2, a_parasite_11_10, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_11, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, -1, a_parasite_11_12, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_parasite_11_13, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, -2, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -2, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_parasite_12[] = {
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 1, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 2, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 1, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 1, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -18, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 3, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 9, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -18, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 9, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -18, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 11, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -2, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, -5, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 1, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_13_3[] = {
  {false, "parasite_jump_up", 0, false},
};
static const qa_q2m_action a_parasite_13_6[] = {
  {false, "parasite_jump_wait_land", 0, false},
};
static const qa_q2m_frame f_parasite_13[] = {
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -8, a_parasite_13_3, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_parasite_13_6, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_parasite_14_3[] = {
  {false, "parasite_jump_down", 0, false},
};
static const qa_q2m_action a_parasite_14_6[] = {
  {false, "parasite_jump_wait_land", 0, false},
};
static const qa_q2m_frame f_parasite_14[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_parasite_14_3, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_parasite_14_6, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_parasite_15[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
const qa_q2m_move qa_q2mpm_rogue_parasite_moves[] = {
  {"parasite_move_start_fidget", 100, 103, "parasite_do_fidget", 0, f_parasite_0, 4},
  {"parasite_move_fidget", 104, 109, "parasite_refidget", 0, f_parasite_1, 6},
  {"parasite_move_end_fidget", 110, 117, "parasite_stand", 0, f_parasite_2, 8},
  {"parasite_move_stand", 83, 99, "parasite_stand", 0, f_parasite_3, 17},
  {"parasite_move_run", 70, 76, NULL, 0, f_parasite_4, 7},
  {"parasite_move_start_run", 68, 69, "parasite_run", 0, f_parasite_5, 2},
  {"parasite_move_stop_run", 77, 82, NULL, 0, f_parasite_6, 6},
  {"parasite_move_walk", 70, 76, "parasite_walk", 0, f_parasite_7, 7},
  {"parasite_move_start_walk", 68, 69, NULL, 0, f_parasite_8, 2},
  {"parasite_move_stop_walk", 77, 82, NULL, 0, f_parasite_9, 6},
  {"parasite_move_pain1", 57, 67, "parasite_start_run", 0, f_parasite_10, 11},
  {"parasite_move_drain", 39, 56, "parasite_start_run", 0, f_parasite_11, 18},
  {"parasite_move_break", 0, 31, "parasite_start_run", 0, f_parasite_12, 32},
  {"parasite_move_jump_up", 118, 125, "parasite_run", 0, f_parasite_13, 8},
  {"parasite_move_jump_down", 118, 125, "parasite_run", 0, f_parasite_14, 8},
  {"parasite_move_death", 32, 38, "parasite_dead", 0, f_parasite_15, 7},
};
const size_t qa_q2mpm_rogue_parasite_move_count = sizeof qa_q2mpm_rogue_parasite_moves / sizeof qa_q2mpm_rogue_parasite_moves[0];
