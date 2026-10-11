#ifndef QA_Q1_INTERNAL_H
#define QA_Q1_INTERNAL_H

#include "boss_types.h"
#include "frame_actions.h"
#include "qa/game_q1.h"
#include "qa/target_keys.h"
#include "qa/game_q1_source_powers.h"
#include "qa/game_q1_rogue.h"
#include "qa/game_q1_source_flags.h"
#include "qa/game_q1_source_runes.h"
#include "qa/game_q1_source_rogue_tag.h"
#include "qa/game_q1_source_rogue_flags.h"
#include <stdlib.h>
#include <string.h>

/* SV_SpawnServer starts the source clock at one second, before entity spawn. */
#define Q1_SOURCE_INITIAL_TIME_NS UINT64_C(1000000000)

#define Q1_RUNTIME_NAME_LIST(X) \
    X(VENGEANCE, "Vengeance") \
    X(AMBIENT_COMP_HUM, "ambient_comp_hum") \
    X(AMBIENT_DRIP, "ambient_drip") \
    X(AMBIENT_DRONE, "ambient_drone") \
    X(AMBIENT_FAN_BLOWING, "ambient_fan_blowing") \
    X(AMBIENT_FLOURO_BUZZ, "ambient_flouro_buzz") \
    X(AMBIENT_GENERIC, "ambient_generic") \
    X(AMBIENT_HUMMING, "ambient_humming") \
    X(AMBIENT_LIGHT_BUZZ, "ambient_light_buzz") \
    X(AMBIENT_RIFTPOWER, "ambient_riftpower") \
    X(AMBIENT_RUNNING_WATER, "ambient_running_water") \
    X(AMBIENT_RUSHING, "ambient_rushing") \
    X(AMBIENT_SUCK_WIND, "ambient_suck_wind") \
    X(AMBIENT_SWAMP1, "ambient_swamp1") \
    X(AMBIENT_SWAMP2, "ambient_swamp2") \
    X(AMBIENT_THUNDER, "ambient_thunder") \
    X(AMBIENT_WATERFALL, "ambient_waterfall") \
    X(BUZZSAW, "buzzsaw") \
    X(DMATCH_TAG_TOKEN, "dmatch_tag_token") \
    X(DRAGON_CORNER, "dragon_corner") \
    X(EXPLO_BOX, "explo_box") \
    X(FALLING, "falling") \
    X(FIREBALL, "fireball") \
    X(FUNC_AXE_BUTTON, "func_axe_button") \
    X(FUNC_COUNTER, "func_counter") \
    X(FUNC_CTF_WALL, "func_ctf_wall") \
    X(FUNC_DOOR, "func_door") \
    X(FUNC_EPISODEGATE, "func_episodegate") \
    X(FUNC_ILLUSIONARY, "func_illusionary") \
    X(FUNC_MULTI_EXPLODER, "func_multi_exploder") \
    X(FUNC_ONCOUNT, "func_oncount") \
    X(FUNC_RUBBLE1, "func_rubble1") \
    X(FUNC_RUBBLE2, "func_rubble2") \
    X(FUNC_RUBBLE3, "func_rubble3") \
    X(GRENADE, "grenade") \
    X(HIPLASER, "hiplaser") \
    X(HIPNOTIC_EMPATHY, "hipnotic:empathy") \
    X(HORDE_MANAGER, "horde_manager") \
    X(HUB_TRIGGER_CHANGELEVEL, "hub_trigger_changelevel") \
    X(INFO_BOSS_TELEPORT_BOSS, "info_boss_teleport_boss") \
    X(INFO_BOSS_TELEPORT_FIRST, "info_boss_teleport_first") \
    X(INFO_BOSS_TELEPORT_SECOND, "info_boss_teleport_second") \
    X(INFO_INTERMISSION, "info_intermission") \
    X(INFO_OVERLORD_DESTINATION, "info_overlord_destination") \
    X(INFO_PLAYER_DEATHMATCH, "info_player_deathmatch") \
    X(INFO_SZOMBIE_SPAWN, "info_szombie_spawn") \
    X(INFO_VOTE_DESTINATION, "info_vote_destination") \
    X(ITEM_ARTIFACT_INVULNERABILITY, "item_artifact_invulnerability") \
    X(ITEM_ARTIFACT_SUPER_DAMAGE, "item_artifact_super_damage") \
    X(ITEM_CELLS, "item_cells") \
    X(ITEM_FLAG, "item_flag") \
    X(ITEM_FLAG_TEAM1, "item_flag_team1") \
    X(ITEM_FLAG_TEAM2, "item_flag_team2") \
    X(ITEM_ROCKETS, "item_rockets") \
    X(ITEM_SHELLS, "item_shells") \
    X(ITEM_SPIKES, "item_spikes") \
    X(LAVA_SPIKE, "lava_spike") \
    X(LIGHT, "light") \
    X(LIGHT_FLAME_LARGE_YELLOW, "light_flame_large_yellow") \
    X(LIGHT_FLAME_SMALL_WHITE, "light_flame_small_white") \
    X(LIGHT_FLAME_SMALL_YELLOW, "light_flame_small_yellow") \
    X(LIGHT_FLUORO, "light_fluoro") \
    X(LIGHT_FLUOROSPARK, "light_fluorospark") \
    X(LIGHT_GLOBE, "light_globe") \
    X(LIGHT_LANTERN, "light_lantern") \
    X(LIGHT_TORCH_SMALL_WALLTORCH, "light_torch_small_walltorch") \
    X(LIGHTNING_CHILD, "lightning_child") \
    X(LTRAIL_END, "ltrail_end") \
    X(LTRAIL_RELAY, "ltrail_relay") \
    X(LTRAIL_START, "ltrail_start") \
    X(MISC_CORPSE, "misc_corpse") \
    X(MISC_EXPLOBOX, "misc_explobox") \
    X(MISC_EXPLOBOX2, "misc_explobox2") \
    X(MISC_FIREBALL, "misc_fireball") \
    X(MISC_TELEPORTTRAIN, "misc_teleporttrain") \
    X(MISSILE, "missile") \
    X(MONSTER_ARMAGON, "monster_armagon") \
    X(MONSTER_ARMY, "monster_army") \
    X(MONSTER_BOSS, "monster_boss") \
    X(MONSTER_DECOY, "monster_decoy") \
    X(MONSTER_DEMON1, "monster_demon1") \
    X(MONSTER_DOG, "monster_dog") \
    X(MONSTER_DRAGON, "monster_dragon") \
    X(MONSTER_DRAGON_DEAD, "monster_dragon_dead") \
    X(MONSTER_EEL, "monster_eel") \
    X(MONSTER_ENFORCER, "monster_enforcer") \
    X(MONSTER_FISH, "monster_fish") \
    X(MONSTER_GREMLIN, "monster_gremlin") \
    X(MONSTER_HELL_KNIGHT, "monster_hell_knight") \
    X(MONSTER_KNIGHT, "monster_knight") \
    X(MONSTER_LAVA_MAN, "monster_lava_man") \
    X(MONSTER_MORPH, "monster_morph") \
    X(MONSTER_MUMMY, "monster_mummy") \
    X(MONSTER_OGRE, "monster_ogre") \
    X(MONSTER_OLDONE, "monster_oldone") \
    X(MONSTER_OLDONE_NEW, "monster_oldone_new") \
    X(MONSTER_ORB, "monster_orb") \
    X(MONSTER_SCOURGE, "monster_scourge") \
    X(MONSTER_SHALRATH, "monster_shalrath") \
    X(MONSTER_SHAMBLER, "monster_shambler") \
    X(MONSTER_SUPER_SHAMBLER, "monster_super_shambler") \
    X(MONSTER_SUPER_WRATH, "monster_super_wrath") \
    X(MONSTER_SWORD, "monster_sword") \
    X(MONSTER_SZOMBIE, "monster_szombie") \
    X(MONSTER_TARBABY, "monster_tarbaby") \
    X(MONSTER_VOMIT, "monster_vomit") \
    X(MONSTER_WIZARD, "monster_wizard") \
    X(MONSTER_WRATH, "monster_wrath") \
    X(MONSTER_ZOMBIE, "monster_zombie") \
    X(MUMMY_GRENADE, "mummy_grenade") \
    X(OLDNEW_CHILD, "oldnew_child") \
    X(OLDNEW_EYE, "oldnew_eye") \
    X(PATH_CORNER, "path_corner") \
    X(PENDULUM, "pendulum") \
    X(PLASMA, "plasma") \
    X(PLAYER, "player") \
    X(POWER_SHIELD, "power_shield") \
    X(PROXIMITY_GRENADE, "proximity_grenade") \
    X(ROCK, "rock") \
    X(SCOURGE_TRIGGER, "scourge_trigger") \
    X(SPAWNED_EXPLOSION, "spawned_explosion") \
    X(SPHERE, "sphere") \
    X(TELEDEATH, "teledeath") \
    X(TELEDEATH2, "teledeath2") \
    X(TRAP_SHOOTER, "trap_shooter") \
    X(TRAP_SPIKE_MINE, "trap_spike_mine") \
    X(TRAP_SPIKESHOOTER, "trap_spikeshooter") \
    X(TRIGGER_BOSS_TELEPORT, "trigger_boss_teleport") \
    X(TRIGGER_CHANGELEVEL, "trigger_changelevel") \
    X(TRIGGER_ONCE, "trigger_once") \
    X(TRIGGER_SECRET, "trigger_secret") \
    X(TRIGGER_TELEPORT, "trigger_teleport") \
    X(WORLDSPAWN, "worldspawn") \
    X(INFO_PLAYER_COOP, "info_player_coop") \
    X(MGE2M2_ELECTRODE_TARGET, "mge2m2_electrode_target") \
    X(TRIGGER_MUSIC, "trigger_music") \
    X(BOTTOM, "bottom") \
    X(UP, "up") \
    X(TOP, "top") \
    X(DOWN, "down") \
    X(FUNC_MOVEWALL, "func_movewall") \
    X(ROTATE_OBJECT, "rotate_object") \
    X(TRIGGER_HURT, "trigger_hurt") \
    X(PATH_ROTATE, "path_rotate") \
    X(CLASS_ACTOR, "actor") \
    X(CLASS_ARMAGON_BODY, "armagon_body") \
    X(CLASS_AXE_STRIKE, "axe_strike") \
    X(CLASS_BUBBLE, "bubble") \
    X(CLASS_CHARMED_GOAL, "charmed_goal") \
    X(CLASS_CHTHON_LAVABALL, "chthon_lavaball") \
    X(CLASS_CTF_BACKPACK, "ctf_backpack") \
    X(CLASS_CTF_HOOK, "ctf_hook") \
    X(CLASS_CTF_HOOK_ANIMATION, "ctf_hook_animation") \
    X(CLASS_CTF_HOOK_LINK, "ctf_hook_link") \
    X(CLASS_CTF_NEXTLEVEL, "ctf_nextlevel") \
    X(CLASS_CTF_RUNE_SPAWN, "ctf_rune_spawn") \
    X(CLASS_DEATH_BUBBLES, "death_bubbles") \
    X(CLASS_DELAYEDREMOVE, "DelayedRemove") \
    X(CLASS_DELAYEDUSE, "DelayedUse") \
    X(CLASS_DOOR_TRIGGER, "door_trigger") \
    X(CLASS_EMPTY, "") \
    X(CLASS_ENFORCER_LASER, "enforcer_laser") \
    X(CLASS_EXIT_MARKER, "exit_marker") \
    X(CLASS_FADE_MANAGER, "fade_manager") \
    X(CLASS_FINALE_PLAYER, "finale_player") \
    X(CLASS_FINALE_TIMER, "finale_timer") \
    X(CLASS_FINALE_WAIT, "finale_wait") \
    X(CLASS_GAS_FLAME, "gas_flame") \
    X(CLASS_GIB, "gib") \
    X(CLASS_GREMLIN_GOAL, "gremlin_goal") \
    X(CLASS_HIPNOTIC_HAMMER_STRIKE, "hipnotic_hammer_strike") \
    X(CLASS_HIPNOTIC_LIGHTNING, "hipnotic_lightning") \
    X(CLASS_HIPNOTIC_MJOLNIR_BASE, "hipnotic_mjolnir_base") \
    X(CLASS_HIPNOTIC_MJOLNIR_LIGHTNING, "hipnotic_mjolnir_lightning") \
    X(CLASS_HIPNOTIC_TESLA_LIGHTNING, "hipnotic_tesla_lightning") \
    X(CLASS_HIP_EXPLOSION, "hip_explosion") \
    X(CLASS_HIP_MULTI_EXPLOSION, "hip_multi_explosion") \
    X(CLASS_HIP_RUBBLE, "hip_rubble") \
    X(CLASS_HOOK, "hook") \
    X(CLASS_ITEM_BACKPACK, "item_backpack") \
    X(CLASS_ITEM_FLAGBASE, "item_flagbase") \
    X(CLASS_ITEM_FLAGBASE_TEAM1, "item_flagbase_team1") \
    X(CLASS_ITEM_FLAGBASE_TEAM2, "item_flagbase_team2") \
    X(CLASS_ITEM_RUNE_HASTE, "item_rune_haste") \
    X(CLASS_ITEM_RUNE_REGENERATION, "item_rune_regeneration") \
    X(CLASS_ITEM_RUNE_RESISTANCE, "item_rune_resistance") \
    X(CLASS_ITEM_RUNE_STRENGTH, "item_rune_strength") \
    X(CLASS_ITEM_UPGRADE_CELLS, "item_upgrade_cells") \
    X(CLASS_ITEM_UPGRADE_HEALTH, "item_upgrade_health") \
    X(CLASS_ITEM_UPGRADE_NAILS, "item_upgrade_nails") \
    X(CLASS_ITEM_UPGRADE_ROCKETS, "item_upgrade_rockets") \
    X(CLASS_ITEM_UPGRADE_SHELLS, "item_upgrade_shells") \
    X(CLASS_KNIGHT_SPIKE, "knight_spike") \
    X(CLASS_LAVAMAN_BALL, "lavaman_ball") \
    X(CLASS_MEAT_SPRAY, "meat_spray") \
    X(CLASS_MG3_HAMMER_STRIKE, "mg3_hammer_strike") \
    X(CLASS_MISC_ROPE_SEGMENT, "misc_rope_segment") \
    X(CLASS_MULTIGRENADE, "MultiGrenade") \
    X(CLASS_MULTIROCKET, "MultiRocket") \
    X(CLASS_NEXTLEVEL, "nextlevel") \
    X(CLASS_OGRE_GRENADE, "ogre_grenade") \
    X(CLASS_PLAT_TRIGGER, "plat_trigger") \
    X(CLASS_PUSHABLEWALLPROXY, "pushablewallproxy") \
    X(CLASS_ROGUE_CAMERA_TRACKER, "rogue_camera_tracker") \
    X(CLASS_ROGUE_PLAT2_TRIGGER, "rogue_plat2_trigger") \
    X(CLASS_ROGUE_RUNE, "rogue_rune") \
    X(CLASS_ROGUE_RUNE_SPAWNER, "rogue_rune_spawner") \
    X(CLASS_ROGUE_TEAM_STATE, "rogue_team_state") \
    X(CLASS_RUBBLE, "rubble") \
    X(CLASS_SECRET_MARKER, "secret_marker") \
    X(CLASS_SHAMBLER_LIGHT, "shambler_light") \
    X(CLASS_SPAM, "spam") \
    X(CLASS_SPIKE, "spike") \
    X(CLASS_SUPERSPIKE, "superspike") \
    X(CLASS_TELEPORT_FOG, "teleport_fog") \
    X(CLASS_TIME_MACHINE_GIB, "time_machine_gib") \
    X(CLASS_TIME_MACHINE_PAIN, "time_machine_pain") \
    X(CLASS_TIME_STOP_SHAKE, "time_stop_shake") \
    X(CLASS_VORE_BALL, "vore_ball") \
    X(CLASS_WEAPON_GRENADELAUNCHER, "weapon_grenadelauncher") \
    X(CLASS_WEAPON_LIGHTNING, "weapon_lightning") \
    X(CLASS_WEAPON_NAILGUN, "weapon_nailgun") \
    X(CLASS_WEAPON_ROCKETLAUNCHER, "weapon_rocketlauncher") \
    X(CLASS_WEAPON_SUPERNAILGUN, "weapon_supernailgun") \
    X(CLASS_WEAPON_SUPERSHOTGUN, "weapon_supershotgun") \
    X(CLASS_WIZARD_FASTFIRE, "wizard_fastfire") \
    X(CLASS_WIZARD_SPIKE, "wizard_spike") \
    X(CLASS_WRATH_MISSILE, "wrath_missile") \
    X(CLASS_ZOMBIE_GRENADE, "zombie_grenade") \
    X(RESOURCE_AMBIENCE_WINDFLY_WAV, "ambience/windfly.wav") \
    X(RESOURCE_ARMAGON_IDLE1_WAV, "armagon/idle1.wav") \
    X(RESOURCE_ARMAGON_IDLE2_WAV, "armagon/idle2.wav") \
    X(RESOURCE_ARMAGON_IDLE3_WAV, "armagon/idle3.wav") \
    X(RESOURCE_ARMAGON_IDLE4_WAV, "armagon/idle4.wav") \
    X(RESOURCE_ARMAGON_PAIN_WAV, "armagon/pain.wav") \
    X(RESOURCE_ARMAGON_REPEL_WAV, "armagon/repel.wav") \
    X(RESOURCE_ARMAGON_SIGHT2_WAV, "armagon/sight2.wav") \
    X(RESOURCE_ARMAGON_SIGHT_WAV, "armagon/sight.wav") \
    X(RESOURCE_BELT_FADEOUT_WAV, "belt/fadeout.wav") \
    X(RESOURCE_BLOB_DEATH1_WAV, "blob/death1.wav") \
    X(RESOURCE_BLOB_HIT1_WAV, "blob/hit1.wav") \
    X(RESOURCE_BLOB_LAND1_WAV, "blob/land1.wav") \
    X(RESOURCE_BLOB_SIGHT1_WAV, "blob/sight1.wav") \
    X(RESOURCE_BOSS1_DEATH_WAV, "boss1/death.wav") \
    X(RESOURCE_BOSS1_OUT1_WAV, "boss1/out1.wav") \
    X(RESOURCE_BOSS1_PAIN_WAV, "boss1/pain.wav") \
    X(RESOURCE_BOSS1_SIGHT1_WAV, "boss1/sight1.wav") \
    X(RESOURCE_BOSS1_THROW_WAV, "boss1/throw.wav") \
    X(RESOURCE_BOSS2_DEATH_WAV, "boss2/death.wav") \
    X(RESOURCE_BOSS2_POP2_WAV, "boss2/pop2.wav") \
    X(RESOURCE_BOSS2_SIGHT_WAV, "boss2/sight.wav") \
    X(RESOURCE_BUTTONS_AIRBUT1_WAV, "buttons/airbut1.wav") \
    X(RESOURCE_BUTTONS_SWITCH02_WAV, "buttons/switch02.wav") \
    X(RESOURCE_BUTTONS_SWITCH04_WAV, "buttons/switch04.wav") \
    X(RESOURCE_BUTTONS_SWITCH21_WAV, "buttons/switch21.wav") \
    X(RESOURCE_BUZZ_BUZZ_WAV, "buzz/buzz.wav") \
    X(RESOURCE_DEMON_DDEATH_WAV, "demon/ddeath.wav") \
    X(RESOURCE_DEMON_DHIT2_WAV, "demon/dhit2.wav") \
    X(RESOURCE_DEMON_DJUMP_WAV, "demon/djump.wav") \
    X(RESOURCE_DEMON_DPAIN1_WAV, "demon/dpain1.wav") \
    X(RESOURCE_DEMON_IDLE1_WAV, "demon/idle1.wav") \
    X(RESOURCE_DEMON_SIGHT2_WAV, "demon/sight2.wav") \
    X(RESOURCE_DOG_DATTACK1_WAV, "dog/dattack1.wav") \
    X(RESOURCE_DOG_DDEATH_WAV, "dog/ddeath.wav") \
    X(RESOURCE_DOG_DPAIN1_WAV, "dog/dpain1.wav") \
    X(RESOURCE_DOG_DSIGHT_WAV, "dog/dsight.wav") \
    X(RESOURCE_DOG_IDLE_WAV, "dog/idle.wav") \
    X(RESOURCE_DOORS_AIRDOOR1_WAV, "doors/airdoor1.wav") \
    X(RESOURCE_DOORS_AIRDOOR2_WAV, "doors/airdoor2.wav") \
    X(RESOURCE_DOORS_BASESEC1_WAV, "doors/basesec1.wav") \
    X(RESOURCE_DOORS_BASESEC2_WAV, "doors/basesec2.wav") \
    X(RESOURCE_DOORS_BASETRY_WAV, "doors/basetry.wav") \
    X(RESOURCE_DOORS_BASEUSE_WAV, "doors/baseuse.wav") \
    X(RESOURCE_DOORS_DDOOR1_WAV, "doors/ddoor1.wav") \
    X(RESOURCE_DOORS_DDOOR2_WAV, "doors/ddoor2.wav") \
    X(RESOURCE_DOORS_DOORMV1_WAV, "doors/doormv1.wav") \
    X(RESOURCE_DOORS_DRCLOS4_WAV, "doors/drclos4.wav") \
    X(RESOURCE_DOORS_HYDRO1_WAV, "doors/hydro1.wav") \
    X(RESOURCE_DOORS_HYDRO2_WAV, "doors/hydro2.wav") \
    X(RESOURCE_DOORS_LATCH2_WAV, "doors/latch2.wav") \
    X(RESOURCE_DOORS_MEDTRY_WAV, "doors/medtry.wav") \
    X(RESOURCE_DOORS_MEDUSE_WAV, "doors/meduse.wav") \
    X(RESOURCE_DOORS_RUNETRY_WAV, "doors/runetry.wav") \
    X(RESOURCE_DOORS_RUNEUSE_WAV, "doors/runeuse.wav") \
    X(RESOURCE_DOORS_STNDR1_WAV, "doors/stndr1.wav") \
    X(RESOURCE_DOORS_STNDR2_WAV, "doors/stndr2.wav") \
    X(RESOURCE_DOORS_WINCH2_WAV, "doors/winch2.wav") \
    X(RESOURCE_DRAGON_ATTACK_WAV, "dragon/attack.wav") \
    X(RESOURCE_DRAGON_DEATH_WAV, "dragon/death.wav") \
    X(RESOURCE_DRAGON_PAIN_WAV, "dragon/pain.wav") \
    X(RESOURCE_DRAGON_SEE_WAV, "dragon/see.wav") \
    X(RESOURCE_EEL_EACTIVE1_WAV, "eel/eactive1.wav") \
    X(RESOURCE_EEL_EATT1_WAV, "eel/eatt1.wav") \
    X(RESOURCE_EEL_EDIE3R_WAV, "eel/edie3r.wav") \
    X(RESOURCE_EEL_EELC5_WAV, "eel/eelc5.wav") \
    X(RESOURCE_EEL_EPAIN3_WAV, "eel/epain3.wav") \
    X(RESOURCE_ENFORCER_DEATH1_WAV, "enforcer/death1.wav") \
    X(RESOURCE_ENFORCER_ENFIRE_WAV, "enforcer/enfire.wav") \
    X(RESOURCE_ENFORCER_ENFSTOP_WAV, "enforcer/enfstop.wav") \
    X(RESOURCE_ENFORCER_IDLE1_WAV, "enforcer/idle1.wav") \
    X(RESOURCE_ENFORCER_PAIN1_WAV, "enforcer/pain1.wav") \
    X(RESOURCE_ENFORCER_PAIN2_WAV, "enforcer/pain2.wav") \
    X(RESOURCE_ENFORCER_SIGHT1_WAV, "enforcer/sight1.wav") \
    X(RESOURCE_ENFORCER_SIGHT2_WAV, "enforcer/sight2.wav") \
    X(RESOURCE_ENFORCER_SIGHT3_WAV, "enforcer/sight3.wav") \
    X(RESOURCE_ENFORCER_SIGHT4_WAV, "enforcer/sight4.wav") \
    X(RESOURCE_EQUAKE_RUMBLE_WAV, "equake/rumble.wav") \
    X(RESOURCE_FISH_BITE_WAV, "fish/bite.wav") \
    X(RESOURCE_FISH_DEATH_WAV, "fish/death.wav") \
    X(RESOURCE_FISH_IDLE_WAV, "fish/idle.wav") \
    X(RESOURCE_GREM_ATTACK_WAV, "grem/attack.wav") \
    X(RESOURCE_GREM_DEATH_WAV, "grem/death.wav") \
    X(RESOURCE_GREM_IDLE_WAV, "grem/idle.wav") \
    X(RESOURCE_GREM_PAIN1_WAV, "grem/pain1.wav") \
    X(RESOURCE_GREM_PAIN2_WAV, "grem/pain2.wav") \
    X(RESOURCE_GREM_PAIN3_WAV, "grem/pain3.wav") \
    X(RESOURCE_GREM_SIGHT1_WAV, "grem/sight1.wav") \
    X(RESOURCE_GUARD_DEATH_WAV, "guard/death.wav") \
    X(RESOURCE_GUARD_PAIN1_WAV, "guard/pain1.wav") \
    X(RESOURCE_GUARD_SEE1_WAV, "guard/see1.wav") \
    X(RESOURCE_HIPITEMS_SPIKMINE_WAV, "hipitems/spikmine.wav") \
    X(RESOURCE_HIPWEAP_LASERG_WAV, "hipweap/laserg.wav") \
    X(RESOURCE_HIPWEAP_LASERRIC_WAV, "hipweap/laserric.wav") \
    X(RESOURCE_HIPWEAP_MJOLHIT_WAV, "hipweap/mjolhit.wav") \
    X(RESOURCE_HIPWEAP_MJOLSLAP_WAV, "hipweap/mjolslap.wav") \
    X(RESOURCE_HIPWEAP_MJOLTINK_WAV, "hipweap/mjoltink.wav") \
    X(RESOURCE_HIPWEAP_PROXBOMB_WAV, "hipweap/proxbomb.wav") \
    X(RESOURCE_HIPWEAP_PROXWARN_WAV, "hipweap/proxwarn.wav") \
    X(RESOURCE_HKNIGHT_ATTACK1_WAV, "hknight/attack1.wav") \
    X(RESOURCE_HKNIGHT_DEATH1_WAV, "hknight/death1.wav") \
    X(RESOURCE_HKNIGHT_IDLE_WAV, "hknight/idle.wav") \
    X(RESOURCE_HKNIGHT_PAIN1_WAV, "hknight/pain1.wav") \
    X(RESOURCE_HKNIGHT_SIGHT1_WAV, "hknight/sight1.wav") \
    X(RESOURCE_HKNIGHT_SLASH1_WAV, "hknight/slash1.wav") \
    X(RESOURCE_INFECTED_DEATH1_REV_WAV, "infected/death1_rev.wav") \
    X(RESOURCE_ITEMS_DAMAGE_WAV, "items/damage.wav") \
    X(RESOURCE_ITEMS_ITEMBK2_WAV, "items/itembk2.wav") \
    X(RESOURCE_ITEMS_PROTECT3_WAV, "items/protect3.wav") \
    X(RESOURCE_ITEMS_SUIT2_WAV, "items/suit2.wav") \
    X(RESOURCE_KNIGHT_IDLE_WAV, "knight/idle.wav") \
    X(RESOURCE_KNIGHT_KDEATH_WAV, "knight/kdeath.wav") \
    X(RESOURCE_KNIGHT_KHURT_WAV, "knight/khurt.wav") \
    X(RESOURCE_KNIGHT_KSIGHT_WAV, "knight/ksight.wav") \
    X(RESOURCE_KNIGHT_SWORD1_WAV, "knight/sword1.wav") \
    X(RESOURCE_KNIGHT_SWORD2_WAV, "knight/sword2.wav") \
    X(RESOURCE_MISC_H2OHIT1_WAV, "misc/h2ohit1.wav") \
    X(RESOURCE_MISC_LONGEXPL_WAV, "misc/longexpl.wav") \
    X(RESOURCE_MISC_NULL_WAV, "misc/null.wav") \
    X(RESOURCE_MISC_OUTWATER_WAV, "misc/outwater.wav") \
    X(RESOURCE_MISC_POWER_WAV, "misc/power.wav") \
    X(RESOURCE_MISC_QUAKEEND_WAV, "misc/quakeend.wav") \
    X(RESOURCE_MISC_QUAKE_WAV, "misc/quake.wav") \
    X(RESOURCE_MISC_RUNEKEY_WAV, "misc/runekey.wav") \
    X(RESOURCE_MISC_R_TELE1_WAV, "misc/r_tele1.wav") \
    X(RESOURCE_MISC_R_TELE2_WAV, "misc/r_tele2.wav") \
    X(RESOURCE_MISC_R_TELE3_WAV, "misc/r_tele3.wav") \
    X(RESOURCE_MISC_R_TELE4_WAV, "misc/r_tele4.wav") \
    X(RESOURCE_MISC_R_TELE5_WAV, "misc/r_tele5.wav") \
    X(RESOURCE_MISC_SECRET_WAV, "misc/secret.wav") \
    X(RESOURCE_MISC_TALK_WAV, "misc/talk.wav") \
    X(RESOURCE_MISC_TESLA_WAV, "misc/tesla.wav") \
    X(RESOURCE_MISC_TRIGGER1_WAV, "misc/trigger1.wav") \
    X(RESOURCE_MISC_WETSUIT_WAV, "misc/wetsuit.wav") \
    X(RESOURCE_OGRE_OGDRAG_WAV, "ogre/ogdrag.wav") \
    X(RESOURCE_OGRE_OGDTH_WAV, "ogre/ogdth.wav") \
    X(RESOURCE_OGRE_OGIDLE2_WAV, "ogre/ogidle2.wav") \
    X(RESOURCE_OGRE_OGIDLE_WAV, "ogre/ogidle.wav") \
    X(RESOURCE_OGRE_OGPAIN1_WAV, "ogre/ogpain1.wav") \
    X(RESOURCE_OGRE_OGSAWATK_WAV, "ogre/ogsawatk.wav") \
    X(RESOURCE_OGRE_OGWAKE_WAV, "ogre/ogwake.wav") \
    X(RESOURCE_ORB_ORB_DEATH_WAV, "orb/orb_death.wav") \
    X(RESOURCE_ORB_ORB_PAIN_WAV, "orb/orb_pain.wav") \
    X(RESOURCE_PENDULUM_HIT_WAV, "pendulum/hit.wav") \
    X(RESOURCE_PLASMA_EXPLODE_WAV, "plasma/explode.wav") \
    X(RESOURCE_PLASMA_FLIGHT_WAV, "plasma/flight.wav") \
    X(RESOURCE_PLATS_MEDPLAT1_WAV, "plats/medplat1.wav") \
    X(RESOURCE_PLATS_MEDPLAT2_WAV, "plats/medplat2.wav") \
    X(RESOURCE_PLATS_PLAT1_WAV, "plats/plat1.wav") \
    X(RESOURCE_PLATS_PLAT2_WAV, "plats/plat2.wav") \
    X(RESOURCE_PLATS_TRAIN1_WAV, "plats/train1.wav") \
    X(RESOURCE_PLATS_TRAIN2_WAV, "plats/train2.wav") \
    X(RESOURCE_PLAYER_AXHIT1_WAV, "player/axhit1.wav") \
    X(RESOURCE_PLAYER_AXHIT2_WAV, "player/axhit2.wav") \
    X(RESOURCE_PLAYER_DEATH1_WAV, "player/death1.wav") \
    X(RESOURCE_PLAYER_DEATH2_WAV, "player/death2.wav") \
    X(RESOURCE_PLAYER_DEATH3_WAV, "player/death3.wav") \
    X(RESOURCE_PLAYER_DEATH4_WAV, "player/death4.wav") \
    X(RESOURCE_PLAYER_DEATH5_WAV, "player/death5.wav") \
    X(RESOURCE_PLAYER_DROWN1_WAV, "player/drown1.wav") \
    X(RESOURCE_PLAYER_DROWN2_WAV, "player/drown2.wav") \
    X(RESOURCE_PLAYER_GASP1_WAV, "player/gasp1.wav") \
    X(RESOURCE_PLAYER_GASP2_WAV, "player/gasp2.wav") \
    X(RESOURCE_PLAYER_GIB_WAV, "player/gib.wav") \
    X(RESOURCE_PLAYER_H2ODEATH_WAV, "player/h2odeath.wav") \
    X(RESOURCE_PLAYER_H2OJUMP_WAV, "player/h2ojump.wav") \
    X(RESOURCE_PLAYER_INH2O_WAV, "player/inh2o.wav") \
    X(RESOURCE_PLAYER_INLAVA_WAV, "player/inlava.wav") \
    X(RESOURCE_PLAYER_LAND2_WAV, "player/land2.wav") \
    X(RESOURCE_PLAYER_LAND_WAV, "player/land.wav") \
    X(RESOURCE_PLAYER_LBURN1_WAV, "player/lburn1.wav") \
    X(RESOURCE_PLAYER_LBURN2_WAV, "player/lburn2.wav") \
    X(RESOURCE_PLAYER_PAIN1_WAV, "player/pain1.wav") \
    X(RESOURCE_PLAYER_PAIN2_WAV, "player/pain2.wav") \
    X(RESOURCE_PLAYER_PAIN3_WAV, "player/pain3.wav") \
    X(RESOURCE_PLAYER_PAIN4_WAV, "player/pain4.wav") \
    X(RESOURCE_PLAYER_PAIN5_WAV, "player/pain5.wav") \
    X(RESOURCE_PLAYER_PAIN6_WAV, "player/pain6.wav") \
    X(RESOURCE_PLAYER_SLIMBRN2_WAV, "player/slimbrn2.wav") \
    X(RESOURCE_PLAYER_TELEDTH1_WAV, "player/teledth1.wav") \
    X(RESOURCE_PLAYER_TORNOFF2_WAV, "player/tornoff2.wav") \
    X(RESOURCE_PLAYER_UDEATH_WAV, "player/udeath.wav") \
    X(RESOURCE_RKNIGHT_DEATH_01_WAV, "rknight/death_01.wav") \
    X(RESOURCE_RKNIGHT_DEATH_02_WAV, "rknight/death_02.wav") \
    X(RESOURCE_RKNIGHT_IDLE_02_WAV, "rknight/idle_02.wav") \
    X(RESOURCE_RKNIGHT_IDLE_03_WAV, "rknight/idle_03.wav") \
    X(RESOURCE_RKNIGHT_IDLE_05_WAV, "rknight/idle_05.wav") \
    X(RESOURCE_RKNIGHT_PAIN_01_WAV, "rknight/pain_01.wav") \
    X(RESOURCE_RKNIGHT_PAIN_02_WAV, "rknight/pain_02.wav") \
    X(RESOURCE_RKNIGHT_PAIN_03_WAV, "rknight/pain_03.wav") \
    X(RESOURCE_RKNIGHT_SIGHT_01_WAV, "rknight/sight_01.wav") \
    X(RESOURCE_RKNIGHT_SIGHT_03_WAV, "rknight/sight_03.wav") \
    X(RESOURCE_RUNES_END1_WAV, "runes/end1.wav") \
    X(RESOURCE_RUNES_END2_WAV, "runes/end2.wav") \
    X(RESOURCE_RUNES_END3_WAV, "runes/end3.wav") \
    X(RESOURCE_RUNES_END4_WAV, "runes/end4.wav") \
    X(RESOURCE_SCOURGE_IDLE_WAV, "scourge/idle.wav") \
    X(RESOURCE_SCOURGE_PAIN2_WAV, "scourge/pain2.wav") \
    X(RESOURCE_SCOURGE_PAIN_WAV, "scourge/pain.wav") \
    X(RESOURCE_SCOURGE_SIGHT_WAV, "scourge/sight.wav") \
    X(RESOURCE_SCOURGE_TAILSWNG_WAV, "scourge/tailswng.wav") \
    X(RESOURCE_SCOURGE_WALK_WAV, "scourge/walk.wav") \
    X(RESOURCE_SHALRATH_ATTACK2_WAV, "shalrath/attack2.wav") \
    X(RESOURCE_SHALRATH_ATTACK_WAV, "shalrath/attack.wav") \
    X(RESOURCE_SHALRATH_DEATH_WAV, "shalrath/death.wav") \
    X(RESOURCE_SHALRATH_IDLE_WAV, "shalrath/idle.wav") \
    X(RESOURCE_SHALRATH_PAIN_WAV, "shalrath/pain.wav") \
    X(RESOURCE_SHALRATH_SIGHT_WAV, "shalrath/sight.wav") \
    X(RESOURCE_SHAMBLER_MELEE1_WAV, "shambler/melee1.wav") \
    X(RESOURCE_SHAMBLER_MELEE2_WAV, "shambler/melee2.wav") \
    X(RESOURCE_SHAMBLER_SATTCK1_WAV, "shambler/sattck1.wav") \
    X(RESOURCE_SHAMBLER_SBOOM_WAV, "shambler/sboom.wav") \
    X(RESOURCE_SHAMBLER_SDEATH_WAV, "shambler/sdeath.wav") \
    X(RESOURCE_SHAMBLER_SHURT2_WAV, "shambler/shurt2.wav") \
    X(RESOURCE_SHAMBLER_SIDLE_WAV, "shambler/sidle.wav") \
    X(RESOURCE_SHAMBLER_SMACK_WAV, "shambler/smack.wav") \
    X(RESOURCE_SHAMBLER_SSIGHT_WAV, "shambler/ssight.wav") \
    X(RESOURCE_SHIELD_FADEOUT_WAV, "shield/fadeout.wav") \
    X(RESOURCE_SHIELD_HIT_WAV, "shield/hit.wav") \
    X(RESOURCE_SOLDIER_DEATH1_WAV, "soldier/death1.wav") \
    X(RESOURCE_SOLDIER_IDLE_WAV, "soldier/idle.wav") \
    X(RESOURCE_SOLDIER_PAIN1_WAV, "soldier/pain1.wav") \
    X(RESOURCE_SOLDIER_PAIN2_WAV, "soldier/pain2.wav") \
    X(RESOURCE_SOLDIER_SATTCK1_WAV, "soldier/sattck1.wav") \
    X(RESOURCE_SOLDIER_SIGHT1_WAV, "soldier/sight1.wav") \
    X(RESOURCE_SPHERE_SPHERE_WAV, "sphere/sphere.wav") \
    X(RESOURCE_S_WRATH_SMASH_WAV, "s_wrath/smash.wav") \
    X(RESOURCE_WEAPONS_AX1_WAV, "weapons/ax1.wav") \
    X(RESOURCE_WEAPONS_BOUNCE2_WAV, "weapons/bounce2.wav") \
    X(RESOURCE_WEAPONS_BOUNCE_WAV, "weapons/bounce.wav") \
    X(RESOURCE_WEAPONS_CHAIN1_WAV, "weapons/chain1.wav") \
    X(RESOURCE_WEAPONS_CHAIN2_WAV, "weapons/chain2.wav") \
    X(RESOURCE_WEAPONS_CHAIN3_WAV, "weapons/chain3.wav") \
    X(RESOURCE_WEAPONS_GRENADE_WAV, "weapons/grenade.wav") \
    X(RESOURCE_WEAPONS_GUNCOCK_WAV, "weapons/guncock.wav") \
    X(RESOURCE_WEAPONS_LHIT_WAV, "weapons/lhit.wav") \
    X(RESOURCE_WEAPONS_LOCK4_WAV, "weapons/lock4.wav") \
    X(RESOURCE_WEAPONS_LSTART_WAV, "weapons/lstart.wav") \
    X(RESOURCE_WEAPONS_PKUP_WAV, "weapons/pkup.wav") \
    X(RESOURCE_WEAPONS_ROCKET1I_WAV, "weapons/rocket1i.wav") \
    X(RESOURCE_WEAPONS_R_EXP3_WAV, "weapons/r_exp3.wav") \
    X(RESOURCE_WEAPONS_SGUN1_WAV, "weapons/sgun1.wav") \
    X(RESOURCE_WEAPONS_SHOTGN2_WAV, "weapons/shotgn2.wav") \
    X(RESOURCE_WEAPONS_SPIKE2_WAV, "weapons/spike2.wav") \
    X(RESOURCE_WEAPONS_TINK1_WAV, "weapons/tink1.wav") \
    X(RESOURCE_WIZARD_WATTACK_WAV, "wizard/wattack.wav") \
    X(RESOURCE_WIZARD_WDEATH_WAV, "wizard/wdeath.wav") \
    X(RESOURCE_WIZARD_WIDLE1_WAV, "wizard/widle1.wav") \
    X(RESOURCE_WIZARD_WIDLE2_WAV, "wizard/widle2.wav") \
    X(RESOURCE_WIZARD_WPAIN_WAV, "wizard/wpain.wav") \
    X(RESOURCE_WIZARD_WSIGHT_WAV, "wizard/wsight.wav") \
    X(RESOURCE_WRATH_WATT_WAV, "wrath/watt.wav") \
    X(RESOURCE_WRATH_WDTHC_WAV, "wrath/wdthc.wav") \
    X(RESOURCE_WRATH_WPAIN_WAV, "wrath/wpain.wav") \
    X(RESOURCE_WRATH_WSEE_WAV, "wrath/wsee.wav") \
    X(RESOURCE_ZOMBIE_IDLE_W2_WAV, "zombie/idle_w2.wav") \
    X(RESOURCE_ZOMBIE_Z_FALL_WAV, "zombie/z_fall.wav") \
    X(RESOURCE_ZOMBIE_Z_GIB_WAV, "zombie/z_gib.wav") \
    X(RESOURCE_ZOMBIE_Z_HIT_WAV, "zombie/z_hit.wav") \
    X(RESOURCE_ZOMBIE_Z_IDLE1_WAV, "zombie/z_idle1.wav") \
    X(RESOURCE_ZOMBIE_Z_IDLE_WAV, "zombie/z_idle.wav") \
    X(RESOURCE_ZOMBIE_Z_MISS_WAV, "zombie/z_miss.wav") \
    X(RESOURCE_ZOMBIE_Z_PAIN1_WAV, "zombie/z_pain1.wav") \
    X(RESOURCE_ZOMBIE_Z_PAIN_WAV, "zombie/z_pain.wav") \
    X(RESOURCE_ZOMBIE_Z_SHOT1_WAV, "zombie/z_shot1.wav") \
    X(RESOURCE_DISCHARGE, "discharge")

