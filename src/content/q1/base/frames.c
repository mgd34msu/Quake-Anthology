/* Port of src/content/q1/base/frames.ts. Generated mechanically
 * from a bun JSON dump of monsterFrames (1496 entries, in map order);
 * count and entries cross-checked against the TS source.
 * GPL-2.0-or-later. */
#include "qa/content/q1/base/frames.h"

static const qa_q1_monster_operation OPS_0[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_2[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_3[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_4[] = {{.kind = QA_Q1_OP_SOUND, .path = "ogre/ogidle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_5[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_6[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_7[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_8[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_9[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_10[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_11[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}, {.kind = QA_Q1_OP_SOUND, .path = "ogre/ogidle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}};
static const qa_q1_monster_operation OPS_12[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_13[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_14[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}, {.kind = QA_Q1_OP_SOUND, .path = "ogre/ogdrag.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.1}};
static const qa_q1_monster_operation OPS_15[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_16[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_17[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_18[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_19[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_20[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_21[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_22[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_23[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_24[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_25[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 9}, {.kind = QA_Q1_OP_SOUND, .path = "ogre/ogidle2.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}};
static const qa_q1_monster_operation OPS_26[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_27[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_28[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 22}};
static const qa_q1_monster_operation OPS_29[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_30[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 4}};
static const qa_q1_monster_operation OPS_31[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 13}};
static const qa_q1_monster_operation OPS_32[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 24}};
static const qa_q1_monster_operation OPS_33[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 11}, {.kind = QA_Q1_OP_SOUND, .path = "ogre/ogsawatk.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_34[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_35[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_36[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 13}};
static const qa_q1_monster_operation OPS_37[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 9}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_swing5"}};
static const qa_q1_monster_operation OPS_38[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_swing6"}};
static const qa_q1_monster_operation OPS_39[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_swing7"}};
static const qa_q1_monster_operation OPS_40[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_swing8"}};
static const qa_q1_monster_operation OPS_41[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_swing9"}};
static const qa_q1_monster_operation OPS_42[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_swing10"}};
static const qa_q1_monster_operation OPS_43[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_swing11"}};
static const qa_q1_monster_operation OPS_44[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_45[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}};
static const qa_q1_monster_operation OPS_46[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 9}};
static const qa_q1_monster_operation OPS_47[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}, {.kind = QA_Q1_OP_SOUND, .path = "ogre/ogsawatk.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_48[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_49[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_50[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_51[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_52[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_smash6"}};
static const qa_q1_monster_operation OPS_53[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_smash7"}};
static const qa_q1_monster_operation OPS_54[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_smash8"}};
static const qa_q1_monster_operation OPS_55[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 13}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_smash9"}};
static const qa_q1_monster_operation OPS_56[] = {{.kind = QA_Q1_OP_ACTION, .name = "ogre_smash10"}};
static const qa_q1_monster_operation OPS_57[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_smash11"}};
static const qa_q1_monster_operation OPS_58[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_59[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_60[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 12}};
static const qa_q1_monster_operation OPS_61[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_62[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_63[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_64[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_nail4"}};
static const qa_q1_monster_operation OPS_65[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_66[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_67[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_83[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10}};
static const qa_q1_monster_operation OPS_84[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 9}};
static const qa_q1_monster_operation OPS_85[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4}};
static const qa_q1_monster_operation OPS_99[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10}};
static const qa_q1_monster_operation OPS_100[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 9}};
static const qa_q1_monster_operation OPS_101[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4}};
static const qa_q1_monster_operation OPS_115[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_die3"}};
static const qa_q1_monster_operation OPS_128[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_129[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_ACTION, .name = "ogre_bdie3"}};
static const qa_q1_monster_operation OPS_130[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_131[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 3}};
static const qa_q1_monster_operation OPS_132[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 7}};
static const qa_q1_monster_operation OPS_133[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 25}};
static const qa_q1_monster_operation OPS_137[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_138[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_139[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_140[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_141[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_142[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_143[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_144[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_145[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_146[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_147[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_148[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_149[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_150[] = {{.kind = QA_Q1_OP_SOUND, .path = "demon/idle1.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_151[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_152[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_153[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 7}};
static const qa_q1_monster_operation OPS_154[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_155[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_156[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 10}};
static const qa_q1_monster_operation OPS_157[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 10}};
static const qa_q1_monster_operation OPS_158[] = {{.kind = QA_Q1_OP_SOUND, .path = "demon/idle1.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_159[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15}};
static const qa_q1_monster_operation OPS_160[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 36}};
static const qa_q1_monster_operation OPS_161[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_162[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15}};
static const qa_q1_monster_operation OPS_163[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 36}};
static const qa_q1_monster_operation OPS_164[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_165[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_166[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_167[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "demon1_jump4"}};
static const qa_q1_monster_operation OPS_173[] = {{.kind = QA_Q1_OP_ACTION, .name = "demon1_jump10"}};
static const qa_q1_monster_operation OPS_176[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_177[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_178[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_179[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_180[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}, {.kind = QA_Q1_OP_ACTION, .name = "demon1_atta5"}};
static const qa_q1_monster_operation OPS_181[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_182[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}};
static const qa_q1_monster_operation OPS_183[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}};
static const qa_q1_monster_operation OPS_184[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_185[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}};
static const qa_q1_monster_operation OPS_186[] = {{.kind = QA_Q1_OP_ACTION, .name = "demon1_atta11"}};
static const qa_q1_monster_operation OPS_187[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}};
static const qa_q1_monster_operation OPS_188[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}};
static const qa_q1_monster_operation OPS_189[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_190[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_197[] = {{.kind = QA_Q1_OP_SOUND, .path = "demon/ddeath.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_202[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_206[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_207[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_208[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_209[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_210[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_211[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_212[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_213[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_214[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_215[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_216[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_217[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_218[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_219[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_220[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_221[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_222[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_223[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 10}};
static const qa_q1_monster_operation OPS_224[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 9}};
static const qa_q1_monster_operation OPS_225[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 9}};
static const qa_q1_monster_operation OPS_226[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}};
static const qa_q1_monster_operation OPS_227[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_228[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 12}};
static const qa_q1_monster_operation OPS_229[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_230[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_231[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 13}};
static const qa_q1_monster_operation OPS_232[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 9}};
static const qa_q1_monster_operation OPS_233[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 7}};
static const qa_q1_monster_operation OPS_234[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 7}, {.kind = QA_Q1_OP_SOUND, .path = "shambler/sidle.wav", .channel = "voice", .attenuation = 2, .comparison = "greater", .has_chance = true, .chance = 0.8}};
static const qa_q1_monster_operation OPS_235[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_236[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 24}};
static const qa_q1_monster_operation OPS_237[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_238[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_239[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 24}};
static const qa_q1_monster_operation OPS_240[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}, {.kind = QA_Q1_OP_SOUND, .path = "shambler/sidle.wav", .channel = "voice", .attenuation = 2, .comparison = "greater", .has_chance = true, .chance = 0.8}};
static const qa_q1_monster_operation OPS_241[] = {{.kind = QA_Q1_OP_SOUND, .path = "shambler/melee1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}};
static const qa_q1_monster_operation OPS_242[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}};
static const qa_q1_monster_operation OPS_243[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}};
static const qa_q1_monster_operation OPS_244[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}};
static const qa_q1_monster_operation OPS_245[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_246[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_247[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_248[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_249[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_250[] = {{.kind = QA_Q1_OP_ACTION, .name = "sham_smash10"}};
static const qa_q1_monster_operation OPS_251[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}};
static const qa_q1_monster_operation OPS_252[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_253[] = {{.kind = QA_Q1_OP_SOUND, .path = "shambler/melee2.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}};
static const qa_q1_monster_operation OPS_254[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_255[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}};
static const qa_q1_monster_operation OPS_256[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_257[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}};
static const qa_q1_monster_operation OPS_258[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 9}};
static const qa_q1_monster_operation OPS_259[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}, {.kind = QA_Q1_OP_ACTION, .name = "sham_swingl7"}};
static const qa_q1_monster_operation OPS_260[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_261[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}, {.kind = QA_Q1_OP_ACTION, .name = "sham_swingl9"}};
static const qa_q1_monster_operation OPS_262[] = {{.kind = QA_Q1_OP_SOUND, .path = "shambler/melee1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_263[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}};
static const qa_q1_monster_operation OPS_264[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14}};
static const qa_q1_monster_operation OPS_265[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}};
static const qa_q1_monster_operation OPS_266[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_267[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}};
static const qa_q1_monster_operation OPS_268[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}, {.kind = QA_Q1_OP_ACTION, .name = "sham_swingr7"}};
static const qa_q1_monster_operation OPS_269[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_270[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}, {.kind = QA_Q1_OP_ACTION, .name = "sham_swingr9"}};
static const qa_q1_monster_operation OPS_271[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_SOUND, .path = "shambler/sattck1.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_272[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_273[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "sham_magic3"}};
static const qa_q1_monster_operation OPS_274[] = {{.kind = QA_Q1_OP_ACTION, .name = "sham_magic4"}};
static const qa_q1_monster_operation OPS_275[] = {{.kind = QA_Q1_OP_ACTION, .name = "sham_magic5"}};
static const qa_q1_monster_operation OPS_276[] = {{.kind = QA_Q1_OP_ACTION, .name = "sham_magic6"}};
static const qa_q1_monster_operation OPS_277[] = {{.kind = QA_Q1_OP_ACTION, .name = "sham_magic9"}};
static const qa_q1_monster_operation OPS_278[] = {{.kind = QA_Q1_OP_ACTION, .name = "sham_magic10"}};
static const qa_q1_monster_operation OPS_289[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_298[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_299[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_300[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_301[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_302[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_303[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_304[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_305[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_306[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_307[] = {{.kind = QA_Q1_OP_SOUND, .path = "knight/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_308[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_309[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_310[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_311[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_312[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_313[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_314[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_315[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_316[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_317[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_318[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_319[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_320[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_321[] = {{.kind = QA_Q1_OP_SOUND, .path = "knight/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_322[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_323[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 13}};
static const qa_q1_monster_operation OPS_324[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 7}};
static const qa_q1_monster_operation OPS_325[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_326[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_327[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14}};
static const qa_q1_monster_operation OPS_328[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 6}};
static const qa_q1_monster_operation OPS_329[] = {{.kind = QA_Q1_OP_ACTION, .name = "knight_runatk1"}};
static const qa_q1_monster_operation OPS_330[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_331[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_332[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_333[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_334[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_335[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_336[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_337[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_338[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE_SIDE, .distance = 0}};
static const qa_q1_monster_operation OPS_339[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_340[] = {{.kind = QA_Q1_OP_SOUND, .path = "knight/sword1.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_341[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}};
static const qa_q1_monster_operation OPS_342[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_343[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_344[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_345[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_346[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_347[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_348[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_349[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}};
static const qa_q1_monster_operation OPS_353[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 0}};
static const qa_q1_monster_operation OPS_354[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3}};
static const qa_q1_monster_operation OPS_357[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 2}};
static const qa_q1_monster_operation OPS_358[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4}};
static const qa_q1_monster_operation OPS_359[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 2}};
static const qa_q1_monster_operation OPS_360[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_361[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_362[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 0}};
static const qa_q1_monster_operation OPS_364[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_365[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_366[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_367[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_368[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_369[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_370[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_371[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_372[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_373[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_376[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_386[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_395[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_396[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_397[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_398[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_399[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_400[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_401[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_402[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_403[] = {{.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_404[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_405[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_406[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_407[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_408[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_409[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_410[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_411[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_412[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_413[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_414[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_415[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_416[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_417[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_418[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_419[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_420[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_421[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_422[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_423[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_424[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_425[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_426[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_427[] = {{.kind = QA_Q1_OP_SOUND, .path = "soldier/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11}};
static const qa_q1_monster_operation OPS_428[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15}};
static const qa_q1_monster_operation OPS_429[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10}};
static const qa_q1_monster_operation OPS_430[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10}};
static const qa_q1_monster_operation OPS_431[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_432[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 15}};
static const qa_q1_monster_operation OPS_433[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 10}};
static const qa_q1_monster_operation OPS_434[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_435[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_436[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_437[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_438[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_439[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "army_atk5"}};
static const qa_q1_monster_operation OPS_440[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_441[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "army_atk7"}};
static const qa_q1_monster_operation OPS_442[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_443[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_449[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_451[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 13}};
static const qa_q1_monster_operation OPS_452[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 9}};
static const qa_q1_monster_operation OPS_461[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2}};
static const qa_q1_monster_operation OPS_465[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_468[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_469[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_471[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_472[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 4}};
static const qa_q1_monster_operation OPS_473[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3}};
static const qa_q1_monster_operation OPS_474[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 6}};
static const qa_q1_monster_operation OPS_475[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 8}};
static const qa_q1_monster_operation OPS_479[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_ACTION, .name = "army_die3"}};
static const qa_q1_monster_operation OPS_488[] = {{.kind = QA_Q1_OP_ACTION, .name = "army_cdie2"}};
static const qa_q1_monster_operation OPS_489[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_ACTION, .name = "army_cdie3"}};
static const qa_q1_monster_operation OPS_490[] = {{.kind = QA_Q1_OP_ACTION, .name = "army_cdie4"}};
static const qa_q1_monster_operation OPS_491[] = {{.kind = QA_Q1_OP_ACTION, .name = "army_cdie5"}};
static const qa_q1_monster_operation OPS_492[] = {{.kind = QA_Q1_OP_ACTION, .name = "army_cdie6"}};
static const qa_q1_monster_operation OPS_498[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_499[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_500[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_501[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_502[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_503[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_504[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_505[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_506[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}, {.kind = QA_Q1_OP_ACTION, .name = "wiz_walk1"}};
static const qa_q1_monster_operation OPS_507[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_508[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_509[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_510[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_511[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_512[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_513[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_514[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}, {.kind = QA_Q1_OP_ACTION, .name = "wiz_side1"}};
static const qa_q1_monster_operation OPS_515[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_516[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_517[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_518[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_519[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_520[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_521[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}};
static const qa_q1_monster_operation OPS_522[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}, {.kind = QA_Q1_OP_ACTION, .name = "wiz_run1"}};
static const qa_q1_monster_operation OPS_523[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_524[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_525[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_526[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_527[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_528[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_529[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_530[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_531[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_532[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_533[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_534[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_535[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_536[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "wiz_fast1"}};
static const qa_q1_monster_operation OPS_537[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_538[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_539[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_540[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_541[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_542[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_543[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_544[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_545[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "wiz_fast10"}};
static const qa_q1_monster_operation OPS_550[] = {{.kind = QA_Q1_OP_ACTION, .name = "wiz_death1"}};
static const qa_q1_monster_operation OPS_552[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_558[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_559[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_560[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_561[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_562[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_563[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_564[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_565[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_566[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_567[] = {{.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_568[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_569[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_570[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_571[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_572[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_573[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_574[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_575[] = {{.kind = QA_Q1_OP_SOUND, .path = "dog/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_576[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32}};
static const qa_q1_monster_operation OPS_577[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32}};
static const qa_q1_monster_operation OPS_578[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_579[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64}};
static const qa_q1_monster_operation OPS_580[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32}};
static const qa_q1_monster_operation OPS_581[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_582[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32}};
static const qa_q1_monster_operation OPS_583[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32}};
static const qa_q1_monster_operation OPS_584[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 20}};
static const qa_q1_monster_operation OPS_585[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 64}};
static const qa_q1_monster_operation OPS_586[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 32}};
static const qa_q1_monster_operation OPS_587[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_588[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_589[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_590[] = {{.kind = QA_Q1_OP_SOUND, .path = "dog/dattack1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_ACTION, .name = "dog_atta4"}};
static const qa_q1_monster_operation OPS_591[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_592[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_593[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_594[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_595[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_596[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "dog_leap2"}};
static const qa_q1_monster_operation OPS_612[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4}};
static const qa_q1_monster_operation OPS_613[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12}};
static const qa_q1_monster_operation OPS_614[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 12}};
static const qa_q1_monster_operation OPS_615[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2}};
static const qa_q1_monster_operation OPS_617[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 4}};
static const qa_q1_monster_operation OPS_619[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 10}};
static const qa_q1_monster_operation OPS_644[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_645[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_646[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_647[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_648[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_649[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_650[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_651[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_652[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_653[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_654[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_655[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_656[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_657[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_658[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_659[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/idle_w2.wav", .channel = "voice", .attenuation = 3, .comparison = "less", .has_chance = true, .chance = 0.1}};
static const qa_q1_monster_operation OPS_660[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_cruc2"}};
static const qa_q1_monster_operation OPS_661[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_cruc3"}};
static const qa_q1_monster_operation OPS_662[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_cruc4"}};
static const qa_q1_monster_operation OPS_663[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_cruc5"}};
static const qa_q1_monster_operation OPS_664[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_cruc6"}};
static const qa_q1_monster_operation OPS_665[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_666[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_667[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_668[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_669[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_670[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_671[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_672[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_673[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_674[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_675[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_676[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_677[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_678[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_679[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_680[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_681[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_682[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_683[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}, {.kind = QA_Q1_OP_SOUND, .path = "zombie/z_idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}};
static const qa_q1_monster_operation OPS_684[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 1}, {.kind = QA_Q1_OP_ACTION, .name = "zombie_run1"}};
static const qa_q1_monster_operation OPS_685[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 1}};
static const qa_q1_monster_operation OPS_686[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_687[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 1}};
static const qa_q1_monster_operation OPS_688[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_689[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 3}};
static const qa_q1_monster_operation OPS_690[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 4}};
static const qa_q1_monster_operation OPS_691[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 4}};
static const qa_q1_monster_operation OPS_692[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_693[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_694[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_695[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_696[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_697[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 4}};
static const qa_q1_monster_operation OPS_698[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 6}};
static const qa_q1_monster_operation OPS_699[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 7}};
static const qa_q1_monster_operation OPS_700[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 3}};
static const qa_q1_monster_operation OPS_701[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 8}, {.kind = QA_Q1_OP_SOUND, .path = "zombie/z_idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_SOUND, .path = "zombie/z_idle1.wav", .channel = "voice", .attenuation = 2, .comparison = "greater", .has_chance = true, .chance = 0.8}};
static const qa_q1_monster_operation OPS_702[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_703[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_704[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_705[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_706[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_707[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_708[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_709[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_710[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_711[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_712[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_713[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_714[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "zombie_atta13"}};
static const qa_q1_monster_operation OPS_715[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_716[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_717[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_718[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_719[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_720[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_721[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_722[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_723[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_724[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_725[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_726[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_727[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_728[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "zombie_attb14"}};
static const qa_q1_monster_operation OPS_729[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_730[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_731[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_732[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_733[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_734[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_735[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_736[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_737[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_738[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_739[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_740[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "zombie_attc12"}};
static const qa_q1_monster_operation OPS_741[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_pain.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_742[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3}};
static const qa_q1_monster_operation OPS_743[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_744[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_745[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3}};
static const qa_q1_monster_operation OPS_746[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_753[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_pain1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_754[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2}};
static const qa_q1_monster_operation OPS_755[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 8}};
static const qa_q1_monster_operation OPS_756[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_757[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2}};
static const qa_q1_monster_operation OPS_761[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_fall.wav", .channel = "body", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_777[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_781[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_pain1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_783[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3}};
static const qa_q1_monster_operation OPS_784[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_791[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_792[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_799[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_pain.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_807[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_812[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_pain.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_ACTION, .name = "zombie_paine1"}};
static const qa_q1_monster_operation OPS_813[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 8}};
static const qa_q1_monster_operation OPS_814[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 5}};
static const qa_q1_monster_operation OPS_815[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 3}};
static const qa_q1_monster_operation OPS_816[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_817[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2}};
static const qa_q1_monster_operation OPS_818[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_819[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_820[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 2}};
static const qa_q1_monster_operation OPS_821[] = {{.kind = QA_Q1_OP_SOUND, .path = "zombie/z_fall.wav", .channel = "body", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_822[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_paine11"}};
static const qa_q1_monster_operation OPS_823[] = {{.kind = QA_Q1_OP_ACTION, .name = "zombie_paine12"}};
static const qa_q1_monster_operation OPS_836[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_837[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 3}};
static const qa_q1_monster_operation OPS_838[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_839[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_842[] = {{.kind = QA_Q1_OP_SOUND, .path = "boss1/out1.wav", .channel = "weapon", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_843[] = {{.kind = QA_Q1_OP_SOUND, .path = "boss1/sight1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_859[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle1"}};
static const qa_q1_monster_operation OPS_860[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle2"}};
static const qa_q1_monster_operation OPS_861[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle3"}};
static const qa_q1_monster_operation OPS_862[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle4"}};
static const qa_q1_monster_operation OPS_863[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle5"}};
static const qa_q1_monster_operation OPS_864[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle6"}};
static const qa_q1_monster_operation OPS_865[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle7"}};
static const qa_q1_monster_operation OPS_866[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle8"}};
static const qa_q1_monster_operation OPS_867[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle9"}};
static const qa_q1_monster_operation OPS_868[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle10"}};
static const qa_q1_monster_operation OPS_869[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle11"}};
static const qa_q1_monster_operation OPS_870[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle12"}};
static const qa_q1_monster_operation OPS_871[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle13"}};
static const qa_q1_monster_operation OPS_872[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle14"}};
static const qa_q1_monster_operation OPS_873[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle15"}};
static const qa_q1_monster_operation OPS_874[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle16"}};
static const qa_q1_monster_operation OPS_875[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle17"}};
static const qa_q1_monster_operation OPS_876[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle18"}};
static const qa_q1_monster_operation OPS_877[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle19"}};
static const qa_q1_monster_operation OPS_878[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle20"}};
static const qa_q1_monster_operation OPS_879[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle21"}};
static const qa_q1_monster_operation OPS_880[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle22"}};
static const qa_q1_monster_operation OPS_881[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle23"}};
static const qa_q1_monster_operation OPS_882[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle24"}};
static const qa_q1_monster_operation OPS_883[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle25"}};
static const qa_q1_monster_operation OPS_884[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle26"}};
static const qa_q1_monster_operation OPS_885[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle27"}};
static const qa_q1_monster_operation OPS_886[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle28"}};
static const qa_q1_monster_operation OPS_887[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle29"}};
static const qa_q1_monster_operation OPS_888[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle30"}};
static const qa_q1_monster_operation OPS_889[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_idle31"}};
static const qa_q1_monster_operation OPS_890[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile1"}};
static const qa_q1_monster_operation OPS_891[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile2"}};
static const qa_q1_monster_operation OPS_892[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile3"}};
static const qa_q1_monster_operation OPS_893[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile4"}};
static const qa_q1_monster_operation OPS_894[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile5"}};
static const qa_q1_monster_operation OPS_895[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile6"}};
static const qa_q1_monster_operation OPS_896[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile7"}};
static const qa_q1_monster_operation OPS_897[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile8"}};
static const qa_q1_monster_operation OPS_898[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile9"}};
static const qa_q1_monster_operation OPS_899[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile10"}};
static const qa_q1_monster_operation OPS_900[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile11"}};
static const qa_q1_monster_operation OPS_901[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile12"}};
static const qa_q1_monster_operation OPS_902[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile13"}};
static const qa_q1_monster_operation OPS_903[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile14"}};
static const qa_q1_monster_operation OPS_904[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile15"}};
static const qa_q1_monster_operation OPS_905[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile16"}};
static const qa_q1_monster_operation OPS_906[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile17"}};
static const qa_q1_monster_operation OPS_907[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile18"}};
static const qa_q1_monster_operation OPS_908[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile19"}};
static const qa_q1_monster_operation OPS_909[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile20"}};
static const qa_q1_monster_operation OPS_910[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile21"}};
static const qa_q1_monster_operation OPS_911[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile22"}};
static const qa_q1_monster_operation OPS_912[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_missile23"}};
static const qa_q1_monster_operation OPS_943[] = {{.kind = QA_Q1_OP_SOUND, .path = "boss1/death.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_951[] = {{.kind = QA_Q1_OP_SOUND, .path = "boss1/out1.wav", .channel = "body", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_ACTION, .name = "boss_death9"}};
static const qa_q1_monster_operation OPS_952[] = {{.kind = QA_Q1_OP_ACTION, .name = "boss_death10"}};
static const qa_q1_monster_operation OPS_953[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_954[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_955[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_956[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_957[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_958[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_959[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_960[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_961[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_962[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_963[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_964[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_TURN, .distance = 0}};
static const qa_q1_monster_operation OPS_965[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_966[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_967[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_968[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_969[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_970[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_971[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_972[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_973[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_974[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_975[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_976[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_977[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_978[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_979[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_980[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_981[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_982[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_983[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_984[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_985[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_986[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_987[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_988[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_989[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_990[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_991[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_992[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_993[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_994[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_995[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_996[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_997[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_998[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_999[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_1000[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_1001[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_1002[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_1003[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_1004[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 2}};
static const qa_q1_monster_operation OPS_1008[] = {{.kind = QA_Q1_OP_ACTION, .name = "tbaby_fly4"}};
static const qa_q1_monster_operation OPS_1009[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1010[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1011[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1012[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1013[] = {{.kind = QA_Q1_OP_ACTION, .name = "tbaby_jump5"}};
static const qa_q1_monster_operation OPS_1015[] = {{.kind = QA_Q1_OP_ACTION, .name = "tbaby_die1"}};
static const qa_q1_monster_operation OPS_1016[] = {{.kind = QA_Q1_OP_ACTION, .name = "tbaby_die2"}};
static const qa_q1_monster_operation OPS_1017[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1018[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1019[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1020[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1021[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1022[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1023[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1024[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1025[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1026[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_walk1"}};
static const qa_q1_monster_operation OPS_1027[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}};
static const qa_q1_monster_operation OPS_1028[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}};
static const qa_q1_monster_operation OPS_1029[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1030[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1031[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1032[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1033[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1034[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1035[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1036[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1037[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1038[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_1039[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1040[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1041[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1042[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1043[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1044[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1045[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1046[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_run1"}};
static const qa_q1_monster_operation OPS_1047[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 25}};
static const qa_q1_monster_operation OPS_1048[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 18}};
static const qa_q1_monster_operation OPS_1049[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 16}};
static const qa_q1_monster_operation OPS_1050[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14}};
static const qa_q1_monster_operation OPS_1051[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 25}};
static const qa_q1_monster_operation OPS_1052[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 21}};
static const qa_q1_monster_operation OPS_1053[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 13}};
static const qa_q1_monster_operation OPS_1054[] = {{.kind = QA_Q1_OP_SOUND, .path = "hknight/pain1.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_1059[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 10}};
static const qa_q1_monster_operation OPS_1060[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 8}};
static const qa_q1_monster_operation OPS_1061[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 7}};
static const qa_q1_monster_operation OPS_1066[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 10}};
static const qa_q1_monster_operation OPS_1067[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 11}};
static const qa_q1_monster_operation OPS_1073[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_1080[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1081[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1082[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1083[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1084[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1085[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1086[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magica7"}};
static const qa_q1_monster_operation OPS_1087[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magica8"}};
static const qa_q1_monster_operation OPS_1088[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magica9"}};
static const qa_q1_monster_operation OPS_1089[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magica10"}};
static const qa_q1_monster_operation OPS_1090[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magica11"}};
static const qa_q1_monster_operation OPS_1091[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magica12"}};
static const qa_q1_monster_operation OPS_1092[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1093[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1094[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1095[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1096[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1097[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1098[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1099[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1100[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicb7"}};
static const qa_q1_monster_operation OPS_1101[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicb8"}};
static const qa_q1_monster_operation OPS_1102[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicb9"}};
static const qa_q1_monster_operation OPS_1103[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicb10"}};
static const qa_q1_monster_operation OPS_1104[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicb11"}};
static const qa_q1_monster_operation OPS_1105[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicb12"}};
static const qa_q1_monster_operation OPS_1106[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1107[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1108[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1109[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1110[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1111[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1112[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicc6"}};
static const qa_q1_monster_operation OPS_1113[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicc7"}};
static const qa_q1_monster_operation OPS_1114[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicc8"}};
static const qa_q1_monster_operation OPS_1115[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicc9"}};
static const qa_q1_monster_operation OPS_1116[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicc10"}};
static const qa_q1_monster_operation OPS_1117[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_magicc11"}};
static const qa_q1_monster_operation OPS_1118[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 20}};
static const qa_q1_monster_operation OPS_1119[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 25}};
static const qa_q1_monster_operation OPS_1120[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 18}};
static const qa_q1_monster_operation OPS_1121[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 16}};
static const qa_q1_monster_operation OPS_1122[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14}};
static const qa_q1_monster_operation OPS_1123[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 20}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1124[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 21}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1125[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 13}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1126[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 20}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1127[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 20}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1128[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 18}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1129[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 16}};
static const qa_q1_monster_operation OPS_1130[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 14}};
static const qa_q1_monster_operation OPS_1131[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 25}};
static const qa_q1_monster_operation OPS_1132[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 21}};
static const qa_q1_monster_operation OPS_1133[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 13}};
static const qa_q1_monster_operation OPS_1134[] = {{.kind = QA_Q1_OP_ACTION, .name = "hknight_char_b1"}};
static const qa_q1_monster_operation OPS_1135[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 17}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1136[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 12}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1137[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 22}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1138[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 18}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1139[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1140[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 9}};
static const qa_q1_monster_operation OPS_1141[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}};
static const qa_q1_monster_operation OPS_1142[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 13}};
static const qa_q1_monster_operation OPS_1143[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_1144[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1145[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 15}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1146[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 8}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1147[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1148[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1149[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_1150[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_1151[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 13}};
static const qa_q1_monster_operation OPS_1152[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 9}};
static const qa_q1_monster_operation OPS_1153[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 11}};
static const qa_q1_monster_operation OPS_1154[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1155[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1156[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 12}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1157[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1158[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1159[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1160[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1161[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}};
static const qa_q1_monster_operation OPS_1162[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1163[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1164[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1165[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1166[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1167[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_1168[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}};
static const qa_q1_monster_operation OPS_1169[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 5}};
static const qa_q1_monster_operation OPS_1170[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1171[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1172[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1173[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1174[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1175[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 0}};
static const qa_q1_monster_operation OPS_1176[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}};
static const qa_q1_monster_operation OPS_1177[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 1}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1178[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1179[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 4}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_MELEE, .distance = 0}};
static const qa_q1_monster_operation OPS_1180[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 6}};
static const qa_q1_monster_operation OPS_1181[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 7}};
static const qa_q1_monster_operation OPS_1182[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 3}};
static const qa_q1_monster_operation OPS_1183[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1184[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1185[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1186[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1187[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1188[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1189[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1190[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1191[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1192[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1193[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1194[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1195[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1196[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1197[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1198[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1199[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1200[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1201[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1202[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1203[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1204[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1205[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1206[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1207[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1208[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1209[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1210[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1211[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1212[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1213[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1214[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1215[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1216[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1217[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1218[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 8}};
static const qa_q1_monster_operation OPS_1219[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}, {.kind = QA_Q1_OP_SOUND, .path = "fish/idle.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = true, .chance = 0.5}};
static const qa_q1_monster_operation OPS_1220[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1221[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1222[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1223[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1224[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1225[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1226[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1227[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1228[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1229[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1230[] = {{.kind = QA_Q1_OP_ACTION, .name = "f_attack3"}};
static const qa_q1_monster_operation OPS_1231[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1232[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1233[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1234[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1235[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1236[] = {{.kind = QA_Q1_OP_ACTION, .name = "f_attack9"}};
static const qa_q1_monster_operation OPS_1237[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1238[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1239[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1240[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1241[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1242[] = {{.kind = QA_Q1_OP_ACTION, .name = "f_attack15"}};
static const qa_q1_monster_operation OPS_1243[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1244[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1245[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_CHARGE, .distance = 10}};
static const qa_q1_monster_operation OPS_1246[] = {{.kind = QA_Q1_OP_SOUND, .path = "fish/death.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}};
static const qa_q1_monster_operation OPS_1247[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}};
static const qa_q1_monster_operation OPS_1268[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1269[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1270[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1271[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1272[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1273[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1274[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1275[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 6}};
static const qa_q1_monster_operation OPS_1276[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1277[] = {{.kind = QA_Q1_OP_SOUND, .path = "shalrath/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_1278[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1279[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_1280[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_1281[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_1282[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_1283[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}};
static const qa_q1_monster_operation OPS_1284[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 6}};
static const qa_q1_monster_operation OPS_1285[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}};
static const qa_q1_monster_operation OPS_1286[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 0}};
static const qa_q1_monster_operation OPS_1287[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1288[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 5}};
static const qa_q1_monster_operation OPS_1289[] = {{.kind = QA_Q1_OP_SOUND, .path = "shalrath/idle.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 6}};
static const qa_q1_monster_operation OPS_1290[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 4}};
static const qa_q1_monster_operation OPS_1291[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_1292[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_1293[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_1294[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_1295[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 5}};
static const qa_q1_monster_operation OPS_1296[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 6}};
static const qa_q1_monster_operation OPS_1297[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 5}};
static const qa_q1_monster_operation OPS_1298[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 0}};
static const qa_q1_monster_operation OPS_1299[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 4}};
static const qa_q1_monster_operation OPS_1300[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 5}};
static const qa_q1_monster_operation OPS_1301[] = {{.kind = QA_Q1_OP_SOUND, .path = "shalrath/attack.wav", .channel = "voice", .attenuation = 1, .comparison = "less", .has_chance = false}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1302[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1303[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1304[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1305[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1306[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1307[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1308[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1309[] = {{.kind = QA_Q1_OP_ACTION, .name = "shal_attack9"}};
static const qa_q1_monster_operation OPS_1310[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1324[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1325[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1326[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1327[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1328[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1329[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1330[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};
static const qa_q1_monster_operation OPS_1331[] = {{.kind = QA_Q1_OP_SOUND, .path = "enforcer/idle1.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1332[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1333[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1334[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1335[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_1336[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1337[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1338[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_1339[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1340[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1341[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1342[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 1}};
static const qa_q1_monster_operation OPS_1343[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1344[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 3}};
static const qa_q1_monster_operation OPS_1345[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 4}};
static const qa_q1_monster_operation OPS_1346[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_WALK, .distance = 2}};
static const qa_q1_monster_operation OPS_1347[] = {{.kind = QA_Q1_OP_SOUND, .path = "enforcer/idle1.wav", .channel = "voice", .attenuation = 2, .comparison = "less", .has_chance = true, .chance = 0.2}, {.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 18}};
static const qa_q1_monster_operation OPS_1348[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14}};
static const qa_q1_monster_operation OPS_1349[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 7}};
static const qa_q1_monster_operation OPS_1350[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 12}};
static const qa_q1_monster_operation OPS_1351[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14}};
static const qa_q1_monster_operation OPS_1352[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 14}};
static const qa_q1_monster_operation OPS_1353[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 7}};
static const qa_q1_monster_operation OPS_1354[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_RUN, .distance = 11}};
static const qa_q1_monster_operation OPS_1355[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1356[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1357[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1358[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1359[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1360[] = {{.kind = QA_Q1_OP_ACTION, .name = "enf_atk6"}};
static const qa_q1_monster_operation OPS_1361[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1362[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1363[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1364[] = {{.kind = QA_Q1_OP_ACTION, .name = "enf_atk10"}};
static const qa_q1_monster_operation OPS_1365[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1366[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1367[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}};
static const qa_q1_monster_operation OPS_1368[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FACE, .distance = 0}, {.kind = QA_Q1_OP_ACTION, .name = "enf_atk14"}};
static const qa_q1_monster_operation OPS_1389[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 2}};
static const qa_q1_monster_operation OPS_1390[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_1396[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_1397[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_1398[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAINFORWARD, .distance = 1}};
static const qa_q1_monster_operation OPS_1401[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_1402[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_PAIN, .distance = 1}};
static const qa_q1_monster_operation OPS_1407[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_ACTION, .name = "enf_die3"}};
static const qa_q1_monster_operation OPS_1408[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 14}};
static const qa_q1_monster_operation OPS_1409[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 2}};
static const qa_q1_monster_operation OPS_1413[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 3}};
static const qa_q1_monster_operation OPS_1414[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_1415[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_1416[] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_FORWARD, .distance = 5}};
static const qa_q1_monster_operation OPS_1421[] = {{.kind = QA_Q1_OP_SOLID, .solid = "none"}, {.kind = QA_Q1_OP_ACTION, .name = "enf_fdie3"}};
static const qa_q1_monster_operation OPS_1476[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "m"}};
static const qa_q1_monster_operation OPS_1477[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "k"}};
static const qa_q1_monster_operation OPS_1478[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "k"}};
static const qa_q1_monster_operation OPS_1479[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "i"}};
static const qa_q1_monster_operation OPS_1480[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "g"}};
static const qa_q1_monster_operation OPS_1481[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "e"}};
static const qa_q1_monster_operation OPS_1482[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "c"}};
static const qa_q1_monster_operation OPS_1483[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "a"}};
static const qa_q1_monster_operation OPS_1484[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "c"}};
static const qa_q1_monster_operation OPS_1485[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "e"}};
static const qa_q1_monster_operation OPS_1486[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "g"}};
static const qa_q1_monster_operation OPS_1487[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "i"}};
static const qa_q1_monster_operation OPS_1488[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "k"}};
static const qa_q1_monster_operation OPS_1489[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "m"}};
static const qa_q1_monster_operation OPS_1490[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "m"}, {.kind = QA_Q1_OP_ACTION, .name = "old_thrash15"}};
static const qa_q1_monster_operation OPS_1491[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "g"}};
static const qa_q1_monster_operation OPS_1492[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "c"}};
static const qa_q1_monster_operation OPS_1493[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "b"}};
static const qa_q1_monster_operation OPS_1494[] = {{.kind = QA_Q1_OP_LIGHTSTYLE, .pattern = "a"}};
static const qa_q1_monster_operation OPS_1495[] = {{.kind = QA_Q1_OP_ACTION, .name = "old_thrash20"}};
static const qa_q1_monster_frame_entry ENTRIES[] = {
  {"ogre_stand1", {0, "ogre_stand2", OPS_0, 1}},
  {"ogre_stand2", {1, "ogre_stand3", OPS_1, 1}},
  {"ogre_stand3", {2, "ogre_stand4", OPS_2, 1}},
  {"ogre_stand4", {3, "ogre_stand5", OPS_3, 1}},
  {"ogre_stand5", {4, "ogre_stand6", OPS_4, 2}},
  {"ogre_stand6", {5, "ogre_stand7", OPS_5, 1}},
  {"ogre_stand7", {6, "ogre_stand8", OPS_6, 1}},
  {"ogre_stand8", {7, "ogre_stand9", OPS_7, 1}},
  {"ogre_stand9", {8, "ogre_stand1", OPS_8, 1}},
  {"ogre_walk1", {9, "ogre_walk2", OPS_9, 1}},
  {"ogre_walk2", {10, "ogre_walk3", OPS_10, 1}},
  {"ogre_walk3", {11, "ogre_walk4", OPS_11, 2}},
  {"ogre_walk4", {12, "ogre_walk5", OPS_12, 1}},
  {"ogre_walk5", {13, "ogre_walk6", OPS_13, 1}},
  {"ogre_walk6", {14, "ogre_walk7", OPS_14, 2}},
  {"ogre_walk7", {15, "ogre_walk8", OPS_15, 1}},
  {"ogre_walk8", {16, "ogre_walk9", OPS_16, 1}},
  {"ogre_walk9", {17, "ogre_walk10", OPS_17, 1}},
  {"ogre_walk10", {18, "ogre_walk11", OPS_18, 1}},
  {"ogre_walk11", {19, "ogre_walk12", OPS_19, 1}},
  {"ogre_walk12", {20, "ogre_walk13", OPS_20, 1}},
  {"ogre_walk13", {21, "ogre_walk14", OPS_21, 1}},
  {"ogre_walk14", {22, "ogre_walk15", OPS_22, 1}},
  {"ogre_walk15", {23, "ogre_walk16", OPS_23, 1}},
  {"ogre_walk16", {24, "ogre_walk1", OPS_24, 1}},
  {"ogre_run1", {25, "ogre_run2", OPS_25, 2}},
  {"ogre_run2", {26, "ogre_run3", OPS_26, 1}},
  {"ogre_run3", {27, "ogre_run4", OPS_27, 1}},
  {"ogre_run4", {28, "ogre_run5", OPS_28, 1}},
  {"ogre_run5", {29, "ogre_run6", OPS_29, 1}},
  {"ogre_run6", {30, "ogre_run7", OPS_30, 1}},
  {"ogre_run7", {31, "ogre_run8", OPS_31, 1}},
  {"ogre_run8", {32, "ogre_run1", OPS_32, 1}},
  {"ogre_swing1", {33, "ogre_swing2", OPS_33, 2}},
  {"ogre_swing2", {34, "ogre_swing3", OPS_34, 1}},
  {"ogre_swing3", {35, "ogre_swing4", OPS_35, 1}},
  {"ogre_swing4", {36, "ogre_swing5", OPS_36, 1}},
  {"ogre_swing5", {37, "ogre_swing6", OPS_37, 2}},
  {"ogre_swing6", {38, "ogre_swing7", OPS_38, 1}},
  {"ogre_swing7", {39, "ogre_swing8", OPS_39, 1}},
  {"ogre_swing8", {40, "ogre_swing9", OPS_40, 1}},
  {"ogre_swing9", {41, "ogre_swing10", OPS_41, 1}},
  {"ogre_swing10", {42, "ogre_swing11", OPS_42, 1}},
  {"ogre_swing11", {43, "ogre_swing12", OPS_43, 1}},
  {"ogre_swing12", {44, "ogre_swing13", OPS_44, 1}},
  {"ogre_swing13", {45, "ogre_swing14", OPS_45, 1}},
  {"ogre_swing14", {46, "ogre_run1", OPS_46, 1}},
  {"ogre_smash1", {47, "ogre_smash2", OPS_47, 2}},
  {"ogre_smash2", {48, "ogre_smash3", OPS_48, 1}},
  {"ogre_smash3", {49, "ogre_smash4", OPS_49, 1}},
  {"ogre_smash4", {50, "ogre_smash5", OPS_50, 1}},
  {"ogre_smash5", {51, "ogre_smash6", OPS_51, 1}},
  {"ogre_smash6", {52, "ogre_smash7", OPS_52, 2}},
  {"ogre_smash7", {53, "ogre_smash8", OPS_53, 2}},
  {"ogre_smash8", {54, "ogre_smash9", OPS_54, 2}},
  {"ogre_smash9", {55, "ogre_smash10", OPS_55, 2}},
  {"ogre_smash10", {56, "ogre_smash11", OPS_56, 1}},
  {"ogre_smash11", {57, "ogre_smash12", OPS_57, 2}},
  {"ogre_smash12", {58, "ogre_smash13", OPS_58, 1}},
  {"ogre_smash13", {59, "ogre_smash14", OPS_59, 1}},
  {"ogre_smash14", {60, "ogre_run1", OPS_60, 1}},
  {"ogre_nail1", {61, "ogre_nail2", OPS_61, 1}},
  {"ogre_nail2", {62, "ogre_nail3", OPS_62, 1}},
  {"ogre_nail3", {62, "ogre_nail4", OPS_63, 1}},
  {"ogre_nail4", {63, "ogre_nail5", OPS_64, 2}},
  {"ogre_nail5", {64, "ogre_nail6", OPS_65, 1}},
  {"ogre_nail6", {65, "ogre_nail7", OPS_66, 1}},
  {"ogre_nail7", {66, "ogre_run1", OPS_67, 1}},
  {"ogre_pain1", {67, "ogre_pain2", NULL, 0}},
  {"ogre_pain2", {68, "ogre_pain3", NULL, 0}},
  {"ogre_pain3", {69, "ogre_pain4", NULL, 0}},
  {"ogre_pain4", {70, "ogre_pain5", NULL, 0}},
  {"ogre_pain5", {71, "ogre_run1", NULL, 0}},
  {"ogre_painb1", {72, "ogre_painb2", NULL, 0}},
  {"ogre_painb2", {73, "ogre_painb3", NULL, 0}},
  {"ogre_painb3", {74, "ogre_run1", NULL, 0}},
  {"ogre_painc1", {75, "ogre_painc2", NULL, 0}},
  {"ogre_painc2", {76, "ogre_painc3", NULL, 0}},
  {"ogre_painc3", {77, "ogre_painc4", NULL, 0}},
  {"ogre_painc4", {78, "ogre_painc5", NULL, 0}},
  {"ogre_painc5", {79, "ogre_painc6", NULL, 0}},
  {"ogre_painc6", {80, "ogre_run1", NULL, 0}},
  {"ogre_paind1", {81, "ogre_paind2", NULL, 0}},
  {"ogre_paind2", {82, "ogre_paind3", OPS_83, 1}},
  {"ogre_paind3", {83, "ogre_paind4", OPS_84, 1}},
  {"ogre_paind4", {84, "ogre_paind5", OPS_85, 1}},
  {"ogre_paind5", {85, "ogre_paind6", NULL, 0}},
  {"ogre_paind6", {86, "ogre_paind7", NULL, 0}},
  {"ogre_paind7", {87, "ogre_paind8", NULL, 0}},
  {"ogre_paind8", {88, "ogre_paind9", NULL, 0}},
  {"ogre_paind9", {89, "ogre_paind10", NULL, 0}},
  {"ogre_paind10", {90, "ogre_paind11", NULL, 0}},
  {"ogre_paind11", {91, "ogre_paind12", NULL, 0}},
  {"ogre_paind12", {92, "ogre_paind13", NULL, 0}},
  {"ogre_paind13", {93, "ogre_paind14", NULL, 0}},
  {"ogre_paind14", {94, "ogre_paind15", NULL, 0}},
  {"ogre_paind15", {95, "ogre_paind16", NULL, 0}},
  {"ogre_paind16", {96, "ogre_run1", NULL, 0}},
  {"ogre_paine1", {97, "ogre_paine2", NULL, 0}},
  {"ogre_paine2", {98, "ogre_paine3", OPS_99, 1}},
  {"ogre_paine3", {99, "ogre_paine4", OPS_100, 1}},
  {"ogre_paine4", {100, "ogre_paine5", OPS_101, 1}},
  {"ogre_paine5", {101, "ogre_paine6", NULL, 0}},
  {"ogre_paine6", {102, "ogre_paine7", NULL, 0}},
  {"ogre_paine7", {103, "ogre_paine8", NULL, 0}},
  {"ogre_paine8", {104, "ogre_paine9", NULL, 0}},
  {"ogre_paine9", {105, "ogre_paine10", NULL, 0}},
  {"ogre_paine10", {106, "ogre_paine11", NULL, 0}},
  {"ogre_paine11", {107, "ogre_paine12", NULL, 0}},
  {"ogre_paine12", {108, "ogre_paine13", NULL, 0}},
  {"ogre_paine13", {109, "ogre_paine14", NULL, 0}},
  {"ogre_paine14", {110, "ogre_paine15", NULL, 0}},
  {"ogre_paine15", {111, "ogre_run1", NULL, 0}},
  {"ogre_die1", {112, "ogre_die2", NULL, 0}},
  {"ogre_die2", {113, "ogre_die3", NULL, 0}},
  {"ogre_die3", {114, "ogre_die4", OPS_115, 2}},
  {"ogre_die4", {115, "ogre_die5", NULL, 0}},
  {"ogre_die5", {116, "ogre_die6", NULL, 0}},
  {"ogre_die6", {117, "ogre_die7", NULL, 0}},
  {"ogre_die7", {118, "ogre_die8", NULL, 0}},
  {"ogre_die8", {119, "ogre_die9", NULL, 0}},
  {"ogre_die9", {120, "ogre_die10", NULL, 0}},
  {"ogre_die10", {121, "ogre_die11", NULL, 0}},
  {"ogre_die11", {122, "ogre_die12", NULL, 0}},
  {"ogre_die12", {123, "ogre_die13", NULL, 0}},
  {"ogre_die13", {124, "ogre_die14", NULL, 0}},
  {"ogre_die14", {125, "ogre_die14", NULL, 0}},
  {"ogre_bdie1", {126, "ogre_bdie2", NULL, 0}},
  {"ogre_bdie2", {127, "ogre_bdie3", OPS_128, 1}},
  {"ogre_bdie3", {128, "ogre_bdie4", OPS_129, 2}},
  {"ogre_bdie4", {129, "ogre_bdie5", OPS_130, 1}},
  {"ogre_bdie5", {130, "ogre_bdie6", OPS_131, 1}},
  {"ogre_bdie6", {131, "ogre_bdie7", OPS_132, 1}},
  {"ogre_bdie7", {132, "ogre_bdie8", OPS_133, 1}},
  {"ogre_bdie8", {133, "ogre_bdie9", NULL, 0}},
  {"ogre_bdie9", {134, "ogre_bdie10", NULL, 0}},
  {"ogre_bdie10", {135, "ogre_bdie10", NULL, 0}},
  {"demon1_stand1", {0, "demon1_stand2", OPS_137, 1}},
  {"demon1_stand2", {1, "demon1_stand3", OPS_138, 1}},
  {"demon1_stand3", {2, "demon1_stand4", OPS_139, 1}},
  {"demon1_stand4", {3, "demon1_stand5", OPS_140, 1}},
  {"demon1_stand5", {4, "demon1_stand6", OPS_141, 1}},
  {"demon1_stand6", {5, "demon1_stand7", OPS_142, 1}},
  {"demon1_stand7", {6, "demon1_stand8", OPS_143, 1}},
  {"demon1_stand8", {7, "demon1_stand9", OPS_144, 1}},
  {"demon1_stand9", {8, "demon1_stand10", OPS_145, 1}},
  {"demon1_stand10", {9, "demon1_stand11", OPS_146, 1}},
  {"demon1_stand11", {10, "demon1_stand12", OPS_147, 1}},
  {"demon1_stand12", {11, "demon1_stand13", OPS_148, 1}},
  {"demon1_stand13", {12, "demon1_stand1", OPS_149, 1}},
  {"demon1_walk1", {13, "demon1_walk2", OPS_150, 2}},
  {"demon1_walk2", {14, "demon1_walk3", OPS_151, 1}},
  {"demon1_walk3", {15, "demon1_walk4", OPS_152, 1}},
  {"demon1_walk4", {16, "demon1_walk5", OPS_153, 1}},
  {"demon1_walk5", {17, "demon1_walk6", OPS_154, 1}},
  {"demon1_walk6", {18, "demon1_walk7", OPS_155, 1}},
  {"demon1_walk7", {19, "demon1_walk8", OPS_156, 1}},
  {"demon1_walk8", {20, "demon1_walk1", OPS_157, 1}},
  {"demon1_run1", {21, "demon1_run2", OPS_158, 2}},
  {"demon1_run2", {22, "demon1_run3", OPS_159, 1}},
  {"demon1_run3", {23, "demon1_run4", OPS_160, 1}},
  {"demon1_run4", {24, "demon1_run5", OPS_161, 1}},
  {"demon1_run5", {25, "demon1_run6", OPS_162, 1}},
  {"demon1_run6", {26, "demon1_run1", OPS_163, 1}},
  {"demon1_jump1", {27, "demon1_jump2", OPS_164, 1}},
  {"demon1_jump2", {28, "demon1_jump3", OPS_165, 1}},
  {"demon1_jump3", {29, "demon1_jump4", OPS_166, 1}},
  {"demon1_jump4", {30, "demon1_jump5", OPS_167, 2}},
  {"demon1_jump5", {31, "demon1_jump6", NULL, 0}},
  {"demon1_jump6", {32, "demon1_jump7", NULL, 0}},
  {"demon1_jump7", {33, "demon1_jump8", NULL, 0}},
  {"demon1_jump8", {34, "demon1_jump9", NULL, 0}},
  {"demon1_jump9", {35, "demon1_jump10", NULL, 0}},
  {"demon1_jump10", {36, "demon1_jump1", OPS_173, 1}},
  {"demon1_jump11", {37, "demon1_jump12", NULL, 0}},
  {"demon1_jump12", {38, "demon1_run1", NULL, 0}},
  {"demon1_atta1", {54, "demon1_atta2", OPS_176, 1}},
  {"demon1_atta2", {55, "demon1_atta3", OPS_177, 1}},
  {"demon1_atta3", {56, "demon1_atta4", OPS_178, 1}},
  {"demon1_atta4", {57, "demon1_atta5", OPS_179, 1}},
  {"demon1_atta5", {58, "demon1_atta6", OPS_180, 2}},
  {"demon1_atta6", {59, "demon1_atta7", OPS_181, 1}},
  {"demon1_atta7", {60, "demon1_atta8", OPS_182, 1}},
  {"demon1_atta8", {61, "demon1_atta9", OPS_183, 1}},
  {"demon1_atta9", {62, "demon1_atta10", OPS_184, 1}},
  {"demon1_atta10", {63, "demon1_atta11", OPS_185, 1}},
  {"demon1_atta11", {64, "demon1_atta12", OPS_186, 1}},
  {"demon1_atta12", {65, "demon1_atta13", OPS_187, 1}},
  {"demon1_atta13", {66, "demon1_atta14", OPS_188, 1}},
  {"demon1_atta14", {67, "demon1_atta15", OPS_189, 1}},
  {"demon1_atta15", {68, "demon1_run1", OPS_190, 1}},
  {"demon1_pain1", {39, "demon1_pain2", NULL, 0}},
  {"demon1_pain2", {40, "demon1_pain3", NULL, 0}},
  {"demon1_pain3", {41, "demon1_pain4", NULL, 0}},
  {"demon1_pain4", {42, "demon1_pain5", NULL, 0}},
  {"demon1_pain5", {43, "demon1_pain6", NULL, 0}},
  {"demon1_pain6", {44, "demon1_run1", NULL, 0}},
  {"demon1_die1", {45, "demon1_die2", OPS_197, 1}},
  {"demon1_die2", {46, "demon1_die3", NULL, 0}},
  {"demon1_die3", {47, "demon1_die4", NULL, 0}},
  {"demon1_die4", {48, "demon1_die5", NULL, 0}},
  {"demon1_die5", {49, "demon1_die6", NULL, 0}},
  {"demon1_die6", {50, "demon1_die7", OPS_202, 1}},
  {"demon1_die7", {51, "demon1_die8", NULL, 0}},
  {"demon1_die8", {52, "demon1_die9", NULL, 0}},
  {"demon1_die9", {53, "demon1_die9", NULL, 0}},
  {"sham_stand1", {0, "sham_stand2", OPS_206, 1}},
  {"sham_stand2", {1, "sham_stand3", OPS_207, 1}},
  {"sham_stand3", {2, "sham_stand4", OPS_208, 1}},
  {"sham_stand4", {3, "sham_stand5", OPS_209, 1}},
  {"sham_stand5", {4, "sham_stand6", OPS_210, 1}},
  {"sham_stand6", {5, "sham_stand7", OPS_211, 1}},
  {"sham_stand7", {6, "sham_stand8", OPS_212, 1}},
  {"sham_stand8", {7, "sham_stand9", OPS_213, 1}},
  {"sham_stand9", {8, "sham_stand10", OPS_214, 1}},
  {"sham_stand10", {9, "sham_stand11", OPS_215, 1}},
  {"sham_stand11", {10, "sham_stand12", OPS_216, 1}},
  {"sham_stand12", {11, "sham_stand13", OPS_217, 1}},
  {"sham_stand13", {12, "sham_stand14", OPS_218, 1}},
  {"sham_stand14", {13, "sham_stand15", OPS_219, 1}},
  {"sham_stand15", {14, "sham_stand16", OPS_220, 1}},
  {"sham_stand16", {15, "sham_stand17", OPS_221, 1}},
  {"sham_stand17", {16, "sham_stand1", OPS_222, 1}},
  {"sham_walk1", {17, "sham_walk2", OPS_223, 1}},
  {"sham_walk2", {18, "sham_walk3", OPS_224, 1}},
  {"sham_walk3", {19, "sham_walk4", OPS_225, 1}},
  {"sham_walk4", {20, "sham_walk5", OPS_226, 1}},
  {"sham_walk5", {21, "sham_walk6", OPS_227, 1}},
  {"sham_walk6", {22, "sham_walk7", OPS_228, 1}},
  {"sham_walk7", {23, "sham_walk8", OPS_229, 1}},
  {"sham_walk8", {24, "sham_walk9", OPS_230, 1}},
  {"sham_walk9", {25, "sham_walk10", OPS_231, 1}},
  {"sham_walk10", {26, "sham_walk11", OPS_232, 1}},
  {"sham_walk11", {27, "sham_walk12", OPS_233, 1}},
  {"sham_walk12", {28, "sham_walk1", OPS_234, 2}},
  {"sham_run1", {29, "sham_run2", OPS_235, 1}},
  {"sham_run2", {30, "sham_run3", OPS_236, 1}},
  {"sham_run3", {31, "sham_run4", OPS_237, 1}},
  {"sham_run4", {32, "sham_run5", OPS_238, 1}},
  {"sham_run5", {33, "sham_run6", OPS_239, 1}},
  {"sham_run6", {34, "sham_run1", OPS_240, 2}},
  {"sham_smash1", {35, "sham_smash2", OPS_241, 2}},
  {"sham_smash2", {36, "sham_smash3", OPS_242, 1}},
  {"sham_smash3", {37, "sham_smash4", OPS_243, 1}},
  {"sham_smash4", {38, "sham_smash5", OPS_244, 1}},
  {"sham_smash5", {39, "sham_smash6", OPS_245, 1}},
  {"sham_smash6", {40, "sham_smash7", OPS_246, 1}},
  {"sham_smash7", {41, "sham_smash8", OPS_247, 1}},
  {"sham_smash8", {42, "sham_smash9", OPS_248, 1}},
  {"sham_smash9", {43, "sham_smash10", OPS_249, 1}},
  {"sham_smash10", {44, "sham_smash11", OPS_250, 1}},
  {"sham_smash11", {45, "sham_smash12", OPS_251, 1}},
  {"sham_smash12", {46, "sham_run1", OPS_252, 1}},
  {"sham_swingl1", {56, "sham_swingl2", OPS_253, 2}},
  {"sham_swingl2", {57, "sham_swingl3", OPS_254, 1}},
  {"sham_swingl3", {58, "sham_swingl4", OPS_255, 1}},
  {"sham_swingl4", {59, "sham_swingl5", OPS_256, 1}},
  {"sham_swingl5", {60, "sham_swingl6", OPS_257, 1}},
  {"sham_swingl6", {61, "sham_swingl7", OPS_258, 1}},
  {"sham_swingl7", {62, "sham_swingl8", OPS_259, 2}},
  {"sham_swingl8", {63, "sham_swingl9", OPS_260, 1}},
  {"sham_swingl9", {64, "sham_run1", OPS_261, 2}},
  {"sham_swingr1", {47, "sham_swingr2", OPS_262, 2}},
  {"sham_swingr2", {48, "sham_swingr3", OPS_263, 1}},
  {"sham_swingr3", {49, "sham_swingr4", OPS_264, 1}},
  {"sham_swingr4", {50, "sham_swingr5", OPS_265, 1}},
  {"sham_swingr5", {51, "sham_swingr6", OPS_266, 1}},
  {"sham_swingr6", {52, "sham_swingr7", OPS_267, 1}},
  {"sham_swingr7", {53, "sham_swingr8", OPS_268, 2}},
  {"sham_swingr8", {54, "sham_swingr9", OPS_269, 1}},
  {"sham_swingr9", {55, "sham_run1", OPS_270, 3}},
  {"sham_magic1", {65, "sham_magic2", OPS_271, 2}},
  {"sham_magic2", {66, "sham_magic3", OPS_272, 1}},
  {"sham_magic3", {67, "sham_magic4", OPS_273, 2}},
  {"sham_magic4", {68, "sham_magic5", OPS_274, 1}},
  {"sham_magic5", {69, "sham_magic6", OPS_275, 1}},
  {"sham_magic6", {70, "sham_magic9", OPS_276, 1}},
  {"sham_magic9", {73, "sham_magic10", OPS_277, 1}},
  {"sham_magic10", {74, "sham_magic11", OPS_278, 1}},
  {"sham_magic11", {75, "sham_magic12", NULL, 0}},
  {"sham_magic12", {76, "sham_run1", NULL, 0}},
  {"sham_pain1", {77, "sham_pain2", NULL, 0}},
  {"sham_pain2", {78, "sham_pain3", NULL, 0}},
  {"sham_pain3", {79, "sham_pain4", NULL, 0}},
  {"sham_pain4", {80, "sham_pain5", NULL, 0}},
  {"sham_pain5", {81, "sham_pain6", NULL, 0}},
  {"sham_pain6", {82, "sham_run1", NULL, 0}},
  {"sham_death1", {83, "sham_death2", NULL, 0}},
  {"sham_death2", {84, "sham_death3", NULL, 0}},
  {"sham_death3", {85, "sham_death4", OPS_289, 1}},
  {"sham_death4", {86, "sham_death5", NULL, 0}},
  {"sham_death5", {87, "sham_death6", NULL, 0}},
  {"sham_death6", {88, "sham_death7", NULL, 0}},
  {"sham_death7", {89, "sham_death8", NULL, 0}},
  {"sham_death8", {90, "sham_death9", NULL, 0}},
  {"sham_death9", {91, "sham_death10", NULL, 0}},
  {"sham_death10", {92, "sham_death11", NULL, 0}},
  {"sham_death11", {93, "sham_death11", NULL, 0}},
  {"knight_stand1", {0, "knight_stand2", OPS_298, 1}},
  {"knight_stand2", {1, "knight_stand3", OPS_299, 1}},
  {"knight_stand3", {2, "knight_stand4", OPS_300, 1}},
  {"knight_stand4", {3, "knight_stand5", OPS_301, 1}},
  {"knight_stand5", {4, "knight_stand6", OPS_302, 1}},
  {"knight_stand6", {5, "knight_stand7", OPS_303, 1}},
  {"knight_stand7", {6, "knight_stand8", OPS_304, 1}},
  {"knight_stand8", {7, "knight_stand9", OPS_305, 1}},
  {"knight_stand9", {8, "knight_stand1", OPS_306, 1}},
  {"knight_walk1", {53, "knight_walk2", OPS_307, 2}},
  {"knight_walk2", {54, "knight_walk3", OPS_308, 1}},
  {"knight_walk3", {55, "knight_walk4", OPS_309, 1}},
  {"knight_walk4", {56, "knight_walk5", OPS_310, 1}},
  {"knight_walk5", {57, "knight_walk6", OPS_311, 1}},
  {"knight_walk6", {58, "knight_walk7", OPS_312, 1}},
  {"knight_walk7", {59, "knight_walk8", OPS_313, 1}},
  {"knight_walk8", {60, "knight_walk9", OPS_314, 1}},
  {"knight_walk9", {61, "knight_walk10", OPS_315, 1}},
  {"knight_walk10", {62, "knight_walk11", OPS_316, 1}},
  {"knight_walk11", {63, "knight_walk12", OPS_317, 1}},
  {"knight_walk12", {64, "knight_walk13", OPS_318, 1}},
  {"knight_walk13", {65, "knight_walk14", OPS_319, 1}},
  {"knight_walk14", {66, "knight_walk1", OPS_320, 1}},
  {"knight_run1", {9, "knight_run2", OPS_321, 2}},
  {"knight_run2", {10, "knight_run3", OPS_322, 1}},
  {"knight_run3", {11, "knight_run4", OPS_323, 1}},
  {"knight_run4", {12, "knight_run5", OPS_324, 1}},
  {"knight_run5", {13, "knight_run6", OPS_325, 1}},
  {"knight_run6", {14, "knight_run7", OPS_326, 1}},
  {"knight_run7", {15, "knight_run8", OPS_327, 1}},
  {"knight_run8", {16, "knight_run1", OPS_328, 1}},
  {"knight_runatk1", {17, "knight_runatk2", OPS_329, 1}},
  {"knight_runatk2", {18, "knight_runatk3", OPS_330, 1}},
  {"knight_runatk3", {19, "knight_runatk4", OPS_331, 1}},
  {"knight_runatk4", {20, "knight_runatk5", OPS_332, 1}},
  {"knight_runatk5", {21, "knight_runatk6", OPS_333, 1}},
  {"knight_runatk6", {22, "knight_runatk7", OPS_334, 1}},
  {"knight_runatk7", {23, "knight_runatk8", OPS_335, 1}},
  {"knight_runatk8", {24, "knight_runatk9", OPS_336, 1}},
  {"knight_runatk9", {25, "knight_runatk10", OPS_337, 1}},
  {"knight_runatk10", {26, "knight_runatk11", OPS_338, 1}},
  {"knight_runatk11", {27, "knight_run1", OPS_339, 1}},
  {"knight_atk1", {42, "knight_atk2", OPS_340, 2}},
  {"knight_atk2", {43, "knight_atk3", OPS_341, 1}},
  {"knight_atk3", {44, "knight_atk4", OPS_342, 1}},
  {"knight_atk4", {45, "knight_atk5", OPS_343, 1}},
  {"knight_atk5", {46, "knight_atk6", OPS_344, 1}},
  {"knight_atk6", {47, "knight_atk7", OPS_345, 2}},
  {"knight_atk7", {48, "knight_atk8", OPS_346, 2}},
  {"knight_atk8", {49, "knight_atk9", OPS_347, 2}},
  {"knight_atk9", {50, "knight_atk10", OPS_348, 1}},
  {"knight_atk10", {51, "knight_run1", OPS_349, 1}},
  {"knight_pain1", {28, "knight_pain2", NULL, 0}},
  {"knight_pain2", {29, "knight_pain3", NULL, 0}},
  {"knight_pain3", {30, "knight_run1", NULL, 0}},
  {"knight_painb1", {31, "knight_painb2", OPS_353, 1}},
  {"knight_painb2", {32, "knight_painb3", OPS_354, 1}},
  {"knight_painb3", {33, "knight_painb4", NULL, 0}},
  {"knight_painb4", {34, "knight_painb5", NULL, 0}},
  {"knight_painb5", {35, "knight_painb6", OPS_357, 1}},
  {"knight_painb6", {36, "knight_painb7", OPS_358, 1}},
  {"knight_painb7", {37, "knight_painb8", OPS_359, 1}},
  {"knight_painb8", {38, "knight_painb9", OPS_360, 1}},
  {"knight_painb9", {39, "knight_painb10", OPS_361, 1}},
  {"knight_painb10", {40, "knight_painb11", OPS_362, 1}},
  {"knight_painb11", {41, "knight_run1", NULL, 0}},
  {"knight_bow1", {67, "knight_bow2", OPS_364, 1}},
  {"knight_bow2", {68, "knight_bow3", OPS_365, 1}},
  {"knight_bow3", {69, "knight_bow4", OPS_366, 1}},
  {"knight_bow4", {70, "knight_bow5", OPS_367, 1}},
  {"knight_bow5", {71, "knight_bow5", OPS_368, 1}},
  {"knight_bow6", {70, "knight_bow7", OPS_369, 1}},
  {"knight_bow7", {69, "knight_bow8", OPS_370, 1}},
  {"knight_bow8", {68, "knight_bow9", OPS_371, 1}},
  {"knight_bow9", {67, "knight_bow10", OPS_372, 1}},
  {"knight_bow10", {53, "knight_walk1", OPS_373, 1}},
  {"knight_die1", {76, "knight_die2", NULL, 0}},
  {"knight_die2", {77, "knight_die3", NULL, 0}},
  {"knight_die3", {78, "knight_die4", OPS_376, 1}},
  {"knight_die4", {79, "knight_die5", NULL, 0}},
  {"knight_die5", {80, "knight_die6", NULL, 0}},
  {"knight_die6", {81, "knight_die7", NULL, 0}},
  {"knight_die7", {82, "knight_die8", NULL, 0}},
  {"knight_die8", {83, "knight_die9", NULL, 0}},
  {"knight_die9", {84, "knight_die10", NULL, 0}},
  {"knight_die10", {85, "knight_die10", NULL, 0}},
  {"knight_dieb1", {86, "knight_dieb2", NULL, 0}},
  {"knight_dieb2", {87, "knight_dieb3", NULL, 0}},
  {"knight_dieb3", {88, "knight_dieb4", OPS_386, 1}},
  {"knight_dieb4", {89, "knight_dieb5", NULL, 0}},
  {"knight_dieb5", {90, "knight_dieb6", NULL, 0}},
  {"knight_dieb6", {91, "knight_dieb7", NULL, 0}},
  {"knight_dieb7", {92, "knight_dieb8", NULL, 0}},
  {"knight_dieb8", {93, "knight_dieb9", NULL, 0}},
  {"knight_dieb9", {94, "knight_dieb10", NULL, 0}},
  {"knight_dieb10", {95, "knight_dieb11", NULL, 0}},
  {"knight_dieb11", {96, "knight_dieb11", NULL, 0}},
  {"army_stand1", {0, "army_stand2", OPS_395, 1}},
  {"army_stand2", {1, "army_stand3", OPS_396, 1}},
  {"army_stand3", {2, "army_stand4", OPS_397, 1}},
  {"army_stand4", {3, "army_stand5", OPS_398, 1}},
  {"army_stand5", {4, "army_stand6", OPS_399, 1}},
  {"army_stand6", {5, "army_stand7", OPS_400, 1}},
  {"army_stand7", {6, "army_stand8", OPS_401, 1}},
  {"army_stand8", {7, "army_stand1", OPS_402, 1}},
  {"army_walk1", {90, "army_walk2", OPS_403, 2}},
  {"army_walk2", {91, "army_walk3", OPS_404, 1}},
  {"army_walk3", {92, "army_walk4", OPS_405, 1}},
  {"army_walk4", {93, "army_walk5", OPS_406, 1}},
  {"army_walk5", {94, "army_walk6", OPS_407, 1}},
  {"army_walk6", {95, "army_walk7", OPS_408, 1}},
  {"army_walk7", {96, "army_walk8", OPS_409, 1}},
  {"army_walk8", {97, "army_walk9", OPS_410, 1}},
  {"army_walk9", {98, "army_walk10", OPS_411, 1}},
  {"army_walk10", {99, "army_walk11", OPS_412, 1}},
  {"army_walk11", {100, "army_walk12", OPS_413, 1}},
  {"army_walk12", {101, "army_walk13", OPS_414, 1}},
  {"army_walk13", {102, "army_walk14", OPS_415, 1}},
  {"army_walk14", {103, "army_walk15", OPS_416, 1}},
  {"army_walk15", {104, "army_walk16", OPS_417, 1}},
  {"army_walk16", {105, "army_walk17", OPS_418, 1}},
  {"army_walk17", {106, "army_walk18", OPS_419, 1}},
  {"army_walk18", {107, "army_walk19", OPS_420, 1}},
  {"army_walk19", {108, "army_walk20", OPS_421, 1}},
  {"army_walk20", {109, "army_walk21", OPS_422, 1}},
  {"army_walk21", {110, "army_walk22", OPS_423, 1}},
  {"army_walk22", {111, "army_walk23", OPS_424, 1}},
  {"army_walk23", {112, "army_walk24", OPS_425, 1}},
  {"army_walk24", {113, "army_walk1", OPS_426, 1}},
  {"army_run1", {73, "army_run2", OPS_427, 2}},
  {"army_run2", {74, "army_run3", OPS_428, 1}},
  {"army_run3", {75, "army_run4", OPS_429, 1}},
  {"army_run4", {76, "army_run5", OPS_430, 1}},
  {"army_run5", {77, "army_run6", OPS_431, 1}},
  {"army_run6", {78, "army_run7", OPS_432, 1}},
  {"army_run7", {79, "army_run8", OPS_433, 1}},
  {"army_run8", {80, "army_run1", OPS_434, 1}},
  {"army_atk1", {81, "army_atk2", OPS_435, 1}},
  {"army_atk2", {82, "army_atk3", OPS_436, 1}},
  {"army_atk3", {83, "army_atk4", OPS_437, 1}},
  {"army_atk4", {84, "army_atk5", OPS_438, 1}},
  {"army_atk5", {85, "army_atk6", OPS_439, 2}},
  {"army_atk6", {86, "army_atk7", OPS_440, 1}},
  {"army_atk7", {87, "army_atk8", OPS_441, 2}},
  {"army_atk8", {88, "army_atk9", OPS_442, 1}},
  {"army_atk9", {89, "army_run1", OPS_443, 1}},
  {"army_pain1", {40, "army_pain2", NULL, 0}},
  {"army_pain2", {41, "army_pain3", NULL, 0}},
  {"army_pain3", {42, "army_pain4", NULL, 0}},
  {"army_pain4", {43, "army_pain5", NULL, 0}},
  {"army_pain5", {44, "army_pain6", NULL, 0}},
  {"army_pain6", {45, "army_run1", OPS_449, 1}},
  {"army_painb1", {46, "army_painb2", NULL, 0}},
  {"army_painb2", {47, "army_painb3", OPS_451, 1}},
  {"army_painb3", {48, "army_painb4", OPS_452, 1}},
  {"army_painb4", {49, "army_painb5", NULL, 0}},
  {"army_painb5", {50, "army_painb6", NULL, 0}},
  {"army_painb6", {51, "army_painb7", NULL, 0}},
  {"army_painb7", {52, "army_painb8", NULL, 0}},
  {"army_painb8", {53, "army_painb9", NULL, 0}},
  {"army_painb9", {54, "army_painb10", NULL, 0}},
  {"army_painb10", {55, "army_painb11", NULL, 0}},
  {"army_painb11", {56, "army_painb12", NULL, 0}},
  {"army_painb12", {57, "army_painb13", OPS_461, 1}},
  {"army_painb13", {58, "army_painb14", NULL, 0}},
  {"army_painb14", {59, "army_run1", NULL, 0}},
  {"army_painc1", {60, "army_painc2", NULL, 0}},
  {"army_painc2", {61, "army_painc3", OPS_465, 1}},
  {"army_painc3", {62, "army_painc4", NULL, 0}},
  {"army_painc4", {63, "army_painc5", NULL, 0}},
  {"army_painc5", {64, "army_painc6", OPS_468, 1}},
  {"army_painc6", {65, "army_painc7", OPS_469, 1}},
  {"army_painc7", {66, "army_painc8", NULL, 0}},
  {"army_painc8", {67, "army_painc9", OPS_471, 1}},
  {"army_painc9", {68, "army_painc10", OPS_472, 1}},
  {"army_painc10", {69, "army_painc11", OPS_473, 1}},
  {"army_painc11", {70, "army_painc12", OPS_474, 1}},
  {"army_painc12", {71, "army_painc13", OPS_475, 1}},
  {"army_painc13", {72, "army_run1", NULL, 0}},
  {"army_die1", {8, "army_die2", NULL, 0}},
  {"army_die2", {9, "army_die3", NULL, 0}},
  {"army_die3", {10, "army_die4", OPS_479, 2}},
  {"army_die4", {11, "army_die5", NULL, 0}},
  {"army_die5", {12, "army_die6", NULL, 0}},
  {"army_die6", {13, "army_die7", NULL, 0}},
  {"army_die7", {14, "army_die8", NULL, 0}},
  {"army_die8", {15, "army_die9", NULL, 0}},
  {"army_die9", {16, "army_die10", NULL, 0}},
  {"army_die10", {17, "army_die10", NULL, 0}},
  {"army_cdie1", {18, "army_cdie2", NULL, 0}},
  {"army_cdie2", {19, "army_cdie3", OPS_488, 1}},
  {"army_cdie3", {20, "army_cdie4", OPS_489, 2}},
  {"army_cdie4", {21, "army_cdie5", OPS_490, 1}},
  {"army_cdie5", {22, "army_cdie6", OPS_491, 1}},
  {"army_cdie6", {23, "army_cdie7", OPS_492, 1}},
  {"army_cdie7", {24, "army_cdie8", NULL, 0}},
  {"army_cdie8", {25, "army_cdie9", NULL, 0}},
  {"army_cdie9", {26, "army_cdie10", NULL, 0}},
  {"army_cdie10", {27, "army_cdie11", NULL, 0}},
  {"army_cdie11", {28, "army_cdie11", NULL, 0}},
  {"wiz_stand1", {0, "wiz_stand2", OPS_498, 1}},
  {"wiz_stand2", {1, "wiz_stand3", OPS_499, 1}},
  {"wiz_stand3", {2, "wiz_stand4", OPS_500, 1}},
  {"wiz_stand4", {3, "wiz_stand5", OPS_501, 1}},
  {"wiz_stand5", {4, "wiz_stand6", OPS_502, 1}},
  {"wiz_stand6", {5, "wiz_stand7", OPS_503, 1}},
  {"wiz_stand7", {6, "wiz_stand8", OPS_504, 1}},
  {"wiz_stand8", {7, "wiz_stand1", OPS_505, 1}},
  {"wiz_walk1", {0, "wiz_walk2", OPS_506, 2}},
  {"wiz_walk2", {1, "wiz_walk3", OPS_507, 1}},
  {"wiz_walk3", {2, "wiz_walk4", OPS_508, 1}},
  {"wiz_walk4", {3, "wiz_walk5", OPS_509, 1}},
  {"wiz_walk5", {4, "wiz_walk6", OPS_510, 1}},
  {"wiz_walk6", {5, "wiz_walk7", OPS_511, 1}},
  {"wiz_walk7", {6, "wiz_walk8", OPS_512, 1}},
  {"wiz_walk8", {7, "wiz_walk1", OPS_513, 1}},
  {"wiz_side1", {0, "wiz_side2", OPS_514, 2}},
  {"wiz_side2", {1, "wiz_side3", OPS_515, 1}},
  {"wiz_side3", {2, "wiz_side4", OPS_516, 1}},
  {"wiz_side4", {3, "wiz_side5", OPS_517, 1}},
  {"wiz_side5", {4, "wiz_side6", OPS_518, 1}},
  {"wiz_side6", {5, "wiz_side7", OPS_519, 1}},
  {"wiz_side7", {6, "wiz_side8", OPS_520, 1}},
  {"wiz_side8", {7, "wiz_side1", OPS_521, 1}},
  {"wiz_run1", {15, "wiz_run2", OPS_522, 2}},
  {"wiz_run2", {16, "wiz_run3", OPS_523, 1}},
  {"wiz_run3", {17, "wiz_run4", OPS_524, 1}},
  {"wiz_run4", {18, "wiz_run5", OPS_525, 1}},
  {"wiz_run5", {19, "wiz_run6", OPS_526, 1}},
  {"wiz_run6", {20, "wiz_run7", OPS_527, 1}},
  {"wiz_run7", {21, "wiz_run8", OPS_528, 1}},
  {"wiz_run8", {22, "wiz_run9", OPS_529, 1}},
  {"wiz_run9", {23, "wiz_run10", OPS_530, 1}},
  {"wiz_run10", {24, "wiz_run11", OPS_531, 1}},
  {"wiz_run11", {25, "wiz_run12", OPS_532, 1}},
  {"wiz_run12", {26, "wiz_run13", OPS_533, 1}},
  {"wiz_run13", {27, "wiz_run14", OPS_534, 1}},
  {"wiz_run14", {28, "wiz_run1", OPS_535, 1}},
  {"wiz_fast1", {29, "wiz_fast2", OPS_536, 2}},
  {"wiz_fast2", {30, "wiz_fast3", OPS_537, 1}},
  {"wiz_fast3", {31, "wiz_fast4", OPS_538, 1}},
  {"wiz_fast4", {32, "wiz_fast5", OPS_539, 1}},
  {"wiz_fast5", {33, "wiz_fast6", OPS_540, 1}},
  {"wiz_fast6", {34, "wiz_fast7", OPS_541, 1}},
  {"wiz_fast7", {33, "wiz_fast8", OPS_542, 1}},
  {"wiz_fast8", {32, "wiz_fast9", OPS_543, 1}},
  {"wiz_fast9", {31, "wiz_fast10", OPS_544, 1}},
  {"wiz_fast10", {30, "wiz_run1", OPS_545, 2}},
  {"wiz_pain1", {42, "wiz_pain2", NULL, 0}},
  {"wiz_pain2", {43, "wiz_pain3", NULL, 0}},
  {"wiz_pain3", {44, "wiz_pain4", NULL, 0}},
  {"wiz_pain4", {45, "wiz_run1", NULL, 0}},
  {"wiz_death1", {46, "wiz_death2", OPS_550, 1}},
  {"wiz_death2", {47, "wiz_death3", NULL, 0}},
  {"wiz_death3", {48, "wiz_death4", OPS_552, 1}},
  {"wiz_death4", {49, "wiz_death5", NULL, 0}},
  {"wiz_death5", {50, "wiz_death6", NULL, 0}},
  {"wiz_death6", {51, "wiz_death7", NULL, 0}},
  {"wiz_death7", {52, "wiz_death8", NULL, 0}},
  {"wiz_death8", {53, "wiz_death8", NULL, 0}},
  {"dog_stand1", {69, "dog_stand2", OPS_558, 1}},
  {"dog_stand2", {70, "dog_stand3", OPS_559, 1}},
  {"dog_stand3", {71, "dog_stand4", OPS_560, 1}},
  {"dog_stand4", {72, "dog_stand5", OPS_561, 1}},
  {"dog_stand5", {73, "dog_stand6", OPS_562, 1}},
  {"dog_stand6", {74, "dog_stand7", OPS_563, 1}},
  {"dog_stand7", {75, "dog_stand8", OPS_564, 1}},
  {"dog_stand8", {76, "dog_stand9", OPS_565, 1}},
  {"dog_stand9", {77, "dog_stand1", OPS_566, 1}},
  {"dog_walk1", {78, "dog_walk2", OPS_567, 2}},
  {"dog_walk2", {79, "dog_walk3", OPS_568, 1}},
  {"dog_walk3", {80, "dog_walk4", OPS_569, 1}},
  {"dog_walk4", {81, "dog_walk5", OPS_570, 1}},
  {"dog_walk5", {82, "dog_walk6", OPS_571, 1}},
  {"dog_walk6", {83, "dog_walk7", OPS_572, 1}},
  {"dog_walk7", {84, "dog_walk8", OPS_573, 1}},
  {"dog_walk8", {85, "dog_walk1", OPS_574, 1}},
  {"dog_run1", {48, "dog_run2", OPS_575, 2}},
  {"dog_run2", {49, "dog_run3", OPS_576, 1}},
  {"dog_run3", {50, "dog_run4", OPS_577, 1}},
  {"dog_run4", {51, "dog_run5", OPS_578, 1}},
  {"dog_run5", {52, "dog_run6", OPS_579, 1}},
  {"dog_run6", {53, "dog_run7", OPS_580, 1}},
  {"dog_run7", {54, "dog_run8", OPS_581, 1}},
  {"dog_run8", {55, "dog_run9", OPS_582, 1}},
  {"dog_run9", {56, "dog_run10", OPS_583, 1}},
  {"dog_run10", {57, "dog_run11", OPS_584, 1}},
  {"dog_run11", {58, "dog_run12", OPS_585, 1}},
  {"dog_run12", {59, "dog_run1", OPS_586, 1}},
  {"dog_atta1", {0, "dog_atta2", OPS_587, 1}},
  {"dog_atta2", {1, "dog_atta3", OPS_588, 1}},
  {"dog_atta3", {2, "dog_atta4", OPS_589, 1}},
  {"dog_atta4", {3, "dog_atta5", OPS_590, 2}},
  {"dog_atta5", {4, "dog_atta6", OPS_591, 1}},
  {"dog_atta6", {5, "dog_atta7", OPS_592, 1}},
  {"dog_atta7", {6, "dog_atta8", OPS_593, 1}},
  {"dog_atta8", {7, "dog_run1", OPS_594, 1}},
  {"dog_leap1", {60, "dog_leap2", OPS_595, 1}},
  {"dog_leap2", {61, "dog_leap3", OPS_596, 2}},
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
  {"dog_painb3", {34, "dog_painb4", OPS_612, 1}},
  {"dog_painb4", {35, "dog_painb5", OPS_613, 1}},
  {"dog_painb5", {36, "dog_painb6", OPS_614, 1}},
  {"dog_painb6", {37, "dog_painb7", OPS_615, 1}},
  {"dog_painb7", {38, "dog_painb8", NULL, 0}},
  {"dog_painb8", {39, "dog_painb9", OPS_617, 1}},
  {"dog_painb9", {40, "dog_painb10", NULL, 0}},
  {"dog_painb10", {41, "dog_painb11", OPS_619, 1}},
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
  {"zombie_stand1", {0, "zombie_stand2", OPS_644, 1}},
  {"zombie_stand2", {1, "zombie_stand3", OPS_645, 1}},
  {"zombie_stand3", {2, "zombie_stand4", OPS_646, 1}},
  {"zombie_stand4", {3, "zombie_stand5", OPS_647, 1}},
  {"zombie_stand5", {4, "zombie_stand6", OPS_648, 1}},
  {"zombie_stand6", {5, "zombie_stand7", OPS_649, 1}},
  {"zombie_stand7", {6, "zombie_stand8", OPS_650, 1}},
  {"zombie_stand8", {7, "zombie_stand9", OPS_651, 1}},
  {"zombie_stand9", {8, "zombie_stand10", OPS_652, 1}},
  {"zombie_stand10", {9, "zombie_stand11", OPS_653, 1}},
  {"zombie_stand11", {10, "zombie_stand12", OPS_654, 1}},
  {"zombie_stand12", {11, "zombie_stand13", OPS_655, 1}},
  {"zombie_stand13", {12, "zombie_stand14", OPS_656, 1}},
  {"zombie_stand14", {13, "zombie_stand15", OPS_657, 1}},
  {"zombie_stand15", {14, "zombie_stand1", OPS_658, 1}},
  {"zombie_cruc1", {192, "zombie_cruc2", OPS_659, 1}},
  {"zombie_cruc2", {193, "zombie_cruc3", OPS_660, 1}},
  {"zombie_cruc3", {194, "zombie_cruc4", OPS_661, 1}},
  {"zombie_cruc4", {195, "zombie_cruc5", OPS_662, 1}},
  {"zombie_cruc5", {196, "zombie_cruc6", OPS_663, 1}},
  {"zombie_cruc6", {197, "zombie_cruc1", OPS_664, 1}},
  {"zombie_walk1", {15, "zombie_walk2", OPS_665, 1}},
  {"zombie_walk2", {16, "zombie_walk3", OPS_666, 1}},
  {"zombie_walk3", {17, "zombie_walk4", OPS_667, 1}},
  {"zombie_walk4", {18, "zombie_walk5", OPS_668, 1}},
  {"zombie_walk5", {19, "zombie_walk6", OPS_669, 1}},
  {"zombie_walk6", {20, "zombie_walk7", OPS_670, 1}},
  {"zombie_walk7", {21, "zombie_walk8", OPS_671, 1}},
  {"zombie_walk8", {22, "zombie_walk9", OPS_672, 1}},
  {"zombie_walk9", {23, "zombie_walk10", OPS_673, 1}},
  {"zombie_walk10", {24, "zombie_walk11", OPS_674, 1}},
  {"zombie_walk11", {25, "zombie_walk12", OPS_675, 1}},
  {"zombie_walk12", {26, "zombie_walk13", OPS_676, 1}},
  {"zombie_walk13", {27, "zombie_walk14", OPS_677, 1}},
  {"zombie_walk14", {28, "zombie_walk15", OPS_678, 1}},
  {"zombie_walk15", {29, "zombie_walk16", OPS_679, 1}},
  {"zombie_walk16", {30, "zombie_walk17", OPS_680, 1}},
  {"zombie_walk17", {31, "zombie_walk18", OPS_681, 1}},
  {"zombie_walk18", {32, "zombie_walk19", OPS_682, 1}},
  {"zombie_walk19", {33, "zombie_walk1", OPS_683, 2}},
  {"zombie_run1", {34, "zombie_run2", OPS_684, 2}},
  {"zombie_run2", {35, "zombie_run3", OPS_685, 1}},
  {"zombie_run3", {36, "zombie_run4", OPS_686, 1}},
  {"zombie_run4", {37, "zombie_run5", OPS_687, 1}},
  {"zombie_run5", {38, "zombie_run6", OPS_688, 1}},
  {"zombie_run6", {39, "zombie_run7", OPS_689, 1}},
  {"zombie_run7", {40, "zombie_run8", OPS_690, 1}},
  {"zombie_run8", {41, "zombie_run9", OPS_691, 1}},
  {"zombie_run9", {42, "zombie_run10", OPS_692, 1}},
  {"zombie_run10", {43, "zombie_run11", OPS_693, 1}},
  {"zombie_run11", {44, "zombie_run12", OPS_694, 1}},
  {"zombie_run12", {45, "zombie_run13", OPS_695, 1}},
  {"zombie_run13", {46, "zombie_run14", OPS_696, 1}},
  {"zombie_run14", {47, "zombie_run15", OPS_697, 1}},
  {"zombie_run15", {48, "zombie_run16", OPS_698, 1}},
  {"zombie_run16", {49, "zombie_run17", OPS_699, 1}},
  {"zombie_run17", {50, "zombie_run18", OPS_700, 1}},
  {"zombie_run18", {51, "zombie_run1", OPS_701, 3}},
  {"zombie_atta1", {52, "zombie_atta2", OPS_702, 1}},
  {"zombie_atta2", {53, "zombie_atta3", OPS_703, 1}},
  {"zombie_atta3", {54, "zombie_atta4", OPS_704, 1}},
  {"zombie_atta4", {55, "zombie_atta5", OPS_705, 1}},
  {"zombie_atta5", {56, "zombie_atta6", OPS_706, 1}},
  {"zombie_atta6", {57, "zombie_atta7", OPS_707, 1}},
  {"zombie_atta7", {58, "zombie_atta8", OPS_708, 1}},
  {"zombie_atta8", {59, "zombie_atta9", OPS_709, 1}},
  {"zombie_atta9", {60, "zombie_atta10", OPS_710, 1}},
  {"zombie_atta10", {61, "zombie_atta11", OPS_711, 1}},
  {"zombie_atta11", {62, "zombie_atta12", OPS_712, 1}},
  {"zombie_atta12", {63, "zombie_atta13", OPS_713, 1}},
  {"zombie_atta13", {64, "zombie_run1", OPS_714, 2}},
  {"zombie_attb1", {65, "zombie_attb2", OPS_715, 1}},
  {"zombie_attb2", {66, "zombie_attb3", OPS_716, 1}},
  {"zombie_attb3", {67, "zombie_attb4", OPS_717, 1}},
  {"zombie_attb4", {68, "zombie_attb5", OPS_718, 1}},
  {"zombie_attb5", {69, "zombie_attb6", OPS_719, 1}},
  {"zombie_attb6", {70, "zombie_attb7", OPS_720, 1}},
  {"zombie_attb7", {71, "zombie_attb8", OPS_721, 1}},
  {"zombie_attb8", {72, "zombie_attb9", OPS_722, 1}},
  {"zombie_attb9", {73, "zombie_attb10", OPS_723, 1}},
  {"zombie_attb10", {74, "zombie_attb11", OPS_724, 1}},
  {"zombie_attb11", {75, "zombie_attb12", OPS_725, 1}},
  {"zombie_attb12", {76, "zombie_attb13", OPS_726, 1}},
  {"zombie_attb13", {77, "zombie_attb14", OPS_727, 1}},
  {"zombie_attb14", {77, "zombie_run1", OPS_728, 2}},
  {"zombie_attc1", {79, "zombie_attc2", OPS_729, 1}},
  {"zombie_attc2", {80, "zombie_attc3", OPS_730, 1}},
  {"zombie_attc3", {81, "zombie_attc4", OPS_731, 1}},
  {"zombie_attc4", {82, "zombie_attc5", OPS_732, 1}},
  {"zombie_attc5", {83, "zombie_attc6", OPS_733, 1}},
  {"zombie_attc6", {84, "zombie_attc7", OPS_734, 1}},
  {"zombie_attc7", {85, "zombie_attc8", OPS_735, 1}},
  {"zombie_attc8", {86, "zombie_attc9", OPS_736, 1}},
  {"zombie_attc9", {87, "zombie_attc10", OPS_737, 1}},
  {"zombie_attc10", {88, "zombie_attc11", OPS_738, 1}},
  {"zombie_attc11", {89, "zombie_attc12", OPS_739, 1}},
  {"zombie_attc12", {90, "zombie_run1", OPS_740, 2}},
  {"zombie_paina1", {91, "zombie_paina2", OPS_741, 1}},
  {"zombie_paina2", {92, "zombie_paina3", OPS_742, 1}},
  {"zombie_paina3", {93, "zombie_paina4", OPS_743, 1}},
  {"zombie_paina4", {94, "zombie_paina5", OPS_744, 1}},
  {"zombie_paina5", {95, "zombie_paina6", OPS_745, 1}},
  {"zombie_paina6", {96, "zombie_paina7", OPS_746, 1}},
  {"zombie_paina7", {97, "zombie_paina8", NULL, 0}},
  {"zombie_paina8", {98, "zombie_paina9", NULL, 0}},
  {"zombie_paina9", {99, "zombie_paina10", NULL, 0}},
  {"zombie_paina10", {100, "zombie_paina11", NULL, 0}},
  {"zombie_paina11", {101, "zombie_paina12", NULL, 0}},
  {"zombie_paina12", {102, "zombie_run1", NULL, 0}},
  {"zombie_painb1", {103, "zombie_painb2", OPS_753, 1}},
  {"zombie_painb2", {104, "zombie_painb3", OPS_754, 1}},
  {"zombie_painb3", {105, "zombie_painb4", OPS_755, 1}},
  {"zombie_painb4", {106, "zombie_painb5", OPS_756, 1}},
  {"zombie_painb5", {107, "zombie_painb6", OPS_757, 1}},
  {"zombie_painb6", {108, "zombie_painb7", NULL, 0}},
  {"zombie_painb7", {109, "zombie_painb8", NULL, 0}},
  {"zombie_painb8", {110, "zombie_painb9", NULL, 0}},
  {"zombie_painb9", {111, "zombie_painb10", OPS_761, 1}},
  {"zombie_painb10", {112, "zombie_painb11", NULL, 0}},
  {"zombie_painb11", {113, "zombie_painb12", NULL, 0}},
  {"zombie_painb12", {114, "zombie_painb13", NULL, 0}},
  {"zombie_painb13", {115, "zombie_painb14", NULL, 0}},
  {"zombie_painb14", {116, "zombie_painb15", NULL, 0}},
  {"zombie_painb15", {117, "zombie_painb16", NULL, 0}},
  {"zombie_painb16", {118, "zombie_painb17", NULL, 0}},
  {"zombie_painb17", {119, "zombie_painb18", NULL, 0}},
  {"zombie_painb18", {120, "zombie_painb19", NULL, 0}},
  {"zombie_painb19", {121, "zombie_painb20", NULL, 0}},
  {"zombie_painb20", {122, "zombie_painb21", NULL, 0}},
  {"zombie_painb21", {123, "zombie_painb22", NULL, 0}},
  {"zombie_painb22", {124, "zombie_painb23", NULL, 0}},
  {"zombie_painb23", {125, "zombie_painb24", NULL, 0}},
  {"zombie_painb24", {126, "zombie_painb25", NULL, 0}},
  {"zombie_painb25", {127, "zombie_painb26", OPS_777, 1}},
  {"zombie_painb26", {128, "zombie_painb27", NULL, 0}},
  {"zombie_painb27", {129, "zombie_painb28", NULL, 0}},
  {"zombie_painb28", {130, "zombie_run1", NULL, 0}},
  {"zombie_painc1", {131, "zombie_painc2", OPS_781, 1}},
  {"zombie_painc2", {132, "zombie_painc3", NULL, 0}},
  {"zombie_painc3", {133, "zombie_painc4", OPS_783, 1}},
  {"zombie_painc4", {134, "zombie_painc5", OPS_784, 1}},
  {"zombie_painc5", {135, "zombie_painc6", NULL, 0}},
  {"zombie_painc6", {136, "zombie_painc7", NULL, 0}},
  {"zombie_painc7", {137, "zombie_painc8", NULL, 0}},
  {"zombie_painc8", {138, "zombie_painc9", NULL, 0}},
  {"zombie_painc9", {139, "zombie_painc10", NULL, 0}},
  {"zombie_painc10", {140, "zombie_painc11", NULL, 0}},
  {"zombie_painc11", {141, "zombie_painc12", OPS_791, 1}},
  {"zombie_painc12", {142, "zombie_painc13", OPS_792, 1}},
  {"zombie_painc13", {143, "zombie_painc14", NULL, 0}},
  {"zombie_painc14", {144, "zombie_painc15", NULL, 0}},
  {"zombie_painc15", {145, "zombie_painc16", NULL, 0}},
  {"zombie_painc16", {146, "zombie_painc17", NULL, 0}},
  {"zombie_painc17", {147, "zombie_painc18", NULL, 0}},
  {"zombie_painc18", {148, "zombie_run1", NULL, 0}},
  {"zombie_paind1", {149, "zombie_paind2", OPS_799, 1}},
  {"zombie_paind2", {150, "zombie_paind3", NULL, 0}},
  {"zombie_paind3", {151, "zombie_paind4", NULL, 0}},
  {"zombie_paind4", {152, "zombie_paind5", NULL, 0}},
  {"zombie_paind5", {153, "zombie_paind6", NULL, 0}},
  {"zombie_paind6", {154, "zombie_paind7", NULL, 0}},
  {"zombie_paind7", {155, "zombie_paind8", NULL, 0}},
  {"zombie_paind8", {156, "zombie_paind9", NULL, 0}},
  {"zombie_paind9", {157, "zombie_paind10", OPS_807, 1}},
  {"zombie_paind10", {158, "zombie_paind11", NULL, 0}},
  {"zombie_paind11", {159, "zombie_paind12", NULL, 0}},
  {"zombie_paind12", {160, "zombie_paind13", NULL, 0}},
  {"zombie_paind13", {161, "zombie_run1", NULL, 0}},
  {"zombie_paine1", {162, "zombie_paine2", OPS_812, 2}},
  {"zombie_paine2", {163, "zombie_paine3", OPS_813, 1}},
  {"zombie_paine3", {164, "zombie_paine4", OPS_814, 1}},
  {"zombie_paine4", {165, "zombie_paine5", OPS_815, 1}},
  {"zombie_paine5", {166, "zombie_paine6", OPS_816, 1}},
  {"zombie_paine6", {167, "zombie_paine7", OPS_817, 1}},
  {"zombie_paine7", {168, "zombie_paine8", OPS_818, 1}},
  {"zombie_paine8", {169, "zombie_paine9", OPS_819, 1}},
  {"zombie_paine9", {170, "zombie_paine10", OPS_820, 1}},
  {"zombie_paine10", {171, "zombie_paine11", OPS_821, 2}},
  {"zombie_paine11", {172, "zombie_paine12", OPS_822, 1}},
  {"zombie_paine12", {173, "zombie_paine13", OPS_823, 1}},
  {"zombie_paine13", {174, "zombie_paine14", NULL, 0}},
  {"zombie_paine14", {175, "zombie_paine15", NULL, 0}},
  {"zombie_paine15", {176, "zombie_paine16", NULL, 0}},
  {"zombie_paine16", {177, "zombie_paine17", NULL, 0}},
  {"zombie_paine17", {178, "zombie_paine18", NULL, 0}},
  {"zombie_paine18", {179, "zombie_paine19", NULL, 0}},
  {"zombie_paine19", {180, "zombie_paine20", NULL, 0}},
  {"zombie_paine20", {181, "zombie_paine21", NULL, 0}},
  {"zombie_paine21", {182, "zombie_paine22", NULL, 0}},
  {"zombie_paine22", {183, "zombie_paine23", NULL, 0}},
  {"zombie_paine23", {184, "zombie_paine24", NULL, 0}},
  {"zombie_paine24", {185, "zombie_paine25", NULL, 0}},
  {"zombie_paine25", {186, "zombie_paine26", OPS_836, 1}},
  {"zombie_paine26", {187, "zombie_paine27", OPS_837, 1}},
  {"zombie_paine27", {188, "zombie_paine28", OPS_838, 1}},
  {"zombie_paine28", {189, "zombie_paine29", OPS_839, 1}},
  {"zombie_paine29", {190, "zombie_paine30", NULL, 0}},
  {"zombie_paine30", {191, "zombie_run1", NULL, 0}},
  {"boss_rise1", {0, "boss_rise2", OPS_842, 1}},
  {"boss_rise2", {1, "boss_rise3", OPS_843, 1}},
  {"boss_rise3", {2, "boss_rise4", NULL, 0}},
  {"boss_rise4", {3, "boss_rise5", NULL, 0}},
  {"boss_rise5", {4, "boss_rise6", NULL, 0}},
  {"boss_rise6", {5, "boss_rise7", NULL, 0}},
  {"boss_rise7", {6, "boss_rise8", NULL, 0}},
  {"boss_rise8", {7, "boss_rise9", NULL, 0}},
  {"boss_rise9", {8, "boss_rise10", NULL, 0}},
  {"boss_rise10", {9, "boss_rise11", NULL, 0}},
  {"boss_rise11", {10, "boss_rise12", NULL, 0}},
  {"boss_rise12", {11, "boss_rise13", NULL, 0}},
  {"boss_rise13", {12, "boss_rise14", NULL, 0}},
  {"boss_rise14", {13, "boss_rise15", NULL, 0}},
  {"boss_rise15", {14, "boss_rise16", NULL, 0}},
  {"boss_rise16", {15, "boss_rise17", NULL, 0}},
  {"boss_rise17", {16, "boss_missile1", NULL, 0}},
  {"boss_idle1", {17, "boss_idle2", OPS_859, 1}},
  {"boss_idle2", {18, "boss_idle3", OPS_860, 1}},
  {"boss_idle3", {19, "boss_idle4", OPS_861, 1}},
  {"boss_idle4", {20, "boss_idle5", OPS_862, 1}},
  {"boss_idle5", {21, "boss_idle6", OPS_863, 1}},
  {"boss_idle6", {22, "boss_idle7", OPS_864, 1}},
  {"boss_idle7", {23, "boss_idle8", OPS_865, 1}},
  {"boss_idle8", {24, "boss_idle9", OPS_866, 1}},
  {"boss_idle9", {25, "boss_idle10", OPS_867, 1}},
  {"boss_idle10", {26, "boss_idle11", OPS_868, 1}},
  {"boss_idle11", {27, "boss_idle12", OPS_869, 1}},
  {"boss_idle12", {28, "boss_idle13", OPS_870, 1}},
  {"boss_idle13", {29, "boss_idle14", OPS_871, 1}},
  {"boss_idle14", {30, "boss_idle15", OPS_872, 1}},
  {"boss_idle15", {31, "boss_idle16", OPS_873, 1}},
  {"boss_idle16", {32, "boss_idle17", OPS_874, 1}},
  {"boss_idle17", {33, "boss_idle18", OPS_875, 1}},
  {"boss_idle18", {34, "boss_idle19", OPS_876, 1}},
  {"boss_idle19", {35, "boss_idle20", OPS_877, 1}},
  {"boss_idle20", {36, "boss_idle21", OPS_878, 1}},
  {"boss_idle21", {37, "boss_idle22", OPS_879, 1}},
  {"boss_idle22", {38, "boss_idle23", OPS_880, 1}},
  {"boss_idle23", {39, "boss_idle24", OPS_881, 1}},
  {"boss_idle24", {40, "boss_idle25", OPS_882, 1}},
  {"boss_idle25", {41, "boss_idle26", OPS_883, 1}},
  {"boss_idle26", {42, "boss_idle27", OPS_884, 1}},
  {"boss_idle27", {43, "boss_idle28", OPS_885, 1}},
  {"boss_idle28", {44, "boss_idle29", OPS_886, 1}},
  {"boss_idle29", {45, "boss_idle30", OPS_887, 1}},
  {"boss_idle30", {46, "boss_idle31", OPS_888, 1}},
  {"boss_idle31", {47, "boss_idle1", OPS_889, 1}},
  {"boss_missile1", {57, "boss_missile2", OPS_890, 1}},
  {"boss_missile2", {58, "boss_missile3", OPS_891, 1}},
  {"boss_missile3", {59, "boss_missile4", OPS_892, 1}},
  {"boss_missile4", {60, "boss_missile5", OPS_893, 1}},
  {"boss_missile5", {61, "boss_missile6", OPS_894, 1}},
  {"boss_missile6", {62, "boss_missile7", OPS_895, 1}},
  {"boss_missile7", {63, "boss_missile8", OPS_896, 1}},
  {"boss_missile8", {64, "boss_missile9", OPS_897, 1}},
  {"boss_missile9", {65, "boss_missile10", OPS_898, 1}},
  {"boss_missile10", {66, "boss_missile11", OPS_899, 1}},
  {"boss_missile11", {67, "boss_missile12", OPS_900, 1}},
  {"boss_missile12", {68, "boss_missile13", OPS_901, 1}},
  {"boss_missile13", {69, "boss_missile14", OPS_902, 1}},
  {"boss_missile14", {70, "boss_missile15", OPS_903, 1}},
  {"boss_missile15", {71, "boss_missile16", OPS_904, 1}},
  {"boss_missile16", {72, "boss_missile17", OPS_905, 1}},
  {"boss_missile17", {73, "boss_missile18", OPS_906, 1}},
  {"boss_missile18", {74, "boss_missile19", OPS_907, 1}},
  {"boss_missile19", {75, "boss_missile20", OPS_908, 1}},
  {"boss_missile20", {76, "boss_missile21", OPS_909, 1}},
  {"boss_missile21", {77, "boss_missile22", OPS_910, 1}},
  {"boss_missile22", {78, "boss_missile23", OPS_911, 1}},
  {"boss_missile23", {79, "boss_missile1", OPS_912, 1}},
  {"boss_shocka1", {80, "boss_shocka2", NULL, 0}},
  {"boss_shocka2", {81, "boss_shocka3", NULL, 0}},
  {"boss_shocka3", {82, "boss_shocka4", NULL, 0}},
  {"boss_shocka4", {83, "boss_shocka5", NULL, 0}},
  {"boss_shocka5", {84, "boss_shocka6", NULL, 0}},
  {"boss_shocka6", {85, "boss_shocka7", NULL, 0}},
  {"boss_shocka7", {86, "boss_shocka8", NULL, 0}},
  {"boss_shocka8", {87, "boss_shocka9", NULL, 0}},
  {"boss_shocka9", {88, "boss_shocka10", NULL, 0}},
  {"boss_shocka10", {89, "boss_missile1", NULL, 0}},
  {"boss_shockb1", {90, "boss_shockb2", NULL, 0}},
  {"boss_shockb2", {91, "boss_shockb3", NULL, 0}},
  {"boss_shockb3", {92, "boss_shockb4", NULL, 0}},
  {"boss_shockb4", {93, "boss_shockb5", NULL, 0}},
  {"boss_shockb5", {94, "boss_shockb6", NULL, 0}},
  {"boss_shockb6", {95, "boss_shockb7", NULL, 0}},
  {"boss_shockb7", {90, "boss_shockb8", NULL, 0}},
  {"boss_shockb8", {91, "boss_shockb9", NULL, 0}},
  {"boss_shockb9", {92, "boss_shockb10", NULL, 0}},
  {"boss_shockb10", {93, "boss_missile1", NULL, 0}},
  {"boss_shockc1", {96, "boss_shockc2", NULL, 0}},
  {"boss_shockc2", {97, "boss_shockc3", NULL, 0}},
  {"boss_shockc3", {98, "boss_shockc4", NULL, 0}},
  {"boss_shockc4", {99, "boss_shockc5", NULL, 0}},
  {"boss_shockc5", {100, "boss_shockc6", NULL, 0}},
  {"boss_shockc6", {101, "boss_shockc7", NULL, 0}},
  {"boss_shockc7", {102, "boss_shockc8", NULL, 0}},
  {"boss_shockc8", {103, "boss_shockc9", NULL, 0}},
  {"boss_shockc9", {104, "boss_shockc10", NULL, 0}},
  {"boss_shockc10", {105, "boss_death1", NULL, 0}},
  {"boss_death1", {48, "boss_death2", OPS_943, 1}},
  {"boss_death2", {49, "boss_death3", NULL, 0}},
  {"boss_death3", {50, "boss_death4", NULL, 0}},
  {"boss_death4", {51, "boss_death5", NULL, 0}},
  {"boss_death5", {52, "boss_death6", NULL, 0}},
  {"boss_death6", {53, "boss_death7", NULL, 0}},
  {"boss_death7", {54, "boss_death8", NULL, 0}},
  {"boss_death8", {55, "boss_death9", NULL, 0}},
  {"boss_death9", {56, "boss_death10", OPS_951, 2}},
  {"boss_death10", {56, "boss_death10", OPS_952, 1}},
  {"tbaby_stand1", {0, "tbaby_stand1", OPS_953, 1}},
  {"tbaby_hang1", {0, "tbaby_hang1", OPS_954, 1}},
  {"tbaby_walk1", {0, "tbaby_walk2", OPS_955, 1}},
  {"tbaby_walk2", {1, "tbaby_walk3", OPS_956, 1}},
  {"tbaby_walk3", {2, "tbaby_walk4", OPS_957, 1}},
  {"tbaby_walk4", {3, "tbaby_walk5", OPS_958, 1}},
  {"tbaby_walk5", {4, "tbaby_walk6", OPS_959, 1}},
  {"tbaby_walk6", {5, "tbaby_walk7", OPS_960, 1}},
  {"tbaby_walk7", {6, "tbaby_walk8", OPS_961, 1}},
  {"tbaby_walk8", {7, "tbaby_walk9", OPS_962, 1}},
  {"tbaby_walk9", {8, "tbaby_walk10", OPS_963, 1}},
  {"tbaby_walk10", {9, "tbaby_walk11", OPS_964, 1}},
  {"tbaby_walk11", {10, "tbaby_walk12", OPS_965, 1}},
  {"tbaby_walk12", {11, "tbaby_walk13", OPS_966, 1}},
  {"tbaby_walk13", {12, "tbaby_walk14", OPS_967, 1}},
  {"tbaby_walk14", {13, "tbaby_walk15", OPS_968, 1}},
  {"tbaby_walk15", {14, "tbaby_walk16", OPS_969, 1}},
  {"tbaby_walk16", {15, "tbaby_walk17", OPS_970, 1}},
  {"tbaby_walk17", {16, "tbaby_walk18", OPS_971, 1}},
  {"tbaby_walk18", {17, "tbaby_walk19", OPS_972, 1}},
  {"tbaby_walk19", {18, "tbaby_walk20", OPS_973, 1}},
  {"tbaby_walk20", {19, "tbaby_walk21", OPS_974, 1}},
  {"tbaby_walk21", {20, "tbaby_walk22", OPS_975, 1}},
  {"tbaby_walk22", {21, "tbaby_walk23", OPS_976, 1}},
  {"tbaby_walk23", {22, "tbaby_walk24", OPS_977, 1}},
  {"tbaby_walk24", {23, "tbaby_walk25", OPS_978, 1}},
  {"tbaby_walk25", {24, "tbaby_walk1", OPS_979, 1}},
  {"tbaby_run1", {25, "tbaby_run2", OPS_980, 1}},
  {"tbaby_run2", {26, "tbaby_run3", OPS_981, 1}},
  {"tbaby_run3", {27, "tbaby_run4", OPS_982, 1}},
  {"tbaby_run4", {28, "tbaby_run5", OPS_983, 1}},
  {"tbaby_run5", {29, "tbaby_run6", OPS_984, 1}},
  {"tbaby_run6", {30, "tbaby_run7", OPS_985, 1}},
  {"tbaby_run7", {31, "tbaby_run8", OPS_986, 1}},
  {"tbaby_run8", {32, "tbaby_run9", OPS_987, 1}},
  {"tbaby_run9", {33, "tbaby_run10", OPS_988, 1}},
  {"tbaby_run10", {34, "tbaby_run11", OPS_989, 1}},
  {"tbaby_run11", {35, "tbaby_run12", OPS_990, 1}},
  {"tbaby_run12", {36, "tbaby_run13", OPS_991, 1}},
  {"tbaby_run13", {37, "tbaby_run14", OPS_992, 1}},
  {"tbaby_run14", {38, "tbaby_run15", OPS_993, 1}},
  {"tbaby_run15", {39, "tbaby_run16", OPS_994, 1}},
  {"tbaby_run16", {40, "tbaby_run17", OPS_995, 1}},
  {"tbaby_run17", {41, "tbaby_run18", OPS_996, 1}},
  {"tbaby_run18", {42, "tbaby_run19", OPS_997, 1}},
  {"tbaby_run19", {43, "tbaby_run20", OPS_998, 1}},
  {"tbaby_run20", {44, "tbaby_run21", OPS_999, 1}},
  {"tbaby_run21", {45, "tbaby_run22", OPS_1000, 1}},
  {"tbaby_run22", {46, "tbaby_run23", OPS_1001, 1}},
  {"tbaby_run23", {47, "tbaby_run24", OPS_1002, 1}},
  {"tbaby_run24", {48, "tbaby_run25", OPS_1003, 1}},
  {"tbaby_run25", {49, "tbaby_run1", OPS_1004, 1}},
  {"tbaby_fly1", {56, "tbaby_fly2", NULL, 0}},
  {"tbaby_fly2", {57, "tbaby_fly3", NULL, 0}},
  {"tbaby_fly3", {58, "tbaby_fly4", NULL, 0}},
  {"tbaby_fly4", {59, "tbaby_fly1", OPS_1008, 1}},
  {"tbaby_jump1", {50, "tbaby_jump2", OPS_1009, 1}},
  {"tbaby_jump2", {51, "tbaby_jump3", OPS_1010, 1}},
  {"tbaby_jump3", {52, "tbaby_jump4", OPS_1011, 1}},
  {"tbaby_jump4", {53, "tbaby_jump5", OPS_1012, 1}},
  {"tbaby_jump5", {54, "tbaby_jump6", OPS_1013, 1}},
  {"tbaby_jump6", {55, "tbaby_fly1", NULL, 0}},
  {"tbaby_die1", {60, "tbaby_die2", OPS_1015, 1}},
  {"tbaby_die2", {60, "tbaby_run1", OPS_1016, 1}},
  {"hknight_stand1", {0, "hknight_stand2", OPS_1017, 1}},
  {"hknight_stand2", {1, "hknight_stand3", OPS_1018, 1}},
  {"hknight_stand3", {2, "hknight_stand4", OPS_1019, 1}},
  {"hknight_stand4", {3, "hknight_stand5", OPS_1020, 1}},
  {"hknight_stand5", {4, "hknight_stand6", OPS_1021, 1}},
  {"hknight_stand6", {5, "hknight_stand7", OPS_1022, 1}},
  {"hknight_stand7", {6, "hknight_stand8", OPS_1023, 1}},
  {"hknight_stand8", {7, "hknight_stand9", OPS_1024, 1}},
  {"hknight_stand9", {8, "hknight_stand1", OPS_1025, 1}},
  {"hknight_walk1", {9, "hknight_walk2", OPS_1026, 1}},
  {"hknight_walk2", {10, "hknight_walk3", OPS_1027, 1}},
  {"hknight_walk3", {11, "hknight_walk4", OPS_1028, 1}},
  {"hknight_walk4", {12, "hknight_walk5", OPS_1029, 1}},
  {"hknight_walk5", {13, "hknight_walk6", OPS_1030, 1}},
  {"hknight_walk6", {14, "hknight_walk7", OPS_1031, 1}},
  {"hknight_walk7", {15, "hknight_walk8", OPS_1032, 1}},
  {"hknight_walk8", {16, "hknight_walk9", OPS_1033, 1}},
  {"hknight_walk9", {17, "hknight_walk10", OPS_1034, 1}},
  {"hknight_walk10", {18, "hknight_walk11", OPS_1035, 1}},
  {"hknight_walk11", {19, "hknight_walk12", OPS_1036, 1}},
  {"hknight_walk12", {20, "hknight_walk13", OPS_1037, 1}},
  {"hknight_walk13", {21, "hknight_walk14", OPS_1038, 1}},
  {"hknight_walk14", {22, "hknight_walk15", OPS_1039, 1}},
  {"hknight_walk15", {23, "hknight_walk16", OPS_1040, 1}},
  {"hknight_walk16", {24, "hknight_walk17", OPS_1041, 1}},
  {"hknight_walk17", {25, "hknight_walk18", OPS_1042, 1}},
  {"hknight_walk18", {26, "hknight_walk19", OPS_1043, 1}},
  {"hknight_walk19", {27, "hknight_walk20", OPS_1044, 1}},
  {"hknight_walk20", {28, "hknight_walk1", OPS_1045, 1}},
  {"hknight_run1", {29, "hknight_run2", OPS_1046, 1}},
  {"hknight_run2", {30, "hknight_run3", OPS_1047, 1}},
  {"hknight_run3", {31, "hknight_run4", OPS_1048, 1}},
  {"hknight_run4", {32, "hknight_run5", OPS_1049, 1}},
  {"hknight_run5", {33, "hknight_run6", OPS_1050, 1}},
  {"hknight_run6", {34, "hknight_run7", OPS_1051, 1}},
  {"hknight_run7", {35, "hknight_run8", OPS_1052, 1}},
  {"hknight_run8", {36, "hknight_run1", OPS_1053, 1}},
  {"hknight_pain1", {37, "hknight_pain2", OPS_1054, 1}},
  {"hknight_pain2", {38, "hknight_pain3", NULL, 0}},
  {"hknight_pain3", {39, "hknight_pain4", NULL, 0}},
  {"hknight_pain4", {40, "hknight_pain5", NULL, 0}},
  {"hknight_pain5", {41, "hknight_run1", NULL, 0}},
  {"hknight_die1", {42, "hknight_die2", OPS_1059, 1}},
  {"hknight_die2", {43, "hknight_die3", OPS_1060, 1}},
  {"hknight_die3", {44, "hknight_die4", OPS_1061, 2}},
  {"hknight_die4", {45, "hknight_die5", NULL, 0}},
  {"hknight_die5", {46, "hknight_die6", NULL, 0}},
  {"hknight_die6", {47, "hknight_die7", NULL, 0}},
  {"hknight_die7", {48, "hknight_die8", NULL, 0}},
  {"hknight_die8", {49, "hknight_die9", OPS_1066, 1}},
  {"hknight_die9", {50, "hknight_die10", OPS_1067, 1}},
  {"hknight_die10", {51, "hknight_die11", NULL, 0}},
  {"hknight_die11", {52, "hknight_die12", NULL, 0}},
  {"hknight_die12", {53, "hknight_die12", NULL, 0}},
  {"hknight_dieb1", {54, "hknight_dieb2", NULL, 0}},
  {"hknight_dieb2", {55, "hknight_dieb3", NULL, 0}},
  {"hknight_dieb3", {56, "hknight_dieb4", OPS_1073, 1}},
  {"hknight_dieb4", {57, "hknight_dieb5", NULL, 0}},
  {"hknight_dieb5", {58, "hknight_dieb6", NULL, 0}},
  {"hknight_dieb6", {59, "hknight_dieb7", NULL, 0}},
  {"hknight_dieb7", {60, "hknight_dieb8", NULL, 0}},
  {"hknight_dieb8", {61, "hknight_dieb9", NULL, 0}},
  {"hknight_dieb9", {62, "hknight_dieb9", NULL, 0}},
  {"hknight_magica1", {79, "hknight_magica2", OPS_1080, 1}},
  {"hknight_magica2", {80, "hknight_magica3", OPS_1081, 1}},
  {"hknight_magica3", {81, "hknight_magica4", OPS_1082, 1}},
  {"hknight_magica4", {82, "hknight_magica5", OPS_1083, 1}},
  {"hknight_magica5", {83, "hknight_magica6", OPS_1084, 1}},
  {"hknight_magica6", {84, "hknight_magica7", OPS_1085, 1}},
  {"hknight_magica7", {85, "hknight_magica8", OPS_1086, 1}},
  {"hknight_magica8", {86, "hknight_magica9", OPS_1087, 1}},
  {"hknight_magica9", {87, "hknight_magica10", OPS_1088, 1}},
  {"hknight_magica10", {88, "hknight_magica11", OPS_1089, 1}},
  {"hknight_magica11", {89, "hknight_magica12", OPS_1090, 1}},
  {"hknight_magica12", {90, "hknight_magica13", OPS_1091, 1}},
  {"hknight_magica13", {91, "hknight_magica14", OPS_1092, 1}},
  {"hknight_magica14", {92, "hknight_run1", OPS_1093, 1}},
  {"hknight_magicb1", {93, "hknight_magicb2", OPS_1094, 1}},
  {"hknight_magicb2", {94, "hknight_magicb3", OPS_1095, 1}},
  {"hknight_magicb3", {95, "hknight_magicb4", OPS_1096, 1}},
  {"hknight_magicb4", {96, "hknight_magicb5", OPS_1097, 1}},
  {"hknight_magicb5", {97, "hknight_magicb6", OPS_1098, 1}},
  {"hknight_magicb6", {98, "hknight_magicb7", OPS_1099, 1}},
  {"hknight_magicb7", {99, "hknight_magicb8", OPS_1100, 1}},
  {"hknight_magicb8", {100, "hknight_magicb9", OPS_1101, 1}},
  {"hknight_magicb9", {101, "hknight_magicb10", OPS_1102, 1}},
  {"hknight_magicb10", {102, "hknight_magicb11", OPS_1103, 1}},
  {"hknight_magicb11", {103, "hknight_magicb12", OPS_1104, 1}},
  {"hknight_magicb12", {104, "hknight_magicb13", OPS_1105, 1}},
  {"hknight_magicb13", {105, "hknight_run1", OPS_1106, 1}},
  {"hknight_magicc1", {155, "hknight_magicc2", OPS_1107, 1}},
  {"hknight_magicc2", {156, "hknight_magicc3", OPS_1108, 1}},
  {"hknight_magicc3", {157, "hknight_magicc4", OPS_1109, 1}},
  {"hknight_magicc4", {158, "hknight_magicc5", OPS_1110, 1}},
  {"hknight_magicc5", {159, "hknight_magicc6", OPS_1111, 1}},
  {"hknight_magicc6", {160, "hknight_magicc7", OPS_1112, 1}},
  {"hknight_magicc7", {161, "hknight_magicc8", OPS_1113, 1}},
  {"hknight_magicc8", {162, "hknight_magicc9", OPS_1114, 1}},
  {"hknight_magicc9", {163, "hknight_magicc10", OPS_1115, 1}},
  {"hknight_magicc10", {164, "hknight_magicc11", OPS_1116, 1}},
  {"hknight_magicc11", {165, "hknight_run1", OPS_1117, 1}},
  {"hknight_char_a1", {63, "hknight_char_a2", OPS_1118, 1}},
  {"hknight_char_a2", {64, "hknight_char_a3", OPS_1119, 1}},
  {"hknight_char_a3", {65, "hknight_char_a4", OPS_1120, 1}},
  {"hknight_char_a4", {66, "hknight_char_a5", OPS_1121, 1}},
  {"hknight_char_a5", {67, "hknight_char_a6", OPS_1122, 1}},
  {"hknight_char_a6", {68, "hknight_char_a7", OPS_1123, 2}},
  {"hknight_char_a7", {69, "hknight_char_a8", OPS_1124, 2}},
  {"hknight_char_a8", {70, "hknight_char_a9", OPS_1125, 2}},
  {"hknight_char_a9", {71, "hknight_char_a10", OPS_1126, 2}},
  {"hknight_char_a10", {72, "hknight_char_a11", OPS_1127, 2}},
  {"hknight_char_a11", {73, "hknight_char_a12", OPS_1128, 2}},
  {"hknight_char_a12", {74, "hknight_char_a13", OPS_1129, 1}},
  {"hknight_char_a13", {75, "hknight_char_a14", OPS_1130, 1}},
  {"hknight_char_a14", {76, "hknight_char_a15", OPS_1131, 1}},
  {"hknight_char_a15", {77, "hknight_char_a16", OPS_1132, 1}},
  {"hknight_char_a16", {78, "hknight_run1", OPS_1133, 1}},
  {"hknight_char_b1", {106, "hknight_char_b2", OPS_1134, 1}},
  {"hknight_char_b2", {107, "hknight_char_b3", OPS_1135, 2}},
  {"hknight_char_b3", {108, "hknight_char_b4", OPS_1136, 2}},
  {"hknight_char_b4", {109, "hknight_char_b5", OPS_1137, 2}},
  {"hknight_char_b5", {110, "hknight_char_b6", OPS_1138, 2}},
  {"hknight_char_b6", {111, "hknight_char_b1", OPS_1139, 2}},
  {"hknight_slice1", {112, "hknight_slice2", OPS_1140, 1}},
  {"hknight_slice2", {113, "hknight_slice3", OPS_1141, 1}},
  {"hknight_slice3", {114, "hknight_slice4", OPS_1142, 1}},
  {"hknight_slice4", {115, "hknight_slice5", OPS_1143, 1}},
  {"hknight_slice5", {116, "hknight_slice6", OPS_1144, 2}},
  {"hknight_slice6", {117, "hknight_slice7", OPS_1145, 2}},
  {"hknight_slice7", {118, "hknight_slice8", OPS_1146, 2}},
  {"hknight_slice8", {119, "hknight_slice9", OPS_1147, 2}},
  {"hknight_slice9", {120, "hknight_slice10", OPS_1148, 1}},
  {"hknight_slice10", {121, "hknight_run1", OPS_1149, 1}},
  {"hknight_smash1", {122, "hknight_smash2", OPS_1150, 1}},
  {"hknight_smash2", {123, "hknight_smash3", OPS_1151, 1}},
  {"hknight_smash3", {124, "hknight_smash4", OPS_1152, 1}},
  {"hknight_smash4", {125, "hknight_smash5", OPS_1153, 1}},
  {"hknight_smash5", {126, "hknight_smash6", OPS_1154, 2}},
  {"hknight_smash6", {127, "hknight_smash7", OPS_1155, 2}},
  {"hknight_smash7", {128, "hknight_smash8", OPS_1156, 2}},
  {"hknight_smash8", {129, "hknight_smash9", OPS_1157, 2}},
  {"hknight_smash9", {130, "hknight_smash10", OPS_1158, 2}},
  {"hknight_smash10", {131, "hknight_smash11", OPS_1159, 1}},
  {"hknight_smash11", {132, "hknight_run1", OPS_1160, 1}},
  {"hknight_watk1", {133, "hknight_watk2", OPS_1161, 1}},
  {"hknight_watk2", {134, "hknight_watk3", OPS_1162, 1}},
  {"hknight_watk3", {135, "hknight_watk4", OPS_1163, 1}},
  {"hknight_watk4", {136, "hknight_watk5", OPS_1164, 1}},
  {"hknight_watk5", {137, "hknight_watk6", OPS_1165, 1}},
  {"hknight_watk6", {138, "hknight_watk7", OPS_1166, 1}},
  {"hknight_watk7", {139, "hknight_watk8", OPS_1167, 1}},
  {"hknight_watk8", {140, "hknight_watk9", OPS_1168, 1}},
  {"hknight_watk9", {141, "hknight_watk10", OPS_1169, 1}},
  {"hknight_watk10", {142, "hknight_watk11", OPS_1170, 2}},
  {"hknight_watk11", {143, "hknight_watk12", OPS_1171, 2}},
  {"hknight_watk12", {144, "hknight_watk13", OPS_1172, 2}},
  {"hknight_watk13", {145, "hknight_watk14", OPS_1173, 1}},
  {"hknight_watk14", {146, "hknight_watk15", OPS_1174, 1}},
  {"hknight_watk15", {147, "hknight_watk16", OPS_1175, 1}},
  {"hknight_watk16", {148, "hknight_watk17", OPS_1176, 1}},
  {"hknight_watk17", {149, "hknight_watk18", OPS_1177, 2}},
  {"hknight_watk18", {150, "hknight_watk19", OPS_1178, 2}},
  {"hknight_watk19", {151, "hknight_watk20", OPS_1179, 2}},
  {"hknight_watk20", {152, "hknight_watk21", OPS_1180, 1}},
  {"hknight_watk21", {153, "hknight_watk22", OPS_1181, 1}},
  {"hknight_watk22", {154, "hknight_run1", OPS_1182, 1}},
  {"f_stand1", {39, "f_stand2", OPS_1183, 1}},
  {"f_stand2", {40, "f_stand3", OPS_1184, 1}},
  {"f_stand3", {41, "f_stand4", OPS_1185, 1}},
  {"f_stand4", {42, "f_stand5", OPS_1186, 1}},
  {"f_stand5", {43, "f_stand6", OPS_1187, 1}},
  {"f_stand6", {44, "f_stand7", OPS_1188, 1}},
  {"f_stand7", {45, "f_stand8", OPS_1189, 1}},
  {"f_stand8", {46, "f_stand9", OPS_1190, 1}},
  {"f_stand9", {47, "f_stand10", OPS_1191, 1}},
  {"f_stand10", {48, "f_stand11", OPS_1192, 1}},
  {"f_stand11", {49, "f_stand12", OPS_1193, 1}},
  {"f_stand12", {50, "f_stand13", OPS_1194, 1}},
  {"f_stand13", {51, "f_stand14", OPS_1195, 1}},
  {"f_stand14", {52, "f_stand15", OPS_1196, 1}},
  {"f_stand15", {53, "f_stand16", OPS_1197, 1}},
  {"f_stand16", {54, "f_stand17", OPS_1198, 1}},
  {"f_stand17", {55, "f_stand18", OPS_1199, 1}},
  {"f_stand18", {56, "f_stand1", OPS_1200, 1}},
  {"f_walk1", {39, "f_walk2", OPS_1201, 1}},
  {"f_walk2", {40, "f_walk3", OPS_1202, 1}},
  {"f_walk3", {41, "f_walk4", OPS_1203, 1}},
  {"f_walk4", {42, "f_walk5", OPS_1204, 1}},
  {"f_walk5", {43, "f_walk6", OPS_1205, 1}},
  {"f_walk6", {44, "f_walk7", OPS_1206, 1}},
  {"f_walk7", {45, "f_walk8", OPS_1207, 1}},
  {"f_walk8", {46, "f_walk9", OPS_1208, 1}},
  {"f_walk9", {47, "f_walk10", OPS_1209, 1}},
  {"f_walk10", {48, "f_walk11", OPS_1210, 1}},
  {"f_walk11", {49, "f_walk12", OPS_1211, 1}},
  {"f_walk12", {50, "f_walk13", OPS_1212, 1}},
  {"f_walk13", {51, "f_walk14", OPS_1213, 1}},
  {"f_walk14", {52, "f_walk15", OPS_1214, 1}},
  {"f_walk15", {53, "f_walk16", OPS_1215, 1}},
  {"f_walk16", {54, "f_walk17", OPS_1216, 1}},
  {"f_walk17", {55, "f_walk18", OPS_1217, 1}},
  {"f_walk18", {56, "f_walk1", OPS_1218, 1}},
  {"f_run1", {39, "f_run2", OPS_1219, 2}},
  {"f_run2", {41, "f_run3", OPS_1220, 1}},
  {"f_run3", {43, "f_run4", OPS_1221, 1}},
  {"f_run4", {45, "f_run5", OPS_1222, 1}},
  {"f_run5", {47, "f_run6", OPS_1223, 1}},
  {"f_run6", {49, "f_run7", OPS_1224, 1}},
  {"f_run7", {51, "f_run8", OPS_1225, 1}},
  {"f_run8", {53, "f_run9", OPS_1226, 1}},
  {"f_run9", {55, "f_run1", OPS_1227, 1}},
  {"f_attack1", {0, "f_attack2", OPS_1228, 1}},
  {"f_attack2", {1, "f_attack3", OPS_1229, 1}},
  {"f_attack3", {2, "f_attack4", OPS_1230, 1}},
  {"f_attack4", {3, "f_attack5", OPS_1231, 1}},
  {"f_attack5", {4, "f_attack6", OPS_1232, 1}},
  {"f_attack6", {5, "f_attack7", OPS_1233, 1}},
  {"f_attack7", {6, "f_attack8", OPS_1234, 1}},
  {"f_attack8", {7, "f_attack9", OPS_1235, 1}},
  {"f_attack9", {8, "f_attack10", OPS_1236, 1}},
  {"f_attack10", {9, "f_attack11", OPS_1237, 1}},
  {"f_attack11", {10, "f_attack12", OPS_1238, 1}},
  {"f_attack12", {11, "f_attack13", OPS_1239, 1}},
  {"f_attack13", {12, "f_attack14", OPS_1240, 1}},
  {"f_attack14", {13, "f_attack15", OPS_1241, 1}},
  {"f_attack15", {14, "f_attack16", OPS_1242, 1}},
  {"f_attack16", {15, "f_attack17", OPS_1243, 1}},
  {"f_attack17", {16, "f_attack18", OPS_1244, 1}},
  {"f_attack18", {17, "f_run1", OPS_1245, 1}},
  {"f_death1", {18, "f_death2", OPS_1246, 1}},
  {"f_death2", {19, "f_death3", OPS_1247, 1}},
  {"f_death3", {20, "f_death4", NULL, 0}},
  {"f_death4", {21, "f_death5", NULL, 0}},
  {"f_death5", {22, "f_death6", NULL, 0}},
  {"f_death6", {23, "f_death7", NULL, 0}},
  {"f_death7", {24, "f_death8", NULL, 0}},
  {"f_death8", {25, "f_death9", NULL, 0}},
  {"f_death9", {26, "f_death10", NULL, 0}},
  {"f_death10", {27, "f_death11", NULL, 0}},
  {"f_death11", {28, "f_death12", NULL, 0}},
  {"f_death12", {29, "f_death13", NULL, 0}},
  {"f_death13", {30, "f_death14", NULL, 0}},
  {"f_death14", {31, "f_death15", NULL, 0}},
  {"f_death15", {32, "f_death16", NULL, 0}},
  {"f_death16", {33, "f_death17", NULL, 0}},
  {"f_death17", {34, "f_death18", NULL, 0}},
  {"f_death18", {35, "f_death19", NULL, 0}},
  {"f_death19", {36, "f_death20", NULL, 0}},
  {"f_death20", {37, "f_death21", NULL, 0}},
  {"f_death21", {38, "f_death21", NULL, 0}},
  {"f_pain1", {57, "f_pain2", NULL, 0}},
  {"f_pain2", {58, "f_pain3", OPS_1268, 1}},
  {"f_pain3", {59, "f_pain4", OPS_1269, 1}},
  {"f_pain4", {60, "f_pain5", OPS_1270, 1}},
  {"f_pain5", {61, "f_pain6", OPS_1271, 1}},
  {"f_pain6", {62, "f_pain7", OPS_1272, 1}},
  {"f_pain7", {63, "f_pain8", OPS_1273, 1}},
  {"f_pain8", {64, "f_pain9", OPS_1274, 1}},
  {"f_pain9", {65, "f_run1", OPS_1275, 1}},
  {"shal_stand", {23, "shal_stand", OPS_1276, 1}},
  {"shal_walk1", {24, "shal_walk2", OPS_1277, 2}},
  {"shal_walk2", {25, "shal_walk3", OPS_1278, 1}},
  {"shal_walk3", {26, "shal_walk4", OPS_1279, 1}},
  {"shal_walk4", {27, "shal_walk5", OPS_1280, 1}},
  {"shal_walk5", {28, "shal_walk6", OPS_1281, 1}},
  {"shal_walk6", {29, "shal_walk7", OPS_1282, 1}},
  {"shal_walk7", {30, "shal_walk8", OPS_1283, 1}},
  {"shal_walk8", {31, "shal_walk9", OPS_1284, 1}},
  {"shal_walk9", {32, "shal_walk10", OPS_1285, 1}},
  {"shal_walk10", {33, "shal_walk11", OPS_1286, 1}},
  {"shal_walk11", {34, "shal_walk12", OPS_1287, 1}},
  {"shal_walk12", {23, "shal_walk1", OPS_1288, 1}},
  {"shal_run1", {24, "shal_run2", OPS_1289, 2}},
  {"shal_run2", {25, "shal_run3", OPS_1290, 1}},
  {"shal_run3", {26, "shal_run4", OPS_1291, 1}},
  {"shal_run4", {27, "shal_run5", OPS_1292, 1}},
  {"shal_run5", {28, "shal_run6", OPS_1293, 1}},
  {"shal_run6", {29, "shal_run7", OPS_1294, 1}},
  {"shal_run7", {30, "shal_run8", OPS_1295, 1}},
  {"shal_run8", {31, "shal_run9", OPS_1296, 1}},
  {"shal_run9", {32, "shal_run10", OPS_1297, 1}},
  {"shal_run10", {33, "shal_run11", OPS_1298, 1}},
  {"shal_run11", {34, "shal_run12", OPS_1299, 1}},
  {"shal_run12", {23, "shal_run1", OPS_1300, 1}},
  {"shal_attack1", {0, "shal_attack2", OPS_1301, 2}},
  {"shal_attack2", {1, "shal_attack3", OPS_1302, 1}},
  {"shal_attack3", {2, "shal_attack4", OPS_1303, 1}},
  {"shal_attack4", {3, "shal_attack5", OPS_1304, 1}},
  {"shal_attack5", {4, "shal_attack6", OPS_1305, 1}},
  {"shal_attack6", {5, "shal_attack7", OPS_1306, 1}},
  {"shal_attack7", {6, "shal_attack8", OPS_1307, 1}},
  {"shal_attack8", {7, "shal_attack9", OPS_1308, 1}},
  {"shal_attack9", {8, "shal_attack10", OPS_1309, 1}},
  {"shal_attack10", {9, "shal_attack11", OPS_1310, 1}},
  {"shal_attack11", {10, "shal_run1", NULL, 0}},
  {"shal_pain1", {11, "shal_pain2", NULL, 0}},
  {"shal_pain2", {12, "shal_pain3", NULL, 0}},
  {"shal_pain3", {13, "shal_pain4", NULL, 0}},
  {"shal_pain4", {14, "shal_pain5", NULL, 0}},
  {"shal_pain5", {15, "shal_run1", NULL, 0}},
  {"shal_death1", {16, "shal_death2", NULL, 0}},
  {"shal_death2", {17, "shal_death3", NULL, 0}},
  {"shal_death3", {18, "shal_death4", NULL, 0}},
  {"shal_death4", {19, "shal_death5", NULL, 0}},
  {"shal_death5", {20, "shal_death6", NULL, 0}},
  {"shal_death6", {21, "shal_death7", NULL, 0}},
  {"shal_death7", {22, "shal_death7", NULL, 0}},
  {"enf_stand1", {0, "enf_stand2", OPS_1324, 1}},
  {"enf_stand2", {1, "enf_stand3", OPS_1325, 1}},
  {"enf_stand3", {2, "enf_stand4", OPS_1326, 1}},
  {"enf_stand4", {3, "enf_stand5", OPS_1327, 1}},
  {"enf_stand5", {4, "enf_stand6", OPS_1328, 1}},
  {"enf_stand6", {5, "enf_stand7", OPS_1329, 1}},
  {"enf_stand7", {6, "enf_stand1", OPS_1330, 1}},
  {"enf_walk1", {7, "enf_walk2", OPS_1331, 2}},
  {"enf_walk2", {8, "enf_walk3", OPS_1332, 1}},
  {"enf_walk3", {9, "enf_walk4", OPS_1333, 1}},
  {"enf_walk4", {10, "enf_walk5", OPS_1334, 1}},
  {"enf_walk5", {11, "enf_walk6", OPS_1335, 1}},
  {"enf_walk6", {12, "enf_walk7", OPS_1336, 1}},
  {"enf_walk7", {13, "enf_walk8", OPS_1337, 1}},
  {"enf_walk8", {14, "enf_walk9", OPS_1338, 1}},
  {"enf_walk9", {15, "enf_walk10", OPS_1339, 1}},
  {"enf_walk10", {16, "enf_walk11", OPS_1340, 1}},
  {"enf_walk11", {17, "enf_walk12", OPS_1341, 1}},
  {"enf_walk12", {18, "enf_walk13", OPS_1342, 1}},
  {"enf_walk13", {19, "enf_walk14", OPS_1343, 1}},
  {"enf_walk14", {20, "enf_walk15", OPS_1344, 1}},
  {"enf_walk15", {21, "enf_walk16", OPS_1345, 1}},
  {"enf_walk16", {22, "enf_walk1", OPS_1346, 1}},
  {"enf_run1", {23, "enf_run2", OPS_1347, 2}},
  {"enf_run2", {24, "enf_run3", OPS_1348, 1}},
  {"enf_run3", {25, "enf_run4", OPS_1349, 1}},
  {"enf_run4", {26, "enf_run5", OPS_1350, 1}},
  {"enf_run5", {27, "enf_run6", OPS_1351, 1}},
  {"enf_run6", {28, "enf_run7", OPS_1352, 1}},
  {"enf_run7", {29, "enf_run8", OPS_1353, 1}},
  {"enf_run8", {30, "enf_run1", OPS_1354, 1}},
  {"enf_atk1", {31, "enf_atk2", OPS_1355, 1}},
  {"enf_atk2", {32, "enf_atk3", OPS_1356, 1}},
  {"enf_atk3", {33, "enf_atk4", OPS_1357, 1}},
  {"enf_atk4", {34, "enf_atk5", OPS_1358, 1}},
  {"enf_atk5", {35, "enf_atk6", OPS_1359, 1}},
  {"enf_atk6", {36, "enf_atk7", OPS_1360, 1}},
  {"enf_atk7", {37, "enf_atk8", OPS_1361, 1}},
  {"enf_atk8", {38, "enf_atk9", OPS_1362, 1}},
  {"enf_atk9", {35, "enf_atk10", OPS_1363, 1}},
  {"enf_atk10", {36, "enf_atk11", OPS_1364, 1}},
  {"enf_atk11", {37, "enf_atk12", OPS_1365, 1}},
  {"enf_atk12", {38, "enf_atk13", OPS_1366, 1}},
  {"enf_atk13", {39, "enf_atk14", OPS_1367, 1}},
  {"enf_atk14", {40, "enf_run1", OPS_1368, 2}},
  {"enf_paina1", {66, "enf_paina2", NULL, 0}},
  {"enf_paina2", {67, "enf_paina3", NULL, 0}},
  {"enf_paina3", {68, "enf_paina4", NULL, 0}},
  {"enf_paina4", {69, "enf_run1", NULL, 0}},
  {"enf_painb1", {70, "enf_painb2", NULL, 0}},
  {"enf_painb2", {71, "enf_painb3", NULL, 0}},
  {"enf_painb3", {72, "enf_painb4", NULL, 0}},
  {"enf_painb4", {73, "enf_painb5", NULL, 0}},
  {"enf_painb5", {74, "enf_run1", NULL, 0}},
  {"enf_painc1", {75, "enf_painc2", NULL, 0}},
  {"enf_painc2", {76, "enf_painc3", NULL, 0}},
  {"enf_painc3", {77, "enf_painc4", NULL, 0}},
  {"enf_painc4", {78, "enf_painc5", NULL, 0}},
  {"enf_painc5", {79, "enf_painc6", NULL, 0}},
  {"enf_painc6", {80, "enf_painc7", NULL, 0}},
  {"enf_painc7", {81, "enf_painc8", NULL, 0}},
  {"enf_painc8", {82, "enf_run1", NULL, 0}},
  {"enf_paind1", {83, "enf_paind2", NULL, 0}},
  {"enf_paind2", {84, "enf_paind3", NULL, 0}},
  {"enf_paind3", {85, "enf_paind4", NULL, 0}},
  {"enf_paind4", {86, "enf_paind5", OPS_1389, 1}},
  {"enf_paind5", {87, "enf_paind6", OPS_1390, 1}},
  {"enf_paind6", {88, "enf_paind7", NULL, 0}},
  {"enf_paind7", {89, "enf_paind8", NULL, 0}},
  {"enf_paind8", {90, "enf_paind9", NULL, 0}},
  {"enf_paind9", {91, "enf_paind10", NULL, 0}},
  {"enf_paind10", {92, "enf_paind11", NULL, 0}},
  {"enf_paind11", {93, "enf_paind12", OPS_1396, 1}},
  {"enf_paind12", {94, "enf_paind13", OPS_1397, 1}},
  {"enf_paind13", {95, "enf_paind14", OPS_1398, 1}},
  {"enf_paind14", {96, "enf_paind15", NULL, 0}},
  {"enf_paind15", {97, "enf_paind16", NULL, 0}},
  {"enf_paind16", {98, "enf_paind17", OPS_1401, 1}},
  {"enf_paind17", {99, "enf_paind18", OPS_1402, 1}},
  {"enf_paind18", {100, "enf_paind19", NULL, 0}},
  {"enf_paind19", {101, "enf_run1", NULL, 0}},
  {"enf_die1", {41, "enf_die2", NULL, 0}},
  {"enf_die2", {42, "enf_die3", NULL, 0}},
  {"enf_die3", {43, "enf_die4", OPS_1407, 2}},
  {"enf_die4", {44, "enf_die5", OPS_1408, 1}},
  {"enf_die5", {45, "enf_die6", OPS_1409, 1}},
  {"enf_die6", {46, "enf_die7", NULL, 0}},
  {"enf_die7", {47, "enf_die8", NULL, 0}},
  {"enf_die8", {48, "enf_die9", NULL, 0}},
  {"enf_die9", {49, "enf_die10", OPS_1413, 1}},
  {"enf_die10", {50, "enf_die11", OPS_1414, 1}},
  {"enf_die11", {51, "enf_die12", OPS_1415, 1}},
  {"enf_die12", {52, "enf_die13", OPS_1416, 1}},
  {"enf_die13", {53, "enf_die14", NULL, 0}},
  {"enf_die14", {54, "enf_die14", NULL, 0}},
  {"enf_fdie1", {55, "enf_fdie2", NULL, 0}},
  {"enf_fdie2", {56, "enf_fdie3", NULL, 0}},
  {"enf_fdie3", {57, "enf_fdie4", OPS_1421, 2}},
  {"enf_fdie4", {58, "enf_fdie5", NULL, 0}},
  {"enf_fdie5", {59, "enf_fdie6", NULL, 0}},
  {"enf_fdie6", {60, "enf_fdie7", NULL, 0}},
  {"enf_fdie7", {61, "enf_fdie8", NULL, 0}},
  {"enf_fdie8", {62, "enf_fdie9", NULL, 0}},
  {"enf_fdie9", {63, "enf_fdie10", NULL, 0}},
  {"enf_fdie10", {64, "enf_fdie11", NULL, 0}},
  {"enf_fdie11", {65, "enf_fdie11", NULL, 0}},
  {"old_idle1", {0, "old_idle2", NULL, 0}},
  {"old_idle2", {1, "old_idle3", NULL, 0}},
  {"old_idle3", {2, "old_idle4", NULL, 0}},
  {"old_idle4", {3, "old_idle5", NULL, 0}},
  {"old_idle5", {4, "old_idle6", NULL, 0}},
  {"old_idle6", {5, "old_idle7", NULL, 0}},
  {"old_idle7", {6, "old_idle8", NULL, 0}},
  {"old_idle8", {7, "old_idle9", NULL, 0}},
  {"old_idle9", {8, "old_idle10", NULL, 0}},
  {"old_idle10", {9, "old_idle11", NULL, 0}},
  {"old_idle11", {10, "old_idle12", NULL, 0}},
  {"old_idle12", {11, "old_idle13", NULL, 0}},
  {"old_idle13", {12, "old_idle14", NULL, 0}},
  {"old_idle14", {13, "old_idle15", NULL, 0}},
  {"old_idle15", {14, "old_idle16", NULL, 0}},
  {"old_idle16", {15, "old_idle17", NULL, 0}},
  {"old_idle17", {16, "old_idle18", NULL, 0}},
  {"old_idle18", {17, "old_idle19", NULL, 0}},
  {"old_idle19", {18, "old_idle20", NULL, 0}},
  {"old_idle20", {19, "old_idle21", NULL, 0}},
  {"old_idle21", {20, "old_idle22", NULL, 0}},
  {"old_idle22", {21, "old_idle23", NULL, 0}},
  {"old_idle23", {22, "old_idle24", NULL, 0}},
  {"old_idle24", {23, "old_idle25", NULL, 0}},
  {"old_idle25", {24, "old_idle26", NULL, 0}},
  {"old_idle26", {25, "old_idle27", NULL, 0}},
  {"old_idle27", {26, "old_idle28", NULL, 0}},
  {"old_idle28", {27, "old_idle29", NULL, 0}},
  {"old_idle29", {28, "old_idle30", NULL, 0}},
  {"old_idle30", {29, "old_idle31", NULL, 0}},
  {"old_idle31", {30, "old_idle32", NULL, 0}},
  {"old_idle32", {31, "old_idle33", NULL, 0}},
  {"old_idle33", {32, "old_idle34", NULL, 0}},
  {"old_idle34", {33, "old_idle35", NULL, 0}},
  {"old_idle35", {34, "old_idle36", NULL, 0}},
  {"old_idle36", {35, "old_idle37", NULL, 0}},
  {"old_idle37", {36, "old_idle38", NULL, 0}},
  {"old_idle38", {37, "old_idle39", NULL, 0}},
  {"old_idle39", {38, "old_idle40", NULL, 0}},
  {"old_idle40", {39, "old_idle41", NULL, 0}},
  {"old_idle41", {40, "old_idle42", NULL, 0}},
  {"old_idle42", {41, "old_idle43", NULL, 0}},
  {"old_idle43", {42, "old_idle44", NULL, 0}},
  {"old_idle44", {43, "old_idle45", NULL, 0}},
  {"old_idle45", {44, "old_idle46", NULL, 0}},
  {"old_idle46", {45, "old_idle1", NULL, 0}},
  {"old_thrash1", {46, "old_thrash2", OPS_1476, 1}},
  {"old_thrash2", {47, "old_thrash3", OPS_1477, 1}},
  {"old_thrash3", {48, "old_thrash4", OPS_1478, 1}},
  {"old_thrash4", {49, "old_thrash5", OPS_1479, 1}},
  {"old_thrash5", {50, "old_thrash6", OPS_1480, 1}},
  {"old_thrash6", {51, "old_thrash7", OPS_1481, 1}},
  {"old_thrash7", {52, "old_thrash8", OPS_1482, 1}},
  {"old_thrash8", {53, "old_thrash9", OPS_1483, 1}},
  {"old_thrash9", {54, "old_thrash10", OPS_1484, 1}},
  {"old_thrash10", {55, "old_thrash11", OPS_1485, 1}},
  {"old_thrash11", {56, "old_thrash12", OPS_1486, 1}},
  {"old_thrash12", {57, "old_thrash13", OPS_1487, 1}},
  {"old_thrash13", {58, "old_thrash14", OPS_1488, 1}},
  {"old_thrash14", {59, "old_thrash15", OPS_1489, 1}},
  {"old_thrash15", {60, "old_thrash16", OPS_1490, 2}},
  {"old_thrash16", {61, "old_thrash17", OPS_1491, 1}},
  {"old_thrash17", {62, "old_thrash18", OPS_1492, 1}},
  {"old_thrash18", {63, "old_thrash19", OPS_1493, 1}},
  {"old_thrash19", {64, "old_thrash20", OPS_1494, 1}},
  {"old_thrash20", {65, "old_thrash20", OPS_1495, 1}},
};
const qa_q1_monster_frame_table QA_Q1_MONSTER_FRAMES = {ENTRIES, sizeof ENTRIES / sizeof *ENTRIES};

const qa_q1_monster_frame *qa_q1_monster_frames_get(const char *name) {
  return qa_q1_monster_frame_table_get(&QA_Q1_MONSTER_FRAMES, name);
}
bool qa_q1_monster_frames_has(const char *name) {
  return qa_q1_monster_frame_table_has(&QA_Q1_MONSTER_FRAMES, name);
}
