/*
 * C checks for the TS drift packet D3 (src/content/q1 since 53ad56d), ported from
 * the TS tests added with it:
 *   tests/gameplay/q1-addons/coop-spawns.test.ts           (both tests)
 *   tests/gameplay/q1-addons/fog.test.ts                   (both tests)
 *   tests/gameplay/q1-addons/items/start-flags.test.ts     (MG1 start flags, MG trigger_teleport, MG3 sigils)
 *   tests/gameplay/q1-addons/monsters/ordinary/ordinary.test.ts
 *                                                          (army/dog InitMonster, MG1 killable Chthon)
 * plus the sourceWorldWords / pathGoal / restoreAbsent behaviour of
 * addons/monsters/ai/{controller,targets}.ts and foundation/entity-services.ts.
 * Expected values are the TS assertions of those files; `bun test` ran them green
 * on 2026-11-03 (19 pass, 121 expect() calls, TS 84aeef8d). The world() harness
 * (tests/gameplay/q1-addons/monsters/ordinary/harness.ts, random fixed at 0.4) is
 * reproduced by q1_addon_fixture.h with the combat policy carrying the game's
 * damage source effects. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_addon_fixture.h"
#include "qa/content/q1/addons/coop_spawns.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"
#include "qa/content/q1/addons/monsters/ordinary/index.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/persistence/value.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

/* --- TS world(program, withHorde=false, walkable=false, coop, skill=1) --- */
typedef struct world {
  q1_addon_fixture af;
  qa_owned_actor player;
} world;

static void open_world(world *w, qa_q1ad_program program, bool coop) {
  memset(w, 0, sizeof *w);
  qa_q1_options options = q1_world_fixture_options();
  options.campaign = program == QA_Q1AD_MG1 ? "q1:mg1" : "q1:mg3";
  options.coop = coop;
  q1_wf_with_source_effects = true;
  q1af_open_world(&w->af, &options);
  q1_wf_with_source_effects = false;
  w->af.context = qa_q1ad_register_q1_campaign_addons(w->af.base, program, q1af_services());
  if (w->af.context == NULL) abort();
  w->player = q1_world_fixture_admit_player(&w->af.fx, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(w->af.fx.game, w->player, "shotgun", true, NULL);
}
static void close_world(world *w) { q1af_close(&w->af); }

/* TS spawn(classname, fields). */
static qa_q1_actor *spawn(world *w, const char *classname, const char *const *pairs, size_t pair_count) {
  return q1af_spawn_fields(&w->af, classname, pairs, pair_count);
}
#define SPAWN(w, classname, ...)                                                                        \
  spawn((w), (classname), (const char *const[]){__VA_ARGS__},                                           \
        sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *) / 2)

/* TS think(entity, time): drop the pending think, then run the entity's think at `time`. */
static void think(world *w, qa_q1_actor *entity, double time) {
  q1_world_fixture *fx = &w->af.fx;
  for (size_t i = 0; i < fx->think_count;)
    if (qa_same_owned_actor(fx->thinks[i].actor, entity->actor)) {
      memmove(&fx->thinks[i], &fx->thinks[i + 1], (fx->think_count - i - 1) * sizeof *fx->thinks);
      fx->think_count--;
    } else i++;
  qa_frame_context frame;
  memset(&frame, 0, sizeof frame);
  frame.time.kind = QA_SOURCE_TIME_SECONDS;
  frame.time.value = time;
  frame.elapsed.kind = QA_SOURCE_TIME_SECONDS;
  frame.elapsed.value = 0.1;
  (void)qa_actor_callback_table_think(fx->callbacks, entity->actor, frame);
}
/* TS touch(entity): callbacks.touch({ self: entity, other: player }). */
static void touch(world *w, qa_q1_actor *entity) {
  qa_touch_contact contact;
  memset(&contact, 0, sizeof contact);
  contact.self = entity->actor;
  contact.other = w->player.id;
  (void)qa_actor_callback_table_touch(w->af.fx.callbacks, contact);
}
/* TS game.named.use(entity, name)(player, player). */
static void named_use(world *w, qa_q1_actor *entity, const char *name) {
  qa_q1_services *game = w->af.fx.game;
  qa_q1_call_use(game, entity, qa_q1_callback_registry_use(game->named, entity, name), &w->player.id, &w->player.id);
}
static bool live(world *w, qa_q1_actor *entity) { return qa_q1_live(w->af.fx.game, entity); }
static double inventory(world *w, qa_item_id item) {
  return qa_shared_inventory_table_count(w->af.fx.inventory, w->player.id, item);
}
static bool callback_bound(const char *name) { return name != NULL; }

