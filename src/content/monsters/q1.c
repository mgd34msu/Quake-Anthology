/* Port of src/content/monsters/q1.ts. Resource lists transcribed from the TS const tables. GPL-2.0-or-later. */
#include "qa/content/monsters/q1.h"

static const char *const res_0[] = {"progs/fish.mdl", "sound/fish/death.wav", "sound/fish/bite.wav", "sound/fish/idle.wav"};
static const char *const res_1[] = {"progs/soldier.mdl", "progs/h_guard.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/backpack.mdl", "sound/soldier/death1.wav", "sound/soldier/idle.wav", "sound/soldier/pain1.wav", "sound/soldier/pain2.wav", "sound/soldier/sattck1.wav", "sound/soldier/sight1.wav", "sound/player/udeath.wav", "sound/weapons/lock4.wav"};
static const char *const res_2[] = {"progs/dog.mdl", "progs/h_dog.mdl", "progs/gib3.mdl", "sound/dog/dattack1.wav", "sound/dog/ddeath.wav", "sound/dog/dpain1.wav", "sound/dog/dsight.wav", "sound/dog/idle.wav", "sound/player/udeath.wav"};
static const char *const res_3[] = {"progs/enforcer.mdl", "progs/h_mega.mdl", "progs/laser.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/backpack.mdl", "sound/enforcer/death1.wav", "sound/enforcer/enfire.wav", "sound/enforcer/enfstop.wav", "sound/enforcer/idle1.wav", "sound/enforcer/pain1.wav", "sound/enforcer/pain2.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/player/udeath.wav", "sound/weapons/lock4.wav"};
static const char *const res_4[] = {"progs/knight.mdl", "progs/h_knight.mdl", "sound/knight/kdeath.wav", "sound/knight/khurt.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/knight/idle.wav", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "sound/player/udeath.wav"};
static const char *const res_5[] = {"progs/demon.mdl", "progs/h_demon.mdl", "sound/demon/ddeath.wav", "sound/demon/dhit2.wav", "sound/demon/djump.wav", "sound/demon/dpain1.wav", "sound/demon/idle1.wav", "sound/demon/sight2.wav", "progs/gib1.mdl", "progs/zom_gib.mdl", "sound/player/udeath.wav"};
static const char *const res_6[] = {"progs/ogre.mdl", "progs/h_ogre.mdl", "progs/grenade.mdl", "sound/ogre/ogdrag.wav", "sound/ogre/ogdth.wav", "sound/ogre/ogidle.wav", "sound/ogre/ogidle2.wav", "sound/ogre/ogpain1.wav", "sound/ogre/ogsawatk.wav", "sound/ogre/ogwake.wav", "progs/gib3.mdl", "progs/zom_gib.mdl", "progs/backpack.mdl", "progs/s_explod.spr", "sound/player/udeath.wav", "sound/weapons/grenade.wav", "sound/weapons/r_exp3.wav", "sound/weapons/bounce.wav", "sound/weapons/lock4.wav"};
static const char *const res_7[] = {"progs/hknight.mdl", "progs/k_spike.mdl", "progs/h_hellkn.mdl", "sound/hknight/attack1.wav", "sound/hknight/death1.wav", "sound/hknight/pain1.wav", "sound/hknight/sight1.wav", "sound/hknight/hit.wav", "sound/hknight/slash1.wav", "sound/hknight/idle.wav", "sound/hknight/grunt.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "sound/player/udeath.wav"};
static const char *const res_8[] = {"progs/shambler.mdl", "progs/s_light.mdl", "progs/h_shams.mdl", "progs/bolt.mdl", "sound/shambler/sattck1.wav", "sound/shambler/sboom.wav", "sound/shambler/sdeath.wav", "sound/shambler/shurt2.wav", "sound/shambler/sidle.wav", "sound/shambler/ssight.wav", "sound/shambler/melee1.wav", "sound/shambler/melee2.wav", "sound/shambler/smack.wav", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/zom_gib.mdl", "sound/player/udeath.wav"};
static const char *const res_9[] = {"progs/wizard.mdl", "progs/h_wizard.mdl", "progs/w_spike.mdl", "sound/wizard/hit.wav", "sound/wizard/wattack.wav", "sound/wizard/wdeath.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wpain.wav", "sound/wizard/wsight.wav", "progs/gib2.mdl", "sound/player/udeath.wav"};
static const char *const res_10[] = {"progs/shalrath.mdl", "progs/h_shal.mdl", "progs/v_spike.mdl", "sound/shalrath/attack.wav", "sound/shalrath/attack2.wav", "sound/shalrath/death.wav", "sound/shalrath/idle.wav", "sound/shalrath/pain.wav", "sound/shalrath/sight.wav", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/s_explod.spr", "sound/player/udeath.wav", "sound/weapons/r_exp3.wav"};
static const char *const res_11[] = {"progs/tarbaby.mdl", "sound/blob/death1.wav", "sound/blob/hit1.wav", "sound/blob/land1.wav", "sound/blob/sight1.wav"};
static const char *const res_12[] = {"progs/zombie.mdl", "progs/h_zombie.mdl", "progs/zom_gib.mdl", "sound/zombie/z_idle.wav", "sound/zombie/z_idle1.wav", "sound/zombie/z_shot1.wav", "sound/zombie/z_gib.wav", "sound/zombie/z_pain.wav", "sound/zombie/z_pain1.wav", "sound/zombie/z_fall.wav", "sound/zombie/z_miss.wav", "sound/zombie/z_hit.wav", "sound/zombie/idle_w2.wav", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl"};

static const qa_monster_creature_entry qa_q1_monster_sources_creatures_0[] = {
  {"monster_fish", res_0, 4},
  {"monster_army", res_1, 14},
  {"monster_dog", res_2, 9},
  {"monster_enforcer", res_3, 19},
  {"monster_knight", res_4, 12},
  {"monster_demon1", res_5, 11},
  {"monster_ogre", res_6, 19},
  {"monster_ogre_marksman", res_6, 19},
  {"monster_hell_knight", res_7, 17},
  {"monster_shambler", res_8, 18},
  {"monster_wizard", res_9, 12},
  {"monster_shalrath", res_10, 15},
  {"monster_tarbaby", res_11, 5},
  {"monster_zombie", res_12, 16},
};

static const qa_monster_creature_entry qa_q1_monster_sources_creatures_1[] = {
  {"monster_fish", res_0, 4},
  {"monster_army", res_1, 14},
  {"monster_dog", res_2, 9},
  {"monster_enforcer", res_3, 19},
  {"monster_knight", res_4, 12},
  {"monster_demon1", res_5, 11},
  {"monster_ogre", res_6, 19},
  {"monster_ogre_marksman", res_6, 19},
  {"monster_hell_knight", res_7, 17},
  {"monster_shambler", res_8, 18},
  {"monster_wizard", res_9, 12},
  {"monster_shalrath", res_10, 15},
  {"monster_tarbaby", res_11, 5},
  {"monster_zombie", res_12, 16},
};

const qa_monster_source_definition qa_q1_monster_sources[] = {
  {"q1:monsters/classic/id1", "classic", "q1", "id1", qa_q1_monster_sources_creatures_0, 14},
  {"q1:monsters/rerelease/id1", "rerelease", "q1", "id1", qa_q1_monster_sources_creatures_1, 14},
};
const size_t qa_q1_monster_source_count = 2;
