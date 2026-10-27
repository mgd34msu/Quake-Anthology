/* Port of src/content/q2/missionpacks/monsters/tables/rogue-stalker.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/tables/rogue_stalker.h"
const qa_q2mpm_rogue_stalker_frames qa_q2mpm_rogue_stalker_frames_value = {
  .idle01 = 0,
  .idle02 = 1,
  .idle03 = 2,
  .idle04 = 3,
  .idle05 = 4,
  .idle06 = 5,
  .idle07 = 6,
  .idle08 = 7,
  .idle09 = 8,
  .idle10 = 9,
  .idle11 = 10,
  .idle12 = 11,
  .idle13 = 12,
  .idle14 = 13,
  .idle15 = 14,
  .idle16 = 15,
  .idle17 = 16,
  .idle18 = 17,
  .idle19 = 18,
  .idle20 = 19,
  .idle21 = 20,
  .idle201 = 21,
  .idle202 = 22,
  .idle203 = 23,
  .idle204 = 24,
  .idle205 = 25,
  .idle206 = 26,
  .idle207 = 27,
  .idle208 = 28,
  .idle209 = 29,
  .idle210 = 30,
  .idle211 = 31,
  .idle212 = 32,
  .idle213 = 33,
  .walk01 = 34,
  .walk02 = 35,
  .walk03 = 36,
  .walk04 = 37,
  .walk05 = 38,
  .walk06 = 39,
  .walk07 = 40,
  .walk08 = 41,
  .jump01 = 42,
  .jump02 = 43,
  .jump03 = 44,
  .jump04 = 45,
  .jump05 = 46,
  .jump06 = 47,
  .jump07 = 48,
  .run01 = 49,
  .run02 = 50,
  .run03 = 51,
  .run04 = 52,
  .attack01 = 53,
  .attack02 = 54,
  .attack03 = 55,
  .attack04 = 56,
  .attack05 = 57,
  .attack06 = 58,
  .attack07 = 59,
  .attack08 = 60,
  .attack11 = 61,
  .attack12 = 62,
  .attack13 = 63,
  .attack14 = 64,
  .attack15 = 65,
  .pain01 = 66,
  .pain02 = 67,
  .pain03 = 68,
  .pain04 = 69,
  .death01 = 70,
  .death02 = 71,
  .death03 = 72,
  .death04 = 73,
  .death05 = 74,
  .death06 = 75,
  .death07 = 76,
  .death08 = 77,
  .death09 = 78,
  .twitch01 = 79,
  .twitch02 = 80,
  .twitch03 = 81,
  .twitch04 = 82,
  .twitch05 = 83,
  .twitch06 = 84,
  .twitch07 = 85,
  .twitch08 = 86,
  .twitch09 = 87,
  .twitch10 = 88,
  .reactive01 = 89,
  .reactive02 = 90,
  .reactive03 = 91,
  .reactive04 = 92,
};
static const qa_q2m_action a_stalker_0_6[] = {
  {false, "stalker_idle_noise", 0, false},
};
static const qa_q2m_frame f_stalker_0[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_stalker_0_6, 1, -1},
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
static const qa_q2m_frame f_stalker_1[] = {
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
static const qa_q2m_action a_stalker_2_6[] = {
  {false, "stalker_idle_noise", 0, false},
};
static const qa_q2m_frame f_stalker_2[] = {
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_STAND, NULL, 0, a_stalker_2_6, 1, -1},
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
static const qa_q2m_frame f_stalker_3[] = {
  {QA_Q2M_AI_RUN, NULL, 13, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 17, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 21, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 18, NULL, 0, -1},
};
static const qa_q2m_frame f_stalker_4[] = {
  {QA_Q2M_AI_WALK, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 8, NULL, 0, -1},
  {QA_Q2M_AI_WALK, NULL, 4, NULL, 0, -1},
};
static const qa_q2m_frame f_stalker_5[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_stalker_6_0[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_1[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_2[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_3[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_4[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_5[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_6[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_7[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_8[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_action a_stalker_6_9[] = {
  {false, "stalker_heal", 0, false},
};
static const qa_q2m_frame f_stalker_6[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_0, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_1, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_2, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_3, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_4, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_5, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_6, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_7, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_8, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_6_9, 1, -1},
};
static const qa_q2m_frame f_stalker_7[] = {
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
static const qa_q2m_frame f_stalker_8[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_stalker_9_1[] = {
  {false, "stalker_shoot_attack", 0, false},
};
static const qa_q2m_action a_stalker_9_3[] = {
  {false, "stalker_shoot_attack2", 0, false},
};
static const qa_q2m_frame f_stalker_9[] = {
  {QA_Q2M_AI_CHARGE, NULL, 13, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 17, a_stalker_9_1, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 21, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 18, a_stalker_9_3, 1, -1},
};
static const qa_q2m_action a_stalker_10_4[] = {
  {false, "stalker_swing_attack", 0, false},
};
static const qa_q2m_frame f_stalker_10[] = {
  {QA_Q2M_AI_CHARGE, NULL, 2, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 10, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, a_stalker_10_4, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, NULL, 0, -1},
};
static const qa_q2m_action a_stalker_11_2[] = {
  {false, "stalker_swing_attack", 0, false},
};
static const qa_q2m_frame f_stalker_11[] = {
  {QA_Q2M_AI_CHARGE, NULL, 4, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 6, a_stalker_11_2, 1, -1},
  {QA_Q2M_AI_CHARGE, NULL, 10, NULL, 0, -1},
  {QA_Q2M_AI_CHARGE, NULL, 5, NULL, 0, -1},
};
static const qa_q2m_action a_stalker_12_0[] = {
  {false, "stalker_jump_straightup", 0, false},
};
static const qa_q2m_action a_stalker_12_1[] = {
  {false, "stalker_jump_wait_land", 0, false},
};
static const qa_q2m_frame f_stalker_12[] = {
  {QA_Q2M_AI_MOVE, NULL, 1, a_stalker_12_0, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 1, a_stalker_12_1, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, -1, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -1, NULL, 0, -1},
};
static const qa_q2m_action a_stalker_13_3[] = {
  {false, "monster_done_dodge", 0, false},
};
static const qa_q2m_frame f_stalker_13[] = {
  {QA_Q2M_AI_RUN, NULL, 13, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 17, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 21, NULL, 0, -1},
  {QA_Q2M_AI_RUN, NULL, 18, a_stalker_13_3, 1, -1},
};
static const qa_q2m_action a_stalker_14_4[] = {
  {false, "stalker_jump_up", 0, false},
};
static const qa_q2m_action a_stalker_14_5[] = {
  {false, "stalker_jump_wait_land", 0, false},
};
static const qa_q2m_frame f_stalker_14[] = {
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -8, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_14_4, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_14_5, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_action a_stalker_15_4[] = {
  {false, "stalker_jump_down", 0, false},
};
static const qa_q2m_action a_stalker_15_5[] = {
  {false, "stalker_jump_wait_land", 0, false},
};
static const qa_q2m_frame f_stalker_15[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_15_4, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, a_stalker_15_5, 1, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
static const qa_q2m_frame f_stalker_16[] = {
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -5, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -10, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -20, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -10, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -10, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -5, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, -5, NULL, 0, -1},
  {QA_Q2M_AI_MOVE, NULL, 0, NULL, 0, -1},
};
const qa_q2m_move qa_q2mpm_rogue_stalker_moves[] = {
  {"stalker_move_idle", 0, 20, "stalker_stand", 0, f_stalker_0, 21},
  {"stalker_move_idle2", 21, 33, "stalker_stand", 0, f_stalker_1, 13},
  {"stalker_move_stand", 0, 20, "stalker_stand", 0, f_stalker_2, 21},
  {"stalker_move_run", 49, 52, NULL, 0, f_stalker_3, 4},
  {"stalker_move_walk", 34, 41, "stalker_walk", 0, f_stalker_4, 8},
  {"stalker_move_false_death_end", 89, 92, "stalker_run", 0, f_stalker_5, 4},
  {"stalker_move_false_death", 79, 88, "stalker_false_death", 0, f_stalker_6, 10},
  {"stalker_move_false_death_start", 70, 78, "stalker_false_death", 0, f_stalker_7, 9},
  {"stalker_move_pain", 66, 69, "stalker_run", 0, f_stalker_8, 4},
  {"stalker_move_shoot", 49, 52, "stalker_run", 0, f_stalker_9, 4},
  {"stalker_move_swing_l", 53, 60, "stalker_run", 0, f_stalker_10, 8},
  {"stalker_move_swing_r", 61, 65, "stalker_run", 0, f_stalker_11, 5},
  {"stalker_move_jump_straightup", 45, 48, "stalker_run", 0, f_stalker_12, 4},
  {"stalker_move_dodge_run", 49, 52, NULL, 0, f_stalker_13, 4},
  {"stalker_move_jump_up", 42, 48, "stalker_run", 0, f_stalker_14, 7},
  {"stalker_move_jump_down", 42, 48, "stalker_run", 0, f_stalker_15, 7},
  {"stalker_move_death", 70, 78, "stalker_dead", 0, f_stalker_16, 9},
};
const size_t qa_q2mpm_rogue_stalker_move_count = sizeof qa_q2mpm_rogue_stalker_moves / sizeof qa_q2mpm_rogue_stalker_moves[0];