/* --- coop-spawns.test.ts --- */
static void coop_checkpoints(void) {
  const qa_q1ad_program programs[2] = {QA_Q1AD_MG1, QA_Q1AD_MG3};
  for (size_t p = 0; p < 2; p++) {
    world w;
    open_world(&w, programs[p], true);
    qa_q1_services *game = w.af.fx.game;
    (void)SPAWN(&w, "info_player_start", "origin", "0 0 0");
    qa_q1_actor *open = SPAWN(&w, "info_player_coop", "origin", "64 0 0");
    qa_q1_actor *first = SPAWN(&w, "info_player_coop", "origin", "128 0 0", "targetname", "cp1");
    qa_q1_actor *second = SPAWN(&w, "info_player_coop", "origin", "192 0 0", "targetname", "cp2");
    qa_q1_actor *flagged = SPAWN(&w, "info_player_coop", "origin", "256 0 0", "targetname", "cp3", "spawnflags", "1");
    CHECK(open->activated && !first->activated && !second->activated && flagged->activated);
    qa_q1_spawn_selector *selector = qa_q1_base_spawn_selector(w.af.base);
    bool seen64 = false, seen256 = false, other = false;
    for (int i = 0; i < 6; i++) {
      qa_q1_actor *point = qa_q1_spawn_selector_select(selector, false);
      if (point == NULL || strcmp(point->classname, "info_player_coop") != 0) continue;
      const double x = qa_q1_get_body(game, point).origin.x;
      if (x == 64) seen64 = true;
      else if (x == 256) seen256 = true;
      else other = true;
    }
    CHECK(seen64 && seen256 && !other); /* [...seen].sort() toEqual [64, 256] */
    qa_q1_actor *trigger = SPAWN(&w, "trigger_activate_coop_spawns", "target", "cp1");
    named_use(&w, trigger, "mg:coop_spawns_use");
    CHECK(!open->activated && first->activated && !second->activated && !flagged->activated);
    CHECK(live(&w, trigger));
    bool only_first = true, any = false;
    for (int i = 0; i < 6; i++) {
      qa_q1_actor *point = qa_q1_spawn_selector_select(selector, false);
      if (point == NULL || strcmp(point->classname, "info_player_coop") != 0) continue;
      any = true;
      if (point != first) only_first = false;
    }
    CHECK(any && only_first); /* [...after] toEqual [first] */
    close_world(&w);
  }
}
static void coop_respawn_weapons(void) {
  const qa_q1ad_program programs[2] = {QA_Q1AD_MG1, QA_Q1AD_MG3};
  for (size_t p = 0; p < 2; p++) {
    world w;
    open_world(&w, programs[p], true);
    qa_q1_services *game = w.af.fx.game;
    qa_shared_inventory_table *table = w.af.fx.inventory;
    const qa_item_id rocket = qa_q1_services_weapon_item(game, "rocketlauncher");
    const qa_item_id lightning = qa_q1_services_weapon_item(game, "lightning");
    const qa_item_id nailgun = qa_q1_services_weapon_item(game, "nailgun");
    (void)qa_shared_inventory_table_give(table, w.player, rocket, 1);
    (void)qa_shared_inventory_table_give(table, w.player, lightning, 1);
    qa_q1_actor *checkpoint = SPAWN(&w, "info_player_coop", "origin", "128 0 0", "targetname", "cp1");
    named_use(&w, SPAWN(&w, "trigger_activate_coop_spawns", "target", "cp1"), "mg:coop_spawns_use");
    const char *remembered = qa_q1_actor_text(checkpoint, "coop.weapons");
    /* remembered.split(" ") contains "lightning" and "rocketlauncher" */
    char copy[128];
    (void)snprintf(copy, sizeof copy, " %s ", remembered);
    CHECK(strstr(copy, " lightning ") != NULL);
    CHECK(strstr(copy, " rocketlauncher ") != NULL);
    (void)qa_shared_inventory_table_consume(table, w.player, rocket, 1);
    (void)qa_shared_inventory_table_consume(table, w.player, lightning, 1);
    (void)qa_shared_inventory_table_consume(table, w.player, "q1:ammo/rockets", inventory(&w, "q1:ammo/rockets"));
    (void)qa_shared_inventory_table_consume(table, w.player, "q1:ammo/cells", inventory(&w, "q1:ammo/cells"));
    (void)qa_shared_inventory_table_give(table, w.player, "q1:ammo/cells", 20);
    CHECK(inventory(&w, rocket) == 0);
    qa_q1ad_restore_coop_weapons(w.af.context, w.player.id, checkpoint);
    CHECK(inventory(&w, rocket) == 1);
    CHECK(inventory(&w, lightning) == 1);
    CHECK(inventory(&w, nailgun) == 0);
    CHECK(inventory(&w, "q1:ammo/rockets") == 4);
    CHECK(inventory(&w, "q1:ammo/cells") == 20);
    CHECK(inventory(&w, "q1:ammo/nails") == 0);
    close_world(&w);
  }
}

