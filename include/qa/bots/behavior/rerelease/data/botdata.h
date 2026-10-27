/* Port of src/bots/behavior/rerelease/data/botdata.ts. Entry string/flag
 * fields borrow from the result's embedded parsed blocks; each result's
 * destroy frees everything. TS classes become structs + init defaults.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_DATA_BOTDATA_H
#define QA_BOTS_BEHAVIOR_RERELEASE_DATA_BOTDATA_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/bots/behavior/rerelease/data/blockparse.h"
typedef enum qa_bot_data_format { QA_BOT_DATA_Q1, QA_BOT_DATA_Q2 } qa_bot_data_format;
typedef struct qa_bot_source_entry_base { const qa_block *source; const qa_block_field **unknown; size_t unknown_count; } qa_bot_source_entry_base;
typedef struct qa_character_entry { qa_bot_source_entry_base base; const char *fun_name, *name, *skin, *dogtag; double shirt_color, pants_color; } qa_character_entry;
typedef struct qa_weapon_identity { bool is_q2_classname; double bit; const char *classname; } qa_weapon_identity;
typedef struct qa_weapon_entry { qa_bot_source_entry_base base; qa_weapon_identity identity; double speed, ideal_fov, trigger_hold, trigger_cooldown, number, damage, min_range, max_range, min_height, max_height, priority, min_ammo, max_ammo; bool trigger_hold_and_release; const char *name, *ammo, *ammo_name, *aim_point; const char **flags; size_t flag_count; } qa_weapon_entry;
typedef struct qa_item_spawnflag { double bit; const char *name; } qa_item_spawnflag;
typedef struct qa_item_entry { qa_bot_source_entry_base base; double sight_dist; const char *name; qa_item_spawnflag *spawnflags; size_t spawnflag_count; const char **flags; size_t flag_count; bool has_team; double team; } qa_item_entry;
typedef struct qa_monster_entry { qa_bot_source_entry_base base; const char *classname; const char **flags; size_t flag_count; } qa_monster_entry;
typedef struct qa_bot_interactable_bounds { double mins[3], maxs[3]; } qa_bot_interactable_bounds;
typedef struct qa_interactable_entry { qa_bot_source_entry_base base; bool has_bounds; qa_bot_interactable_bounds bounds; const char *name, *interaction, *logic_op; bool has_spawnflags; double spawnflags; bool has_health, health, has_targetname, targetname; } qa_interactable_entry;
typedef struct qa_bot_rule_condition { const char *cvar; double value; } qa_bot_rule_condition;
typedef struct qa_game_rule_entry { qa_bot_source_entry_base base; qa_bot_rule_condition *conditions; size_t condition_count; const char *only_game_type, *not_game_type, *cvar, *game_type; bool logic_is_and, has_teams_present, has_teams, team_damage_present, team_damage, weapon_stay_specified, weapon_stay; double value; } qa_game_rule_entry;
typedef struct qa_team_entry { qa_bot_source_entry_base base; double value; const char *name; } qa_team_entry;
typedef struct qa_chat_entry { qa_bot_source_entry_base base; const char *locstring, *type; double time, chance; bool team; } qa_chat_entry;
typedef struct qa_danger_entry { qa_bot_source_entry_base base; const char *name; double sight_dist; const char **flags; size_t flag_count; } qa_danger_entry;
typedef struct qa_bot_aiming_settings { double max_acceleration, spring_stiffness, damping, velocity_offset, modifier_max_angle, modifier_apply_time, modifier_accel_scalar, modifier_spring_scalar, modifier_damping_scalar; bool lead_targets; } qa_bot_aiming_settings;
void qa_bot_aiming_settings_init(qa_bot_aiming_settings *s);
typedef struct qa_bot_behavior_settings { double combat_max_item_dist, combat_min_health_pct, combat_min_ammo_pct, combat_min_armor_pct, min_respawn_time, max_respawn_time; bool combat_grab_weapons, react_to_dangers, allow_combat, allow_grab_items_in_combat, allow_melee, allow_check_six, allow_grab_items, allow_grab_power_items, defer_power_items_to_humans; } qa_bot_behavior_settings;
void qa_bot_behavior_settings_init(qa_bot_behavior_settings *s);
typedef struct qa_bot_movement_settings { bool allow_jumping_in_combat, walk_only, allow_rocket_jumping; double jump_chance, jump_cooldown; } qa_bot_movement_settings;
void qa_bot_movement_settings_init(qa_bot_movement_settings *s);
typedef struct qa_bot_senses_settings { double sight_time, sight_decay_time, invis_enemy_sight_scalar, max_invis_enemy_sight_dist, fov_angle, forget_non_vis_enemy_time, sound_range, sound_time, sound_decay_time, sound_persist_time; } qa_bot_senses_settings;
void qa_bot_senses_settings_init(qa_bot_senses_settings *s);
typedef struct qa_bot_weapon_sense_settings { double fov_scalar, decay_time, fov_angle, sight_time; } qa_bot_weapon_sense_settings;
void qa_bot_weapon_sense_settings_init(qa_bot_weapon_sense_settings *s);
typedef struct qa_bot_skill_settings { const qa_block *source; const char *skill; qa_bot_aiming_settings aiming; qa_bot_behavior_settings behaviors; qa_bot_movement_settings movement; qa_bot_senses_settings senses; qa_bot_weapon_sense_settings weapons; const qa_block_field **unknown; size_t unknown_count; } qa_bot_skill_settings;
void qa_bot_skill_settings_init(qa_bot_skill_settings *s);
#define QA_BOTDATA_RESULT(EntryT, Name) typedef struct Name { qa_block_parse_result parsed; EntryT *entries; size_t entry_count; char **errors; size_t error_count; } Name
QA_BOTDATA_RESULT(qa_character_entry, qa_characters_result);
QA_BOTDATA_RESULT(qa_weapon_entry, qa_weapons_result);
QA_BOTDATA_RESULT(qa_item_entry, qa_items_result);
QA_BOTDATA_RESULT(qa_monster_entry, qa_monsters_result);
QA_BOTDATA_RESULT(qa_interactable_entry, qa_interactables_result);
QA_BOTDATA_RESULT(qa_game_rule_entry, qa_game_rules_result);
QA_BOTDATA_RESULT(qa_team_entry, qa_teams_result);
QA_BOTDATA_RESULT(qa_chat_entry, qa_chats_result);
QA_BOTDATA_RESULT(qa_danger_entry, qa_dangers_result);
typedef struct qa_bot_settings_result { qa_block_parse_result parsed; qa_bot_skill_settings *skills; size_t skill_count; char **errors; size_t error_count; } qa_bot_settings_result;
bool qa_parse_characters(qa_characters_result *out, const char *text); void qa_characters_result_destroy(qa_characters_result *r);
bool qa_parse_weapons(qa_weapons_result *out, const char *text, qa_bot_data_format format); void qa_weapons_result_destroy(qa_weapons_result *r);
bool qa_parse_items(qa_items_result *out, const char *text); void qa_items_result_destroy(qa_items_result *r);
bool qa_parse_monsters(qa_monsters_result *out, const char *text); void qa_monsters_result_destroy(qa_monsters_result *r);
bool qa_parse_interactables(qa_interactables_result *out, const char *text); void qa_interactables_result_destroy(qa_interactables_result *r);
bool qa_parse_game_rules(qa_game_rules_result *out, const char *text); void qa_game_rules_result_destroy(qa_game_rules_result *r);
bool qa_parse_teams(qa_teams_result *out, const char *text); void qa_teams_result_destroy(qa_teams_result *r);
bool qa_parse_chats(qa_chats_result *out, const char *text); void qa_chats_result_destroy(qa_chats_result *r);
bool qa_parse_dangers(qa_dangers_result *out, const char *text); void qa_dangers_result_destroy(qa_dangers_result *r);
bool qa_parse_bot_settings(qa_bot_settings_result *out, const char *text); void qa_bot_settings_result_destroy(qa_bot_settings_result *r);
#endif
