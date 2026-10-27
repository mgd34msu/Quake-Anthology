/* Port of src/content/q1/base/rules.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/rules.h"

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
  qa_q1b_game *game;
  qa_q1_campaign_binding campaign;
  qa_q1b_actor *last_spawn;
  selection_entry *selections; size_t selection_count; size_t selection_capacity;
};
qa_q1_spawn_selector *qa_q1_spawn_selector_create(qa_q1b_game *game, qa_q1_campaign_binding campaign) {
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
  if (!selector->game->host->actors_resolve_saved(selector->game->host->ctx, slot_value, generation_value, &actor))
    qa_save_reader_fail(reader, "missing spawn point");
  selector->last_spawn = selector->game->entity(selector->game, qa_q1b_some_actor(actor.id));
}
static bool selector_nearby(qa_q1_spawn_selector *selector, qa_q1b_actor *point, double radius, bool living) {
  qa_q1b_game *game = selector->game;
  qa_actor_id players[64];
  size_t count = game->host->players(game->host->ctx, players, 64);
  for (size_t i = 0; i < count; i++) {
    if (living && game->health(game, players[i]) <= 0) continue;
    qa_q1b_body body;
    if (!game->host->bodies_read(game->host->ctx, players[i], &body)) continue;
    qa_vec3 center = qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5));
    if (qa_length3(qa_sub3(center, game->body(game, point).origin)) <= radius) return true;
  }
  return false;
}
static bool selector_visible(qa_q1_spawn_selector *selector, qa_q1b_actor *point) {
  qa_q1b_game *game = selector->game;
  qa_actor_id players[64];
  size_t count = game->host->players(game->host->ctx, players, 64);
  for (size_t i = 0; i < count; i++) {
    if (game->health(game, players[i]) <= 0) continue;
    qa_q1b_body body;
    if (!game->host->bodies_read(game->host->ctx, players[i], &body)) continue;
    qa_q1b_trace_request request = {
        qa_add3(game->body(game, point).origin, V3(0, 0, 22)),
        qa_add3(body.origin, V3(0, 0, 22)), POINT_BOUNDS,
        qa_q1b_some_actor(point->actor.id), false, false};
    qa_q1b_trace trace = game->host->trace(game->host->ctx, &request);
    if (trace.fraction >= 1) return true;
  }
  return false;
}
static qa_q1b_actor *find_classname(qa_q1b_actor *const *entities, size_t count, const char *classname) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(entities[i]->classname, classname) == 0) return entities[i];
  return NULL;
}
qa_q1b_actor *qa_q1_spawn_selector_select(qa_q1_spawn_selector *selector, bool force_spawn) {
  qa_q1b_game *game = selector->game;
  for (size_t i = 0; i < selector->selection_count; i++) {
    qa_q1b_actor *point = NULL;
    int result = selector->selections[i].fn(selector->selections[i].ctx, force_spawn, &point);
    if (result == 1) return point;
    if (result == 2) return NULL;
  }
  qa_q1b_actor *entities[4096];
  size_t count = game->entities(game, entities, 4096);
  qa_q1b_actor *region = find_classname(entities, count, "testplayerstart");
  if (region != NULL) return region;
  if (game->options.coop) {
    long after = -1;
    if (selector->last_spawn != NULL)
      for (size_t i = 0; i < count; i++) if (entities[i] == selector->last_spawn) { after = (long)i; break; }
    bool activated = false;
    for (size_t i = 0; i < count; i++)
      if (strcmp(entities[i]->classname, "info_player_coop") == 0 && entities[i]->activated) activated = true;
    qa_q1b_actor *point = NULL;
    for (size_t i = (size_t)(after + 1); i < count && point == NULL; i++)
      if (strcmp(entities[i]->classname, "info_player_coop") == 0 && (!activated || entities[i]->activated)) point = entities[i];
    if (point == NULL)
      for (size_t i = 0; i < count && point == NULL; i++)
        if (strcmp(entities[i]->classname, "info_player_coop") == 0 && (!activated || entities[i]->activated)) point = entities[i];
    if (point == NULL) point = find_classname(entities, count, "info_player_start");
    if (point != NULL) { selector->last_spawn = point; return point; }
  } else if (game->options.deathmatch != 0) {
    qa_q1b_actor *points[1024];
    size_t point_count = 0;
    for (size_t i = 0; i < count && point_count < 1024; i++)
      if (strcmp(entities[i]->classname, "info_player_deathmatch") == 0) points[point_count++] = entities[i];
    if (point_count == 0) qa_range_error("No info_player_deathmatch on level");
    if (strcmp(game->options.edition, "classic") == 0) {
      long start = -1;
      if (selector->last_spawn != NULL)
        for (size_t i = 0; i < point_count; i++) if (points[i] == selector->last_spawn) { start = (long)i; break; }
      for (size_t i = 1; i <= point_count; i++) {
        qa_q1b_actor *point = points[(size_t)(start + (long)i) % point_count];
        if (point == selector->last_spawn || !selector_nearby(selector, point, 32, false)) {
          selector->last_spawn = point;
          return point;
        }
      }
      return force_spawn ? points[0] : NULL;
    }
    qa_q1b_actor *available[1024];
    size_t available_count = 0;
    for (size_t i = point_count; i-- > 0;)
      if (!selector_nearby(selector, points[i], 384, true) && !selector_visible(selector, points[i]))
        available[available_count++] = points[i];
    if (available_count == 0)
      for (size_t i = point_count; i-- > 0;)
        if (!selector_nearby(selector, points[i], 84, true)) available[available_count++] = points[i];
    if (available_count == 0) {
      if (!force_spawn) return NULL;
      size_t index = (size_t)floor(game->host->random(game->host->ctx) * ((double)point_count - 1) + 0.5);
      return index < point_count ? points[index] : NULL;
    }
    size_t index = (size_t)floor(game->host->random(game->host->ctx) * ((double)available_count - 1) + 0.5);
    return index < available_count ? available[index] : NULL;
  }
  if (selector->campaign.read_flags(selector->campaign.ctx) != 0) {
    qa_q1b_actor *returned = find_classname(entities, count, "info_player_start2");
    if (returned != NULL) return returned;
  }
  qa_q1b_actor *start = find_classname(entities, count, "info_player_start");
  if (start == NULL) qa_range_error("PutClientInServer: no info_player_start on level");
  return start;
}

/* ---------- q1ClientNotice / q1Obituary ---------- */
qa_q1_client_notice qa_q1_client_notice_make(const char *edition, const char *event,
                                             const char *name, double frags) {
  qa_q1_client_notice notice = {0};
  notice.score_delta = strcmp(event, "suicide") == 0 ? -2 : 0;
  if (strcmp(edition, "rerelease") == 0) {
    notice.key = strcmp(event, "connect") == 0 ? "$qc_entered"
                 : strcmp(event, "disconnect") == 0 ? "$qc_left_game"
                 : strcmp(event, "suicide") == 0 ? "$qc_suicides" : "$qc_exited";
    (void)snprintf(notice.text, sizeof notice.text, "%s", notice.key);
    notice.arguments[0] = name;
    notice.argument_count = 1;
    if (strcmp(event, "disconnect") == 0) {
      qa_q1b_format_number(notice.frags, sizeof notice.frags, frags);
      notice.arguments[1] = notice.frags;
      notice.argument_count = 2;
    }
    return notice;
  }
  char frags_text[32];
  qa_q1b_format_number(frags_text, sizeof frags_text, frags);
  if (strcmp(event, "connect") == 0) (void)snprintf(notice.text, sizeof notice.text, "%s entered the game\n", name);
  else if (strcmp(event, "disconnect") == 0) (void)snprintf(notice.text, sizeof notice.text, "%s left the game with %s frags\n", name, frags_text);
  else if (strcmp(event, "suicide") == 0) (void)snprintf(notice.text, sizeof notice.text, "%s suicides\n", name);
  else (void)snprintf(notice.text, sizeof notice.text, "%s exited the level\n", name);
  notice.argument_count = 0;
  return notice;
}
typedef struct obituary_ctx {
  const qa_q1_obituary_input *input;
} obituary_ctx;
static qa_q1_obituary obituary_result(const qa_q1_obituary_input *input, const char *text,
                                      qa_actor_id credited, double delta,
                                      const char *const *args, size_t arg_count,
                                      bool has_achievement, qa_actor_id achievement_actor,
                                      const char *achievement_id) {
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
  return result;
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
    return obituary_result(input, "$qc_telefragged", input->telefrag_owner->actor, 1, args, 2, false, victim->actor, NULL);
  }
  if (attacker != NULL && strcmp(attacker->classname, "teledeath2") == 0)
    return obituary_result(input, "$qc_satans_power", victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
  if (attacker != NULL && attacker->is_player) {
    if (qa_same_actor(victim->actor, attacker->actor)) {
      if (victim->weapon != NULL && strcmp(victim->weapon, "lightning") == 0 && victim->water_level > 1) {
        const char *key = strcmp(victim->water_type, "slime") == 0 ? "$qc_discharge_slime"
                          : strcmp(victim->water_type, "lava") == 0 ? "$qc_discharge_lava" : "$qc_discharge_water";
        return obituary_result(input, key, victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
      }
      const char *key = victim->weapon != NULL && strcmp(victim->weapon, "grenadelauncher") == 0
                            ? "$qc_suicide_pin" : roll != 0 ? "$qc_suicide_bored" : "$qc_suicide_loaded";
      return obituary_result(input, key, victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
    }
    bool classic = strcmp(input->edition, "classic") == 0;
    if (input->teamplay == 2 && victim->team == attacker->team &&
        (classic ? victim->team > 0 : attacker->team != 0)) {
      const char *key = roll < 0.25 ? "$qc_ff_teammate" : roll < 0.5 ? "$qc_ff_glasses"
                        : roll < 0.75 ? "$qc_ff_otherteam" : "$qc_ff_friend";
      const char *args[1] = {attacker->name};
      return obituary_result(input, key, attacker->actor, -1, args, 1, false, victim->actor, NULL);
    }
    const char *args[2] = {victim->name, attacker->name};
    const char *weapon = attacker->weapon;
    if (weapon != NULL && strcmp(weapon, "axe") == 0)
      return obituary_result(input, "$qc_death_ax", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    if (weapon != NULL && strcmp(weapon, "shotgun") == 0)
      return obituary_result(input, "$qc_death_sg", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    if (weapon != NULL && strcmp(weapon, "supershotgun") == 0)
      return obituary_result(input, "$qc_death_dbl", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    if (weapon != NULL && strcmp(weapon, "nailgun") == 0)
      return obituary_result(input, "$qc_death_nail", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    if (weapon != NULL && strcmp(weapon, "supernailgun") == 0)
      return obituary_result(input, "$qc_death_sng", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    if (weapon != NULL && strcmp(weapon, "grenadelauncher") == 0)
      return obituary_result(input, victim->health < -40 ? "$qc_death_gl1" : "$qc_death_gl2", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    if (weapon != NULL && strcmp(weapon, "rocketlauncher") == 0) {
      if (!classic && attacker->quad_expires > 0 && victim->health < -40) {
        double r = input->random(input->random_ctx);
        return obituary_result(input, r < 0.3 ? "$qc_death_rl_quad1" : r < 0.6 ? "$qc_death_rl_quad2" : "$qc_death_rl1",
                               attacker->actor, 1, args, 2, false, victim->actor, NULL);
      }
      return obituary_result(input, victim->health < -40 ? "$qc_death_rl2" : "$qc_death_rl3", attacker->actor, 1, args, 2, false, victim->actor, NULL);
    }
    if (weapon != NULL && strcmp(weapon, "lightning") == 0) {
      bool achievement = !classic && attacker->water_level > 1 && attacker->invulnerable_expires != 0;
      return obituary_result(input, attacker->water_level > 1 ? "$qc_death_lg1" : "$qc_death_lg2",
                             attacker->actor, 1, args, 2, achievement, attacker->actor,
                             achievement ? "ACH_SURVIVE_DISCHARGE" : NULL);
    }
    return obituary_result(input, attacker->kill_string, attacker->actor, 1, args, 2, false, victim->actor, NULL);
  }
  if (strcmp(input->edition, "classic") == 0 && attacker != NULL) {
    if (attacker->is_monster) {
      char composed[512];
      const char *suffix = qa_q1_classic_monster_obituary(attacker->classname);
      (void)snprintf(composed, sizeof composed, "%s%s", victim->name, suffix != NULL ? suffix : "");
      return obituary_result(input, composed, victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
    }
    const char *trap = NULL;
    if (strcmp(attacker->classname, "explo_box") == 0 || strcmp(attacker->classname, "misc_explobox") == 0 ||
        strcmp(attacker->classname, "misc_explobox2") == 0) trap = " blew up\n";
    else if (attacker->brush && strcmp(attacker->classname, "worldspawn") != 0) trap = " was squished\n";
    else if (strcmp(attacker->classname, "trap_shooter") == 0 || strcmp(attacker->classname, "trap_spikeshooter") == 0) trap = " was spiked\n";
    else if (strcmp(attacker->classname, "fireball") == 0 || strcmp(attacker->classname, "misc_fireball") == 0) trap = " ate a lavaball\n";
    else if (strcmp(attacker->classname, "trigger_changelevel") == 0) trap = " tried to leave\n";
    if (trap != NULL) {
      char composed[512];
      (void)snprintf(composed, sizeof composed, "%s%s", victim->name, trap);
      return obituary_result(input, composed, victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
    }
  }
  if (strcmp(victim->water_type, "water") == 0)
    return obituary_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_drown1" : "$qc_death_drown2",
                           victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
  if (strcmp(victim->water_type, "slime") == 0)
    return obituary_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_slime1" : "$qc_death_slime2",
                           victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
  if (strcmp(victim->water_type, "lava") == 0) {
    const char *key = victim->health < -15 ? "$qc_death_lava1"
                      : input->random(input->random_ctx) < 0.5 ? "$qc_death_lava2" : "$qc_death_lava3";
    return obituary_result(input, key, victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
  }
  if (attacker != NULL && attacker->brush && strcmp(attacker->classname, "worldspawn") != 0)
    return obituary_result(input, "$qc_death_squish", victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
  if (attacker != NULL && attacker->kill_string[0] != '\0')
    return obituary_result(input, attacker->kill_string, victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
  return obituary_result(input, strcmp(input->death_type, "falling") == 0 ? "$qc_death_fall" : "$qc_death_died",
                         victim->actor, -1, victim_args, 1, false, victim->actor, NULL);
}

/* ---------- Q1LevelRules ---------- */
typedef struct player_stats_entry {
  qa_owned_actor actor; bool fired_weapon; bool took_damage;
} player_stats_entry;
struct qa_q1_level_rules {
  qa_q1b_game *game;
  qa_q1_campaign_binding campaign;
  bool registered; bool official_campaign;
  char next_map[64];
  double stage; double exit_after;
  player_stats_entry *players; size_t player_count; size_t player_capacity;
  qa_q1_intermission_rule *rules; size_t rule_count; size_t rule_capacity;
};
static player_stats_entry *stats_for(qa_q1_level_rules *rules, qa_owned_actor actor, bool create) {
  for (size_t i = 0; i < rules->player_count; i++)
    if (qa_q1b_owned_equal(rules->players[i].actor, actor)) return &rules->players[i];
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
    if (qa_q1b_owned_equal(rules->players[i].actor, actor)) {
      memmove(&rules->players[i], &rules->players[i + 1],
              (rules->player_count - i - 1) * sizeof *rules->players);
      rules->player_count--;
      return;
    }
  }
}
typedef struct next_level_ctx { qa_q1_level_rules *rules; } next_level_ctx;
static void next_level_action(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  next_level_ctx *c = ctx;
  qa_q1_level_rules_begin(c->rules, c->rules->next_map, QA_Q1B_NO_ACTOR);
  game->remove(game, entity);
}
qa_q1_level_rules *qa_q1_level_rules_create(qa_q1b_game *game, qa_q1_campaign_binding campaign,
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
  qa_q1b_handlers handlers = {.ctx = c, .action = next_level_action};
  game->named.register_named(game->named.ctx, "base:next_level", &handlers);
  game->host->actors_on_release(game->host->ctx, rules_on_release, rules);
  return rules;
}
void qa_q1_level_rules_destroy(qa_q1_level_rules *rules) {
  if (rules == NULL) return;
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
    if (!rules->game->host->actors_resolve_saved(rules->game->host->ctx, slot_value, generation_value, &actor))
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
void qa_q1_level_rules_changelevel_touched(qa_q1_level_rules *rules, qa_q1b_actor *trigger, qa_actor_id player) {
  for (size_t i = 0; i < rules->rule_count; i++)
    if (rules->rules[i].touch != NULL) rules->rules[i].touch(rules->rules[i].ctx, trigger, player);
}
void qa_q1_level_rules_travel_to(qa_q1_level_rules *rules, const char *map, qa_q1b_opt_actor cause) {
  for (size_t i = 0; i < rules->rule_count; i++)
    if (rules->rules[i].travel != NULL && rules->rules[i].travel(rules->rules[i].ctx, map, cause)) return;
  rules->game->travel(rules->game, map, cause);
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
static void emit_achievement(qa_q1b_game *game, qa_q1b_opt_actor player, const char *id) {
  qa_q1b_event event = {0};
  event.kind = "achievement";
  event.player = player;
  event.id = id;
  game->host->emit(game->host->ctx, &event);
}
static void uppercase_into(char *dst, size_t size, const char *src) {
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < size; i++) {
    char ch = src[i];
    dst[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
  }
  dst[i] = '\0';
}
void qa_q1_level_rules_begin(qa_q1_level_rules *rules, const char *map, qa_q1b_opt_actor cause) {
  qa_q1b_game *game = rules->game;
  copy_map(rules->next_map, sizeof rules->next_map, map);
  rules->stage = 1;
  rules->exit_after = game->time + (game->options.deathmatch != 0 ? 5 : 2);
  for (size_t i = 0; i < rules->rule_count; i++)
    if (rules->rules[i].begin != NULL) rules->rules[i].begin(rules->rules[i].ctx, map, cause);
  game->begin_intermission(game, map, cause);
  if (strcmp(game->options.edition, "rerelease") == 0) {
    if (game->options.skill == 3) {
      qa_actor_id players[64];
      size_t count = game->host->players(game->host->ctx, players, 64);
      for (size_t i = 0; i < count; i++) {
        qa_owned_actor actor;
        if (!game->host->actors_resolve_owned(game->host->ctx, players[i], &actor)) continue;
        player_stats_entry *stats = stats_for(rules, actor, false);
        if (strcmp(game->map_name, "e1m1") == 0 && !(stats != NULL && stats->fired_weapon))
          emit_achievement(game, qa_q1b_some_actor(players[i]), "ACH_PACIFIST");
        if (strcmp(game->map_name, "e4m6") == 0 && !(stats != NULL && stats->took_damage))
          emit_achievement(game, qa_q1b_some_actor(players[i]), "ACH_PAINLESS_MAZE");
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
      emit_achievement(game, QA_Q1B_NO_ACTOR, id);
    }
    bool secret = (strcmp(game->map_name, "e1m4") == 0 && strcmp(map, "e1m8") == 0) ||
                  (strcmp(game->map_name, "e2m3") == 0 && strcmp(map, "e2m7") == 0) ||
                  (strcmp(game->map_name, "e3m4") == 0 && strcmp(map, "e3m7") == 0) ||
                  (strcmp(game->map_name, "e4m5") == 0 && strcmp(map, "e4m8") == 0);
    if (secret) {
      char id[64], upper[32];
      uppercase_into(upper, sizeof upper, map);
      (void)snprintf(id, sizeof id, "ACH_FIND_%s", upper);
      emit_achievement(game, QA_Q1B_NO_ACTOR, id);
    }
  }
}
void qa_q1_level_rules_begin_cutscene(qa_q1_level_rules *rules, const char *map,
                                      qa_q1b_opt_actor cause, double exit_after) {
  copy_map(rules->next_map, sizeof rules->next_map, map);
  rules->stage = 1;
  rules->exit_after = exit_after;
  rules->game->has_intermission = true;
  copy_map(rules->game->intermission_map, sizeof rules->game->intermission_map, map);
  rules->game->intermission_cause = cause;
  rules->game->intermission_exit_after = exit_after;
}
bool qa_q1_level_rules_check_limits(qa_q1_level_rules *rules, double seconds,
                                    const double *scores, size_t score_count,
                                    double timelimit_minutes, double fraglimit) {
  qa_q1b_game *game = rules->game;
  if (rules->next_map[0] != '\0' || (timelimit_minutes == 0 && fraglimit == 0)) return false;
  bool time_hit = timelimit_minutes != 0 && seconds >= timelimit_minutes * 60;
  bool frag_hit = false;
  if (fraglimit != 0)
    for (size_t i = 0; i < score_count; i++) if (scores[i] >= fraglimit) frag_hit = true;
  if (!time_hit && !frag_hit) return false;
  qa_q1b_game_set_time(game, seconds);
  char next[64];
  copy_map(next, sizeof next, game->map_name);
  if (strcmp(next, "start") == 0) {
    double flags = rules->campaign.read_flags(rules->campaign.ctx);
    long bits = (long)flags;
    if (!rules->registered) copy_map(next, sizeof next, "e1m1");
    else if ((bits & 1) == 0) { copy_map(next, sizeof next, "e1m1"); rules->campaign.write_flags(rules->campaign.ctx, (double)(bits | 1)); }
    else if ((bits & 2) == 0) { copy_map(next, sizeof next, "e2m1"); rules->campaign.write_flags(rules->campaign.ctx, (double)(bits | 2)); }
    else if ((bits & 4) == 0) { copy_map(next, sizeof next, "e3m1"); rules->campaign.write_flags(rules->campaign.ctx, (double)(bits | 4)); }
    else if ((bits & 8) == 0) { copy_map(next, sizeof next, "e4m1"); rules->campaign.write_flags(rules->campaign.ctx, flags - 7); }
  } else {
    qa_q1b_actor *entities[4096];
    size_t count = game->entities(game, entities, 4096);
    for (size_t i = 0; i < count; i++) {
      if (strcmp(entities[i]->classname, "trigger_changelevel") == 0) {
        const char *map = qa_q1b_actor_text(entities[i], "map");
        if (map[0] != '\0') copy_map(next, sizeof next, map);
        break;
      }
    }
  }
  copy_map(rules->next_map, sizeof rules->next_map, next);
  qa_q1b_actor *timer = game->create(game, "nextlevel");
  game->schedule(game, timer, 0.1, game->named.action(game->named.ctx, timer, "base:next_level"));
  return true;
}
static qa_q1_intermission_result rules_travel(qa_q1_level_rules *rules, double seconds, bool same_level) {
  qa_q1b_game *game = rules->game;
  char map[64];
  copy_map(map, sizeof map, same_level ? game->map_name : rules->next_map);
  qa_q1b_opt_actor cause = game->has_intermission ? game->intermission_cause : QA_Q1B_NO_ACTOR;
  qa_q1b_game_set_time(game, seconds);
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
  qa_q1b_game *game = rules->game;
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
    if (((long)rules->campaign.read_flags(rules->campaign.ctx) & 15) == 15) {
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