typedef enum q1_runtime_name {
#define Q1_RUNTIME_NAME_ENUM(key, text) Q1_NAME_##key,
    Q1_RUNTIME_NAME_LIST(Q1_RUNTIME_NAME_ENUM)
#undef Q1_RUNTIME_NAME_ENUM
    Q1_NAME_COUNT
} q1_runtime_name;

typedef struct q1_map_state q1_map_state;
typedef struct q1_map_runtime q1_map_runtime;

typedef enum q1_entity_kind {
    Q1_ENTITY,
    Q1_MONSTER,
    Q1_PROJECTILE,
    Q1_TIMER,
    Q1_PICKUP,
    Q1_GIB,
    Q1_MAP,
    Q1_BOSS_CHILD,
    Q1_ROGUE_TEAM_STATE,
    Q1_SOURCE_CTF_FLAG,
    Q1_SOURCE_CTF_RUNE,
    Q1_SOURCE_CTF_RUNE_TIMER,
    Q1_SOURCE_ROGUE_TAG,
    Q1_SOURCE_ROGUE_FLAG,
    Q1_SOURCE_ROGUE_FLAG_BASE,
    Q1_SOURCE_ROGUE_RUNE,
    Q1_SOURCE_ROGUE_RUNE_TIMER,
    Q1_BODY
} q1_entity_kind;
typedef enum q1_think_kind {
    Q1_THINK_NONE,
    Q1_THINK_REMOVE,
    Q1_THINK_MONSTER_START,
    Q1_THINK_MONSTER_FRAME,
    Q1_THINK_AXE,
    Q1_THINK_EXPLODE,
    Q1_THINK_VORE,
    Q1_THINK_SPRITE,
    Q1_THINK_WIZARD,
    Q1_THINK_RESPAWN,
    Q1_THINK_MEGA_ROT,
    Q1_THINK_MONSTER_FOUND,
    Q1_THINK_ITEM_PLACE,
    Q1_THINK_DEATH_BUBBLES,
    Q1_THINK_BUBBLE,
    Q1_THINK_HIP_LASER,
    Q1_THINK_PROX_WATCH,
    Q1_THINK_PROX_EXPLODE,
    Q1_THINK_HAMMER_STRIKE,
    Q1_THINK_HAMMER_BOLT,
    Q1_THINK_MULTI_SPLIT,
    Q1_THINK_MINI_EXPLODE,
    Q1_THINK_MULTI_EXPLODE,
    Q1_THINK_MULTI_ACQUIRE,
    Q1_THINK_MULTI_HOME,
    Q1_THINK_PLASMA_LAUNCH,
    Q1_THINK_SHIELD,
    Q1_THINK_SPHERE_ORBIT,
    Q1_THINK_SPHERE_ATTACK,
    Q1_THINK_HOOK_FLY,
    Q1_THINK_HOOK_TRACK,
    Q1_THINK_HOOK_RESET,
    Q1_THINK_HOOK_LINK,
    Q1_THINK_SCOURGE_TRIGGER,
    Q1_THINK_WRATH_HOME,
    Q1_THINK_TELEPORT_FOG,
    Q1_THINK_ARMAGON_BODY,
    Q1_THINK_ARMAGON_EXPLOSION,
    Q1_THINK_MULTI_EXPLOSION,
    Q1_THINK_HOOK_LAUNCH,
    Q1_THINK_MAP,
    Q1_THINK_MG3_HAMMER,
    Q1_THINK_MG3_ITEM_START,
    Q1_THINK_DEMODOG_EXPLODE,
    Q1_THINK_HORDE_HEAD_WAIT,
    Q1_THINK_HORDE_HEAD_STEP,
    Q1_THINK_HEAVY_SOURCE_DIE,
    Q1_THINK_GHOST_BUBBLES,
    Q1_THINK_HOMING_FLAME,
    Q1_THINK_BOSS_CHILD,
    Q1_THINK_SPAWN_TEMPLATE,
    Q1_THINK_SOURCE_CTF_FLAG_PLACE,
    Q1_THINK_SOURCE_CTF_FLAG,
    Q1_THINK_SOURCE_CTF_RUNE_SPAWN,
    Q1_THINK_SOURCE_CTF_RUNE_RESPAWN,
    Q1_THINK_SOURCE_ROGUE_TAG_PLACE,
    Q1_THINK_SOURCE_ROGUE_TAG,
    Q1_THINK_SOURCE_ROGUE_TAG_FALL,
    Q1_THINK_SOURCE_ROGUE_TAG_RESPAWN,
    Q1_THINK_SOURCE_ROGUE_FLAG_PLACE,
    Q1_THINK_SOURCE_ROGUE_FLAG,
    Q1_THINK_SOURCE_ROGUE_RUNE_SPAWN,
    Q1_THINK_SOURCE_ROGUE_RUNE_RESPAWN
} q1_think_kind;
typedef enum q1_projectile_kind {
    Q1_SPIKE,
    Q1_SUPERSPIKE,
    Q1_ROCKET,
    Q1_GRENADE,
    Q1_WIZARD_SPIKE,
    Q1_KNIGHT_SPIKE,
    Q1_ENFORCER_LASER,
    Q1_OGRE_GRENADE,
    Q1_ZOMBIE_GRENADE,
    Q1_VORE_BALL,
    Q1_LAVA_BALL,
    Q1_HIP_LASER,
    Q1_PROXIMITY,
    Q1_LAVA_SPIKE,
    Q1_MULTI_GRENADE,
    Q1_MULTI_ROCKET,
    Q1_PLASMA,
    Q1_VENGEANCE,
    Q1_ROGUE_HOOK,
    Q1_CTF_HOOK,
    Q1_WRATH_MISSILE,
    Q1_LAVAMAN_BALL,
    Q1_DRAGON_FIREBALL,
    Q1_MG3_OGRE_ROCKET,
    Q1_DEMODOG_GRENADE,
    Q1_HEAVY_SPIKE,
    Q1_MG3_LAVAMAN_BALL,
    Q1_ORB_ROCK,
    Q1_SHUB_GRENADE,
    Q1_BOSS_SPHERE_SHOT,
    Q1_BOSS_BLAST_SHOT,
    Q1_FINAL_ROCK
} q1_projectile_kind;
typedef enum q1_heavy_kind {
    Q1_HEAVY_NONE,
    Q1_HEAVY_SUPER_SHAMBLER,
    Q1_HEAVY_RUNE_KNIGHT
} q1_heavy_kind;
typedef enum q1_boss_kind {
    Q1_BOSS_NONE,
    Q1_BOSS_GHOST,
    Q1_BOSS_ORB,
    Q1_BOSS_SHUB_ZOMBIE,
    Q1_BOSS_OLDNEW,
    Q1_BOSS_FINAL
} q1_boss_kind;
typedef enum q1_ai {
    Q1_AI_STAND,
    Q1_AI_WALK,
    Q1_AI_RUN,
    Q1_AI_CHARGE_SIDE,
    Q1_AI_MELEE_SIDE,
    Q1_AI_CHARGE,
    Q1_AI_MELEE,
    Q1_AI_PAINFORWARD,
    Q1_AI_TURN,
    Q1_AI_FACE,
    Q1_AI_PAIN,
    Q1_AI_FORWARD
} q1_ai;
typedef enum q1_frame_operation_kind {
    Q1_FRAME_AI,
    Q1_FRAME_SOUND,
    Q1_FRAME_SOLID,
    Q1_FRAME_LIGHTSTYLE,
    Q1_FRAME_ACTION
} q1_frame_operation_kind;
typedef struct q1_frame_operation {
    q1_frame_operation_kind kind;
    q1_ai ai;
    q1_frame_action action;
    float distance, attenuation, chance;
    int32_t channel;
    bool greater;
    const char *text;
    q1_runtime_name sound;
} q1_frame_operation;
typedef struct q1_frame {
    const char *name;
    uint16_t frame, next;
    uint32_t operation;
    uint8_t count;
} q1_frame;
typedef struct q1_species {
    qa_q1_species species;
    const char *classname, *model, *head;
    q1_runtime_name sight;
    const char *stand, *walk, *run, *missile;
    float health, gib_health;
    qa_bounds bounds;
    uint32_t flags;
    bool melee;
} q1_species;
typedef struct q1_monster {
    const q1_species *species;
    uint64_t birth_epoch;
    bool dead;
    qa_string_id path;
    uint16_t current_frame, next_frame;
    q1_ref enemy, old_enemy, charmer, charm_goal, move_target, previous_corner;
    double pause_until, attack_finished, pain_finished, search_until, idle_until, straight_after,
        dodge_after, hostile_until, follow_until;
    uint32_t counter, lightning_count;
    uint8_t attack_state, in_pain, hunting_charmer;
    bool sliding, lefty, counted_death, jump_touch, horde, path_end;
    struct {
        bool enabled, waiting, path_wait, started, rocket_ogre, allow_path, normal_use;
        bool infected, transformed, risen, infection_count_pending, demodog;
        uint8_t infected_kind, corpse;
        q1_heavy_kind heavy;
        q1_boss_kind boss;
        uint8_t projectiles, projectile_max, combat_style;
        double damage_at;
    } addon;
    union {
        struct {
            qa_vec3 anchor, destination;
            uint16_t death_frame;
            int32_t shots, shocks;
            bool touch, immune, awake, swipe_side, vortex_side;
            uint32_t phase, cycles, stage;
            qa_string_id waves[4];
        } boss;
        struct {
            q1_ref child;
            uint32_t lightning_count;
            int32_t nails;
        } heavy;
        struct {
            int16_t pitch;
        } eel;
        struct {
            bool asleep;
        } mummy;
        struct {
            bool awakened, pain_disabled;
        } sword;
        struct {
            q1_ref trigger;
            double dodge_until;
            bool initialized, silent, previous_silent;
        } scourge;
        struct {
            uint32_t children;
        } morph;
        struct {
            qa_vec3 last_velocity;
            uint16_t missile;
            uint8_t pain_sequence, death_state;
            bool attacking;
        } dragon;
        struct {
            q1_ref body;
            qa_vec3 old_origin;
            float torso_yaw, aim_threshold;
            double idle_at;
            uint8_t repulse_state;
            bool behind;
        } armagon;
        struct {
            q1_ref last_victim, flee_goal;
            qa_vec3 view_angles;
            qa_q1_weapon weapon;
            float current_ammo;
            double protection_sound;
            uint8_t touch;
            bool stolen, gorging, pain_disabled;
        } gremlin;
    } source;
} q1_monster;
typedef struct q1_projectile {
    q1_projectile_kind kind;
    qa_q1_weapon weapon;
    qa_attack attack;
    q1_ref enemy, activator, surface;
    q1_ref links[3];
    qa_vec3 right, movedir, launch_angles;
    float damage, radius_damage;
    double expires;
    uint32_t count;
    bool remove_touch, mini, detonating;
} q1_projectile;
typedef enum q1_drop_kind {
    Q1_DROP_NONE,
    Q1_DROP_CTF_AMMO,
    Q1_DROP_CTF_WEAPON,
    Q1_DROP_ROGUE_WEAPON
} q1_drop_kind;
typedef struct q1_pickup {
    qa_item_id item;
    qa_string_id original_model, sound;
    float count, respawn;
    uint32_t kind;
    uint32_t upgrade_flag;
    uint8_t upgrade;
    q1_ref holder;
    bool hidden, mega, artifact, mission, random, external;
    bool backpack_rank, avoid_underwater_lightning;
    float absorption, duration;
    qa_q1_weapon weapon;
    q1_drop_kind drop;
    float owner_delay;
    float ammo[QA_Q1_AMMO_COUNT];
} q1_pickup;
typedef struct q1_timed_effect {
    double expires;
    float volume;
    uint32_t count;
} q1_timed_effect;
typedef struct q1_actor {
    struct q1_actor *allocation_next, *pool_next;
    q1_map_state *map;
    qa_pickup_lease pickup_observation;
    bool restored_target;
    qa_actor_id id;
    q1_ref owner, activator;
    qa_string_id classname, model, target, targetname, killtarget, message;
    qa_string_id source_netname, source_kill_string, source_death_type, source_team;
    float rogue_next_update;
    q1_ref rogue_tag_owner;
    float rogue_runes_spawned;
    q1_ref rogue_rune_spawn;
    float ctf_last_capture, ctf_last_capture_team;
    float ctf_runes_spawned;
    q1_ref ctf_rune_spawn;
    qa_vec3 initial_angles;
    qa_physics_properties physics;
    q1_entity_kind kind;
    q1_think_kind think;
    double next_think;
    float max_health, delay, wait, speed, damage, count, alpha, scale;
    uint32_t spawnflags, effects;
    /* Source-only movement bits such as FL_ITEM. Common physics bits retain
     * their existing authority in physics.flags. CTF actors use their unions. */
    uint32_t source_movement_flags;
    int32_t frame, skin;
    bool active, native, aimed_damage, consumed_corpse, axe_hit, touch_disabled;
    struct {
        q1_think_kind think;
        double next_think;
        bool active, damageable;
    } frozen;
    union {
        q1_monster monster;
        q1_projectile projectile;
        q1_pickup pickup;
        q1_timed_effect effect;
        q1_boss_child boss_child;
        struct { int32_t color_map; } body;
        float rogue_fields[QA_Q1_ROGUE_FIELDS];
        struct { float frags, message_time; } source_tag;
        float rogue_rune;
        struct {
            float values[3]; /* team, cnt, super_time */
            qa_vec3 origin, angles;
            bool placed;
        } rogue_flag;
        struct {
            qa_vec3 base, angles;
            float return_time;
            uint32_t movement_flags;
            bool placed;
        } source_flag;
        struct {
            qa_string_id rune;
            uint32_t movement_flags;
        } source_rune;
    } state;
} q1_actor;
typedef struct q1_character {
    qa_q1_character_input input;
    qa_q1_character_pose pose;
    qa_q1_life life;
    uint64_t birth_epoch;
    qa_string_id model;
    int32_t frame;
    qa_q1_frame_range animation;
    uint16_t animation_frame, walk_frame;
    uint8_t locomotion;
    bool death_animation, attack_animation, in_water, weapon_hidden;
    qa_vec3 view_offset;
    double next_animation, pain_until, air_until, hazard_at;
    float fall_speed, drown_damage;
} q1_character;
struct qa_q1_source_client_view;
typedef struct q1_source_info {qa_string_id key,value;} q1_source_info;
typedef struct q1_player {
    struct q1_player *allocation_next, *pool_next;
    qa_actor_id id;
    qa_q1_input input;
    qa_q1_weapon weapon;
    int32_t weapon_frame, animation_base, nail_side;
    float current_ammo;
    double attack_finished, next_weapon_frame, animation_at, lightning_sound_at;
    double hostile_until, mega_rot_at, air_finished, drown_at, hazard_at;
    double power_expires[QA_Q1_POWER_COUNT];
    uint64_t power_order[QA_Q1_POWER_COUNT], power_sequence;
    double power_flash[QA_Q1_POWER_COUNT], scuba_at, shield_until, shield_sound_at;
    uint64_t wetsuit_scaled_frame;
    uint16_t power_warned, power_lost;
    uint8_t wetsuit_scaled_level;
    qa_q1_auto_switch auto_switch;
    qa_q1_mg3_progress mg3_progress;
    q1_ref mg3_hammer_target;
    double mg3_hammer_until;
    double horde_axe_chain_until;
    uint32_t horde_axe_chain;
    int32_t mg3_hammer_body;
    bool mg3_infinite_ammo, mg3_hammer_glow;
    q1_ref killer;
    q1_ref hook;
    qa_q1_input grapple_input;
    bool grapple_release, grapple_pulling;
    struct {
        q1_ref animation;
        double attack_finished, release_time;
        int32_t frame;
        bool selected, available;
    } grapple_weapon;
    float max_health, drown_damage;
    qa_vec3 punch;
    uint32_t client_slot;
    bool active, continuous, primary_holstered, arsenal, character, source_client;
    qa_q1_game *inventory_game;
    qa_inventory_lease weapon_definitions;
    q1_source_info *source_info;
    size_t source_info_count;
    float source_frags,source_team;
    bool source_observer,source_no_target,source_god_mode,source_superhealth;
    q1_ref source_spectator_goal,source_spectator_track;
    int32_t source_impulse;
    bool source_use,source_death_recorded;
    bool finale_held_present, finale_held;
    double source_respawn_requested_at;
    q1_character character_state;
} q1_player;
typedef struct q1_rogue_rune_player {
    struct q1_rogue_rune_player *next;
    qa_actor_id actor;
    uint32_t rune;
    double notice, earth_noise, black_noise, hell_noise, regeneration;
} q1_rogue_rune_player;
struct qa_q1_game {
    qa_builtin_services services;
    qa_q1_options options;
    qa_q1_host host;
    qa_string_id runtime_names[Q1_NAME_COUNT];
    qa_string_id body_queue_class;
    qa_string_id field_keys[QA_TARGET_KEY_TOTAL];
    qa_cvar_handle source_settings[QA_Q1_SOURCE_SETTING_COUNT];
    qa_q1_source_flags_services source_flags;
    qa_q1_source_runes_services source_runes;
    qa_q1_source_rogue_tag_services source_rogue_tag;
    qa_q1_source_rogue_flags_services source_rogue_flags;
    q1_rogue_rune_player *rogue_rune_players;
    void *source_client_context;
    bool (*source_client_publish)(void *,const struct qa_q1_source_client_view *,qa_error *);
    bool (*source_client_observer)(void *,qa_actor_id,bool,qa_error *);
    double source_captures[2];
    q1_map_runtime *maps;
    struct q1_wire_state *wire;
    q1_actor **actors, *allocated_actors, *spare_actors, *retired_actors;
    q1_player **players, *allocated_players, *spare_players, *retired_players;
    uint32_t capacity, total_monsters, killed_monsters, hellknight_melee;
    uint32_t authored_gremlins, spawned_gremlins;
    qa_builtin_random random;
    q1_ref sight_actor, horn_charmer, rogue_runes_world, body_queue_head;
    double time, elapsed, sight_time;
    double finale_last_poll;
    bool finale_polled, finale_acknowledged;
    uint64_t time_ns, attack_sequence;
    uint32_t force_retouch;
    uint32_t check_client_slot;
    double check_client_time;
    int32_t check_client_cluster;
    qa_vec3 check_client_eye;
    qa_vec3 forward, right, up;
    q1_ref qw_multi_entity;
    float qw_multi_damage, qw_blood_count, qw_puff_count;
    float qw_rj;
    qa_vec3 qw_blood_origin, qw_puff_origin;
    qa_item_id weapons[QA_Q1_WEAPON_COUNT], ammo[QA_Q1_AMMO_COUNT];
    qa_string_id weapon_models[QA_Q1_WEAPON_COUNT];
    qa_string_id hammer_glow_model, blood_shotgun_model, blood_super_shotgun_model;
    qa_string_id player_model, eyes_model, player_head_model;
    qa_item_id vengeance_item;
    qa_supply *source_supply;
    qa_builtin_snapshot_frame *snapshots;
    size_t observation_depth;
    size_t retention_depth;
    bool destroy_pending;
    bool continuation_pending;
    bool run_straight;
    bool rogue_runes_started;
    bool component_admitted;
    uint8_t rune_knight_melee, enemy_range;
    bool enemy_visible;
};
void q1_source_client_clear(q1_player *);
bool q1_source_flag_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_source_flag_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_flag_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_runes_start(qa_q1_game *, qa_error *);
bool q1_source_rune_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rune_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_rogue_tag_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_source_rogue_tag_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rogue_tag_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_rogue_flag_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_source_rogue_flag_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rogue_flag_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_rogue_rune_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rogue_rune_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
void q1_source_rogue_runes_release(qa_q1_game *, qa_actor_id);
void q1_source_rogue_runes_free(qa_q1_game *);
bool q1_source_value(const qa_q1_game *, qa_q1_source_setting, float fallback, float *, qa_error *);