/* --- fog.test.ts --- */
static size_t fog_events(world *w, qa_q1ad_event *last) {
  (void)last;
  return (size_t)w->af.fog_events;
}
static void fog_teleport_and_intermission(void) {
  const qa_q1ad_program programs[2] = {QA_Q1AD_MG1, QA_Q1AD_MG3};
  for (size_t p = 0; p < 2; p++) {
    world w;
    open_world(&w, programs[p], false);
    qa_q1_services *game = w.af.fx.game;
    (void)SPAWN(&w, "worldspawn", "fog_info_entity", "fog_world");
    (void)SPAWN(&w, "info_fog", "targetname", "fog_world", "fog_density", "0.01", "fog_color", "0.1 0.2 0.3");
    (void)SPAWN(&w, "info_fog", "targetname", "fog_abyss", "fog_density", "0.2", "fog_color", "0.5 0.5 0.5");
    (void)SPAWN(&w, "info_teleport_destination", "targetname", "dest", "origin", "512 0 0", "fog_info_entity", "fog_abyss");
    qa_q1_actor *teleport = SPAWN(&w, "trigger_teleport", "target", "dest");
    qa_q1_services_set_time(game, 5);
    touch(&w, teleport);
    CHECK(fog_events(&w, NULL) == 1);
    CHECK(w.af.last_event.kind == QA_Q1AD_EVENT_FOG);
    CHECK(qa_same_actor(w.af.last_event.player, w.player.id));
    CHECK(w.af.last_event.duration == 0);
    CHECK(fabs(w.af.last_event.density - 0.2) < 5e-6); /* toBeCloseTo(0.2, 5) */
    (void)SPAWN(&w, "info_intermission", "origin", "0 0 64");
    qa_q1_begin_intermission(game, "start", NULL);
    CHECK(fog_events(&w, NULL) == 2);
    CHECK(w.af.last_event.kind == QA_Q1AD_EVENT_FOG);
    CHECK(fabs(w.af.last_event.density - 0.01) < 5e-6);
    close_world(&w);
  }
}
static void fog_not_in_coop(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, true);
  (void)spawn(&w, "worldspawn", NULL, 0);
  (void)SPAWN(&w, "info_fog", "targetname", "fog_abyss", "fog_density", "0.2");
  (void)SPAWN(&w, "info_teleport_destination", "targetname", "dest", "origin", "512 0 0", "fog_info_entity", "fog_abyss");
  qa_q1_services_set_time(w.af.fx.game, 5);
  touch(&w, SPAWN(&w, "trigger_teleport", "target", "dest"));
  CHECK(fog_events(&w, NULL) == 0);
  close_world(&w);
}

