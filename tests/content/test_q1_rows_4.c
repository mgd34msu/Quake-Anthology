/*
 * C checks for src/content/q1/addons/monsters rows (fork D): the source
 * controller registries (demodog, infected, heavy) and the oldone attack
 * schedule, over the real Q1 game (tests/content/q1_world_fixture.h,
 * q1_addon_fixture.h). Expected values come from running the TypeScript
 * under bun against a fake game on the same inputs (scratchpad
 * oracle_d_1..5.ts):
 *  - Map<OwnedActor, controller> keeps insertion position on re-set, and
 *    releasing the last entry leaves later entries reachable.
 *  - Q1Infected.die replaces its controller and keeps reading the old one;
 *    the displaced controller must stay valid (ASan run).
 *  - Q1Mg3Heavy.definitions is a Map: the latest register() of a classname
 *    wins on restore.
 *  - Q1Oldnew.attack uses JS `%` on the double "cnt" field.
 * The oracle numbered its actors 1, 2, 3; the real registry allocates its
 * own slots, so captures print each actor's oracle number.
 * Every host callback checks a sentinel context.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_addon_fixture.h"
#include "qa/content/q1/addons/monsters.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/value.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static q1_addon_fixture AF;
static int host_ctx_errors;
#define HOST_CTX(ctx) do { if ((ctx) != &AF.fx) host_ctx_errors++; } while (0)
static double randoms[8];
static size_t random_head, random_count;
static int damage_count;
static double damage_amount;
/* The oracle's actor numbering: labels[i] is the actor numbered i + 1. */
static qa_q1_actor *labels[8];
static size_t label_count;
static char health_log[512];
static qa_q1_actor *logged_entity;

static double t_random(void *ctx) {
  HOST_CTX(ctx);
  return random_head < random_count ? randoms[random_head++] : AF.fx.random_value;
}
static int label_of_slot(double slot) {
  for (size_t i = 0; i < label_count; i++)
    if (labels[i] != NULL && (double)labels[i]->actor.id.slot == slot) return (int)i + 1;
  return 0;
}
static int label_of(const qa_q1_actor *entity) { return label_of_slot((double)entity->actor.id.slot); }
static void log_health(const qa_q1_actor *entity) {
  const size_t used = strlen(health_log);
  char number[40];
  qa_net_js_number(qa_q1_health(AF.fx.game, entity->actor.id), number, sizeof number);
  (void)snprintf(health_log + used, sizeof health_log - used, "%s%d:%s", used ? "," : "", label_of(entity), number);
}
/* game.host.walkMove fails (the fixture's default), logging the health the
 * transformed infected has when its walk is tried (TS setHealth order). */
