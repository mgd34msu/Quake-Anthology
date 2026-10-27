/*
 * Port of key cases from tests/gameplay/q2-base/entities/entities
 * .test.ts and player/player.test.ts (snap/clock/obituary/blend/
 * rules, character lifecycle on real shared tables, player
 * admission/userinfo/spawn/chat on a lean game-services fake, and
 * entity module surface). TypeScript toThrow cases run in a child
 * process and must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/base/entities.h"
#include "qa/content/q2/base/player.h"
#include "qa/content/q2/foundation.h"
#include "qa/world/actors.h"
#include "qa/world/gameplay.h"

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

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

/* ---------------- pure helpers (TS "Q2 entity helpers") ---------------- */

static void test_pure_helpers(void) {
  CHECK(near(qa_q2_snap_turret_eighth(1.04), 1));
  CHECK(near(qa_q2_snap_turret_eighth(1.09), 1.125));
  CHECK(near(qa_q2_snap_turret_eighth(-1.04), -1));
  CHECK(near(qa_q2_snap_turret_eighth(-1.09), -1.125));
  char text[64];
  qa_q2_clock_text(5, 0, text, sizeof text);
  CHECK(strcmp(text, " 5") == 0);
  qa_q2_clock_text(65, 1, text, sizeof text);
  CHECK(strcmp(text, " 1:05") == 0);
  qa_q2_clock_text(3661, 2, text, sizeof text);
  CHECK(strcmp(text, " 1:01:01") == 0);

  /* exports surface */
  CHECK(qa_q2_base_entity_classname_count == 38);
  CHECK(strcmp(qa_q2_base_entity_classnames[0], "func_plat") == 0);
  bool has_turret = false, has_clock = false;
  for (size_t i = 0; i < qa_q2_base_entity_classname_count; i++) {
    if (strcmp(qa_q2_base_entity_classnames[i], "turret_driver") == 0) has_turret = true;
    if (strcmp(qa_q2_base_entity_classnames[i], "func_clock") == 0) has_clock = true;
  }
  CHECK(has_turret && has_clock);
  CHECK(qa_q2_client_command_count == 28);
  CHECK(strcmp(qa_q2_client_commands[0].name, "say") == 0);
  CHECK(qa_q2_client_commands[0].documentation.summary != NULL);
  CHECK(qa_q2_character_sound_count == 39);
  CHECK(strcmp(qa_q2_character_sounds[0], "*death1.wav") == 0);
  CHECK(strcmp(qa_q2_character_sounds[38], "player/step4.wav") == 0);
  CHECK(qa_q2_character_model_count == 7);

  /* blend + animation ranges */
  qa_vec4 blend = qa_q2_add_blend((qa_vec4){0, 0, 0, 0}, (qa_vec3){1, 0, 0}, 0.5);
  CHECK(near(blend.x, 1) && near(blend.w, 0.5));
  qa_q2_base_frame_range range = qa_q2_pain_animation_frames(false, 2);
  CHECK(range.first == 61 && range.last == 65);
  range = qa_q2_death_animation_frames(false, 1);
  CHECK(range.first == 183 && range.last == 189);
  range = qa_q2_death_animation_frames(true, 0);
  CHECK(range.first == 172 && range.last == 177);

  /* rules defaults + partial override */
  qa_q2_base_player_rules rules = qa_q2_create_player_rules(NULL, 0);
  CHECK(rules.max_spectators == 4 && !rules.cheats && rules.flood_messages == 4);
  CHECK(near(rules.bob_up, 0.005) && strcmp(rules.password, "") == 0);
  qa_q2_base_player_rules changes = {0};
  changes.cheats = true;
  changes.frag_limit = 25;
  rules = qa_q2_create_player_rules(&changes, QA_Q2_RULE_CHEATS | QA_Q2_RULE_FRAG_LIMIT);
  CHECK(rules.cheats && rules.frag_limit == 25 && rules.time_limit_minutes == 0);

  /* state defaults */
  qa_q2_base_player_state state;
  qa_q2_base_player_state_init(&state, 2, 100);
  CHECK(state.slot == 2 && state.fov == 90 && strcmp(state.skin, "male/grunt") == 0);
  CHECK(state.connected && state.drown_damage == 2 && state.animation_end == 39);
  CHECK(strcmp(state.selected_item, "q2:weapon_blaster") == 0);
  qa_q2_base_player_state_destroy(&state);

  /* userinfo lookup */
  char scratch[64];
  CHECK(qa_q2_userinfo_get("\\name\\Q\\skin\\female/athena", "name", scratch, sizeof scratch) &&
        strcmp(scratch, "Q") == 0);
  CHECK(qa_q2_userinfo_get("\\name\\Q", "missing", scratch, sizeof scratch) == NULL);
  CHECK(qa_q2_userinfo_get("\\name\\First\\name\\Second", "name", scratch, sizeof scratch) &&
        strcmp(scratch, "First") == 0);
}

