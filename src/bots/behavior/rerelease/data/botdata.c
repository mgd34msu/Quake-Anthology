/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { char **v; size_t n, cap; } Errs;
static void errf(Errs *e, const char *fmt, const char *a, int64_t line) { char buf[1024]; if (a) snprintf(buf, sizeof buf, fmt, (long long)line, a); else snprintf(buf, sizeof buf, fmt, (long long)line);
  if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 8; e->v = realloc(e->v, e->cap * sizeof *e->v); } size_t l = strlen(buf); e->v[e->n] = malloc(l + 1); memcpy(e->v[e->n], buf, l + 1); e->n++; }
static void errs_from_blocks(Errs *e, qa_block_parse_result *p) { for (size_t i = 0; i < p->error_count; i++) errf(e, "line %lld: %s", p->errors[i].message, p->errors[i].line); }
static void errs_free(char **v, size_t n) { for (size_t i = 0; i < n; i++) free(v[i]); free(v); }
static bool exp_str(Errs *e, const qa_block_field *f, const char **o) { if (qa_field_string((const char *const *)f->values, f->value_count, o)) return true; errf(e, "line %lld: \"%s\" expects a single string", f->key, f->line); return false; }
static bool exp_num(Errs *e, const qa_block_field *f, double *o) { if (qa_field_number((const char *const *)f->values, f->value_count, o)) return true; errf(e, "line %lld: \"%s\" expects a single number", f->key, f->line); return false; }
static bool exp_bool(Errs *e, const qa_block_field *f, bool *o) { if (qa_field_bool((const char *const *)f->values, f->value_count, o)) return true; errf(e, "line %lld: \"%s\" expects \"true\" or \"false\"", f->key, f->line); return false; }
static void unknown(Errs *e, qa_bot_source_entry_base *b, const qa_block_field *f) { b->unknown = realloc((void *)b->unknown, (b->unknown_count + 1) * sizeof *b->unknown); b->unknown[b->unknown_count++] = f; errf(e, "line %lld: unknown key \"%s\"", f->key, f->line); }
static void base_free(qa_bot_source_entry_base *b) { free((void *)b->unknown); }
void qa_bot_aiming_settings_init(qa_bot_aiming_settings *s) { memset(s, 0, sizeof *s); }
void qa_bot_behavior_settings_init(qa_bot_behavior_settings *s) { memset(s, 0, sizeof *s); s->combat_max_item_dist = 512; s->combat_min_health_pct = 100; s->combat_min_ammo_pct = 100; s->combat_min_armor_pct = 100; s->combat_grab_weapons = true; }
void qa_bot_movement_settings_init(qa_bot_movement_settings *s) { memset(s, 0, sizeof *s); }
void qa_bot_senses_settings_init(qa_bot_senses_settings *s) { memset(s, 0, sizeof *s); }
void qa_bot_weapon_sense_settings_init(qa_bot_weapon_sense_settings *s) { memset(s, 0, sizeof *s); }
void qa_bot_skill_settings_init(qa_bot_skill_settings *s) { memset(s, 0, sizeof *s); qa_bot_aiming_settings_init(&s->aiming); qa_bot_behavior_settings_init(&s->behaviors); qa_bot_movement_settings_init(&s->movement); qa_bot_senses_settings_init(&s->senses); qa_bot_weapon_sense_settings_init(&s->weapons); s->skill = ""; }
#define BEGIN(ResultT, EntryT) memset(out, 0, sizeof *out); if (!qa_parse_blocks(&out->parsed, text)) return false; Errs errs = {0}; errs_from_blocks(&errs, &out->parsed); out->entries = calloc(out->parsed.block_count ? out->parsed.block_count : 1, sizeof(EntryT)); out->entry_count = out->parsed.block_count
#define END() out->errors = errs.v; out->error_count = errs.n; return true
bool qa_parse_characters(qa_characters_result *out, const char *text) { BEGIN(qa_characters_result, qa_character_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_character_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->fun_name = en->name = en->skin = en->dogtag = "";
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n;
      if (!strcmp(f->key, "fun_name")) { if (exp_str(&errs, f, &s)) en->fun_name = s; } else if (!strcmp(f->key, "name")) { if (exp_str(&errs, f, &s)) en->name = s; }
      else if (!strcmp(f->key, "skin")) { if (exp_str(&errs, f, &s)) en->skin = s; } else if (!strcmp(f->key, "dogtag")) { if (exp_str(&errs, f, &s)) en->dogtag = s; }
      else if (!strcmp(f->key, "shirt_color")) { if (exp_num(&errs, f, &n)) en->shirt_color = n; } else if (!strcmp(f->key, "pants_color")) { if (exp_num(&errs, f, &n)) en->pants_color = n; }
      else unknown(&errs, &en->base, f); } } END(); }
