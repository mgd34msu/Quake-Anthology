/* Port of src/content/monsters/expansions.ts. Resource lists generated from the TS const tables. GPL-2.0-or-later. */
#include "qa/content/monsters/expansions.h"

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
static const char *const res_13[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_scourg.mdl", "progs/scor.mdl", "progs/spike.mdl", "sound/misc/null.wav", "sound/player/udeath.wav", "sound/scourge/idle.wav", "sound/scourge/pain.wav", "sound/scourge/pain2.wav", "sound/scourge/sight.wav", "sound/scourge/tailswng.wav", "sound/scourge/walk.wav", "sound/shambler/smack.wav", "sound/weapons/rocket1i.wav"};
static const char *const res_14[] = {"progs/backpack.mdl", "progs/bolt.mdl", "progs/bolt2.mdl", "progs/bolt3.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/grem.mdl", "progs/grenade.mdl", "progs/h_grem.mdl", "progs/laser.mdl", "progs/missile.mdl", "progs/s_explod.spr", "progs/spike.mdl", "progs/zom_gib.mdl", "sound/demon/dhit2.wav", "sound/grem/attack.wav", "sound/grem/death.wav", "sound/grem/idle.wav", "sound/grem/sight1.wav", "sound/items/protect3.wav", "sound/player/udeath.wav", "sound/weapons/bounce.wav", "sound/weapons/grenade.wav", "sound/weapons/guncock.wav", "sound/weapons/lstart.wav", "sound/weapons/r_exp3.wav", "sound/weapons/ric1.wav", "sound/weapons/ric2.wav", "sound/weapons/ric3.wav", "sound/weapons/rocket1i.wav", "sound/weapons/sgun1.wav", "sound/weapons/shotgn2.wav", "sound/weapons/tink1.wav"};
static const char *const res_15[] = {"progs/armabody.mdl", "progs/armalegs.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/laser.mdl", "progs/missile.mdl", "progs/s_explod.spr", "sound/armagon/death.wav", "sound/armagon/footfall.wav", "sound/armagon/pain.wav", "sound/armagon/repel.wav", "sound/armagon/servo.wav", "sound/armagon/sight.wav", "sound/misc/longexpl.wav", "sound/player/udeath.wav", "sound/weapons/r_exp3.wav", "sound/weapons/sgun1.wav"};
static const char *const res_16[] = {"progs/demon.mdl", "progs/h_demon.mdl", "sound/demon/ddeath.wav", "sound/demon/dhit2.wav", "sound/demon/djump.wav", "sound/demon/dpain1.wav", "sound/demon/idle1.wav", "sound/demon/sight2.wav"};
static const char *const res_17[] = {"progs/eel2.mdl", "progs/eelgib.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "sound/eel/eactive1.wav", "sound/eel/eatt1.wav", "sound/eel/edie3r.wav", "sound/eel/eelc5.wav", "sound/eel/epain3.wav", "sound/player/udeath.wav"};
static const char *const res_18[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/sword.mdl", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/player/axhit2.wav", "sound/player/udeath.wav"};
static const char *const res_19[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/s_explod.spr", "progs/w_ball.mdl", "progs/wrath.mdl", "progs/wrthgib1.mdl", "progs/wrthgib2.mdl", "progs/wrthgib3.mdl", "sound/player/udeath.wav", "sound/weapons/r_exp3.wav", "sound/wrath/watt.wav", "sound/wrath/wdthc.wav", "sound/wrath/wpain.wav", "sound/wrath/wsee.wav"};
static const char *const res_20[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_zombie.mdl", "progs/mummy.mdl", "progs/zom_gib.mdl", "sound/player/udeath.wav", "sound/zombie/z_fall.wav", "sound/zombie/z_gib.wav", "sound/zombie/z_hit.wav", "sound/zombie/z_idle.wav", "sound/zombie/z_idle1.wav", "sound/zombie/z_miss.wav", "sound/zombie/z_pain.wav", "sound/zombie/z_pain1.wav", "sound/zombie/z_shot1.wav"};
static const char *const res_21[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/s_explod.spr", "progs/s_wrath.mdl", "progs/s_wrtgb2.mdl", "progs/s_wrtgb3.mdl", "progs/w_ball.mdl", "progs/wrthgib1.mdl", "progs/wrthgib2.mdl", "progs/wrthgib3.mdl", "sound/player/udeath.wav", "sound/s_wrath/smash.wav", "sound/weapons/r_exp3.wav", "sound/wrath/watt.wav", "sound/wrath/wpain.wav", "sound/wrath/wsee.wav"};
static const char *const res_22[] = {"progs/lavaball.mdl", "progs/lavaman.mdl", "sound/boss1/death.wav", "sound/boss1/out1.wav", "sound/boss1/sight1.wav", "sound/boss1/throw.wav"};
static const char *const res_23[] = {"progs/dragon.mdl", "progs/drggib01.mdl", "progs/drggib02.mdl", "progs/drggib03.mdl", "progs/fireball.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "sound/dragon/active.wav", "sound/dragon/attack.wav", "sound/dragon/death.wav", "sound/dragon/pain.wav", "sound/dragon/see.wav", "sound/player/udeath.wav"};
static const char *const res_24[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/laser.mdl", "progs/morph_az.mdl", "progs/morph_eg.mdl", "progs/morph_gr.mdl", "sound/enforcer/enfire.wav", "sound/guard/death.wav", "sound/guard/pain1.wav", "sound/guard/see1.wav", "sound/player/udeath.wav"};
static const char *const res_25[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/grenade.mdl", "progs/h_ogre.mdl", "progs/missile.mdl", "progs/ogre.mdl", "progs/ogre_rocket.mdl", "progs/s_explod.spr", "progs/zom_gib.mdl", "sound/armagon/idle1.wav", "sound/armagon/idle2.wav", "sound/armagon/idle3.wav", "sound/armagon/idle4.wav", "sound/armagon/pain.wav", "sound/armagon/sight.wav", "sound/armagon/sight2.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogdrag.wav", "sound/ogre/ogdth.wav", "sound/ogre/ogidle.wav", "sound/ogre/ogidle2.wav", "sound/ogre/ogpain1.wav", "sound/ogre/ogsawatk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/weapons/sgun1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_26[] = {"progs/dog_explosive.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/grenade.mdl", "progs/h_dog.mdl", "progs/s_explod.spr", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dattack1.wav", "sound/dog/ddeath.wav", "sound/dog/dpain1.wav", "sound/dog/dsight.wav", "sound/dog/idle.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/weapons/bounce.wav", "sound/weapons/grenade.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_27[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_guard.mdl", "progs/h_zombie.mdl", "progs/soldier.mdl", "progs/zom_gib.mdl", "progs/zombie.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/death1.wav", "sound/soldier/idle.wav", "sound/soldier/pain1.wav", "sound/soldier/pain2.wav", "sound/soldier/sattck1.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/idle_w2.wav", "sound/zombie/z_fall.wav", "sound/zombie/z_gib.wav", "sound/zombie/z_hit.wav", "sound/zombie/z_idle.wav", "sound/zombie/z_idle1.wav", "sound/zombie/z_miss.wav", "sound/zombie/z_pain.wav", "sound/zombie/z_pain1.wav", "sound/zombie/z_shot1.wav"};
static const char *const res_28[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_knight.mdl", "progs/h_zombie.mdl", "progs/knight.mdl", "progs/zom_gib.mdl", "progs/zombie.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/kdeath.wav", "sound/knight/khurt.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/idle_w2.wav", "sound/zombie/z_fall.wav", "sound/zombie/z_gib.wav", "sound/zombie/z_hit.wav", "sound/zombie/z_idle.wav", "sound/zombie/z_idle1.wav", "sound/zombie/z_miss.wav", "sound/zombie/z_pain.wav", "sound/zombie/z_pain1.wav", "sound/zombie/z_shot1.wav"};
static const char *const res_29[] = {"progs/demon.mdl", "progs/enforcer.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_demon.mdl", "progs/h_mega.mdl", "progs/laser.mdl", "progs/zom_gib.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/ddeath.wav", "sound/demon/dhit2.wav", "sound/demon/djump.wav", "sound/demon/dpain1.wav", "sound/demon/idle1.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/death1.wav", "sound/enforcer/enfire.wav", "sound/enforcer/enfstop.wav", "sound/enforcer/idle1.wav", "sound/enforcer/pain1.wav", "sound/enforcer/pain2.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_30[] = {"progs/demon.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_demon.mdl", "progs/h_hellkn.mdl", "progs/hknight.mdl", "progs/k_spike.mdl", "progs/spike.mdl", "progs/zom_gib.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/ddeath.wav", "sound/demon/dhit2.wav", "sound/demon/djump.wav", "sound/demon/dpain1.wav", "sound/demon/idle1.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/attack1.wav", "sound/hknight/death1.wav", "sound/hknight/grunt.wav", "sound/hknight/hit.wav", "sound/hknight/idle.wav", "sound/hknight/pain1.wav", "sound/hknight/sight1.wav", "sound/hknight/slash1.wav", "sound/infected/death1_rev.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_31[] = {"progs/diamond_trail.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_hellkn.mdl", "progs/rknight.mdl", "progs/spike.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/attack1.wav", "sound/hknight/hit.wav", "sound/hknight/sight1.wav", "sound/hknight/slash1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/death_01.wav", "sound/rknight/death_02.wav", "sound/rknight/idle_02.wav", "sound/rknight/idle_03.wav", "sound/rknight/idle_05.wav", "sound/rknight/pain_01.wav", "sound/rknight/pain_02.wav", "sound/rknight/pain_03.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_32[] = {"progs/bolt.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_shams.mdl", "progs/k_spike.mdl", "progs/rogue/plasma.mdl", "progs/s_light.mdl", "progs/shambler_blood.mdl", "progs/spike.mdl", "progs/zom_gib.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/attack1.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/melee1.wav", "sound/shambler/melee2.wav", "sound/shambler/sattck1.wav", "sound/shambler/sboom.wav", "sound/shambler/sdeath.wav", "sound/shambler/shurt2.wav", "sound/shambler/sidle.wav", "sound/shambler/smack.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav", "sound/zombie/z_shot1.wav"};
static const char *const res_33[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/lavaball.mdl", "progs/lavaman.mdl", "progs/s_explod.spr", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss1/death.wav", "sound/boss1/out1.wav", "sound/boss1/pain.wav", "sound/boss1/sight1.wav", "sound/boss1/throw.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/power.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/weapons/rocket1i.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_34[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/player.mdl", "progs/s_bubble.spr", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/player/death1.wav", "sound/player/death2.wav", "sound/player/death3.wav", "sound/player/death4.wav", "sound/player/death5.wav", "sound/player/h2odeath.wav", "sound/player/udeath.wav"};
static const char *const res_35[] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/rogue/sphere.mdl", "progs/spike.mdl", "progs/teleporter_eye_blink.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/idle.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/power.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/orb/orb_death.wav", "sound/orb/orb_pain.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/hit.wav", "sound/wizard/wattack.wav", "sound/wizard/wdeath.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wpain.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};
static const char *const res_36[] = {"progs/flame2.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/h_zombie.mdl", "progs/zom_gib.mdl", "progs/zombie.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/idle_w2.wav", "sound/zombie/z_fall.wav", "sound/zombie/z_gib.wav", "sound/zombie/z_hit.wav", "sound/zombie/z_idle.wav", "sound/zombie/z_idle1.wav", "sound/zombie/z_miss.wav", "sound/zombie/z_pain.wav", "sound/zombie/z_pain1.wav", "sound/zombie/z_shot1.wav"};
static const char *const res_37[] = {"progs/diamond.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/oldone.mdl", "progs/rogue/plasma.mdl", "progs/rogue/sphere.mdl", "progs/s_light.mdl", "progs/spike.mdl", "progs/teleport.mdl", "progs/teleporter_eye.mdl", "progs/zom_gib.mdl", "progs/zombie.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss2/death.wav", "sound/boss2/idle.wav", "sound/boss2/pop2.wav", "sound/boss2/sight.wav", "sound/demon/djump.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/idle.wav", "sound/knight/ksight.wav", "sound/knight/sword1.wav", "sound/knight/sword2.wav", "sound/misc/power.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/orb/orb_death.wav", "sound/orb/orb_pain.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/weapons/grenade.wav", "sound/weapons/lhit.wav", "sound/weapons/lstart.wav", "sound/weapons/spike2.wav", "sound/wizard/widle1.wav", "sound/wizard/widle2.wav", "sound/wizard/wsight.wav", "sound/zombie/z_fall.wav", "sound/zombie/z_gib.wav", "sound/zombie/z_hit.wav", "sound/zombie/z_idle.wav", "sound/zombie/z_idle1.wav", "sound/zombie/z_miss.wav", "sound/zombie/z_pain.wav", "sound/zombie/z_pain1.wav", "sound/zombie/z_shot1.wav"};
static const char *const res_38[] = {"progs/boss.mdl", "progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl", "progs/k_spike.mdl", "progs/lavaball.mdl", "progs/rogue/plasma.mdl", "progs/rogue/rubble.mdl", "progs/rogue/sphere.mdl", "progs/s_explod.spr", "progs/spike.mdl", "progs/teleport.mdl", "sound/armagon/sight.wav", "sound/blob/sight1.wav", "sound/boss1/death.wav", "sound/boss1/out1.wav", "sound/boss1/pain.wav", "sound/boss1/sight1.wav", "sound/boss1/throw.wav", "sound/boss2/pop2.wav", "sound/boss2/sight.wav", "sound/demon/sight2.wav", "sound/dog/dsight.wav", "sound/enforcer/sight1.wav", "sound/enforcer/sight2.wav", "sound/enforcer/sight3.wav", "sound/enforcer/sight4.wav", "sound/hknight/attack1.wav", "sound/hknight/sight1.wav", "sound/items/protect3.wav", "sound/knight/ksight.wav", "sound/misc/power.wav", "sound/misc/r_tele1.wav", "sound/misc/r_tele2.wav", "sound/misc/r_tele3.wav", "sound/misc/r_tele4.wav", "sound/misc/r_tele5.wav", "sound/misc/talk.wav", "sound/ogre/ogwake.wav", "sound/player/udeath.wav", "sound/rknight/sight_01.wav", "sound/rknight/sight_03.wav", "sound/shalrath/sight.wav", "sound/shambler/ssight.wav", "sound/soldier/sight1.wav", "sound/weapons/rocket1i.wav", "sound/wizard/wsight.wav", "sound/zombie/z_idle.wav"};

static const qa_monster_creature_entry qa_q1_expansion_monster_sources_creatures_0[] = {
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
  {"monster_scourge", res_13, 16},
  {"monster_gremlin", res_14, 34},
  {"monster_armagon", res_15, 18},
  {"monster_spikemine", res_16, 8},
};

static const qa_monster_creature_entry qa_q1_expansion_monster_sources_creatures_1[] = {
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
  {"monster_eel", res_17, 11},
  {"monster_sword", res_18, 8},
  {"monster_wrath", res_19, 15},
  {"monster_mummy", res_20, 16},
  {"monster_super_wrath", res_21, 17},
  {"monster_lava_man", res_22, 6},
  {"monster_dragon", res_23, 14},
  {"monster_morph", res_24, 12},
};

static const qa_monster_creature_entry qa_q1_expansion_monster_sources_creatures_2[] = {
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
  {"monster_scourge", res_13, 16},
  {"monster_gremlin", res_14, 34},
  {"monster_armagon", res_15, 18},
  {"monster_spikemine", res_16, 8},
};

static const qa_monster_creature_entry qa_q1_expansion_monster_sources_creatures_3[] = {
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
  {"monster_eel", res_17, 11},
  {"monster_sword", res_18, 8},
  {"monster_wrath", res_19, 15},
  {"monster_mummy", res_20, 16},
  {"monster_super_wrath", res_21, 17},
  {"monster_lava_man", res_22, 6},
  {"monster_dragon", res_23, 14},
  {"monster_morph", res_24, 12},
};

const qa_monster_source_definition qa_q1_expansion_monster_sources[] = {
  {"q1:monsters/classic/hipnotic", "classic", "q1", "hipnotic", qa_q1_expansion_monster_sources_creatures_0, 18},
  {"q1:monsters/classic/rogue", "classic", "q1", "rogue", qa_q1_expansion_monster_sources_creatures_1, 22},
  {"q1:monsters/rerelease/hipnotic", "rerelease", "q1", "hipnotic", qa_q1_expansion_monster_sources_creatures_2, 18},
  {"q1:monsters/rerelease/rogue", "rerelease", "q1", "rogue", qa_q1_expansion_monster_sources_creatures_3, 22},
};
const size_t qa_q1_expansion_monster_source_count = 4;

static const qa_monster_creature_entry qa_q1_addon_monster_sources_creatures_0[] = {
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

static const qa_monster_creature_entry qa_q1_addon_monster_sources_creatures_1[] = {
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

static const qa_monster_creature_entry qa_q1_addon_monster_sources_creatures_2[] = {
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
  {"monster_ogre_rocket", res_25, 56},
  {"monster_demodog", res_26, 46},
  {"monster_army_infected", res_27, 55},
  {"monster_knight_infected", res_28, 52},
  {"monster_enforcer_infected", res_29, 52},
  {"monster_hell_knight_infected", res_30, 55},
  {"monster_ranged_knight", res_31, 51},
  {"monster_super_shambler", res_32, 54},
  {"monster_lava_man", res_33, 41},
  {"monster_ghost", res_34, 17},
  {"monster_orb", res_35, 47},
  {"monster_szombie", res_36, 44},
  {"monster_oldone_new", res_37, 64},
  {"monster_boss_final", res_38, 48},
};

const qa_monster_source_definition qa_q1_addon_monster_sources[] = {
  {"q1:monsters/rerelease/dopa", "rerelease", "q1", "dopa", qa_q1_addon_monster_sources_creatures_0, 14},
  {"q1:monsters/rerelease/mg1", "rerelease", "q1", "mg1", qa_q1_addon_monster_sources_creatures_1, 14},
  {"q1:monsters/rerelease/mg3", "rerelease", "q1", "mg3", qa_q1_addon_monster_sources_creatures_2, 28},
};
const size_t qa_q1_addon_monster_source_count = 3;