static void test_obituary(void) {
  qa_q2_base_player_state victim, attacker;
  qa_q2_base_player_state_init(&victim, 0, 0);
  qa_q2_base_player_state_init(&attacker, 1, 0);
  (void)snprintf(victim.name, sizeof victim.name, "Victim");
  (void)snprintf(attacker.name, sizeof attacker.name, "Killer");
  victim.gender = "female";
  char out[256];
  qa_q2_obituary(&victim, NULL, 19, true, false, NULL, NULL, out, sizeof out);
  CHECK(strcmp(out, "Victim does a back flip into the lava.\n") == 0);
  CHECK(victim.score == -1);
  qa_q2_obituary(&victim, &victim, 7, true, false, NULL, NULL, out, sizeof out);
  CHECK(strcmp(out, "Victim tripped on her own grenade.\n") == 0);
  CHECK(victim.score == -2);
  qa_q2_obituary(&victim, &attacker, 3, true, false, NULL, NULL, out, sizeof out);
  CHECK(strcmp(out, "Victim was blown away by Killer's super shotgun\n") == 0);
  CHECK(attacker.score == 1);
  qa_q2_obituary(&victim, &attacker, 0x8000000 | 8, true, false, NULL, NULL, out, sizeof out);
  CHECK(strcmp(out, "Victim ate Killer's rocket\n") == 0);
  CHECK(attacker.score == 0); /* friendly fire: -1 */
  qa_q2_obituary(&victim, &attacker, 99, true, false, NULL, NULL, out, sizeof out);
  CHECK(strcmp(out, "Victim died.\n") == 0);
  qa_q2_base_player_state_destroy(&victim);
  qa_q2_base_player_state_destroy(&attacker);
}

/* ---------------- lean game-services fake ---------------- */

typedef struct fake_game {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q2_source_callbacks *source_callbacks;
  qa_q2_foundation_host host;
  qa_q2_game_services game;
  qa_q2_counters counters;
  qa_owned_actor world;
  qa_q2_entity **entities; size_t entity_count, entity_cap;
  qa_q2_entity **target_list;
  double now;
  double randoms[64]; size_t random_count, random_at;
  int emits, diagnostics, gibs, respawns, deaths;
  qa_q2_base_player_view last_view;
  qa_attack_provenance attack_storage;
  qa_actor_id attack_attacker, attack_inflictor;
} fake_game;

static fake_game *FG;

static double fake_now(void *c) { (void)c; return FG->now; }
static double fake_frame(void *c) { (void)c; return 0.1; }
static double fake_random(void *c) {
  (void)c;
  if (FG->random_at < FG->random_count) return FG->randoms[FG->random_at++];
  return 0.5;
}
static void fake_emit(void *c, qa_q2_presentation_event e) { (void)c; (void)e; FG->emits++; }
static void fake_diagnostic(void *c, const char *m) { (void)c; (void)m; FG->diagnostics++; }
static double fake_contents(void *c, qa_vec3 p) { (void)c; (void)p; return 0; }
static bool fake_is_player(void *c, qa_actor_id a) { (void)c; (void)a; return false; }
static bool fake_is_monster(void *c, qa_actor_id a) { (void)c; (void)a; return false; }
static const qa_actor_id *fake_players_list(void *c, size_t *n) { (void)c; *n = 0; return NULL; }
static qa_trace_result fake_trace(void *c, qa_q2_trace_request r) {
  (void)c;
  qa_trace_result out = {0};
  out.fraction = 1;
  out.end = r.end;
  out.hit.kind = QA_TRACE_HIT_NONE;
  return out;
}
static qa_actor_id fake_world(void *c) { (void)c; return FG->world.id; }
static void fake_portal(void *c, double p, bool open) { (void)c; (void)p; (void)open; }
static void fake_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { (void)c; (void)m; (void)l; (void)f; }
static void fake_transition(void *c, qa_transition_intent t) { (void)c; (void)t; }
static qa_q2_player_view_result fake_player_view(void *c, qa_actor_id a) {
  (void)c; (void)a;
  qa_q2_player_view_result out = {0};
  out.has_value = true;
  return out;
}

