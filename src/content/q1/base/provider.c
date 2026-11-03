/* Port of src/content/q1/base/provider.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/provider.h"

#include "base_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/finales.h"
#include "qa/content/q1/base/map_entities.h"
#include "qa/content/q1/base/player.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
static const qa_vec3 ZERO3 = {0, 0, 0};

/* TS Q1CampaignState. */
static double state_read_flags(void *ctx) { return ((qa_q1_campaign_state *)ctx)->flags; }
static void state_write_flags(void *ctx, double flags) { ((qa_q1_campaign_state *)ctx)->flags = flags; }
static void state_set_skill(void *ctx, int skill) { ((qa_q1_campaign_state *)ctx)->skill = skill; }
void qa_q1_campaign_state_init(qa_q1_campaign_state *state, double flags, int skill) {
  state->flags = flags;
  state->skill = skill;
  state->binding = (qa_q1_campaign_binding){.ctx = state, .read_flags = state_read_flags,
                                            .write_flags = state_write_flags, .set_skill = state_set_skill};
}

size_t qa_q1_base_classnames(const char *const **out) {
  static const char *names[256];
  static size_t count;
  static bool built;
  if (!built) {
    built = true;
    for (size_t s = 0; s < QA_Q1_BASE_SPECIES_COUNT; s++)
      for (size_t c = 0; c < QA_Q1_BASE_SPECIES[s].classname_count; c++)
        names[count++] = QA_Q1_BASE_SPECIES[s].classnames[c];
    for (size_t i = 0; i < QA_Q1_REMAINING_MAP_CLASSNAME_COUNT; i++)
      names[count++] = QA_Q1_REMAINING_MAP_CLASSNAMES[i];
  }
  *out = names;
  return count;
}

typedef struct map_spawn { struct qa_q1_base *base; } map_spawn;
typedef struct kill_rule { char *id; qa_q1_kill_count_rule_fn fn; void *ctx; } kill_rule;
struct qa_q1_base {
  qa_q1_services *game;
  qa_q1_base_options options;
  qa_q1_campaign_state campaign_state;
  qa_q1_campaign_binding campaign;
  bool registered;
  qa_q1_creatures *creatures;
  qa_q1_level_rules *level_rules;
  qa_q1_spawn_selector *spawn_selector;
  kill_rule *kill_rules; size_t kill_rule_count; size_t kill_rule_capacity;
  double lightning_end;
  bool has_electrodes; qa_q1_actor *electrodes[2];
  bool finale_started;
  bool finale_dismissed;
  qa_q1_player_extension level_stats_extension;
  qa_q1_state_extension state_extension;
  struct map_spawn *map_spawn;
};
typedef struct registration { qa_q1_services *game; qa_q1_base *base; } registration;
static registration REGISTRY[64];
static size_t REGISTRY_COUNT;

qa_q1_base *qa_q1_base_for(qa_q1_services *game) {
  for (size_t i = 0; i < REGISTRY_COUNT; i++)
    if (REGISTRY[i].game == game) return REGISTRY[i].base;
  qa_range_error("Q1 base content was not registered");
}
qa_q1_services *qa_q1_base_game(qa_q1_base *base) { return base->game; }
const qa_q1_base_options *qa_q1_base_options_of(qa_q1_base *base) { return &base->options; }
qa_q1_creatures *qa_q1_base_creatures(qa_q1_base *base) { return base->creatures; }
qa_q1_level_rules *qa_q1_base_level_rules(qa_q1_base *base) { return base->level_rules; }
qa_q1_spawn_selector *qa_q1_base_spawn_selector(qa_q1_base *base) { return base->spawn_selector; }
qa_q1_campaign_binding qa_q1_base_campaign(qa_q1_base *base) { return base->campaign; }
bool qa_q1_base_registered(qa_q1_base *base) { return base->registered; }