static bool t_walk_move(void *ctx, qa_owned_actor actor, double yaw, double distance) {
  (void)yaw; (void)distance;
  HOST_CTX(ctx);
  if (logged_entity != NULL && qa_same_owned_actor(actor, logged_entity->actor)) log_health(logged_entity);
  return false;
}
static bool t_damage_allowed(void *ctx, qa_damage_request request) {
  if (ctx != &AF) host_ctx_errors++;
  damage_count++;
  damage_amount = request.amount;
  return true;
}
static void open_world(qa_q1ad_program program) {
  q1af_open(&AF, program);
  qa_q1_services *game = AF.fx.game;
  game->host.random = t_random;
  game->host.walk_move = t_walk_move;
  AF.fx.damage_allowed = t_damage_allowed;
  AF.fx.damage_allowed_ctx = &AF;
  game->world = q1af_create(&AF, "worldspawn");
  random_head = random_count = 0;
  damage_count = 0;
  damage_amount = 0;
  label_count = 0;
  health_log[0] = '\0';
  logged_entity = NULL;
}
static void close_world(void) {
  CHECK(AF.fx.ctx_errors == 0);
  CHECK(q1af_ctx_errors == 0);
  CHECK(host_ctx_errors == 0);
  qa_q1am_release_addon_monsters(AF.fx.game);
  q1af_close(&AF);
}
static qa_q1_actor *labelled(const char *classname) {
  qa_q1_actor *entity = q1af_create(&AF, classname);
  labels[label_count++] = entity;
  return entity;
}
static const qa_q1_state_extension *extension_find(const char *id) {
  qa_q1_services *game = AF.fx.game;
  for (size_t i = 0; i < game->state_extension_count; i++)
    if (strcmp(game->state_extensions[i]->id, id) == 0) return game->state_extensions[i];
  return NULL;
}
static qa_save_value *state_value(const char *frame, double in_pain, double counter, bool lefty) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "currentFrame", qa_save_value_string(frame));
  qa_save_value_set(o, "nextFrame", qa_save_value_string(frame));
  qa_save_value_set(o, "inPain", qa_save_value_number(in_pain));
  qa_save_value_set(o, "counter", qa_save_value_number(counter));
  qa_save_value_set(o, "idleUntil", qa_save_value_number(0));
  qa_save_value_set(o, "lefty", qa_save_value_boolean(lefty));
  qa_save_value_set(o, "sliding", qa_save_value_boolean(false));
  qa_save_value_set(o, "countedDeath", qa_save_value_boolean(false));
  return o;
}
static qa_save_value *slot_entry(const qa_q1_actor *entity, qa_save_value *state) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "slot", qa_save_value_number((double)entity->actor.id.slot));
  qa_save_value_set(o, "generation", qa_save_value_number((double)entity->actor.id.generation));
  qa_save_value_set(o, "state", state);
  return o;
}
static void restore_list(const qa_q1_state_extension *ext, qa_save_value *list) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(list, &length);
  qa_save_value_free(list);
  ext->restore(ext->ctx, bytes, length);
  free(bytes);
}
/* "label:currentFrame:inPain" joined by commas, like the oracle prints. */
static void capture_order(const qa_q1_state_extension *ext, char *out, size_t cap) {
  size_t length = 0;
  uint8_t *bytes = NULL;
  CHECK(ext->capture(ext->ctx, &bytes, &length));
  qa_save_value *list = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  out[0] = '\0';
  for (size_t i = 0; list && i < list->item_count; i++) {
    const qa_save_value *item = list->items[i], *state = qa_save_value_get(item, "state");
    size_t used = strlen(out);
    (void)snprintf(out + used, cap - used, "%s%d:%s:%g", i ? "," : "", label_of_slot(qa_save_value_get(item, "slot")->number),
                   qa_save_value_get(state, "currentFrame")->text, qa_save_value_get(state, "inPain")->number);
  }
  qa_save_value_free(list);
}
static size_t count_events(const char *kind) {
  size_t n = 0;
  for (size_t i = 0; i < AF.fx.event_count; i++)
    if (strcmp(AF.fx.events[i].kind, kind) == 0) n++;
  return n;
}

/* oracle_d_1.ts */
static void test_demodog_registry(void) {
  open_world(QA_Q1AD_MG3);
  (void)qa_q1am_register_mg3_demodog(AF.context);
  const qa_q1_state_extension *ext = extension_find("mg3:demodog");
  CHECK(ext != NULL);
  if (ext == NULL) { close_world(); return; }
  qa_q1_actor *a = labelled("monster_dog"), *b = labelled("monster_dog"), *c = labelled("monster_dog");
  qa_save_value *list = qa_save_value_array();
  qa_save_value_push(list, slot_entry(a, state_value("demodog_stand1", 1, 0, false)));
  qa_save_value_push(list, slot_entry(b, state_value("demodog_run1", 2, 0, false)));
  restore_list(ext, list);
  char order[256];
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:demodog_stand1:1,2:demodog_run1:2") == 0);
  qa_q1_remove(AF.fx.game, b); /* the tail */
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:demodog_stand1:1") == 0);
  ext->clone(ext->ctx, a, c);
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:demodog_stand1:1,3:demodog_stand1:1") == 0);
  ext->clone(ext->ctx, c, b); /* clone keys the released actor's OwnedActor, as TS Map.set does */
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:demodog_stand1:1,3:demodog_stand1:1,2:demodog_stand1:1") == 0);
  /* A released actor no longer resolves from a save: the oracle's actor 2
   * comes back as a new live actor for the restore. */
  b = q1af_create(&AF, "monster_dog");
  labels[1] = b;
  list = qa_save_value_array();
  qa_save_value_push(list, slot_entry(a, state_value("demodog_stand1", 1, 0, false)));
  qa_save_value_push(list, slot_entry(b, state_value("demodog_run1", 2, 0, false)));
  qa_save_value_push(list, slot_entry(c, state_value("demodog_walk1", 3, 0, false)));
  restore_list(ext, list);
  ext->clone(ext->ctx, c, a); /* Map.set on the head key keeps its position */
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:demodog_walk1:3,2:demodog_run1:2,3:demodog_walk1:3") == 0);
  close_world();
}

