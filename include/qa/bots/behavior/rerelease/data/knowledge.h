/* Port of src/bots/behavior/rerelease/data/knowledge.ts. TS BotWeaponT/
 * BotItemT (entry + derived flags) become wrapper recs borrowing entries
 * owned by the embedded parse results. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_DATA_KNOWLEDGE_H
#define QA_BOTS_BEHAVIOR_RERELEASE_DATA_KNOWLEDGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include "qa/bots/behavior/rerelease/world.h"
extern const char *const QA_ITEM_FLAG_HEALTH, *const QA_ITEM_FLAG_AMMO, *const QA_ITEM_FLAG_ARMOR, *const QA_ITEM_FLAG_WEAPON, *const QA_ITEM_FLAG_POWERUP, *const QA_ITEM_FLAG_DROPPED, *const QA_ITEM_FLAG_MEGA_ITEM, *const QA_ITEM_FLAG_BACKPACK, *const QA_ITEM_FLAG_ZAP_PROOF, *const QA_ITEM_FLAG_CHECK_ITEMS, *const QA_ITEM_FLAG_OBJECTIVE, *const QA_ITEM_FLAG_RUNE, *const QA_ITEM_FLAG_KEY, *const QA_ITEM_FLAG_USABLE, *const QA_ITEM_FLAG_IGNORE_LIMITS;
extern const char *const QA_WEAPON_FLAG_MELEE, *const QA_WEAPON_FLAG_HITSCAN, *const QA_WEAPON_FLAG_PROJECTILE, *const QA_WEAPON_FLAG_PARABOLIC, *const QA_WEAPON_FLAG_EXPLOSIVE, *const QA_WEAPON_FLAG_STICKY, *const QA_WEAPON_FLAG_ELECTRIC, *const QA_WEAPON_FLAG_STARTING, *const QA_WEAPON_FLAG_INITIAL, *const QA_WEAPON_FLAG_SUITABLE, *const QA_WEAPON_FLAG_ROCKET_JUMP;
extern const char *const QA_MONSTER_FLAG_MELEE, *const QA_MONSTER_FLAG_RANGED, *const QA_MONSTER_FLAG_AQUATIC, *const QA_MONSTER_FLAG_FLYER, *const QA_MONSTER_FLAG_TANK, *const QA_MONSTER_FLAG_SPONGE, *const QA_MONSTER_FLAG_TURRET, *const QA_MONSTER_FLAG_NEUTRAL;
extern const char *const QA_DANGER_FLAG_TAKES_DAMAGE, *const QA_DANGER_FLAG_IGNORE_IF_TEAM, *const QA_DANGER_FLAG_IGNORE_IF_COOP, *const QA_DANGER_FLAG_IGNORE_IF_NO_TEAM_DMG, *const QA_DANGER_FLAG_TIMER_BASED, *const QA_DANGER_FLAG_NAV_HAZARD, *const QA_DANGER_FLAG_CHECK_VELOCITY, *const QA_DANGER_FLAG_CHECK_BEAM, *const QA_DANGER_FLAG_EXPLODING_BARREL, *const QA_DANGER_FLAG_UPDATE_PVS;
extern const char *const QA_INTERACTION_PUSH, *const QA_INTERACTION_SHOOT, *const QA_INTERACTION_USE, *const QA_INTERACTION_RIDE, *const QA_INTERACTION_TOUCH;
#define QA_BOT_GAME_TYPE_DEATHMATCH "deathmatch"
#define QA_BOT_GAME_TYPE_TDM "tdm"
#define QA_BOT_GAME_TYPE_COOP "coop"
#define QA_BOT_GAME_TYPE_HORDE "horde"
#define QA_BOT_GAME_TYPE_CTF "ctf"
typedef struct qa_bot_data_files { const char *characters, *weapons, *items, *monsters, *interactables, *game_rules, *teams, *chats, *settings, *dangers; } qa_bot_data_files; /* dangers NULL = TS undefined */
typedef struct qa_bot_weapon_rec { qa_weapon_entry *entry; bool is_melee, is_electric, needs_ammo; } qa_bot_weapon_rec;
typedef struct qa_bot_item_rec { qa_item_entry *entry; bool is_health, is_armor, is_ammo, is_weapon, is_powerup, is_mega; double mega_spawnflag; } qa_bot_item_rec;
typedef struct qa_bot_game_mode { const char *game_type; char *game_type_owned; bool weapon_stay, has_has_teams, has_teams, has_team_damage, team_damage; } qa_bot_game_mode;
typedef struct qa_bot_knowledge {
  qa_bot_data_format format;
  qa_characters_result chars; qa_weapons_result weapons_r; qa_items_result items_r; qa_dangers_result dangers_r;
  qa_monsters_result monsters_r; qa_interactables_result inter_r; qa_game_rules_result rules_r; qa_teams_result teams_r; qa_chats_result chats_r; qa_bot_settings_result settings_r;
  qa_bot_weapon_rec *weapons; size_t weapon_count; qa_bot_item_rec *items; size_t item_count;
  char **errors; size_t error_count;
  qa_bot_weapon_rec *replaced; size_t replaced_count; /* non-NULL after replace_weapons */
} qa_bot_knowledge;
typedef bool (*qa_weapon_number_fn)(void *ctx, const char *classname, double *out);
/* weapon_number NULL = TS options.weaponNumber undefined. Files borrowed only during init. */
bool qa_bot_knowledge_init(qa_bot_knowledge *k, qa_bot_data_files files, qa_bot_data_format format, qa_weapon_number_fn weapon_number, void *wn_ctx);
void qa_bot_knowledge_destroy(qa_bot_knowledge *k);
void qa_bot_knowledge_replace_weapons(qa_bot_knowledge *k, qa_bot_weapon_rec *weapons, size_t count); /* borrows array */
const qa_bot_skill_settings *qa_bot_knowledge_get_skill(const qa_bot_knowledge *k, const char *name);
/* TS skillNames(): file order. Returns name or NULL out of range; count via out. */
const char *qa_bot_knowledge_skill_name(const qa_bot_knowledge *k, size_t index, size_t *out_count);
const qa_bot_item_rec *qa_bot_knowledge_get_item(const qa_bot_knowledge *k, const char *classname);
const qa_monster_entry *qa_bot_knowledge_get_monster(const qa_bot_knowledge *k, const char *classname);
const qa_bot_weapon_rec *qa_bot_knowledge_weapon_by_name(const qa_bot_knowledge *k, const char *name);
const qa_danger_entry *qa_bot_knowledge_get_danger(const qa_bot_knowledge *k, const char *classname);
const qa_bot_weapon_rec *qa_bot_knowledge_weapon_by_number(const qa_bot_knowledge *k, double number);
const qa_character_entry *qa_bot_knowledge_get_character(const qa_bot_knowledge *k, const char *name);
int64_t qa_bot_knowledge_team_index(const qa_bot_knowledge *k, double quakec_team_value);
/* Caller frees array only. */
const qa_chat_entry **qa_bot_knowledge_chats_of_type(const qa_bot_knowledge *k, const char *type, size_t *out_count);
qa_bot_game_mode qa_bot_knowledge_game_mode(const qa_bot_knowledge *k, double (*cvar_value)(void *ctx, const char *name), void *ctx);
void qa_bot_game_mode_destroy(qa_bot_game_mode *m);
const char *qa_bot_knowledge_interaction_for(const qa_bot_knowledge *k, const char *classname, int64_t spawnflags, bool has_health, bool has_targetname);
typedef struct qa_bot_weapon_pick { const qa_bot_weapon_rec *weapon; double number; } qa_bot_weapon_pick;
typedef struct qa_bot_weapon_context { int64_t items; const qa_bot_ammo_entry *ammo; size_t ammo_count; double range, height_delta; bool in_water, has_protection, target_in_water, allow_melee; } qa_bot_weapon_context;
bool qa_choose_weapon(const qa_bot_weapon_rec *weapons, size_t count, qa_bot_weapon_context ctx, qa_bot_weapon_pick *out);
typedef struct qa_bot_item_context { int64_t spawnflags; double health, max_health, armor; int64_t items; const qa_bot_ammo_entry *ammo; size_t ammo_count; bool weapon_stay, allow_power_items; const qa_bot_weapon_rec *weapons; size_t weapon_count; int64_t team, item_team; bool objective_at_home; } qa_bot_item_context;
double qa_bot_item_value(const qa_bot_item_rec *item, qa_bot_item_context ctx);
const qa_bot_weapon_rec *qa_weapon_for_item(const qa_bot_weapon_rec *weapons, size_t count, const char *item_name);
#endif