static bool base_services_count(void *ctx, qa_q1_base_monster *monster) {
  return qa_q1_base_count_monster_kill(ctx, monster);
}
static const char *base_services_melee(void *ctx) {
  return qa_q1_base_next_hell_knight_melee(ctx);
}
static void base_services_finale(void *ctx, qa_q1_base_monster *monster) {
  qa_q1_base_finale(ctx, monster);
}
static void base_services_finish(void *ctx, qa_q1_base_monster *monster) {
  qa_q1_base_finish_finale(ctx, monster);
}
qa_q1_monster_services qa_q1_base_as_monster_services(qa_q1_base *base) {
  qa_q1_monster_services services = {.ctx = base, .count_monster_kill = base_services_count,
                                     .next_hell_knight_melee = base_services_melee,
                                     .finale = base_services_finale, .finish_finale = base_services_finish};
  return services;
}
static void attach_level_stats(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game;
  qa_q1_base *base = ctx;
  qa_q1_level_rules_reset_player(base->level_rules, player->actor);
}
static qa_q1_base_monster *require_monster(qa_q1_base *base, qa_q1_actor *entity) {
  qa_q1_base_monster *monster = qa_q1_creatures_monsters_get(base->creatures, entity->actor);
  if (monster == NULL) qa_range_error("Missing Q1 monster controller for %s", entity->classname);
  return monster;
}
static void lightning_fire_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity);
static void use_lightning(qa_q1_base *base, qa_q1_actor *entity, const qa_actor_id *activator);
static void fire_lightning(qa_q1_base *base, qa_q1_actor *entity);
static void lightning_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity,
                          const qa_actor_id *other, const qa_actor_id *activator) {
  (void)game; (void)other;
  use_lightning(ctx, entity, activator);
}
static void lightning_fire_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  fire_lightning(ctx, entity);
}
static void finale_2(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  qa_q1_base *base = ctx;
  qa_q1_actor *owner = timer->has_owner ? qa_q1_get_entity(game, &timer->owner) : NULL;
  if (owner == NULL) qa_range_error("Finale timer lost Shub");
  qa_q1_base_monster *shub = require_monster(base, owner);
  qa_q1_effect(game, "teleport", qa_sub3(qa_q1_base_monster_origin(shub), V3(0, 100, 0)), NULL, 1);
  qa_q1_sound_defaults(game, owner->actor.id, "misc/r_tele1.wav");
  qa_q1_event event = {0};
  event.kind = "finale"; event.text = ""; event.stage = 2;
  qa_q1_emit(game, event);
  qa_q1_schedule(game, timer, 2, qa_q1_callback_registry_action(game->named, timer, "base:finale_3"));
}
static void finale_3(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  qa_q1_base *base = ctx;
  qa_q1_actor *owner = timer->has_owner ? qa_q1_get_entity(game, &timer->owner) : NULL;
  if (owner == NULL) qa_range_error("Finale timer lost Shub");
  qa_q1_base_monster *shub = require_monster(base, owner);
  qa_q1_sound_defaults(game, owner->actor.id, "boss2/death.wav");
  qa_q1_event event = {0};
  event.kind = "lightstyle"; event.style = 0; event.pattern = "abcdefghijklmlkjihgfedcb";
  qa_q1_emit(game, event);
  event = (qa_q1_event){0};
  event.kind = "finale"; event.text = ""; event.stage = 3;
  qa_q1_emit(game, event);
  size_t length = strlen("old_thrash1");
  memcpy(shub->next_frame, "old_thrash1", length + 1);
  qa_q1_base_monster_delay(shub, 0.1);
  qa_q1_remove(game, timer);
}
static void finale_wait(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  qa_q1_base *base = ctx;
  if (!qa_q1_base_has_finished_finale(base)) {
    qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, "base:finale_wait"));
    return;
  }
  qa_q1_event event = {0};
  event.kind = "finale"; event.text = ""; event.stage = 5;
  qa_q1_emit(game, event);
  qa_q1_schedule(game, timer, 5, qa_q1_callback_registry_action(game->named, timer, "base:finale_6"));
}
static void finale_6(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  qa_q1_base *base = ctx;
  qa_q1_event event = {0};
  event.kind = "finale"; event.text = ""; event.stage = 6;
  qa_q1_emit(game, event);
  if (game->options.coop) qa_q1_travel(game, "start", NULL);
  else if (base->options.finish_campaign != NULL) base->options.finish_campaign(base->options.ctx);
  qa_q1_remove(game, timer);
}
static bool set_owned(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool base_capture(void *ctx, uint8_t **out, size_t *out_length);
static void base_restore(void *ctx, const uint8_t *bytes, size_t length);
static void base_clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1_base *base = ctx;
  qa_q1_creatures_clone(base->creatures, source, target);
}