static qa_body_state default_body(void) {
  qa_body_state state = {0};
  state.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  return state;
}
static qa_q2_entity *fake_add_entity(qa_owned_actor actor, const char *classname, const qa_q2_field_pair *values, size_t count) {
  qa_q2_entity *entity = calloc(1, sizeof *entity);
  qa_q2_spawn_fields fields = {0};
  fields.ordinal = -1;
  fields.classname = classname;
  qa_q2_spawn_set(&fields, "classname", classname);
  for (size_t i = 0; i < count; i++) qa_q2_spawn_set(&fields, values[i].key, values[i].value);
  qa_q2_entity_init(entity, actor, fields);
  if (!qa_shared_body_table_read(FG->bodies, actor.id).has_value)
    qa_shared_body_table_create_body(FG->bodies, actor, default_body());
  if (FG->entity_count == FG->entity_cap) {
    FG->entity_cap = FG->entity_cap ? FG->entity_cap * 2 : 8;
    FG->entities = realloc(FG->entities, FG->entity_cap * sizeof *FG->entities);
  }
  FG->entities[FG->entity_count++] = entity;
  return entity;
}
static qa_q2_entity *fake_create(void *c, const char *classname, const qa_q2_field_pair *values, size_t count) {
  (void)c;
  qa_owned_actor actor = qa_session_actor_registry_allocate(FG->actors, "q2:test", classname);
  return fake_add_entity(actor, classname, values, count);
}
static qa_q2_entity *fake_spawn(void *c, qa_q2_spawn_fields fields) {
  (void)c;
  qa_owned_actor actor = qa_session_actor_registry_allocate(FG->actors, "q2:test", fields.classname);
  size_t count = fields.value_count;
  const qa_q2_field_pair *values = fields.values;
  qa_q2_field_pair *copy = NULL;
  size_t copy_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(values[i].key, "classname") == 0) continue;
    copy = realloc(copy, (copy_count + 1) * sizeof *copy);
    copy[copy_count++] = values[i];
  }
  qa_q2_entity *entity = fake_add_entity(actor, fields.classname, copy, copy_count);
  free(copy);
  return entity;
}
static qa_q2_entity *const *fake_entities(void *c, size_t *n) {
  (void)c;
  *n = FG->entity_count;
  return FG->entities;
}
static void fake_remove(void *c, qa_q2_entity *entity) {
  (void)c;
  for (size_t i = 0; i < FG->entity_count; i++) {
    if (FG->entities[i] == entity) {
      FG->entities[i] = FG->entities[--FG->entity_count];
      qa_owned_actor actor = entity->actor;
      qa_q2_entity_destroy(entity);
      free(entity);
      qa_session_actor_registry_release(FG->actors, actor);
      return;
    }
  }
}
static qa_q2_entity *fake_entity_of(void *c, const qa_actor_id *actor) {
  (void)c;
  if (!actor) return NULL;
  for (size_t i = 0; i < FG->entity_count; i++)
    if (qa_same_actor(FG->entities[i]->actor.id, *actor)) return FG->entities[i];
  return NULL;
}
static qa_q2_entity *const *fake_targets(void *c, const char *name, size_t *n) {
  (void)c;
  size_t count = 0;
  free(FG->target_list);
  FG->target_list = NULL;
  for (size_t i = 0; i < FG->entity_count; i++) {
    if (strcmp(FG->entities[i]->targetname, name) == 0) {
      FG->target_list = realloc(FG->target_list, (count + 1) * sizeof *FG->target_list);
      FG->target_list[count++] = FG->entities[i];
    }
  }
  *n = count;
  return FG->target_list;
}
static qa_q2_entity *fake_pick_target(void *c, const char *name) {
  (void)c;
  for (size_t i = 0; i < FG->entity_count; i++)
    if (strcmp(FG->entities[i]->targetname, name) == 0) return FG->entities[i];
  return NULL;
}
static qa_body_state fake_body(void *c, qa_owned_actor actor) {
  (void)c;
  qa_body_state_result body = qa_shared_body_table_read(FG->bodies, actor.id);
  if (!body.has_value) { qa_body_state empty = {0}; return empty; }
  return body.value;
}
static void fake_move(void *c, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  (void)c; (void)link;
  qa_body_state next = fake_body(NULL, entity->actor);
  if (changes.has_origin) next.origin = changes.origin;
  if (changes.has_angles) next.angles = changes.angles;
  if (changes.has_velocity) next.velocity = changes.velocity;
  if (changes.has_bounds) next.bounds = changes.bounds;
  qa_actor_id ground = changes.ground;
  if (changes.has_ground) next.ground = changes.ground_null ? NULL : &ground;
  qa_shared_body_table_write(FG->bodies, entity->actor, next);
}
static void fake_link(void *c, qa_q2_entity *e) { (void)c; (void)e; }
static void fake_solid(void *c, qa_q2_entity *e, const char *s) { (void)c; e->solid = s; }
static void fake_motion(void *c, qa_q2_entity *e, const char *k) { (void)c; e->motion = k; }
static void fake_schedule(void *c, qa_q2_entity *e, double delay, qa_q2_think think) {
  (void)c;
  e->think = think;
  e->has_next_think = true;
  e->next_think = FG->now + delay;
}
static void fake_cancel(void *c, qa_q2_entity *e) { (void)c; e->think = (qa_q2_think){0}; e->has_next_think = false; }
static void fake_show(void *c, qa_q2_entity *e) { (void)c; (void)e; }
static void fake_sound(void *c, qa_owned_actor a, const char *p, double ch, double v, double at) { (void)c; (void)a; (void)p; (void)ch; (void)v; (void)at; }
static void fake_use_targets(void *c, qa_authored_target e, const qa_actor_id *a, bool d) { (void)c; (void)e; (void)a; (void)d; }
static qa_damage_outcome fake_damage(void *c, qa_actor_id t, qa_q2_entity *i, qa_actor_id ii, const qa_actor_id *a, double amt, double kb, qa_vec3 d, qa_vec3 p, qa_vec3 n, double m, double f, qa_item_id w) {
  (void)c; (void)t; (void)i; (void)ii; (void)a; (void)amt; (void)kb; (void)d; (void)p; (void)n; (void)m; (void)f; (void)w;
  qa_damage_outcome out = {0};
  return out;
}
static void fake_radius(void *c, qa_q2_entity *i, const qa_actor_id *a, double d, const qa_actor_id *ig, double r, double m, double f, qa_item_id w) {
  (void)c; (void)i; (void)a; (void)d; (void)ig; (void)r; (void)m; (void)f; (void)w;
}
static qa_attack_provenance fake_attack(void *c, qa_q2_entity *i, qa_actor_id ii, const qa_actor_id *a, double m, double f, qa_item_id w) {
  (void)c; (void)i;
  FG->attack_inflictor = ii;
  if (a) FG->attack_attacker = *a;
  qa_attack_provenance attack = {0};
  attack.attacker = a ? &FG->attack_attacker : NULL;
  attack.inflictor = &FG->attack_inflictor;
  attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  attack.cause.means_of_death = m;
  attack.cause.damage_flags = f;
  (void)w;
  FG->attack_storage = attack;
  return attack;
}