void qa_characters_result_destroy(qa_characters_result *r) { for (size_t i = 0; i < r->entry_count; i++) base_free(&r->entries[i].base); free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_weapons(qa_weapons_result *out, const char *text, qa_bot_data_format format) { BEGIN(qa_weapons_result, qa_weapon_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_weapon_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->name = en->ammo = en->ammo_name = en->aim_point = "";
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n;
      if (!strcmp(f->key, "name")) { if (exp_str(&errs, f, &s)) en->name = s; } else if (!strcmp(f->key, "speed")) { if (exp_num(&errs, f, &n)) en->speed = n; }
      else if (!strcmp(f->key, "ideal_fov")) { if (exp_num(&errs, f, &n)) en->ideal_fov = n; } else if (!strcmp(f->key, "trigger_hold")) { if (exp_num(&errs, f, &n)) en->trigger_hold = n; }
      else if (!strcmp(f->key, "trigger_cooldown")) { if (exp_num(&errs, f, &n)) en->trigger_cooldown = n; }
      else if (!strcmp(f->key, "trigger_type")) { if (exp_str(&errs, f, &s)) { if (!strcmp(s, "continuous")) en->trigger_hold_and_release = false; else if (!strcmp(s, "hold_and_release")) en->trigger_hold_and_release = true; else errf(&errs, "line %lld: unsupported trigger_type \"%s\"", s, f->line); } }
      else if (!strcmp(f->key, "number")) { if (exp_num(&errs, f, &n)) en->number = n; } else if (!strcmp(f->key, "damage")) { if (exp_num(&errs, f, &n)) en->damage = n; }
      else if (!strcmp(f->key, "min_range")) { if (exp_num(&errs, f, &n)) en->min_range = n; } else if (!strcmp(f->key, "max_range")) { if (exp_num(&errs, f, &n)) en->max_range = n; }
      else if (!strcmp(f->key, "min_height")) { if (exp_num(&errs, f, &n)) en->min_height = n; } else if (!strcmp(f->key, "max_height")) { if (exp_num(&errs, f, &n)) en->max_height = n; }
      else if (!strcmp(f->key, "priority")) { if (exp_num(&errs, f, &n)) en->priority = n; } else if (!strcmp(f->key, "ammo")) { if (exp_str(&errs, f, &s)) en->ammo = s; }
      else if (!strcmp(f->key, "ammo_name")) { if (exp_str(&errs, f, &s)) en->ammo_name = s; } else if (!strcmp(f->key, "min_ammo")) { if (exp_num(&errs, f, &n)) en->min_ammo = n; }
      else if (!strcmp(f->key, "max_ammo")) { if (exp_num(&errs, f, &n)) en->max_ammo = n; } else if (!strcmp(f->key, "flags")) { free((void *)en->flags); en->flags = qa_field_flag_list((const char *const *)f->values, f->value_count, &en->flag_count); }
      else if (!strcmp(f->key, "aim_point")) { if (exp_str(&errs, f, &s)) en->aim_point = s; } else unknown(&errs, &en->base, f); }
    en->identity.is_q2_classname = format != QA_BOT_DATA_Q1; en->identity.bit = en->number; en->identity.classname = en->name; } END(); }
