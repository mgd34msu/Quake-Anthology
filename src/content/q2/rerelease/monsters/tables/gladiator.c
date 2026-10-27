/* Port of src/content/q2/rerelease/monsters/tables/gladiator.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/tables/gladiator.h"
const qa_q2rm_gladiator_frames qa_q2rm_gladiator_frames_value = {
  .stand1 = 0,
  .stand2 = 1,
  .stand3 = 2,
  .stand4 = 3,
  .stand5 = 4,
  .stand6 = 5,
  .stand7 = 6,
  .walk1 = 7,
  .walk2 = 8,
  .walk3 = 9,
  .walk4 = 10,
  .walk5 = 11,
  .walk6 = 12,
  .walk7 = 13,
  .walk8 = 14,
  .walk9 = 15,
  .walk10 = 16,
  .walk11 = 17,
  .walk12 = 18,
  .walk13 = 19,
  .walk14 = 20,
  .walk15 = 21,
  .walk16 = 22,
  .run1 = 23,
  .run2 = 24,
  .run3 = 25,
  .run4 = 26,
  .run5 = 27,
  .run6 = 28,
  .melee1 = 29,
  .melee2 = 30,
  .melee3 = 31,
  .melee4 = 32,
  .melee5 = 33,
  .melee6 = 34,
  .melee7 = 35,
  .melee8 = 36,
  .melee9 = 37,
  .melee10 = 38,
  .melee11 = 39,
  .melee12 = 40,
  .melee13 = 41,
  .melee14 = 42,
  .melee15 = 43,
  .melee16 = 44,
  .melee17 = 45,
  .attack1 = 46,
  .attack2 = 47,
  .attack3 = 48,
  .attack4 = 49,
  .attack5 = 50,
  .attack6 = 51,
  .attack7 = 52,
  .attack8 = 53,
  .attack9 = 54,
  .pain1 = 55,
  .pain2 = 56,
  .pain3 = 57,
  .pain4 = 58,
  .pain5 = 59,
  .pain6 = 60,
  .death1 = 61,
  .death2 = 62,
  .death3 = 63,
  .death4 = 64,
  .death5 = 65,
  .death6 = 66,
  .death7 = 67,
  .death8 = 68,
  .death9 = 69,
  .death10 = 70,
  .death11 = 71,
  .death12 = 72,
  .death13 = 73,
  .death14 = 74,
  .death15 = 75,
  .death16 = 76,
  .death17 = 77,
  .death18 = 78,
  .death19 = 79,
  .death20 = 80,
  .death21 = 81,
  .death22 = 82,
  .painup1 = 83,
  .painup2 = 84,
  .painup3 = 85,
  .painup4 = 86,
  .painup5 = 87,
  .painup6 = 88,
  .painup7 = 89,
};
static const qa_q2m_frame f_gladiator_0[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_gladiator_1_4[] = {
  {false, "monster_footstep", 0, false},
};
static const qa_q2m_action a_gladiator_1_12[] = {
  {false, "monster_footstep", 0, false},
};
static const qa_q2m_frame f_gladiator_1[] = {
  {QA_Q2M_AI_WALK, NULL, 15, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 7, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 2, a_gladiator_1_4, 1, -1},
  {QA_Q2M_AI_WALK, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 2, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 12, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 2, a_gladiator_1_12, 1, -1},
  {QA_Q2M_AI_WALK, NULL, 2, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 1, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 8, NULL, 0, -1},
};
static const qa_q2m_action a_gladiator_2_2[] = {
  {false, "monster_footstep", 0, false},
};
static const qa_q2m_action a_gladiator_2_5[] = {
  {false, "monster_footstep", 0, false},
};
static const qa_q2m_frame f_gladiator_2[] = {
  {QA_Q2M_AI_RUN, NULL, 23, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 14, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 14, a_gladiator_2_2, 1, -1},
  {QA_Q2M_AI_RUN, NULL, 21, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 12, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 13, a_gladiator_2_5, 1, -1},
};
static const qa_q2m_action a_gladiator_3_2[] = {
  {false, "gladiator_cleaver_swing", 0, false},
};
static const qa_q2m_action a_gladiator_3_4[] = {
  {false, "GladiatorMelee", 0, false},
};
static const qa_q2m_action a_gladiator_3_8[] = {
  {false, "gladiator_cleaver_swing", 0, false},
};
static const qa_q2m_action a_gladiator_3_11[] = {
  {false, "GladiatorMelee", 0, false},
};
static const qa_q2m_frame f_gladiator_3[] = {
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_3_2, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_3_4, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_3_8, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_3_11, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_gladiator_4_3[] = {
  {false, "GladiatorGun", 0, false},
};
static const qa_q2m_action a_gladiator_4_7[] = {
  {false, "monster_footstep", 0, false},
};
static const qa_q2m_frame f_gladiator_4[] = {
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_4_3, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_4_7, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_gladiator_5_2[] = {
  {false, "gladbGun", 0, false},
};
static const qa_q2m_action a_gladiator_5_5[] = {
  {false, "gladbGun", 0, false},
};
static const qa_q2m_action a_gladiator_5_8[] = {
  {false, "gladbGun_check", 0, false},
};
static const qa_q2m_frame f_gladiator_5[] = {
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_5_2, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_5_5, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 0, a_gladiator_5_8, 1, -1},
};
static const qa_q2m_frame f_gladiator_6[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_gladiator_7[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_gladiator_8_2[] = {
  {false, "gladiator_shrink", 0, false},
};
static const qa_q2m_action a_gladiator_8_3[] = {
  {false, "monster_footstep", 0, false},
};
static const qa_q2m_frame f_gladiator_8[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_gladiator_8_2, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_gladiator_8_3, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
const qa_q2m_move qa_q2rm_gladiator_moves[] = {
  {"gladiator_move_stand", 0, 6, NULL, 0, f_gladiator_0, 7},
  {"gladiator_move_walk", 7, 22, NULL, 0, f_gladiator_1, 16},
  {"gladiator_move_run", 23, 28, NULL, 0, f_gladiator_2, 6},
  {"gladiator_move_attack_melee", 31, 44, "gladiator_run", 0, f_gladiator_3, 14},
  {"gladiator_move_attack_gun", 46, 54, "gladiator_run", 0, f_gladiator_4, 9},
  {"gladb_move_attack_gun", 46, 54, "gladiator_run", 0, f_gladiator_5, 9},
  {"gladiator_move_pain", 56, 59, "gladiator_run", 0, f_gladiator_6, 4},
  {"gladiator_move_pain_air", 84, 88, "gladiator_run", 0, f_gladiator_7, 5},
  {"gladiator_move_death", 62, 82, "gladiator_dead", 0, f_gladiator_8, 21},
};
const size_t qa_q2rm_gladiator_move_count = sizeof qa_q2rm_gladiator_moves / sizeof qa_q2rm_gladiator_moves[0];