/* oracle_d_3.ts */
static void test_infected_die_replaces_controller(void) {
  open_world(QA_Q1AD_MG3);
  qa_q1_services *game = AF.fx.game;
  (void)qa_q1am_register_mg3_infected(AF.context);
  const qa_q1_state_extension *ext = extension_find("mg3:infected");
  CHECK(ext != NULL);
  if (ext == NULL) { close_world(); return; }
  qa_q1_actor *a = labelled("monster_army"), *b = labelled("monster_army");
  (void)qa_q1_actor_field_set(a, "infected.kind", "army");
  (void)qa_q1_actor_field_set(b, "infected.kind", "army");
  qa_save_value *list = qa_save_value_array();
  qa_save_value_push(list, slot_entry(a, state_value("army_stand1", 1, 4, true)));
  qa_save_value_push(list, slot_entry(b, state_value("army_run1", 2, 4, true)));
  restore_list(ext, list);
  char order[256];
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:army_stand1:1,2:army_run1:2") == 0);
  /* registerMonsterCallbacks' monster_die: monster(entity).die(attacker). */
  const double killed_before = game->killed_monsters;
  const qa_q1_bound_die die = qa_q1_callback_registry_die(game->named, a, "mg3:infected:monster_die");
  CHECK(die.fn != NULL); /* registerMonsterCallbacks installed the infected lookup */
  logged_entity = a;
  qa_q1_call_die(game, a, die, NULL);
  log_health(a);
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:army_stand1:1,2:army_run1:2") == 0);
  CHECK(strcmp(a->classname, "monster_zombie") == 0);
  CHECK(strcmp(qa_q1_actor_text(a, "noise"), "zombie/z_idle.wav") == 0);
  CHECK(a->spawnflags == 128);
  CHECK(a->max_health == 60);
  CHECK(qa_q1_actor_number(a, "infected.transformed") == 1);
  CHECK(strcmp(a->monster->species, "zombie") == 0);
  CHECK(game->killed_monsters - killed_before == 2);
  CHECK(strcmp(health_log, "1:60,1:-100") == 0);
  CHECK(count_events("monster-killed") == 2);
  /* capture of the transformed controller carries the copied fields. */
  size_t length = 0;
  uint8_t *bytes = NULL;
  CHECK(ext->capture(ext->ctx, &bytes, &length));
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  const qa_save_value *state = qa_save_value_get(decoded->items[0], "state");
  CHECK(qa_save_value_get(state, "countedDeath")->boolean);
  CHECK(qa_save_value_get(state, "counter")->number == 4);
  CHECK(qa_save_value_get(state, "lefty")->boolean);
  qa_save_value_free(decoded);
  close_world();
}