void qa_weapons_result_destroy(qa_weapons_result *r) { for (size_t i = 0; i < r->entry_count; i++) { base_free(&r->entries[i].base); free((void *)r->entries[i].flags); } free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_items(qa_items_result *out, const char *text) { BEGIN(qa_items_result, qa_item_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_item_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->sight_dist = 3.4028234663852886e38; en->name = "";
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n;
      if (!strcmp(f->key, "name")) { if (exp_str(&errs, f, &s)) en->name = s; } else if (!strcmp(f->key, "sight_dist")) { if (exp_num(&errs, f, &n)) en->sight_dist = n; }
      else if (!strcmp(f->key, "flags")) { free((void *)en->flags); en->flags = qa_field_flag_list((const char *const *)f->values, f->value_count, &en->flag_count); }
      else if (!strcmp(f->key, "spawnflags")) { if (f->value_count == 3 && !strcmp(f->values[1], "=")) { double bit = strtod(f->values[0], NULL); char *e = NULL; bit = strtod(f->values[0], &e);
          if (e && *e) bit = NAN;
          if (bit != trunc(bit) || isnan(bit)) errf(&errs, "line %lld: \"spawnflags\" bit \"%s\" is not an integer", f->values[0], f->line);
          else { en->spawnflags = realloc(en->spawnflags, (en->spawnflag_count + 1) * sizeof *en->spawnflags); en->spawnflags[en->spawnflag_count++] = (qa_item_spawnflag){bit, f->values[2]}; } }
        else errf(&errs, "line %lld: \"spawnflags\" expects \"BIT = name\"", NULL, f->line); }
      else if (!strcmp(f->key, "team")) { if (exp_num(&errs, f, &n)) { en->team = n; en->has_team = true; } } else unknown(&errs, &en->base, f); } } END(); }
void qa_items_result_destroy(qa_items_result *r) { for (size_t i = 0; i < r->entry_count; i++) { base_free(&r->entries[i].base); free((void *)r->entries[i].flags); free(r->entries[i].spawnflags); } free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_monsters(qa_monsters_result *out, const char *text) { BEGIN(qa_monsters_result, qa_monster_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_monster_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->classname = "";
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s;
      if (!strcmp(f->key, "classname")) { if (exp_str(&errs, f, &s)) en->classname = s; }
      else if (!strcmp(f->key, "flags")) { free((void *)en->flags); en->flags = qa_field_flag_list((const char *const *)f->values, f->value_count, &en->flag_count); }
      else unknown(&errs, &en->base, f); } } END(); }
void qa_monsters_result_destroy(qa_monsters_result *r) { for (size_t i = 0; i < r->entry_count; i++) { base_free(&r->entries[i].base); free((void *)r->entries[i].flags); } free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_interactables(qa_interactables_result *out, const char *text) { BEGIN(qa_interactables_result, qa_interactable_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_interactable_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->name = en->interaction = ""; en->logic_op = NULL;
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n; bool b;
      if (!strcmp(f->key, "name")) { if (exp_str(&errs, f, &s)) en->name = s; } else if (!strcmp(f->key, "interaction")) { if (exp_str(&errs, f, &s)) en->interaction = s; }
      else if (!strcmp(f->key, "spawnflags")) { if (exp_num(&errs, f, &n)) { en->spawnflags = n; en->has_spawnflags = true; } }
      else if (!strcmp(f->key, "health")) { if (exp_bool(&errs, f, &b)) { en->health = b; en->has_health = true; } }
      else if (!strcmp(f->key, "targetname")) { if (exp_bool(&errs, f, &b)) { en->targetname = b; en->has_targetname = true; } }
      else if (!strcmp(f->key, "bounds")) { bool ok = f->value_count == 10 && !strcmp(f->values[0], "{") && !strcmp(f->values[4], "}") && !strcmp(f->values[5], "{") && !strcmp(f->values[9], "}");
        double vs[6] = {0}; if (ok) { const char *idx[6] = {f->values[1], f->values[2], f->values[3], f->values[6], f->values[7], f->values[8]}; for (int k = 0; k < 6; k++) { char *e = NULL; vs[k] = strtod(idx[k], &e); if (!e || *e || !isfinite(vs[k])) ok = false; } }
        if (!ok) errf(&errs, "line %lld: \"bounds\" expects { x y z } { x y z }", NULL, f->line);
        else { en->has_bounds = true; memcpy(en->bounds.mins, vs, 3 * sizeof(double)); memcpy(en->bounds.maxs, vs + 3, 3 * sizeof(double)); } }
      else if (!strcmp(f->key, "logic_op")) { if (exp_str(&errs, f, &s)) en->logic_op = s; } else unknown(&errs, &en->base, f); } } END(); }
