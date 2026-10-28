/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/data/knowledge.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *const QA_ITEM_FLAG_HEALTH = "health", *const QA_ITEM_FLAG_AMMO = "ammo", *const QA_ITEM_FLAG_ARMOR = "armor", *const QA_ITEM_FLAG_WEAPON = "weapon", *const QA_ITEM_FLAG_POWERUP = "powerup", *const QA_ITEM_FLAG_DROPPED = "dropped", *const QA_ITEM_FLAG_MEGA_ITEM = "mega_item", *const QA_ITEM_FLAG_BACKPACK = "backpack", *const QA_ITEM_FLAG_ZAP_PROOF = "zap_proof", *const QA_ITEM_FLAG_CHECK_ITEMS = "check_items", *const QA_ITEM_FLAG_OBJECTIVE = "objective", *const QA_ITEM_FLAG_RUNE = "rune", *const QA_ITEM_FLAG_KEY = "key", *const QA_ITEM_FLAG_USABLE = "usable", *const QA_ITEM_FLAG_IGNORE_LIMITS = "ignore_limits";
const char *const QA_WEAPON_FLAG_MELEE = "melee", *const QA_WEAPON_FLAG_HITSCAN = "hitscan", *const QA_WEAPON_FLAG_PROJECTILE = "projectile", *const QA_WEAPON_FLAG_PARABOLIC = "parabolic", *const QA_WEAPON_FLAG_EXPLOSIVE = "explosive", *const QA_WEAPON_FLAG_STICKY = "sticky", *const QA_WEAPON_FLAG_ELECTRIC = "electric", *const QA_WEAPON_FLAG_STARTING = "starting", *const QA_WEAPON_FLAG_INITIAL = "initial", *const QA_WEAPON_FLAG_SUITABLE = "suitable", *const QA_WEAPON_FLAG_ROCKET_JUMP = "rocket_jump";
const char *const QA_MONSTER_FLAG_MELEE = "melee", *const QA_MONSTER_FLAG_RANGED = "ranged", *const QA_MONSTER_FLAG_AQUATIC = "aquatic", *const QA_MONSTER_FLAG_FLYER = "flyer", *const QA_MONSTER_FLAG_TANK = "tank", *const QA_MONSTER_FLAG_SPONGE = "sponge", *const QA_MONSTER_FLAG_TURRET = "turret", *const QA_MONSTER_FLAG_NEUTRAL = "neutral";
const char *const QA_DANGER_FLAG_TAKES_DAMAGE = "takes_damage", *const QA_DANGER_FLAG_IGNORE_IF_TEAM = "ignore_if_team", *const QA_DANGER_FLAG_IGNORE_IF_COOP = "ignore_if_coop", *const QA_DANGER_FLAG_IGNORE_IF_NO_TEAM_DMG = "ignore_if_no_team_dmg", *const QA_DANGER_FLAG_TIMER_BASED = "timer_based", *const QA_DANGER_FLAG_NAV_HAZARD = "nav_hazard", *const QA_DANGER_FLAG_CHECK_VELOCITY = "check_velocity", *const QA_DANGER_FLAG_CHECK_BEAM = "check_beam", *const QA_DANGER_FLAG_EXPLODING_BARREL = "exploding_barrel", *const QA_DANGER_FLAG_UPDATE_PVS = "update_pvs";
const char *const QA_INTERACTION_PUSH = "push", *const QA_INTERACTION_SHOOT = "shoot", *const QA_INTERACTION_USE = "use", *const QA_INTERACTION_RIDE = "ride", *const QA_INTERACTION_TOUCH = "touch";
static bool has_flag(const char **flags, size_t n, const char *f) { for (size_t i = 0; i < n; i++) if (!strcmp(flags[i], f)) return true;
return false; }
static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p;
}
static void take(qa_bot_knowledge *k, const char *label, char **errs, size_t n) { for (size_t i = 0; i < n; i++) { char buf[2048]; snprintf(buf, sizeof buf, "%s: %s", label, errs[i]); k->errors = realloc(k->errors, (k->error_count + 1) * sizeof *k->errors); k->errors[k->error_count++] = dups(buf); } }
static void push_err(qa_bot_knowledge *k, const char *msg) { k->errors = realloc(k->errors, (k->error_count + 1) * sizeof *k->errors); k->errors[k->error_count++] = dups(msg); }
bool qa_bot_knowledge_init(qa_bot_knowledge *k, qa_bot_data_files f, qa_bot_data_format format, qa_weapon_number_fn wn, void *wn_ctx) {
  memset(k, 0, sizeof *k); k->format = format;
  if (!qa_parse_characters(&k->chars, f.characters)) return false;
  take(k, "characters.txt", k->chars.errors, k->chars.error_count);
  if (!qa_parse_weapons(&k->weapons_r, f.weapons, format)) return false;
  take(k, "weapons.txt", k->weapons_r.errors, k->weapons_r.error_count);
  k->weapon_count = k->weapons_r.entry_count; k->weapons = calloc(k->weapon_count ? k->weapon_count : 1, sizeof *k->weapons);
  for (size_t i = 0; i < k->weapon_count; i++) { qa_weapon_entry *w = &k->weapons_r.entries[i];
    if (w->identity.is_q2_classname) { double bit = 0; bool ok = wn && wn(wn_ctx, w->identity.classname, &bit);
      if (!ok) { char buf[1024]; snprintf(buf, sizeof buf, "weapons.txt: no inventory binding for Q2 classname \"%s\"", w->identity.classname); push_err(k, buf); } w->number = ok ? bit : 0; }
    k->weapons[i].entry = w; k->weapons[i].is_melee = has_flag(w->flags, w->flag_count, "melee"); k->weapons[i].is_electric = has_flag(w->flags, w->flag_count, "electric");
    k->weapons[i].needs_ammo = w->identity.is_q2_classname ? (strcmp(w->ammo_name, "") && strcmp(w->ammo_name, "none")) : (strcmp(w->ammo, "") && strcmp(w->ammo, "none")); }
  if (!qa_parse_items(&k->items_r, f.items)) return false;
  take(k, "items.txt", k->items_r.errors, k->items_r.error_count);
  k->item_count = k->items_r.entry_count; k->items = calloc(k->item_count ? k->item_count : 1, sizeof *k->items);
  for (size_t i = 0; i < k->item_count; i++) { qa_item_entry *it = &k->items_r.entries[i]; qa_bot_item_rec *r = &k->items[i]; r->entry = it;
    r->is_health = has_flag(it->flags, it->flag_count, "health"); r->is_armor = has_flag(it->flags, it->flag_count, "armor"); r->is_ammo = has_flag(it->flags, it->flag_count, "ammo");
    r->is_weapon = has_flag(it->flags, it->flag_count, "weapon"); r->is_powerup = has_flag(it->flags, it->flag_count, "powerup"); r->is_mega = has_flag(it->flags, it->flag_count, "mega_item");
    r->mega_spawnflag = 0; for (size_t j = 0; j < it->spawnflag_count; j++) if (!strcmp(it->spawnflags[j].name, "mega_item")) { r->mega_spawnflag = it->spawnflags[j].bit; break; } }
  if (!qa_parse_dangers(&k->dangers_r, f.dangers ? f.dangers : "")) return false;
  take(k, "dangers.txt", k->dangers_r.errors, k->dangers_r.error_count);
  if (!qa_parse_monsters(&k->monsters_r, f.monsters)) return false;
  take(k, "monsters.txt", k->monsters_r.errors, k->monsters_r.error_count);
  if (!qa_parse_interactables(&k->inter_r, f.interactables)) return false;
  take(k, "interactables.txt", k->inter_r.errors, k->inter_r.error_count);
  if (!qa_parse_game_rules(&k->rules_r, f.game_rules)) return false;
  take(k, "game_rules.txt", k->rules_r.errors, k->rules_r.error_count);
  if (!qa_parse_teams(&k->teams_r, f.teams)) return false;
  take(k, "teams.txt", k->teams_r.errors, k->teams_r.error_count);
  if (!qa_parse_chats(&k->chats_r, f.chats)) return false;
  take(k, "chats.txt", k->chats_r.errors, k->chats_r.error_count);
  if (!qa_parse_bot_settings(&k->settings_r, f.settings)) return false;
  take(k, "settings.txt", k->settings_r.errors, k->settings_r.error_count);
  return true;
}
void qa_bot_knowledge_destroy(qa_bot_knowledge *k) { if (!k) return;
free(k->weapons); free(k->items); for (size_t i = 0; i < k->error_count; i++) free(k->errors[i]); free(k->errors);
  qa_characters_result_destroy(&k->chars); qa_weapons_result_destroy(&k->weapons_r); qa_items_result_destroy(&k->items_r); qa_dangers_result_destroy(&k->dangers_r);
  qa_monsters_result_destroy(&k->monsters_r); qa_interactables_result_destroy(&k->inter_r); qa_game_rules_result_destroy(&k->rules_r); qa_teams_result_destroy(&k->teams_r);
  qa_chats_result_destroy(&k->chats_r); qa_bot_settings_result_destroy(&k->settings_r); memset(k, 0, sizeof *k); }
