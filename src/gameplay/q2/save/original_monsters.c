#include "original_monsters.h"
#include "original_edicts.h"
#include "original_symbols.h"
#include "qa/game_q2_combat.h"
#include "../monsters/reinforcements.h"

#include <limits.h>

enum { Q2_MONSTER_CALLBACK_COUNT = 17 };
/* Exact g_spawn SP callback assignments from original/baseq2, xatrix,
 * rogue and rerelease m_*. No callback is inferred from a name substring. */
static const char *const monster_callbacks[][Q2_MONSTER_CALLBACK_COUNT] = {
    {"actor_stand", NULL, NULL, "actor_walk", "actor_run", NULL, "actor_attack", NULL, NULL, "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "actor_pain", "actor_die"},
    {"berserk_stand", NULL, "berserk_search", "berserk_walk", "berserk_run", NULL, NULL, "berserk_melee", "berserk_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "berserk_pain", "berserk_die"},
    {"boss2_stand", NULL, "boss2_search", "boss2_walk", "boss2_run", NULL, "boss2_attack", NULL, NULL, "Boss2_CheckAttack", NULL, NULL, NULL, NULL, NULL, "boss2_pain", "boss2_die"},
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {"brain_stand", "brain_idle", "brain_search", "brain_walk", "brain_run", "brain_dodge", NULL, "brain_melee", "brain_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "brain_pain", "brain_die"},
    {"chick_stand", NULL, NULL, "chick_walk", "chick_run", "chick_dodge", "chick_attack", "chick_melee", "chick_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "chick_pain", "chick_die"},
    {"flipper_stand", NULL, NULL, "flipper_walk", "flipper_start_run", NULL, NULL, "flipper_melee", "flipper_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "flipper_pain", "flipper_die"},
    {"floater_stand", "floater_idle", NULL, "floater_walk", "floater_run", NULL, "floater_attack", "floater_melee", "floater_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "floater_pain", "floater_die"},
    {"flyer_stand", "flyer_idle", NULL, "flyer_walk", "flyer_run", NULL, "flyer_attack", "flyer_melee", "flyer_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "flyer_pain", "flyer_die"},
    {"gladiator_stand", "gladiator_idle", "gladiator_search", "gladiator_walk", "gladiator_run", NULL, "gladiator_attack", "gladiator_melee", "gladiator_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "gladiator_pain", "gladiator_die"},
    {"gunner_stand", NULL, "gunner_search", "gunner_walk", "gunner_run", "gunner_dodge", "gunner_attack", NULL, "gunner_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "gunner_pain", "gunner_die"},
    {"hover_stand", NULL, "hover_search", "hover_walk", "hover_run", NULL, "hover_start_attack", NULL, "hover_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "hover_pain", "hover_die"},
    {"infantry_stand", "infantry_fidget", NULL, "infantry_walk", "infantry_run", "infantry_dodge", "infantry_attack", NULL, "infantry_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "infantry_pain", "infantry_die"},
    {"jorg_stand", NULL, "jorg_search", "jorg_walk", "jorg_run", NULL, "jorg_attack", NULL, NULL, "Jorg_CheckAttack", NULL, NULL, NULL, NULL, NULL, "jorg_pain", "jorg_die"},
    {"medic_stand", "medic_idle", "medic_search", "medic_walk", "medic_run", "medic_dodge", "medic_attack", NULL, "medic_sight", "medic_checkattack", NULL, NULL, NULL, NULL, NULL, "medic_pain", "medic_die"},
    {"mutant_stand", "mutant_idle", "mutant_search", "mutant_walk", "mutant_run", NULL, "mutant_jump", "mutant_melee", "mutant_sight", "mutant_checkattack", NULL, NULL, NULL, NULL, NULL, "mutant_pain", "mutant_die"},
    {"parasite_stand", "parasite_idle", NULL, "parasite_start_walk", "parasite_start_run", NULL, "parasite_attack", NULL, "parasite_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "parasite_pain", "parasite_die"},
    {"soldier_stand", NULL, NULL, "soldier_walk", "soldier_run", "soldier_dodge", "soldier_attack", NULL, "soldier_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "soldier_pain", "soldier_die"},
    {"supertank_stand", NULL, "supertank_search", "supertank_walk", "supertank_run", NULL, "supertank_attack", NULL, NULL, "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "supertank_pain", "supertank_die"},
    {"tank_stand", "tank_idle", NULL, "tank_walk", "tank_run", NULL, "tank_attack", NULL, "tank_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "tank_pain", "tank_die"},
    {"infantry_stand", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "turret_driver_die"},
    {"boss5_stand", NULL, "boss5_search", "boss5_walk", "boss5_run", NULL, "boss5_attack", NULL, NULL, "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "boss5_pain", "boss5_die"},
    {"brain_stand", "brain_idle", "brain_search", "brain_walk", "brain_run", "brain_dodge", "brain_attack", "brain_melee", "brain_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "brain_pain", "brain_die"},
    {"fixbot_stand", NULL, NULL, "fixbot_walk", "fixbot_run", NULL, "fixbot_attack", NULL, NULL, "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "fixbot_pain", "fixbot_die"},
    {"gekk_stand", "gekk_idle", "gekk_search", "gekk_walk", "gekk_run_start", "gekk_dodge", "gekk_jump", "gekk_melee", "gekk_sight", "gekk_checkattack", NULL, NULL, NULL, NULL, NULL, "gekk_pain", "gekk_die"},
    {"gladb_stand", "gladb_idle", "gladb_search", "gladb_walk", "gladb_run", NULL, "gladb_attack", "gladb_melee", "gladb_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "gladb_pain", "gladb_die"},
    {"soldierh_stand", NULL, NULL, "soldierh_walk", "soldierh_run", "soldierh_dodge", "soldierh_attack", NULL, "soldierh_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "soldierh_pain", "soldierh_die"},
    {"berserk_stand", NULL, "berserk_search", "berserk_walk", "berserk_run", "M_MonsterDodge", NULL, "berserk_melee", "berserk_sight", "M_CheckAttack", NULL, "berserk_blocked", NULL, NULL, "berserk_sidestep", "berserk_pain", "berserk_die"},
    {"brain_stand", "brain_idle", "brain_search", "brain_walk", "brain_run", "M_MonsterDodge", NULL, "brain_melee", "brain_sight", "M_CheckAttack", NULL, NULL, "brain_duck", "monster_duck_up", NULL, "brain_pain", "brain_die"},
    {"carrier_stand", NULL, NULL, "carrier_walk", "carrier_run", NULL, "carrier_attack", NULL, "carrier_sight", "Carrier_CheckAttack", NULL, NULL, NULL, NULL, NULL, "carrier_pain", "carrier_die"},
    {"chick_stand", NULL, NULL, "chick_walk", "chick_run", "M_MonsterDodge", "chick_attack", "chick_melee", "chick_sight", "M_CheckAttack", NULL, "chick_blocked", "chick_duck", "monster_duck_up", "chick_sidestep", "chick_pain", "chick_die"},
    {"hover_stand", NULL, "hover_search", "hover_walk", "hover_run", NULL, "hover_start_attack", NULL, "hover_sight", "M_CheckAttack", NULL, "hover_blocked", NULL, NULL, NULL, "hover_pain", "hover_die"},
    {"floater_stand", "floater_idle", NULL, "floater_walk", "floater_run", NULL, "floater_attack", "floater_melee", "floater_sight", "M_CheckAttack", NULL, "floater_blocked", NULL, NULL, NULL, "floater_pain", "floater_die"},
    {"flyer_stand", "flyer_idle", NULL, "flyer_walk", "flyer_run", NULL, "flyer_attack", "flyer_melee", "flyer_sight", "M_CheckAttack", NULL, "flyer_blocked", NULL, NULL, NULL, "flyer_pain", "flyer_die"},
    {"gladiator_stand", "gladiator_idle", "gladiator_search", "gladiator_walk", "gladiator_run", NULL, "gladiator_attack", "gladiator_melee", "gladiator_sight", "M_CheckAttack", NULL, "gladiator_blocked", NULL, NULL, NULL, "gladiator_pain", "gladiator_die"},
    {"gunner_stand", NULL, "gunner_search", "gunner_walk", "gunner_run", "M_MonsterDodge", "gunner_attack", NULL, "gunner_sight", "M_CheckAttack", NULL, "gunner_blocked", "gunner_duck", "monster_duck_up", "gunner_sidestep", "gunner_pain", "gunner_die"},
    {"infantry_stand", "infantry_fidget", NULL, "infantry_walk", "infantry_run", "M_MonsterDodge", "infantry_attack", NULL, "infantry_sight", "M_CheckAttack", NULL, "infantry_blocked", "infantry_duck", "monster_duck_up", "infantry_sidestep", "infantry_pain", "infantry_die"},
    {"medic_stand", "medic_idle", "medic_search", "medic_walk", "medic_run", "M_MonsterDodge", "medic_attack", NULL, "medic_sight", "medic_checkattack", NULL, "medic_blocked", "medic_duck", "monster_duck_up", "medic_sidestep", "medic_pain", "medic_die"},
    {"mutant_stand", "mutant_idle", "mutant_search", "mutant_walk", "mutant_run", NULL, "mutant_jump", "mutant_melee", "mutant_sight", "mutant_checkattack", NULL, "mutant_blocked", NULL, NULL, NULL, "mutant_pain", "mutant_die"},
    {"parasite_stand", "parasite_idle", NULL, "parasite_start_walk", "parasite_start_run", NULL, "parasite_attack", NULL, "parasite_sight", "parasite_checkattack", NULL, "parasite_blocked", NULL, NULL, NULL, "parasite_pain", "parasite_die"},
    {"soldier_stand", NULL, NULL, "soldier_walk", "soldier_run", "M_MonsterDodge", "soldier_attack", NULL, "soldier_sight", "M_CheckAttack", NULL, "soldier_blocked", "soldier_duck", "monster_duck_up", "soldier_sidestep", "soldier_pain", "soldier_die"},
    {"stalker_stand", "stalker_idle", NULL, "stalker_walk", "stalker_run", "stalker_dodge", "stalker_attack_ranged", "stalker_attack_melee", "stalker_sight", "M_CheckAttack", NULL, "stalker_blocked", NULL, NULL, NULL, "stalker_pain", "stalker_die"},
    {"supertank_stand", NULL, "supertank_search", "supertank_walk", "supertank_run", NULL, "supertank_attack", NULL, NULL, "M_CheckAttack", NULL, "supertank_blocked", NULL, NULL, NULL, "supertank_pain", "supertank_die"},
    {"tank_stand", "tank_idle", NULL, "tank_walk", "tank_run", NULL, "tank_attack", NULL, "tank_sight", "M_CheckAttack", NULL, "tank_blocked", NULL, NULL, NULL, "tank_pain", "tank_die"},
    {"turret_stand", NULL, "turret_search", "turret_walk", "turret_run", NULL, "turret_attack", NULL, "turret_sight", "turret_checkattack", NULL, NULL, NULL, NULL, NULL, "turret_pain", "turret_die"},
    {"widow_stand", NULL, "widow_search", "widow_walk", "widow_run", NULL, "widow_attack", "widow_melee", "widow_sight", "Widow_CheckAttack", NULL, "widow_blocked", NULL, NULL, NULL, "widow_pain", "widow_die"},
    {"widow2_stand", NULL, "widow2_search", "widow2_walk", "widow2_run", NULL, "widow2_attack", "widow2_melee", NULL, "Widow2_CheckAttack", NULL, NULL, NULL, NULL, NULL, "widow2_pain", "widow2_die"},
    {"actor_stand", NULL, NULL, "actor_walk", "actor_run", NULL, "actor_attack", NULL, NULL, "M_CheckAttack", "actor_setskin", NULL, NULL, NULL, NULL, "actor_pain", "actor_die"},
    {"arachnid_stand", NULL, NULL, "arachnid_walk", "arachnid_run", NULL, "arachnid_attack", NULL, "arachnid_sight", "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "arachnid_pain", "arachnid_die"},
    {"berserk_stand", NULL, "berserk_search", "berserk_walk", "berserk_run", "M_MonsterDodge", "berserk_attack", "berserk_melee", "berserk_sight", "M_CheckAttack", "berserk_setskin", "berserk_blocked", "berserk_duck", "monster_duck_up", "berserk_sidestep", "berserk_pain", "berserk_die"},
    {"boss2_stand", NULL, "boss2_search", "boss2_walk", "boss2_run", NULL, "boss2_attack", NULL, NULL, "Boss2_CheckAttack", "boss2_setskin", NULL, NULL, NULL, NULL, "boss2_pain", "boss2_die"},
    {"supertank_stand", NULL, "supertank_search", "supertank_walk", "supertank_run", NULL, "supertank_attack", NULL, NULL, "M_CheckAttack", "supertank_setskin", "supertank_blocked", NULL, NULL, NULL, "supertank_pain", "supertank_die"},
    {"brain_stand", "brain_idle", "brain_search", "brain_walk", "brain_run", "M_MonsterDodge", "brain_attack", "brain_melee", "brain_sight", "M_CheckAttack", "brain_setskin", NULL, "brain_duck", "monster_duck_up", NULL, "brain_pain", "brain_die"},
    {"carrier_stand", NULL, NULL, "carrier_walk", "carrier_run", NULL, "carrier_attack", NULL, "carrier_sight", "Carrier_CheckAttack", "carrier_setskin", NULL, NULL, NULL, NULL, "carrier_pain", "carrier_die"},
    {"chick_stand", NULL, NULL, "chick_walk", "chick_run", "M_MonsterDodge", "chick_attack", "chick_melee", "chick_sight", "M_CheckAttack", "chick_setpain", "chick_blocked", "chick_duck", "monster_duck_up", "chick_sidestep", "chick_pain", "chick_die"},
    {"hover_stand", NULL, "hover_search", "hover_walk", "hover_run", NULL, "hover_start_attack", NULL, "hover_sight", "M_CheckAttack", "hover_setskin", NULL, NULL, NULL, NULL, "hover_pain", "hover_die"},
    {"flipper_stand", NULL, NULL, "flipper_walk", "flipper_start_run", NULL, NULL, "flipper_melee", "flipper_sight", "M_CheckAttack", "flipper_setskin", NULL, NULL, NULL, NULL, "flipper_pain", "flipper_die"},
    {"floater_stand", "floater_idle", NULL, "floater_walk", "floater_run", NULL, "floater_attack", "floater_melee", "floater_sight", "M_CheckAttack", "floater_setskin", NULL, NULL, NULL, NULL, "floater_pain", "floater_die"},
    {"flyer_stand", "flyer_idle", NULL, "flyer_walk", "flyer_run", NULL, "flyer_attack", "flyer_melee", "flyer_sight", "M_CheckAttack", "flyer_setskin", "flyer_blocked", NULL, NULL, NULL, "flyer_pain", "flyer_die"},
    {"gekk_stand", "gekk_idle", "gekk_search", "gekk_walk", "gekk_run_start", "gekk_dodge", "gekk_attack", "gekk_melee", "gekk_sight", "gekk_checkattack", "gekk_setskin", "gekk_blocked", NULL, NULL, NULL, "gekk_pain", "gekk_die"},
    {"gladiator_stand", "gladiator_idle", "gladiator_search", "gladiator_walk", "gladiator_run", NULL, "gladiator_attack", "gladiator_melee", "gladiator_sight", "M_CheckAttack", "gladiator_setskin", "gladiator_blocked", NULL, NULL, NULL, "gladiator_pain", "gladiator_die"},
    {"guardian_stand", NULL, NULL, "guardian_walk", "guardian_run", NULL, "guardian_attack", NULL, NULL, "M_CheckAttack", NULL, NULL, NULL, NULL, NULL, "guardian_pain", "guardian_die"},
    {"guncmdr_stand", NULL, "guncmdr_search", "guncmdr_walk", "guncmdr_run", "M_MonsterDodge", "guncmdr_attack", NULL, "guncmdr_sight", "M_CheckAttack", "guncmdr_setskin", "guncmdr_blocked", "guncmdr_duck", "monster_duck_up", "guncmdr_sidestep", "guncmdr_pain", "guncmdr_die"},
    {"gunner_stand", NULL, "gunner_search", "gunner_walk", "gunner_run", "M_MonsterDodge", "gunner_attack", NULL, "gunner_sight", "M_CheckAttack", "gunner_setskin", "gunner_blocked", "gunner_duck", "monster_duck_up", "gunner_sidestep", "gunner_pain", "gunner_die"},
    {"infantry_stand", "infantry_fidget", NULL, "infantry_walk", "infantry_run", "M_MonsterDodge", "infantry_attack", NULL, "infantry_sight", "M_CheckAttack", "infantry_setskin", "infantry_blocked", "infantry_duck", "monster_duck_up", "infantry_sidestep", "infantry_pain", "infantry_die"},
    {"jorg_stand", NULL, "jorg_search", "jorg_walk", "jorg_run", NULL, "jorg_attack", NULL, NULL, "Jorg_CheckAttack", "jorg_setskin", NULL, NULL, NULL, NULL, "jorg_pain", "jorg_die"},
    {"makron_stand", NULL, NULL, "makron_walk", "makron_run", NULL, "makron_attack", NULL, "makron_sight", "Makron_CheckAttack", "makron_setskin", NULL, NULL, NULL, NULL, "makron_pain", "makron_die"},
    {"medic_stand", "medic_idle", "medic_search", "medic_walk", "medic_run", "M_MonsterDodge", "medic_attack", NULL, "medic_sight", "medic_checkattack", "medic_setskin", "medic_blocked", "medic_duck", "monster_duck_up", "medic_sidestep", "medic_pain", "medic_die"},
    {"mutant_stand", "mutant_idle", "mutant_search", "mutant_walk", "mutant_run", NULL, "mutant_jump", "mutant_melee", "mutant_sight", "mutant_checkattack", "mutant_setskin", "mutant_blocked", NULL, NULL, NULL, "mutant_pain", "mutant_die"},
    {"parasite_stand", "parasite_idle", NULL, "parasite_start_walk", "parasite_start_run", NULL, "parasite_attack", NULL, "parasite_sight", "M_CheckAttack", "parasite_setskin", "parasite_blocked", NULL, NULL, NULL, "parasite_pain", "parasite_die"},
    {"shambler_stand", "shambler_idle", NULL, "shambler_walk", "shambler_run", NULL, "shambler_attack", "shambler_melee", "shambler_sight", "M_CheckAttack", "shambler_setskin", NULL, NULL, NULL, NULL, "shambler_pain", "shambler_die"},
    {"soldier_stand", NULL, NULL, "soldier_walk", "soldier_run", "M_MonsterDodge", "soldier_attack", NULL, "soldier_sight", "M_CheckAttack", "soldier_setskin", "soldier_blocked", "soldier_duck", "monster_duck_up", "soldier_sidestep", "soldier_pain", "soldier_die"},
    {"stalker_stand", "stalker_idle", NULL, "stalker_walk", "stalker_run", "stalker_dodge", "stalker_attack_ranged", "stalker_attack_melee", "stalker_sight", "M_CheckAttack", "stalker_setskin", "stalker_blocked", NULL, NULL, NULL, "stalker_pain", "stalker_die"},
    {"tank_stand", "tank_idle", NULL, "tank_walk", "tank_run", NULL, "tank_attack", NULL, "tank_sight", "M_CheckAttack", "tank_setskin", "tank_blocked", NULL, NULL, NULL, "tank_pain", "tank_die"},
    {"widow_stand", NULL, "widow_search", "widow_walk", "widow_run", NULL, "widow_attack", "widow_melee", "widow_sight", "Widow_CheckAttack", "widow_setskin", "widow_blocked", NULL, NULL, NULL, "widow_pain", "widow_die"},
    {"widow2_stand", NULL, "widow2_search", "widow2_walk", "widow2_run", NULL, "widow2_attack", "widow2_melee", NULL, "Widow2_CheckAttack", "widow2_setskin", NULL, NULL, NULL, NULL, "widow2_pain", "widow2_die"},
    {"infantry_stand", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "infantry_setskin", NULL, NULL, NULL, NULL, "infantry_pain", "turret_driver_die"},
};

typedef struct monster_source_binding {
    const char *classname;
    uint8_t source, callbacks;
} monster_source_binding;
static const monster_source_binding monster_bindings[] = {
    {"misc_actor", 0, 0},
    {"monster_berserk", 0, 1},
    {"monster_boss2", 0, 2},
    {"monster_boss3_stand", 0, 3},
    {"monster_brain", 0, 4},
    {"monster_chick", 0, 5},
    {"monster_flipper", 0, 6},
    {"monster_floater", 0, 7},
    {"monster_flyer", 0, 8},
    {"monster_gladiator", 0, 9},
    {"monster_gunner", 0, 10},
    {"monster_hover", 0, 11},
    {"monster_infantry", 0, 12},
    {"monster_jorg", 0, 13},
    {"monster_medic", 0, 14},
    {"monster_mutant", 0, 15},
    {"monster_parasite", 0, 16},
    {"monster_soldier", 0, 17},
    {"monster_soldier_light", 0, 17},
    {"monster_soldier_ss", 0, 17},
    {"monster_supertank", 0, 18},
    {"monster_tank", 0, 19},
    {"monster_tank_commander", 0, 19},
    {"turret_driver", 0, 20},
    {"misc_actor", 1, 0},
    {"monster_berserk", 1, 1},
    {"monster_boss2", 1, 2},
    {"monster_boss3_stand", 1, 3},
    {"monster_boss5", 1, 21},
    {"monster_brain", 1, 22},
    {"monster_chick", 1, 5},
    {"monster_chick_heat", 1, 5},
    {"monster_fixbot", 1, 23},
    {"monster_flipper", 1, 6},
    {"monster_floater", 1, 7},
    {"monster_flyer", 1, 8},
    {"monster_gekk", 1, 24},
    {"monster_gladb", 1, 25},
    {"monster_gladiator", 1, 9},
    {"monster_gunner", 1, 10},
    {"monster_hover", 1, 11},
    {"monster_infantry", 1, 12},
    {"monster_jorg", 1, 13},
    {"monster_medic", 1, 14},
    {"monster_mutant", 1, 15},
    {"monster_parasite", 1, 16},
    {"monster_soldier", 1, 17},
    {"monster_soldier_hypergun", 1, 26},
    {"monster_soldier_lasergun", 1, 26},
    {"monster_soldier_light", 1, 17},
    {"monster_soldier_ripper", 1, 26},
    {"monster_soldier_ss", 1, 17},
    {"monster_supertank", 1, 18},
    {"monster_tank", 1, 19},
    {"monster_tank_commander", 1, 19},
    {"turret_driver", 1, 20},
    {"misc_actor", 2, 0},
    {"monster_berserk", 2, 27},
    {"monster_boss2", 2, 2},
    {"monster_boss3_stand", 2, 3},
    {"monster_brain", 2, 28},
    {"monster_carrier", 2, 29},
    {"monster_chick", 2, 30},
    {"monster_daedalus", 2, 31},
    {"monster_flipper", 2, 6},
    {"monster_floater", 2, 32},
    {"monster_flyer", 2, 33},
    {"monster_gladiator", 2, 34},
    {"monster_gunner", 2, 35},
    {"monster_hover", 2, 31},
    {"monster_infantry", 2, 36},
    {"monster_jorg", 2, 13},
    {"monster_kamikaze", 2, 33},
    {"monster_medic", 2, 37},
    {"monster_medic_commander", 2, 37},
    {"monster_mutant", 2, 38},
    {"monster_parasite", 2, 39},
    {"monster_soldier", 2, 40},
    {"monster_soldier_light", 2, 40},
    {"monster_soldier_ss", 2, 40},
    {"monster_stalker", 2, 41},
    {"monster_supertank", 2, 42},
    {"monster_tank", 2, 43},
    {"monster_tank_commander", 2, 43},
    {"monster_turret", 2, 44},
    {"monster_widow", 2, 45},
    {"monster_widow2", 2, 46},
    {"turret_driver", 2, 20},
    {"misc_actor", 3, 47},
    {"monster_arachnid", 3, 48},
    {"monster_berserk", 3, 49},
    {"monster_boss2", 3, 50},
    {"monster_boss3_stand", 3, 3},
    {"monster_boss5", 3, 51},
    {"monster_brain", 3, 52},
    {"monster_carrier", 3, 53},
    {"monster_chick", 3, 54},
    {"monster_chick_heat", 3, 54},
    {"monster_daedalus", 3, 55},
    {"monster_fixbot", 3, 23},
    {"monster_flipper", 3, 56},
    {"monster_floater", 3, 57},
    {"monster_flyer", 3, 58},
    {"monster_gekk", 3, 59},
    {"monster_gladb", 3, 60},
    {"monster_gladiator", 3, 60},
    {"monster_guardian", 3, 61},
    {"monster_guncmdr", 3, 62},
    {"monster_gunner", 3, 63},
    {"monster_hover", 3, 55},
    {"monster_infantry", 3, 64},
    {"monster_jorg", 3, 65},
    {"monster_kamikaze", 3, 58},
    {"monster_makron", 3, 66},
    {"monster_medic", 3, 67},
    {"monster_medic_commander", 3, 67},
    {"monster_mutant", 3, 68},
    {"monster_parasite", 3, 69},
    {"monster_shambler", 3, 70},
    {"monster_soldier", 3, 71},
    {"monster_soldier_hypergun", 3, 71},
    {"monster_soldier_lasergun", 3, 71},
    {"monster_soldier_light", 3, 71},
    {"monster_soldier_ripper", 3, 71},
    {"monster_soldier_ss", 3, 71},
    {"monster_stalker", 3, 72},
    {"monster_supertank", 3, 51},
    {"monster_tank", 3, 73},
    {"monster_tank_commander", 3, 73},
    {"monster_tank_stand", 3, 3},
    {"monster_turret", 3, 44},
    {"monster_widow", 3, 74},
    {"monster_widow2", 3, 75},
    {"turret_driver", 3, 76},
};

static bool monster_error(q2_original_record_io *io, const char *member,
    const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, 0, "%s: %s", member, message);
    return false;
}

static const char *const *callbacks_for(const qa_q2_game *game, const char *classname)
{
    unsigned source = game->options.edition == QA_Q2_RERELEASE ? 3u :
        (unsigned)game->options.product;
    for (size_t i = 0; i < sizeof(monster_bindings) / sizeof(*monster_bindings); ++i)
        if (monster_bindings[i].source == source &&
            !strcmp(monster_bindings[i].classname, classname))
            return monster_callbacks[monster_bindings[i].callbacks];
    return NULL;
}

/* A folded retail function can have several Source names. Compare the exact
 * symbol's encoded address, rather than choosing the first reverse alias. */
static bool callback(qa_q2_game *game, q2_original_record_io *io,
    const char *field, uint16_t offset, const char *name, bool data_relative)
{
    if (io->edition == QA_Q2_RERELEASE || !data_relative) {
        if (!io->reading) return q2_original_function(game, io, field, offset, name);
        bool matches = false;
        return q2_original_function_matches(game, io, field, offset, name, &matches) &&
            (matches || monster_error(io, field, "Callback differs from its exact native Source owner"));
    }
    /* Rogue's added blocked/duck/unduck/sidestep fields are F_MMOVE in
     * g_save.c, despite pointing at functions: relocate from data, not InitGame. */
    int32_t value = 0;
    if (name && !q2_original_symbol_encode(Q2_ORIGINAL_FUNCTION, io->product,
        name, &value, io->error)) return false;
    if (value) {
        const q2_original_library *lib = q2_original_library_for(io->product);
        int64_t relocated = (int64_t)value + lib->init_game - lib->mmove;
        if (relocated < INT32_MIN || relocated > INT32_MAX)
            return monster_error(io, field, "Callback exceeds the original data-relative storage");
        value = (int32_t)relocated;
    }
    int32_t actual = value;
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &actual))
        return false;
    return !io->reading || actual == value ||
        monster_error(io, field, "Callback differs from its exact native Source owner");
}

static bool move_record(q2_original_record_io *io, const char *field, uint16_t offset,
    struct qa_q2_monster *m, const q2m_move **move)
{
    if (io->edition == QA_Q2_RERELEASE) {
        if (!io->reading) {
            if (*move) {
                qa_json_writer_key(io->writer, field);
                qa_json_writer_string(io->writer, q2m_move_name(*move));
            }
            return !io->writer->failed;
        }
        qa_json_id id = qa_json_get(io->document, io->object, field);
        if (id == QA_JSON_NONE || qa_json_type(io->document, id) == QA_JSON_NULL) {
            *move = NULL;
            return true;
        }
        qa_buffer text = {0};
        bool okay = qa_json_string(io->document, id, &text, io->error);
        if (okay && memchr(text.data, 0, text.size))
            okay = monster_error(io, field, "Move name contains a NUL");
        if (okay) {
            *move = q2m_move_named(m, (const char *)text.data);
            if (!*move) okay = monster_error(io, field, "Move is absent from its exact Source move table");
        }
        qa_buffer_free(&text);
        return okay;
    }
    int32_t value = 0;
    if (!io->reading && *move && !q2_original_symbol_encode(Q2_ORIGINAL_MOVE,
        io->product, q2m_move_name(*move), &value, io->error)) return false;
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &value))
        return false;
    if (!io->reading) return true;
    *move = NULL;
    if (!value) return true;
    const char *name = NULL;
    while ((name = q2_original_symbol_next(Q2_ORIGINAL_MOVE, io->product, value, name))) {
        const q2m_move *candidate = q2m_move_named(m, name);
        if (candidate) { *move = candidate; return true; }
    }
    return monster_error(io, field, "Move address has no exact Source move in this monster");
}

static bool integer_float(q2_original_record_io *io, const char *field,
    uint16_t offset, float *native)
{
    int32_t source = 0;
    if (!io->reading) {
        if (!isfinite(*native) || truncf(*native) != *native ||
            (double)*native < INT32_MIN || (double)*native > INT32_MAX)
            return monster_error(io, field, "Value exceeds its original integer field");
        source = (int32_t)*native;
    }
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &source))
        return false;
    if (io->reading) *native = (float)source;
    return true;
}

static bool integer_double(q2_original_record_io *io, const char *field,
    uint16_t offset, double *native)
{
    if (!io->reading && (!isfinite(*native) || trunc(*native) != *native ||
        *native < INT32_MIN || *native > INT32_MAX))
        return monster_error(io, field, "Value exceeds its original integer field");
    int32_t value = io->reading ? 0 : (int32_t)*native;
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &value)) return false;
    if (io->reading) *native = value;
    return true;
}

static bool hostile_record(q2_original_record_io *io, uint64_t *native)
{
    if (io->edition == QA_Q2_RERELEASE)
        return q2_original_scalar(io, "show_hostile", Q2_ORIGINAL_TIME, 496, 496, 496, native);
    /* Classic show_hostile is qboolean storage, but its users store/compare
     * an integer level.time deadline. It is not a float or a true/false flag. */
    if (!io->reading && *native / Q2_NS > INT32_MAX)
        return monster_error(io, "show_hostile", "Hostility deadline exceeds Source integer seconds");
    int32_t seconds = io->reading ? 0 : (int32_t)(*native / Q2_NS);
    if (!q2_original_scalar(io, "show_hostile", Q2_ORIGINAL_I32, 496, 496, 496, &seconds)) return false;
    if (seconds < 0) return monster_error(io, "show_hostile", "Negative original hostility deadline");
    if (io->reading) *native = (uint64_t)(uint32_t)seconds * Q2_NS;
    return true;
}

static bool integer_wide(q2_original_record_io *io, const char *field,
    uint16_t offset, int64_t *native)
{
    if (!io->reading && (*native < INT32_MIN || *native > INT32_MAX))
        return monster_error(io, field, "Value exceeds its original integer field");
    int32_t value = io->reading ? 0 : (int32_t)*native;
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &value))
        return false;
    if (io->reading) *native = value;
    return true;
}

static bool power_kind(q2_original_record_io *io, const char *field,
    uint16_t offset, qa_power_kind *kind)
{
    if (io->edition == QA_Q2_RERELEASE) {
        static const char *const names[] = {NULL, "item_power_screen", "item_power_shield"};
        if (!io->reading) {
            if ((unsigned)*kind >= sizeof(names) / sizeof(*names))
                return monster_error(io, field, "Unknown native power armor");
            if (*kind) {
                qa_json_writer_key(io->writer, field);
                qa_json_writer_string(io->writer, names[*kind]);
            }
            return !io->writer->failed;
        }
        qa_json_id id = qa_json_get(io->document, io->object, field);
        *kind = QA_POWER_NONE;
        if (id == QA_JSON_NONE || qa_json_type(io->document, id) == QA_JSON_NULL) return true;
        qa_buffer text = {0};
        bool okay = qa_json_string(io->document, id, &text, io->error);
        if (okay) {
            for (size_t i = 1; i < sizeof(names) / sizeof(*names); ++i)
                if (text.size == strlen(names[i]) && !memcmp(text.data, names[i], text.size))
                    *kind = (qa_power_kind)i;
            if (!*kind) okay = monster_error(io, field, "Unknown Source power armor item");
        }
        qa_buffer_free(&text);
        return okay;
    }
    int32_t value = (int32_t)*kind;
    if (!q2_original_scalar(io, field, Q2_ORIGINAL_I32, offset, offset, offset, &value)) return false;
    if (value < 0 || value > 2) return monster_error(io, field, "Unknown Source power armor kind");
    if (io->reading) *kind = (qa_power_kind)value;
    return true;
}

/* Classic Rogue inserted AI_WALK_WALLS at bit 15. The rerelease removed that
 * bit and added its flying flags above bit 32. Native state stores booleans. */
typedef struct monster_flag_field { size_t offset; uint8_t bit; } monster_flag_field;
#define FLAG(MEMBER, BIT) {offsetof(struct qa_q2_monster, MEMBER), BIT}
static const monster_flag_field monster_flags[] = {
    FLAG(stand_ground, 0), FLAG(temporary_stand_ground, 1), FLAG(sound_target.present, 2),
    FLAG(lost_sight, 3), FLAG(pursuit_last_seen, 4), FLAG(pursue_next, 5),
    FLAG(pursue_temporary, 6), FLAG(hold_frame, 7), FLAG(good_guy, 8),
    FLAG(brutal, 9), FLAG(ducked, 11), FLAG(combat_point, 12), FLAG(medic, 13),
    FLAG(resurrecting, 14), FLAG(manual_steering, 15), FLAG(target_anger, 16),
    FLAG(dodging, 17), FLAG(charging, 18), FLAG(hint_path, 19), FLAG(ignore_shots, 20),
    FLAG(do_not_count, 21), FLAG(source_blocked, 25), FLAG(high_tick_rate, 28), FLAG(alternate_fly, 33)
};
#undef FLAG

static bool flags_record(q2_original_record_io *io, q2_actor *actor,
    struct qa_q2_monster *m)
{
    bool rr = io->edition == QA_Q2_RERELEASE;
    bool rogue = io->product == QA_Q2_ROGUE;
    uint64_t bits = 0;
    for (size_t i = 0; i < sizeof(monster_flags) / sizeof(*monster_flags); ++i) {
        unsigned bit = monster_flags[i].bit;
        if (!rr && bit >= 15) { if (!rogue || bit > 25) continue; ++bit; }
        bool *value = (bool *)((uint8_t *)m + monster_flags[i].offset);
        if (!io->reading && *value) bits |= UINT64_C(1) << bit;
    }
    if (!io->reading) {
        if (!rr && rogue && m->definition->species == Q2M_STALKER) bits |= UINT64_C(1) << 15;
        unsigned spawn = rr ? 22u : 23u;
        if (m->spawned_by != Q2M_SPAWN_NONE)
            bits |= UINT64_C(1) << (spawn + (unsigned)m->spawned_by - 1);
        if (rr) {
            if (m->definition->species == Q2M_INFANTRY || m->definition->species == Q2M_MUTANT)
                bits |= UINT64_C(1) << 31;
            if (m->flies_ns == UINT64_MAX) bits |= UINT64_C(1) << 32;
            if (m->definition->species == Q2M_JORG) bits |= UINT64_C(1) << 36;
        }
    }
    if (rr) {
        if (!q2_original_scalar(io, "monsterinfo.aiflags", Q2_ORIGINAL_U64,
            65535, 65535, 65535, &bits)) return false;
    } else {
        uint32_t value = (uint32_t)bits;
        if (!q2_original_scalar(io, "monsterinfo.aiflags", Q2_ORIGINAL_U32, 776, 776, 776, &value))
            return false;
        bits = value;
    }
    if (!io->reading) return true;
    for (size_t i = 0; i < sizeof(monster_flags) / sizeof(*monster_flags); ++i) {
        unsigned bit = monster_flags[i].bit;
        if (!rr && bit >= 15) { if (!rogue || bit > 25) continue; ++bit; }
        *(bool *)((uint8_t *)m + monster_flags[i].offset) = (bits & (UINT64_C(1) << bit)) != 0;
    }
    if (bits & (UINT64_C(1) << 10))
        return monster_error(io, "monsterinfo.aiflags", "AI_NOSTEP has no native movement state");
    const uint64_t unavailable = (UINT64_C(3) << 29) | (UINT64_C(3) << 34) |
        (UINT64_C(3) << 37);
    if (rr && (bits & unavailable)) {
        qa_error_set(io->error, QA_ERROR_UNSUPPORTED, 0,
            "Original Q2 rerelease navigation or combat AI state has no native owner");
        return false;
    }
    if (rr && (bits & (UINT64_C(1) << 32))) m->flies_ns = UINT64_MAX;
    (void)actor;
    unsigned spawn = rr ? 22u : 23u;
    m->spawned_by = bits & (UINT64_C(1) << spawn) ? Q2M_SPAWN_CARRIER :
        bits & (UINT64_C(1) << (spawn + 1)) ? Q2M_SPAWN_MEDIC :
        bits & (UINT64_C(1) << (spawn + 2)) ? Q2M_SPAWN_WIDOW : Q2M_SPAWN_NONE;
    m->summoned = m->spawned_by != Q2M_SPAWN_NONE;
    m->has_saved_goal = m->pursue_temporary;
    return true;
}

#define FIELD(NAME, MEMBER, KIND, BASE, ROGUE) \
    {NAME, offsetof(struct qa_q2_monster, MEMBER), 1, KIND, {BASE, BASE, ROGUE}}
static const q2_original_field monster_fields[] = {
    FIELD("spawnflags", spawnflags, Q2_ORIGINAL_U32, 284, 284),
    FIELD("s.frame", frame, Q2_ORIGINAL_I32, 56, 56),
    FIELD("s.skinnum", skin, Q2_ORIGINAL_I32, 60, 60),
    FIELD("s.renderfx", render_flags, Q2_ORIGINAL_U32, 68, 68),
    FIELD("style", style, Q2_ORIGINAL_I32, 644, 644),
    FIELD("count", count, Q2_ORIGINAL_I32, 532, 532),
    FIELD("timestamp", timestamp_ns, Q2_ORIGINAL_TIME, 288, 288),
    FIELD("fly_sound_debounce_time", flies_ns, Q2_ORIGINAL_TIME, 476, 476),
    FIELD("air_finished", air_ns, Q2_ORIGINAL_TIME, 404, 404),
    FIELD("pain_debounce_time", pain_ns, Q2_ORIGINAL_TIME, 464, 464),
    FIELD("damage_debounce_time", environment_ns, Q2_ORIGINAL_TIME, 468, 468),
    FIELD("ideal_yaw", ideal_yaw, Q2_ORIGINAL_F32, 424, 424),
    FIELD("yaw_speed", yaw_speed, Q2_ORIGINAL_F32, 420, 420),
    FIELD("waterlevel", water_level, Q2_ORIGINAL_I32, 612, 612),
    FIELD("watertype", water_type, Q2_ORIGINAL_I32, 608, 608),
    FIELD("monsterinfo.nextframe", next_frame, Q2_ORIGINAL_I32, 780, 780),
    FIELD("monsterinfo.scale", animation_scale, Q2_ORIGINAL_F32, 784, 784),
    FIELD("monsterinfo.attack_finished", attack_ns, Q2_ORIGINAL_TIME, 832, 832),
    FIELD("monsterinfo.saved_goal", saved_goal, Q2_ORIGINAL_VECTOR, 836, 836),
    FIELD("monsterinfo.search_time", search_ns, Q2_ORIGINAL_TIME, 848, 848),
    FIELD("monsterinfo.trail_time", trail_ns, Q2_ORIGINAL_TIME, 852, 852),
    FIELD("monsterinfo.last_sighting", last_sighting, Q2_ORIGINAL_VECTOR, 856, 856),
    FIELD("monsterinfo.lefty", lefty, Q2_ORIGINAL_BOOL, 872, 872),
    FIELD("monsterinfo.idle_time", idle_ns, Q2_ORIGINAL_TIME, 876, 876),
    FIELD("monsterinfo.medicTries", medic_tries, Q2_ORIGINAL_U32, 65535, 904),
    FIELD("monsterinfo.base_height", normal_height, Q2_ORIGINAL_F32, 65535, 932),
    FIELD("monsterinfo.next_duck_time", next_duck_ns, Q2_ORIGINAL_TIME, 65535, 936),
    FIELD("monsterinfo.duck_wait_time", duck_ns, Q2_ORIGINAL_TIME, 65535, 940),
    FIELD("monsterinfo.blind_fire_target", blind_fire_target, Q2_ORIGINAL_VECTOR, 65535, 956),
    FIELD("monsterinfo.quad_time", widow_powers.quad_until_ns, Q2_ORIGINAL_FRAME_TIME, 65535, 980),
    FIELD("monsterinfo.invincible_time", widow_powers.invulnerability_until_ns, Q2_ORIGINAL_FRAME_TIME, 65535, 984),
    FIELD("monsterinfo.double_time", widow_powers.double_until_ns, Q2_ORIGINAL_FRAME_TIME, 65535, 988),
    FIELD("s.scale", entity_scale, Q2_ORIGINAL_F32, 65535, 65535),
    FIELD("monsterinfo.had_visibility", had_visibility, Q2_ORIGINAL_BOOL, 65535, 65535),
    FIELD("monsterinfo.close_sight_tripped", close_sight_tripped, Q2_ORIGINAL_BOOL, 65535, 65535),
    FIELD("monsterinfo.melee_debounce_time", melee_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.strafe_check_time", strafe_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.fire_wait", fire_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.next_move_time", next_frame_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.fly_min_distance", fly_min_distance, Q2_ORIGINAL_F32, 65535, 65535),
    FIELD("monsterinfo.fly_max_distance", fly_max_distance, Q2_ORIGINAL_F32, 65535, 65535),
    FIELD("monsterinfo.fly_acceleration", fly_acceleration, Q2_ORIGINAL_F32, 65535, 65535),
    FIELD("monsterinfo.fly_speed", fly_speed, Q2_ORIGINAL_F32, 65535, 65535),
    FIELD("monsterinfo.fly_ideal_position", fly_ideal_position, Q2_ORIGINAL_VECTOR, 65535, 65535),
    FIELD("monsterinfo.fly_position_time", fly_position_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.fly_buzzard", fly_buzzard, Q2_ORIGINAL_BOOL, 65535, 65535),
    FIELD("monsterinfo.fly_above", fly_above, Q2_ORIGINAL_BOOL, 65535, 65535),
    FIELD("monsterinfo.fly_pinned", fly_pinned, Q2_ORIGINAL_BOOL, 65535, 65535),
    FIELD("monsterinfo.fly_thrusters", fly_thrusters, Q2_ORIGINAL_BOOL, 65535, 65535),
    FIELD("monsterinfo.fly_recovery_time", recovery_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.fly_recovery_dir", fly_recovery_direction, Q2_ORIGINAL_VECTOR, 65535, 65535),
    FIELD("monsterinfo.checkattack_time", check_attack_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.dodge_time", dodge_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.react_to_damage_time", react_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("monsterinfo.jump_time", jump_ns, Q2_ORIGINAL_TIME, 65535, 65535),
    FIELD("pos1", saved_attack_position, Q2_ORIGINAL_VECTOR, 352, 352),
};
#undef FIELD

static bool references_record(qa_q2_game *g, q2_original_record_io *io,
    struct qa_q2_monster *m)
{
#define REF(NAME, OFFSET, MEMBER) \
    if (!q2_original_reference(g, io, NAME, OFFSET, &m->MEMBER)) return false
    REF("enemy", 540, enemy); REF("oldenemy", 544, old_enemy);
    REF("goalentity", 412, goal); REF("movetarget", 416, move_target);
    REF("activator", 548, activator);
    if (io->edition == QA_Q2_RERELEASE || io->product == QA_Q2_ROGUE) {
        REF("monsterinfo.badMedic1", 908, bad_medic[0]);
        REF("monsterinfo.badMedic2", 912, bad_medic[1]);
        REF("monsterinfo.last_player_enemy", 944, last_player_enemy);
        REF("monsterinfo.commander", 976, commander);
        REF("bad_area", 1020, hazard);
        if (io->edition == QA_Q2_RERELEASE) {
            REF("monsterinfo.healer", 916, healer);
            REF("proboscus", 65535, proboscis);
        }
    }
#undef REF
    if (io->reading) {
        m->resurrect_target = m->medic ? m->enemy : (qa_actor_id){0};
        if (m->sound_target.present) {
            m->sound_target.actor = m->enemy;
            qa_body_state body;
            if (m->enemy.registry && qa_world_body_read(g->services.world, m->enemy, &body, NULL))
                m->sound_target.origin = body.origin;
        }
    }
    return true;
}

static bool monster_callbacks_record(qa_q2_game *game, q2_original_record_io *io,
    struct qa_q2_monster *m, const char *const *bindings)
{
    static const char *const fields[Q2_MONSTER_CALLBACK_COUNT] = {
        "monsterinfo.stand", "monsterinfo.idle", "monsterinfo.search", "monsterinfo.walk",
        "monsterinfo.run", "monsterinfo.dodge", "monsterinfo.attack", "monsterinfo.melee",
        "monsterinfo.sight", "monsterinfo.checkattack", "monsterinfo.setskin",
        "monsterinfo.blocked", "monsterinfo.duck", "monsterinfo.unduck", "monsterinfo.sidestep",
        "pain", "die"
    };
    static const uint16_t offsets[Q2_MONSTER_CALLBACK_COUNT] = {
        788, 792, 796, 800, 804, 808, 812, 816, 820, 824, 65535, 892, 920, 924, 928, 452, 456
    };
    bool wall_dormant = m->definition->species == Q2M_TURRET &&
        (m->spawnflags & 128u) && m->start_phase == Q2M_START_MANUAL;
    if (io->reading && m->definition->species == Q2M_TURRET) {
        if (!q2_original_function_matches(game, io, "use", 448, "turret_activate", &wall_dormant)) return false;
    }
    for (size_t i = 0; i < Q2_MONSTER_CALLBACK_COUNT; ++i) {
        if (io->edition == QA_Q2_CLASSIC && (i == 10 ||
            (i >= 11 && i <= 14 && io->product != QA_Q2_ROGUE))) continue;
        const char *name = wall_dormant && i < 9 ? NULL : bindings[i];
        if (i == 0 && m->definition->species >= Q2M_SOLDIER_LIGHT &&
            m->definition->species <= Q2M_SOLDIER_SS && (m->spawnflags & 8u) &&
            (io->edition == QA_Q2_RERELEASE || io->product == QA_Q2_ROGUE))
            name = "soldier_blind";
        if (!callback(game, io, fields[i], offsets[i], name, i >= 11 && i <= 14)) return false;
    }
    return true;
}

static bool weapon_flags_record(q2_original_record_io *io, struct qa_q2_monster *m)
{
    if (io->edition != QA_Q2_RERELEASE) return true;
    q2m_species species = m->definition->species;
    if (species == Q2M_INFANTRY || species == Q2M_TURRET_DRIVER) {
        int32_t cocked = m->cocked ? 1 : 0;
        if (!q2_original_scalar(io, "count", Q2_ORIGINAL_I32, 532, 532, 532, &cocked)) return false;
        if (io->reading) { m->count = cocked; m->cocked = cocked != 0; }
        return true;
    }
    bool soldier = (species >= Q2M_SOLDIER_LIGHT && species <= Q2M_SOLDIER_SS) ||
        species == Q2M_SOLDIER_RIPPER || species == Q2M_SOLDIER_HYPER || species == Q2M_SOLDIER_LASER;
    if (!soldier) return true;
    int32_t needs_cock = m->cocked ? 0 : 1;
    if (!q2_original_scalar(io, "dmg", Q2_ORIGINAL_I32, 516, 516, 516, &needs_cock)) return false;
    if (io->reading) m->cocked = needs_cock == 0;
    /* Source uses this member as a muzzle id for laser soldiers; that member
     * belongs to the beam continuation, not the shotgun refire boolean. */
    if (species == Q2M_SOLDIER_LASER) return true;
    int32_t refire = m->force_refire ? 1 : 0;
    if (!q2_original_scalar(io, "radius_dmg", Q2_ORIGINAL_I32, 520, 520, 520, &refire)) return false;
    if (io->reading) m->force_refire = refire != 0;
    return true;
}

static bool turret_orientation_record(q2_original_record_io *io, struct qa_q2_monster *m)
{
    if (m->definition->species != Q2M_TURRET) return true;
    qa_vec3 offset = qa_v3(0, (float)m->turret_orientation, 0);
    if (!q2_original_scalar(io, "offset", Q2_ORIGINAL_VECTOR, 65535, 65535, 996, &offset)) return false;
    if (io->reading) {
        if (truncf(offset.y) != offset.y || (double)offset.y < INT32_MIN || (double)offset.y > INT32_MAX)
            return monster_error(io, "offset", "Invalid original turret orientation");
        m->turret_orientation = (int)offset.y;
    }
    return true;
}

static bool reinforcement_record(qa_q2_game *game, q2_original_record_io *io,
    q2m_reinforcement *entry)
{
    char classname[QA_Q2_MONSTER_NAME_CAPACITY] = {0};
    if (!io->reading) {
        if (!entry->definition)
            return monster_error(io, "monsterinfo.reinforcements", "Reinforcement has no native Source definition");
        size_t length = strlen(entry->definition->classname);
        if (length >= sizeof(classname))
            return monster_error(io, "monsterinfo.reinforcements", "Source reinforcement classname is too long");
        memcpy(classname, entry->definition->classname, length);
    }
    const q2_original_field name = {"classname", 0, sizeof(classname), Q2_ORIGINAL_TEXT,
        {65535, 65535, 65535}};
    if (!q2_original_value(io, &name, classname) ||
        !q2_original_scalar(io, "strength", Q2_ORIGINAL_I32, 65535, 65535, 65535, &entry->strength) ||
        !q2_original_scalar(io, "mins", Q2_ORIGINAL_VECTOR, 65535, 65535, 65535, &entry->bounds.mins) ||
        !q2_original_scalar(io, "maxs", Q2_ORIGINAL_VECTOR, 65535, 65535, 65535, &entry->bounds.maxs))
        return false;
    if (io->reading) {
        entry->definition = q2m_definition_for(game, classname);
        if (!entry->definition)
            return monster_error(io, "monsterinfo.reinforcements", "Unknown original reinforcement classname");
    }
    return true;
}

static bool reinforcement_equal(const q2m_reinforcement *a, const q2m_reinforcement *b)
{
    return a->definition == b->definition && a->strength == b->strength &&
        a->bounds.mins.x == b->bounds.mins.x && a->bounds.mins.y == b->bounds.mins.y &&
        a->bounds.mins.z == b->bounds.mins.z && a->bounds.maxs.x == b->bounds.maxs.x &&
        a->bounds.maxs.y == b->bounds.maxs.y && a->bounds.maxs.z == b->bounds.maxs.z;
}

static bool summons_record(qa_q2_game *game, q2_original_record_io *io,
    struct qa_q2_monster *m)
{
    bool medic = m->definition->species == Q2M_MEDIC_COMMANDER ||
        (m->definition->species == Q2M_MEDIC &&
            (io->edition == QA_Q2_RERELEASE || io->product == QA_Q2_ROGUE));
    if (!medic) return true;
    q2m_summon_state *state = m->summons;
    if (io->reading && !state) {
        state = calloc(1, sizeof(*state));
        if (!state) {
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring original medic reinforcement state");
            return false;
        }
        m->summons = state;
    }
    if (io->edition == QA_Q2_CLASSIC) {
        int32_t strength = state ? state->classic_strength : 0;
        if (!q2_original_scalar(io, "plat2flags", Q2_ORIGINAL_I32, 65535, 65535, 992, &strength))
            return false;
        if (!io->reading) return true;
        if (strength < -6 || strength > 6)
            return monster_error(io, "plat2flags", "Original medic summon strength is out of range");
        if (strength < 0) { strength = -strength; m->manual_steering = true; }
        state->classic_strength = strength;
        /* Original Rogue stores the selection as plat2flags, not an array.
         * Expand that saved selection without rolling RNG or spawning actors. */
        static const char *const classes[] = {
            "monster_soldier_light", "monster_soldier", "monster_soldier_ss", "monster_infantry",
            "monster_gunner", "monster_medic", "monster_gladiator"
        };
        state->chosen_count = strength ? (size_t)(strength - 1 + strength % 2) : 1;
        for (size_t i = 0; i < state->chosen_count; ++i) {
            int index = strength - (int)i - (int)(i % 2);
            const q2m_definition *definition = q2m_definition_for(game, classes[index]);
            if (!definition)
                return monster_error(io, "plat2flags", "Original medic choice lacks its native definition");
            qa_bounds bounds = index == 6 ? (qa_bounds){{-32, -32, -24}, {32, 32, 64}} :
                (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
            state->chosen[i] = (q2m_reinforcement){definition, 1, bounds};
        }
        return true;
    }
    qa_json_id list = io->reading ?
        qa_json_get(io->document, io->object, "monsterinfo.reinforcements") : QA_JSON_NONE;
    if (io->reading) {
        size_t count = 0;
        if (list != QA_JSON_NONE && qa_json_type(io->document, list) != QA_JSON_NULL) {
            if (qa_json_type(io->document, list) != QA_JSON_ARRAY)
                return monster_error(io, "monsterinfo.reinforcements", "Expected original reinforcement array");
            count = qa_json_size(io->document, list);
        }
        if (count > SIZE_MAX / sizeof(q2m_reinforcement))
            return monster_error(io, "monsterinfo.reinforcements", "Original reinforcement list is too large");
        q2m_reinforcement *entries = count ? calloc(count, sizeof(*entries)) : NULL;
        if (count && !entries) {
            qa_error_set(io->error, QA_ERROR_MEMORY, count, "Restoring original reinforcement list");
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            q2_original_record_io entry_io = *io;
            entry_io.object = qa_json_at(io->document, list, i);
            if (qa_json_type(io->document, entry_io.object) != QA_JSON_OBJECT) {
                free(entries);
                return monster_error(io, "monsterinfo.reinforcements", "Invalid original reinforcement member");
            }
            if (!reinforcement_record(game, &entry_io, entries + i)) { free(entries); return false; }
        }
        free(state->entries);
        state->entries = entries;
        state->entry_count = count;
        state->configured = true;
        state->chosen_count = 0;
    } else if (state && state->entry_count) {
        qa_json_writer_key(io->writer, "monsterinfo.reinforcements");
        qa_json_writer_array(io->writer);
        for (size_t i = 0; i < state->entry_count; ++i) {
            qa_json_writer_object(io->writer);
            if (!reinforcement_record(game, io, state->entries + i)) return false;
            qa_json_writer_end(io->writer);
        }
        qa_json_writer_end(io->writer);
        if (io->writer->failed) return false;
    }
    uint32_t choices[5] = {255, 255, 255, 255, 255};
    if (!io->reading) {
        if (!state || !state->chosen_count) return true;
        if (state->chosen_count > sizeof(choices) / sizeof(*choices))
            return monster_error(io, "monsterinfo.chosen_reinforcements", "Too many native summon choices");
        for (size_t i = 0; i < state->chosen_count; ++i) {
            size_t index = 0;
            while (index < state->entry_count && !reinforcement_equal(state->chosen + i, state->entries + index))
                ++index;
            if (index >= state->entry_count || index >= UINT8_MAX)
                return monster_error(io, "monsterinfo.chosen_reinforcements", "Choice has no original reinforcement index");
            choices[i] = (uint32_t)index;
        }
    } else {
        qa_json_id selected = qa_json_get(io->document, io->object, "monsterinfo.chosen_reinforcements");
        if (selected == QA_JSON_NONE || qa_json_type(io->document, selected) == QA_JSON_NULL) return true;
        if (qa_json_type(io->document, selected) != QA_JSON_ARRAY ||
            qa_json_size(io->document, selected) != sizeof(choices) / sizeof(*choices))
            return monster_error(io, "monsterinfo.chosen_reinforcements", "Expected original five-choice array");
    }
    const q2_original_field chosen = {"monsterinfo.chosen_reinforcements", 0,
        sizeof(choices) / sizeof(*choices), Q2_ORIGINAL_U8, {65535, 65535, 65535}};
    if (!q2_original_value(io, &chosen, choices)) return false;
    if (io->reading) {
        for (size_t i = 0; i < sizeof(choices) / sizeof(*choices); ++i) {
            if (choices[i] == UINT8_MAX) continue;
            if (choices[i] > UINT8_MAX || choices[i] >= state->entry_count)
                return monster_error(io, "monsterinfo.chosen_reinforcements", "Choice exceeds the original reinforcement list");
            state->chosen[state->chosen_count++] = state->entries[choices[i]];
        }
    }
    return true;
}

static bool statue(const struct qa_q2_monster *m)
{
    return m->definition->species == Q2M_BOSS3_STAND ||
        m->definition->species == Q2M_TANK_STAND;
}

static bool stationary_source(const q2_original_record_io *io, const struct qa_q2_monster *m)
{
    return m->definition->locomotion == Q2M_STATIONARY ||
        (io->edition == QA_Q2_RERELEASE && m->definition->species == Q2M_INSANE && (m->spawnflags & 8u));
}

static bool schedule_record(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, struct qa_q2_monster *m)
{
    const char *think = "monster_think", *use = "monster_use", *touch = NULL;
    uint64_t due = m->next_frame_ns;
    switch (m->start_phase) {
    case Q2M_START_ACTIVE: break;
    case Q2M_START_PENDING:
        think = stationary_source(io, m) ? "stationarymonster_start_go" :
            m->definition->locomotion == Q2M_WALK ? "walkmonster_start_go" :
            m->definition->locomotion == Q2M_SWIM ? "swimmonster_start_go" :
            "flymonster_start_go";
        if (io->edition == QA_Q2_CLASSIC && m->definition->species == Q2M_INSANE && (m->spawnflags & 8u))
            think = "flymonster_start_go";
        due = m->start_due_ns;
        break;
    case Q2M_START_DORMANT:
        think = NULL; use = stationary_source(io, m) ? "stationarymonster_triggered_spawn_use" :
            "monster_triggered_spawn_use"; due = 0; break;
    case Q2M_START_TRIGGER:
        think = stationary_source(io, m) ? "stationarymonster_triggered_spawn" : "monster_triggered_spawn";
        due = m->start_due_ns; break;
    case Q2M_START_MANUAL:
        think = NULL; due = 0;
        if (m->definition->species == Q2M_TURRET && (m->spawnflags & 128u)) use = "turret_activate";
        break;
    }
    if (statue(m)) {
        think = m->definition->species == Q2M_BOSS3_STAND ? "Think_Boss3Stand" : "Think_TankStand";
        use = "Use_Boss3";
    } else if (m->turret_attached) {
        think = actor->entity && actor->entity->think == Q2ET_TURRET_LINK ?
            "turret_driver_link" : "turret_driver_think";
        due = actor->entity ? actor->entity->due_ns : 0;
    } else if (m->death_ns) {
        think = m->definition->species == Q2M_WIDOW2 ? "WidowExplode" :
            m->definition->species == Q2M_BOSS5 ? "BossExplode2" : "BossExplode";
        due = m->death_ns;
    } else if (m->corpse) {
        static const char *const corpses[] = {NULL, "M_FliesOn", "M_FliesOff", "monster_dead_think", "hover_deadthink"};
        if ((unsigned)m->corpse_phase >= sizeof(corpses) / sizeof(*corpses))
            return monster_error(io, "think", "Invalid native corpse schedule");
        think = corpses[m->corpse_phase];
        due = m->corpse_due_ns;
    }
    if (m->touch_active) {
        if (m->definition->species == Q2M_MUTANT) touch = "mutant_jump_touch";
        else if (m->definition->species == Q2M_GEKK) touch = "gekk_jump_touch";
        else if (m->definition->species == Q2M_BERSERK) touch = "berserk_jump_touch";
    }
    if (!io->reading) return callback(game, io, "think", 436, think, false) &&
        callback(game, io, "use", 448, use, false) && callback(game, io, "touch", 444, touch, false) &&
        q2_original_scalar(io, "nextthink", Q2_ORIGINAL_TIME, 428, 428, 428, &due);
    /* Original callbacks are the scheduling state; there is no private trailer
     * and loading never replays an SP function. */
    static const char *const think_names[] = {
        "monster_think", "walkmonster_start_go", "flymonster_start_go", "swimmonster_start_go",
        "monster_triggered_spawn", "M_FliesOn", "M_FliesOff", "monster_dead_think", "hover_deadthink",
        NULL, "Think_Boss3Stand", "Think_TankStand", "turret_driver_link", "turret_driver_think",
        "stationarymonster_start_go", "BossExplode", "BossExplode2", "WidowExplode", "stationarymonster_triggered_spawn"
    };
    size_t selected = sizeof(think_names) / sizeof(*think_names);
    for (size_t i = 0; i < sizeof(think_names) / sizeof(*think_names); ++i) {
        bool matches = false;
        if (!q2_original_function_matches(game, io, "think", 436, think_names[i], &matches)) return false;
        if (matches) { selected = i; break; }
    }
    if (selected == sizeof(think_names) / sizeof(*think_names))
        return monster_error(io, "think", "Unknown native monster Source schedule");
    if (!q2_original_scalar(io, "nextthink", Q2_ORIGINAL_TIME, 428, 428, 428, &due)) return false;
    bool is_statue = selected == 10 || selected == 11;
    if (is_statue != statue(m) || (selected == 10 && m->definition->species != Q2M_BOSS3_STAND) ||
        (selected == 11 && m->definition->species != Q2M_TANK_STAND) ||
        ((selected == 12 || selected == 13) && m->definition->species != Q2M_TURRET_DRIVER) ||
        (selected == 14 && !stationary_source(io, m)) ||
        (selected == 18 && !stationary_source(io, m)) ||
        (selected == 15 && m->definition->species != Q2M_SUPERTANK && m->definition->species != Q2M_JORG) ||
        (selected == 16 && m->definition->species != Q2M_BOSS5) ||
        (selected == 17 && m->definition->species != Q2M_WIDOW2))
        return monster_error(io, "think", "Schedule belongs to a different Source actor");
    m->turret_attached = selected == 12 || selected == 13;
    m->death_ns = selected >= 15 && selected <= 17 ? due : 0;
    m->start_phase = (selected >= 1 && selected <= 3) || selected == 14 ? Q2M_START_PENDING :
        selected == 4 || selected == 18 ? Q2M_START_TRIGGER : Q2M_START_ACTIVE;
    use = is_statue ? "Use_Boss3" : "monster_use";
    if (selected == 9 && !m->corpse) {
        bool dormant = false;
        const char *spawn_use = stationary_source(io, m) ? "stationarymonster_triggered_spawn_use" :
            "monster_triggered_spawn_use";
        if (!q2_original_function_matches(game, io, "use", 448, spawn_use, &dormant)) return false;
        m->start_phase = dormant ? Q2M_START_DORMANT : Q2M_START_MANUAL;
        if (dormant) use = spawn_use;
        else if (m->definition->species == Q2M_TURRET && (m->spawnflags & 128u)) use = "turret_activate";
    }
    if (!callback(game, io, "use", 448, use, false)) return false;
    m->triggered = m->start_phase == Q2M_START_DORMANT || m->start_phase == Q2M_START_TRIGGER;
    m->visible = m->start_phase != Q2M_START_DORMANT;
    m->start_due_ns = due;
    if (m->corpse || m->turret_attached || m->death_ns) m->next_frame_ns = UINT64_MAX;
    else if (io->edition == QA_Q2_CLASSIC) m->next_frame_ns = due;
    if (selected >= 5 && selected <= 8) {
        m->corpse = true;
        m->corpse_phase = (q2m_corpse_phase)(selected - 4);
        m->corpse_due_ns = due;
        if (m->corpse_phase == Q2M_CORPSE_HOVER) m->corpse_end_ns = m->timestamp_ns;
    }
    m->touch_active = false;
    static const char *const touches[] = {"mutant_jump_touch", "gekk_jump_touch", "berserk_jump_touch"};
    const q2m_species species[] = {Q2M_MUTANT, Q2M_GEKK, Q2M_BERSERK};
    for (size_t i = 0; i < sizeof(touches) / sizeof(*touches); ++i) {
        bool matches = false;
        if (m->definition->species == species[i]) {
            if (!q2_original_function_matches(game, io, "touch", 444, touches[i], &matches)) return false;
            if (matches) m->touch_active = true;
        }
    }
    return m->touch_active || callback(game, io, "touch", 444, NULL, false);
}

static bool monster_projectile_owner(qa_q2_game *game, q2_original_record_io *io,
    const q2_actor *actor, bool *projectile)
{
    *projectile = actor->projectile.kind != Q2_PROJECTILE_NONE;
    if (*projectile || !io->reading) return true;
    uint32_t flags = 0;
    uint64_t effects = 0;
    if (!q2_original_scalar(io, "svflags", Q2_ORIGINAL_U32, 184, 184, 184, &flags) ||
        !q2_original_scalar(io, "s.effects", Q2_ORIGINAL_U64, 64, 64, 64, &effects))
        return false;
    /* Source ThrowHead keeps classname while replacing the monster with
     * a gib edict. Its projectile continuation owns the remaining state. */
    if (!(flags & 2u) && (effects & 2u))
        return q2_original_function_matches(game, io, "die", 456, "gib_die", projectile);
    return true;
}

bool q2_original_monster_record(qa_q2_game *game, q2_original_record_io *io,
    q2_actor *actor, const qa_q2_save_level *level, qa_error *error)
{
    if (!game || !io || !actor) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 monster requires its actual Source actor");
        return false;
    }
    const char *classname = actor->monster && actor->monster->definition ?
        actor->monster->definition->classname : actor->entity ?
        qa_strings_cstr(qa_session_strings(game->services.session), actor->entity->classname) : NULL;
    const q2m_definition *definition = q2m_definition_for(game, classname);
    if (!definition) return true;
    bool projectile = false;
    if (!monster_projectile_owner(game, io, actor, &projectile)) return false;
    if (projectile) return true;
    const char *const *bindings = callbacks_for(game, classname);
    if (!bindings) return monster_error(io, "classname", "Monster has no exact Source callback bindings");
    struct qa_q2_monster *m = actor->monster;
    bool created = false;
    if (!m && io->reading && !io->references_only) {
        m = calloc(1, sizeof(*m));
        if (!m) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring original Q2 monster state");
            return false;
        }
        created = true;
        m->definition = definition;
        m->move_set = q2m_move_set_for(game, definition);
        m->classname = actor->entity->classname;
        m->entity_scale = io->edition == QA_Q2_CLASSIC ? 1 : 0;
        m->health_scaling = 1;
        m->animation_scale = definition->scale;
        m->normal_height = definition->bounds.maxs.z;
        m->base_health = definition->health;
        m->max_health = definition->health;
        m->gib_health = definition->gib_health;
        m->view_height = definition->view_height;
        m->initialized = true;
        m->old_frame = -1;
        m->visible = true;
        m->can_take_damage = true;
        m->attack_state = Q2M_STRAIGHT;
        if (!m->move_set) {
            free(m);
            return monster_error(io, "classname", "Monster has no exact Source move table");
        }
    }
    if (!m || m->controller_kind != Q2M_CONTROLLER_NONE)
        return monster_error(io, "classname", "Actor has no normal native monster owner");
    bool okay = true;
    if (io->references_only) return references_record(game, io, m);
    for (size_t i = 0; okay && i < sizeof(monster_fields) / sizeof(*monster_fields); ++i) {
        const q2_original_field *field = monster_fields + i;
        if (!io->reading && (!strncmp(field->name, "s.", 2) ||
            !strcmp(field->name, "ideal_yaw") || !strcmp(field->name, "yaw_speed") ||
            !strcmp(field->name, "waterlevel") || !strcmp(field->name, "watertype") ||
            (!strcmp(field->name, "count") && io->edition == QA_Q2_RERELEASE &&
                (definition->species == Q2M_INFANTRY || definition->species == Q2M_TURRET_DRIVER)))) continue;
        if (!io->reading && !strcmp(field->name, "timestamp") && m->corpse_phase == Q2M_CORPSE_HOVER) {
            okay = q2_original_value(io, field, &m->corpse_end_ns);
            continue;
        }
        if (!io->reading && !strcmp(field->name, "fly_sound_debounce_time") && m->flies_ns == UINT64_MAX) {
            /* The native completed-flies marker belongs to Source AI_STUNK,
             * rather than an infinite gtime_t deadline. */
            uint64_t completed = 0;
            okay = q2_original_value(io, field, &completed);
            continue;
        }
        okay = q2_original_value(io, field, (uint8_t *)m + field->offset);
    }
    if (okay) okay = weapon_flags_record(io, m) && turret_orientation_record(io, m) &&
        hostile_record(io, &m->hostile_ns) &&
        integer_float(io, "gib_health", 488, &m->gib_health) &&
        integer_float(io, "max_health", 484, &m->max_health) &&
        integer_float(io, "viewheight", 508, &m->view_height);
    uint64_t pause = m->pause_ns == UINT64_MAX ? UINT64_C(100000000000000000) : m->pause_ns;
    if (okay) okay = q2_original_scalar(io, "monsterinfo.pausetime", Q2_ORIGINAL_TIME,
        828, 828, 828, &pause);
    if (io->reading) m->pause_ns = pause >= UINT64_C(100000000000000000) ? UINT64_MAX : pause;
    int32_t attack = (int32_t)m->attack_state + (io->edition == QA_Q2_RERELEASE ? 1 : 0);
    if (okay) okay = q2_original_scalar(io, "monsterinfo.attack_state", Q2_ORIGINAL_I32,
        868, 868, 868, &attack);
    if (okay && io->reading) {
        if (io->edition == QA_Q2_RERELEASE && attack > 0) --attack;
        if (attack < 0 || attack > Q2M_BLIND)
            okay = monster_error(io, "monsterinfo.attack_state", "Invalid original attack state");
        else m->attack_state = (q2m_attack_state)attack;
    }
    if (okay) {
        int32_t link = 0;
        if (!io->reading && m->last_link_count > INT32_MAX)
            okay = monster_error(io, "monsterinfo.linkcount", "Link revision exceeds its Source integer");
        else {
            link = io->reading ? 0 : (int32_t)m->last_link_count;
            okay = q2_original_scalar(io, "monsterinfo.linkcount", Q2_ORIGINAL_I32,
                880, 880, 880, &link);
            if (okay && link < 0)
                okay = monster_error(io, "monsterinfo.linkcount", "Negative Source link revision");
            if (okay && io->reading) m->last_link_count = (uint32_t)link;
        }
    }
    if (okay) okay = flags_record(io, actor, m);
    if (okay && (io->edition == QA_Q2_RERELEASE || io->product == QA_Q2_ROGUE)) {
        okay = integer_wide(io, "monsterinfo.monster_slots", 968, &m->monster_slots) &&
            integer_wide(io, "monsterinfo.monster_used", 972, &m->monster_used) &&
            q2_original_scalar(io, "monsterinfo.blind_fire_delay", Q2_ORIGINAL_SECONDS_TIME,
                65535, 65535, 952, &m->blind_fire_delay);
    }
    if (okay && io->edition == QA_Q2_RERELEASE)
        okay = integer_float(io, "monsterinfo.base_health", 65535, &m->base_health) &&
            integer_float(io, "monsterinfo.health_scaling", 65535, &m->health_scaling);
    bool wall_dormant = m->definition->species == Q2M_TURRET && (m->spawnflags & 128u) &&
        m->start_phase == Q2M_START_MANUAL;
    const q2m_move *source_move = !io->reading && (statue(m) || m->turret_attached || wall_dormant) ? NULL : m->move;
    if (okay) okay = move_record(io, io->edition == QA_Q2_RERELEASE ?
        "monsterinfo.active_move" : "monsterinfo.currentmove", 772, m, &source_move);
    if (okay && io->reading) m->move = source_move;
    if (okay && io->edition == QA_Q2_RERELEASE)
        okay = move_record(io, "monsterinfo.next_move", 65535, m, &m->next_move);
    if (okay) okay = references_record(game, io, m) &&
        (!io->reading || q2_original_string(game, io, "combattarget", 320, &m->combat_target)) &&
        (!io->reading || q2_original_resource(game, io, level, "s.modelindex",
            40, 40, 40, 32, &m->model));
    if (okay && io->edition == QA_Q2_RERELEASE)
        okay = q2_original_resource(game, io, level, "monsterinfo.weapon_sound",
            65535, 65535, 65535, 288, &m->weapon_sound);
    if (okay && io->reading) {
        if (io->edition == QA_Q2_RERELEASE) {
            okay = q2_original_scalar(io, "deadflag", Q2_ORIGINAL_BOOL, 492, 492, 492, &m->dead) &&
                q2_original_scalar(io, "takedamage", Q2_ORIGINAL_BOOL, 512, 512, 512, &m->can_take_damage);
        } else {
            int32_t dead = 0, damage = 0;
            okay = q2_original_scalar(io, "deadflag", Q2_ORIGINAL_I32, 492, 492, 492, &dead) &&
                q2_original_scalar(io, "takedamage", Q2_ORIGINAL_I32, 512, 512, 512, &damage);
            m->dead = dead != 0;
            m->can_take_damage = damage != 0;
        }
        m->death_notified = m->dead;
        m->old_frame = m->frame;
        m->corpse = m->dead && (actor->physics.flags & QA_PHYSICS_DEAD) != 0;
        if (io->edition == QA_Q2_CLASSIC) m->entity_scale = 1;
        if (!(m->animation_scale > 0)) m->animation_scale = definition->scale *
            (m->entity_scale != 0 ? m->entity_scale : 1);
        if (io->edition == QA_Q2_CLASSIC) m->base_health = m->max_health;
    }
    if (okay) okay = monster_callbacks_record(game, io, m, bindings) && schedule_record(game, io, actor, m) &&
        summons_record(game, io, m);
    if (okay && io->reading && !m->move && (statue(m) || m->turret_attached ||
        (m->definition->species == Q2M_TURRET && (m->spawnflags & 128u) && m->start_phase == Q2M_START_MANUAL)))
        m->move = q2m_move_find(m, definition->initial_move);
    if (okay && io->reading && !m->move)
        okay = monster_error(io, "monsterinfo.currentmove", "Normal monster has no active Source move");
    qa_combat_state combat;
    bool has_combat = okay && qa_combat_read(game->services.combat, actor->id, &combat, error);
    if (okay && !has_combat) okay = false;
    if (okay) {
        qa_power_kind kind = combat.armor.powered.kind;
        double cells = combat.armor.powered.cells;
        okay = power_kind(io, "monsterinfo.power_armor_type", 884, &kind) &&
            integer_double(io, "monsterinfo.power_armor_power", 888, &cells);
        if (okay && io->reading) {
            combat.armor.powered.kind = kind;
            combat.armor.powered.cells = cells;
            qa_q2_combat_power_armor_source(game, &combat.armor.powered);
            okay = qa_combat_set_armor(game->services.combat, actor->id, &combat.armor, error);
        }
        if (okay && io->edition == QA_Q2_RERELEASE) {
            okay = power_kind(io, "monsterinfo.initial_power_armor_type", 65535, &m->initial_power_armor) &&
                integer_double(io, "monsterinfo.max_power_armor_power", 65535, &m->max_power_armor);
            qa_item_id armor_item = combat.armor.regular.item;
            double armor_points = combat.armor.regular.points;
            if (okay) okay = q2_original_item(game, io, "monsterinfo.armor_type",
                65535, 65535, 65535, &armor_item) &&
                integer_double(io, "monsterinfo.armor_power", 65535, &armor_points);
            if (okay && io->reading) {
                const qa_q2_item_definition *armor = armor_item ? q2_item_by_id(game, armor_item) : NULL;
                if (armor_item && (!armor || armor->kind != QA_Q2_ITEM_ARMOR))
                    okay = monster_error(io, "monsterinfo.armor_type", "Source monster armor is not a regular armor item");
                else {
                    combat.armor.regular = armor ? (qa_regular_armor){.kind=QA_ARMOR_Q2,
                        .points=armor_points, .item=armor_item,
                        .protection.q2={armor->normal_protection,armor->energy_protection}} :
                        (qa_regular_armor){0};
                    okay = qa_combat_set_armor(game->services.combat, actor->id, &combat.armor, error);
                }
            }
        }
    }
    if (created) {
        if (okay) actor->monster = m;
        else q2m_free_monster(m);
    }
    return okay;
}