void qa_interactables_result_destroy(qa_interactables_result *r) { for (size_t i = 0; i < r->entry_count; i++) base_free(&r->entries[i].base); free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_game_rules(qa_game_rules_result *out, const char *text) { BEGIN(qa_game_rules_result, qa_game_rule_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_game_rule_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->cvar = en->game_type = ""; en->only_game_type = en->not_game_type = NULL;
    const char *cvars[64]; double vals[64]; size_t nc = 0, nv = 0;
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n; bool b;
      if (!strcmp(f->key, "cvar")) { if (exp_str(&errs, f, &s)) { en->cvar = s; if (nc < 64) cvars[nc++] = s; } }
      else if (!strcmp(f->key, "value")) { if (exp_num(&errs, f, &n)) { en->value = n; if (nv < 64) vals[nv++] = n; } }
      else if (!strcmp(f->key, "weapon_stay")) { if (exp_bool(&errs, f, &b)) { en->weapon_stay = b; en->weapon_stay_specified = true; } }
      else if (!strcmp(f->key, "has_teams")) { if (exp_bool(&errs, f, &b)) { en->has_teams = b; en->has_teams_present = true; } }
      else if (!strcmp(f->key, "team_damage")) { if (exp_bool(&errs, f, &b)) { en->team_damage = b; en->team_damage_present = true; } }
      else if (!strcmp(f->key, "only_gametype")) { if (exp_str(&errs, f, &s)) en->only_game_type = s; }
      else if (!strcmp(f->key, "not_gametype")) { if (exp_str(&errs, f, &s)) en->not_game_type = s; }
      else if (!strcmp(f->key, "logic_op")) { if (exp_str(&errs, f, &s)) { if (!strcmp(s, "and")) en->logic_is_and = true; else if (!strcmp(s, "or")) en->logic_is_and = false; else errf(&errs, "line %lld: unsupported logic_op \"%s\"", s, f->line); } }
      else if (!strcmp(f->key, "game_type")) { if (exp_str(&errs, f, &s)) en->game_type = s; } else unknown(&errs, &en->base, f); }
    if (nc != nv || nc > 2) errf(&errs, "line %lld: game rule expects one or two paired cvar/value entries", NULL, bl->line);
    en->conditions = malloc((nc ? nc : 1) * sizeof *en->conditions); size_t cc = 0; for (size_t k = 0; k < nc && k < nv; k++) en->conditions[cc++] = (qa_bot_rule_condition){cvars[k], vals[k]}; en->condition_count = cc; } END(); }
void qa_game_rules_result_destroy(qa_game_rules_result *r) { for (size_t i = 0; i < r->entry_count; i++) { base_free(&r->entries[i].base); free(r->entries[i].conditions); } free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_teams(qa_teams_result *out, const char *text) { BEGIN(qa_teams_result, qa_team_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_team_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->name = "";
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n;
      if (!strcmp(f->key, "value")) { if (exp_num(&errs, f, &n)) en->value = n; } else if (!strcmp(f->key, "name")) { if (exp_str(&errs, f, &s)) en->name = s; } else unknown(&errs, &en->base, f); } } END(); }
void qa_teams_result_destroy(qa_teams_result *r) { for (size_t i = 0; i < r->entry_count; i++) base_free(&r->entries[i].base); free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_chats(qa_chats_result *out, const char *text) { BEGIN(qa_chats_result, qa_chat_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_chat_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->locstring = en->type = "";
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n; bool b;
      if (!strcmp(f->key, "locstring")) { if (exp_str(&errs, f, &s)) en->locstring = s; } else if (!strcmp(f->key, "type")) { if (exp_str(&errs, f, &s)) en->type = s; }
      else if (!strcmp(f->key, "time")) { if (exp_num(&errs, f, &n)) en->time = n; } else if (!strcmp(f->key, "chance")) { if (exp_num(&errs, f, &n)) en->chance = n; }
      else if (!strcmp(f->key, "team")) { if (exp_bool(&errs, f, &b)) en->team = b; } else unknown(&errs, &en->base, f); } } END(); }