static const qa_bot_weapon_rec *weapon_list(const qa_bot_knowledge *k, size_t *n) { if (k->replaced) { *n = k->replaced_count; return k->replaced;
} *n = k->weapon_count; return k->weapons; }
void qa_bot_knowledge_replace_weapons(qa_bot_knowledge *k, qa_bot_weapon_rec *w, size_t n) { k->replaced = w; k->replaced_count = n; }
const qa_bot_skill_settings *qa_bot_knowledge_get_skill(const qa_bot_knowledge *k, const char *name) { const qa_bot_skill_settings *r = NULL; for (size_t i = 0; i < k->settings_r.skill_count; i++) if (!strcmp(k->settings_r.skills[i].skill, name)) r = &k->settings_r.skills[i]; return r;
}
const char *qa_bot_knowledge_skill_name(const qa_bot_knowledge *k, size_t index, size_t *out_count) { if (out_count) *out_count = k->settings_r.skill_count; return index < k->settings_r.skill_count ? k->settings_r.skills[index].skill : NULL; }
const qa_bot_item_rec *qa_bot_knowledge_get_item(const qa_bot_knowledge *k, const char *cn) { const qa_bot_item_rec *r = NULL; for (size_t i = 0; i < k->item_count; i++) if (!strcmp(k->items[i].entry->name, cn)) r = &k->items[i]; return r;
}
const qa_monster_entry *qa_bot_knowledge_get_monster(const qa_bot_knowledge *k, const char *cn) { const qa_monster_entry *r = NULL; for (size_t i = 0; i < k->monsters_r.entry_count; i++) if (!strcmp(k->monsters_r.entries[i].classname, cn)) r = &k->monsters_r.entries[i]; return r;
}
const qa_bot_weapon_rec *qa_bot_knowledge_weapon_by_name(const qa_bot_knowledge *k, const char *name) { size_t n; const qa_bot_weapon_rec *ws = weapon_list(k, &n), *r = NULL; for (size_t i = 0; i < n; i++) if (!strcmp(ws[i].entry->name, name)) r = &ws[i]; return r;
}
const qa_danger_entry *qa_bot_knowledge_get_danger(const qa_bot_knowledge *k, const char *cn) { const qa_danger_entry *r = NULL; for (size_t i = 0; i < k->dangers_r.entry_count; i++) if (!strcmp(k->dangers_r.entries[i].name, cn)) r = &k->dangers_r.entries[i]; return r;
}
const qa_bot_weapon_rec *qa_bot_knowledge_weapon_by_number(const qa_bot_knowledge *k, double num) { size_t n; const qa_bot_weapon_rec *ws = weapon_list(k, &n), *r = NULL; for (size_t i = 0; i < n; i++) if (ws[i].entry->number == num) r = &ws[i]; if (num == 0) return NULL;
return r; }
static bool eq_lower(const char *a, const char *b) { while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
a++; b++; } return *a == *b; }
const qa_character_entry *qa_bot_knowledge_get_character(const qa_bot_knowledge *k, const char *name) { for (size_t i = 0; i < k->chars.entry_count; i++) if (eq_lower(k->chars.entries[i].name, name)) return &k->chars.entries[i];
return NULL; }
int64_t qa_bot_knowledge_team_index(const qa_bot_knowledge *k, double v) { size_t cap = k->format == QA_BOT_DATA_Q2 ? 2 : 4; for (size_t i = 0; i < k->teams_r.entry_count && i < cap; i++) if (k->teams_r.entries[i].value == v) return (int64_t)i;
return -1; }
const qa_chat_entry **qa_bot_knowledge_chats_of_type(const qa_bot_knowledge *k, const char *type, size_t *out_count) { const qa_chat_entry **r = malloc((k->chats_r.entry_count ? k->chats_r.entry_count : 1) * sizeof *r); size_t c = 0; for (size_t i = 0; i < k->chats_r.entry_count; i++) if (!strcmp(k->chats_r.entries[i].type, type)) r[c++] = &k->chats_r.entries[i]; *out_count = c; return r;
}
qa_bot_game_mode qa_bot_knowledge_game_mode(const qa_bot_knowledge *k, double (*cv)(void *, const char *), void *ctx) {
  qa_bot_game_mode mode; memset(&mode, 0, sizeof mode);
  if (k->format == QA_BOT_DATA_Q2) { mode.game_type_owned = dups("dm"); mode.game_type = mode.game_type_owned; mode.has_has_teams = mode.has_team_damage = true;
    bool a_gt = false, a_ws = false, a_ht = false, a_td = false;
    for (size_t i = 0; i < k->rules_r.entry_count; i++) { const qa_game_rule_entry *rule = &k->rules_r.entries[i];
      if (rule->only_game_type && strcmp(mode.game_type, rule->only_game_type)) continue;
      if (rule->not_game_type && !strcmp(mode.game_type, rule->not_game_type)) continue;
      if (!rule->condition_count) continue;
      bool ok;
      if (rule->logic_is_and) { ok = true; for (size_t j = 0; j < rule->condition_count; j++) if (cv(ctx, rule->conditions[j].cvar) != rule->conditions[j].value) ok = false; }
      else { ok = false; for (size_t j = 0; j < rule->condition_count; j++) if (cv(ctx, rule->conditions[j].cvar) == rule->conditions[j].value) ok = true; }
      if (!ok) continue;
      if (strcmp(rule->game_type, "") && !a_gt) { free(mode.game_type_owned); mode.game_type_owned = dups(rule->game_type); mode.game_type = mode.game_type_owned; a_gt = true; }
      if (rule->weapon_stay_specified && !a_ws) { mode.weapon_stay = rule->weapon_stay; a_ws = true; }
      if (rule->has_teams_present && !a_ht) { mode.has_teams = rule->has_teams; a_ht = true; }
      if (rule->team_damage_present && !a_td) { mode.team_damage = rule->team_damage; a_td = true; } }
    return mode; }
  for (size_t i = 0; i < k->rules_r.entry_count; i++) { const qa_game_rule_entry *rule = &k->rules_r.entries[i];
    if (!strcmp(rule->cvar, "")) continue;
    if (cv(ctx, rule->cvar) != rule->value) continue;
    const char *type = rule->game_type; if (!strcmp(type, QA_BOT_GAME_TYPE_DEATHMATCH) && cv(ctx, "teamplay") == 1) type = QA_BOT_GAME_TYPE_TDM;
    mode.game_type_owned = dups(type); mode.game_type = mode.game_type_owned; mode.weapon_stay = rule->weapon_stay; return mode; }
  mode.game_type_owned = dups(QA_BOT_GAME_TYPE_DEATHMATCH); mode.game_type = mode.game_type_owned; return mode;
}
void qa_bot_game_mode_destroy(qa_bot_game_mode *m) { free(m->game_type_owned); m->game_type_owned = NULL; m->game_type = NULL; }
const char *qa_bot_knowledge_interaction_for(const qa_bot_knowledge *k, const char *cn, int64_t spawnflags, bool has_health, bool has_targetname) {
  for (size_t i = 0; i < k->inter_r.entry_count; i++) { const qa_interactable_entry *e = &k->inter_r.entries[i]; if (strcmp(e->name, cn)) continue;
    bool conds[3]; size_t nc = 0;
    if (e->has_spawnflags) conds[nc++] = (spawnflags & (int64_t)e->spawnflags) == (int64_t)e->spawnflags;
    if (e->has_health) conds[nc++] = e->health == has_health;
    if (e->has_targetname) conds[nc++] = e->targetname == has_targetname;
    if (!nc) return e->interaction;
    bool ok; if (e->logic_op && !strcmp(e->logic_op, "or")) { ok = false; for (size_t j = 0; j < nc; j++) ok = ok || conds[j]; } else { ok = true; for (size_t j = 0; j < nc; j++) ok = ok && conds[j]; }
    if (ok) return e->interaction;
    }
  return NULL;
}
bool qa_choose_weapon(const qa_bot_weapon_rec *ws, size_t count, qa_bot_weapon_context ctx, qa_bot_weapon_pick *out) {
  const qa_bot_weapon_rec *best = NULL;
  for (size_t i = 0; i < count; i++) { const qa_bot_weapon_rec *w = &ws[i]; const qa_weapon_entry *e = w->entry;
    if (e->priority <= 0) continue;
    if (w->is_melee && !ctx.allow_melee) continue;
    if ((ctx.items & (int64_t)e->number) == 0) continue;
    if (w->needs_ammo) { double have = qa_bot_ammo_get(ctx.ammo, ctx.ammo_count, e->ammo_name); if (have < fmax(1, e->min_ammo)) continue; }
    if (ctx.range < e->min_range) continue;
    if (e->max_range > 0 && ctx.range > e->max_range) continue;
    if (e->max_height > 0 && ctx.height_delta > e->max_height) continue;
    if (e->min_height > 0 && -ctx.height_delta > e->min_height) continue;
    if (w->is_electric && ctx.in_water && !(ctx.has_protection && ctx.target_in_water)) continue;
    if (!best || e->priority > best->entry->priority) best = w; }
  if (!best) return false;
  out->weapon = best; out->number = best->entry->number; return true;
}
static void strip_us(const char *in, char *out, size_t cap) { size_t j = 0; for (size_t i = 0; in[i] && j + 1 < cap; i++) if (in[i] != '_') out[j++] = in[i]; out[j] = 0; }
const qa_bot_weapon_rec *qa_weapon_for_item(const qa_bot_weapon_rec *ws, size_t count, const char *item_name) {
  for (size_t i = 0; i < count; i++) if (!strcmp(ws[i].entry->name, item_name)) return &ws[i];
  const char *base = item_name; if (!strncmp(base, "weapon_", 7)) base += 7;
  char key[512]; strip_us(base, key, sizeof key); size_t klen = strlen(key);
  const qa_bot_weapon_rec *best = NULL; size_t best_len = 0;
  for (size_t i = 0; i < count; i++) { const qa_weapon_entry *e = ws[i].entry; if (e->identity.is_q2_classname || !strcmp(e->name, "")) continue;
    char name[512]; strip_us(e->name, name, sizeof name); size_t nl = strlen(name);
    bool pre = (nl <= klen && !strncmp(key, name, nl)) || (klen <= nl && !strncmp(name, key, klen)); if (!pre) continue;
    if (!best || nl > best_len) { best = &ws[i]; best_len = nl; } }
  return best;
}
double qa_bot_item_value(const qa_bot_item_rec *item, qa_bot_item_context ctx) {
  const qa_item_entry *e = item->entry;
  bool mega = item->is_mega || (item->mega_spawnflag != 0 && (ctx.spawnflags & (int64_t)item->mega_spawnflag) == (int64_t)item->mega_spawnflag);
  if (item->is_powerup || mega) { if (!ctx.allow_power_items) return 0;
  return item->is_powerup ? 1000 : 700; }
  if (item->is_weapon) { const qa_bot_weapon_rec *w = qa_weapon_for_item(ctx.weapons, ctx.weapon_count, e->name); double number = w ? w->entry->number : 0;
    bool owned = number != 0 && (ctx.items & (int64_t)number) != 0; if (owned && !ctx.weapon_stay) return 200;
    if (owned) return 0;
    return 600 + (w ? w->entry->priority : 0) * 10; }
  if (item->is_health) { double missing = fmax(0, ctx.max_health - ctx.health); if (missing <= 0) return 0;
  return 100 + missing * 4; }
  if (item->is_armor) { if (ctx.armor >= 200) return 0;
  return 300 + (200 - ctx.armor); }
  if (item->is_ammo) { double want = 0;
    for (size_t i = 0; i < ctx.weapon_count; i++) { const qa_bot_weapon_rec *w = &ctx.weapons[i]; if (!w->needs_ammo) continue;
      if (w->entry->identity.is_q2_classname ? strcmp(e->name, w->entry->ammo_name) : !has_flag(e->flags, e->flag_count, w->entry->ammo)) continue;
      double have = qa_bot_ammo_get(ctx.ammo, ctx.ammo_count, w->entry->ammo_name); double room = fmax(0, w->entry->max_ammo - have);
      bool owned = (ctx.items & (int64_t)w->entry->number) != 0; want = fmax(want, (owned ? 150 : 40) * (w->entry->max_ammo > 0 ? room / w->entry->max_ammo : 0)); }
    return want; }
  if (has_flag(e->flags, e->flag_count, "backpack")) return 120;
  if (has_flag(e->flags, e->flag_count, "objective")) { if (ctx.item_team <= 0 || ctx.team <= 0) return 900;
  if (ctx.item_team != ctx.team) return 900;
  return ctx.objective_at_home ? 0 : 950; }
  if (has_flag(e->flags, e->flag_count, "rune")) return 900;
  return 0;
}
