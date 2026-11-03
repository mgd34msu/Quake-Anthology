/* Port of src/content/q1/addons/monsters/heavy/tables/mg3_super_shambler.ts. Generated mechanically from the TS
 * frame table via bun JSON dump; do not hand-edit entries. */
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_super_shambler.h"

static const qa_q1_monster_operation ops_0[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_1[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_2[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_3[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_4[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_5[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_6[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_7[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_8[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_9[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_10[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_11[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_12[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_13[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_14[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_15[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_16[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0},
};
static const qa_q1_monster_operation ops_17[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 10},
};
static const qa_q1_monster_operation ops_18[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 9},
};
static const qa_q1_monster_operation ops_19[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 9},
};
static const qa_q1_monster_operation ops_20[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5},
};
static const qa_q1_monster_operation ops_21[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6},
};
static const qa_q1_monster_operation ops_22[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 12},
};
static const qa_q1_monster_operation ops_23[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8},
};
static const qa_q1_monster_operation ops_24[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3},
};
static const qa_q1_monster_operation ops_25[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 13},
};
static const qa_q1_monster_operation ops_26[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 9},
};
static const qa_q1_monster_operation ops_27[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 7},
};
static const qa_q1_monster_operation ops_28[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 7},
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/sidle.wav", .channel = "voice", .attenuation = 2, .comparison = "greater", .has_chance = true, .chance = 0.8},
};
static const qa_q1_monster_operation ops_29[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "supsham_removechild"},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation ops_30[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 24},
};
static const qa_q1_monster_operation ops_31[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation ops_32[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
};
static const qa_q1_monster_operation ops_33[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 24},
};
static const qa_q1_monster_operation ops_34[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20},
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/sidle.wav", .channel = "voice", .attenuation = 2, .comparison = "greater", .has_chance = true, .chance = 0.8},
};
static const qa_q1_monster_operation ops_35[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/melee1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2},
};
static const qa_q1_monster_operation ops_36[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6},
};
static const qa_q1_monster_operation ops_37[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6},
};
static const qa_q1_monster_operation ops_38[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5},
};
static const qa_q1_monster_operation ops_39[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4},
};
static const qa_q1_monster_operation ops_40[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1},
};
static const qa_q1_monster_operation ops_41[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0},
};
static const qa_q1_monster_operation ops_42[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0},
};
static const qa_q1_monster_operation ops_43[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0},
};
static const qa_q1_monster_operation ops_44[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_smash10"},
};
static const qa_q1_monster_operation ops_45[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5},
};
static const qa_q1_monster_operation ops_46[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_smash12"},
};
static const qa_q1_monster_operation ops_47[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/melee2.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5},
};
static const qa_q1_monster_operation ops_48[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3},
};
static const qa_q1_monster_operation ops_49[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7},
};
static const qa_q1_monster_operation ops_50[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3},
};
static const qa_q1_monster_operation ops_51[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7},
};
static const qa_q1_monster_operation ops_52[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 9},
};
static const qa_q1_monster_operation ops_53[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_swingl7"},
};
static const qa_q1_monster_operation ops_54[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4},
};
static const qa_q1_monster_operation ops_55[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_swingl9"},
};
static const qa_q1_monster_operation ops_56[] = {
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/melee1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1},
};
static const qa_q1_monster_operation ops_57[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8},
};
static const qa_q1_monster_operation ops_58[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14},
};
static const qa_q1_monster_operation ops_59[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7},
};
static const qa_q1_monster_operation ops_60[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3},
};
static const qa_q1_monster_operation ops_61[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6},
};
static const qa_q1_monster_operation ops_62[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_swingr7"},
};
static const qa_q1_monster_operation ops_63[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3},
};
static const qa_q1_monster_operation ops_64[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_swingr9"},
};
static const qa_q1_monster_operation ops_65[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic1"},
};
static const qa_q1_monster_operation ops_66[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation ops_67[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic3"},
};
static const qa_q1_monster_operation ops_68[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic4"},
};
static const qa_q1_monster_operation ops_69[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic5"},
};
static const qa_q1_monster_operation ops_70[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic4b"},
};
static const qa_q1_monster_operation ops_71[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic5"},
};
static const qa_q1_monster_operation ops_72[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic4"},
};
static const qa_q1_monster_operation ops_73[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic5"},
};
static const qa_q1_monster_operation ops_74[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "supsham_removechild"},
  {.kind = QA_Q1_OP_ACTION, .name = "SupCastLightning"},
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/sboom.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
};
static const qa_q1_monster_operation ops_75[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "SupCastLightning"},
};
static const qa_q1_monster_operation ops_76[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "SupCastLightning"},
};
static const qa_q1_monster_operation ops_79[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic1"},
};
static const qa_q1_monster_operation ops_80[] = {
  {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0},
};
static const qa_q1_monster_operation ops_81[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic_b3"},
};
static const qa_q1_monster_operation ops_82[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic4"},
};
static const qa_q1_monster_operation ops_83[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "mg3_super_shambler:supsham_magic5"},
};
static const qa_q1_monster_operation ops_84[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "supsham_removechild"},
  {.kind = QA_Q1_OP_ACTION, .name = "SupCastLightning"},
  {.kind = QA_Q1_OP_SOUND, .path = "shambler/sboom.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false, .chance = 0},
};
static const qa_q1_monster_operation ops_85[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "SupCastLightning"},
};
static const qa_q1_monster_operation ops_86[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "SupCastLightning"},
};
static const qa_q1_monster_operation ops_97[] = {
  {.kind = QA_Q1_OP_SOLID, .solid = "none"},
  {.kind = QA_Q1_OP_ACTION, .name = "cleanup_orbs"},
};
static const qa_q1_monster_frame_entry entries[] = {
  {"supsham_stand1", {0, "supsham_stand2", ops_0, 1}},
  {"supsham_stand2", {1, "supsham_stand3", ops_1, 1}},
  {"supsham_stand3", {2, "supsham_stand4", ops_2, 1}},
  {"supsham_stand4", {3, "supsham_stand5", ops_3, 1}},
  {"supsham_stand5", {4, "supsham_stand6", ops_4, 1}},
  {"supsham_stand6", {5, "supsham_stand7", ops_5, 1}},
  {"supsham_stand7", {6, "supsham_stand8", ops_6, 1}},
  {"supsham_stand8", {7, "supsham_stand9", ops_7, 1}},
  {"supsham_stand9", {8, "supsham_stand10", ops_8, 1}},
  {"supsham_stand10", {9, "supsham_stand11", ops_9, 1}},
  {"supsham_stand11", {10, "supsham_stand12", ops_10, 1}},
  {"supsham_stand12", {11, "supsham_stand13", ops_11, 1}},
  {"supsham_stand13", {12, "supsham_stand14", ops_12, 1}},
  {"supsham_stand14", {13, "supsham_stand15", ops_13, 1}},
  {"supsham_stand15", {14, "supsham_stand16", ops_14, 1}},
  {"supsham_stand16", {15, "supsham_stand17", ops_15, 1}},
  {"supsham_stand17", {16, "supsham_stand1", ops_16, 1}},
  {"supsham_walk1", {17, "supsham_walk2", ops_17, 1}},
  {"supsham_walk2", {18, "supsham_walk3", ops_18, 1}},
  {"supsham_walk3", {19, "supsham_walk4", ops_19, 1}},
  {"supsham_walk4", {20, "supsham_walk5", ops_20, 1}},
  {"supsham_walk5", {21, "supsham_walk6", ops_21, 1}},
  {"supsham_walk6", {22, "supsham_walk7", ops_22, 1}},
  {"supsham_walk7", {23, "supsham_walk8", ops_23, 1}},
  {"supsham_walk8", {24, "supsham_walk9", ops_24, 1}},
  {"supsham_walk9", {25, "supsham_walk10", ops_25, 1}},
  {"supsham_walk10", {26, "supsham_walk11", ops_26, 1}},
  {"supsham_walk11", {27, "supsham_walk12", ops_27, 1}},
  {"supsham_walk12", {28, "supsham_walk1", ops_28, 2}},
  {"supsham_run1", {29, "supsham_run2", ops_29, 2}},
  {"supsham_run2", {30, "supsham_run3", ops_30, 1}},
  {"supsham_run3", {31, "supsham_run4", ops_31, 1}},
  {"supsham_run4", {32, "supsham_run5", ops_32, 1}},
  {"supsham_run5", {33, "supsham_run6", ops_33, 1}},
  {"supsham_run6", {34, "supsham_run1", ops_34, 2}},
  {"supsham_smash1", {35, "supsham_smash2", ops_35, 2}},
  {"supsham_smash2", {36, "supsham_smash3", ops_36, 1}},
  {"supsham_smash3", {37, "supsham_smash4", ops_37, 1}},
  {"supsham_smash4", {38, "supsham_smash5", ops_38, 1}},
  {"supsham_smash5", {39, "supsham_smash6", ops_39, 1}},
  {"supsham_smash6", {40, "supsham_smash7", ops_40, 1}},
  {"supsham_smash7", {41, "supsham_smash8", ops_41, 1}},
  {"supsham_smash8", {42, "supsham_smash9", ops_42, 1}},
  {"supsham_smash9", {43, "supsham_smash10", ops_43, 1}},
  {"supsham_smash10", {44, "supsham_smash11", ops_44, 1}},
  {"supsham_smash11", {45, "supsham_smash12", ops_45, 1}},
  {"supsham_smash12", {46, "supsham_run1", ops_46, 1}},
  {"supsham_swingl1", {56, "supsham_swingl2", ops_47, 2}},
  {"supsham_swingl2", {57, "supsham_swingl3", ops_48, 1}},
  {"supsham_swingl3", {58, "supsham_swingl4", ops_49, 1}},
  {"supsham_swingl4", {59, "supsham_swingl5", ops_50, 1}},
  {"supsham_swingl5", {60, "supsham_swingl6", ops_51, 1}},
  {"supsham_swingl6", {61, "supsham_swingl7", ops_52, 1}},
  {"supsham_swingl7", {62, "supsham_swingl8", ops_53, 1}},
  {"supsham_swingl8", {63, "supsham_swingl9", ops_54, 1}},
  {"supsham_swingl9", {64, "supsham_run1", ops_55, 1}},
  {"supsham_swingr1", {47, "supsham_swingr2", ops_56, 2}},
  {"supsham_swingr2", {48, "supsham_swingr3", ops_57, 1}},
  {"supsham_swingr3", {49, "supsham_swingr4", ops_58, 1}},
  {"supsham_swingr4", {50, "supsham_swingr5", ops_59, 1}},
  {"supsham_swingr5", {51, "supsham_swingr6", ops_60, 1}},
  {"supsham_swingr6", {52, "supsham_swingr7", ops_61, 1}},
  {"supsham_swingr7", {53, "supsham_swingr8", ops_62, 1}},
  {"supsham_swingr8", {54, "supsham_swingr9", ops_63, 1}},
  {"supsham_swingr9", {55, "supsham_run1", ops_64, 1}},
  {"supsham_magic1", {65, "supsham_magic2", ops_65, 1}},
  {"supsham_magic2", {66, "supsham_magic3", ops_66, 1}},
  {"supsham_magic3", {67, "supsham_magic4", ops_67, 1}},
  {"supsham_magic4", {68, "supsham_magic5", ops_68, 1}},
  {"supsham_magic5", {69, "supsham_magic4b", ops_69, 1}},
  {"supsham_magic4b", {68, "supsham_magic5b", ops_70, 1}},
  {"supsham_magic5b", {69, "supsham_magic4c", ops_71, 1}},
  {"supsham_magic4c", {68, "supsham_magic5c", ops_72, 1}},
  {"supsham_magic5c", {69, "supsham_magic6", ops_73, 1}},
  {"supsham_magic6", {70, "supsham_magic9", ops_74, 3}},
  {"supsham_magic9", {73, "supsham_magic10", ops_75, 1}},
  {"supsham_magic10", {74, "supsham_magic11", ops_76, 1}},
  {"supsham_magic11", {75, "supsham_magic12", NULL, 0}},
  {"supsham_magic12", {76, "supsham_run1", NULL, 0}},
  {"supsham_magic_b1", {65, "supsham_magic_b2", ops_79, 1}},
  {"supsham_magic_b2", {66, "supsham_magic_b3", ops_80, 1}},
  {"supsham_magic_b3", {67, "supsham_magic_b4", ops_81, 1}},
  {"supsham_magic_b4", {68, "supsham_magic_b5", ops_82, 1}},
  {"supsham_magic_b5", {69, "supsham_magic_b6", ops_83, 1}},
  {"supsham_magic_b6", {70, "supsham_magic_b9", ops_84, 3}},
  {"supsham_magic_b9", {73, "supsham_magic_b10", ops_85, 1}},
  {"supsham_magic_b10", {74, "supsham_magic_b11", ops_86, 1}},
  {"supsham_magic_b11", {75, "supsham_magic_b12", NULL, 0}},
  {"supsham_magic_b12", {76, "supsham_run1", NULL, 0}},
  {"supsham_pain1", {77, "supsham_pain2", NULL, 0}},
  {"supsham_pain2", {78, "supsham_pain3", NULL, 0}},
  {"supsham_pain3", {79, "supsham_pain4", NULL, 0}},
  {"supsham_pain4", {80, "supsham_pain5", NULL, 0}},
  {"supsham_pain5", {81, "supsham_pain6", NULL, 0}},
  {"supsham_pain6", {82, "supsham_run1", NULL, 0}},
  {"supsham_death1", {83, "supsham_death2", NULL, 0}},
  {"supsham_death2", {84, "supsham_death3", NULL, 0}},
  {"supsham_death3", {85, "supsham_death4", ops_97, 2}},
  {"supsham_death4", {86, "supsham_death5", NULL, 0}},
  {"supsham_death5", {87, "supsham_death6", NULL, 0}},
  {"supsham_death6", {88, "supsham_death7", NULL, 0}},
  {"supsham_death7", {89, "supsham_death8", NULL, 0}},
  {"supsham_death8", {90, "supsham_death9", NULL, 0}},
  {"supsham_death9", {91, "supsham_death10", NULL, 0}},
  {"supsham_death10", {92, "supsham_death11", NULL, 0}},
  {"supsham_death11", {93, "supsham_death11", NULL, 0}},
};
const qa_q1_monster_frame_table qa_q1am_mg3_super_shambler_frames = {entries, sizeof entries / sizeof entries[0]};
