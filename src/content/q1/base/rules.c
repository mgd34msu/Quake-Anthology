/* Port of src/content/q1/base/rules.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/rules.h"
#include "base_internal.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/finales.h"
#include "qa/content/q1/base/messages.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
static const qa_bounds POINT_BOUNDS = {{0, 0, 0}, {0, 0, 0}};

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static void must_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static bool set_owned(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_owned(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
static double saved_integer(const qa_save_reader *reader) {
  return qa_save_reader_integer_min(reader, 0);
}

/* ---------- Q1SpawnSelector ---------- */
typedef struct selection_entry {
  char *id; qa_q1_spawn_selection_fn fn; void *ctx;
} selection_entry;
struct qa_q1_spawn_selector {
  qa_q1_services *game;
  qa_q1_campaign_binding campaign;
  qa_q1_actor *last_spawn;
  selection_entry *selections; size_t selection_count; size_t selection_capacity;
};
qa_q1_spawn_selector *qa_q1_spawn_selector_create(qa_q1_services *game, qa_q1_campaign_binding campaign) {
  qa_q1_spawn_selector *selector = malloc(sizeof *selector);
  if (selector == NULL) return NULL;
  memset(selector, 0, sizeof *selector);
  selector->game = game;
  selector->campaign = campaign;
  return selector;
}
void qa_q1_spawn_selector_destroy(qa_q1_spawn_selector *selector) {
  if (selector == NULL) return;
  for (size_t i = 0; i < selector->selection_count; i++) free(selector->selections[i].id);
  free(selector->selections);
  free(selector);
}
void qa_q1_spawn_selector_register_selection(qa_q1_spawn_selector *selector, const char *id,
                                             qa_q1_spawn_selection_fn select, void *ctx) {
  for (size_t i = 0; i < selector->selection_count; i++)
    if (strcmp(selector->selections[i].id, id) == 0)
      qa_range_error("Duplicate Q1 source spawn selector %s", id);
  if (selector->selection_count == selector->selection_capacity) {
    size_t capacity = selector->selection_capacity == 0 ? 4 : selector->selection_capacity * 2;
    selection_entry *grown = realloc(selector->selections, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    selector->selections = grown;
    selector->selection_capacity = capacity;
  }
  selector->selections[selector->selection_count].id = dup_text(id);
  selector->selections[selector->selection_count].fn = select;
  selector->selections[selector->selection_count].ctx = ctx;
  selector->selection_count++;
}
qa_save_value *qa_q1_spawn_selector_capture(const qa_q1_spawn_selector *selector) {
  if (selector->last_spawn == NULL) return qa_save_value_null();
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_owned(object, "slot", qa_save_value_number((double)selector->last_spawn->actor.id.slot));
  ok = ok && set_owned(object, "generation", qa_save_value_number((double)selector->last_spawn->actor.id.generation));
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
void qa_q1_spawn_selector_restore(qa_q1_spawn_selector *selector, const qa_save_reader *reader) {
  if (qa_save_reader_is_null(reader)) { selector->last_spawn = NULL; return; }
  qa_save_reader slot, generation;
  must_field(reader, "slot", &slot);
  int64_t slot_value = (int64_t)saved_integer(&slot);
  qa_save_reader_destroy(&slot);
  must_field(reader, "generation", &generation);
  int64_t generation_value = (int64_t)saved_integer(&generation);
  qa_save_reader_destroy(&generation);
  qa_owned_actor actor;
  if (!q1base_resolve_saved(selector->game, slot_value, generation_value, &actor))
    qa_save_reader_fail(reader, "missing spawn point");
  selector->last_spawn = qa_q1_get_entity(selector->game, &actor.id);
}
static bool selector_nearby(qa_q1_spawn_selector *selector, qa_q1_actor *point, double radius, bool living) {
  qa_q1_services *game = selector->game;
  size_t count = 0;
  const qa_actor_id *players = q1base_players(game, &count);
  for (size_t i = 0; i < count; i++) {
    if (living && qa_q1_health(game, players[i]) <= 0) continue;
    qa_q1_body body;
    if (!q1base_read_body(game, players[i], &body)) continue;
    qa_vec3 center = qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5));
    if (qa_length3(qa_sub3(center, qa_q1_get_body(game, point).origin)) <= radius) return true;
  }
  return false;
}
static bool selector_visible(qa_q1_spawn_selector *selector, qa_q1_actor *point) {
  qa_q1_services *game = selector->game;
  size_t count = 0;
  const qa_actor_id *players = q1base_players(game, &count);
  for (size_t i = 0; i < count; i++) {
    if (qa_q1_health(game, players[i]) <= 0) continue;
    qa_q1_body body;
    if (!q1base_read_body(game, players[i], &body)) continue;
    qa_q1_trace_request request;
    memset(&request, 0, sizeof request);
    request.start = qa_add3(qa_q1_get_body(game, point).origin, V3(0, 0, 22));
    request.end = qa_add3(body.origin, V3(0, 0, 22));
    request.bounds = POINT_BOUNDS;
    request.has_ignore = true;
    request.ignore = point->actor.id;
    request.monsters = false;
    qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (trace.fraction >= 1) return true;
  }
  return false;
}
static qa_q1_actor *find_classname(qa_q1_actor *const *entities, size_t count, const char *classname) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(entities[i]->classname, classname) == 0) return entities[i];
  return NULL;
}
typedef struct spawn_lists { qa_q1_actor **points, **available; } spawn_lists;
static void free_spawn_lists(void *arg) {
  spawn_lists *lists = arg;
  free(lists->points);
  free(lists->available);
  lists->points = lists->available = NULL;
}
static qa_q1_actor *select_deathmatch(qa_q1_spawn_selector *selector, qa_q1_actor *const *entities, size_t count,
                                      qa_q1_actor **points, size_t point_count, spawn_lists *lists,
                                      bool force_spawn) {
  qa_q1_services *game = selector->game;
  /* The rerelease defers a deathmatch spawn on a map with no deathmatch
   * points and forces info_player_start after the retry window. */
  if (point_count == 0 && strcmp(game->options.edition, "classic") == 0)
    qa_range_error("No info_player_deathmatch on level");
  if (point_count == 0) return force_spawn ? find_classname(entities, count, "info_player_start") : NULL;
  if (strcmp(game->options.edition, "classic") == 0) {
    long start = -1;
    if (selector->last_spawn != NULL)
      for (size_t i = 0; i < point_count; i++) if (points[i] == selector->last_spawn) { start = (long)i; break; }
    for (size_t i = 1; i <= point_count; i++) {
      qa_q1_actor *point = points[(size_t)(start + (long)i) % point_count];
      if (point == selector->last_spawn || !selector_nearby(selector, point, 32, false)) {
        selector->last_spawn = point;
        return point;
      }
    }
    return force_spawn ? points[0] : NULL;
  }
  qa_q1_actor **available = lists->available = malloc((point_count != 0 ? point_count : 1) * sizeof *available);
  if (available == NULL) qa_range_error("out of memory");
  size_t available_count = 0;
  /* TS points.filter(...).reverse(): the probes run in point order, then the
   * kept points are reversed. */
  for (size_t i = 0; i < point_count; i++)
    if (!selector_nearby(selector, points[i], 384, true) && !selector_visible(selector, points[i]))
      available[available_count++] = points[i];
  if (available_count == 0)
    for (size_t i = 0; i < point_count; i++)
      if (!selector_nearby(selector, points[i], 84, true)) available[available_count++] = points[i];
  for (size_t i = 0, j = available_count; i + 1 < j; i++, j--) {
    qa_q1_actor *swap = available[i];
    available[i] = available[j - 1];
    available[j - 1] = swap;
  }
  if (available_count == 0) {
    if (!force_spawn) return NULL;
    /* points[-1] ?? null when there are no points: no size_t cast of -1. */
    double index = floor(game->host.random(game->host.ctx) * ((double)point_count - 1) + 0.5);
    return index >= 0 && index < (double)point_count ? points[(size_t)index] : NULL;
  }
  double index = floor(game->host.random(game->host.ctx) * ((double)available_count - 1) + 0.5);
  return index >= 0 && index < (double)available_count ? available[(size_t)index] : NULL;
}
qa_q1_actor *qa_q1_spawn_selector_select(qa_q1_spawn_selector *selector, bool force_spawn) {
  qa_q1_services *game = selector->game;
  for (size_t i = 0; i < selector->selection_count; i++) {
    qa_q1_actor *point = NULL;
    int result = selector->selections[i].fn(selector->selections[i].ctx, force_spawn, &point);
    if (result == 1) return point;
    if (result == 2) return NULL;
  }
  qa_q1_actor *const *entities = game->entities;
  const size_t count = game->entity_count;
  qa_q1_actor *region = find_classname(entities, count, "testplayerstart");
  if (region != NULL) return region;
  if (game->options.coop) {
    long after = -1;
    if (selector->last_spawn != NULL)
      for (size_t i = 0; i < count; i++) if (entities[i] == selector->last_spawn) { after = (long)i; break; }
    bool activated = false;
    for (size_t i = 0; i < count; i++)
      if (strcmp(entities[i]->classname, "info_player_coop") == 0 && entities[i]->activated) activated = true;
    qa_q1_actor *point = NULL;
    for (size_t i = (size_t)(after + 1); i < count && point == NULL; i++)
      if (strcmp(entities[i]->classname, "info_player_coop") == 0 && (!activated || entities[i]->activated)) point = entities[i];
    if (point == NULL) point = find_classname(entities, count, "info_player_start");
    if (point != NULL) { selector->last_spawn = point; return point; }
  } else if (game->options.deathmatch != 0) {
    /* TS filters every entity (no limit); the lists are released on every exit,
     * including a raise out of the trace probes (TS GC). */
    spawn_lists lists = {malloc((count != 0 ? count : 1) * sizeof *lists.points), NULL};
    if (lists.points == NULL) qa_range_error("out of memory");
    qa_cleanup lists_node;
    qa_cleanup_push(&lists_node, free_spawn_lists, &lists);
    qa_q1_actor **points = lists.points;
    size_t point_count = 0;
    for (size_t i = 0; i < count; i++)
      if (strcmp(entities[i]->classname, "info_player_deathmatch") == 0) points[point_count++] = entities[i];
    qa_q1_actor *chosen = select_deathmatch(selector, entities, count, points, point_count, &lists, force_spawn);
    qa_cleanup_pop(&lists_node);
    free_spawn_lists(&lists);
    return chosen;
  }
  if (selector->campaign.read_flags(selector->campaign.ctx) != 0) {
    qa_q1_actor *returned = find_classname(entities, count, "info_player_start2");
    if (returned != NULL) return returned;
  }
  qa_q1_actor *start = find_classname(entities, count, "info_player_start");
  if (start == NULL) qa_range_error("PutClientInServer: no info_player_start on level");
  return start;
}