/* --- start-flags.test.ts --- */
static void mg1_start_flags(void) {
  world w;
  open_world(&w, QA_Q1AD_MG1, false);
  qa_q1_actor *coop_only = SPAWN(&w, "item_shells", "spawnflags", "32768", "origin", "0 0 0");
  CHECK(!live(&w, coop_only));
  qa_q1_actor *fixed = SPAWN(&w, "item_armor1", "spawnflags", "65536", "origin", "64 0 0");
  think(&w, fixed, 0.2);
  CHECK(live(&w, fixed));
  CHECK(strcmp(fixed->movement, "none") == 0);
  close_world(&w);
}
static void mg_trigger_teleport_flags(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, false);
  qa_q1_services *game = w.af.fx.game;
  (void)spawn(&w, "worldspawn", NULL, 0);
  (void)SPAWN(&w, "info_teleport_destination", "targetname", "dest", "origin", "512 0 0");
  qa_q1_actor *open = SPAWN(&w, "trigger_teleport", "target", "dest", "targetname", "tele", "spawnflags", "6");
  qa_q1_services_set_time(game, 5);
  touch(&w, open);
  const qa_body_state_result body = qa_shared_body_table_read(w.af.fx.bodies, w.player.id);
  CHECK(body.has_value && body.value.origin.x == 512);
  qa_q1_actor *dormant = SPAWN(&w, "trigger_teleport", "target", "dest", "targetname", "tele2", "spawnflags", "14");
  CHECK(!callback_bound(qa_q1_touch_name(dormant->touch)));
  named_use(&w, dormant, "mg3:teleport_activate");
  CHECK(callback_bound(qa_q1_touch_name(dormant->touch)));
  close_world(&w);
}
static void mg3_sigil_regenerate(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, false);
  qa_q1_actor *sigil = SPAWN(&w, "item_sigil", "spawnflags", "129", "targetname", "rune", "origin", "0 0 0");
  CHECK(callback_bound(qa_q1_use_name(sigil->use)));
  CHECK(sigil->original_model != NULL && strcmp(sigil->original_model, "progs/end1.mdl") == 0);
  close_world(&w);
}

/* --- ordinary.test.ts --- */
static void army_dog_init_monster(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, false);
  qa_q1_services *game = w.af.fx.game;
  qa_q1_actor *hidden = SPAWN(&w, "monster_army", "spawnflags", "4", "origin", "0 0 0");
  CHECK(strcmp(hidden->solid, "none") == 0);
  CHECK(strcmp(hidden->model, "") == 0);
  qa_q1_actor *army = SPAWN(&w, "monster_army", "origin", "64 0 0");
  qa_q1_actor *dog = SPAWN(&w, "monster_dog", "origin", "128 0 0");
  think(&w, army, 1);
  think(&w, dog, 1);
  CHECK(qa_q1_get_body(game, army).bounds.max.z == 32);
  CHECK(qa_q1_get_body(game, dog).bounds.max.z == 64);
  CHECK(strcmp(army->solid, "slidebox") == 0);
  CHECK(strcmp(dog->solid, "slidebox") == 0);
  close_world(&w);
}
static void mg1_killable_chthon(void) {
  world w;
  open_world(&w, QA_Q1AD_MG1, false);
  qa_q1_services *game = w.af.fx.game;
  (void)spawn(&w, "worldspawn", NULL, 0);
  qa_q1_actor *boss = SPAWN(&w, "monster_boss", "spawnflags", "2", "targetname", "chthon", "origin", "0 0 0");
  qa_q1_base_monster *controller = qa_q1am_ordinary_addon_monster(w.af.context, boss);
  CHECK(controller != NULL);
  if (controller == NULL) { close_world(&w); return; }
  CHECK(!qa_q1_actor_damageable(boss));
  named_use(&w, boss, "mg1:ordinary:boss_awake");
  CHECK(qa_q1_health(game, boss->actor.id) == 4000);
  CHECK(qa_q1_actor_damageable(boss));
  qa_q1_player_state *state = qa_q1_attach_player(game, w.player, "shotgun", true, NULL);
  const qa_actor_id player = w.player.id;
  state->weapon = "rocketlauncher";
  (void)qa_q1_damage(game, boss->actor.id, player, &player, 500, NULL, NULL, NULL, NULL);
  CHECK(qa_q1_health(game, boss->actor.id) == 4000);
  state->weapon = "lightning";
  (void)qa_q1_damage(game, boss->actor.id, player, &player, 500, NULL, NULL, NULL, NULL);
  CHECK(qa_q1_health(game, boss->actor.id) == 3500);
  CHECK(strcmp(controller->current_frame, "boss_shockz1") == 0);
  (void)qa_q1_damage(game, boss->actor.id, player, &player, 2200, NULL, NULL, NULL, NULL);
  CHECK(qa_q1_health(game, boss->actor.id) == 1300);
  CHECK(strcmp(controller->current_frame, "boss_shockb1") == 0);
  (void)qa_q1_damage(game, boss->actor.id, player, &player, 5000, NULL, NULL, NULL, NULL);
  CHECK(strcmp(live(&w, boss) ? controller->current_frame : "removed", "boss_death1") == 0);
  think(&w, boss, 1);
  close_world(&w);
}