static void spawn_map(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1_spawn_remaining_map_actor(((map_spawn *)ctx)->base, entity);
}

qa_q1_base *qa_q1_base_create(qa_q1_services *game, const qa_q1_base_options *options) {
  qa_q1_base *base = malloc(sizeof *base);
  if (base == NULL) return NULL;
  memset(base, 0, sizeof *base);
  base->game = game;
  if (options != NULL) base->options = *options;
  if (base->options.campaign != NULL) {
    base->campaign = *base->options.campaign;
  } else {
    qa_q1_campaign_state_init(&base->campaign_state, 0, qa_q1_option_skill(game));
    base->campaign = base->campaign_state.binding;
  }
  base->registered = base->options.has_registered ? base->options.registered : true;
  base->lightning_end = -1;
  /* TS providers.set: a later registration for the same game replaces the earlier one. */
  size_t entry = 0;
  while (entry < REGISTRY_COUNT && REGISTRY[entry].game != game) entry++;
  if (entry == REGISTRY_COUNT) {
    if (REGISTRY_COUNT == 64) qa_range_error("Q1 base registry is full");
    REGISTRY_COUNT++;
  }
  REGISTRY[entry].game = game;
  REGISTRY[entry].base = base;
  bool official = base->options.has_official_campaign ? base->options.official_campaign
                  : game->options.campaign != NULL &&
                        strlen(game->options.campaign) >= 4 &&
                        strcmp(game->options.campaign + strlen(game->options.campaign) - 4, ":id1") == 0;
  base->level_rules = qa_q1_level_rules_create(game, base->campaign, base->registered, official);
  base->spawn_selector = qa_q1_spawn_selector_create(game, base->campaign);
  /* The foundation keeps extensions by pointer: they live on the base. */
  base->level_stats_extension = (qa_q1_player_extension){.id = "q1:base-level-stats", .ctx = base,
                                                         .attach = attach_level_stats};
  qa_q1_register_player_extension(game, &base->level_stats_extension);
  qa_q1_monster_services services = {.ctx = base, .count_monster_kill = base_services_count,
                                     .next_hell_knight_melee = base_services_melee,
                                     .finale = base_services_finale, .finish_finale = base_services_finish};
  base->creatures = qa_q1_creatures_create(game, services);
  qa_q1_register_map_callbacks(game);
  qa_q1_register_character_callbacks(game);
  qa_q1_callback_handlers handlers;
  handlers = (qa_q1_callback_handlers){.ctx = base, .use = lightning_use};
  qa_q1_callback_registry_register(game->named, "base:lightning_use", handlers);
  handlers = (qa_q1_callback_handlers){.ctx = base, .action = lightning_fire_action};
  qa_q1_callback_registry_register(game->named, "base:lightning_fire", handlers);
  handlers = (qa_q1_callback_handlers){.ctx = base, .action = finale_2};
  qa_q1_callback_registry_register(game->named, "base:finale_2", handlers);
  handlers = (qa_q1_callback_handlers){.ctx = base, .action = finale_3};
  qa_q1_callback_registry_register(game->named, "base:finale_3", handlers);
  handlers = (qa_q1_callback_handlers){.ctx = base, .action = finale_wait};
  qa_q1_callback_registry_register(game->named, "base:finale_wait", handlers);
  handlers = (qa_q1_callback_handlers){.ctx = base, .action = finale_6};
  qa_q1_callback_registry_register(game->named, "base:finale_6", handlers);
  base->state_extension = (qa_q1_state_extension){.id = "q1:base", .ctx = base,
                                                  .capture = base_capture, .restore = base_restore,
                                                  .clone = base_clone};
  qa_q1_register_state_extension(game, &base->state_extension);
  qa_q1_creatures_register_species(base->creatures, QA_Q1_BASE_SPECIES, QA_Q1_BASE_SPECIES_COUNT);
  map_spawn *spawn = malloc(sizeof *spawn);
  if (spawn == NULL) qa_range_error("out of memory");
  spawn->base = base;
  base->map_spawn = spawn;
  for (size_t i = 0; i < QA_Q1_REMAINING_MAP_CLASSNAME_COUNT; i++)
    qa_q1_register_spawn(game, QA_Q1_REMAINING_MAP_CLASSNAMES[i], spawn_map, spawn);
  return base;
}
qa_q1_base *qa_q1_register_base(qa_q1_services *game, const qa_q1_base_options *options) {
  return qa_q1_base_create(game, options);
}
void qa_q1_base_destroy(qa_q1_base *base) {
  if (base == NULL) return;
  for (size_t i = 0; i < REGISTRY_COUNT; i++) {
    if (REGISTRY[i].base == base) {
      memmove(&REGISTRY[i], &REGISTRY[i + 1], (REGISTRY_COUNT - i - 1) * sizeof *REGISTRY);
      REGISTRY_COUNT--;
      break;
    }
  }
  for (size_t i = 0; i < base->kill_rule_count; i++) free(base->kill_rules[i].id);
  free(base->kill_rules);
  qa_q1_creatures_destroy(base->creatures);
  qa_q1_level_rules_destroy(base->level_rules);
  qa_q1_spawn_selector_destroy(base->spawn_selector);
  free(base->map_spawn);
  free(base);
}
static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
void qa_q1_base_register_kill_count_rule(qa_q1_base *base, const char *id,
                                         qa_q1_kill_count_rule_fn rule, void *ctx) {
  for (size_t i = 0; i < base->kill_rule_count; i++)
    if (strcmp(base->kill_rules[i].id, id) == 0) qa_range_error("Duplicate Q1 kill-count rule %s", id);
  if (base->kill_rule_count == base->kill_rule_capacity) {
    size_t capacity = base->kill_rule_capacity == 0 ? 4 : base->kill_rule_capacity * 2;
    kill_rule *grown = realloc(base->kill_rules, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    base->kill_rules = grown;
    base->kill_rule_capacity = capacity;
  }
  base->kill_rules[base->kill_rule_count].id = dup_text(id);
  base->kill_rules[base->kill_rule_count].fn = rule;
  base->kill_rules[base->kill_rule_count].ctx = ctx;
  base->kill_rule_count++;
}
bool qa_q1_base_count_monster_kill(qa_q1_base *base, qa_q1_base_monster *monster) {
  for (size_t i = 0; i < base->kill_rule_count; i++)
    if (!base->kill_rules[i].fn(base->kill_rules[i].ctx, monster)) return false;
  return true;
}
const char *qa_q1_base_next_hell_knight_melee(qa_q1_base *base) {
  return qa_q1_creatures_next_hell_knight_melee(base->creatures);
}
void qa_q1_base_spawn_lightning(qa_q1_base *base, qa_q1_actor *entity) {
  entity->use = qa_q1_callback_registry_use(base->game->named, entity, "base:lightning_use");
}

static qa_save_value *saved_actor(qa_actor_id actor) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_owned(object, "slot", qa_save_value_number((double)actor.slot));
  ok = ok && set_owned(object, "generation", qa_save_value_number((double)actor.generation));
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
static bool base_capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1_base *base = ctx;
  *out = NULL;
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return false;
  bool ok = set_owned(object, "version", qa_save_value_number(1));
  ok = ok && set_owned(object, "flags", qa_save_value_number(base->campaign.read_flags(base->campaign.ctx)));
  ok = ok && set_owned(object, "lightningEnd", qa_save_value_number(base->lightning_end));
  ok = ok && set_owned(object, "finaleStarted", qa_save_value_boolean(base->finale_started));
  ok = ok && set_owned(object, "finaleDismissed", qa_save_value_boolean(base->finale_dismissed));
  ok = ok && set_owned(object, "spawn", qa_q1_spawn_selector_capture(base->spawn_selector));
  ok = ok && set_owned(object, "rules", qa_q1_level_rules_capture(base->level_rules));
  if (!base->has_electrodes) {
    ok = ok && set_owned(object, "electrodes", qa_save_value_null());
  } else {
    qa_save_value *array = qa_save_value_array();
    if (array == NULL) ok = false;
    for (int i = 0; ok && i < 2; i++) {
      qa_save_value *entry = saved_actor(base->electrodes[i]->actor.id);
      if (entry == NULL || !qa_save_value_push(array, entry)) { qa_save_value_free(entry); ok = false; }
    }
    ok = ok && set_owned(object, "electrodes", array);
  }
  ok = ok && qa_q1_creatures_capture_fields(base->creatures, object);
  if (!ok) { qa_save_value_free(object); return false; }
  *out = qa_encode_checkpoint_value(object, out_length);
  qa_save_value_free(object);
  return *out != NULL;
}
static void must_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static bool field_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static qa_owned_actor restore_actor(qa_q1_base *base, const qa_save_reader *reader) {
  qa_save_reader slot, generation;
  must_field(reader, "slot", &slot);
  int64_t slot_value = (int64_t)qa_save_reader_integer_min(&slot, 0);
  qa_save_reader_destroy(&slot);
  must_field(reader, "generation", &generation);
  int64_t generation_value = (int64_t)qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&generation);
  qa_owned_actor actor;
  if (!q1base_resolve_saved(base->game, slot_value, generation_value, &actor))
    qa_save_reader_fail(reader, "missing saved actor");
  return actor;
}
static void free_decoded(void *value) { qa_save_value_free(value); }
static void base_restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1_base *base = ctx;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  /* A malformed save raises from the readers below; the decoded tree is the
   * only allocation they borrow from. */
  qa_cleanup decoded_cleanup;
  qa_cleanup_push(&decoded_cleanup, free_decoded, decoded);
  qa_save_reader root, field;
  if (!qa_save_reader_init(&root, decoded, "q1:base")) qa_range_error("out of memory");
  must_field(&root, "version", &field);
  (void)qa_save_reader_literal_number(&field, 1);
  qa_save_reader_destroy(&field);
  base->campaign.write_flags(base->campaign.ctx, field_number(&root, "flags"));
  base->lightning_end = field_number(&root, "lightningEnd");
  base->finale_started = field_boolean(&root, "finaleStarted");
  base->finale_dismissed = field_boolean(&root, "finaleDismissed");
  must_field(&root, "spawn", &field);
  qa_q1_spawn_selector_restore(base->spawn_selector, &field);
  qa_save_reader_destroy(&field);
  must_field(&root, "rules", &field);
  qa_q1_level_rules_restore(base->level_rules, &field);
  qa_save_reader_destroy(&field);
  must_field(&root, "electrodes", &field);
  if (qa_save_reader_is_null(&field)) {
    base->has_electrodes = false;
    base->electrodes[0] = base->electrodes[1] = NULL;
  } else {
    size_t count = qa_save_reader_list_count(&field);
    qa_q1_actor *entries[2] = {NULL, NULL};
    /* TS reader.list resolves every entry before checking the count. */
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&field, i, &item)) qa_range_error("out of memory");
      qa_owned_actor actor = restore_actor(base, &item);
      qa_q1_actor *resolved = qa_q1_get_entity(base->game, &actor.id);
      if (i < 2) entries[i] = resolved;
      qa_save_reader_destroy(&item);
    }
    if (count != 2 || entries[0] == NULL || entries[1] == NULL)
      qa_save_reader_fail(&field, "missing lightning electrodes");
    base->has_electrodes = true;
    base->electrodes[0] = entries[0];
    base->electrodes[1] = entries[1];
  }
  qa_save_reader_destroy(&field);
  qa_q1_creatures_restore_fields(base->creatures, &root);
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}