/* ---------- q1ClientNotice / q1Obituary ---------- */
void qa_q1_client_notice_make(qa_q1_client_notice *notice, const char *edition, const char *event,
                              const char *name, double frags) {
  memset(notice, 0, sizeof *notice);
  notice->score_delta = strcmp(event, "suicide") == 0 ? -2 : 0;
  if (strcmp(edition, "rerelease") == 0) {
    notice->key = strcmp(event, "connect") == 0 ? "$qc_entered"
                 : strcmp(event, "disconnect") == 0 ? "$qc_left_game"
                 : strcmp(event, "suicide") == 0 ? "$qc_suicides" : "$qc_exited";
    (void)snprintf(notice->text, sizeof notice->text, "%s", notice->key);
    notice->arguments[0] = name;
    notice->argument_count = 1;
    if (strcmp(event, "disconnect") == 0) {
      qa_net_js_number(frags, notice->frags, sizeof notice->frags);
      notice->arguments[1] = notice->frags;
      notice->argument_count = 2;
    }
    return;
  }
  char frags_text[32];
  qa_net_js_number(frags, frags_text, sizeof frags_text);
  if (strcmp(event, "connect") == 0) (void)snprintf(notice->text, sizeof notice->text, "%s entered the game\n", name);
  else if (strcmp(event, "disconnect") == 0) (void)snprintf(notice->text, sizeof notice->text, "%s left the game with %s frags\n", name, frags_text);
  else if (strcmp(event, "suicide") == 0) (void)snprintf(notice->text, sizeof notice->text, "%s suicides\n", name);
  else (void)snprintf(notice->text, sizeof notice->text, "%s exited the level\n", name);
  notice->argument_count = 0;
}
/* MG1 and MG3 client.qc check monsters before the environment; the MG
 * programs have no quad rocket lines. */