/* oracle_d_2.ts: heavy definitions */
static const char *heavy_died;
static void die_first(qa_q1am_heavy_monster *m) { (void)m; heavy_died = "first"; }
static void die_second(qa_q1am_heavy_monster *m) { (void)m; heavy_died = "second"; }
static void heavy_spawn_noop(qa_q1am_heavy_monster *m) { (void)m; }
static void heavy_pain_noop(qa_q1am_heavy_monster *m, const qa_actor_id *a, double d) { (void)m; (void)a; (void)d; }
static const char *const x_classnames[] = {"monster_x"};
static const qa_q1_monster_frame_table empty_frames = {NULL, 0};
static qa_q1am_heavy_definition first_definition, second_definition;
static qa_q1am_mg3_heavy *heavy_runtime;
static int adjoin_sentinel;
/* TS game.adjoin(() => runtime.register(second)): the owned spawn is kept. */
static void register_second(void *ctx, qa_q1_services *game) {
  (void)game;
  if (ctx != &adjoin_sentinel) host_ctx_errors++;
  qa_q1am_mg3_heavy_register(heavy_runtime, &second_definition);
}
static void test_heavy_latest_definition_wins(void) {
  open_world(QA_Q1AD_MG3);
  qa_q1_base_monster_species spec;
  memset(&spec, 0, sizeof spec);
  spec.species = "ogre"; spec.model = "first"; spec.stand = spec.walk = spec.run = "ogre_stand1"; spec.sight = "";
  spec.movement = "walk"; spec.classnames = x_classnames; spec.classname_count = 1; spec.health = 10; spec.gib_health = -10;
  memset(&first_definition, 0, sizeof first_definition);
  first_definition.spec = spec; first_definition.frames = &empty_frames; first_definition.spawn = heavy_spawn_noop;
  first_definition.pain = heavy_pain_noop; first_definition.die = die_first;
  second_definition = first_definition;
  second_definition.spec.model = "second"; second_definition.die = die_second;
  heavy_runtime = qa_q1am_mg3_heavy_create(AF.context);
  CHECK(heavy_runtime != NULL);
  qa_q1am_mg3_heavy_register(heavy_runtime, &first_definition);
  qa_q1_adjoin(AF.fx.game, register_second, &adjoin_sentinel);
  qa_q1_actor *e = labelled("monster_x");
  qa_save_value *root = qa_save_value_object(), *monsters = qa_save_value_array(), *entry = qa_save_value_object(), *actor = qa_save_value_object();
  qa_save_value_set(root, "runeKnightMeleeCycle", qa_save_value_number(2));
  qa_save_value_set(actor, "slot", qa_save_value_number((double)e->actor.id.slot));
  qa_save_value_set(actor, "generation", qa_save_value_number((double)e->actor.id.generation));
  qa_save_value_set(entry, "actor", actor);
  qa_save_value_set(entry, "definition", qa_save_value_string("monster_x"));
  qa_save_value_set(entry, "state", state_value("ogre_stand1", 0, 0, false));
  qa_save_value_push(monsters, entry);
  qa_save_value_set(root, "monsters", monsters);
  const qa_q1_state_extension *ext = extension_find("mg3:heavy");
  CHECK(ext != NULL);
  if (ext == NULL) { close_world(); return; }
  restore_list(ext, root);
  const qa_q1_bound_action source_die = qa_q1_callback_registry_action(AF.fx.game->named, e, "mg3:heavy:source_die");
  CHECK(source_die.fn != NULL);
  heavy_died = NULL;
  qa_q1_call_action(AF.fx.game, e, source_die);
  CHECK(heavy_died != NULL && strcmp(heavy_died, "second") == 0);
  CHECK(heavy_runtime->rune_knight_melee_cycle == 2);
  close_world(); /* releases the runtime with the game */
}