extern const q1_frame q1_frames[];
bool q1_map_spawn(qa_q1_game *, q1_actor *, const qa_q1_spawn *, bool *, qa_error *);
bool q1_map_use(qa_q1_game *, q1_actor *, qa_actor_id other, qa_actor_id activator, qa_error *);
bool q1_map_touch(qa_q1_game *, q1_actor *, const qa_touch_contact *, qa_error *);
bool q1_map_blocked(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_radius_only(const qa_q1_game *, qa_actor_id);
bool q1_map_clone(qa_q1_game *, const q1_actor *, q1_actor *, qa_error *);
void q1_map_actor_released(qa_q1_game *, q1_actor *);
void q1_map_rotation_released(qa_q1_game *, qa_actor_id);
void q1_map_addon_released(qa_q1_game *, qa_actor_id);
void q1_map_addon_clone(qa_q1_game *, qa_actor_id, qa_actor_id);
void q1_map_destroy(qa_q1_game *);
void q1_map_frame_begin(qa_q1_game *);
bool q1_map_addon_frame(qa_q1_game *, qa_error *);
bool q1_map_level_frame(qa_q1_game *, const qa_source_frame *, qa_error *);
bool q1_map_ctf_frame(qa_q1_game *, qa_error *);
bool q1_map_collision(const q1_actor *, qa_actor_collision *);
bool q1_map_bind_target(qa_q1_game *, q1_actor *, qa_error *);
bool q1_spawn_template(qa_q1_game *, const qa_q1_spawn *, const qa_body_state *, qa_actor_id *,
                       qa_error *);
bool q1_map_spawn_template_wait(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_multi_explosion_begin(qa_q1_game *, q1_actor *, qa_error *);
bool q1_mg3_hammer_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_mg3_hammer_strike(qa_q1_game *, q1_actor *, qa_error *);
bool q1_mg3_weapon_frame(qa_q1_game *, q1_player *, qa_error *);
bool q1_mg3_capacities(qa_q1_game *, q1_player *, qa_error *);
bool q1_mg3_upgrade(qa_q1_game *, q1_player *, unsigned type, uint32_t flag, bool *collected,
                    float *maximum, qa_error *);
bool q1_mg3_impulse(qa_q1_game *, q1_player *, uint8_t, bool *, qa_error *);
bool q1_mg3_debug_upgrade(qa_q1_game *, qa_actor_id, unsigned, uint32_t, qa_error *);
bool q1_addon_target(qa_q1_game *, q1_actor *, qa_actor_id *, qa_error *);
bool q1_addon_contents(qa_q1_game *, q1_actor *, bool *, qa_error *);
unsigned q1_mg3_range(const q1_actor *, float distance);
bool q1_addon_move(qa_q1_game *, q1_actor *, float distance, bool seen, qa_error *);
bool q1_rocket_ogre_frame(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_rocket_ogre_override(const char *);
bool q1_rocket_ogre_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_sprite_explosion(qa_q1_game *, q1_actor *, qa_error *);
bool q1_sprite_prepare(qa_q1_game *, q1_actor *, qa_error *);
bool q1_hipnotic_hammer_base(qa_q1_game *, q1_player *, qa_vec3, qa_q1_weapon, qa_error *);
extern const q1_frame_operation q1_frame_operations[];
extern const size_t q1_frame_count;
uint16_t q1_frame_index(const char *);
uint16_t q1_infected_frame(uint16_t);
const q1_species *q1_infected_form(qa_q1_species, unsigned corpse);
bool q1_infected_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_infected_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_monster_count_kill(qa_q1_game *, q1_actor *, qa_actor_id killer, qa_error *);
bool q1_monster_death_report(qa_q1_game *, q1_actor *, qa_actor_id killer, bool count, qa_error *);
bool q1_demodog_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_demodog_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_demodog_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_demodog_grenade_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_demodog_explode(qa_q1_game *, q1_actor *, qa_actor_id ignore, qa_error *);
bool q1_heavy_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_heavy_check_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_heavy_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_heavy_pain(qa_q1_game *, q1_actor *, qa_actor_id, float, qa_error *);
bool q1_heavy_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_heavy_spike_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
uint16_t q1_mg3_lavaman_frame(uint16_t);
bool q1_lavaman_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_spawn(qa_q1_game *, q1_actor *, bool *handled, qa_error *);
bool q1_boss_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_boss_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_orb_check_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_orb_rock_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_shub_grenade_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_spawn_shub_zombie(qa_q1_game *, qa_actor_id *, qa_error *);
bool q1_spawn_homing_flame(qa_q1_game *, q1_actor *, qa_actor_id *, qa_error *);
bool q1_homing_flame_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_ghost_bubbles(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_pain_lightning(qa_q1_game *, q1_actor *, qa_vec3, qa_error *);
uint16_t q1_shub_zombie_frame(uint16_t);
bool q1_horde_head_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_horde_axe_delay(qa_q1_game *, q1_player *, float *interval, qa_error *);
const q1_species *q1_species_find(const char *);
q1_actor *q1_entity(qa_q1_game *, qa_actor_id);
const q1_actor *q1_entity_const(const qa_q1_game *, qa_actor_id);
q1_player *q1_player_get(qa_q1_game *, qa_actor_id);
q1_player *q1_player_allocate(qa_q1_game *, qa_actor_id, qa_error *);
bool q1_inventory_bind(qa_q1_game *, q1_player *, qa_error *);
bool q1_inventory_register(qa_q1_game_operation *, qa_actor_id, qa_error *);
bool q1_inventory_attach(qa_q1_game_operation *, q1_player *, qa_error *);
void q1_inventory_close(qa_q1_game *, q1_player *);
bool q1_alive(qa_q1_game *, qa_actor_id);
float q1_random(qa_q1_game *);
float q1_health(qa_q1_game *, qa_actor_id);
bool q1_damageable(qa_q1_game *, qa_actor_id);
float q1_actor_view_height(const q1_actor *, bool player);
bool q1_target(qa_q1_game *, qa_actor_id, qa_q1_target *);
bool q1_classnamed(qa_q1_game *, qa_actor_id, qa_string_id);
bool q1_model(qa_q1_game *, q1_actor *, const char *, qa_error *);
const char *q1_runtime_name_text(q1_runtime_name);
q1_runtime_name q1_door_key_sound(int32_t world_type, bool accepted);
bool q1_sound_resource(qa_q1_game *, qa_actor_id, qa_string_id, int32_t channel, float attenuation,
                       float volume, qa_error *);
bool q1_effect(qa_q1_game *, qa_builtin_event_kind, qa_actor_id, qa_vec3, float, int32_t,
               qa_error *);
bool q1_create(qa_q1_game *, qa_string_id, q1_entity_kind, qa_actor_id owner, q1_actor **,
               qa_error *);
bool q1_create_source(qa_q1_game *, qa_string_id, q1_entity_kind, uint32_t source_slot,
    q1_actor **, qa_error *);
const char *q1_body_queue_classname(const qa_q1_game *);
bool q1_body_queue_initialize(qa_q1_game *, qa_error *);
bool q1_body_queue_ring_valid(q1_ref head, const q1_ref nodes[4], const q1_ref links[4]);
bool q1_body_queue_validate(const qa_q1_game *, qa_error *);
bool q1_remove(qa_q1_game *, q1_actor *, qa_error *);
bool q1_schedule(qa_q1_game *, q1_actor *, double, q1_think_kind, qa_error *);
bool q1_current_ammo_select(qa_q1_game *, q1_player *, qa_error *);
bool q1_local_time(const q1_actor *, double *, qa_error *);
bool q1_think_deadline(double, double, double *, qa_error *);
bool q1_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_link(qa_q1_game *, q1_actor *, qa_error *);
bool q1_trace(qa_q1_game *, qa_vec3, qa_vec3, qa_actor_id, bool monsters, qa_trace_result *,
              qa_error *);
qa_attack q1_attack(qa_q1_game *, qa_actor_id attacker, qa_actor_id inflictor, qa_q1_weapon);
bool q1_damage(qa_q1_game *, qa_actor_id target, qa_actor_id inflictor, qa_actor_id attacker, float,
               qa_q1_weapon, qa_error *);
bool q1_damage_typed(qa_q1_game *, qa_actor_id, qa_actor_id, qa_actor_id, float, qa_q1_weapon,
                     qa_q1_armor_effect, qa_string_id death_type, qa_error *);
bool q1_radius(qa_q1_game *, qa_actor_id inflictor, qa_actor_id attacker, float, qa_actor_id ignore,
               qa_q1_weapon, qa_error *);
bool q1_radius_typed(qa_q1_game *, qa_actor_id, qa_actor_id, float, qa_actor_id, qa_q1_weapon,
                     qa_string_id, qa_error *);
bool q1_can_damage(qa_q1_game *, qa_actor_id target, qa_actor_id from, bool *, qa_error *);
double q1_ammo_count(qa_q1_game *, qa_actor_id, qa_q1_ammo);
int q1_weapon_declared_ammo(qa_q1_weapon);
bool q1_weapon_ui_available_read(qa_q1_game *, qa_actor_id, qa_q1_weapon, bool *, qa_error *);
bool q1_consume(qa_q1_game *, qa_actor_id, qa_q1_ammo, float, qa_error *);
bool q1_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_weapon_parameters(qa_q1_game *, qa_actor_id, qa_q1_weapon, qa_q1_weapon_parameters *,
                          qa_error *);
bool q1_weapon_attack_delay(qa_q1_game *, q1_player *, float *, qa_error *);
bool q1_aim(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3 *, qa_error *);
qa_string_id q1_weapon_model(const qa_q1_game *, const q1_player *);
bool q1_weapon_event(qa_q1_game *, q1_player *, float, int32_t, qa_error *);
bool q1_expansion_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_expansion_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *,
                        qa_error *);
bool q1_expansion_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_hipnotic_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_hipnotic_launch_laser(qa_q1_game *, qa_actor_id, qa_q1_weapon, qa_vec3, qa_vec3, bool light,
                              qa_error *);
bool q1_hipnotic_launch_proximity(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool q1_hipnotic_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *, qa_error *);
bool q1_hipnotic_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_proximity_arm(qa_q1_game *, q1_actor *, double delay, qa_error *);
bool q1_rogue_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_rogue_launch_plasma(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, q1_actor **, qa_error *);
bool q1_rogue_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_rogue_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_launch_behavior(qa_q1_game *, q1_actor *, qa_builtin_projectile_role, qa_error *);
bool q1_native_trajectory(qa_q1_game *, qa_actor_id);
bool q1_missile_velocity(qa_q1_game *, q1_actor *, qa_vec3, qa_error *);
qa_vec3 q1_grenade_launch_velocity(const qa_q1_weapon_view *,bool level,qa_vec3 aim,
    qa_vec3 forward,qa_vec3 right,qa_vec3 up,float side,float vertical);
bool q1_grenade_velocity(qa_q1_game *, q1_player *, qa_vec3 *, qa_error *);
bool q1_bullets(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, unsigned, float, float, qa_q1_weapon,
                qa_error *);
enum {
    Q1_LIGHTNING_DAMAGE_FIRST = 1u,
    Q1_LIGHTNING_REMEMBER_ALL = 2u,
    Q1_LIGHTNING_PARTICLES = 4u,
    Q1_LIGHTNING_WETSUIT = 8u
};
bool q1_lightning_rays(qa_q1_game *, qa_actor_id attacker, qa_actor_id inflictor, qa_vec3 start,
                       qa_vec3 end, float damage, float blood, int32_t color, qa_vec3 direction,
                       uint32_t flags, qa_q1_weapon, const char *cause, qa_error *);
bool q1_electric_rays(qa_q1_game *, qa_actor_id attacker, qa_actor_id inflictor,
                      qa_actor_id ignore, qa_vec3 start, qa_vec3 end, float damage, float blood,
                      int32_t color, qa_vec3 direction, uint32_t flags, qa_q1_weapon,
                      const char *cause, qa_error *);
bool q1_hipnotic_lightning_claimed(const qa_q1_game *, qa_actor_id);
bool q1_axe_strike(qa_q1_game *, q1_actor *, qa_error *);
bool q1_projectile_spawn(qa_q1_game *, qa_actor_id, qa_q1_weapon, q1_projectile_kind, qa_vec3,
                         qa_vec3, q1_actor **, qa_error *);
bool q1_projectile_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *,
                         qa_error *);
bool q1_projectile_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_explode(qa_q1_game *, q1_actor *, qa_actor_id direct, qa_error *);
bool q1_gib(qa_q1_game *, q1_actor *, const char *, bool head, qa_error *);
bool q1_gib_head(qa_q1_game *, q1_actor *, const char *, float damage, qa_error *);
bool q1_gib_at(qa_q1_game *, qa_actor_id owner, qa_vec3, float health, const char *, qa_error *);
bool q1_meat_spray(qa_q1_game *, q1_actor *, qa_vec3 origin, qa_vec3 velocity, qa_error *);
bool q1_monster_spawn(qa_q1_game *, q1_actor *, const q1_species *, qa_error *);
bool q1_monster_drop_floor(qa_q1_game *, q1_actor *, qa_error *);
bool q1_lavaman_awake(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_lavaman_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_lavaman_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_lavaman_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_teleport_fog_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_teledeath_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_spawn_teledeath(qa_q1_game *, qa_vec3, qa_actor_id, double duration, bool retouch,
                        qa_actor_id *, qa_error *);
bool q1_monster_start(qa_q1_game *, q1_actor *, qa_error *);
bool q1_monster_play(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_monster_frame(qa_q1_game *, q1_actor *, qa_error *);
bool q1_monster_ai(qa_q1_game *, q1_actor *, q1_ai, float, qa_error *);
bool q1_monster_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_mission_monster_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_mission_monster_pain(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_mission_monster_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_scourge_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_scourge_trigger(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_wrath_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_wrath_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_wrath_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_wrath_launch(qa_q1_game *, q1_actor *, unsigned attack, qa_error *);
bool q1_overlord_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_overlord_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_overlord_destination(qa_q1_game *, qa_actor_id *, qa_error *);
bool q1_spawnpoint_empty(qa_q1_game *, qa_actor_id, qa_vec3);
bool q1_morph_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_morph_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_morph_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_morph_pain(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_dragon_pain(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_dragon_corner_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_dragon_launch_fireball(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool q1_dragon_fireball_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_armagon_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_armagon_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_armagon_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_armagon_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_multi_explosion_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_radius_snapshot(qa_q1_game *, qa_vec3, float, qa_builtin_snapshot_frame **, qa_error *);
bool q1_snapshot_actors(qa_q1_game *, qa_builtin_snapshot_frame **, qa_error *);
bool q1_snapshot_players(qa_q1_game *, qa_builtin_snapshot_frame **, qa_error *);
bool q1_snapshot_targets(qa_q1_game *, qa_targets *, qa_string_id, qa_builtin_snapshot_frame **,
                         qa_error *);
bool q1_gremlin_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_gremlin_pain(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_gremlin_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_gremlin_touch(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_run(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_gremlin_walk(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_gremlin_find_victim(qa_q1_game *, q1_actor *, qa_actor_id *, qa_error *);
bool q1_gremlin_has_ammo(q1_actor *);
bool q1_gremlin_steal(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_gremlin_weapon_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_gremlin_fire_nail(qa_q1_game *, q1_actor *, bool laser, qa_error *);
bool q1_gremlin_lightning(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_backpack(qa_q1_game *, q1_actor *, qa_error *);
bool q1_weapon_impulse(qa_q1_game *, q1_player *, uint8_t, qa_error *);
bool q1_source_impulse(qa_q1_game *, qa_actor_id, uint8_t, bool *handled, qa_error *);
bool q1_source_select_weapon(qa_q1_game *, qa_actor_id, qa_q1_weapon, bool *, qa_error *);
bool q1_developer_message(qa_q1_game *, const char *, qa_error *);
bool q1_addon_omnicide(qa_q1_game *, qa_actor_id, qa_error *);
bool q1_monster_pain(qa_q1_game *, q1_actor *, qa_actor_id, float, qa_error *);
bool q1_monster_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_face(qa_q1_game *, q1_actor *, qa_error *);
bool q1_ctf_monster_removed(qa_bytes classname);
qa_actor_id q1_find_target(const qa_q1_game *, qa_string_id);
bool q1_monster_mission_turn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_monster_mission(const qa_q1_game *, qa_actor_id, qa_monster_mission *);
qa_actor_id q1_monster_route(const qa_q1_game *, const q1_actor *);
bool q1_monster_found(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_find_target(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_charmed_find_target(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_charmed_hunt(qa_q1_game *, q1_actor *, bool flee, qa_error *);
bool q1_charmed_walk(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_monster_visible(qa_q1_game *, q1_actor *, qa_actor_id, bool *, qa_error *);
bool q1_monster_melee(qa_q1_game *, q1_actor *, float range, float scale, unsigned rolls,
                      bool visible, qa_error *);
bool q1_pickup_define(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_supply_create(qa_q1_game *, qa_error *);
bool q1_pickup_observe(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_pickup_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_drop_backpack(qa_q1_game *, q1_actor *, qa_q1_weapon, const float ammo[QA_Q1_AMMO_COUNT],
                      qa_error *);
bool q1_spawn_backpack(qa_q1_game *, qa_actor_id, qa_vec3, qa_q1_weapon, const float ammo[QA_Q1_AMMO_COUNT],
                       q1_actor **, qa_error *);
bool q1_backpack_definition(qa_q1_game *, q1_actor *, qa_error *);
qa_q1_weapon q1_best_weapon(qa_q1_game *, q1_player *);
int q1_weapon_ammo(qa_q1_weapon);
float q1_weapon_interval(qa_q1_weapon);
const qa_q1_weapon_view *q1_weapon_shape(qa_q1_weapon);
bool q1_horde_axe_interval(qa_q1_game *, q1_player *, float *, bool *, qa_error *);
bool q1_character_bubbles(qa_q1_game *, q1_actor *, qa_error *);
bool q1_bubble_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_environment_damage(qa_q1_game *, qa_actor_id, float, qa_hazard, qa_error *);
bool q1_power_frame(qa_q1_game *, q1_player *, double seconds, uint64_t frame_ns, qa_error *);
bool q1_power_assign(qa_q1_game *, qa_actor_id, qa_q1_power, double, bool present, qa_error *);
bool q1_power_give(qa_q1_game *, qa_actor_id, qa_q1_power, double duration, qa_error *);
bool q1_powers_expire(qa_q1_game *, qa_actor_id, double seconds, qa_error *);
void q1_powers_forget(q1_player *);
bool q1_grapple_frame(qa_q1_game *, q1_player *, qa_error *);
bool q1_grapple_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_grapple_weapon_launch(qa_q1_game *, q1_actor *, qa_error *);
bool q1_grapple_weapon_frame(qa_q1_game *, q1_player *, qa_error *);
bool q1_grapple_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *, qa_error *);
void q1_grapple_released(qa_q1_game *, qa_actor_record);
bool q1_power_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_sphere_pickup(qa_q1_game *, q1_actor *, qa_actor_id, bool *, qa_error *);
bool q1_sphere_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_message(qa_q1_game *, qa_actor_id, const char *, qa_error *);
bool q1_spawn_bubble(qa_q1_game *, qa_vec3 origin, qa_vec3 velocity, bool split, qa_error *);
bool q1_message_args(qa_q1_game *, qa_actor_id, const char *, const qa_builtin_message_arg *,
                     size_t, qa_error *);
bool q1_toss_backpack(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, const float[QA_Q1_AMMO_COUNT],
                      q1_actor **, qa_error *);
void q1_drop_offer(const qa_q1_game *, const q1_actor *, qa_actor_id,
                     qa_pickup_offer *, qa_pickup_cargo cargo[4]);
bool q1_drop_eligible(qa_q1_game *, q1_actor *, qa_actor_id);
bool q1_drop_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_rogue_toss(qa_q1_game *, q1_player *, bool weapon, qa_error *);
int q1_weapon_rank(const qa_q1_game *, qa_q1_weapon);
bool q1_enable_combos(qa_q1_game *, q1_player *, qa_error *);
bool q1_enable_combos_read(qa_q1_game *, qa_actor_id, q1_player *, qa_error *);
qa_q1_weapon q1_combo_weapon(qa_q1_game *, q1_player *, qa_q1_weapon);
qa_q1_weapon q1_best_weapon_before(qa_q1_game *, q1_player *, const qa_pickup_receipt *, size_t);
bool q1_best_weapon_before_read(qa_q1_game *, qa_actor_id, q1_player *,
    const qa_pickup_receipt *, size_t, qa_q1_weapon *, qa_error *);
bool q1_player_select_read(qa_q1_game *, qa_actor_id, q1_player *, qa_q1_weapon, qa_error *);

#endif
