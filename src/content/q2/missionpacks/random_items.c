/* Port of src/content/q2/missionpacks/random-items.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/items.h"

#include <math.h>
#include <string.h>

#include "missionpacks_internal.h"

typedef enum category { CAT_WEAPON, CAT_AMMO, CAT_POWER, CAT_KEY, CAT_NONE } category;
typedef struct table { const char *const *rows[4]; size_t counts[4]; } table;

static const char *const classic_weapon[] = {"weapon_blaster", "weapon_shotgun", "weapon_supershotgun", "weapon_machinegun", "weapon_chaingun", "weapon_etf_rifle", "weapon_grenadelauncher", "weapon_proxlauncher", "weapon_rocketlauncher", "weapon_hyperblaster", "weapon_plasmabeam", "weapon_railgun", "weapon_bfg", "weapon_chainfist"};
static const char *const classic_ammo[] = {"ammo_grenades", "ammo_shells", "ammo_bullets", "ammo_cells", "ammo_rockets", "ammo_slugs", "ammo_flechettes", "ammo_prox", "ammo_tesla"};
static const char *const classic_power[] = {"ammo_nuke", "item_quad", "item_invulnerability", "item_silencer", "item_breather", "item_enviro", "item_ir_goggles", "item_double", "item_compass", "item_sphere_vengeance", "item_sphere_hunter", "item_sphere_defender", "item_doppleganger"};
static const char *const classic_key[] = {"key_data_cd", "key_power_cube", "key_pyramid", "key_data_spinner", "key_pass", "key_blue_key", "key_red_key", "key_commander_head", "key_airstrike_target", "key_nuke_container", "key_nuke"};
static const char *const rr_weapon[] = {"weapon_chainfist", "weapon_shotgun", "weapon_supershotgun", "weapon_machinegun", "weapon_etf_rifle", "weapon_chaingun", "weapon_grenadelauncher", "weapon_proxlauncher", "weapon_rocketlauncher", "weapon_hyperblaster", "weapon_boomer", "weapon_plasmabeam", "weapon_railgun", "weapon_phalanx", "weapon_bfg", "weapon_disintegrator"};
static const char *const rr_ammo[] = {"ammo_grenades", "ammo_trap", "ammo_tesla", "ammo_shells", "ammo_bullets", "ammo_cells", "ammo_rockets", "ammo_slugs", "ammo_magslug", "ammo_flechettes", "ammo_prox", "ammo_disruptor"};
static const char *const rr_power[] = {"ammo_nuke", "item_quad", "item_quadfire", "item_invulnerability", "item_invisibility", "item_silencer", "item_breather", "item_enviro", "item_adrenaline", "item_bandolier", "item_pack", "item_ir_goggles", "item_double", "item_sphere_vengeance", "item_sphere_hunter", "item_sphere_defender", "item_doppleganger", "item_health_mega"};
static const char *const rr_key[] = {"key_data_cd", "key_power_cube", "key_explosive_charges", "key_yellow_key", "key_power_core", "key_pyramid", "key_data_spinner", "key_pass", "key_blue_key", "key_red_key", "key_green_key", "key_commander_head", "key_airstrike_target", "key_nuke_container", "key_nuke"};
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const table classic_table = {{classic_weapon, classic_ammo, classic_power, classic_key},
                                    {N(classic_weapon), N(classic_ammo), N(classic_power), N(classic_key)}};
static const table rr_table = {{rr_weapon, rr_ammo, rr_power, rr_key}, {N(rr_weapon), N(rr_ammo), N(rr_power), N(rr_key)}};

static bool in_list(const char *value, const char *const *list, size_t count) {
  for (size_t i = 0; i < count; i++) if (strcmp(value, list[i]) == 0) return true;
  return false;
}
static category find_category(const char *classname, const table *t) {
  for (int kind = 0; kind < 4; kind++) if (in_list(classname, t->rows[kind], t->counts[kind])) return (category)kind;
  return CAT_NONE;
}
static double rr_integer(qa_q2_game_services *game, double count) {
  return game->host->rerelease_random != NULL
      ? (double)qa_q2_rerelease_random_integer_max(game->host->rerelease_random, (int64_t)count)
      : floor(q2mp_rnd(game) * count);
}
static bool is(const char *a, const char *b) { return strcmp(a, b) == 0; }

const char *qa_q2mp_random_item(qa_q2_entity *entity, const qa_q2_item_definition *item, qa_q2_game_services *game,
                                qa_q2mp_random_item_settings settings) {
  if (!settings.enabled) return NULL;
  if (game->options.edition == QA_Q2_EDITION_CLASSIC) {
    if (is(item->kind, "power-armor")) return NULL;
    if (is(item->kind, "health") || is(item->classname, "item_adrenaline")) {
      if (is(entity->classname, "item_health_small")) return NULL;
      double chance = q2mp_rnd(game);
      return chance < 0.6 ? "item_health" : chance < 0.9 ? "item_health_large" : chance < 0.99 ? "item_adrenaline" : "item_health_mega";
    }
    if (is(item->kind, "armor")) {
      double chance = q2mp_rnd(game);
      return chance < 0.6 ? "item_armor_jacket" : chance < 0.9 ? "item_armor_combat" : "item_armor_body";
    }
    if (is(item->kind, "shard")) return NULL;
    category kind = find_category(item->classname, &classic_table);
    if (kind == CAT_NONE) return NULL;
    static const char *const spheres[] = {"item_sphere_vengeance", "item_sphere_hunter", "item_spehre_defender"};
    static const char *const mines[] = {"ammo_prox", "ammo_tesla"};
    if ((settings.no_spheres && in_list(entity->classname, spheres, N(spheres))) ||
        (settings.no_nukes && is(entity->classname, "ammo_nuke")) ||
        (settings.no_mines && in_list(entity->classname, mines, N(mines)))) return NULL;
    const char *const *choices = classic_table.rows[kind];
    double pick = ceil(q2mp_rnd(game) * (double)classic_table.counts[kind]);
    return pick >= 1 && pick <= (double)classic_table.counts[kind] ? choices[(size_t)pick - 1] : NULL;
  }
  static const char *const flags[] = {"item_flag_team1", "item_flag_team2", "dm_tag_token"};
  if (in_list(item->classname, flags, N(flags))) return NULL;
  if (is(item->classname, "item_health_small") || is(item->classname, "item_armor_shard"))
    return rr_integer(game, 2) == 0 ? "item_health_small" : "item_armor_shard";
  if (is(item->classname, "item_health") || is(item->classname, "item_health_large"))
    return q2mp_rnd(game) < 0.6 ? "item_health" : "item_health_large";
  static const char *const armors[] = {"item_armor_jacket", "item_armor_combat", "item_armor_body", "item_power_screen", "item_power_shield"};
  if (in_list(item->classname, armors, N(armors))) {
    double chance = q2mp_rnd(game);
    return chance < 0.4 ? "item_armor_jacket" : chance < 0.6 ? "item_armor_combat" : chance < 0.8 ? "item_armor_body"
                        : chance < 0.9 ? "item_power_screen" : "item_power_shield";
  }
  if (is(item->classname, "item_ancient_head") || is(item->classname, "item_legacy_head")) {
    static const char *const choices[] = {"item_health_small", "item_health", "item_health_large"};
    double index = rr_integer(game, 3);
    return index >= 0 && index < 3 ? choices[(size_t)index] : NULL;
  }
  category kind = is(item->classname, "weapon_blaster") || is(item->classname, "weapon_grapple")
      ? CAT_WEAPON : find_category(item->classname, &rr_table);
  if (kind == CAT_NONE) return NULL;
  static const char *const rr_mines[] = {"ammo_prox", "ammo_tesla", "ammo_trap", "weapon_proxlauncher"};
  const char *choices[18];
  size_t count = 0;
  for (size_t i = 0; i < rr_table.counts[kind]; i++) {
    const char *classname = rr_table.rows[kind][i];
    if (!((settings.no_spheres && strncmp(classname, "item_sphere_", 12) == 0) ||
          (settings.no_nukes && is(classname, "ammo_nuke")) ||
          (settings.no_mines && in_list(classname, rr_mines, N(rr_mines)))))
      choices[count++] = classname;
  }
  if (count == 0) return NULL;
  double index = rr_integer(game, (double)count);
  return index >= 0 && index < (double)count ? choices[(size_t)index] : NULL;
}