/* oracle_d_2.ts: oldone attack with a fractional cnt */
static void test_oldnew_attack_fractional_cnt(void) {
  open_world(QA_Q1AD_MG3);
  qa_q1_services *game = AF.fx.game;
  qa_q1am_register_oldnew_projectiles(AF.context);
  qa_q1_actor *e = labelled("monster_oldone_new");
  (void)qa_q1_actor_field_set(e, "boss_phase", "4");
  (void)qa_q1_actor_field_set(e, "cnt", "3.5");
  e->count = 0;
  qa_q1am_oldnew *boss = calloc(1, sizeof *boss);
  CHECK(qa_q1am_oldnew_init(boss, AF.context, e));
  const size_t created_before = game->entity_count;
  qa_q1am_oldnew_attack(boss);
  CHECK(e->count == 0);
  CHECK(qa_q1_actor_number(e, "cnt") == 4.5);
  CHECK(game->entity_count - created_before == 1);
  bool found = false;
  for (size_t i = created_before; i < game->entity_count; i++) {
    qa_q1_actor *created = game->entities[i];
    found = true;
    CHECK(created->classname[0] == '\0');
    CHECK(qa_q1_actor_number(created, "cnt") == 2);
    CHECK(created->wait == 22.5);
    CHECK(created->delay == 0.8);
  }
  CHECK(found);
  free(boss);
  close_world();
}

/* oracle_d_4.ts: T_RockTouch2 draws the up jitter before the right jitter. */
static void test_final_rock_touch_random_order(void) {
  open_world(QA_Q1AD_MG3);
  qa_q1_services *game = AF.fx.game;
  qa_q1am_register_final_boss(AF.context);
  game->basis.forward = (qa_vec3){1, 0, 0};
  game->basis.right = (qa_vec3){0, 1, 0};
  game->basis.up = (qa_vec3){0, 0, 1};
  randoms[0] = 0.9; randoms[1] = 0.1; random_count = 2;
  qa_q1_actor *rock = labelled("rock");
  qa_q1_actor *dog = labelled("monster_dog");
  qa_q1_actor_set_damageable(dog, true);
  const qa_q1_bound_touch touch = qa_q1_callback_registry_touch(game->named, rock, "mg3:final:T_RockTouch2");
  CHECK(touch.fn != NULL);
  qa_q1_call_touch(game, rock, touch, dog->actor.id, NULL, NULL);
  const qa_q1_event *particles = q1af_first_event(&AF, "particles");
  CHECK(particles != NULL);
  if (particles != NULL) {
    CHECK(particles->direction.x == 0);
    CHECK(particles->direction.y == -2.8284270763397217);
    CHECK(particles->direction.z == 2.8284270763397217);
    CHECK(particles->origin.y == -0.28284272551536560);
    CHECK(particles->origin.z == 0.28284272551536560);
  }
  CHECK(damage_amount == 18);
  close_world();
}

/* oracle_d_5.ts: the ordinary addon registry, same Map semantics. */
static void test_ordinary_registry(void) {
  open_world(QA_Q1AD_MG1);
  qa_q1am_register_ordinary_addon_monsters(AF.context);
  const qa_q1_state_extension *ext = extension_find("mg1:ordinary");
  CHECK(ext != NULL);
  if (ext == NULL) { close_world(); return; }
  qa_q1_actor *a = labelled("monster_ogre"), *b = labelled("monster_ogre"), *c = labelled("monster_ogre");
  qa_save_value *list = qa_save_value_array();
  qa_save_value_push(list, slot_entry(a, state_value("ogre_stand1", 1, 0, false)));
  qa_save_value_push(list, slot_entry(b, state_value("ogre_walk1", 2, 0, false)));
  restore_list(ext, list);
  qa_q1_remove(AF.fx.game, b);
  ext->clone(ext->ctx, a, c);
  char order[256];
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:ogre_stand1:1,3:ogre_stand1:1") == 0);
  ext->clone(ext->ctx, c, a);
  capture_order(ext, order, sizeof order);
  CHECK(strcmp(order, "1:ogre_stand1:1,3:ogre_stand1:1") == 0);
  close_world();
}

int main(void) {
  test_demodog_registry();
  test_infected_die_replaces_controller();
  test_heavy_latest_definition_wins();
  test_oldnew_attack_fractional_cnt();
  test_final_rock_touch_random_order();
  test_ordinary_registry();
  printf("test_q1_rows_4: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
