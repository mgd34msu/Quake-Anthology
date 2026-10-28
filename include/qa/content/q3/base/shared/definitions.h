/* Port of src/content/q3/base/shared/definitions.ts. GPL-2.0-or-later.
 * MoveType/WeaponState/Powerup/Holdable/Weapon/EntityEvent re-exports are
 * the landed qa/movement/q3/constants.h enums. */
#ifndef QA_CONTENT_Q3_BASE_SHARED_DEFINITIONS_H
#define QA_CONTENT_Q3_BASE_SHARED_DEFINITIONS_H
#include <stdbool.h>
#include "qa/movement/q3/constants.h"

/* TS Product = "baseq3" | "missionpack". */
typedef enum qa_q3_base_product { QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_PRODUCT_MISSIONPACK } qa_q3_base_product;
const char *qa_q3_base_product_name(qa_q3_base_product product);

enum {
  QA_Q3_BASE_DEFAULT_GRAVITY = 800,
  QA_Q3_BASE_GIB_HEALTH = -40,
  QA_Q3_BASE_MAX_ITEMS = 256,
  QA_Q3_BASE_EVENT_VALID_MSEC = 300,
  QA_Q3_BASE_EV_EVENT_BIT1 = 0x100,
  QA_Q3_BASE_EV_EVENT_BIT2 = 0x200,
  QA_Q3_BASE_EV_EVENT_BITS = 0x300
};
#define QA_Q3_BASE_ARMOR_PROTECTION 0.66

typedef enum qa_q3_base_game_type {
  QA_Q3_BASE_GT_FFA = 0, QA_Q3_BASE_GT_TOURNAMENT = 1, QA_Q3_BASE_GT_SINGLE_PLAYER = 2,
  QA_Q3_BASE_GT_TEAM = 3, QA_Q3_BASE_GT_CTF = 4, QA_Q3_BASE_GT_1FCTF = 5,
  QA_Q3_BASE_GT_OBELISK = 6, QA_Q3_BASE_GT_HARVESTER = 7, QA_Q3_BASE_GT_MAX_GAME_TYPE = 8
} qa_q3_base_game_type;
typedef enum qa_q3_base_team {
  QA_Q3_BASE_TEAM_FREE = 0, QA_Q3_BASE_TEAM_RED = 1, QA_Q3_BASE_TEAM_BLUE = 2,
  QA_Q3_BASE_TEAM_SPECTATOR = 3, QA_Q3_BASE_TEAM_NUM_TEAMS = 4
} qa_q3_base_team;
typedef enum qa_q3_base_item_type {
  QA_Q3_BASE_IT_BAD = 0, QA_Q3_BASE_IT_WEAPON = 1, QA_Q3_BASE_IT_AMMO = 2, QA_Q3_BASE_IT_ARMOR = 3,
  QA_Q3_BASE_IT_HEALTH = 4, QA_Q3_BASE_IT_POWERUP = 5, QA_Q3_BASE_IT_HOLDABLE = 6,
  QA_Q3_BASE_IT_PERSISTANT_POWERUP = 7, QA_Q3_BASE_IT_TEAM = 8
} qa_q3_base_item_type;
typedef enum qa_q3_base_entity_type {
  QA_Q3_BASE_ET_GENERAL = 0, QA_Q3_BASE_ET_PLAYER = 1, QA_Q3_BASE_ET_ITEM = 2, QA_Q3_BASE_ET_MISSILE = 3,
  QA_Q3_BASE_ET_MOVER = 4, QA_Q3_BASE_ET_BEAM = 5, QA_Q3_BASE_ET_PORTAL = 6, QA_Q3_BASE_ET_SPEAKER = 7,
  QA_Q3_BASE_ET_PUSH_TRIGGER = 8, QA_Q3_BASE_ET_TELEPORT_TRIGGER = 9, QA_Q3_BASE_ET_INVISIBLE = 10,
  QA_Q3_BASE_ET_GRAPPLE = 11, QA_Q3_BASE_ET_TEAM = 12, QA_Q3_BASE_ET_EVENTS = 13
} qa_q3_base_entity_type;
typedef enum qa_q3_base_persistent_index {
  QA_Q3_BASE_PERS_SCORE = 0, QA_Q3_BASE_PERS_HITS = 1, QA_Q3_BASE_PERS_RANK = 2, QA_Q3_BASE_PERS_TEAM = 3,
  QA_Q3_BASE_PERS_SPAWN_COUNT = 4, QA_Q3_BASE_PERS_PLAYEREVENTS = 5, QA_Q3_BASE_PERS_ATTACKER = 6,
  QA_Q3_BASE_PERS_ATTACKEE_ARMOR = 7, QA_Q3_BASE_PERS_KILLED = 8, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT = 9,
  QA_Q3_BASE_PERS_EXCELLENT_COUNT = 10, QA_Q3_BASE_PERS_DEFEND_COUNT = 11, QA_Q3_BASE_PERS_ASSIST_COUNT = 12,
  QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT = 13, QA_Q3_BASE_PERS_CAPTURES = 14
} qa_q3_base_persistent_index;
typedef enum qa_q3_base_stat_index {
  QA_Q3_BASE_STAT_HEALTH = 0, QA_Q3_BASE_STAT_HOLDABLE_ITEM = 1, QA_Q3_BASE_STAT_WEAPONS = 2,
  QA_Q3_BASE_STAT_ARMOR = 3, QA_Q3_BASE_STAT_DEAD_YAW = 4, QA_Q3_BASE_STAT_CLIENTS_READY = 5,
  QA_Q3_BASE_STAT_MAX_HEALTH = 6
} qa_q3_base_stat_index;
typedef enum qa_q3_base_missionpack_stat_index {
  QA_Q3_BASE_MP_STAT_HEALTH = 0, QA_Q3_BASE_MP_STAT_HOLDABLE_ITEM = 1, QA_Q3_BASE_MP_STAT_PERSISTANT_POWERUP = 2,
  QA_Q3_BASE_MP_STAT_WEAPONS = 3, QA_Q3_BASE_MP_STAT_ARMOR = 4, QA_Q3_BASE_MP_STAT_DEAD_YAW = 5,
  QA_Q3_BASE_MP_STAT_CLIENTS_READY = 6, QA_Q3_BASE_MP_STAT_MAX_HEALTH = 7
} qa_q3_base_missionpack_stat_index;

/* TS StatSchema (+ product; missionpack's persistentPowerup present only
 * when has_persistent_powerup). Returned pointer is a static frozen row. */
typedef struct qa_q3_base_stat_schema {
  qa_q3_base_product product;
  double health, holdable_item, weapons, armor, dead_yaw, clients_ready, max_health;
  bool has_persistent_powerup; double persistent_powerup;
} qa_q3_base_stat_schema;
const qa_q3_base_stat_schema *qa_q3_base_stat_schema_for(qa_q3_base_product product);
double qa_q3_base_weapon_count(qa_q3_base_product product);
bool qa_q3_base_weapon_available(qa_q3_base_product product, qa_q3_weapon weapon);
#endif