typedef struct key_entry { const char *key; const char *value; } key_entry;
static const key_entry MG_MONSTER_KEYS[] = {
  {"monster_army", "$qc_ks_grunt"}, {"monster_demon1", "$qc_ks_fiend"}, {"monster_dog", "$qc_ks_rottweiler"},
  {"monster_enforcer", "$qc_ks_enforcer"}, {"monster_fish", "$qc_ks_rotfish"},
  {"monster_hell_knight", "$qc_ks_deathknight"}, {"monster_ranged_knight", "$qc_ks_deathknight"},
  {"monster_knight", "$qc_ks_knight"}, {"monster_ogre", "$qc_ks_ogre"}, {"monster_shalrath", "$qc_ks_vore"},
  {"monster_shambler", "$qc_ks_shambler"}, {"monster_tarbaby", "$qc_ks_spawn"}, {"monster_wizard", "$qc_ks_scrag"},
  {"monster_zombie", "$qc_ks_zombie"},
  {"monster_super_shambler", "$mg3_qc_obit_supershambler"}, {"monster_lava_man", "$mg3_qc_obit_lavaman"},
};
static const key_entry MG_VARIANT_KEYS[] = {
  {"monster_dog", "$mg3_qc_obit_demodog"}, {"monster_ogre", "$mg3_qc_obit_rocketogre"}, {"monster_tarbaby", "$mg3_qc_obit_slime"},
};
static const key_entry MG_HAZARD_KEYS[] = {
  {"explo_box", "$qc_ks_blew_up"}, {"trap_shooter", "$qc_ks_spiked"}, {"trap_spikeshooter", "$qc_ks_spiked"},
  {"fireball", "$qc_ks_lavaball"}, {"trigger_changelevel", "$qc_ks_tried_leave"},
};
/* CTF log.qc `what` tokens. */
static const key_entry CTF_LOG_WEAPONS[] = {
  {"grenadelauncher", "grenade"}, {"rocketlauncher", "rocket"}, {"ctf:grapple", "hook"},
};
static const key_entry CTF_LOG_MONSTERS[] = {
  {"monster_army", "grunt"}, {"monster_demon1", "fiend"}, {"monster_dog", "dog"}, {"monster_dragon", "dragon"},
  {"monster_enforcer", "enforcer"}, {"monster_fish", "fish"}, {"monster_hell_knight", "deathknight"},
  {"monster_knight", "knight"}, {"monster_ogre", "ogre"}, {"monster_oldone", "shub"}, {"monster_shalrath", "vore"},
  {"monster_shambler", "shambler"}, {"monster_tarbaby", "spawn"}, {"monster_vomit", "vomitus"},
  {"monster_wizard", "scrag"}, {"monster_zombie", "zombie"},
};
/* MG1 and MG3 client.qc deathmatch rotation (Q1LevelRules.checkLimits). */
static const key_entry MG_DEATHMATCH_ROTATION[] = {
  {"mgdm1", "mgdm2"}, {"mgdm2", "mgdm3"}, {"mgdm3", "mgdm4"}, {"mgdm4", "mgdm1"},
};
#define KEYS(table) (table), (sizeof (table) / sizeof *(table))
static const char *key_lookup(const key_entry *table, size_t count, const char *key) {
  for (size_t i = 0; i < count; i++) if (strcmp(table[i].key, key) == 0) return table[i].value;
  return NULL;
}
/* TS Q1ObituaryLog; has false = TS undefined log. */
typedef struct obituary_log { bool has; const char *what; bool has_versus; qa_actor_id versus; } obituary_log;
static const obituary_log NO_LOG = {.has = false};
static obituary_log log_of(const char *what) { return (obituary_log){.has = true, .what = what}; }
static obituary_log log_versus(const char *what, qa_actor_id versus) { return (obituary_log){true, what, true, versus}; }
static qa_q1_obituary obituary_result(const qa_q1_obituary_input *input, const char *text,
                                      qa_actor_id credited, double delta,
                                      const char *const *args, size_t arg_count,
                                      bool has_achievement, qa_actor_id achievement_actor,
                                      const char *achievement_id, obituary_log log) {
  qa_q1_obituary result = {0};
  result.has_message = text[0] != '\0';
  if (result.has_message) {
    if (strcmp(input->edition, "classic") == 0) {
      qa_q1_classic_obituary_text(text, args, arg_count, result.text, sizeof result.text);
      result.argument_count = 0;
    } else {
      (void)snprintf(result.text, sizeof result.text, "%s", text);
      result.argument_count = arg_count < 2 ? arg_count : 2;
      for (size_t i = 0; i < result.argument_count; i++) result.arguments[i] = args[i];
    }
  }
  result.has_score = true;
  result.score_actor = credited;
  result.score_delta = delta;
  result.has_achievement = has_achievement;
  result.achievement_actor = achievement_actor;
  result.achievement_id = achievement_id;
  result.has_log = log.has;
  result.log_what = log.what;
  result.log_has_versus = log.has_versus;
  result.log_versus = log.versus;
  return result;
}
static bool variant_is(const qa_q1_obituary_input *input, const char *variant) {
  return input->variant != NULL && strcmp(input->variant, variant) == 0;
}
/* TS `attacker.weapon === null ? "" : ctfLogWeapons.get(attacker.weapon) ?? attacker.weapon`. */
static const char *ctf_log_weapon(const char *weapon) {
  if (weapon == NULL) return "";
  const char *mapped = key_lookup(KEYS(CTF_LOG_WEAPONS), weapon);
  return mapped != NULL ? mapped : weapon;
}
qa_q1_obituary qa_q1_obituary_compute(const qa_q1_obituary_input *input) {
  const qa_q1_obituary_actor *victim = &input->victim;
  const qa_q1_obituary_actor *attacker = input->attacker;
  double roll = input->random(input->random_ctx);
  qa_q1_obituary empty = {0};
  if (!victim->is_player) return empty;
  const char *victim_args[1] = {victim->name};
  if (attacker != NULL && strcmp(attacker->classname, "teledeath") == 0 && input->telefrag_owner != NULL) {
    const char *args[2] = {victim->name, input->telefrag_owner->name};
    return obituary_result(input, "$qc_telefragged", input->telefrag_owner->actor, 1, args, 2, false, victim->actor, NULL,
                           log_versus("telefrag", input->telefrag_owner->actor));
  }
  if (attacker != NULL && strcmp(attacker->classname, "teledeath2") == 0)
    return obituary_result(input, "$qc_satans_power", victim->actor, -1, victim_args, 1, false, victim->actor, NULL,
                           log_of("telefrag"));
  if (attacker != NULL && attacker->is_player) {
    if (qa_same_actor(victim->actor, attacker->actor)) {
      if (victim->weapon != NULL && strcmp(victim->weapon, "lightning") == 0 && victim->water_level > 1) {
        const char *key = strcmp(victim->water_type, "slime") == 0 ? "$qc_discharge_slime"
                          : strcmp(victim->water_type, "lava") == 0 ? "$qc_discharge_lava" : "$qc_discharge_water";
        return obituary_result(input, key, victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("discharge"));
      }
      const bool grenade = victim->weapon != NULL && strcmp(victim->weapon, "grenadelauncher") == 0;
      if (variant_is(input, "ctf") && !grenade && strcmp(input->death_type, "ctf:teamchange") == 0)
        return obituary_result(input, (qa_js_to_int32(input->teamplay) & 64) != 0 ? "$qc_ks_tried_change_teams" : "$qc_ks_changed_teams",
                               victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("teamchange"));
      const char *key = grenade ? "$qc_suicide_pin" : roll != 0 ? "$qc_suicide_bored" : "$qc_suicide_loaded";
      return obituary_result(input, key, victim->actor, -1, victim_args, 1, false, victim->actor, NULL,
                             log_of(grenade ? "grenade" : "rocket"));
    }
    bool classic = strcmp(input->edition, "classic") == 0;
    if (input->teamplay == 2 && victim->team == attacker->team &&
        (classic ? victim->team > 0 : attacker->team != 0)) {
      const char *key = roll < 0.25 ? "$qc_ff_teammate" : roll < 0.5 ? "$qc_ff_glasses"
                        : roll < 0.75 ? "$qc_ff_otherteam" : "$qc_ff_friend";
      const char *args[1] = {attacker->name};
      return obituary_result(input, key, attacker->actor, -1, args, 1, false, victim->actor, NULL,
                             log_versus(ctf_log_weapon(attacker->weapon), attacker->actor));
    }
    const obituary_log log = log_versus(ctf_log_weapon(attacker->weapon), attacker->actor);
    const char *args[2] = {victim->name, attacker->name};
    const char *weapon = attacker->weapon;
    if (weapon != NULL && strcmp(weapon, "axe") == 0)
      return obituary_result(input, "$qc_death_ax", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "shotgun") == 0)
      return obituary_result(input, "$qc_death_sg", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "supershotgun") == 0)
      return obituary_result(input, "$qc_death_dbl", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "nailgun") == 0)
      return obituary_result(input, "$qc_death_nail", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "supernailgun") == 0)
      return obituary_result(input, "$qc_death_sng", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "grenadelauncher") == 0)
      return obituary_result(input, victim->health < -40 ? "$qc_death_gl1" : "$qc_death_gl2", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "rocketlauncher") == 0) {
      if (!classic && !variant_is(input, "mg") && attacker->quad_expires > 0 && victim->health < -40) {
        double r = input->random(input->random_ctx);
        return obituary_result(input, r < 0.3 ? "$qc_death_rl_quad1" : r < 0.6 ? "$qc_death_rl_quad2" : "$qc_death_rl1",
                               attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
      }
      return obituary_result(input, victim->health < -40 ? "$qc_death_rl2" : "$qc_death_rl3", attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    }
    if (weapon != NULL && strcmp(weapon, "ctf:grapple") == 0)
      return obituary_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_hook1" : "$qc_death_hook2",
                             attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
    if (weapon != NULL && strcmp(weapon, "lightning") == 0) {
      bool achievement = !classic && attacker->water_level > 1 && attacker->invulnerable_expires != 0;
      return obituary_result(input, attacker->water_level > 1 ? "$qc_death_lg1" : "$qc_death_lg2",
                             attacker->actor, 1, args, 2, achievement, attacker->actor,
                             achievement ? "ACH_SURVIVE_DISCHARGE" : NULL, log);
    }
    return obituary_result(input, attacker->kill_string, attacker->actor, 1, args, 2, false, victim->actor, NULL, log);
  }
  if (variant_is(input, "mg") && attacker != NULL) {
    if (attacker->is_monster) {
      const char *key = NULL;
      if (attacker->has_aflag && attacker->aflag != 0) key = key_lookup(KEYS(MG_VARIANT_KEYS), attacker->classname);
      if (key == NULL) key = key_lookup(KEYS(MG_MONSTER_KEYS), attacker->classname);
      return obituary_result(input, key != NULL ? key : "", victim->actor, -1, victim_args, 1, false, victim->actor, NULL, NO_LOG);
    }
    const char *hazard = key_lookup(KEYS(MG_HAZARD_KEYS), attacker->classname);
    if (hazard != NULL) return obituary_result(input, hazard, victim->actor, -1, victim_args, 1, false, victim->actor, NULL, NO_LOG);
    if (attacker->brush && strcmp(attacker->classname, "worldspawn") != 0)
      return obituary_result(input, "$qc_death_squish", victim->actor, -1, victim_args, 1, false, victim->actor, NULL, NO_LOG);
  }
  /* TS tests the victim's water before the classic monster and trap branches. */
  if (strcmp(victim->water_type, "water") == 0)
    return obituary_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_drown1" : "$qc_death_drown2",
                           victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("drowned"));
  if (strcmp(victim->water_type, "slime") == 0)
    return obituary_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_slime1" : "$qc_death_slime2",
                           victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("slimed"));
  if (strcmp(victim->water_type, "lava") == 0) {
    const char *key = victim->health < -15 ? "$qc_death_lava1"
                      : input->random(input->random_ctx) < 0.5 ? "$qc_death_lava2" : "$qc_death_lava3";
    return obituary_result(input, key, victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("melted"));
  }
  if (strcmp(input->edition, "classic") == 0 && attacker != NULL) {
    if (attacker->is_monster) {
      char composed[512];
      const char *suffix = qa_q1_classic_monster_obituary(attacker->classname);
      const char *what = key_lookup(KEYS(CTF_LOG_MONSTERS), attacker->classname);
      (void)snprintf(composed, sizeof composed, "%s%s", victim->name, suffix != NULL ? suffix : "");
      return obituary_result(input, composed, victim->actor, -1, victim_args, 1, false, victim->actor, NULL,
                             log_of(what != NULL ? what : ""));
    }
    const char *trap = NULL, *trap_log = "noexit";
    if (strcmp(attacker->classname, "explo_box") == 0 || strcmp(attacker->classname, "misc_explobox") == 0 ||
        strcmp(attacker->classname, "misc_explobox2") == 0) { trap = " blew up\n"; trap_log = "explosion"; }
    else if (attacker->brush && strcmp(attacker->classname, "worldspawn") != 0) { trap = " was squished\n"; trap_log = "squished"; }
    else if (strcmp(attacker->classname, "trap_shooter") == 0 || strcmp(attacker->classname, "trap_spikeshooter") == 0) { trap = " was spiked\n"; trap_log = "spiked"; }
    else if (strcmp(attacker->classname, "fireball") == 0 || strcmp(attacker->classname, "misc_fireball") == 0) { trap = " ate a lavaball\n"; trap_log = "fireball"; }
    else if (strcmp(attacker->classname, "trigger_changelevel") == 0) trap = " tried to leave\n";
    if (trap != NULL) {
      char composed[512];
      (void)snprintf(composed, sizeof composed, "%s%s", victim->name, trap);
      return obituary_result(input, composed, victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of(trap_log));
    }
  }
  if (attacker != NULL && attacker->brush && strcmp(attacker->classname, "worldspawn") != 0)
    return obituary_result(input, "$qc_death_squish", victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("squished"));
  if (attacker != NULL && attacker->kill_string[0] != '\0')
    return obituary_result(input, attacker->kill_string, victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of("died"));
  const bool falling = strcmp(input->death_type, "falling") == 0;
  return obituary_result(input, falling ? "$qc_death_fall" : "$qc_death_died",
                         victim->actor, -1, victim_args, 1, false, victim->actor, NULL, log_of(falling ? "falling" : "died"));
}