static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) {
  (void)c; (void)a;
  return (qa_bounds){qa_q2_add(b.origin, b.bounds.min), qa_q2_add(b.origin, b.bounds.max)};
}
static void link_noop(void *c, qa_linked_body b) { (void)c; (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { (void)c; (void)a; }

static void fake_setup(qa_q2_mode mode) {
  FG = calloc(1, sizeof *FG);
  FG->now = 10;
  qa_identity_owner_create(&FG->identities, "test");
  FG->actors = qa_session_actor_registry_create(&FG->identities, 65536);
  qa_body_link_hooks link_hooks = {0};
  link_hooks.absolute_bounds = link_bounds;
  link_hooks.on_link = link_noop;
  link_hooks.on_unlink = unlink_noop;
  FG->bodies = qa_shared_body_table_create(FG->actors, link_hooks);
  FG->callbacks = qa_actor_callback_table_create(FG->actors);
  qa_gameplay_hooks hooks = {0};
  FG->combat = qa_gameplay_authority_create(FG->actors, FG->callbacks, hooks);
  FG->inventory = qa_shared_inventory_table_create(FG->actors);
  FG->source_callbacks = qa_q2_source_callbacks_create();
  FG->world = qa_session_actor_registry_allocate(FG->actors, "q2:test", "world");
  qa_shared_body_table_create_body(FG->bodies, FG->world, default_body());
  FG->host = (qa_q2_foundation_host){0};
  FG->host.now = fake_now; FG->host.frame_seconds = fake_frame; FG->host.random = fake_random;
  FG->host.actors = FG->actors; FG->host.bodies = FG->bodies; FG->host.callbacks = FG->callbacks;
  FG->host.combat = FG->combat; FG->host.inventory = FG->inventory;
  FG->host.world_actor = fake_world; FG->host.trace = fake_trace;
  FG->host.point_contents = fake_contents; FG->host.emit = fake_emit;
  FG->host.prepare_level_change = fake_prepare; FG->host.transition = fake_transition;
  FG->host.set_area_portal = fake_portal; FG->host.is_player = fake_is_player;
  FG->host.is_monster = fake_is_monster; FG->host.players = fake_players_list;
  FG->host.player_view_state = fake_player_view; FG->host.diagnostic = fake_diagnostic;
  FG->game = (qa_q2_game_services){0};
  FG->game.host = &FG->host;
  FG->game.source_callbacks = FG->source_callbacks;
  FG->game.counters = &FG->counters;
  FG->game.options.mode = mode;
  FG->game.options.edition = QA_Q2_EDITION_CLASSIC;
  FG->game.options.max_clients = 4;
  FG->game.options.campaign = "q2";
  FG->game.options.map_name = "base1";
  FG->game.options.inventory_provider = "q2:items";
  FG->game.entities = fake_entities; FG->game.spawn = fake_spawn; FG->game.create = fake_create;
  FG->game.remove = fake_remove; FG->game.entity = fake_entity_of; FG->game.body = fake_body;
  FG->game.move = fake_move; FG->game.link = fake_link; FG->game.solid = fake_solid;
  FG->game.motion = fake_motion; FG->game.schedule = fake_schedule; FG->game.cancel = fake_cancel;
  FG->game.use_targets = fake_use_targets; FG->game.targets = fake_targets;
  FG->game.pick_target = fake_pick_target; FG->game.show = fake_show; FG->game.sound = fake_sound;
  FG->game.damage = fake_damage; FG->game.radius_damage = fake_radius; FG->game.attack = fake_attack;
}
static void fake_teardown(void) {
  qa_q2_source_callbacks_destroy(FG->source_callbacks);
  qa_shared_inventory_table_destroy(FG->inventory);
  qa_gameplay_authority_destroy(FG->combat);
  qa_actor_callback_table_destroy(FG->callbacks);
  qa_shared_body_table_destroy(FG->bodies);
  qa_session_actor_registry_close(FG->actors);
  qa_session_actor_registry_destroy(FG->actors);
  qa_identity_owner_destroy(&FG->identities);
  free(FG->entities);
  free(FG->target_list);
  free(FG);
  FG = NULL;
}

/* ---------------- character (TS "Q2 character adapter") ---------------- */

static qa_q2_player_movement char_movement(void *c, qa_actor_id a) {
  (void)c; (void)a;
  qa_q2_player_movement movement = {0};
  movement.standing_bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  movement.animate_q2 = true;
  return movement;
}
static qa_q2_player_powerups char_powerups(void *c, qa_actor_id a) { (void)c; (void)a; qa_q2_player_powerups p = {0}; return p; }
static bool char_weapon(void *c, qa_actor_id a, qa_q2_character_weapon *out) { (void)c; (void)a; (void)out; return false; }
static void char_view(void *c, qa_actor_id a, qa_q2_base_player_view view) { (void)c; (void)a; FG->last_view = view; }
static void char_noise(void *c, qa_actor_id a, qa_vec3 o) { (void)c; (void)a; (void)o; }
static void char_env(void *c, qa_owned_actor a, double amt, double m, double f) { (void)c; (void)a; (void)amt; (void)m; (void)f; }
static void char_died(void *c, qa_owned_actor a, qa_death_reaction r) { (void)c; (void)a; (void)r; FG->deaths++; }
static void char_respawn(void *c, qa_owned_actor a) { (void)c; (void)a; FG->respawns++; }
static void char_motion(void *c, qa_owned_actor a, const char *k, const char *s) { (void)c; (void)a; (void)k; (void)s; }
static void char_gib(void *c, qa_q2_character_gib g) { (void)c; (void)g; FG->gibs++; }

static void test_character(void) {
  fake_setup(QA_Q2_MODE_SINGLEPLAYER);
  qa_owned_actor actor = qa_session_actor_registry_allocate(FG->actors, "q2:test", "player");
  qa_shared_body_table_create_body(FG->bodies, actor, default_body());
  qa_combat_state combat = {0};
  combat.health = 100;
  combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  combat.armor.powered.kind = "none";
  combat.mass = 200; combat.can_take_damage = true;
  qa_gameplay_authority_create_state(FG->combat, actor, combat, NULL, NULL);
  qa_q2_character_host host = {0};
  host.bodies = FG->bodies; host.combat = FG->combat; host.inventory = FG->inventory;
  host.now = fake_now; host.random = fake_random; host.movement = char_movement;
  host.point_contents = fake_contents; host.powerups = char_powerups; host.weapon = char_weapon;
  host.emit = fake_emit; host.view = char_view; host.noise = char_noise;
  host.environment_damage = char_env; host.died = char_died; host.request_respawn = char_respawn;
  host.motion = char_motion; host.spawn_gib = char_gib;
  qa_q2_character_options options = {0};
  options.model = "players/male/tris.md2";
  options.mode = QA_Q2_MODE_SINGLEPLAYER;
  qa_q2_character_actor *character = qa_q2_character_actor_create(actor, host, options);
  CHECK(character != NULL);
  CHECK(character->entity.view_height == 22 && character->state.fov == 90);

  /* damage feedback: generic decision accumulates and feeds the view */
  qa_damage_decision decision = {0};
  decision.applied_damage = 12;
  decision.request.knockback = 3;
  decision.request.point = (qa_vec3){10, 0, 0};
  qa_q2_character_actor_record_damage(character, decision);
  CHECK(character->state.damage_blood == 12 && character->state.damage_knockback == 3);
  qa_q2_character_actor_end_frame(character, false);
  CHECK(character->state.damage_blood == 0 && character->state.damage_alpha >= 0);
  CHECK(FG->last_view.health == 100);
  CHECK(FG->emits > 0);

  /* gib death: four gibs, gibbed, host notified once */
  qa_gameplay_authority_set_health(FG->combat, actor, -50);
  qa_death_reaction reaction = {0};
  reaction.pain.damage = 80;
  reaction.pain.self = actor;
  qa_q2_character_actor_die(character, reaction);
  CHECK(FG->gibs == 4 && FG->deaths == 1);
  CHECK(character->state.dead && character->state.gibbed);
  qa_combat_state_result after = qa_gameplay_authority_read(FG->combat, actor.id);
  CHECK(after.has_value && !after.value.can_take_damage);

  /* respawn path: button after respawn time requests a respawn */
  character->state.latched_buttons = 1;
  FG->now = 20;
  qa_q2_character_actor_begin_frame(character);
  CHECK(FG->respawns == 1);
  qa_q2_character_actor_respawned(character);
  CHECK(!character->state.dead && !character->state.gibbed);
  CHECK(strcmp(character->entity.model, "players/male/tris.md2") == 0);

  /* checkpoint round-trip preserves slot/name/score */
  (void)snprintf(character->state.name, sizeof character->state.name, "Hero");
  character->state.score = 7;
  qa_q2_base_character_checkpoint checkpoint = qa_q2_character_actor_capture(character);
  CHECK(checkpoint.version == 1 && checkpoint.state.score == 7);
  character->state.score = 0;
  (void)snprintf(character->state.name, sizeof character->state.name, "X");
  qa_q2_character_actor_restore(character, checkpoint, NULL, NULL);
  CHECK(character->state.score == 7 && strcmp(character->state.name, "Hero") == 0);
  qa_q2_base_player_state_checkpoint_clear(&checkpoint.state);
  qa_q2_character_actor_destroy(character);
  fake_teardown();
}

/* ---------------- players lifecycle ---------------- */

typedef struct hook_state {
  int prints, views, userinfos, scoreboards;
  qa_q2_player_movement_change last_change;
} hook_state;
static hook_state HS;

static qa_q2_player_movement hook_movement(void *c, qa_actor_id a) {
  (void)c; (void)a;
  qa_q2_player_movement movement = {0};
  movement.standing_bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  movement.animate_q2 = true;
  movement.grounded = true;
  return movement;
}
static void hook_set_movement(void *c, qa_actor_id a, qa_q2_player_movement_change change) {
  (void)c; (void)a;
  HS.last_change = change;
}
static void hook_emit(void *c, qa_q2_player_event event) {
  (void)c;
  if (strcmp(event.kind, "print") == 0) HS.prints++;
  else if (strcmp(event.kind, "view") == 0) HS.views++;
  else if (strcmp(event.kind, "userinfo") == 0) HS.userinfos++;
  else if (strcmp(event.kind, "scoreboard") == 0) HS.scoreboards++;
}
static void hook_noise(void *c, qa_actor_id a, qa_vec3 o) { (void)c; (void)a; (void)o; }
static qa_q2w_input hook_input(void *c, qa_actor_id a) { (void)c; (void)a; qa_q2w_input in = {0}; return in; }
static bool hook_banned(void *c, const char *ip) { (void)c; return ip && strcmp(ip, "10.0.0.9") == 0; }

/* mirror weapons fake: one actor's blaster state */
typedef struct weapon_fake { bool bound; const char *weapon; } weapon_fake;
static weapon_fake WF;
static qa_q2_base_weapon_definition WDEFS[] = {{"blaster", "q2:weapon_blaster", "weapon_blaster", NULL}};
static bool wf_has(void *c, qa_actor_id a) { (void)c; (void)a; return WF.bound; }
static const char *wf_weapon(void *c, qa_actor_id a) { (void)c; (void)a; return WF.bound ? WF.weapon : NULL; }
static const char *wf_last(void *c, qa_actor_id a) { (void)c; (void)a; return NULL; }
static const char *wf_loop(void *c, qa_actor_id a) { (void)c; (void)a; return NULL; }
static void wf_bind(void *c, qa_q2_entity *e, qa_q2_game_services *g) { (void)c; (void)e; (void)g; WF.bound = true; WF.weapon = "blaster"; }
static const char *wf_request(void *c, qa_q2_entity *e, qa_q2_game_services *g, const char *n) { (void)c; (void)e; (void)g; WF.weapon = n; return "selected"; }
static bool wf_can_drop(void *c, qa_q2_entity *e, qa_q2_game_services *g, const char *n) { (void)c; (void)e; (void)g; (void)n; return true; }
static void wf_tick(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_q2w_input in) { (void)c; (void)e; (void)g; (void)in; }
static bool wf_definition(void *c, const char *n, qa_q2_base_weapon_definition *out) {
  (void)c;
  if (n && strcmp(n, "blaster") == 0) { *out = WDEFS[0]; return true; }
  return false;
}
static const qa_q2_base_weapon_definition *wf_registered(void *c, size_t *n) { (void)c; *n = 1; return WDEFS; }
static void wf_silencer(void *c, qa_actor_id a) { (void)c; (void)a; }
static void wf_set(void *c, qa_actor_id a, const char *n) { (void)c; (void)a; WF.weapon = n; }
static void wf_reset(void *c, qa_actor_id a, const char *n) { (void)c; (void)a; WF.weapon = n; }
static void wf_recoil(void *c, qa_actor_id a, qa_q2_edition e, double now, qa_vec3 *ko, qa_vec3 *ka) {
  (void)c; (void)a; (void)e; (void)now; *ko = QA_Q2_ZERO; *ka = QA_Q2_ZERO;
}
static void item_hook_noop(void *c, qa_actor_id a, qa_item_id i, bool f) { (void)c; (void)a; (void)i; (void)f; }
static void item_hook_silencer(void *c, qa_actor_id a, double n) { (void)c; (void)a; (void)n; }
static void item_hook_armor(void *c, qa_actor_id a, const char *k) { (void)c; (void)a; (void)k; }

static void test_players(void) {
  fake_setup(QA_Q2_MODE_DEATHMATCH);
  HS = (hook_state){0};
  WF = (weapon_fake){0};
  qa_q2_item_hooks item_hooks = {0};
  item_hooks.weapon_picked = item_hook_noop;
  item_hooks.silencer = item_hook_silencer;
  item_hooks.power_armor = item_hook_armor;
  qa_q2_item_module *items = qa_q2_item_module_create(item_hooks);
  CHECK(items != NULL);
  qa_q2_base_weapons weapons = {0};
  weapons.has_state = wf_has; weapons.weapon = wf_weapon; weapons.last_weapon = wf_last;
  weapons.loop_sound = wf_loop; weapons.bind = wf_bind; weapons.request_weapon = wf_request;
  weapons.can_drop = wf_can_drop; weapons.tick = wf_tick; weapons.definition = wf_definition;
  weapons.registered_definitions = wf_registered; weapons.reset_silencer = wf_silencer;
  weapons.set_weapon = wf_set; weapons.reset_state = wf_reset; weapons.recoil = wf_recoil;
  qa_q2_player_hooks hooks = {0};
  hooks.movement = hook_movement; hooks.set_movement = hook_set_movement; hooks.emit = hook_emit;
  hooks.noise = hook_noise; hooks.weapon_input = hook_input; hooks.banned = hook_banned;
  qa_q2_base_player_rules changes = {0};
  changes.password = "secret";
  qa_q2_players *players = qa_q2_players_create(items, weapons, hooks, &changes, QA_Q2_RULE_PASSWORD);
  CHECK(players != NULL);
  CHECK(qa_q2_players_need_password(players) == 1);

  /* connect: banned, wrong password, accepted */
  qa_q2_connection_result result = qa_q2_players_connect(players, &FG->game, "\\name\\Bad\\ip\\10.0.0.9\\password\\secret");
  CHECK(!result.allowed && strcmp(result.reason, "Banned.") == 0);
  CHECK(strstr(result.userinfo, "\\rejmsg\\Banned.") != NULL);
  result = qa_q2_players_connect(players, &FG->game, "\\name\\Q\\password\\wrong");
  CHECK(!result.allowed && strcmp(result.reason, "Password required or incorrect.") == 0);
  result = qa_q2_players_connect(players, &FG->game, "\\name\\Q\\password\\secret");
  CHECK(result.allowed);

  /* a spawn point and a player entity */
  qa_q2_entity *start = fake_create(NULL, "info_player_start", NULL, 0);
  CHECK(start != NULL);
  qa_q2_entity *entity = fake_create(NULL, "player", NULL, 0);
  qa_q2_player_admission admission = {0};
  admission.slot = 99;
  admission.userinfo = "\\name\\Q";
  CHECK_ABORTS(qa_q2_players_attach(players, entity, &FG->game, admission));
  admission.slot = 0;
  admission.userinfo = "\\name\\VeryLongPlayerNameHere\\skin\\female/athena\\fov\\200\\hand\\1";
  admission.initialize_inventory = true;
  qa_q2_base_player_state *state = qa_q2_players_attach(players, entity, &FG->game, admission);
  CHECK(state != NULL);
  CHECK(strlen(state->name) <= 15 && strcmp(state->skin, "female/athena") == 0);
  CHECK(state->fov == 160 && strcmp(state->hand, "left") == 0);
  CHECK(strcmp(state->gender, "neutral") == 0);
  CHECK(HS.userinfos == 1);
  CHECK(WF.bound);
  qa_combat_state_result combat = qa_gameplay_authority_read(FG->combat, entity->actor.id);
  CHECK(combat.has_value && combat.value.health == 100);

  /* invalid userinfo falls back to badinfo */
  qa_q2_players_userinfo_changed(players, entity, &FG->game, "\\name\\bad\"name");
  CHECK(strcmp(state->name, "badinfo") == 0);

  /* putInServer places at the spawn (+10z) and resets the weapon */
  qa_q2_players_put_in_server(players, entity, &FG->game, true, NULL);
  qa_body_state body = qa_q2_game_body(&FG->game, entity);
  CHECK(near(body.origin.z, 10));
  CHECK(strcmp(WF.weapon, "blaster") == 0);
  CHECK(strcmp(HS.last_change.kind, "spawn") == 0);

  /* chat flood protection: 4 messages in 4s then locked */
  const char *say_args[] = {"hello"};
  FG->now = 20;
  CHECK(qa_q2_players_client_command(players, entity, &FG->game, "SAY", say_args, 1));
  CHECK(qa_q2_players_client_command(players, entity, &FG->game, "say", say_args, 1));
  CHECK(qa_q2_players_client_command(players, entity, &FG->game, "say", say_args, 1));
  CHECK(qa_q2_players_client_command(players, entity, &FG->game, "say", say_args, 1));
  int prints_before = HS.prints;
  CHECK(qa_q2_players_client_command(players, entity, &FG->game, "say", say_args, 1));
  CHECK(state->flood_lock_until > FG->now);
  CHECK(HS.prints > prints_before); /* flood notice printed */

  /* unknown command falls back to say (still flood-gated, returns true) */
  CHECK(qa_q2_players_client_command(players, entity, &FG->game, "frobnicate", NULL, 0));

  /* capture/restore round-trip */
  state->score = 42;
  qa_q2_base_players_checkpoint checkpoint = qa_q2_players_capture(players);
  CHECK(checkpoint.version == 1 && checkpoint.player_count == 1);
  CHECK(checkpoint.players[0].state.score == 42);
  state->score = 0;
  qa_q2_players_restore(players, &FG->game, checkpoint);
  qa_q2_base_player_state *restored = qa_q2_players_get_state(players, entity->actor.id);
  CHECK(restored && restored->score == 42);
  qa_q2_base_players_checkpoint_clear(&checkpoint);

  /* worldspawn reserves eight corpse slots for CopyToBodyQue */
  for (int i = 0; i < 8; i++) (void)fake_create(NULL, "bodyque", NULL, 0);
  /* single-player style respawn is covered by deathmatch respawn here:
   * dead + latched button after respawn time respawns in place. */
  restored->dead = true;
  restored->respawn_time = FG->now - 1;
  restored->latched_buttons = 1;
  qa_q2_player_context context = qa_q2_players_context(players, entity, &FG->game);
  qa_q2_players_dead_frame(players, &context);
  CHECK(!restored->dead);
  CHECK(strcmp(restored->event, "q2:player-teleport") == 0);

  qa_q2_players_destroy(players);
  qa_q2_item_module_destroy(items);
  /* entities are owned by the fake; release actors before teardown */
  while (FG->entity_count > 0) fake_remove(NULL, FG->entities[0]);
  fake_teardown();
}

/* ---------------- entity module spawns ---------------- */

static void test_entity_spawns(void) {
  fake_setup(QA_Q2_MODE_SINGLEPLAYER);
  qa_q2_base_entity_hooks hooks = {0};
  qa_q2_base_entity_module *module = qa_q2_base_entity_module_create(hooks);
  CHECK(module != NULL);
  qa_q2_spawn_module spawn_module = qa_q2_base_entity_module_as_spawn(module);
  CHECK(spawn_module.callbacks != NULL);
  CHECK(spawn_module.callbacks->think_count > 0 && spawn_module.callbacks->use_count > 0);

  /* func_plat: creates a center trigger and starts at the bottom */
  qa_q2_entity *plat = fake_create(NULL, "func_plat", NULL, 0);
  size_t before = FG->entity_count;
  CHECK(qa_q2_base_entity_module_spawn(module, plat, &FG->game));
  CHECK(FG->entity_count == before + 1);
  qa_q2_base_platform_state *platform = qa_q2_base_entity_module_platform_state(module, plat);
  CHECK(platform != NULL && strcmp(platform->phase, "bottom") == 0);
  CHECK(plat->damage == 2 && plat->use.fn != NULL);
  qa_q2_base_traversal traversal = qa_q2_base_entity_module_mover_traversal(module, plat);
  CHECK(traversal.has_value && !traversal.locked);

  /* trigger_hurt defaults */
  qa_q2_entity *hurt = fake_create(NULL, "trigger_hurt", NULL, 0);
  CHECK(qa_q2_base_entity_module_spawn(module, hurt, &FG->game));
  CHECK(hurt->damage == 5 && hurt->touch.fn != NULL && strcmp(hurt->solid, "trigger") == 0);

  /* func_clock without a target is diagnosed and removed */
  qa_q2_entity *clock = fake_create(NULL, "func_clock", NULL, 0);
  qa_actor_id clock_id = clock->actor.id;
  CHECK(qa_q2_base_entity_module_spawn(module, clock, &FG->game));
  CHECK(FG->diagnostics > 0);
  CHECK(fake_entity_of(NULL, &clock_id) == NULL);

  /* turret_base gets the shared blocked callback */
  qa_q2_entity *base = fake_create(NULL, "turret_base", NULL, 0);
  CHECK(qa_q2_base_entity_module_spawn(module, base, &FG->game));
  CHECK(base->blocked.fn != NULL && strcmp(base->solid, "brush") == 0);

  /* unknown classname is not handled */
  qa_q2_entity *other = fake_create(NULL, "monster_soldier", NULL, 0);
  CHECK(!qa_q2_base_entity_module_spawn(module, other, &FG->game));

  /* capture/restore of the platform state */
  qa_q2_base_entities_module_checkpoint checkpoint = qa_q2_base_entity_module_capture(module, &FG->game);
  CHECK(checkpoint.version == 1 && checkpoint.movers.platform_count == 1);
  platform->phase = "up";
  qa_q2_base_entity_module_restore(module, &FG->game, checkpoint);
  platform = qa_q2_base_entity_module_platform_state(module, plat);
  CHECK(platform != NULL && strcmp(platform->phase, "bottom") == 0);
  free(checkpoint.movers.platforms);
  free(checkpoint.movers.secrets);
  free(checkpoint.scenery.animations);
  free(checkpoint.scenery.clocks);
  free(checkpoint.turrets.breaches);
  free(checkpoint.turrets.drivers);
  free(checkpoint.wind_times);

  qa_q2_base_entity_module_destroy(module);
  while (FG->entity_count > 0) fake_remove(NULL, FG->entities[0]);
  fake_teardown();
}

int main(void) {
  test_pure_helpers();
  test_obituary();
  test_character();
  test_players();
  test_entity_spawns();
  if (failures) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok %d checks\n", checks);
  return 0;
}

