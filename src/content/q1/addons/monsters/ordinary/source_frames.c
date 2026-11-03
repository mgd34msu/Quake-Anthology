/* The TS ordinary/index.ts frame tables: hangFrames and the sourceFrames(context, rocket)
 * compositions new Map([...hangFrames, ...classicFrames(mg1|mg3), ...chthonFrames,
 * ...(mg3 ? mg3NightmareFrames : []), ...(rocket ? rocketOgreFrames : [])]) for each
 * distinct key, generated from the TS modules via bun (do not hand-edit).
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/source_frames.h"

static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_hang_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_hang_frames = {entries_qa_q1am_ordinary_hang_frames, sizeof entries_qa_q1am_ordinary_hang_frames / sizeof entries_qa_q1am_ordinary_hang_frames[0]};

static const qa_q1_monster_operation mg1_ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_9[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_29[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_33[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11},
};
static const qa_q1_monster_operation mg1_ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg1_ops_35[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg1_ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_44[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_fire"},
};
static const qa_q1_monster_operation mg1_ops_46[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_47[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_refire"},
};
static const qa_q1_monster_operation mg1_ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_55[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13},
};
static const qa_q1_monster_operation mg1_ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9},
};
static const qa_q1_monster_operation mg1_ops_67[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_71[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_74[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_75[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_77[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg1_ops_78[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_79[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6},
};
static const qa_q1_monster_operation mg1_ops_81[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_83[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_84[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_85[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_86[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_87[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_88[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_89[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_90[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_91[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_92[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_93[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_94[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_95[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_96[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_97[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_98[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_99[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_ops_100[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg1_ops_101[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_ops_102[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_ops_103[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg1_ops_104[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg1_ops_105[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_ops_106[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg1_ops_107[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_ops_108[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_ops_109[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg1_ops_110[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg1_ops_111[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_ops_112[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_113[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_114[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_115[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_bite"},
};
static const qa_q1_monster_operation mg1_ops_116[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_117[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_118[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_119[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_120[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_ops_121[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_jump"},
};
static const qa_q1_monster_operation mg1_ops_137[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_138[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg1_ops_139[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg1_ops_140[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg1_ops_142[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_144[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10},
};
static const qa_q1_monster_operation mg1_ops_171[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
  {.kind = QA_Q1_OP_ACTION, .name = "army_die3"},
};
static const qa_q1_monster_operation mg1_ops_180[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5},
};
static const qa_q1_monster_operation mg1_ops_181[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
  {.kind = QA_Q1_OP_ACTION, .name = "army_die3"},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_182[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 13},
};
static const qa_q1_monster_operation mg1_ops_183[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3},
};
static const qa_q1_monster_operation mg1_ops_184[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_ops_200[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg1_ops_201[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_mg1_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
  {"army_stand1", {0, "army_stand2", mg1_ops_1, 1}},
  {"army_stand2", {1, "army_stand3", mg1_ops_2, 1}},
  {"army_stand3", {2, "army_stand4", mg1_ops_3, 1}},
  {"army_stand4", {3, "army_stand5", mg1_ops_4, 1}},
  {"army_stand5", {4, "army_stand6", mg1_ops_5, 1}},
  {"army_stand6", {5, "army_stand7", mg1_ops_6, 1}},
  {"army_stand7", {6, "army_stand8", mg1_ops_7, 1}},
  {"army_stand8", {7, "army_stand1", mg1_ops_8, 1}},
  {"army_walk1", {90, "army_walk2", mg1_ops_9, 2}},
  {"army_walk2", {91, "army_walk3", mg1_ops_10, 1}},
  {"army_walk3", {92, "army_walk4", mg1_ops_11, 1}},
  {"army_walk4", {93, "army_walk5", mg1_ops_12, 1}},
  {"army_walk5", {94, "army_walk6", mg1_ops_13, 1}},
  {"army_walk6", {95, "army_walk7", mg1_ops_14, 1}},
  {"army_walk7", {96, "army_walk8", mg1_ops_15, 1}},
  {"army_walk8", {97, "army_walk9", mg1_ops_16, 1}},
  {"army_walk9", {98, "army_walk10", mg1_ops_17, 1}},
  {"army_walk10", {99, "army_walk11", mg1_ops_18, 1}},
  {"army_walk11", {100, "army_walk12", mg1_ops_19, 1}},
  {"army_walk12", {101, "army_walk13", mg1_ops_20, 1}},
  {"army_walk13", {102, "army_walk14", mg1_ops_21, 1}},
  {"army_walk14", {103, "army_walk15", mg1_ops_22, 1}},
  {"army_walk15", {104, "army_walk16", mg1_ops_23, 1}},
  {"army_walk16", {105, "army_walk17", mg1_ops_24, 1}},
  {"army_walk17", {106, "army_walk18", mg1_ops_25, 1}},
  {"army_walk18", {107, "army_walk19", mg1_ops_26, 1}},
  {"army_walk19", {108, "army_walk20", mg1_ops_27, 1}},
  {"army_walk20", {109, "army_walk21", mg1_ops_28, 1}},
  {"army_walk21", {110, "army_walk22", mg1_ops_29, 1}},
  {"army_walk22", {111, "army_walk23", mg1_ops_30, 1}},
  {"army_walk23", {112, "army_walk24", mg1_ops_31, 1}},
  {"army_walk24", {113, "army_walk1", mg1_ops_32, 1}},
  {"army_run1", {73, "army_run2", mg1_ops_33, 2}},
  {"army_run2", {74, "army_run3", mg1_ops_34, 1}},
  {"army_run3", {75, "army_run4", mg1_ops_35, 1}},
  {"army_run4", {76, "army_run5", mg1_ops_36, 1}},
  {"army_run5", {77, "army_run6", mg1_ops_37, 1}},
  {"army_run6", {78, "army_run7", mg1_ops_38, 1}},
  {"army_run7", {79, "army_run8", mg1_ops_39, 1}},
  {"army_run8", {80, "army_run1", mg1_ops_40, 1}},
  {"army_atk1", {81, "army_atk2", mg1_ops_41, 1}},
  {"army_atk2", {82, "army_atk3", mg1_ops_42, 1}},
  {"army_atk3", {83, "army_atk4", mg1_ops_43, 1}},
  {"army_atk4", {84, "army_atk5", mg1_ops_44, 1}},
  {"army_atk5", {85, "army_atk6", mg1_ops_45, 2}},
  {"army_atk6", {86, "army_atk7", mg1_ops_46, 1}},
  {"army_atk7", {87, "army_atk8", mg1_ops_47, 2}},
  {"army_atk8", {88, "army_atk9", mg1_ops_48, 1}},
  {"army_atk9", {89, "army_run1", mg1_ops_49, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", mg1_ops_55, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", mg1_ops_57, 1}},
  {"army_painb3", {48, "army_painb4", mg1_ops_58, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", mg1_ops_67, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", mg1_ops_71, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", mg1_ops_74, 1}},
  {"army_painc6", {65, "army_painc7", mg1_ops_75, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", mg1_ops_77, 1}},
  {"army_painc9", {68, "army_painc10", mg1_ops_78, 1}},
  {"army_painc10", {69, "army_painc11", mg1_ops_79, 1}},
  {"army_painc11", {70, "army_painc12", mg1_ops_80, 1}},
  {"army_painc12", {71, "army_painc13", mg1_ops_81, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", mg1_ops_83, 1}},
  {"dog_stand2", {70, "dog_stand3", mg1_ops_84, 1}},
  {"dog_stand3", {71, "dog_stand4", mg1_ops_85, 1}},
  {"dog_stand4", {72, "dog_stand5", mg1_ops_86, 1}},
  {"dog_stand5", {73, "dog_stand6", mg1_ops_87, 1}},
  {"dog_stand6", {74, "dog_stand7", mg1_ops_88, 1}},
  {"dog_stand7", {75, "dog_stand8", mg1_ops_89, 1}},
  {"dog_stand8", {76, "dog_stand9", mg1_ops_90, 1}},
  {"dog_stand9", {77, "dog_stand1", mg1_ops_91, 1}},
  {"dog_walk1", {78, "dog_walk2", mg1_ops_92, 2}},
  {"dog_walk2", {79, "dog_walk3", mg1_ops_93, 1}},
  {"dog_walk3", {80, "dog_walk4", mg1_ops_94, 1}},
  {"dog_walk4", {81, "dog_walk5", mg1_ops_95, 1}},
  {"dog_walk5", {82, "dog_walk6", mg1_ops_96, 1}},
  {"dog_walk6", {83, "dog_walk7", mg1_ops_97, 1}},
  {"dog_walk7", {84, "dog_walk8", mg1_ops_98, 1}},
  {"dog_walk8", {85, "dog_walk1", mg1_ops_99, 1}},
  {"dog_run1", {48, "dog_run2", mg1_ops_100, 2}},
  {"dog_run2", {49, "dog_run3", mg1_ops_101, 1}},
  {"dog_run3", {50, "dog_run4", mg1_ops_102, 1}},
  {"dog_run4", {51, "dog_run5", mg1_ops_103, 1}},
  {"dog_run5", {52, "dog_run6", mg1_ops_104, 1}},
  {"dog_run6", {53, "dog_run7", mg1_ops_105, 1}},
  {"dog_run7", {54, "dog_run8", mg1_ops_106, 1}},
  {"dog_run8", {55, "dog_run9", mg1_ops_107, 1}},
  {"dog_run9", {56, "dog_run10", mg1_ops_108, 1}},
  {"dog_run10", {57, "dog_run11", mg1_ops_109, 1}},
  {"dog_run11", {58, "dog_run12", mg1_ops_110, 1}},
  {"dog_run12", {59, "dog_run1", mg1_ops_111, 1}},
  {"dog_atta1", {0, "dog_atta2", mg1_ops_112, 1}},
  {"dog_atta2", {1, "dog_atta3", mg1_ops_113, 1}},
  {"dog_atta3", {2, "dog_atta4", mg1_ops_114, 1}},
  {"dog_atta4", {3, "dog_atta5", mg1_ops_115, 2}},
  {"dog_atta5", {4, "dog_atta6", mg1_ops_116, 1}},
  {"dog_atta6", {5, "dog_atta7", mg1_ops_117, 1}},
  {"dog_atta7", {6, "dog_atta8", mg1_ops_118, 1}},
  {"dog_atta8", {7, "dog_run1", mg1_ops_119, 1}},
  {"dog_leap1", {60, "dog_leap2", mg1_ops_120, 1}},
  {"dog_leap2", {61, "dog_leap3", mg1_ops_121, 2}},
  {"dog_leap3", {62, "dog_leap4", NULL, 0}},
  {"dog_leap4", {63, "dog_leap5", NULL, 0}},
  {"dog_leap5", {64, "dog_leap6", NULL, 0}},
  {"dog_leap6", {65, "dog_leap7", NULL, 0}},
  {"dog_leap7", {66, "dog_leap8", NULL, 0}},
  {"dog_leap8", {67, "dog_leap9", NULL, 0}},
  {"dog_leap9", {68, "dog_leap9", NULL, 0}},
  {"dog_pain1", {26, "dog_pain2", NULL, 0}},
  {"dog_pain2", {27, "dog_pain3", NULL, 0}},
  {"dog_pain3", {28, "dog_pain4", NULL, 0}},
  {"dog_pain4", {29, "dog_pain5", NULL, 0}},
  {"dog_pain5", {30, "dog_pain6", NULL, 0}},
  {"dog_pain6", {31, "dog_run1", NULL, 0}},
  {"dog_painb1", {32, "dog_painb2", NULL, 0}},
  {"dog_painb2", {33, "dog_painb3", NULL, 0}},
  {"dog_painb3", {34, "dog_painb4", mg1_ops_137, 1}},
  {"dog_painb4", {35, "dog_painb5", mg1_ops_138, 1}},
  {"dog_painb5", {36, "dog_painb6", mg1_ops_139, 1}},
  {"dog_painb6", {37, "dog_painb7", mg1_ops_140, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", mg1_ops_142, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", mg1_ops_144, 1}},
  {"dog_painb11", {42, "dog_painb12", NULL, 0}},
  {"dog_painb12", {43, "dog_painb13", NULL, 0}},
  {"dog_painb13", {44, "dog_painb14", NULL, 0}},
  {"dog_painb14", {45, "dog_painb15", NULL, 0}},
  {"dog_painb15", {46, "dog_painb16", NULL, 0}},
  {"dog_painb16", {47, "dog_run1", NULL, 0}},
  {"dog_die1", {8, "dog_die2", NULL, 0}},
  {"dog_die2", {9, "dog_die3", NULL, 0}},
  {"dog_die3", {10, "dog_die4", NULL, 0}},
  {"dog_die4", {11, "dog_die5", NULL, 0}},
  {"dog_die5", {12, "dog_die6", NULL, 0}},
  {"dog_die6", {13, "dog_die7", NULL, 0}},
  {"dog_die7", {14, "dog_die8", NULL, 0}},
  {"dog_die8", {15, "dog_die9", NULL, 0}},
  {"dog_die9", {16, "dog_die9", NULL, 0}},
  {"dog_dieb1", {17, "dog_dieb2", NULL, 0}},
  {"dog_dieb2", {18, "dog_dieb3", NULL, 0}},
  {"dog_dieb3", {19, "dog_dieb4", NULL, 0}},
  {"dog_dieb4", {20, "dog_dieb5", NULL, 0}},
  {"dog_dieb5", {21, "dog_dieb6", NULL, 0}},
  {"dog_dieb6", {22, "dog_dieb7", NULL, 0}},
  {"dog_dieb7", {23, "dog_dieb8", NULL, 0}},
  {"dog_dieb8", {24, "dog_dieb9", NULL, 0}},
  {"dog_dieb9", {25, "dog_dieb9", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", mg1_ops_171, 2}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", mg1_ops_180, 1}},
  {"army_cdie3", {20, "army_cdie4", mg1_ops_181, 3}},
  {"army_cdie4", {21, "army_cdie5", mg1_ops_182, 1}},
  {"army_cdie5", {22, "army_cdie6", mg1_ops_183, 1}},
  {"army_cdie6", {23, "army_cdie7", mg1_ops_184, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"boss_shockz1", {80, "boss_shockz2", NULL, 0}},
  {"boss_shockz2", {81, "boss_shockz3", NULL, 0}},
  {"boss_shockz3", {82, "boss_shockz4", NULL, 0}},
  {"boss_shockz4", {83, "boss_shockz5", NULL, 0}},
  {"boss_shockz5", {84, "boss_shockz6", NULL, 0}},
  {"boss_shockz6", {85, "boss_shockz7", NULL, 0}},
  {"boss_shockz7", {86, "boss_shockz8", NULL, 0}},
  {"boss_shockz8", {87, "boss_shockz9", NULL, 0}},
  {"boss_shockz9", {88, "boss_shockz10", NULL, 0}},
  {"boss_shockz10", {89, "boss_missile1", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", mg1_ops_200, 1}},
  {"boss_shockb10", {93, "boss_missile1", mg1_ops_201, 1}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_mg1_frames = {entries_qa_q1am_ordinary_mg1_frames, sizeof entries_qa_q1am_ordinary_mg1_frames / sizeof entries_qa_q1am_ordinary_mg1_frames[0]};

static const qa_q1_monster_operation mg1_rocket_ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_9[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_29[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_33[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11},
};
static const qa_q1_monster_operation mg1_rocket_ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg1_rocket_ops_35[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg1_rocket_ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_44[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_fire"},
};
static const qa_q1_monster_operation mg1_rocket_ops_46[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_47[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_refire"},
};
static const qa_q1_monster_operation mg1_rocket_ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_55[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13},
};
static const qa_q1_monster_operation mg1_rocket_ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9},
};
static const qa_q1_monster_operation mg1_rocket_ops_67[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_71[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_74[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_75[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_77[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg1_rocket_ops_78[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_79[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6},
};
static const qa_q1_monster_operation mg1_rocket_ops_81[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_83[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_84[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_85[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_86[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_87[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_88[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_89[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_90[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_91[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_92[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_93[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_94[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_95[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_96[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_97[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_98[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_99[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg1_rocket_ops_100[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg1_rocket_ops_101[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_rocket_ops_102[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_rocket_ops_103[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg1_rocket_ops_104[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg1_rocket_ops_105[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_rocket_ops_106[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg1_rocket_ops_107[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_rocket_ops_108[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_rocket_ops_109[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg1_rocket_ops_110[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg1_rocket_ops_111[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg1_rocket_ops_112[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_113[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_114[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_115[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_bite"},
};
static const qa_q1_monster_operation mg1_rocket_ops_116[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_117[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_118[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_119[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_120[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_121[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_jump"},
};
static const qa_q1_monster_operation mg1_rocket_ops_137[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_138[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg1_rocket_ops_139[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg1_rocket_ops_140[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_142[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_144[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10},
};
static const qa_q1_monster_operation mg1_rocket_ops_171[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
  {.kind = QA_Q1_OP_ACTION, .name = "army_die3"},
};
static const qa_q1_monster_operation mg1_rocket_ops_180[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5},
};
static const qa_q1_monster_operation mg1_rocket_ops_181[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
  {.kind = QA_Q1_OP_ACTION, .name = "army_die3"},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_182[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 13},
};
static const qa_q1_monster_operation mg1_rocket_ops_183[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3},
};
static const qa_q1_monster_operation mg1_rocket_ops_184[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg1_rocket_ops_200[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg1_rocket_ops_201[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg1_rocket_ops_202[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_203[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg1_rocket_ops_204[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 9},
};
static const qa_q1_monster_operation mg1_rocket_ops_205[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_206[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_207[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg1_rocket_ops_208[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation mg1_rocket_ops_209[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_mg1_rocket_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
  {"army_stand1", {0, "army_stand2", mg1_rocket_ops_1, 1}},
  {"army_stand2", {1, "army_stand3", mg1_rocket_ops_2, 1}},
  {"army_stand3", {2, "army_stand4", mg1_rocket_ops_3, 1}},
  {"army_stand4", {3, "army_stand5", mg1_rocket_ops_4, 1}},
  {"army_stand5", {4, "army_stand6", mg1_rocket_ops_5, 1}},
  {"army_stand6", {5, "army_stand7", mg1_rocket_ops_6, 1}},
  {"army_stand7", {6, "army_stand8", mg1_rocket_ops_7, 1}},
  {"army_stand8", {7, "army_stand1", mg1_rocket_ops_8, 1}},
  {"army_walk1", {90, "army_walk2", mg1_rocket_ops_9, 2}},
  {"army_walk2", {91, "army_walk3", mg1_rocket_ops_10, 1}},
  {"army_walk3", {92, "army_walk4", mg1_rocket_ops_11, 1}},
  {"army_walk4", {93, "army_walk5", mg1_rocket_ops_12, 1}},
  {"army_walk5", {94, "army_walk6", mg1_rocket_ops_13, 1}},
  {"army_walk6", {95, "army_walk7", mg1_rocket_ops_14, 1}},
  {"army_walk7", {96, "army_walk8", mg1_rocket_ops_15, 1}},
  {"army_walk8", {97, "army_walk9", mg1_rocket_ops_16, 1}},
  {"army_walk9", {98, "army_walk10", mg1_rocket_ops_17, 1}},
  {"army_walk10", {99, "army_walk11", mg1_rocket_ops_18, 1}},
  {"army_walk11", {100, "army_walk12", mg1_rocket_ops_19, 1}},
  {"army_walk12", {101, "army_walk13", mg1_rocket_ops_20, 1}},
  {"army_walk13", {102, "army_walk14", mg1_rocket_ops_21, 1}},
  {"army_walk14", {103, "army_walk15", mg1_rocket_ops_22, 1}},
  {"army_walk15", {104, "army_walk16", mg1_rocket_ops_23, 1}},
  {"army_walk16", {105, "army_walk17", mg1_rocket_ops_24, 1}},
  {"army_walk17", {106, "army_walk18", mg1_rocket_ops_25, 1}},
  {"army_walk18", {107, "army_walk19", mg1_rocket_ops_26, 1}},
  {"army_walk19", {108, "army_walk20", mg1_rocket_ops_27, 1}},
  {"army_walk20", {109, "army_walk21", mg1_rocket_ops_28, 1}},
  {"army_walk21", {110, "army_walk22", mg1_rocket_ops_29, 1}},
  {"army_walk22", {111, "army_walk23", mg1_rocket_ops_30, 1}},
  {"army_walk23", {112, "army_walk24", mg1_rocket_ops_31, 1}},
  {"army_walk24", {113, "army_walk1", mg1_rocket_ops_32, 1}},
  {"army_run1", {73, "army_run2", mg1_rocket_ops_33, 2}},
  {"army_run2", {74, "army_run3", mg1_rocket_ops_34, 1}},
  {"army_run3", {75, "army_run4", mg1_rocket_ops_35, 1}},
  {"army_run4", {76, "army_run5", mg1_rocket_ops_36, 1}},
  {"army_run5", {77, "army_run6", mg1_rocket_ops_37, 1}},
  {"army_run6", {78, "army_run7", mg1_rocket_ops_38, 1}},
  {"army_run7", {79, "army_run8", mg1_rocket_ops_39, 1}},
  {"army_run8", {80, "army_run1", mg1_rocket_ops_40, 1}},
  {"army_atk1", {81, "army_atk2", mg1_rocket_ops_41, 1}},
  {"army_atk2", {82, "army_atk3", mg1_rocket_ops_42, 1}},
  {"army_atk3", {83, "army_atk4", mg1_rocket_ops_43, 1}},
  {"army_atk4", {84, "army_atk5", mg1_rocket_ops_44, 1}},
  {"army_atk5", {85, "army_atk6", mg1_rocket_ops_45, 2}},
  {"army_atk6", {86, "army_atk7", mg1_rocket_ops_46, 1}},
  {"army_atk7", {87, "army_atk8", mg1_rocket_ops_47, 2}},
  {"army_atk8", {88, "army_atk9", mg1_rocket_ops_48, 1}},
  {"army_atk9", {89, "army_run1", mg1_rocket_ops_49, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", mg1_rocket_ops_55, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", mg1_rocket_ops_57, 1}},
  {"army_painb3", {48, "army_painb4", mg1_rocket_ops_58, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", mg1_rocket_ops_67, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", mg1_rocket_ops_71, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", mg1_rocket_ops_74, 1}},
  {"army_painc6", {65, "army_painc7", mg1_rocket_ops_75, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", mg1_rocket_ops_77, 1}},
  {"army_painc9", {68, "army_painc10", mg1_rocket_ops_78, 1}},
  {"army_painc10", {69, "army_painc11", mg1_rocket_ops_79, 1}},
  {"army_painc11", {70, "army_painc12", mg1_rocket_ops_80, 1}},
  {"army_painc12", {71, "army_painc13", mg1_rocket_ops_81, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", mg1_rocket_ops_83, 1}},
  {"dog_stand2", {70, "dog_stand3", mg1_rocket_ops_84, 1}},
  {"dog_stand3", {71, "dog_stand4", mg1_rocket_ops_85, 1}},
  {"dog_stand4", {72, "dog_stand5", mg1_rocket_ops_86, 1}},
  {"dog_stand5", {73, "dog_stand6", mg1_rocket_ops_87, 1}},
  {"dog_stand6", {74, "dog_stand7", mg1_rocket_ops_88, 1}},
  {"dog_stand7", {75, "dog_stand8", mg1_rocket_ops_89, 1}},
  {"dog_stand8", {76, "dog_stand9", mg1_rocket_ops_90, 1}},
  {"dog_stand9", {77, "dog_stand1", mg1_rocket_ops_91, 1}},
  {"dog_walk1", {78, "dog_walk2", mg1_rocket_ops_92, 2}},
  {"dog_walk2", {79, "dog_walk3", mg1_rocket_ops_93, 1}},
  {"dog_walk3", {80, "dog_walk4", mg1_rocket_ops_94, 1}},
  {"dog_walk4", {81, "dog_walk5", mg1_rocket_ops_95, 1}},
  {"dog_walk5", {82, "dog_walk6", mg1_rocket_ops_96, 1}},
  {"dog_walk6", {83, "dog_walk7", mg1_rocket_ops_97, 1}},
  {"dog_walk7", {84, "dog_walk8", mg1_rocket_ops_98, 1}},
  {"dog_walk8", {85, "dog_walk1", mg1_rocket_ops_99, 1}},
  {"dog_run1", {48, "dog_run2", mg1_rocket_ops_100, 2}},
  {"dog_run2", {49, "dog_run3", mg1_rocket_ops_101, 1}},
  {"dog_run3", {50, "dog_run4", mg1_rocket_ops_102, 1}},
  {"dog_run4", {51, "dog_run5", mg1_rocket_ops_103, 1}},
  {"dog_run5", {52, "dog_run6", mg1_rocket_ops_104, 1}},
  {"dog_run6", {53, "dog_run7", mg1_rocket_ops_105, 1}},
  {"dog_run7", {54, "dog_run8", mg1_rocket_ops_106, 1}},
  {"dog_run8", {55, "dog_run9", mg1_rocket_ops_107, 1}},
  {"dog_run9", {56, "dog_run10", mg1_rocket_ops_108, 1}},
  {"dog_run10", {57, "dog_run11", mg1_rocket_ops_109, 1}},
  {"dog_run11", {58, "dog_run12", mg1_rocket_ops_110, 1}},
  {"dog_run12", {59, "dog_run1", mg1_rocket_ops_111, 1}},
  {"dog_atta1", {0, "dog_atta2", mg1_rocket_ops_112, 1}},
  {"dog_atta2", {1, "dog_atta3", mg1_rocket_ops_113, 1}},
  {"dog_atta3", {2, "dog_atta4", mg1_rocket_ops_114, 1}},
  {"dog_atta4", {3, "dog_atta5", mg1_rocket_ops_115, 2}},
  {"dog_atta5", {4, "dog_atta6", mg1_rocket_ops_116, 1}},
  {"dog_atta6", {5, "dog_atta7", mg1_rocket_ops_117, 1}},
  {"dog_atta7", {6, "dog_atta8", mg1_rocket_ops_118, 1}},
  {"dog_atta8", {7, "dog_run1", mg1_rocket_ops_119, 1}},
  {"dog_leap1", {60, "dog_leap2", mg1_rocket_ops_120, 1}},
  {"dog_leap2", {61, "dog_leap3", mg1_rocket_ops_121, 2}},
  {"dog_leap3", {62, "dog_leap4", NULL, 0}},
  {"dog_leap4", {63, "dog_leap5", NULL, 0}},
  {"dog_leap5", {64, "dog_leap6", NULL, 0}},
  {"dog_leap6", {65, "dog_leap7", NULL, 0}},
  {"dog_leap7", {66, "dog_leap8", NULL, 0}},
  {"dog_leap8", {67, "dog_leap9", NULL, 0}},
  {"dog_leap9", {68, "dog_leap9", NULL, 0}},
  {"dog_pain1", {26, "dog_pain2", NULL, 0}},
  {"dog_pain2", {27, "dog_pain3", NULL, 0}},
  {"dog_pain3", {28, "dog_pain4", NULL, 0}},
  {"dog_pain4", {29, "dog_pain5", NULL, 0}},
  {"dog_pain5", {30, "dog_pain6", NULL, 0}},
  {"dog_pain6", {31, "dog_run1", NULL, 0}},
  {"dog_painb1", {32, "dog_painb2", NULL, 0}},
  {"dog_painb2", {33, "dog_painb3", NULL, 0}},
  {"dog_painb3", {34, "dog_painb4", mg1_rocket_ops_137, 1}},
  {"dog_painb4", {35, "dog_painb5", mg1_rocket_ops_138, 1}},
  {"dog_painb5", {36, "dog_painb6", mg1_rocket_ops_139, 1}},
  {"dog_painb6", {37, "dog_painb7", mg1_rocket_ops_140, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", mg1_rocket_ops_142, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", mg1_rocket_ops_144, 1}},
  {"dog_painb11", {42, "dog_painb12", NULL, 0}},
  {"dog_painb12", {43, "dog_painb13", NULL, 0}},
  {"dog_painb13", {44, "dog_painb14", NULL, 0}},
  {"dog_painb14", {45, "dog_painb15", NULL, 0}},
  {"dog_painb15", {46, "dog_painb16", NULL, 0}},
  {"dog_painb16", {47, "dog_run1", NULL, 0}},
  {"dog_die1", {8, "dog_die2", NULL, 0}},
  {"dog_die2", {9, "dog_die3", NULL, 0}},
  {"dog_die3", {10, "dog_die4", NULL, 0}},
  {"dog_die4", {11, "dog_die5", NULL, 0}},
  {"dog_die5", {12, "dog_die6", NULL, 0}},
  {"dog_die6", {13, "dog_die7", NULL, 0}},
  {"dog_die7", {14, "dog_die8", NULL, 0}},
  {"dog_die8", {15, "dog_die9", NULL, 0}},
  {"dog_die9", {16, "dog_die9", NULL, 0}},
  {"dog_dieb1", {17, "dog_dieb2", NULL, 0}},
  {"dog_dieb2", {18, "dog_dieb3", NULL, 0}},
  {"dog_dieb3", {19, "dog_dieb4", NULL, 0}},
  {"dog_dieb4", {20, "dog_dieb5", NULL, 0}},
  {"dog_dieb5", {21, "dog_dieb6", NULL, 0}},
  {"dog_dieb6", {22, "dog_dieb7", NULL, 0}},
  {"dog_dieb7", {23, "dog_dieb8", NULL, 0}},
  {"dog_dieb8", {24, "dog_dieb9", NULL, 0}},
  {"dog_dieb9", {25, "dog_dieb9", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", mg1_rocket_ops_171, 2}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", mg1_rocket_ops_180, 1}},
  {"army_cdie3", {20, "army_cdie4", mg1_rocket_ops_181, 3}},
  {"army_cdie4", {21, "army_cdie5", mg1_rocket_ops_182, 1}},
  {"army_cdie5", {22, "army_cdie6", mg1_rocket_ops_183, 1}},
  {"army_cdie6", {23, "army_cdie7", mg1_rocket_ops_184, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"boss_shockz1", {80, "boss_shockz2", NULL, 0}},
  {"boss_shockz2", {81, "boss_shockz3", NULL, 0}},
  {"boss_shockz3", {82, "boss_shockz4", NULL, 0}},
  {"boss_shockz4", {83, "boss_shockz5", NULL, 0}},
  {"boss_shockz5", {84, "boss_shockz6", NULL, 0}},
  {"boss_shockz6", {85, "boss_shockz7", NULL, 0}},
  {"boss_shockz7", {86, "boss_shockz8", NULL, 0}},
  {"boss_shockz8", {87, "boss_shockz9", NULL, 0}},
  {"boss_shockz9", {88, "boss_shockz10", NULL, 0}},
  {"boss_shockz10", {89, "boss_missile1", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", mg1_rocket_ops_200, 1}},
  {"boss_shockb10", {93, "boss_missile1", mg1_rocket_ops_201, 1}},
  {"ogre_stand5", {4, "ogre_stand6", mg1_rocket_ops_202, 1}},
  {"ogre_walk3", {11, "ogre_walk4", mg1_rocket_ops_203, 1}},
  {"ogre_run1", {25, "ogre_run2", mg1_rocket_ops_204, 1}},
  {"ogre_nail1", {61, "ogre_nail2", mg1_rocket_ops_205, 1}},
  {"ogre_nail4", {63, "ogre_nail5", mg1_rocket_ops_206, 1}},
  {"ogre_nail5", {64, "ogre_nail6", mg1_rocket_ops_207, 1}},
  {"ogre_die3", {114, "ogre_die4", mg1_rocket_ops_208, 1}},
  {"ogre_bdie3", {128, "ogre_bdie4", mg1_rocket_ops_209, 1}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_mg1_rocket_frames = {entries_qa_q1am_ordinary_mg1_rocket_frames, sizeof entries_qa_q1am_ordinary_mg1_rocket_frames / sizeof entries_qa_q1am_ordinary_mg1_rocket_frames[0]};

static const qa_q1_monster_operation mg3_ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_9[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg3_ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg3_ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_29[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_33[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11},
};
static const qa_q1_monster_operation mg3_ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg3_ops_35[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg3_ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_44[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_fire"},
};
static const qa_q1_monster_operation mg3_ops_46[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_47[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_refire"},
};
static const qa_q1_monster_operation mg3_ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_55[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13},
};
static const qa_q1_monster_operation mg3_ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9},
};
static const qa_q1_monster_operation mg3_ops_67[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_71[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_74[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_75[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_77[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg3_ops_78[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4},
};
static const qa_q1_monster_operation mg3_ops_79[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6},
};
static const qa_q1_monster_operation mg3_ops_81[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_83[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_84[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_85[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_86[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_87[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_88[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_89[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_90[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_91[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_92[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_93[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_94[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_95[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_96[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_97[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_98[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_99[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_ops_100[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg3_ops_101[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_ops_102[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_ops_103[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg3_ops_104[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg3_ops_105[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_ops_106[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg3_ops_107[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_ops_108[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_ops_109[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg3_ops_110[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg3_ops_111[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_ops_112[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_113[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_114[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_115[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_bite"},
};
static const qa_q1_monster_operation mg3_ops_116[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_117[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_118[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_119[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_120[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_ops_121[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_jump"},
};
static const qa_q1_monster_operation mg3_ops_137[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg3_ops_138[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg3_ops_139[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg3_ops_140[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg3_ops_142[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg3_ops_144[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10},
};
static const qa_q1_monster_operation mg3_ops_171[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation mg3_ops_180[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5},
};
static const qa_q1_monster_operation mg3_ops_181[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation mg3_ops_182[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 13},
};
static const qa_q1_monster_operation mg3_ops_183[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3},
};
static const qa_q1_monster_operation mg3_ops_184[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg3_ops_200[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg3_ops_201[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg3_ops_202[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4},
  {.kind = QA_Q1_OP_ACTION, .name = "sham_smash12"},
};
static const qa_q1_monster_operation mg3_ops_203[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "ogre_nail1"},
};
static const qa_q1_monster_operation mg3_ops_204[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "ogre_nail5"},
};
static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_mg3_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
  {"army_stand1", {0, "army_stand2", mg3_ops_1, 1}},
  {"army_stand2", {1, "army_stand3", mg3_ops_2, 1}},
  {"army_stand3", {2, "army_stand4", mg3_ops_3, 1}},
  {"army_stand4", {3, "army_stand5", mg3_ops_4, 1}},
  {"army_stand5", {4, "army_stand6", mg3_ops_5, 1}},
  {"army_stand6", {5, "army_stand7", mg3_ops_6, 1}},
  {"army_stand7", {6, "army_stand8", mg3_ops_7, 1}},
  {"army_stand8", {7, "army_stand1", mg3_ops_8, 1}},
  {"army_walk1", {90, "army_walk2", mg3_ops_9, 2}},
  {"army_walk2", {91, "army_walk3", mg3_ops_10, 1}},
  {"army_walk3", {92, "army_walk4", mg3_ops_11, 1}},
  {"army_walk4", {93, "army_walk5", mg3_ops_12, 1}},
  {"army_walk5", {94, "army_walk6", mg3_ops_13, 1}},
  {"army_walk6", {95, "army_walk7", mg3_ops_14, 1}},
  {"army_walk7", {96, "army_walk8", mg3_ops_15, 1}},
  {"army_walk8", {97, "army_walk9", mg3_ops_16, 1}},
  {"army_walk9", {98, "army_walk10", mg3_ops_17, 1}},
  {"army_walk10", {99, "army_walk11", mg3_ops_18, 1}},
  {"army_walk11", {100, "army_walk12", mg3_ops_19, 1}},
  {"army_walk12", {101, "army_walk13", mg3_ops_20, 1}},
  {"army_walk13", {102, "army_walk14", mg3_ops_21, 1}},
  {"army_walk14", {103, "army_walk15", mg3_ops_22, 1}},
  {"army_walk15", {104, "army_walk16", mg3_ops_23, 1}},
  {"army_walk16", {105, "army_walk17", mg3_ops_24, 1}},
  {"army_walk17", {106, "army_walk18", mg3_ops_25, 1}},
  {"army_walk18", {107, "army_walk19", mg3_ops_26, 1}},
  {"army_walk19", {108, "army_walk20", mg3_ops_27, 1}},
  {"army_walk20", {109, "army_walk21", mg3_ops_28, 1}},
  {"army_walk21", {110, "army_walk22", mg3_ops_29, 1}},
  {"army_walk22", {111, "army_walk23", mg3_ops_30, 1}},
  {"army_walk23", {112, "army_walk24", mg3_ops_31, 1}},
  {"army_walk24", {113, "army_walk1", mg3_ops_32, 1}},
  {"army_run1", {73, "army_run2", mg3_ops_33, 2}},
  {"army_run2", {74, "army_run3", mg3_ops_34, 1}},
  {"army_run3", {75, "army_run4", mg3_ops_35, 1}},
  {"army_run4", {76, "army_run5", mg3_ops_36, 1}},
  {"army_run5", {77, "army_run6", mg3_ops_37, 1}},
  {"army_run6", {78, "army_run7", mg3_ops_38, 1}},
  {"army_run7", {79, "army_run8", mg3_ops_39, 1}},
  {"army_run8", {80, "army_run1", mg3_ops_40, 1}},
  {"army_atk1", {81, "army_atk2", mg3_ops_41, 1}},
  {"army_atk2", {82, "army_atk3", mg3_ops_42, 1}},
  {"army_atk3", {83, "army_atk4", mg3_ops_43, 1}},
  {"army_atk4", {84, "army_atk5", mg3_ops_44, 1}},
  {"army_atk5", {85, "army_atk6", mg3_ops_45, 2}},
  {"army_atk6", {86, "army_atk7", mg3_ops_46, 1}},
  {"army_atk7", {87, "army_atk8", mg3_ops_47, 2}},
  {"army_atk8", {88, "army_atk9", mg3_ops_48, 1}},
  {"army_atk9", {89, "army_run1", mg3_ops_49, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", mg3_ops_55, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", mg3_ops_57, 1}},
  {"army_painb3", {48, "army_painb4", mg3_ops_58, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", mg3_ops_67, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", mg3_ops_71, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", mg3_ops_74, 1}},
  {"army_painc6", {65, "army_painc7", mg3_ops_75, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", mg3_ops_77, 1}},
  {"army_painc9", {68, "army_painc10", mg3_ops_78, 1}},
  {"army_painc10", {69, "army_painc11", mg3_ops_79, 1}},
  {"army_painc11", {70, "army_painc12", mg3_ops_80, 1}},
  {"army_painc12", {71, "army_painc13", mg3_ops_81, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", mg3_ops_83, 1}},
  {"dog_stand2", {70, "dog_stand3", mg3_ops_84, 1}},
  {"dog_stand3", {71, "dog_stand4", mg3_ops_85, 1}},
  {"dog_stand4", {72, "dog_stand5", mg3_ops_86, 1}},
  {"dog_stand5", {73, "dog_stand6", mg3_ops_87, 1}},
  {"dog_stand6", {74, "dog_stand7", mg3_ops_88, 1}},
  {"dog_stand7", {75, "dog_stand8", mg3_ops_89, 1}},
  {"dog_stand8", {76, "dog_stand9", mg3_ops_90, 1}},
  {"dog_stand9", {77, "dog_stand1", mg3_ops_91, 1}},
  {"dog_walk1", {78, "dog_walk2", mg3_ops_92, 2}},
  {"dog_walk2", {79, "dog_walk3", mg3_ops_93, 1}},
  {"dog_walk3", {80, "dog_walk4", mg3_ops_94, 1}},
  {"dog_walk4", {81, "dog_walk5", mg3_ops_95, 1}},
  {"dog_walk5", {82, "dog_walk6", mg3_ops_96, 1}},
  {"dog_walk6", {83, "dog_walk7", mg3_ops_97, 1}},
  {"dog_walk7", {84, "dog_walk8", mg3_ops_98, 1}},
  {"dog_walk8", {85, "dog_walk1", mg3_ops_99, 1}},
  {"dog_run1", {48, "dog_run2", mg3_ops_100, 2}},
  {"dog_run2", {49, "dog_run3", mg3_ops_101, 1}},
  {"dog_run3", {50, "dog_run4", mg3_ops_102, 1}},
  {"dog_run4", {51, "dog_run5", mg3_ops_103, 1}},
  {"dog_run5", {52, "dog_run6", mg3_ops_104, 1}},
  {"dog_run6", {53, "dog_run7", mg3_ops_105, 1}},
  {"dog_run7", {54, "dog_run8", mg3_ops_106, 1}},
  {"dog_run8", {55, "dog_run9", mg3_ops_107, 1}},
  {"dog_run9", {56, "dog_run10", mg3_ops_108, 1}},
  {"dog_run10", {57, "dog_run11", mg3_ops_109, 1}},
  {"dog_run11", {58, "dog_run12", mg3_ops_110, 1}},
  {"dog_run12", {59, "dog_run1", mg3_ops_111, 1}},
  {"dog_atta1", {0, "dog_atta2", mg3_ops_112, 1}},
  {"dog_atta2", {1, "dog_atta3", mg3_ops_113, 1}},
  {"dog_atta3", {2, "dog_atta4", mg3_ops_114, 1}},
  {"dog_atta4", {3, "dog_atta5", mg3_ops_115, 2}},
  {"dog_atta5", {4, "dog_atta6", mg3_ops_116, 1}},
  {"dog_atta6", {5, "dog_atta7", mg3_ops_117, 1}},
  {"dog_atta7", {6, "dog_atta8", mg3_ops_118, 1}},
  {"dog_atta8", {7, "dog_run1", mg3_ops_119, 1}},
  {"dog_leap1", {60, "dog_leap2", mg3_ops_120, 1}},
  {"dog_leap2", {61, "dog_leap3", mg3_ops_121, 2}},
  {"dog_leap3", {62, "dog_leap4", NULL, 0}},
  {"dog_leap4", {63, "dog_leap5", NULL, 0}},
  {"dog_leap5", {64, "dog_leap6", NULL, 0}},
  {"dog_leap6", {65, "dog_leap7", NULL, 0}},
  {"dog_leap7", {66, "dog_leap8", NULL, 0}},
  {"dog_leap8", {67, "dog_leap9", NULL, 0}},
  {"dog_leap9", {68, "dog_leap9", NULL, 0}},
  {"dog_pain1", {26, "dog_pain2", NULL, 0}},
  {"dog_pain2", {27, "dog_pain3", NULL, 0}},
  {"dog_pain3", {28, "dog_pain4", NULL, 0}},
  {"dog_pain4", {29, "dog_pain5", NULL, 0}},
  {"dog_pain5", {30, "dog_pain6", NULL, 0}},
  {"dog_pain6", {31, "dog_run1", NULL, 0}},
  {"dog_painb1", {32, "dog_painb2", NULL, 0}},
  {"dog_painb2", {33, "dog_painb3", NULL, 0}},
  {"dog_painb3", {34, "dog_painb4", mg3_ops_137, 1}},
  {"dog_painb4", {35, "dog_painb5", mg3_ops_138, 1}},
  {"dog_painb5", {36, "dog_painb6", mg3_ops_139, 1}},
  {"dog_painb6", {37, "dog_painb7", mg3_ops_140, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", mg3_ops_142, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", mg3_ops_144, 1}},
  {"dog_painb11", {42, "dog_painb12", NULL, 0}},
  {"dog_painb12", {43, "dog_painb13", NULL, 0}},
  {"dog_painb13", {44, "dog_painb14", NULL, 0}},
  {"dog_painb14", {45, "dog_painb15", NULL, 0}},
  {"dog_painb15", {46, "dog_painb16", NULL, 0}},
  {"dog_painb16", {47, "dog_run1", NULL, 0}},
  {"dog_die1", {8, "dog_die2", NULL, 0}},
  {"dog_die2", {9, "dog_die3", NULL, 0}},
  {"dog_die3", {10, "dog_die4", NULL, 0}},
  {"dog_die4", {11, "dog_die5", NULL, 0}},
  {"dog_die5", {12, "dog_die6", NULL, 0}},
  {"dog_die6", {13, "dog_die7", NULL, 0}},
  {"dog_die7", {14, "dog_die8", NULL, 0}},
  {"dog_die8", {15, "dog_die9", NULL, 0}},
  {"dog_die9", {16, "dog_die9", NULL, 0}},
  {"dog_dieb1", {17, "dog_dieb2", NULL, 0}},
  {"dog_dieb2", {18, "dog_dieb3", NULL, 0}},
  {"dog_dieb3", {19, "dog_dieb4", NULL, 0}},
  {"dog_dieb4", {20, "dog_dieb5", NULL, 0}},
  {"dog_dieb5", {21, "dog_dieb6", NULL, 0}},
  {"dog_dieb6", {22, "dog_dieb7", NULL, 0}},
  {"dog_dieb7", {23, "dog_dieb8", NULL, 0}},
  {"dog_dieb8", {24, "dog_dieb9", NULL, 0}},
  {"dog_dieb9", {25, "dog_dieb9", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", mg3_ops_171, 1}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", mg3_ops_180, 1}},
  {"army_cdie3", {20, "army_cdie4", mg3_ops_181, 1}},
  {"army_cdie4", {21, "army_cdie5", mg3_ops_182, 1}},
  {"army_cdie5", {22, "army_cdie6", mg3_ops_183, 1}},
  {"army_cdie6", {23, "army_cdie7", mg3_ops_184, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"boss_shockz1", {80, "boss_shockz2", NULL, 0}},
  {"boss_shockz2", {81, "boss_shockz3", NULL, 0}},
  {"boss_shockz3", {82, "boss_shockz4", NULL, 0}},
  {"boss_shockz4", {83, "boss_shockz5", NULL, 0}},
  {"boss_shockz5", {84, "boss_shockz6", NULL, 0}},
  {"boss_shockz6", {85, "boss_shockz7", NULL, 0}},
  {"boss_shockz7", {86, "boss_shockz8", NULL, 0}},
  {"boss_shockz8", {87, "boss_shockz9", NULL, 0}},
  {"boss_shockz9", {88, "boss_shockz10", NULL, 0}},
  {"boss_shockz10", {89, "boss_missile1", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", mg3_ops_200, 1}},
  {"boss_shockb10", {93, "boss_missile1", mg3_ops_201, 1}},
  {"sham_smash12", {46, "sham_run1", mg3_ops_202, 2}},
  {"ogre_nail1", {61, "ogre_nail2", mg3_ops_203, 2}},
  {"ogre_nail5", {64, "ogre_nail6", mg3_ops_204, 2}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_mg3_frames = {entries_qa_q1am_ordinary_mg3_frames, sizeof entries_qa_q1am_ordinary_mg3_frames / sizeof entries_qa_q1am_ordinary_mg3_frames[0]};

static const qa_q1_monster_operation mg3_rocket_ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_9[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg3_rocket_ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation mg3_rocket_ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_29[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_33[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11},
};
static const qa_q1_monster_operation mg3_rocket_ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg3_rocket_ops_35[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation mg3_rocket_ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_44[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_fire"},
};
static const qa_q1_monster_operation mg3_rocket_ops_46[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_47[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_refire"},
};
static const qa_q1_monster_operation mg3_rocket_ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_55[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13},
};
static const qa_q1_monster_operation mg3_rocket_ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9},
};
static const qa_q1_monster_operation mg3_rocket_ops_67[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_71[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_74[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_75[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_77[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation mg3_rocket_ops_78[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4},
};
static const qa_q1_monster_operation mg3_rocket_ops_79[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6},
};
static const qa_q1_monster_operation mg3_rocket_ops_81[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_83[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_84[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_85[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_86[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_87[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_88[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_89[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_90[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_91[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_92[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_93[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_94[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_95[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_96[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_97[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_98[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_99[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation mg3_rocket_ops_100[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg3_rocket_ops_101[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_rocket_ops_102[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_rocket_ops_103[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg3_rocket_ops_104[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg3_rocket_ops_105[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_rocket_ops_106[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation mg3_rocket_ops_107[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_rocket_ops_108[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_rocket_ops_109[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation mg3_rocket_ops_110[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation mg3_rocket_ops_111[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation mg3_rocket_ops_112[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_113[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_114[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_115[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_bite"},
};
static const qa_q1_monster_operation mg3_rocket_ops_116[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_117[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_118[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_119[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_120[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_121[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_jump"},
};
static const qa_q1_monster_operation mg3_rocket_ops_137[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg3_rocket_ops_138[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg3_rocket_ops_139[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation mg3_rocket_ops_140[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_142[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg3_rocket_ops_144[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10},
};
static const qa_q1_monster_operation mg3_rocket_ops_171[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation mg3_rocket_ops_180[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5},
};
static const qa_q1_monster_operation mg3_rocket_ops_181[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation mg3_rocket_ops_182[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 13},
};
static const qa_q1_monster_operation mg3_rocket_ops_183[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3},
};
static const qa_q1_monster_operation mg3_rocket_ops_184[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation mg3_rocket_ops_200[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg3_rocket_ops_201[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation mg3_rocket_ops_202[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4},
  {.kind = QA_Q1_OP_ACTION, .name = "sham_smash12"},
};
static const qa_q1_monster_operation mg3_rocket_ops_203[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_204[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_205[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_206[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation mg3_rocket_ops_207[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 9},
};
static const qa_q1_monster_operation mg3_rocket_ops_208[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation mg3_rocket_ops_209[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation mg3_rocket_ops_210[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_mg3_rocket_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
  {"army_stand1", {0, "army_stand2", mg3_rocket_ops_1, 1}},
  {"army_stand2", {1, "army_stand3", mg3_rocket_ops_2, 1}},
  {"army_stand3", {2, "army_stand4", mg3_rocket_ops_3, 1}},
  {"army_stand4", {3, "army_stand5", mg3_rocket_ops_4, 1}},
  {"army_stand5", {4, "army_stand6", mg3_rocket_ops_5, 1}},
  {"army_stand6", {5, "army_stand7", mg3_rocket_ops_6, 1}},
  {"army_stand7", {6, "army_stand8", mg3_rocket_ops_7, 1}},
  {"army_stand8", {7, "army_stand1", mg3_rocket_ops_8, 1}},
  {"army_walk1", {90, "army_walk2", mg3_rocket_ops_9, 2}},
  {"army_walk2", {91, "army_walk3", mg3_rocket_ops_10, 1}},
  {"army_walk3", {92, "army_walk4", mg3_rocket_ops_11, 1}},
  {"army_walk4", {93, "army_walk5", mg3_rocket_ops_12, 1}},
  {"army_walk5", {94, "army_walk6", mg3_rocket_ops_13, 1}},
  {"army_walk6", {95, "army_walk7", mg3_rocket_ops_14, 1}},
  {"army_walk7", {96, "army_walk8", mg3_rocket_ops_15, 1}},
  {"army_walk8", {97, "army_walk9", mg3_rocket_ops_16, 1}},
  {"army_walk9", {98, "army_walk10", mg3_rocket_ops_17, 1}},
  {"army_walk10", {99, "army_walk11", mg3_rocket_ops_18, 1}},
  {"army_walk11", {100, "army_walk12", mg3_rocket_ops_19, 1}},
  {"army_walk12", {101, "army_walk13", mg3_rocket_ops_20, 1}},
  {"army_walk13", {102, "army_walk14", mg3_rocket_ops_21, 1}},
  {"army_walk14", {103, "army_walk15", mg3_rocket_ops_22, 1}},
  {"army_walk15", {104, "army_walk16", mg3_rocket_ops_23, 1}},
  {"army_walk16", {105, "army_walk17", mg3_rocket_ops_24, 1}},
  {"army_walk17", {106, "army_walk18", mg3_rocket_ops_25, 1}},
  {"army_walk18", {107, "army_walk19", mg3_rocket_ops_26, 1}},
  {"army_walk19", {108, "army_walk20", mg3_rocket_ops_27, 1}},
  {"army_walk20", {109, "army_walk21", mg3_rocket_ops_28, 1}},
  {"army_walk21", {110, "army_walk22", mg3_rocket_ops_29, 1}},
  {"army_walk22", {111, "army_walk23", mg3_rocket_ops_30, 1}},
  {"army_walk23", {112, "army_walk24", mg3_rocket_ops_31, 1}},
  {"army_walk24", {113, "army_walk1", mg3_rocket_ops_32, 1}},
  {"army_run1", {73, "army_run2", mg3_rocket_ops_33, 2}},
  {"army_run2", {74, "army_run3", mg3_rocket_ops_34, 1}},
  {"army_run3", {75, "army_run4", mg3_rocket_ops_35, 1}},
  {"army_run4", {76, "army_run5", mg3_rocket_ops_36, 1}},
  {"army_run5", {77, "army_run6", mg3_rocket_ops_37, 1}},
  {"army_run6", {78, "army_run7", mg3_rocket_ops_38, 1}},
  {"army_run7", {79, "army_run8", mg3_rocket_ops_39, 1}},
  {"army_run8", {80, "army_run1", mg3_rocket_ops_40, 1}},
  {"army_atk1", {81, "army_atk2", mg3_rocket_ops_41, 1}},
  {"army_atk2", {82, "army_atk3", mg3_rocket_ops_42, 1}},
  {"army_atk3", {83, "army_atk4", mg3_rocket_ops_43, 1}},
  {"army_atk4", {84, "army_atk5", mg3_rocket_ops_44, 1}},
  {"army_atk5", {85, "army_atk6", mg3_rocket_ops_45, 2}},
  {"army_atk6", {86, "army_atk7", mg3_rocket_ops_46, 1}},
  {"army_atk7", {87, "army_atk8", mg3_rocket_ops_47, 2}},
  {"army_atk8", {88, "army_atk9", mg3_rocket_ops_48, 1}},
  {"army_atk9", {89, "army_run1", mg3_rocket_ops_49, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", mg3_rocket_ops_55, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", mg3_rocket_ops_57, 1}},
  {"army_painb3", {48, "army_painb4", mg3_rocket_ops_58, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", mg3_rocket_ops_67, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", mg3_rocket_ops_71, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", mg3_rocket_ops_74, 1}},
  {"army_painc6", {65, "army_painc7", mg3_rocket_ops_75, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", mg3_rocket_ops_77, 1}},
  {"army_painc9", {68, "army_painc10", mg3_rocket_ops_78, 1}},
  {"army_painc10", {69, "army_painc11", mg3_rocket_ops_79, 1}},
  {"army_painc11", {70, "army_painc12", mg3_rocket_ops_80, 1}},
  {"army_painc12", {71, "army_painc13", mg3_rocket_ops_81, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", mg3_rocket_ops_83, 1}},
  {"dog_stand2", {70, "dog_stand3", mg3_rocket_ops_84, 1}},
  {"dog_stand3", {71, "dog_stand4", mg3_rocket_ops_85, 1}},
  {"dog_stand4", {72, "dog_stand5", mg3_rocket_ops_86, 1}},
  {"dog_stand5", {73, "dog_stand6", mg3_rocket_ops_87, 1}},
  {"dog_stand6", {74, "dog_stand7", mg3_rocket_ops_88, 1}},
  {"dog_stand7", {75, "dog_stand8", mg3_rocket_ops_89, 1}},
  {"dog_stand8", {76, "dog_stand9", mg3_rocket_ops_90, 1}},
  {"dog_stand9", {77, "dog_stand1", mg3_rocket_ops_91, 1}},
  {"dog_walk1", {78, "dog_walk2", mg3_rocket_ops_92, 2}},
  {"dog_walk2", {79, "dog_walk3", mg3_rocket_ops_93, 1}},
  {"dog_walk3", {80, "dog_walk4", mg3_rocket_ops_94, 1}},
  {"dog_walk4", {81, "dog_walk5", mg3_rocket_ops_95, 1}},
  {"dog_walk5", {82, "dog_walk6", mg3_rocket_ops_96, 1}},
  {"dog_walk6", {83, "dog_walk7", mg3_rocket_ops_97, 1}},
  {"dog_walk7", {84, "dog_walk8", mg3_rocket_ops_98, 1}},
  {"dog_walk8", {85, "dog_walk1", mg3_rocket_ops_99, 1}},
  {"dog_run1", {48, "dog_run2", mg3_rocket_ops_100, 2}},
  {"dog_run2", {49, "dog_run3", mg3_rocket_ops_101, 1}},
  {"dog_run3", {50, "dog_run4", mg3_rocket_ops_102, 1}},
  {"dog_run4", {51, "dog_run5", mg3_rocket_ops_103, 1}},
  {"dog_run5", {52, "dog_run6", mg3_rocket_ops_104, 1}},
  {"dog_run6", {53, "dog_run7", mg3_rocket_ops_105, 1}},
  {"dog_run7", {54, "dog_run8", mg3_rocket_ops_106, 1}},
  {"dog_run8", {55, "dog_run9", mg3_rocket_ops_107, 1}},
  {"dog_run9", {56, "dog_run10", mg3_rocket_ops_108, 1}},
  {"dog_run10", {57, "dog_run11", mg3_rocket_ops_109, 1}},
  {"dog_run11", {58, "dog_run12", mg3_rocket_ops_110, 1}},
  {"dog_run12", {59, "dog_run1", mg3_rocket_ops_111, 1}},
  {"dog_atta1", {0, "dog_atta2", mg3_rocket_ops_112, 1}},
  {"dog_atta2", {1, "dog_atta3", mg3_rocket_ops_113, 1}},
  {"dog_atta3", {2, "dog_atta4", mg3_rocket_ops_114, 1}},
  {"dog_atta4", {3, "dog_atta5", mg3_rocket_ops_115, 2}},
  {"dog_atta5", {4, "dog_atta6", mg3_rocket_ops_116, 1}},
  {"dog_atta6", {5, "dog_atta7", mg3_rocket_ops_117, 1}},
  {"dog_atta7", {6, "dog_atta8", mg3_rocket_ops_118, 1}},
  {"dog_atta8", {7, "dog_run1", mg3_rocket_ops_119, 1}},
  {"dog_leap1", {60, "dog_leap2", mg3_rocket_ops_120, 1}},
  {"dog_leap2", {61, "dog_leap3", mg3_rocket_ops_121, 2}},
  {"dog_leap3", {62, "dog_leap4", NULL, 0}},
  {"dog_leap4", {63, "dog_leap5", NULL, 0}},
  {"dog_leap5", {64, "dog_leap6", NULL, 0}},
  {"dog_leap6", {65, "dog_leap7", NULL, 0}},
  {"dog_leap7", {66, "dog_leap8", NULL, 0}},
  {"dog_leap8", {67, "dog_leap9", NULL, 0}},
  {"dog_leap9", {68, "dog_leap9", NULL, 0}},
  {"dog_pain1", {26, "dog_pain2", NULL, 0}},
  {"dog_pain2", {27, "dog_pain3", NULL, 0}},
  {"dog_pain3", {28, "dog_pain4", NULL, 0}},
  {"dog_pain4", {29, "dog_pain5", NULL, 0}},
  {"dog_pain5", {30, "dog_pain6", NULL, 0}},
  {"dog_pain6", {31, "dog_run1", NULL, 0}},
  {"dog_painb1", {32, "dog_painb2", NULL, 0}},
  {"dog_painb2", {33, "dog_painb3", NULL, 0}},
  {"dog_painb3", {34, "dog_painb4", mg3_rocket_ops_137, 1}},
  {"dog_painb4", {35, "dog_painb5", mg3_rocket_ops_138, 1}},
  {"dog_painb5", {36, "dog_painb6", mg3_rocket_ops_139, 1}},
  {"dog_painb6", {37, "dog_painb7", mg3_rocket_ops_140, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", mg3_rocket_ops_142, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", mg3_rocket_ops_144, 1}},
  {"dog_painb11", {42, "dog_painb12", NULL, 0}},
  {"dog_painb12", {43, "dog_painb13", NULL, 0}},
  {"dog_painb13", {44, "dog_painb14", NULL, 0}},
  {"dog_painb14", {45, "dog_painb15", NULL, 0}},
  {"dog_painb15", {46, "dog_painb16", NULL, 0}},
  {"dog_painb16", {47, "dog_run1", NULL, 0}},
  {"dog_die1", {8, "dog_die2", NULL, 0}},
  {"dog_die2", {9, "dog_die3", NULL, 0}},
  {"dog_die3", {10, "dog_die4", NULL, 0}},
  {"dog_die4", {11, "dog_die5", NULL, 0}},
  {"dog_die5", {12, "dog_die6", NULL, 0}},
  {"dog_die6", {13, "dog_die7", NULL, 0}},
  {"dog_die7", {14, "dog_die8", NULL, 0}},
  {"dog_die8", {15, "dog_die9", NULL, 0}},
  {"dog_die9", {16, "dog_die9", NULL, 0}},
  {"dog_dieb1", {17, "dog_dieb2", NULL, 0}},
  {"dog_dieb2", {18, "dog_dieb3", NULL, 0}},
  {"dog_dieb3", {19, "dog_dieb4", NULL, 0}},
  {"dog_dieb4", {20, "dog_dieb5", NULL, 0}},
  {"dog_dieb5", {21, "dog_dieb6", NULL, 0}},
  {"dog_dieb6", {22, "dog_dieb7", NULL, 0}},
  {"dog_dieb7", {23, "dog_dieb8", NULL, 0}},
  {"dog_dieb8", {24, "dog_dieb9", NULL, 0}},
  {"dog_dieb9", {25, "dog_dieb9", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", mg3_rocket_ops_171, 1}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", mg3_rocket_ops_180, 1}},
  {"army_cdie3", {20, "army_cdie4", mg3_rocket_ops_181, 1}},
  {"army_cdie4", {21, "army_cdie5", mg3_rocket_ops_182, 1}},
  {"army_cdie5", {22, "army_cdie6", mg3_rocket_ops_183, 1}},
  {"army_cdie6", {23, "army_cdie7", mg3_rocket_ops_184, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"boss_shockz1", {80, "boss_shockz2", NULL, 0}},
  {"boss_shockz2", {81, "boss_shockz3", NULL, 0}},
  {"boss_shockz3", {82, "boss_shockz4", NULL, 0}},
  {"boss_shockz4", {83, "boss_shockz5", NULL, 0}},
  {"boss_shockz5", {84, "boss_shockz6", NULL, 0}},
  {"boss_shockz6", {85, "boss_shockz7", NULL, 0}},
  {"boss_shockz7", {86, "boss_shockz8", NULL, 0}},
  {"boss_shockz8", {87, "boss_shockz9", NULL, 0}},
  {"boss_shockz9", {88, "boss_shockz10", NULL, 0}},
  {"boss_shockz10", {89, "boss_missile1", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", mg3_rocket_ops_200, 1}},
  {"boss_shockb10", {93, "boss_missile1", mg3_rocket_ops_201, 1}},
  {"sham_smash12", {46, "sham_run1", mg3_rocket_ops_202, 2}},
  {"ogre_nail1", {61, "ogre_nail2", mg3_rocket_ops_203, 1}},
  {"ogre_nail5", {64, "ogre_nail6", mg3_rocket_ops_204, 1}},
  {"ogre_stand5", {4, "ogre_stand6", mg3_rocket_ops_205, 1}},
  {"ogre_walk3", {11, "ogre_walk4", mg3_rocket_ops_206, 1}},
  {"ogre_run1", {25, "ogre_run2", mg3_rocket_ops_207, 1}},
  {"ogre_nail4", {63, "ogre_nail5", mg3_rocket_ops_208, 1}},
  {"ogre_die3", {114, "ogre_die4", mg3_rocket_ops_209, 1}},
  {"ogre_bdie3", {128, "ogre_bdie4", mg3_rocket_ops_210, 1}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_mg3_rocket_frames = {entries_qa_q1am_ordinary_mg3_rocket_frames, sizeof entries_qa_q1am_ordinary_mg3_rocket_frames / sizeof entries_qa_q1am_ordinary_mg3_rocket_frames[0]};

static const qa_q1_monster_operation other_ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_9[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation other_ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation other_ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0},
};
static const qa_q1_monster_operation other_ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_ops_29[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_ops_33[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11},
};
static const qa_q1_monster_operation other_ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation other_ops_35[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation other_ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation other_ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation other_ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation other_ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation other_ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation other_ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_44[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_fire"},
};
static const qa_q1_monster_operation other_ops_46[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_47[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_refire"},
};
static const qa_q1_monster_operation other_ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_55[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation other_ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13},
};
static const qa_q1_monster_operation other_ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9},
};
static const qa_q1_monster_operation other_ops_67[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation other_ops_71[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation other_ops_74[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation other_ops_75[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation other_ops_77[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation other_ops_78[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4},
};
static const qa_q1_monster_operation other_ops_79[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3},
};
static const qa_q1_monster_operation other_ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6},
};
static const qa_q1_monster_operation other_ops_81[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8},
};
static const qa_q1_monster_operation other_ops_83[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_84[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_85[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_86[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_87[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_88[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_89[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_90[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_91[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_ops_92[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_93[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_94[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_95[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_96[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_97[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_98[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_99[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_ops_100[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation other_ops_101[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_ops_102[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_ops_103[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation other_ops_104[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation other_ops_105[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_ops_106[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation other_ops_107[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_ops_108[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_ops_109[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation other_ops_110[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation other_ops_111[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_ops_112[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_113[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_114[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_115[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_bite"},
};
static const qa_q1_monster_operation other_ops_116[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_117[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_118[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_119[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_ops_120[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_ops_121[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_jump"},
};
static const qa_q1_monster_operation other_ops_137[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation other_ops_138[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation other_ops_139[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation other_ops_140[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation other_ops_142[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation other_ops_144[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10},
};
static const qa_q1_monster_operation other_ops_171[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation other_ops_180[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5},
};
static const qa_q1_monster_operation other_ops_181[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation other_ops_182[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 13},
};
static const qa_q1_monster_operation other_ops_183[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3},
};
static const qa_q1_monster_operation other_ops_184[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation other_ops_200[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation other_ops_201[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_other_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
  {"army_stand1", {0, "army_stand2", other_ops_1, 1}},
  {"army_stand2", {1, "army_stand3", other_ops_2, 1}},
  {"army_stand3", {2, "army_stand4", other_ops_3, 1}},
  {"army_stand4", {3, "army_stand5", other_ops_4, 1}},
  {"army_stand5", {4, "army_stand6", other_ops_5, 1}},
  {"army_stand6", {5, "army_stand7", other_ops_6, 1}},
  {"army_stand7", {6, "army_stand8", other_ops_7, 1}},
  {"army_stand8", {7, "army_stand1", other_ops_8, 1}},
  {"army_walk1", {90, "army_walk2", other_ops_9, 2}},
  {"army_walk2", {91, "army_walk3", other_ops_10, 1}},
  {"army_walk3", {92, "army_walk4", other_ops_11, 1}},
  {"army_walk4", {93, "army_walk5", other_ops_12, 1}},
  {"army_walk5", {94, "army_walk6", other_ops_13, 1}},
  {"army_walk6", {95, "army_walk7", other_ops_14, 1}},
  {"army_walk7", {96, "army_walk8", other_ops_15, 1}},
  {"army_walk8", {97, "army_walk9", other_ops_16, 1}},
  {"army_walk9", {98, "army_walk10", other_ops_17, 1}},
  {"army_walk10", {99, "army_walk11", other_ops_18, 1}},
  {"army_walk11", {100, "army_walk12", other_ops_19, 1}},
  {"army_walk12", {101, "army_walk13", other_ops_20, 1}},
  {"army_walk13", {102, "army_walk14", other_ops_21, 1}},
  {"army_walk14", {103, "army_walk15", other_ops_22, 1}},
  {"army_walk15", {104, "army_walk16", other_ops_23, 1}},
  {"army_walk16", {105, "army_walk17", other_ops_24, 1}},
  {"army_walk17", {106, "army_walk18", other_ops_25, 1}},
  {"army_walk18", {107, "army_walk19", other_ops_26, 1}},
  {"army_walk19", {108, "army_walk20", other_ops_27, 1}},
  {"army_walk20", {109, "army_walk21", other_ops_28, 1}},
  {"army_walk21", {110, "army_walk22", other_ops_29, 1}},
  {"army_walk22", {111, "army_walk23", other_ops_30, 1}},
  {"army_walk23", {112, "army_walk24", other_ops_31, 1}},
  {"army_walk24", {113, "army_walk1", other_ops_32, 1}},
  {"army_run1", {73, "army_run2", other_ops_33, 2}},
  {"army_run2", {74, "army_run3", other_ops_34, 1}},
  {"army_run3", {75, "army_run4", other_ops_35, 1}},
  {"army_run4", {76, "army_run5", other_ops_36, 1}},
  {"army_run5", {77, "army_run6", other_ops_37, 1}},
  {"army_run6", {78, "army_run7", other_ops_38, 1}},
  {"army_run7", {79, "army_run8", other_ops_39, 1}},
  {"army_run8", {80, "army_run1", other_ops_40, 1}},
  {"army_atk1", {81, "army_atk2", other_ops_41, 1}},
  {"army_atk2", {82, "army_atk3", other_ops_42, 1}},
  {"army_atk3", {83, "army_atk4", other_ops_43, 1}},
  {"army_atk4", {84, "army_atk5", other_ops_44, 1}},
  {"army_atk5", {85, "army_atk6", other_ops_45, 2}},
  {"army_atk6", {86, "army_atk7", other_ops_46, 1}},
  {"army_atk7", {87, "army_atk8", other_ops_47, 2}},
  {"army_atk8", {88, "army_atk9", other_ops_48, 1}},
  {"army_atk9", {89, "army_run1", other_ops_49, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", other_ops_55, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", other_ops_57, 1}},
  {"army_painb3", {48, "army_painb4", other_ops_58, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", other_ops_67, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", other_ops_71, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", other_ops_74, 1}},
  {"army_painc6", {65, "army_painc7", other_ops_75, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", other_ops_77, 1}},
  {"army_painc9", {68, "army_painc10", other_ops_78, 1}},
  {"army_painc10", {69, "army_painc11", other_ops_79, 1}},
  {"army_painc11", {70, "army_painc12", other_ops_80, 1}},
  {"army_painc12", {71, "army_painc13", other_ops_81, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", other_ops_83, 1}},
  {"dog_stand2", {70, "dog_stand3", other_ops_84, 1}},
  {"dog_stand3", {71, "dog_stand4", other_ops_85, 1}},
  {"dog_stand4", {72, "dog_stand5", other_ops_86, 1}},
  {"dog_stand5", {73, "dog_stand6", other_ops_87, 1}},
  {"dog_stand6", {74, "dog_stand7", other_ops_88, 1}},
  {"dog_stand7", {75, "dog_stand8", other_ops_89, 1}},
  {"dog_stand8", {76, "dog_stand9", other_ops_90, 1}},
  {"dog_stand9", {77, "dog_stand1", other_ops_91, 1}},
  {"dog_walk1", {78, "dog_walk2", other_ops_92, 2}},
  {"dog_walk2", {79, "dog_walk3", other_ops_93, 1}},
  {"dog_walk3", {80, "dog_walk4", other_ops_94, 1}},
  {"dog_walk4", {81, "dog_walk5", other_ops_95, 1}},
  {"dog_walk5", {82, "dog_walk6", other_ops_96, 1}},
  {"dog_walk6", {83, "dog_walk7", other_ops_97, 1}},
  {"dog_walk7", {84, "dog_walk8", other_ops_98, 1}},
  {"dog_walk8", {85, "dog_walk1", other_ops_99, 1}},
  {"dog_run1", {48, "dog_run2", other_ops_100, 2}},
  {"dog_run2", {49, "dog_run3", other_ops_101, 1}},
  {"dog_run3", {50, "dog_run4", other_ops_102, 1}},
  {"dog_run4", {51, "dog_run5", other_ops_103, 1}},
  {"dog_run5", {52, "dog_run6", other_ops_104, 1}},
  {"dog_run6", {53, "dog_run7", other_ops_105, 1}},
  {"dog_run7", {54, "dog_run8", other_ops_106, 1}},
  {"dog_run8", {55, "dog_run9", other_ops_107, 1}},
  {"dog_run9", {56, "dog_run10", other_ops_108, 1}},
  {"dog_run10", {57, "dog_run11", other_ops_109, 1}},
  {"dog_run11", {58, "dog_run12", other_ops_110, 1}},
  {"dog_run12", {59, "dog_run1", other_ops_111, 1}},
  {"dog_atta1", {0, "dog_atta2", other_ops_112, 1}},
  {"dog_atta2", {1, "dog_atta3", other_ops_113, 1}},
  {"dog_atta3", {2, "dog_atta4", other_ops_114, 1}},
  {"dog_atta4", {3, "dog_atta5", other_ops_115, 2}},
  {"dog_atta5", {4, "dog_atta6", other_ops_116, 1}},
  {"dog_atta6", {5, "dog_atta7", other_ops_117, 1}},
  {"dog_atta7", {6, "dog_atta8", other_ops_118, 1}},
  {"dog_atta8", {7, "dog_run1", other_ops_119, 1}},
  {"dog_leap1", {60, "dog_leap2", other_ops_120, 1}},
  {"dog_leap2", {61, "dog_leap3", other_ops_121, 2}},
  {"dog_leap3", {62, "dog_leap4", NULL, 0}},
  {"dog_leap4", {63, "dog_leap5", NULL, 0}},
  {"dog_leap5", {64, "dog_leap6", NULL, 0}},
  {"dog_leap6", {65, "dog_leap7", NULL, 0}},
  {"dog_leap7", {66, "dog_leap8", NULL, 0}},
  {"dog_leap8", {67, "dog_leap9", NULL, 0}},
  {"dog_leap9", {68, "dog_leap9", NULL, 0}},
  {"dog_pain1", {26, "dog_pain2", NULL, 0}},
  {"dog_pain2", {27, "dog_pain3", NULL, 0}},
  {"dog_pain3", {28, "dog_pain4", NULL, 0}},
  {"dog_pain4", {29, "dog_pain5", NULL, 0}},
  {"dog_pain5", {30, "dog_pain6", NULL, 0}},
  {"dog_pain6", {31, "dog_run1", NULL, 0}},
  {"dog_painb1", {32, "dog_painb2", NULL, 0}},
  {"dog_painb2", {33, "dog_painb3", NULL, 0}},
  {"dog_painb3", {34, "dog_painb4", other_ops_137, 1}},
  {"dog_painb4", {35, "dog_painb5", other_ops_138, 1}},
  {"dog_painb5", {36, "dog_painb6", other_ops_139, 1}},
  {"dog_painb6", {37, "dog_painb7", other_ops_140, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", other_ops_142, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", other_ops_144, 1}},
  {"dog_painb11", {42, "dog_painb12", NULL, 0}},
  {"dog_painb12", {43, "dog_painb13", NULL, 0}},
  {"dog_painb13", {44, "dog_painb14", NULL, 0}},
  {"dog_painb14", {45, "dog_painb15", NULL, 0}},
  {"dog_painb15", {46, "dog_painb16", NULL, 0}},
  {"dog_painb16", {47, "dog_run1", NULL, 0}},
  {"dog_die1", {8, "dog_die2", NULL, 0}},
  {"dog_die2", {9, "dog_die3", NULL, 0}},
  {"dog_die3", {10, "dog_die4", NULL, 0}},
  {"dog_die4", {11, "dog_die5", NULL, 0}},
  {"dog_die5", {12, "dog_die6", NULL, 0}},
  {"dog_die6", {13, "dog_die7", NULL, 0}},
  {"dog_die7", {14, "dog_die8", NULL, 0}},
  {"dog_die8", {15, "dog_die9", NULL, 0}},
  {"dog_die9", {16, "dog_die9", NULL, 0}},
  {"dog_dieb1", {17, "dog_dieb2", NULL, 0}},
  {"dog_dieb2", {18, "dog_dieb3", NULL, 0}},
  {"dog_dieb3", {19, "dog_dieb4", NULL, 0}},
  {"dog_dieb4", {20, "dog_dieb5", NULL, 0}},
  {"dog_dieb5", {21, "dog_dieb6", NULL, 0}},
  {"dog_dieb6", {22, "dog_dieb7", NULL, 0}},
  {"dog_dieb7", {23, "dog_dieb8", NULL, 0}},
  {"dog_dieb8", {24, "dog_dieb9", NULL, 0}},
  {"dog_dieb9", {25, "dog_dieb9", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", other_ops_171, 1}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", other_ops_180, 1}},
  {"army_cdie3", {20, "army_cdie4", other_ops_181, 1}},
  {"army_cdie4", {21, "army_cdie5", other_ops_182, 1}},
  {"army_cdie5", {22, "army_cdie6", other_ops_183, 1}},
  {"army_cdie6", {23, "army_cdie7", other_ops_184, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"boss_shockz1", {80, "boss_shockz2", NULL, 0}},
  {"boss_shockz2", {81, "boss_shockz3", NULL, 0}},
  {"boss_shockz3", {82, "boss_shockz4", NULL, 0}},
  {"boss_shockz4", {83, "boss_shockz5", NULL, 0}},
  {"boss_shockz5", {84, "boss_shockz6", NULL, 0}},
  {"boss_shockz6", {85, "boss_shockz7", NULL, 0}},
  {"boss_shockz7", {86, "boss_shockz8", NULL, 0}},
  {"boss_shockz8", {87, "boss_shockz9", NULL, 0}},
  {"boss_shockz9", {88, "boss_shockz10", NULL, 0}},
  {"boss_shockz10", {89, "boss_missile1", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", other_ops_200, 1}},
  {"boss_shockb10", {93, "boss_missile1", other_ops_201, 1}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_other_frames = {entries_qa_q1am_ordinary_other_frames, sizeof entries_qa_q1am_ordinary_other_frames / sizeof entries_qa_q1am_ordinary_other_frames[0]};

static const qa_q1_monster_operation other_rocket_ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_9[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation other_rocket_ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4},
};
static const qa_q1_monster_operation other_rocket_ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_29[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_33[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11},
};
static const qa_q1_monster_operation other_rocket_ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation other_rocket_ops_35[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15},
};
static const qa_q1_monster_operation other_rocket_ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_44[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_fire"},
};
static const qa_q1_monster_operation other_rocket_ops_46[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_47[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "army_refire"},
};
static const qa_q1_monster_operation other_rocket_ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_55[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13},
};
static const qa_q1_monster_operation other_rocket_ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9},
};
static const qa_q1_monster_operation other_rocket_ops_67[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_71[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_74[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_75[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_77[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1},
};
static const qa_q1_monster_operation other_rocket_ops_78[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4},
};
static const qa_q1_monster_operation other_rocket_ops_79[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6},
};
static const qa_q1_monster_operation other_rocket_ops_81[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_83[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_84[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_85[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_86[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_87[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_88[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_89[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_90[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_91[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_92[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_93[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_94[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_95[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_96[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_97[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_98[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_99[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation other_rocket_ops_100[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation other_rocket_ops_101[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_rocket_ops_102[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_rocket_ops_103[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation other_rocket_ops_104[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation other_rocket_ops_105[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_rocket_ops_106[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16},
};
static const qa_q1_monster_operation other_rocket_ops_107[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_rocket_ops_108[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_rocket_ops_109[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation other_rocket_ops_110[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64},
};
static const qa_q1_monster_operation other_rocket_ops_111[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32},
};
static const qa_q1_monster_operation other_rocket_ops_112[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_113[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_114[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_115[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_bite"},
};
static const qa_q1_monster_operation other_rocket_ops_116[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_117[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_118[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_119[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_120[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_121[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
  {.kind = QA_Q1_OP_ACTION, .name = "dog_jump"},
};
static const qa_q1_monster_operation other_rocket_ops_137[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation other_rocket_ops_138[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation other_rocket_ops_139[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12},
};
static const qa_q1_monster_operation other_rocket_ops_140[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_142[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation other_rocket_ops_144[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10},
};
static const qa_q1_monster_operation other_rocket_ops_171[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation other_rocket_ops_180[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5},
};
static const qa_q1_monster_operation other_rocket_ops_181[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation other_rocket_ops_182[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 13},
};
static const qa_q1_monster_operation other_rocket_ops_183[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3},
};
static const qa_q1_monster_operation other_rocket_ops_184[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4},
};
static const qa_q1_monster_operation other_rocket_ops_200[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation other_rocket_ops_201[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "boss_freeze"},
};
static const qa_q1_monster_operation other_rocket_ops_202[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_203[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2},
};
static const qa_q1_monster_operation other_rocket_ops_204[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 9},
};
static const qa_q1_monster_operation other_rocket_ops_205[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_206[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_207[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation other_rocket_ops_208[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_operation other_rocket_ops_209[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
};
static const qa_q1_monster_frame_entry entries_qa_q1am_ordinary_other_rocket_frames[] = {
  {"zombie_hang1", {162, "zombie_hang1", NULL, 0}},
  {"army_stand1", {0, "army_stand2", other_rocket_ops_1, 1}},
  {"army_stand2", {1, "army_stand3", other_rocket_ops_2, 1}},
  {"army_stand3", {2, "army_stand4", other_rocket_ops_3, 1}},
  {"army_stand4", {3, "army_stand5", other_rocket_ops_4, 1}},
  {"army_stand5", {4, "army_stand6", other_rocket_ops_5, 1}},
  {"army_stand6", {5, "army_stand7", other_rocket_ops_6, 1}},
  {"army_stand7", {6, "army_stand8", other_rocket_ops_7, 1}},
  {"army_stand8", {7, "army_stand1", other_rocket_ops_8, 1}},
  {"army_walk1", {90, "army_walk2", other_rocket_ops_9, 2}},
  {"army_walk2", {91, "army_walk3", other_rocket_ops_10, 1}},
  {"army_walk3", {92, "army_walk4", other_rocket_ops_11, 1}},
  {"army_walk4", {93, "army_walk5", other_rocket_ops_12, 1}},
  {"army_walk5", {94, "army_walk6", other_rocket_ops_13, 1}},
  {"army_walk6", {95, "army_walk7", other_rocket_ops_14, 1}},
  {"army_walk7", {96, "army_walk8", other_rocket_ops_15, 1}},
  {"army_walk8", {97, "army_walk9", other_rocket_ops_16, 1}},
  {"army_walk9", {98, "army_walk10", other_rocket_ops_17, 1}},
  {"army_walk10", {99, "army_walk11", other_rocket_ops_18, 1}},
  {"army_walk11", {100, "army_walk12", other_rocket_ops_19, 1}},
  {"army_walk12", {101, "army_walk13", other_rocket_ops_20, 1}},
  {"army_walk13", {102, "army_walk14", other_rocket_ops_21, 1}},
  {"army_walk14", {103, "army_walk15", other_rocket_ops_22, 1}},
  {"army_walk15", {104, "army_walk16", other_rocket_ops_23, 1}},
  {"army_walk16", {105, "army_walk17", other_rocket_ops_24, 1}},
  {"army_walk17", {106, "army_walk18", other_rocket_ops_25, 1}},
  {"army_walk18", {107, "army_walk19", other_rocket_ops_26, 1}},
  {"army_walk19", {108, "army_walk20", other_rocket_ops_27, 1}},
  {"army_walk20", {109, "army_walk21", other_rocket_ops_28, 1}},
  {"army_walk21", {110, "army_walk22", other_rocket_ops_29, 1}},
  {"army_walk22", {111, "army_walk23", other_rocket_ops_30, 1}},
  {"army_walk23", {112, "army_walk24", other_rocket_ops_31, 1}},
  {"army_walk24", {113, "army_walk1", other_rocket_ops_32, 1}},
  {"army_run1", {73, "army_run2", other_rocket_ops_33, 2}},
  {"army_run2", {74, "army_run3", other_rocket_ops_34, 1}},
  {"army_run3", {75, "army_run4", other_rocket_ops_35, 1}},
  {"army_run4", {76, "army_run5", other_rocket_ops_36, 1}},
  {"army_run5", {77, "army_run6", other_rocket_ops_37, 1}},
  {"army_run6", {78, "army_run7", other_rocket_ops_38, 1}},
  {"army_run7", {79, "army_run8", other_rocket_ops_39, 1}},
  {"army_run8", {80, "army_run1", other_rocket_ops_40, 1}},
  {"army_atk1", {81, "army_atk2", other_rocket_ops_41, 1}},
  {"army_atk2", {82, "army_atk3", other_rocket_ops_42, 1}},
  {"army_atk3", {83, "army_atk4", other_rocket_ops_43, 1}},
  {"army_atk4", {84, "army_atk5", other_rocket_ops_44, 1}},
  {"army_atk5", {85, "army_atk6", other_rocket_ops_45, 2}},
  {"army_atk6", {86, "army_atk7", other_rocket_ops_46, 1}},
  {"army_atk7", {87, "army_atk8", other_rocket_ops_47, 2}},
  {"army_atk8", {88, "army_atk9", other_rocket_ops_48, 1}},
  {"army_atk9", {89, "army_run1", other_rocket_ops_49, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", other_rocket_ops_55, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", other_rocket_ops_57, 1}},
  {"army_painb3", {48, "army_painb4", other_rocket_ops_58, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", other_rocket_ops_67, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", other_rocket_ops_71, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", other_rocket_ops_74, 1}},
  {"army_painc6", {65, "army_painc7", other_rocket_ops_75, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", other_rocket_ops_77, 1}},
  {"army_painc9", {68, "army_painc10", other_rocket_ops_78, 1}},
  {"army_painc10", {69, "army_painc11", other_rocket_ops_79, 1}},
  {"army_painc11", {70, "army_painc12", other_rocket_ops_80, 1}},
  {"army_painc12", {71, "army_painc13", other_rocket_ops_81, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", other_rocket_ops_83, 1}},
  {"dog_stand2", {70, "dog_stand3", other_rocket_ops_84, 1}},
  {"dog_stand3", {71, "dog_stand4", other_rocket_ops_85, 1}},
  {"dog_stand4", {72, "dog_stand5", other_rocket_ops_86, 1}},
  {"dog_stand5", {73, "dog_stand6", other_rocket_ops_87, 1}},
  {"dog_stand6", {74, "dog_stand7", other_rocket_ops_88, 1}},
  {"dog_stand7", {75, "dog_stand8", other_rocket_ops_89, 1}},
  {"dog_stand8", {76, "dog_stand9", other_rocket_ops_90, 1}},
  {"dog_stand9", {77, "dog_stand1", other_rocket_ops_91, 1}},
  {"dog_walk1", {78, "dog_walk2", other_rocket_ops_92, 2}},
  {"dog_walk2", {79, "dog_walk3", other_rocket_ops_93, 1}},
  {"dog_walk3", {80, "dog_walk4", other_rocket_ops_94, 1}},
  {"dog_walk4", {81, "dog_walk5", other_rocket_ops_95, 1}},
  {"dog_walk5", {82, "dog_walk6", other_rocket_ops_96, 1}},
  {"dog_walk6", {83, "dog_walk7", other_rocket_ops_97, 1}},
  {"dog_walk7", {84, "dog_walk8", other_rocket_ops_98, 1}},
  {"dog_walk8", {85, "dog_walk1", other_rocket_ops_99, 1}},
  {"dog_run1", {48, "dog_run2", other_rocket_ops_100, 2}},
  {"dog_run2", {49, "dog_run3", other_rocket_ops_101, 1}},
  {"dog_run3", {50, "dog_run4", other_rocket_ops_102, 1}},
  {"dog_run4", {51, "dog_run5", other_rocket_ops_103, 1}},
  {"dog_run5", {52, "dog_run6", other_rocket_ops_104, 1}},
  {"dog_run6", {53, "dog_run7", other_rocket_ops_105, 1}},
  {"dog_run7", {54, "dog_run8", other_rocket_ops_106, 1}},
  {"dog_run8", {55, "dog_run9", other_rocket_ops_107, 1}},
  {"dog_run9", {56, "dog_run10", other_rocket_ops_108, 1}},
  {"dog_run10", {57, "dog_run11", other_rocket_ops_109, 1}},
  {"dog_run11", {58, "dog_run12", other_rocket_ops_110, 1}},
  {"dog_run12", {59, "dog_run1", other_rocket_ops_111, 1}},
  {"dog_atta1", {0, "dog_atta2", other_rocket_ops_112, 1}},
  {"dog_atta2", {1, "dog_atta3", other_rocket_ops_113, 1}},
  {"dog_atta3", {2, "dog_atta4", other_rocket_ops_114, 1}},
  {"dog_atta4", {3, "dog_atta5", other_rocket_ops_115, 2}},
  {"dog_atta5", {4, "dog_atta6", other_rocket_ops_116, 1}},
  {"dog_atta6", {5, "dog_atta7", other_rocket_ops_117, 1}},
  {"dog_atta7", {6, "dog_atta8", other_rocket_ops_118, 1}},
  {"dog_atta8", {7, "dog_run1", other_rocket_ops_119, 1}},
  {"dog_leap1", {60, "dog_leap2", other_rocket_ops_120, 1}},
  {"dog_leap2", {61, "dog_leap3", other_rocket_ops_121, 2}},
  {"dog_leap3", {62, "dog_leap4", NULL, 0}},
  {"dog_leap4", {63, "dog_leap5", NULL, 0}},
  {"dog_leap5", {64, "dog_leap6", NULL, 0}},
  {"dog_leap6", {65, "dog_leap7", NULL, 0}},
  {"dog_leap7", {66, "dog_leap8", NULL, 0}},
  {"dog_leap8", {67, "dog_leap9", NULL, 0}},
  {"dog_leap9", {68, "dog_leap9", NULL, 0}},
  {"dog_pain1", {26, "dog_pain2", NULL, 0}},
  {"dog_pain2", {27, "dog_pain3", NULL, 0}},
  {"dog_pain3", {28, "dog_pain4", NULL, 0}},
  {"dog_pain4", {29, "dog_pain5", NULL, 0}},
  {"dog_pain5", {30, "dog_pain6", NULL, 0}},
  {"dog_pain6", {31, "dog_run1", NULL, 0}},
  {"dog_painb1", {32, "dog_painb2", NULL, 0}},
  {"dog_painb2", {33, "dog_painb3", NULL, 0}},
  {"dog_painb3", {34, "dog_painb4", other_rocket_ops_137, 1}},
  {"dog_painb4", {35, "dog_painb5", other_rocket_ops_138, 1}},
  {"dog_painb5", {36, "dog_painb6", other_rocket_ops_139, 1}},
  {"dog_painb6", {37, "dog_painb7", other_rocket_ops_140, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", other_rocket_ops_142, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", other_rocket_ops_144, 1}},
  {"dog_painb11", {42, "dog_painb12", NULL, 0}},
  {"dog_painb12", {43, "dog_painb13", NULL, 0}},
  {"dog_painb13", {44, "dog_painb14", NULL, 0}},
  {"dog_painb14", {45, "dog_painb15", NULL, 0}},
  {"dog_painb15", {46, "dog_painb16", NULL, 0}},
  {"dog_painb16", {47, "dog_run1", NULL, 0}},
  {"dog_die1", {8, "dog_die2", NULL, 0}},
  {"dog_die2", {9, "dog_die3", NULL, 0}},
  {"dog_die3", {10, "dog_die4", NULL, 0}},
  {"dog_die4", {11, "dog_die5", NULL, 0}},
  {"dog_die5", {12, "dog_die6", NULL, 0}},
  {"dog_die6", {13, "dog_die7", NULL, 0}},
  {"dog_die7", {14, "dog_die8", NULL, 0}},
  {"dog_die8", {15, "dog_die9", NULL, 0}},
  {"dog_die9", {16, "dog_die9", NULL, 0}},
  {"dog_dieb1", {17, "dog_dieb2", NULL, 0}},
  {"dog_dieb2", {18, "dog_dieb3", NULL, 0}},
  {"dog_dieb3", {19, "dog_dieb4", NULL, 0}},
  {"dog_dieb4", {20, "dog_dieb5", NULL, 0}},
  {"dog_dieb5", {21, "dog_dieb6", NULL, 0}},
  {"dog_dieb6", {22, "dog_dieb7", NULL, 0}},
  {"dog_dieb7", {23, "dog_dieb8", NULL, 0}},
  {"dog_dieb8", {24, "dog_dieb9", NULL, 0}},
  {"dog_dieb9", {25, "dog_dieb9", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", other_rocket_ops_171, 1}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", other_rocket_ops_180, 1}},
  {"army_cdie3", {20, "army_cdie4", other_rocket_ops_181, 1}},
  {"army_cdie4", {21, "army_cdie5", other_rocket_ops_182, 1}},
  {"army_cdie5", {22, "army_cdie6", other_rocket_ops_183, 1}},
  {"army_cdie6", {23, "army_cdie7", other_rocket_ops_184, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"boss_shockz1", {80, "boss_shockz2", NULL, 0}},
  {"boss_shockz2", {81, "boss_shockz3", NULL, 0}},
  {"boss_shockz3", {82, "boss_shockz4", NULL, 0}},
  {"boss_shockz4", {83, "boss_shockz5", NULL, 0}},
  {"boss_shockz5", {84, "boss_shockz6", NULL, 0}},
  {"boss_shockz6", {85, "boss_shockz7", NULL, 0}},
  {"boss_shockz7", {86, "boss_shockz8", NULL, 0}},
  {"boss_shockz8", {87, "boss_shockz9", NULL, 0}},
  {"boss_shockz9", {88, "boss_shockz10", NULL, 0}},
  {"boss_shockz10", {89, "boss_missile1", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", other_rocket_ops_200, 1}},
  {"boss_shockb10", {93, "boss_missile1", other_rocket_ops_201, 1}},
  {"ogre_stand5", {4, "ogre_stand6", other_rocket_ops_202, 1}},
  {"ogre_walk3", {11, "ogre_walk4", other_rocket_ops_203, 1}},
  {"ogre_run1", {25, "ogre_run2", other_rocket_ops_204, 1}},
  {"ogre_nail1", {61, "ogre_nail2", other_rocket_ops_205, 1}},
  {"ogre_nail4", {63, "ogre_nail5", other_rocket_ops_206, 1}},
  {"ogre_nail5", {64, "ogre_nail6", other_rocket_ops_207, 1}},
  {"ogre_die3", {114, "ogre_die4", other_rocket_ops_208, 1}},
  {"ogre_bdie3", {128, "ogre_bdie4", other_rocket_ops_209, 1}},
};
const qa_q1_monster_frame_table qa_q1am_ordinary_other_rocket_frames = {entries_qa_q1am_ordinary_other_rocket_frames, sizeof entries_qa_q1am_ordinary_other_rocket_frames / sizeof entries_qa_q1am_ordinary_other_rocket_frames[0]};