static void use_lightning(qa_q1_base *base, qa_q1_actor *entity, const qa_actor_id *activator) {
  qa_q1_services *game = base->game;
  if (base->lightning_end >= game->time + 1) return;
  qa_q1_actor *const *entities = game->entities;
  const size_t count = game->entity_count;
  qa_q1_actor *electrodes[2] = {NULL, NULL};
  size_t found = 0;
  for (size_t i = 0; i < count && found < 2; i++)
    if (strcmp(entities[i]->target, "lightning") == 0) electrodes[found++] = entities[i];
  /* chthon.qc dprints "missing lightning targets" and returns. */
  if (found < 2) return;
  qa_q1_actor *first = electrodes[0], *second = electrodes[1];
  if (strcmp(first->state, second->state) != 0 ||
      (strcmp(first->state, "top") != 0 && strcmp(first->state, "bottom") != 0)) return;
  qa_q1_cancel(game, first);
  qa_q1_cancel(game, second);
  base->has_electrodes = true;
  base->electrodes[0] = first;
  base->electrodes[1] = second;
  base->lightning_end = game->time + 1;
  qa_q1_sound_defaults(game, entity->actor.id, "misc/power.wav");
  fire_lightning(base, entity);
  /* TS [...monsters.values()].find(): every monster in insertion order, no limit. */
  qa_q1_base_monster *boss = NULL;
  for (size_t i = 0; boss == NULL; i++) {
    qa_q1_base_monster *monster;
    if (qa_q1_creatures_monsters_list_from(base->creatures, i, &monster, 1) == 0) break;
    if (strcmp(monster->spec->species, "boss") == 0) boss = monster;
  }
  if (boss == NULL) return;
  qa_q1_base_monster_set_enemy(boss, activator);
  if (strcmp(first->state, "top") == 0 && qa_q1_health(game, boss->entity->actor.id) > 0) {
    qa_q1_sound_defaults(game, boss->entity->actor.id, "boss1/pain.wav");
    double health = qa_q1_health(game, boss->entity->actor.id) - 1;
    q1base_set_health(game, boss->entity->actor, health);
    qa_q1_base_monster_play(boss, health >= 2 ? "boss_shocka1" : health == 1 ? "boss_shockb1" : "boss_shockc1");
  }
}
static void fire_lightning(qa_q1_base *base, qa_q1_actor *entity) {
  qa_q1_services *game = base->game;
  if (!base->has_electrodes) qa_range_error("Lightning has no electrodes");
  qa_q1_actor *first = base->electrodes[0], *second = base->electrodes[1];
  if (game->time >= base->lightning_end) {
    qa_q1_door_down(game, first);
    qa_q1_door_down(game, second);
    return;
  }
  qa_q1_body a = qa_q1_get_body(game, first), b = qa_q1_get_body(game, second);
  qa_vec3 p1 = qa_scale3(qa_add3(a.bounds.min, a.bounds.max), 0.5);
  p1.z = a.origin.z + a.bounds.min.z - 16;
  qa_vec3 p2 = qa_scale3(qa_add3(b.bounds.min, b.bounds.max), 0.5);
  p2.z = b.origin.z + b.bounds.min.z - 16;
  p2 = qa_sub3(p2, qa_scale3(qa_q1_normalize(qa_sub3(p2, p1)), 100));
  qa_q1_event beam = {0};
  beam.kind = "beam"; beam.beam_style = "lightning3";
  beam.has_actor = true;
  beam.actor = game->world != NULL ? game->world->actor.id : entity->actor.id;
  beam.start = p1; beam.end = p2;
  qa_q1_emit(game, beam);
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "base:lightning_fire"));
}
void qa_q1_base_finale(qa_q1_base *base, qa_q1_base_monster *monster) {
  if (base->finale_started) return;
  base->finale_started = true;
  qa_q1_services *game = base->game;
  qa_q1_base_reset_finale(base);
  qa_q1_base_monster_count_kill(monster);
  qa_q1_cancel(game, monster->entity);
  qa_q1_actor *const *entities = game->entities;
  const size_t count = game->entity_count;
  qa_q1_actor *position = NULL, *train = NULL;
  for (size_t i = 0; i < count; i++) {
    if (position == NULL && strcmp(entities[i]->classname, "info_intermission") == 0) position = entities[i];
    if (train == NULL && strcmp(entities[i]->classname, "misc_teleporttrain") == 0) train = entities[i];
  }
  if (position == NULL || train == NULL)
    qa_range_error("Q1 finale requires info_intermission and misc_teleporttrain");
  qa_q1_remove(game, train);
  game->has_intermission = true;
  q1base_set_text(&game->intermission_map, "start");
  game->has_intermission_cause = monster->state->has_enemy;
  game->intermission_cause = monster->state->enemy;
  game->intermission_exit_after = game->time + 10000000;
  qa_vec3 position_origin = qa_q1_get_body(game, position).origin;
  qa_vec3 mangle = qa_q1_actor_vector(position, "mangle");
  size_t player_count = 0;
  const qa_actor_id *players = q1base_players(game, &player_count);
  for (size_t i = 0; i < player_count; i++) {
    qa_owned_actor player;
    qa_q1_body body;
    if (!qa_session_actor_registry_resolve_owned(game->host.actors, players[i], &player)) continue;
    if (!q1base_read_body(game, players[i], &body)) continue;
    body.origin = position_origin;
    body.angles = mangle;
    body.velocity = ZERO3;
    q1base_write_body(game, player, body);
    qa_shared_body_table_link(game->host.bodies, player, NULL);
    q1base_set_can_take_damage(game, player, false);
  }
  qa_q1_event event = {0};
  event.kind = "intermission"; event.has_origin = true; event.origin = position_origin; event.angles = mangle;
  event.map = "start"; event.exit_after = game->time + 10000000; event.track = 0;
  qa_q1_emit(game, event);
  event = (qa_q1_event){0};
  event.kind = "finale"; event.text = ""; event.stage = 1;
  qa_q1_emit(game, event);
  if (strcmp(game->options.edition, "rerelease") == 0 && strcmp(game->map_name, "end") == 0) {
    qa_q1_event achievement = {0};
    achievement.kind = "achievement"; achievement.id = "ACH_DEFEAT_SHUB";
    qa_q1_emit(game, achievement);
    if (qa_q1_option_skill(game) == 3) {
      achievement.id = "ACH_DEFEAT_SHUB_NIGHTMARE";
      qa_q1_emit(game, achievement);
    }
  }
  qa_q1_actor *timer = qa_q1_create(game, "finale_timer", NULL, NULL);
  timer->has_owner = true;
  timer->owner = monster->entity->actor.id;
  qa_q1_schedule(game, timer, 1, qa_q1_callback_registry_action(game->named, timer, "base:finale_2"));
}
void qa_q1_base_finish_finale(qa_q1_base *base, qa_q1_base_monster *monster) {
  qa_q1_services *game = base->game;
  qa_vec3 origin = qa_q1_base_monster_origin(monster);
  qa_q1_sound_defaults(game, monster->entity->actor.id, "boss2/pop2.wav");
  for (double z = 16; z <= 144; z += 96)
    for (double x = -64; x <= 64; x += 32)
      for (double y = -64; y <= 64; y += 32) {
        double r = game->host.random(game->host.ctx);
        (void)qa_q1_throw_gib(game, qa_add3(origin, V3(x, y, z)),
                              r < 0.3 ? "gib1" : r < 0.6 ? "gib2" : "gib3", -999);
      }
  qa_q1_event event = {0};
  event.kind = "finale";
  event.text = qa_q1_finale_text(game->options.edition, "$qc_finale_end");
  event.stage = 4;
  qa_q1_emit(game, event);
  qa_q1_actor *victory = qa_q1_create(game, "finale_player", NULL, NULL);
  q1base_set_text(&victory->model, "progs/player.mdl");
  victory->frame = 1;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_sub3(origin, V3(32, 264, 0));
  patch.has_angles = true; patch.angles = V3(0, 290, 0);
  qa_q1_set_body(game, victory, &patch);
  qa_q1_link(game, victory);
  qa_q1_remove(game, monster->entity);
  event = (qa_q1_event){0};
  event.kind = "lightstyle"; event.style = 0; event.pattern = "m";
  qa_q1_emit(game, event);
  if (strcmp(game->options.edition, "classic") == 0) return;
  qa_q1_actor *timer = qa_q1_create(game, "finale_wait", NULL, NULL);
  qa_q1_schedule(game, timer, 1, qa_q1_callback_registry_action(game->named, timer, "base:finale_wait"));
}
void qa_q1_base_dismiss_finale(qa_q1_base *base) { base->finale_dismissed = true; }
void qa_q1_base_reset_finale(qa_q1_base *base) { base->finale_dismissed = false; }
bool qa_q1_base_has_finished_finale(qa_q1_base *base) {
  if (!base->finale_dismissed && base->options.finale_finished != NULL &&
      base->options.finale_finished(base->options.ctx))
    base->finale_dismissed = true;
  return base->finale_dismissed;
}