/* --- ai/controller.ts word/sourceWord, targets.ts "mg3:world-words",
 * entity-services.ts pathGoal and Q1StateExtension.restoreAbsent ---
 * TS behaviour read from the code: with no Q1 world actor sourceWord writes
 * game.sourceWorldWords (Math.fround), word reads it back (0 when absent); the
 * extension captures [{name, value}] in insertion order, restore replaces the map
 * and restoreAbsent clears it; pathGoal("") is the first client. */
static const qa_q1_state_extension *extension_named(qa_q1_services *game, const char *id) {
  for (size_t i = 0; i < game->state_extension_count; i++)
    if (strcmp(game->state_extensions[i]->id, id) == 0) return game->state_extensions[i];
  return NULL;
}
static void world_words_and_path_goal(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, false);
  qa_q1_services *game = w.af.fx.game;
  CHECK(game->world == NULL);
  qa_q1_actor *army = SPAWN(&w, "monster_army", "origin", "64 0 0");
  qa_q1_base_monster *monster = qa_q1am_ordinary_addon_monster(w.af.context, army);
  CHECK(monster != NULL);
  if (monster != NULL) {
    CHECK(qa_q1am_mg3_word(monster, "enemy_range") == 0);
    qa_q1am_mg3_source_word(monster, "enemy_range", 2);
    qa_q1am_mg3_source_word(monster, "movedist", 0.1);
    CHECK(qa_q1am_mg3_word(monster, "enemy_range") == 2);
    CHECK(qa_q1am_mg3_word(monster, "movedist") == (double)(float)0.1);
  }
  const qa_q1_state_extension *extension = extension_named(game, "mg3:world-words");
  CHECK(extension != NULL);
  if (extension != NULL) {
    uint8_t *bytes = NULL;
    size_t length = 0;
    CHECK(extension->capture(extension->ctx, &bytes, &length));
    qa_q1_source_world_words_clear(game);
    double value = -1;
    CHECK(!qa_q1_source_world_word_get(game, "enemy_range", &value));
    extension->restore(extension->ctx, bytes, length);
    CHECK(qa_q1_source_world_word_get(game, "enemy_range", &value) && value == 2);
    CHECK(game->source_world_word_count == 2 && strcmp(game->source_world_words[0].name, "enemy_range") == 0);
    void (*absent)(void *ctx) = qa_q1_state_extension_restore_absent(game, extension);
    CHECK(absent != NULL);
    if (absent != NULL) absent(extension->ctx);
    CHECK(game->source_world_word_count == 0);
    free(bytes);
  }
  qa_actor_id goal;
  CHECK(qa_q1_path_goal(game, "", &goal) && qa_same_actor(goal, w.player.id));
  CHECK(!qa_q1_path_goal(game, "nowhere", &goal));
  qa_q1_actor *corner = SPAWN(&w, "path_corner", "targetname", "p1", "origin", "0 0 0");
  CHECK(qa_q1_path_goal(game, "p1", &goal) && qa_same_actor(goal, corner->actor.id));
  close_world(&w);
}

int main(void) {
  coop_checkpoints();
  coop_respawn_weapons();
  fog_teleport_and_intermission();
  fog_not_in_coop();
  mg1_start_flags();
  mg_trigger_teleport_flags();
  mg3_sigil_regenerate();
  army_dog_init_monster();
  mg1_killable_chthon();
  world_words_and_path_goal();
  CHECK(q1af_ctx_errors == 0);
  printf("test_q1_d3_drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