void qa_chats_result_destroy(qa_chats_result *r) { for (size_t i = 0; i < r->entry_count; i++) base_free(&r->entries[i].base); free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
bool qa_parse_dangers(qa_dangers_result *out, const char *text) { BEGIN(qa_dangers_result, qa_danger_entry);
  for (size_t i = 0; i < out->entry_count; i++) { qa_danger_entry *en = &out->entries[i]; const qa_block *bl = &out->parsed.blocks[i]; en->base.source = bl; en->name = ""; en->sight_dist = 3.4028234663852886e38;
    for (size_t j = 0; j < bl->field_count; j++) { const qa_block_field *f = &bl->fields[j]; const char *s; double n;
      if (!strcmp(f->key, "name")) { if (exp_str(&errs, f, &s)) en->name = s; } else if (!strcmp(f->key, "sight_dist")) { if (exp_num(&errs, f, &n)) en->sight_dist = n; }
      else if (!strcmp(f->key, "flags")) { free((void *)en->flags); en->flags = qa_field_flag_list((const char *const *)f->values, f->value_count, &en->flag_count); } else unknown(&errs, &en->base, f); } } END(); }
void qa_dangers_result_destroy(qa_dangers_result *r) { for (size_t i = 0; i < r->entry_count; i++) { base_free(&r->entries[i].base); free((void *)r->entries[i].flags); } free(r->entries); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
static void skill_num(Errs *e, const qa_block_field *f, double *o) { double n; if (exp_num(e, f, &n)) *o = n; }
static void skill_bool(Errs *e, const qa_block_field *f, bool *o) { bool b; if (exp_bool(e, f, &b)) *o = b; }
static void apply_skill(qa_bot_skill_settings *st, const qa_block_field *f, Errs *e) { const char *k = f->key;
  if (!strcmp(k, "aiming.lead_targets")) skill_bool(e, f, &st->aiming.lead_targets);
  else if (!strcmp(k, "behaviors.combat.max_item_dist")) skill_num(e, f, &st->behaviors.combat_max_item_dist);
  else if (!strcmp(k, "behaviors.combat.min_health_pct")) skill_num(e, f, &st->behaviors.combat_min_health_pct);
  else if (!strcmp(k, "behaviors.combat.min_ammo_pct")) skill_num(e, f, &st->behaviors.combat_min_ammo_pct);
  else if (!strcmp(k, "behaviors.combat.min_armor_pct")) skill_num(e, f, &st->behaviors.combat_min_armor_pct);
  else if (!strcmp(k, "behaviors.combat.grab_weapons")) skill_bool(e, f, &st->behaviors.combat_grab_weapons);
  else if (!strcmp(k, "behaviors.react_to_dangers")) skill_bool(e, f, &st->behaviors.react_to_dangers);
  else if (!strcmp(k, "movement.allow_rocket_jumping")) skill_bool(e, f, &st->movement.allow_rocket_jumping);
  else if (!strcmp(k, "weapons.fov_scalar")) skill_num(e, f, &st->weapons.fov_scalar);
  else if (!strcmp(k, "aiming.max_acceleration")) skill_num(e, f, &st->aiming.max_acceleration);
  else if (!strcmp(k, "aiming.spring_stiffness")) skill_num(e, f, &st->aiming.spring_stiffness);
  else if (!strcmp(k, "aiming.damping")) skill_num(e, f, &st->aiming.damping);
  else if (!strcmp(k, "aiming.velocity_offset")) skill_num(e, f, &st->aiming.velocity_offset);
  else if (!strcmp(k, "aiming.modifier.max_angle")) skill_num(e, f, &st->aiming.modifier_max_angle);
  else if (!strcmp(k, "aiming.modifier.apply_time")) skill_num(e, f, &st->aiming.modifier_apply_time);
  else if (!strcmp(k, "aiming.modifier.accel_scalar")) skill_num(e, f, &st->aiming.modifier_accel_scalar);
  else if (!strcmp(k, "aiming.modifier.spring_scalar")) skill_num(e, f, &st->aiming.modifier_spring_scalar);
  else if (!strcmp(k, "aiming.modifier.damping_scalar")) skill_num(e, f, &st->aiming.modifier_damping_scalar);
  else if (!strcmp(k, "behaviors.allow_combat")) skill_bool(e, f, &st->behaviors.allow_combat);
  else if (!strcmp(k, "behaviors.combat.allow_grab_items") || !strcmp(k, "behaviors.allow_grab_items_in_combat")) skill_bool(e, f, &st->behaviors.allow_grab_items_in_combat);
  else if (!strcmp(k, "behaviors.combat.allow_melee") || !strcmp(k, "behaviors.allow_melee")) skill_bool(e, f, &st->behaviors.allow_melee);
  else if (!strcmp(k, "behaviors.allow_check_six")) skill_bool(e, f, &st->behaviors.allow_check_six);
  else if (!strcmp(k, "behaviors.allow_grab_items")) skill_bool(e, f, &st->behaviors.allow_grab_items);
  else if (!strcmp(k, "behaviors.allow_grab_power_items")) skill_bool(e, f, &st->behaviors.allow_grab_power_items);
  else if (!strcmp(k, "behaviors.defer_power_items_to_humans")) skill_bool(e, f, &st->behaviors.defer_power_items_to_humans);
  else if (!strcmp(k, "behaviors.min_respawn_time")) skill_num(e, f, &st->behaviors.min_respawn_time);
  else if (!strcmp(k, "behaviors.max_respawn_time")) skill_num(e, f, &st->behaviors.max_respawn_time);
  else if (!strcmp(k, "movement.allow_jumping_in_combat")) skill_bool(e, f, &st->movement.allow_jumping_in_combat);
  else if (!strcmp(k, "movement.jump_chance")) skill_num(e, f, &st->movement.jump_chance);
  else if (!strcmp(k, "movement.jump_cooldown")) skill_num(e, f, &st->movement.jump_cooldown);
  else if (!strcmp(k, "movement.walk_only")) skill_bool(e, f, &st->movement.walk_only);
  else if (!strcmp(k, "senses.sight_time")) skill_num(e, f, &st->senses.sight_time);
  else if (!strcmp(k, "senses.sight_decay_time")) skill_num(e, f, &st->senses.sight_decay_time);
  else if (!strcmp(k, "senses.invis_enemy_sight_scalar")) skill_num(e, f, &st->senses.invis_enemy_sight_scalar);
  else if (!strcmp(k, "senses.max_invis_enemy_sight_dist")) skill_num(e, f, &st->senses.max_invis_enemy_sight_dist);
  else if (!strcmp(k, "senses.fov_angle")) skill_num(e, f, &st->senses.fov_angle);
  else if (!strcmp(k, "senses.forget_non_vis_enemy_time")) skill_num(e, f, &st->senses.forget_non_vis_enemy_time);
  else if (!strcmp(k, "senses.sound_range")) skill_num(e, f, &st->senses.sound_range);
  else if (!strcmp(k, "senses.sound_time")) skill_num(e, f, &st->senses.sound_time);
  else if (!strcmp(k, "senses.sound_decay_time")) skill_num(e, f, &st->senses.sound_decay_time);
  else if (!strcmp(k, "senses.sound_persist_time")) skill_num(e, f, &st->senses.sound_persist_time);
  else if (!strcmp(k, "weapons.decay_time")) skill_num(e, f, &st->weapons.decay_time);
  else if (!strcmp(k, "weapons.fov_angle")) skill_num(e, f, &st->weapons.fov_angle);
  else if (!strcmp(k, "weapons.sight_time")) skill_num(e, f, &st->weapons.sight_time);
  else { st->unknown = realloc((void *)st->unknown, (st->unknown_count + 1) * sizeof *st->unknown); st->unknown[st->unknown_count++] = f; errf(e, "line %lld: unknown key \"%s\"", k, f->line); } }
bool qa_parse_bot_settings(qa_bot_settings_result *out, const char *text) { memset(out, 0, sizeof *out); if (!qa_parse_blocks(&out->parsed, text)) return false; Errs errs = {0}; errs_from_blocks(&errs, &out->parsed);
  out->skills = calloc(out->parsed.block_count ? out->parsed.block_count : 1, sizeof *out->skills); size_t n = 0;
  for (size_t i = 0; i < out->parsed.block_count; i++) { const qa_block *bl = &out->parsed.blocks[i];
    if (bl->header_count != 2 || strcmp(bl->header[0], "skill")) { char hdr[512] = ""; for (size_t h = 0; h < bl->header_count; h++) { if (h) strncat(hdr, ",", sizeof hdr - strlen(hdr) - 1); strncat(hdr, "\"", sizeof hdr - strlen(hdr) - 1); strncat(hdr, bl->header[h], sizeof hdr - strlen(hdr) - 1); strncat(hdr, "\"", sizeof hdr - strlen(hdr) - 1); }
      char buf[640]; snprintf(buf, sizeof buf, "[%s]", hdr); /* TS JSON.stringify(header) */ errf(&errs, "line %lld: block header is not \"skill <name>\" (got %s)", buf, bl->line); continue; }
    qa_bot_skill_settings *st = &out->skills[n++]; qa_bot_skill_settings_init(st); st->source = bl; st->skill = bl->header[1];
    for (size_t j = 0; j < bl->field_count; j++) apply_skill(st, &bl->fields[j], &errs); }
  out->skill_count = n; out->errors = errs.v; out->error_count = errs.n; return true; }
void qa_bot_settings_result_destroy(qa_bot_settings_result *r) { for (size_t i = 0; i < r->skill_count; i++) free((void *)r->skills[i].unknown); free(r->skills); errs_free(r->errors, r->error_count); qa_block_parse_result_destroy(&r->parsed); memset(r, 0, sizeof *r); }