/* ---------- Q1LevelRules ---------- */
typedef struct player_stats_entry {
  qa_owned_actor actor; bool fired_weapon; bool took_damage;
} player_stats_entry;
struct qa_q1_level_rules {
  qa_q1_services *game;
  void *next_level_ctx; /* base:next_level closure state (owned) */
  qa_q1_campaign_binding campaign;
  bool registered; bool official_campaign;
  char next_map[64];
  double stage; double exit_after;
  player_stats_entry *players; size_t player_count; size_t player_capacity;
  qa_q1_intermission_rule *rules; size_t rule_count; size_t rule_capacity;
};
static player_stats_entry *stats_for(qa_q1_level_rules *rules, qa_owned_actor actor, bool create) {
  for (size_t i = 0; i < rules->player_count; i++)
    if (qa_same_owned_actor(rules->players[i].actor, actor)) return &rules->players[i];
  if (!create) return NULL;
  if (rules->player_count == rules->player_capacity) {
    size_t capacity = rules->player_capacity == 0 ? 8 : rules->player_capacity * 2;
    player_stats_entry *grown = realloc(rules->players, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    rules->players = grown;
    rules->player_capacity = capacity;
  }
  rules->players[rules->player_count] = (player_stats_entry){actor, false, false};
  return &rules->players[rules->player_count++];
}
static void rules_on_release(void *ctx, qa_owned_actor actor) {
  qa_q1_level_rules *rules = ctx;
  for (size_t i = 0; i < rules->player_count; i++) {
    if (qa_same_owned_actor(rules->players[i].actor, actor)) {
      memmove(&rules->players[i], &rules->players[i + 1],
              (rules->player_count - i - 1) * sizeof *rules->players);
      rules->player_count--;
      return;
    }
  }
}
typedef struct next_level_ctx { qa_q1_level_rules *rules; } next_level_ctx;
static void next_level_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  next_level_ctx *c = ctx;
  qa_q1_level_rules_begin(c->rules, c->rules->next_map, NULL);
  qa_q1_remove(game, entity);
}
qa_q1_level_rules *qa_q1_level_rules_create(qa_q1_services *game, qa_q1_campaign_binding campaign,
                                            bool registered, bool official_campaign) {
  qa_q1_level_rules *rules = malloc(sizeof *rules);
  if (rules == NULL) return NULL;
  memset(rules, 0, sizeof *rules);
  rules->game = game;
  rules->campaign = campaign;
  rules->registered = registered;
  rules->official_campaign = official_campaign;
  next_level_ctx *c = malloc(sizeof *c);
  if (c == NULL) qa_range_error("out of memory");
  c->rules = rules;
  rules->next_level_ctx = c;
  qa_q1_callback_handlers handlers = {.ctx = c, .action = next_level_action};
  qa_q1_callback_registry_register(game->named, "base:next_level", handlers);
  (void)qa_session_actor_registry_on_release(game->host.actors, rules_on_release, rules);
  return rules;
}
void qa_q1_level_rules_destroy(qa_q1_level_rules *rules) {
  if (rules == NULL) return;
  free(rules->next_level_ctx);
  free(rules->players);
  free(rules->rules);
  free(rules);
}
qa_save_value *qa_q1_level_rules_capture(const qa_q1_level_rules *rules) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_owned(object, "nextMap", qa_save_value_string(rules->next_map));
  ok = ok && set_owned(object, "stage", qa_save_value_number(rules->stage));
  ok = ok && set_owned(object, "exitAfter", qa_save_value_number(rules->exit_after));
  qa_save_value *players = qa_save_value_array();
  if (players == NULL) ok = false;
  for (size_t i = 0; ok && i < rules->player_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *actor = qa_save_value_object();
    if (entry == NULL || actor == NULL) { qa_save_value_free(entry); qa_save_value_free(actor); ok = false; break; }
    ok = set_owned(actor, "slot", qa_save_value_number((double)rules->players[i].actor.id.slot));
    ok = ok && set_owned(actor, "generation", qa_save_value_number((double)rules->players[i].actor.id.generation));
    ok = ok && set_owned(entry, "actor", actor);
    ok = ok && set_owned(entry, "firedWeapon", qa_save_value_boolean(rules->players[i].fired_weapon));
    ok = ok && set_owned(entry, "tookDamage", qa_save_value_boolean(rules->players[i].took_damage));
    if (ok) ok = push_owned(players, entry); else qa_save_value_free(entry);
  }
  ok = ok && set_owned(object, "players", players);
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
void qa_q1_level_rules_restore(qa_q1_level_rules *rules, const qa_save_reader *reader) {
  qa_save_reader field, list;
  must_field(reader, "nextMap", &field);
  const char *next = qa_save_reader_string(&field);
  size_t length = strlen(next);
  if (length >= sizeof rules->next_map) length = sizeof rules->next_map - 1;
  memcpy(rules->next_map, next, length);
  rules->next_map[length] = '\0';
  qa_save_reader_destroy(&field);
  must_field(reader, "stage", &field);
  rules->stage = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  must_field(reader, "exitAfter", &field);
  rules->exit_after = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  rules->player_count = 0;
  must_field(reader, "players", &list);
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, id, slot, generation, value;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("out of memory");
    must_field(&entry, "actor", &id);
    must_field(&id, "slot", &slot);
    int64_t slot_value = (int64_t)saved_integer(&slot);
    qa_save_reader_destroy(&slot);
    must_field(&id, "generation", &generation);
    int64_t generation_value = (int64_t)saved_integer(&generation);
    qa_save_reader_destroy(&generation);
    qa_save_reader_destroy(&id);
    qa_owned_actor actor;
    if (!q1base_resolve_saved(rules->game, slot_value, generation_value, &actor))
      qa_save_reader_fail(&entry, "missing level player");
    player_stats_entry *stats = stats_for(rules, actor, true);
    must_field(&entry, "firedWeapon", &value);
    stats->fired_weapon = qa_save_reader_boolean(&value);
    qa_save_reader_destroy(&value);
    must_field(&entry, "tookDamage", &value);
    stats->took_damage = qa_save_reader_boolean(&value);
    qa_save_reader_destroy(&value);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
}
void qa_q1_level_rules_reset_player(qa_q1_level_rules *rules, qa_owned_actor actor) {
  player_stats_entry *stats = stats_for(rules, actor, true);
  stats->fired_weapon = false;
  stats->took_damage = false;
}
void qa_q1_level_rules_register_rule(qa_q1_level_rules *rules, qa_q1_intermission_rule rule) {
  for (size_t i = 0; i < rules->rule_count; i++)
    if (strcmp(rules->rules[i].id, rule.id) == 0)
      qa_range_error("Duplicate Q1 intermission rule %s", rule.id);
  if (rules->rule_count == rules->rule_capacity) {
    size_t capacity = rules->rule_capacity == 0 ? 4 : rules->rule_capacity * 2;
    qa_q1_intermission_rule *grown = realloc(rules->rules, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    rules->rules = grown;
    rules->rule_capacity = capacity;
  }
  rules->rules[rules->rule_count++] = rule;
}
void qa_q1_level_rules_changelevel_touched(qa_q1_level_rules *rules, qa_q1_actor *trigger, qa_actor_id player) {
  for (size_t i = 0; i < rules->rule_count; i++)
    if (rules->rules[i].touch != NULL) rules->rules[i].touch(rules->rules[i].ctx, trigger, player);
}
void qa_q1_level_rules_travel_to(qa_q1_level_rules *rules, const char *map, const qa_actor_id *cause) {
  for (size_t i = 0; i < rules->rule_count; i++)
    if (rules->rules[i].travel != NULL && rules->rules[i].travel(rules->rules[i].ctx, map, cause)) return;
  qa_q1_travel(rules->game, map, cause);
}
void qa_q1_level_rules_note_attack(qa_q1_level_rules *rules, qa_owned_actor actor, bool axe_only) {
  if (axe_only) return;
  stats_for(rules, actor, true)->fired_weapon = true;
}
void qa_q1_level_rules_note_damage(qa_q1_level_rules *rules, qa_owned_actor actor, double health_damage) {
  if (health_damage == 0) return;
  stats_for(rules, actor, true)->took_damage = true;
}
static void copy_map(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
static void emit_achievement(qa_q1_services *game, const qa_actor_id *player, const char *id) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "achievement";
  event.has_player = player != NULL;
  if (player != NULL) event.player = *player;
  event.id = id;
  qa_q1_emit(game, event);
}
static void uppercase_into(char *dst, size_t size, const char *src) {
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < size; i++) {
    char ch = src[i];
    dst[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
  }
  dst[i] = '\0';
}
void qa_q1_level_rules_begin(qa_q1_level_rules *rules, const char *map, const qa_actor_id *cause) {
  qa_q1_services *game = rules->game;
  copy_map(rules->next_map, sizeof rules->next_map, map);
  rules->stage = 1;
  rules->exit_after = game->time + (game->options.deathmatch != 0 ? 5 : 2);
  for (size_t i = 0; i < rules->rule_count; i++)
    if (rules->rules[i].begin != NULL) rules->rules[i].begin(rules->rules[i].ctx, map, cause);
  qa_q1_begin_intermission(game, map, cause);
  if (strcmp(game->options.edition, "rerelease") == 0) {
    if (qa_q1_option_skill(game) == 3) {
      size_t count = 0;
      const qa_actor_id *players = q1base_players(game, &count);
      for (size_t i = 0; i < count; i++) {
        qa_owned_actor actor;
        if (!qa_session_actor_registry_resolve_owned(game->host.actors, players[i], &actor)) continue;
        player_stats_entry *stats = stats_for(rules, actor, false);
        if (strcmp(game->map_name, "e1m1") == 0 && !(stats != NULL && stats->fired_weapon))
          emit_achievement(game, &players[i], "ACH_PACIFIST");
        if (strcmp(game->map_name, "e4m6") == 0 && !(stats != NULL && stats->took_damage))
          emit_achievement(game, &players[i], "ACH_PAINLESS_MAZE");
      }
    }
    static const char *const COMPLETE_MAPS[] = {"e1m7", "e2m6", "e3m6", "e4m7"};
    bool completed = false;
    for (size_t i = 0; i < 4; i++) if (strcmp(game->map_name, COMPLETE_MAPS[i]) == 0) completed = true;
    if (rules->official_campaign && completed) {
      char id[64], upper[32];
      uppercase_into(upper, sizeof upper, game->map_name);
      (void)snprintf(id, sizeof id, "ACH_COMPLETE_%s", upper);
      /* id outlives emit only by copy in the host mirror. */
      emit_achievement(game, NULL, id);
    }
    bool secret = (strcmp(game->map_name, "e1m4") == 0 && strcmp(map, "e1m8") == 0) ||
                  (strcmp(game->map_name, "e2m3") == 0 && strcmp(map, "e2m7") == 0) ||
                  (strcmp(game->map_name, "e3m4") == 0 && strcmp(map, "e3m7") == 0) ||
                  (strcmp(game->map_name, "e4m5") == 0 && strcmp(map, "e4m8") == 0);
    if (secret) {
      char id[64], upper[32];
      uppercase_into(upper, sizeof upper, map);
      (void)snprintf(id, sizeof id, "ACH_FIND_%s", upper);
      emit_achievement(game, NULL, id);
    }
  }
}
void qa_q1_level_rules_begin_cutscene(qa_q1_level_rules *rules, const char *map,
                                      const qa_actor_id *cause, double exit_after) {
  copy_map(rules->next_map, sizeof rules->next_map, map);
  rules->stage = 1;
  rules->exit_after = exit_after;
  rules->game->has_intermission = true;
  q1base_set_text(&rules->game->intermission_map, map);
  rules->game->has_intermission_cause = cause != NULL;
  if (cause != NULL) rules->game->intermission_cause = *cause;
  rules->game->intermission_exit_after = exit_after;
}
bool qa_q1_level_rules_check_limits(qa_q1_level_rules *rules, double seconds,
                                    const double *scores, size_t score_count,
                                    double timelimit_minutes, double fraglimit) {
  qa_q1_services *game = rules->game;
  if (rules->next_map[0] != '\0' || (timelimit_minutes == 0 && fraglimit == 0)) return false;
  bool time_hit = timelimit_minutes != 0 && seconds >= timelimit_minutes * 60;
  bool frag_hit = false;
  if (fraglimit != 0)
    for (size_t i = 0; i < score_count; i++) if (scores[i] >= fraglimit) frag_hit = true;
  if (!time_hit && !frag_hit) return false;
  qa_q1_services_set_time(game, seconds);
  char next[64];
  copy_map(next, sizeof next, game->map_name);
  if (strcmp(next, "start") == 0) {
    double flags = rules->campaign.read_flags(rules->campaign.ctx);
    int32_t bits = qa_js_to_int32(flags); /* TS flags & x, flags | x: ToInt32 */
    if (!rules->registered) copy_map(next, sizeof next, "e1m1");
    else if ((bits & 1) == 0) { copy_map(next, sizeof next, "e1m1"); rules->campaign.write_flags(rules->campaign.ctx, (double)(bits | 1)); }
    else if ((bits & 2) == 0) { copy_map(next, sizeof next, "e2m1"); rules->campaign.write_flags(rules->campaign.ctx, (double)(bits | 2)); }
    else if ((bits & 4) == 0) { copy_map(next, sizeof next, "e3m1"); rules->campaign.write_flags(rules->campaign.ctx, (double)(bits | 4)); }
    else if ((bits & 8) == 0) { copy_map(next, sizeof next, "e4m1"); rules->campaign.write_flags(rules->campaign.ctx, flags - 7); }
  } else if (game->source_grenade_fix && key_lookup(KEYS(MG_DEATHMATCH_ROTATION), next) != NULL) {
    /* MG1 and MG3 client.qc check the deathmatch maps by name ahead of the
     * trigger_changelevel lookup. */
    copy_map(next, sizeof next, key_lookup(KEYS(MG_DEATHMATCH_ROTATION), next));
  } else {
    qa_q1_actor *const *entities = game->entities;
    const size_t count = game->entity_count;
    for (size_t i = 0; i < count; i++) {
      if (strcmp(entities[i]->classname, "trigger_changelevel") == 0) {
        const char *map = qa_q1_actor_text(entities[i], "map");
        if (map[0] != '\0') copy_map(next, sizeof next, map);
        break;
      }
    }
  }
  copy_map(rules->next_map, sizeof rules->next_map, next);
  qa_q1_actor *timer = qa_q1_create(game, "nextlevel", NULL, NULL);
  qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, "base:next_level"));
  return true;
}
static qa_q1_intermission_result rules_travel(qa_q1_level_rules *rules, double seconds, bool same_level) {
  qa_q1_services *game = rules->game;
  char map[64];
  copy_map(map, sizeof map, same_level ? game->map_name : rules->next_map);
  qa_actor_id cause_id = game->intermission_cause;
  const qa_actor_id *cause = game->has_intermission && game->has_intermission_cause ? &cause_id : NULL;
  qa_q1_services_set_time(game, seconds);
  game->has_intermission = false;
  qa_q1_level_rules_travel_to(rules, map, cause);
  rules->stage = 0;
  qa_q1_intermission_result result = {0};
  result.kind = "travel";
  copy_map(result.map, sizeof result.map, map);
  return result;
}
qa_q1_intermission_result qa_q1_level_rules_request_exit(qa_q1_level_rules *rules,
                                                         double seconds, bool pressed, bool same_level) {
  qa_q1_services *game = rules->game;
  qa_q1_intermission_result waiting = {.kind = "waiting"};
  if (rules->stage == 0 || !pressed || seconds < rules->exit_after) return waiting;
  if (game->options.deathmatch != 0) return rules_travel(rules, seconds, same_level);
  rules->exit_after = seconds + 1;
  rules->stage++;
  for (size_t i = 0; i < rules->rule_count; i++) {
    if (rules->rules[i].finale == NULL) continue;
    qa_q1_intermission_result finale = {0};
    int outcome = rules->rules[i].finale(rules->rules[i].ctx, rules->stage, rules->next_map, &finale);
    if (outcome == 1) return finale;
    if (outcome == 2) return rules_travel(rules, seconds, same_level);
  }
  if (rules->stage == 2) {
    const char *map = game->map_name;
    const char *text = strcmp(map, "e1m7") == 0 ? (rules->registered ? "$qc_finale_e1" : "$qc_finale_e1_shareware")
                       : strcmp(map, "e2m6") == 0 ? "$qc_finale_e2"
                       : strcmp(map, "e3m6") == 0 ? "$qc_finale_e3"
                       : strcmp(map, "e4m7") == 0 ? "$qc_finale_e4" : NULL;
    if (text != NULL) {
      qa_q1_intermission_result result = {.kind = "finale", .track = 2};
      (void)snprintf(result.text, sizeof result.text, "%s", qa_q1_finale_text(game->options.edition, text));
      return result;
    }
    return rules_travel(rules, seconds, same_level);
  }
  if (rules->stage == 3) {
    if (!rules->registered) {
      qa_q1_intermission_result result = {.kind = "sell-screen"};
      return result;
    }
    if ((qa_js_to_int32(rules->campaign.read_flags(rules->campaign.ctx)) & 15) == 15) {
      qa_q1_intermission_result result = {.kind = "finale", .track = 2};
      (void)snprintf(result.text, sizeof result.text, "%s",
                     qa_q1_finale_text(game->options.edition, "$qc_finale_all_runes"));
      return result;
    }
  }
  return rules_travel(rules, seconds, same_level);
}
qa_q1_intermission_result qa_q1_level_rules_client_connected(qa_q1_level_rules *rules,
                                                             double seconds, bool same_level) {
  qa_q1_intermission_result waiting = {.kind = "waiting"};
  if (rules->stage == 0) return waiting;
  rules->exit_after = seconds;
  return qa_q1_level_rules_request_exit(rules, seconds, true, same_level);
}
qa_q1_intermission_result qa_q1_level_rules_advance_finale(qa_q1_level_rules *rules, double seconds) {
  return qa_q1_level_rules_client_connected(rules, seconds, false);
}
void qa_q1_level_rules_defer_exit(qa_q1_level_rules *rules, double until_seconds) {
  rules->exit_after = until_seconds;
  if (rules->game->has_intermission) rules->game->intermission_exit_after = until_seconds;
}
