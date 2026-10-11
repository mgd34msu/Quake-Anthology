#ifndef QA_Q2_INTERNAL_H
#define QA_Q2_INTERNAL_H
#include "qa/game_q2.h"
#include "qa/q2_sound.h"
#include "qa/target_keys.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_source.h"
#include "qa/pool.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define Q2_RUNTIME_NAME_LIST(X) \
    X(MONSTER_FLYER, "monster_flyer") \
    X(MONSTER_KAMIKAZE, "monster_kamikaze") \
    X(MONSTER_STALKER, "monster_stalker") \
    X(PARASITE_PROBOSCIS, "parasite_proboscis") \
    X(PARASITE_PROBOSCIS_SEGMENT, "parasite_proboscis_segment") \
    X(PROBOSCIS_TIP_MODEL, "models/monsters/parasite/tip/tris.md2") \
    X(PROBOSCIS_SEGMENT_MODEL, "models/monsters/parasite/segment/tris.md2") \
    X(RESOURCE_MODELS_CTF_BANNER_SMALL_MD2, "models/ctf/banner/small.md2") \
    X(RESOURCE_MODELS_CTF_BANNER_TRIS_MD2, "models/ctf/banner/tris.md2") \
    X(RESOURCE_MODELS_DEADBODS_DUDE_TRIS_MD2, "models/deadbods/dude/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BERSERK_GIBS_CHEST_MD2, "models/monsters/berserk/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_BERSERK_GIBS_HAMMER_MD2, "models/monsters/berserk/gibs/hammer.md2") \
    X(RESOURCE_MODELS_MONSTERS_BERSERK_GIBS_HEAD_MD2, "models/monsters/berserk/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_BERSERK_GIBS_THIGH_MD2, "models/monsters/berserk/gibs/thigh.md2") \
    X(RESOURCE_MODELS_MONSTERS_BITCH_GIBS_ARM_MD2, "models/monsters/bitch/gibs/arm.md2") \
    X(RESOURCE_MODELS_MONSTERS_BITCH_GIBS_CHEST_MD2, "models/monsters/bitch/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_BITCH_GIBS_FOOT_MD2, "models/monsters/bitch/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_BITCH_GIBS_HEAD_MD2, "models/monsters/bitch/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_BITCH_GIBS_TUBE_MD2, "models/monsters/bitch/gibs/tube.md2") \
    X(RESOURCE_MODELS_MONSTERS_BITCH_TRIS_MD2, "models/monsters/bitch/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW_GIB1_TRIS_MD2, "models/monsters/blackwidow/gib1/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW_GIB2_TRIS_MD2, "models/monsters/blackwidow/gib2/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW_GIB3_TRIS_MD2, "models/monsters/blackwidow/gib3/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW2_GIB1_TRIS_MD2, "models/monsters/blackwidow2/gib1/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW2_GIB2_TRIS_MD2, "models/monsters/blackwidow2/gib2/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW2_GIB3_TRIS_MD2, "models/monsters/blackwidow2/gib3/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BLACKWIDOW2_GIB4_TRIS_MD2, "models/monsters/blackwidow2/gib4/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_CGUN_MD2, "models/monsters/boss1/gibs/cgun.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_CHEST_MD2, "models/monsters/boss1/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_CORE_MD2, "models/monsters/boss1/gibs/core.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_HEAD_MD2, "models/monsters/boss1/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_LTREAD_MD2, "models/monsters/boss1/gibs/ltread.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_RGUN_MD2, "models/monsters/boss1/gibs/rgun.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_RTREAD_MD2, "models/monsters/boss1/gibs/rtread.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS1_GIBS_TUBE_MD2, "models/monsters/boss1/gibs/tube.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_CHAINGUN_MD2, "models/monsters/boss2/gibs/chaingun.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_CHEST_MD2, "models/monsters/boss2/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_CPU_MD2, "models/monsters/boss2/gibs/cpu.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_ENGINE_MD2, "models/monsters/boss2/gibs/engine.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_HEAD_MD2, "models/monsters/boss2/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_LARM_MD2, "models/monsters/boss2/gibs/larm.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_RARM_MD2, "models/monsters/boss2/gibs/rarm.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_ROCKET_MD2, "models/monsters/boss2/gibs/rocket.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_SPINE_MD2, "models/monsters/boss2/gibs/spine.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS2_GIBS_WING_MD2, "models/monsters/boss2/gibs/wing.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_CHEST_MD2, "models/monsters/boss3/jorg/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_FOOT_MD2, "models/monsters/boss3/jorg/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_GUN_MD2, "models/monsters/boss3/jorg/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_HEAD_MD2, "models/monsters/boss3/jorg/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_SPIKE_MD2, "models/monsters/boss3/jorg/gibs/spike.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_SPINE_MD2, "models/monsters/boss3/jorg/gibs/spine.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_THIGH_MD2, "models/monsters/boss3/jorg/gibs/thigh.md2") \
    X(RESOURCE_MODELS_MONSTERS_BOSS3_JORG_GIBS_TUBE_MD2, "models/monsters/boss3/jorg/gibs/tube.md2") \
    X(RESOURCE_MODELS_MONSTERS_BRAIN_GIBS_ARM_MD2, "models/monsters/brain/gibs/arm.md2") \
    X(RESOURCE_MODELS_MONSTERS_BRAIN_GIBS_BOOT_MD2, "models/monsters/brain/gibs/boot.md2") \
    X(RESOURCE_MODELS_MONSTERS_BRAIN_GIBS_CHEST_MD2, "models/monsters/brain/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_BRAIN_GIBS_DOOR_MD2, "models/monsters/brain/gibs/door.md2") \
    X(RESOURCE_MODELS_MONSTERS_BRAIN_GIBS_HEAD_MD2, "models/monsters/brain/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_BRAIN_GIBS_PELVIS_MD2, "models/monsters/brain/gibs/pelvis.md2") \
    X(RESOURCE_MODELS_MONSTERS_COMMANDR_TRIS_MD2, "models/monsters/commandr/tris.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLOAT_GIBS_BASE_MD2, "models/monsters/float/gibs/base.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLOAT_GIBS_GUN_MD2, "models/monsters/float/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLOAT_GIBS_JAR_MD2, "models/monsters/float/gibs/jar.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLOAT_GIBS_PIECE_MD2, "models/monsters/float/gibs/piece.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLYER_GIBS_BASE_MD2, "models/monsters/flyer/gibs/base.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLYER_GIBS_GUN_MD2, "models/monsters/flyer/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLYER_GIBS_HEAD_MD2, "models/monsters/flyer/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_FLYER_GIBS_WING_MD2, "models/monsters/flyer/gibs/wing.md2") \
    X(RESOURCE_MODELS_MONSTERS_GLADIATR_GIBS_CHEST_MD2, "models/monsters/gladiatr/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_GLADIATR_GIBS_HEAD_MD2, "models/monsters/gladiatr/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_GLADIATR_GIBS_LARM_MD2, "models/monsters/gladiatr/gibs/larm.md2") \
    X(RESOURCE_MODELS_MONSTERS_GLADIATR_GIBS_RARM_MD2, "models/monsters/gladiatr/gibs/rarm.md2") \
    X(RESOURCE_MODELS_MONSTERS_GLADIATR_GIBS_THIGH_MD2, "models/monsters/gladiatr/gibs/thigh.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB1_MD2, "models/monsters/guardian/gib1.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB2_MD2, "models/monsters/guardian/gib2.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB3_MD2, "models/monsters/guardian/gib3.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB4_MD2, "models/monsters/guardian/gib4.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB5_MD2, "models/monsters/guardian/gib5.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB6_MD2, "models/monsters/guardian/gib6.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUARDIAN_GIB7_MD2, "models/monsters/guardian/gib7.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUNNER_GIBS_CHEST_MD2, "models/monsters/gunner/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUNNER_GIBS_FOOT_MD2, "models/monsters/gunner/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUNNER_GIBS_GARM_MD2, "models/monsters/gunner/gibs/garm.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUNNER_GIBS_GUN_MD2, "models/monsters/gunner/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_GUNNER_GIBS_HEAD_MD2, "models/monsters/gunner/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_HOVER_GIBS_CHEST_MD2, "models/monsters/hover/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_HOVER_GIBS_FOOT_MD2, "models/monsters/hover/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_HOVER_GIBS_HEAD_MD2, "models/monsters/hover/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_HOVER_GIBS_RING_MD2, "models/monsters/hover/gibs/ring.md2") \
    X(RESOURCE_MODELS_MONSTERS_INFANTRY_GIBS_ARM_MD2, "models/monsters/infantry/gibs/arm.md2") \
    X(RESOURCE_MODELS_MONSTERS_INFANTRY_GIBS_CHEST_MD2, "models/monsters/infantry/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_INFANTRY_GIBS_FOOT_MD2, "models/monsters/infantry/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_INFANTRY_GIBS_GUN_MD2, "models/monsters/infantry/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_INFANTRY_GIBS_HEAD_MD2, "models/monsters/infantry/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_MEDIC_GIBS_CHEST_MD2, "models/monsters/medic/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_MEDIC_GIBS_GUN_MD2, "models/monsters/medic/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_MEDIC_GIBS_HEAD_MD2, "models/monsters/medic/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_MEDIC_GIBS_HOOK_MD2, "models/monsters/medic/gibs/hook.md2") \
    X(RESOURCE_MODELS_MONSTERS_MEDIC_GIBS_LEG_MD2, "models/monsters/medic/gibs/leg.md2") \
    X(RESOURCE_MODELS_MONSTERS_MUTANT_GIBS_CHEST_MD2, "models/monsters/mutant/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_MUTANT_GIBS_FOOT_MD2, "models/monsters/mutant/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_MUTANT_GIBS_HAND_MD2, "models/monsters/mutant/gibs/hand.md2") \
    X(RESOURCE_MODELS_MONSTERS_MUTANT_GIBS_HEAD_MD2, "models/monsters/mutant/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_PARASITE_GIBS_BLEG_MD2, "models/monsters/parasite/gibs/bleg.md2") \
    X(RESOURCE_MODELS_MONSTERS_PARASITE_GIBS_CHEST_MD2, "models/monsters/parasite/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_PARASITE_GIBS_FLEG_MD2, "models/monsters/parasite/gibs/fleg.md2") \
    X(RESOURCE_MODELS_MONSTERS_PARASITE_GIBS_HEAD_MD2, "models/monsters/parasite/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_SOLDIER_GIBS_ARM_MD2, "models/monsters/soldier/gibs/arm.md2") \
    X(RESOURCE_MODELS_MONSTERS_SOLDIER_GIBS_CHEST_MD2, "models/monsters/soldier/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_SOLDIER_GIBS_GUN_MD2, "models/monsters/soldier/gibs/gun.md2") \
    X(RESOURCE_MODELS_MONSTERS_SOLDIER_GIBS_HEAD_MD2, "models/monsters/soldier/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_TANK_GIBS_BARM_MD2, "models/monsters/tank/gibs/barm.md2") \
    X(RESOURCE_MODELS_MONSTERS_TANK_GIBS_CHEST_MD2, "models/monsters/tank/gibs/chest.md2") \
    X(RESOURCE_MODELS_MONSTERS_TANK_GIBS_FOOT_MD2, "models/monsters/tank/gibs/foot.md2") \
    X(RESOURCE_MODELS_MONSTERS_TANK_GIBS_HEAD_MD2, "models/monsters/tank/gibs/head.md2") \
    X(RESOURCE_MODELS_MONSTERS_TANK_GIBS_THIGH_MD2, "models/monsters/tank/gibs/thigh.md2") \
    X(RESOURCE_MODELS_MONSTERS_TANK_TRIS_MD2, "models/monsters/tank/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_BANNER_TRIS_MD2, "models/objects/banner/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_BARRELS_TRIS_MD2, "models/objects/barrels/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_BLACK_TRIS_MD2, "models/objects/black/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_BOMB_TRIS_MD2, "models/objects/bomb/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_DEBRIS1_TRIS_MD2, "models/objects/debris1/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_DEBRIS2_TRIS_MD2, "models/objects/debris2/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_DEBRIS3_TRIS_MD2, "models/objects/debris3/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_ARM_TRIS_MD2, "models/objects/gibs/arm/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_BONE_TRIS_MD2, "models/objects/gibs/bone/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_BONE2_TRIS_MD2, "models/objects/gibs/bone2/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_GEAR_TRIS_MD2, "models/objects/gibs/gear/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_HEAD_TRIS_MD2, "models/objects/gibs/head/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_HEAD2_TRIS_MD2, "models/objects/gibs/head2/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_LEG_TRIS_MD2, "models/objects/gibs/leg/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_SKULL_TRIS_MD2, "models/objects/gibs/skull/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_SM_METAL_TRIS_MD2, "models/objects/gibs/sm_metal/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_MINELITE_LIGHT1_TRIS_MD2, "models/objects/minelite/light1/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_MINELITE_LIGHT2_TRIS_MD2, "models/objects/minelite/light2/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_SATELLITE_TRIS_MD2, "models/objects/satellite/tris.md2") \
    X(RESOURCE_MODELS_SHIPS_BIGVIPER_TRIS_MD2, "models/ships/bigviper/tris.md2") \
    X(RESOURCE_I_HEALTH, "i_health") \
    X(FUNC_PLAT2, "func_plat2") \
    X(FUNC_DOOR_SECRET2, "func_door_secret2") \
    X(FUNC_DOOR_ROTATING, "func_door_rotating") \
    X(FUNC_TRAIN, "func_train") \
    X(MISC_EXPLOBOX, "misc_explobox") \
    X(GRENADE, "grenade") \
    X(POINT_COMBAT, "point_combat") \
    X(PATH_CORNER, "path_corner") \
    X(TESLA, "tesla") \
    X(TESLA_MINE, "tesla_mine") \
    X(MONSTER_GEKK, "monster_gekk") \
    X(BODYQUE, "bodyque") \
    X(WORLDSPAWN, "worldspawn") \
    X(GIB, "gib") \
    X(FUNC_WATER, "func_water") \
    X(INFO_PLAYER_COOP, "info_player_coop") \
    X(INFO_PLAYER_COOP_LAVA, "info_player_coop_lava") \
    X(INFO_PLAYER_DEATHMATCH, "info_player_deathmatch") \
    X(INFO_PLAYER_INTERMISSION, "info_player_intermission") \
    X(INFO_PLAYER_START, "info_player_start") \
    X(INFO_PLAYER_TEAM1, "info_player_team1") \
    X(INFO_PLAYER_TEAM2, "info_player_team2") \
    X(MISC_TELEPORTER_DEST, "misc_teleporter_dest") \
    X(LIGHT, "light") \
    X(MISC_VIPER, "misc_viper") \
    X(TARGET_CHANGELEVEL, "target_changelevel") \
    X(FUNC_AREAPORTAL, "func_areaportal") \
    X(LASER_SPARKS, "q2:laser-sparks") \
    X(NOCLASS, "noclass") \
    X(GRAPPLE, "grapple") \
    X(SPAWNGRO_BEAM, "spawngro_beam") \
    X(PLASMA_BOUNCE_MESSAGE, "bounce plasma\n") \
    X(PLASMA_SPREAD_MESSAGE, "spread plasma\n") \
    X(ZERO, "0") \
    X(SPACE, " ") \
    X(PAIN_DAEMON, "pain daemon") \
    X(WELDING_SPARKS, "q2:welding-sparks") \
    X(RESOURCE_Q2_ROCKET_EXPLOSION_WATER, "q2:rocket-explosion-water") \
    X(RESOURCE_Q2_ROCKET_EXPLOSION, "q2:rocket-explosion") \
    X(RESOURCE_Q2_GRENADE_EXPLOSION_WATER, "q2:grenade-explosion-water") \
    X(RESOURCE_Q2_GRENADE_EXPLOSION, "q2:grenade-explosion") \
    X(RESOURCE_Q2_PLASMA_EXPLOSION, "q2:plasma-explosion") \
    X(RESOURCE_BOLT, "bolt") \
    X(RESOURCE_LOOGIE, "loogie") \
    X(RESOURCE_ROCKET, "rocket") \
    X(RESOURCE_BFG_BLAST, "bfg blast") \
    X(RESOURCE_ION, "ion") \
    X(RESOURCE_PLASMA, "plasma") \
    X(RESOURCE_FLECHETTE, "flechette") \
    X(RESOURCE_TRACKER, "tracker") \
    X(RESOURCE_HAND_GRENADE, "hand_grenade") \
    X(RESOURCE_HGRENADE, "hgrenade") \
    X(RESOURCE_MODELS_OBJECTS_LASER_TRIS_MD2, "models/objects/laser/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_LOOGY_TRIS_MD2, "models/objects/loogy/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_ROCKET_TRIS_MD2, "models/objects/rocket/tris.md2") \
    X(RESOURCE_SPRITES_S_BFG1_SP2, "sprites/s_bfg1.sp2") \
    X(RESOURCE_MODELS_OBJECTS_BOOMRANG_TRIS_MD2, "models/objects/boomrang/tris.md2") \
    X(RESOURCE_SPRITES_S_PHOTON_SP2, "sprites/s_photon.sp2") \
    X(RESOURCE_MODELS_PROJ_FLECHETTE_TRIS_MD2, "models/proj/flechette/tris.md2") \
    X(RESOURCE_MODELS_PROJ_DISINTEGRATOR_TRIS_MD2, "models/proj/disintegrator/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GRENADE3_TRIS_MD2, "models/objects/grenade3/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GRENADE2_TRIS_MD2, "models/objects/grenade2/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GRENADE4_TRIS_MD2, "models/objects/grenade4/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GRENADE_TRIS_MD2, "models/objects/grenade/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_BLASER_TRIS_MD2, "models/objects/blaser/tris.md2") \
    X(RESOURCE_MODELS_PROJ_LASER2_TRIS_MD2, "models/proj/laser2/tris.md2") \
    X(RESOURCE_PROX_MINE, "prox_mine") \
    X(RESOURCE_PROX, "prox") \
    X(RESOURCE_FOOD_CUBE_TRAP, "food_cube_trap") \
    X(RESOURCE_HTRAP, "htrap") \
    X(RESOURCE_MODELS_WEAPONS_G_PROX_TRIS_MD2, "models/weapons/g_prox/tris.md2") \
    X(RESOURCE_MODELS_WEAPONS_G_TESLA_TRIS_MD2, "models/weapons/g_tesla/tris.md2") \
    X(RESOURCE_MODELS_WEAPONS_Z_TRAP_TRIS_MD2, "models/weapons/z_trap/tris.md2") \
    X(RESOURCE_Q2_EXPLOSION1, "q2:explosion1") \
    X(RESOURCE_Q2_EXPLOSION1_BIG, "q2:explosion1_big") \
    X(RESOURCE_Q2_WIDOWBEAMOUT, "q2:widowbeamout") \
    X(RESOURCE_Q2_WIDOWSPLASH, "q2:widowsplash") \
    X(RESOURCE_Q2_BOSSTPORT, "q2:bosstport") \
    X(RESOURCE_Q2_EXPLOSION1_BIG_2, "q2:explosion1-big") \
    X(RESOURCE_Q2_NUKEBLAST, "q2:nukeblast") \
    X(RESOURCE_MODELS_WEAPONS_G_NUKE_TRIS_MD2, "models/weapons/g_nuke/tris.md2") \
    X(RESOURCE_Q2_CHAINFIST_SMOKE, "q2:chainfist-smoke") \
    X(RESOURCE_MODELS_ITEMS_SPAWNGRO2_TRIS_MD2, "models/items/spawngro2/tris.md2") \
    X(RESOURCE_MODELS_ITEMS_SPAWNGRO3_TRIS_MD2, "models/items/spawngro3/tris.md2") \
    X(RESOURCE_MODELS_ITEMS_SPAWNGRO_TRIS_MD2, "models/items/spawngro/tris.md2") \
    X(RESOURCE_Q2_BLASTER, "q2:blaster") \
    X(RESOURCE_Q2_SPARKS, "q2:sparks") \
    X(RESOURCE_I_POWERSCREEN, "i_powerscreen") \
    X(RESOURCE_I_POWERSHIELD, "i_powershield") \
    X(RESOURCE_I_HELP, "i_help") \
    X(RESOURCE_Q2_GUNSHOT, "q2:gunshot") \
    X(RESOURCE_Q2_SHOTGUN, "q2:shotgun") \
    X(RESOURCE_Q2_BUBBLE_TRAIL, "q2:bubble-trail") \
    X(RESOURCE_Q2_RAIL, "q2:rail") \
    X(RESOURCE_Q2_RAIL_WATER, "q2:rail-water") \
    X(RESOURCE_Q2_HEATBEAM, "q2:heatbeam") \
    X(RESOURCE_Q2_MONSTER_HEATBEAM, "q2:monster-heatbeam") \
    X(RESOURCE_Q2_HEATBEAM_SPARKS, "q2:heatbeam_sparks") \
    X(RESOURCE_Q2_HEATBEAM_STEAM, "q2:heatbeam_steam") \
    X(RESOURCE_MODELS_OBJECTS_GHOOK_TRIS_MD2, "models/objects/ghook/tris.md2") \
    X(RESOURCE_MODELS_WEAPONS_GRAPPLE_HOOK_TRIS_MD2, "models/weapons/grapple/hook/tris.md2") \
    X(RESOURCE_SPRITES_S_PLASMA1_SP2, "sprites/s_plasma1.sp2") \
    X(RESOURCE_SPRITES_S_PLASMA2_SP2, "sprites/s_plasma2.sp2") \
    X(RESOURCE_Q2_BLASTER2, "q2:blaster2") \
    X(RESOURCE_Q2_BFG_EXPLOSION, "q2:bfg-explosion") \
    X(RESOURCE_Q2_BFG_ZAP, "q2:bfg-zap") \
    X(RESOURCE_Q2_BFG_LASER, "q2:bfg-laser") \
    X(RESOURCE_Q2_TRACKER_EXPLOSION, "q2:tracker-explosion") \
    X(RESOURCE_Q2_FLECHETTE, "q2:flechette") \
    X(RESOURCE_SPRITES_S_BFG3_SP2, "sprites/s_bfg3.sp2") \
    X(RESOURCE_Q2_BFG_BIGEXPLOSION, "q2:bfg-bigexplosion") \
    X(RESOURCE_MODELS_MONSTERS_LEGS_TRIS_MD2, "models/monsters/legs/tris.md2") \
    X(RESOURCE_Q2_TELEPORT_EFFECT, "q2:teleport_effect") \
    X(RESOURCE_Q2_PLAYER_TELEPORT, "q2:player-teleport") \
    X(RESOURCE_Q2_MONSTER_MUZZLE, "q2:monster-muzzle") \
    X(RESOURCE_EMPTY, "") \
    X(RESOURCE_Q2_OTHER_TELEPORT, "q2:other-teleport") \
    X(RESOURCE_Q2_TUNNEL_SPARKS, "q2:tunnel-sparks") \
    X(RESOURCE_MODELS_OBJECTS_GEKKGIB_TORSO_TRIS_MD2, "models/objects/gekkgib/torso/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_CHEST_TRIS_MD2, "models/objects/gibs/chest/tris.md2") \
    X(RESOURCE_MODELS_OBJECTS_GIBS_SM_MEAT_TRIS_MD2, "models/objects/gibs/sm_meat/tris.md2") \
    X(RESOURCE_Q2_EXPLOSION1_NP, "q2:explosion1_np") \
    X(RESOURCE_Q2_BERSERK_SLAM, "q2:berserk-slam") \
    X(RESOURCE_Q2_PARASITE, "q2:parasite") \
    X(RESOURCE_Q2_MEDIC_CABLE, "q2:medic-cable") \
    X(RESOURCE_Q2_EXPLOSION1_NL, "q2:explosion1-nl") \
    X(RESOURCE_MODELS_PROJ_LIGHTNING_TRIS_MD2, "models/proj/lightning/tris.md2") \
    X(RESOURCE_Q2_PLAIN_EXPLOSION, "q2:plain-explosion") \
    X(RESOURCE_Q2_ENTITY_EVENT, "q2:entity-event") \
    X(RESOURCE_Q2_LIGHTNING, "q2:lightning") \
    X(RESOURCE_Q2_SPLASH, "q2:splash") \
    X(RESOURCE_NUKE, "nuke") \
    X(RESOURCE_Q2_AMMO_NUKE, "q2:ammo_nuke") \
    X(RESOURCE_MODELS_WEAPONS_V_BEAMER2_TRIS_MD2, "models/weapons/v_beamer2/tris.md2") \
    X(RESOURCE_MODELS_WEAPONS_V_TESLA2_TRIS_MD2, "models/weapons/v_tesla2/tris.md2") \
    X(RESOURCE_DEBRIS, "debris") \
    X(RESOURCE_SPAWNGRO, "spawngro") \
    X(RESOURCE_Q2_GRAPPLE_CABLE, "q2:grapple-cable") \
    X(RESOURCE_GOOP, "goop") \
    X(RESOURCE_Q2_LASER_SPARKS, "q2:laser_sparks") \
    X(RESOURCE_TRAP_GIB, "trap_gib") \
    X(RESOURCE_BAD_AREA, "bad_area") \
    X(RESOURCE_Q2_MONSTER_POWER, "q2:monster-power") \
    X(RESOURCE_DABEAM, "dabeam") \
    X(RESOURCE_BOSS_EXPLODER, "boss_exploder") \
    X(RESOURCE_MONSTER_MAKRON, "monster_makron") \
    X(RESOURCE_WIDOWLEGS, "widowlegs") \
    X(RESOURCE_SPHERE, "sphere") \
    X(RESOURCE_MODELS_ITEMS_SHELL_TRIS_MD2, "models/items/shell/tris.md2") \
    X(RESOURCE_DOPPLEGANGER, "doppleganger") \
    X(RESOURCE_DOPPLEGANGER_BODY, "doppleganger_body") \
    X(RESOURCE_FOODCUBE, "foodcube") \
    X(RESOURCE_MODELS_OBJECTS_DMSPOT_TRIS_MD2, "models/objects/dmspot/tris.md2") \
    X(RESOURCE_JAIL3, "jail3") \
    X(RESOURCE_Q2_LOGOUT, "q2:logout") \
    X(RESOURCE_G_COOP_LOSE, "$g_coop_lose") \
    X(RESOURCE_MODELS_OBJECTS_LIGHT_TRIS_MD2, "models/objects/light/tris.md2") \
    X(RESOURCE_OBJECT_REPAIR, "object_repair") \
    X(RESOURCE_BRUSH_WORLD, "*0") \
    X(RESOURCE_FACT3_SECRET1, "fact3$secret1") \
    X(RESOURCE_FRIEND, "friend") \
    X(RESOURCE_UNIT1, "unit1_") \
    X(RESOURCE_G_COOP_WAIT_FOR_PLAYERS, "$g_coop_wait_for_players") \
    X(RESOURCE_G_COOP_PLAYERS_WAITING_FOR_YOU, "$g_coop_players_waiting_for_you") \
    X(RESOURCE_MUSIC_STOP, "0") \
    X(RESOURCE_PROX_FIELD, "prox_field") \
    X(RESOURCE_TESLA_TRIGGER, "tesla trigger") \
    X(RESOURCE_MODELS_ITEMS_DEFENDER_TRIS_MD2, "models/items/defender/tris.md2") \
    X(RESOURCE_MODELS_ITEMS_HUNTER_TRIS_MD2, "models/items/hunter/tris.md2") \
    X(RESOURCE_MODELS_ITEMS_VENGNCE_TRIS_MD2, "models/items/vengnce/tris.md2")

typedef uint32_t q2_runtime_name;
enum {
    Q2_NAME_NONE = QA_Q2_SFX_NONE,
    Q2_NAME_RESOURCE_BASE = QA_Q2_SFX_COUNT - 1,
#define Q2_RUNTIME_NAME_ENUM(key, text) Q2_NAME_##key,
    Q2_RUNTIME_NAME_LIST(Q2_RUNTIME_NAME_ENUM)
#undef Q2_RUNTIME_NAME_ENUM
    Q2_NAME_COUNT
};

#define Q2_NS UINT64_C(1000000000)
#define Q2_MS UINT64_C(1000000)
#define Q2_SHOT_MASK UINT32_C(0x06000003)
#define Q2_PROJECTILE_MASK UINT32_C(0x46004003)
#define Q2_PLAYER_CONTENTS UINT32_C(0x40000000)
#define Q2_WATER_MASK UINT32_C(56)

typedef enum q2_projectile_kind {
    Q2_PROJECTILE_NONE,
    Q2_BOLT,
    Q2_ROCKET,
    Q2_GRENADE,
    Q2_BFG_BALL,
    Q2_ION,
    Q2_PLASMA,
    Q2_FLECHETTE,
    Q2_TRACKER,
    Q2_PROX,
    Q2_TESLA,
    Q2_TRAP,
    Q2_TRACKER_DAEMON,
    Q2_PROX_FIELD,
    Q2_TESLA_FIELD,
    Q2_BAD_AREA,
    Q2_TRAP_GIB,
    Q2_NUKE,
    Q2_GIB,
    Q2_DEBRIS,
    Q2_TRAP_ORBIT_GIB,
    Q2_GREEN_BOLT,
    Q2_BLUE_BOLT,
    Q2_HEAT_ROCKET,
    Q2_CTF_HOOK,
    Q2_LMCTF_HOOK,
    Q2_SPAWN_GROWTH,
    Q2_LMCTF_PLASMA_SPREAD,
    Q2_LMCTF_PLASMA_BOUNCE,
    Q2_PROBOSCIS,
    Q2_PROBOSCIS_SEGMENT,
    Q2_RERELEASE_SPAWN_GROWTH,
    Q2_RERELEASE_SPAWN_BEAM,
    Q2_LOOGIE,
    Q2_BFG_LASER
} q2_projectile_kind;
typedef enum q2_proboscis_phase {
    Q2_PROBOSCIS_FLYING,
    Q2_PROBOSCIS_ATTACHED,
    Q2_PROBOSCIS_RETRACTING,
    Q2_PROBOSCIS_RETURNED
} q2_proboscis_phase;
struct qa_q2_monster;
struct q2_item_state;
struct q2_power_state;
struct qa_q2_player_state;
struct qa_q2_entity_state;
struct qa_targets;
struct q2_items;
struct q2_players;
struct q2_entities;
typedef struct q2_monsters_runtime q2_monsters_runtime;
typedef struct q2_projectile {
    q2_projectile_kind kind;
    qa_attack attack;
    qa_actor_reference owner, enemy, child;
    qa_vec3 movedir;
    float damage, kick, radius_damage, radius, gravity, speed;
    uint64_t born_ns, expire_ns, next_ns, effect_ns;
    int direct_mod, splash_mod, frame, phase, wait;
    float delay, captured_mass, turn_fraction;
    uint64_t effects;
    uint32_t render_flags, gib_flags;
    qa_string_id classname, model, loop_sound;
    int skin;
    float scale, alpha;
    bool hand, held, armed, visible, gekk, dodgeable;
} q2_projectile;
typedef struct qa_q2_player_pm_rules {
    int32_t type;
    uint32_t flags, time;
    int16_t gravity;
    union { int16_t words[3]; qa_vec3 angles; } delta;
    qa_vec3 command_angles;
    uint64_t frame, time_ns, source_sequence, pending_sequence;
    bool command_seen, command_pending;
} qa_q2_player_pm_rules;
typedef struct q2_player_source_info {
    uint32_t slot, seat;
    int score, lives;
    qa_actor_id chase_target;
    qa_item_id selected_item;
    float view_height;
    bool connected, spectator, dead, god, notarget, noclip, flashlight;
} q2_player_source_info;
typedef struct q2_client_state {
    q2_player_source_info info;
    qa_q2_player_rule_tail rule;
    qa_string_id source_name, source_skin, source_model, source_model_resource;
    qa_actor_player *player;
} q2_client_state;

typedef struct q2_actor {
    struct q2_actor *all_next, *free_next;
    size_t storage_slot;
    struct q2_actor *live_next, *live_previous;
    uint64_t source_order;
    uint32_t wire_event;
    uint64_t wire_event_frame;
    qa_q2_wire_view wire_view;
    qa_q2_player_pm_rules source_pm;
    qa_q2_wire_lifetime wire_lifetime;
    /* Ephemeral HUD messages, rebuilt after load and excluded from checkpoints. */
    qa_string_id pickup_icon, pickup_text, selected_item_name;
    uint64_t pickup_until_ns, selected_item_name_until_ns;
    uint64_t extra_effects;
    uint32_t environment_flags;
    uint64_t combat_surprise_ns;
    uint64_t character_birth_epoch;
    bool character_immortal, character_no_damage_effects;
    qa_actor_owner combat_life_owner;
    uint64_t combat_life_birth_epoch, combat_death_ns;
    bool combat_life_present, combat_no_knockback, combat_alive_knockback_only;
    float alpha;
    bool lmctf_plasma_bounce;
    qa_actor_id id;
    bool weapon_bound, physics_bound;
    qa_q2_weapon_state weapon;
    qa_q2_weapon_input input;
    qa_q2_weapon_turn_state weapon_turn;
    int silencer;
    q2_projectile projectile;
    qa_physics_properties physics;
    qa_q2_grapple_state grapples[2];
    bool hand_grenade_bound;
    uint64_t hand_revision;
    qa_q2_hand_grenade_state hand_grenade;
    struct qa_q2_monster *monster;
    struct q2_item_state *item;
    struct q2_power_state *powers;
    q2_client_state *client;
    struct qa_q2_entity_state *entity;
    qa_q2_game *entity_game;
    struct qa_targets *entity_targets;
    bool restore_definitions, restore_power_inventory, restore_targets;
    qa_inventory_entry restore_hand_ammo;
} q2_actor;
typedef struct q2_mt_random {
    uint32_t words[624], index;
    uint64_t draws;
} q2_mt_random;
typedef struct q2_shadow_slot {
    qa_actor_id actor;
    uint32_t source_slot;
} q2_shadow_slot;
typedef struct q2_push_frame {
    struct q2_push_frame *next;
    qa_physics_push *parts;
    size_t capacity;
    bool active;
} q2_push_frame;
struct qa_q2_game {
    q2_monsters_runtime *monster_runtime;
    struct q2m_definition *monster_definitions;
    size_t monster_definition_count;
    const struct q2m_definition *medic_definitions[7];
    qa_builtin_services services;
    const qa_string_id *runtime_names;
    const qa_string_id *lightstyle_bytes;
    qa_string_id temporary_effects[256];
    qa_string_id field_keys[QA_TARGET_KEY_TOTAL];
    const qa_cvars *source_cvars;
    qa_cvar_handle source_settings[QA_Q2_SOURCE_SETTING_COUNT];
    qa_q2_options options;
    qa_q2_hooks hooks;
    qa_q2_grapple_options grapple_options;
    qa_error release_error;
    bool release_failed;
    bool restoring_continuation, continuation_pending, continuation_failed;
    bool frame_stopped; /* Current source-frame early ExitLevel/fade, never continuation state. */
    unsigned hand_steps;
    bool lmctf_plasma_quad;
    uint8_t widow_damage_multiplier;
    uint8_t widow_shot_phase;
    qa_q2_weapon_definition definitions[QA_Q2_WEAPON_COUNT];
    qa_q2_weapon definition_order[QA_Q2_WEAPON_COUNT];
    uint32_t definition_count;
    qa_q2_weapon_rules arsenal_rules;
    bool native_hook;
    qa_ruleset_id hook_ruleset;
    qa_q2_weapon_rules equipment_hook_rules;
    qa_ruleset_id equipment_hook_ruleset;
    qa_item_id items[QA_Q2_WEAPON_COUNT], ammo[QA_Q2_WEAPON_COUNT];
    qa_string_id view_models[QA_Q2_WEAPON_COUNT];
    q2_actor **actors, *all_actors, *retired_actors;
    qa_arena actor_storage;
    qa_pool actor_records;
    qa_arena monster_storage;
    qa_pool monster_records;
    qa_arena entity_storage;
    qa_arena entity_fields;
    qa_pool entity_records;
    q2_actor *first_actor, *last_actor;
    size_t capacity;
    qa_builtin_snapshot_frame *trace_frames;
    struct q2_items *item_runtime;
    struct q2_players *player_runtime;
    struct q2_entities *entity_runtime;
    qa_builtin_random random;
    q2_mt_random rerelease_random;
    qa_actor_id current_actor;
    uint64_t sequence, actor_sequence, now_ns, frame_ns;
    qa_actor_index wire_index;
    uint64_t *wire_freed_ns;
    uint32_t wire_capacity, wire_extent, wire_clients;
    uint64_t wire_frame;
    qa_string_id wire_statusbar_names[5];
    qa_lightstyles wire_lightstyles;
    q2_shadow_slot wire_shadows[256];
    uint32_t wire_shadow_count;
    q2_push_frame *push_frames;
};
typedef struct q2_weapon_call {
    qa_q2_game *game;
    q2_actor *actor;
    qa_q2_weapon_state *state;
    qa_q2_weapon_input input;
    const qa_q2_weapon_definition *definition;
    uint64_t now_ns, frame_ns;
    qa_ruleset_id rules;
    bool silenced, equipment;
    bool has_projectile_enemy;
    qa_actor_id projectile_enemy;
    bool has_attack_owner, has_projectile_effects;
    qa_actor_id attack_owner;
    uint64_t projectile_effects;
    qa_actor_id *spawned_projectile;
    bool has_grenade_impulse;
    float grenade_right, grenade_up, grenade_gravity;
} q2_weapon_call;
typedef struct q2_hand_spec {
    qa_vec3 start, direction;
    float speed, fuse;
    bool held;
} q2_hand_spec;

typedef struct q2_shot_spec {
    q2_projectile_kind kind;
    qa_vec3 offset;
    float damage, damage_random, kick, speed, radius, splash, fuse;
    float spread_x, spread_y, spread_degrees_x, yaw_offset, range, effect_damage, effect_radius;
    unsigned shots;
    int mod, splash_mod, muzzle;
    bool ballistic, homing, deployable, grapple, melee, conditional;
} q2_shot_spec;
qa_vec3 q2_hyper_offset(const q2_weapon_call *);
bool q2_grapple_project(q2_weapon_call *,bool lm,qa_vec3 *,qa_vec3 *,qa_error *);
bool q2_weapon_muzzle(q2_weapon_call *,const q2_shot_spec *,qa_vec3,qa_vec3 *,qa_vec3 *,qa_error *);
bool q2_weapon_shot_spec(const q2_weapon_call *,q2_shot_spec *);
uint64_t q2_throw_cook_ns(void);
float q2_launch_pitch(float);
void q2_throw_spec(const q2_weapon_call *,float fuse,bool alive,q2_shot_spec *);
void q2_grapple_spec(const qa_q2_game *,bool lm,q2_shot_spec *);
void q2_lmctf_plasma_spec(bool bounce,q2_shot_spec *);
void q2_chainfist_spec(const qa_q2_game *,q2_shot_spec *);
float q2_hitscan_range(void);
float q2_trap_capture_damage(void);
float q2_bfg_impact_damage(void);
float q2_bfg_impact_radius(void);
qa_vec3 q2_launch_velocity(qa_vec3 direction,float speed,float lift,float side);
float q2_grenade_gravity_scale(bool rerelease,float gravity);
float q2_grenade_lift(bool rerelease,float gravity,float noise);
qa_vec3 q2_mine_velocity(qa_vec3 direction,float speed,float lift,float side);
uint64_t q2_animation_native(const q2_weapon_call *);
float q2_mine_lift(q2_projectile_kind,bool rerelease,float gravity,float noise);
q2_actor *q2_actor_get(qa_q2_game *, qa_actor_id, bool create, qa_error *);
q2_actor *q2_actor_storage_take(qa_q2_game *, qa_error *);
void q2_actor_storage_release(qa_q2_game *, q2_actor *);
void q2_actor_publish_prepared(qa_q2_game *, q2_actor *, qa_actor_id);
void q2_actor_order(qa_q2_game *, q2_actor *, uint64_t);
bool q2_wire_admit(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_player_source_motion_rules(qa_q2_game *, q2_actor *, const qa_q2_player_motion *, qa_error *);
bool q2_wire_shadow_event(qa_q2_game *, qa_q2_map_event *, qa_error *);
bool q2_wire_bind(qa_q2_game *, q2_actor *, uint32_t, qa_error *);
bool q2_wire_binding_number(const qa_q2_game *, qa_actor_id, uint32_t *);
qa_actor_id q2_wire_actor_at(const qa_q2_game *, uint32_t);
void q2_wire_release(qa_q2_game *, q2_actor *, qa_actor_record);
bool q2_wire_names_prepare(qa_q2_game *,qa_error *);
void q2_wire_reset(qa_q2_game *);
bool q2_actor_live(qa_q2_game *, qa_actor_id);
enum q2_environment_flags {
    Q2_ENV_IN_WATER = 8u, Q2_ENV_IMMUNE_SLIME = 64u, Q2_ENV_IMMUNE_LAVA = 128u
};
bool q2_world_effects(qa_q2_game *, q2_actor *, const qa_body_state *, const qa_combat_state *,
                      uint64_t *air_ns, uint64_t *pain_ns, uint64_t *damage_ns, qa_error *);
float q2_random(qa_q2_game *);
bool q2_monster_timed_invulnerability(const q2_actor *, uint64_t now_ns);
float q2_crandom(qa_q2_game *);
void q2_rerelease_seed(qa_q2_game *, uint32_t);
uint32_t q2_rerelease_word(qa_q2_game *);
uint32_t q2_random_bounded(qa_q2_game *, uint32_t bound);
float q2_rerelease_float(qa_q2_game *, float minimum, float maximum);
int64_t q2_rerelease_time_ms(qa_q2_game *, int64_t minimum, int64_t maximum);
bool q2_count(qa_q2_game *, qa_actor_id, qa_item_id, int *, qa_error *);
bool q2_ammo(q2_weapon_call *, int *, qa_error *);
bool q2_infinite_ammo(const q2_weapon_call *);
bool q2_consume(q2_weapon_call *, int, bool honor_infinite, qa_error *);
bool q2_emit_sound(qa_q2_game *, qa_actor_id, qa_builtin_event_kind, const qa_vec3 *origin, qa_string_id,
    int channel, float volume, float attenuation, uint32_t flags, qa_error *);
bool q2_actor_loop(qa_q2_game *, qa_actor_id, qa_string_id *current, qa_string_id next,
    int channel, bool stop_previous, qa_error *);
bool q2_sound(q2_weapon_call *, q2_runtime_name, int, float attenuation, qa_error *);
bool q2_loop(q2_weapon_call *, q2_runtime_name, qa_error *);
bool q2_event(q2_weapon_call *, qa_builtin_event_kind, int, qa_vec3, qa_vec3, qa_error *);
bool q2_event_named(q2_weapon_call *, qa_builtin_event_kind, q2_runtime_name, int,
    qa_vec3, qa_vec3, qa_error *);
bool q2_noise(q2_weapon_call *, qa_vec3, qa_error *);
bool q2_animation(q2_weapon_call *, int priority, int first, int last, qa_error *);
bool q2_attack_animation(q2_weapon_call *, int offset, qa_error *);
bool q2_reverse_animation(q2_weapon_call *, qa_error *);
bool q2_power_sound(q2_weapon_call *, qa_error *);
bool q2_no_ammo(q2_weapon_call *, bool sound, qa_error *);
bool q2_change_weapon(q2_weapon_call *, qa_error *);
bool q2_weapon_validate(qa_q2_game *, const qa_q2_weapon_state *, qa_error *);
bool q2_weapon_powerups(q2_weapon_call *, qa_error *);
bool q2_generic(q2_weapon_call *, qa_error *);
bool q2_generic_classic(q2_weapon_call *, qa_error *);
bool q2_fire(q2_weapon_call *, bool buffered, qa_error *);
bool q2_weapon_fired(qa_q2_game *, qa_actor_id, qa_q2_weapon, qa_error *);
bool q2_throw_frame(q2_weapon_call *, qa_error *);
bool q2_throw(q2_weapon_call *, bool held, qa_error *);
bool q2_hand_calculate(q2_weapon_call *, uint64_t expires_ns, bool alive, bool held,
                       qa_q2_hand_projection_fn, void *, q2_hand_spec *, qa_error *);
bool q2_hand_validate(const qa_q2_hand_grenade_state *, qa_error *);
bool q2_present(q2_weapon_call *, qa_error *);
bool q2_animation_time(q2_weapon_call *, uint64_t *, qa_error *);
bool q2_animation_deadline(q2_weapon_call *, uint64_t from, uint64_t extra_ns,
                          uint64_t *, qa_error *);
bool q2_interval(q2_weapon_call *, uint64_t, uint64_t *, qa_error *);
bool q2_multiplier(q2_weapon_call *, float *, qa_error *);
void q2_kick(q2_weapon_call *, qa_vec3, qa_vec3, float);
void q2_recoil(q2_weapon_call *, qa_vec3 *origin, qa_vec3 *angles);
bool q2_project(q2_weapon_call *, qa_vec3 angles, qa_vec3 offset, qa_vec3 *, qa_vec3 *, qa_error *);
qa_attack q2_attack(q2_weapon_call *, int mod, uint32_t flags);
bool q2_bullet(q2_weapon_call *, qa_vec3, qa_vec3, float damage, float kick, float hs, float vs,
               int count, int mod, bool shotgun, qa_error *);
bool q2_rail(q2_weapon_call *, qa_vec3, qa_vec3, float, float, int mod, uint32_t flags, qa_error *);
bool q2_heatbeam(q2_weapon_call *, qa_vec3, qa_vec3, float, float, qa_error *);
bool q2_fire_chainfist(q2_weapon_call *, qa_error *);
bool q2_projectile_spawn(q2_weapon_call *, q2_projectile_kind, qa_vec3, qa_vec3, float damage,
                         float kick, float speed, float radius, float radius_damage, float fuse,
                         int direct_mod, int splash_mod, bool hand, bool held, qa_error *);
bool q2_projectile_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_proboscis_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_proboscis_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_proboscis_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_tracker_target(q2_weapon_call *, qa_vec3, qa_vec3, qa_actor_id *, qa_error *);
bool q2_launch_behavior(qa_q2_game *, q2_actor *, qa_builtin_projectile_role, bool *changed,
                        qa_error *);
bool q2_target_damageable(qa_q2_game *, qa_actor_id);
bool q2_target_creature(qa_q2_game *, qa_actor_id, bool *creature, bool *player, qa_error *);
bool q2_projectile_event(qa_q2_game *, qa_actor_id, qa_builtin_event_kind, qa_string_id, int,
                         qa_vec3, qa_vec3, qa_error *);
bool q2_projectile_loop(qa_q2_game *, q2_actor *, qa_string_id, bool stop_previous, qa_error *);
qa_attack q2_projectile_attack(qa_q2_game *, qa_actor_id, const q2_projectile *, int, uint32_t);
bool q2_projectile_noise(qa_q2_game *, const q2_projectile *, qa_vec3, qa_error *);
bool q2_projectile_radius(qa_q2_game *, qa_actor_id, const q2_projectile *, qa_vec3, qa_actor_id,
                          float, float, int, uint32_t, qa_error *);
bool q2_mine_spawn(q2_weapon_call *, q2_projectile_kind, qa_vec3, qa_vec3, float, float, float,
                   float, float, bool, qa_error *);
bool q2_mine_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_mine_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_damage(qa_q2_game *, const qa_attack *, qa_actor_id, float, float, qa_vec3, qa_vec3,
               qa_vec3, bool, qa_error *);
bool q2_prepare_damage(void *, qa_damage_request *, bool *allowed, qa_error *);
bool q2_prepare_radius_damage(void *, qa_damage_request *, bool *allowed, qa_error *);
bool q2_radius_damage(qa_q2_game *, const qa_builtin_radius *, size_t *, qa_error *);
bool q2_definitions(qa_q2_game *, qa_error *);
bool q2_monsters_init(qa_q2_game *, qa_error *);
void q2_monsters_close(qa_q2_game *);
void q2_monsters_begin_map(qa_q2_game *);
bool q2_player_trail_begin(qa_q2_game *, qa_error *);
bool q2_player_trail_step(qa_q2_game *, qa_actor_id, qa_error *);
bool q2_player_trail_destroy(qa_q2_game *, qa_actor_id, qa_error *);
bool q2_player_trail_client(qa_q2_game *, qa_actor_id, bool reading,
                            qa_actor_reference *head, qa_actor_reference *tail, qa_error *);
void q2_player_trail_read_level(qa_q2_game *);
void q2_monsters_reclaim(qa_q2_game *);
void q2_monsters_release_actor(qa_q2_game *, qa_actor_id);
bool q2_monster_tick(qa_q2_game *, q2_actor *, qa_error *);
void q2_monster_release_state(q2_actor *);
bool q2_monster_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_monster_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_monster_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_monster_pain_advance(qa_q2_game *, qa_actor_id, uint64_t amount_ns);
bool q2_monster_dodge(qa_q2_game *, qa_actor_id target, qa_actor_id attacker, float eta_seconds,
                      const qa_trace_result *, bool gravity, qa_error *);
bool q2_items_init(qa_q2_game *, qa_error *);
bool q2_players_init(qa_q2_game *, qa_error *);
bool q2_entities_init(qa_q2_game *, qa_error *);
void q2_items_close(qa_q2_game *);
void q2_players_close(qa_q2_game *);
void q2_entities_close(qa_q2_game *);
void q2_items_release_state(q2_actor *);
void q2_client_release_state(q2_actor *);
void q2_entity_release_state(q2_actor *);
void q2_entity_unbind(qa_q2_game *, q2_actor *);
bool q2_save_reference(qa_q2_game *, qa_actor_id, qa_q2_saved_reference *, qa_error *);
bool q2_resolve_reference(qa_q2_game *, qa_q2_saved_reference, qa_actor_id *, qa_error *);
bool q2_checkpoint_idle(qa_q2_game *, qa_error *);
bool q2_item_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_client_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_client_early_weapon_turn(qa_q2_game *, q2_actor *, const qa_q2_weapon_input *, qa_error *);
bool q2_client_weapon_frame(qa_q2_game *, q2_actor *, const qa_q2_weapon_input *, qa_error *);
bool q2_entity_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_entity_prethink(qa_q2_game *, q2_actor *, qa_error *);
bool q2_actor_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_actor_physics(qa_q2_game *, q2_actor *, qa_error *);
bool q2_item_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_item_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_item_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_client_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_entity_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_client_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_entity_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_client_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_entity_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_player_noise(qa_q2_game *, qa_actor_id, qa_vec3, bool secondary, qa_error *);
bool q2_player_tracker_pain(qa_q2_game *, qa_actor_id, uint64_t until_ns, qa_error *);
bool q2_player_invisibility_reveal(qa_q2_game *, qa_actor_id, uint64_t until_ns, qa_error *);
bool q2_player_nuke_blind(qa_q2_game *, qa_actor_id, uint64_t until_ns, bool inside, qa_error *);
bool q2_player_damage_view(qa_q2_game *, qa_actor_id, float pitch, float roll, uint64_t until_ns,
                           qa_error *);
bool q2_item_food_cube(qa_q2_game *, qa_actor_id source, qa_vec3, float scale, int health,
                       qa_vec3 velocity, qa_error *);
bool q2_fire_nuke(qa_q2_game *, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction, float speed,
                  float multiplier, qa_error *);
bool q2_noise_for_actor(qa_q2_game *, qa_actor_id, qa_vec3, bool secondary, qa_error *);
bool q2_nuke_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_nuke_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
qa_builtin_snapshot_frame *q2_scratch_acquire(qa_q2_game *, qa_error *);
qa_builtin_snapshot_frame *q2_nearby(qa_q2_game *, qa_vec3 origin, float radius, qa_error *);
qa_builtin_snapshot_frame *q2_player_roster(qa_q2_game *, qa_error *);
enum {
    Q2_GIB_HEAD = 1u, Q2_GIB_METALLIC = 2u, Q2_GIB_SKINNED = 4u, Q2_GIB_UPRIGHT = 8u,
    Q2_GIB_WIDOW = 16u, Q2_GIB_WIDOW_SIZED = 32u, Q2_GIB_WIDOW_HIT_SOUND = 64u,
    Q2_GIB_WIDOW_LEGS = 128u, Q2_GIB_DEBRIS = 256u
};
bool q2_widow_legs_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_widow_gib_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_spawn_gib(qa_q2_game *, qa_actor_id source, q2_runtime_name model, float damage, uint32_t flags,
                  int skin, float scale, qa_error *);
bool q2_spawn_debris(qa_q2_game *, qa_actor_id source, qa_error *);
bool q2_spawn_model_debris(qa_q2_game *, qa_actor_id source, q2_runtime_name model, float speed,
                           qa_vec3 origin, qa_error *);
bool q2_admit_growth_part(qa_q2_game *, q2_runtime_name, qa_body_state, q2_actor **, qa_ruleset_id, qa_error *);
bool q2_spawn_growth(qa_q2_game *, qa_vec3 origin, unsigned size, qa_error *);
bool q2_gib_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_gib_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_gib_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_trap_capture_gibs(qa_q2_game *, q2_actor *, qa_error *);
bool q2_fire_actor_bolt(qa_q2_game *, qa_actor_id source, qa_actor_id credited_owner, qa_vec3 start,
                        qa_vec3 direction, float damage, float speed, uint64_t effects,
                        int means_of_death, bool green, qa_error *);
bool q2_fire_actor_loogie(qa_q2_game *, qa_actor_id source, qa_vec3 start, qa_vec3 direction,
                          qa_error *);
bool q2_fire_actor_rocket(qa_q2_game *, qa_actor_id source, qa_actor_id credited_owner,
                          qa_vec3 start, qa_vec3 direction, float damage, float speed,
                          float splash_damage, float radius, int direct_mod, int splash_mod,
                          qa_actor_id *out, qa_error *);
bool q2_green_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, qa_error *);
bool q2_heat_rocket_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_grapple_weapon(q2_weapon_call *, qa_error *);
bool q2_grapple_fire(q2_weapon_call *, qa_error *);
bool q2_grapple_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_grapple_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_grapple_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_grapple_released(qa_q2_game *, q2_actor *, qa_error *);
bool q2_lmctf_plasma_weapon(q2_weapon_call *, qa_error *);
bool q2_lmctf_plasma_fire(q2_weapon_call *, qa_error *);
bool q2_lmctf_plasma_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_lmctf_plasma_mode(qa_q2_game *, q2_actor *, qa_error *);
static inline bool q2_frame_bit(uint64_t bits, int frame) {
    return frame >= 0 && frame < 64 && (bits & (UINT64_C(1) << (unsigned)frame)) != 0;
}
static inline bool q2_continues(const q2_weapon_call *c) {
    return c->state->handoff == QA_Q2_PRIMARY_ACTIVE && c->input.attack;
}
static inline uint64_t q2_deadline(uint64_t now, uint64_t duration) {
    return duration > UINT64_MAX - now ? UINT64_MAX : now + duration;
}
static inline uint64_t q2_duration(double seconds) {
    if (seconds <= 0)
        return 0;
    double ns = seconds * 1e9;
    return !isfinite(ns) || ns >= (double)UINT64_MAX ? UINT64_MAX : (uint64_t)(ns + 0.5);
}
void q2_player_source_info_read(qa_q2_game *, const q2_client_state *, qa_q2_player_info *);
bool q2_player_userinfo_value(const char *, const char *, char *, size_t);
bool q2_player_source_identity_bind(qa_q2_game *,q2_client_state *,const char *,const char *,qa_error *);
const char *q2_player_source_name(const qa_q2_game *, const q2_client_state *);
const char *q2_player_source_skin(const qa_q2_game *, const q2_client_state *);
#endif
