/*
 * Port of key cases from tests/gameplay/q2/horde.test.ts,
 * tests/gameplay/q2-multiplayer/ctf/core.test.ts, and
 * tests/content/q2/lmctf (rules defaults; the application/console
 * cases themselves belong to app/settings slices). GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/multiplayer.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

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

/* ---------------- fake host + services fixture ---------------- */
#define MAX_PLAYERS 8
#define MAX_EVENTS 256

struct fixture {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_body_table *bodies;
  qa_shared_inventory_table *inventory;
  qa_gameplay_authority *combat;
  qa_q2_foundation_host host;
  qa_q2_entity_services *services;
  qa_q2_game_services *game;
  qa_q2_spawn_module modules[2];
  qa_q2_item_module *items;
  double now;
  qa_owned_actor world;
  qa_owned_actor owned[MAX_PLAYERS];
  qa_q2mp_player_state players[MAX_PLAYERS];
  int player_count;
  /* recorded presentation prints */
  char prints[MAX_EVENTS][512];
  qa_actor_id print_actors[MAX_EVENTS];
  bool print_has_actor[MAX_EVENTS];
  char print_levels[MAX_EVENTS][16];
  int print_count;
  /* recorded module events */
  qa_q2_ctf_event ctf_events[64];
  int ctf_event_count;
  qa_lmctf_event lmctf_events[64];
  int lmctf_event_count;
  char destinations[16][64];
  int destination_count;
  double deathmatch_flags;
  /* fake weapons mirror */
  qa_q2mp_weapons weapons;
  qa_q2mp_weapon_state weapon_states[MAX_PLAYERS];
  qa_q2mp_source_rules source_rules;
  bool has_source_rules;
  int weapons_registered;
};

static struct fixture *F;

static double host_now(void *ctx) { (void)ctx; return F->now; }
static double host_frame_seconds(void *ctx) { (void)ctx; return 0.1; }
static double host_gravity(void *ctx) { (void)ctx; return 800; }
static double host_random(void *ctx) { (void)ctx; return 0.25; }
static void host_noop_schedule(void *ctx, qa_owned_actor a, const double *due) { (void)ctx; (void)a; (void)due; }
static void host_noop_actor(void *ctx, qa_owned_actor a) { (void)ctx; (void)a; }
static qa_trace_result host_trace(void *ctx, qa_q2_trace_request request) {
  (void)ctx;
  qa_trace_result result = {0};
  result.kind = "q2";
  result.fraction = 1;
  result.end = request.end;
  return result;
}
static double host_point_contents(void *ctx, qa_vec3 p) { (void)ctx; (void)p; return 0; }
static bool host_true2(void *ctx, qa_vec3 a, qa_vec3 b) { (void)ctx; (void)a; (void)b; return true; }
static const qa_actor_id *host_players(void *ctx, size_t *out) {
  (void)ctx;
  static qa_actor_id ids[MAX_PLAYERS];
  for (int i = 0; i < F->player_count; i++) ids[i] = F->owned[i].id;
  *out = (size_t)F->player_count;
  return ids;
}
static const qa_actor_id *host_nearby(void *ctx, qa_vec3 o, double r, size_t *out) {
  (void)o; (void)r;
  return host_players(ctx, out);
}
static qa_actor_id host_world(void *ctx) { (void)ctx; return F->world.id; }
static bool host_is_player(void *ctx, qa_actor_id actor) {
  (void)ctx;
  for (int i = 0; i < F->player_count; i++)
    if (qa_same_actor(F->owned[i].id, actor)) return true;
  return false;
}
static bool host_is_monster(void *ctx, qa_actor_id a) { (void)ctx; (void)a; return false; }
static qa_bounds host_inline_bounds(void *ctx, double m) { (void)ctx; (void)m; qa_bounds b = {0}; return b; }
static void host_set_solid(void *ctx, qa_owned_actor a, const char *s, const double *m) { (void)ctx; (void)a; (void)s; (void)m; }
static void host_set_motion(void *ctx, qa_q2_motion m) { (void)ctx; (void)m; }
static void host_set_portal(void *ctx, double p, bool open) { (void)ctx; (void)p; (void)open; }
static void host_emit(void *ctx, qa_q2_presentation_event event) {
  (void)ctx;
  if (strcmp(event.kind, "print") == 0 && F->print_count < MAX_EVENTS) {
    int i = F->print_count++;
    snprintf(F->prints[i], sizeof F->prints[i], "%s", event.text == NULL ? "" : event.text);
    F->print_has_actor[i] = event.has_actor;
    F->print_actors[i] = event.actor;
    snprintf(F->print_levels[i], sizeof F->print_levels[i], "%s", event.level == NULL ? "" : event.level);
  }
}
static qa_q2_player_view_result host_view(void *ctx, qa_actor_id a) {
  (void)ctx; (void)a;
  qa_q2_player_view_result r = {0};
  r.has_value = true;
  return r;
}
static void host_key_consumed(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }
static void host_prepare_change(void *ctx, const char *m, const qa_q2_landmark_carry *l, double f) { (void)ctx; (void)m; (void)l; (void)f; }
static void host_transition(void *ctx, qa_transition_intent t) { (void)ctx; (void)t; }
static void host_diagnostic(void *ctx, const char *message) {
  /* Monster spawn handlers are outside this slice, so unsupported
   * horde monster spawns report here; record instead of aborting. */
  (void)ctx;
  (void)message;
}
static qa_bounds abs_bounds(void *ctx, qa_owned_actor a, qa_body_state s) {
  (void)ctx;
  return qa_translated_body_bounds(a, s);
}
static void on_link(void *ctx, qa_linked_body b) { (void)ctx; (void)b; }
static void on_unlink(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }

/* fake weapons mirror */
static int player_index(qa_actor_id actor) {
  for (int i = 0; i < F->player_count; i++)
    if (qa_same_actor(F->owned[i].id, actor)) return i;
  return -1;
}
static qa_q2mp_weapon_state *w_get_state(void *ctx, qa_actor_id actor) {
  (void)ctx;
  int i = player_index(actor);
  return i < 0 ? NULL : &F->weapon_states[i];
}
static bool w_get_input(void *ctx, qa_actor_id actor, qa_q2mp_weapon_input *out) {
  (void)ctx; (void)actor;
  memset(out, 0, sizeof *out);
  out->hand = "right";
  out->has_players_collide = true;
  out->players_collide = true;
  return true;
}
static void w_register(void *ctx, qa_q2mp_weapon_extension e) { (void)ctx; (void)e; F->weapons_registered++; }
static void w_set_rules(void *ctx, qa_q2mp_source_rules rules) { (void)ctx; F->source_rules = rules; F->has_source_rules = true; }
static const char *w_request(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const char *name) { (void)ctx; (void)e; (void)g; (void)name; return "selected"; }
static void w_kick(void *ctx, qa_q2mp_weapon_context *c, qa_vec3 o, qa_vec3 a) { (void)ctx; (void)c; (void)o; (void)a; }
static void w_generic(void *ctx, qa_q2mp_weapon_context *c) { (void)ctx; (void)c; }
static double w_silencer(void *ctx, qa_actor_id a) { (void)ctx; (void)a; return 0; }
static void w_noise(void *ctx, qa_actor_id a, qa_q2_game_services *g, qa_vec3 o, const char *k) { (void)ctx; (void)a; (void)g; (void)o; (void)k; }
static const qa_q2f_weapon_definition *w_definition(void *ctx, const char *name) { (void)ctx; (void)name; return NULL; }
static double w_ammo(void *ctx, qa_q2mp_weapon_context *c) { (void)ctx; (void)c; return 100; }
static void w_no_ammo(void *ctx, qa_q2mp_weapon_context *c, bool s) { (void)ctx; (void)c; (void)s; }
static void w_project(void *ctx, qa_q2mp_weapon_context *c, qa_vec3 off, qa_vec3 *start, qa_vec3 *dir) {
  (void)ctx; (void)c; (void)off;
  *start = (qa_vec3){0, 0, 0};
  *dir = (qa_vec3){1, 0, 0};
}
static void w_ammo_changed(void *ctx, qa_actor_id a, qa_item_id i) { (void)ctx; (void)a; (void)i; }

/* hooks shared by CTF and LMCTF fixtures */
static qa_q2mp_player_state *hook_player(void *ctx, qa_actor_id actor) {
  (void)ctx;
  int i = player_index(actor);
  return i < 0 ? NULL : &F->players[i];
}
static void hook_set_skin(void *ctx, qa_actor_id actor, const char *skin) {
  (void)ctx;
  static char skins[MAX_PLAYERS][128];
  int i = player_index(actor);
  if (i >= 0) {
    snprintf(skins[i], sizeof skins[i], "%s", skin);
    F->players[i].skin = skins[i];
  }
}
static qa_q2_ctf *cur_ctf;
static void hook_spawn_player_ctf(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  (void)ctx;
  qa_vec3 origin, angles;
  if (qa_q2_ctf_select_spawn(cur_ctf, entity, g, &origin, &angles)) {
    qa_q2_body_changes move = {0};
    move.has_origin = true; move.origin = origin;
    move.has_angles = true; move.angles = angles;
    qa_q2_game_move(g, entity, move, true);
  }
  qa_gameplay_authority_set_health(F->combat, entity->actor, 100);
  qa_combat_trait_changes traits = {0};
  traits.has_can_take_damage = true;
  traits.can_take_damage = true;
  qa_gameplay_authority_set_traits(F->combat, entity->actor, traits);
  g->solid(g->ctx, entity, "box");
}
static void hook_spawn_player_plain(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  (void)ctx;
  qa_gameplay_authority_set_health(F->combat, entity->actor, 100);
  g->solid(g->ctx, entity, "box");
}
static void hook_observer(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) { (void)ctx; (void)e; (void)g; }
static void hook_teleport(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 o, qa_vec3 a, qa_vec3 v) {
  (void)ctx;
  qa_q2_body_changes move = {0};
  move.has_origin = true; move.origin = o;
  move.has_angles = true; move.angles = a;
  move.has_velocity = true; move.velocity = v;
  qa_q2_game_move(g, e, move, true);
}
static void hook_chase(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }
static void hook_prediction(void *ctx, qa_actor_id a, bool s) { (void)ctx; (void)a; (void)s; }
static double hook_gravity(void *ctx) { (void)ctx; return 800; }
static void hook_emit_ctf(void *ctx, qa_q2_ctf_event event) {
  (void)ctx;
  if (F->ctf_event_count < 64) F->ctf_events[F->ctf_event_count++] = event;
}
static void hook_emit_lmctf(void *ctx, qa_lmctf_event event) {
  (void)ctx;
  if (F->lmctf_event_count < 64) F->lmctf_events[F->lmctf_event_count++] = event;
}
static void hook_end_level(void *ctx, qa_q2_game_services *g, const char *map) {
  (void)ctx; (void)g;
  if (F->destination_count < 16)
    snprintf(F->destinations[F->destination_count++], 64, "%s", map == NULL ? "<null>" : map);
}
static void hook_kick_actor(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }
static void hook_set_flags(void *ctx, double flags) { (void)ctx; F->deathmatch_flags = flags; if (F->game) F->game->options.deathmatch_flags = flags; }
static bool hook_chat(void *ctx, qa_actor_id a, qa_q2_game_services *g) { (void)ctx; (void)a; (void)g; return true; }
static void item_hook_noop(void *ctx, qa_actor_id a, qa_item_id i, bool f) { (void)ctx; (void)a; (void)i; (void)f; }
static void item_silencer(void *ctx, qa_actor_id a, double c) { (void)ctx; (void)a; (void)c; }
static void item_power(void *ctx, qa_actor_id a, const char *k) { (void)ctx; (void)a; (void)k; }

static void fixture_init(struct fixture *fx, int player_count) {
  memset(fx, 0, sizeof *fx);
  F = fx;
  CHECK(qa_identity_owner_create(&fx->identities, "original-q2-multiplayer-test"));
  fx->actors = qa_session_actor_registry_create(&fx->identities, 1024);
  fx->callbacks = qa_actor_callback_table_create(fx->actors);
  qa_body_link_hooks body_hooks = {0};
  body_hooks.absolute_bounds = abs_bounds;
  body_hooks.on_link = on_link;
  body_hooks.on_unlink = on_unlink;
  fx->bodies = qa_shared_body_table_create(fx->actors, body_hooks);
  fx->inventory = qa_shared_inventory_table_create(fx->actors);
  qa_gameplay_hooks combat_hooks = {0};
  fx->combat = qa_gameplay_authority_create(fx->actors, fx->callbacks, combat_hooks);
  CHECK(fx->actors && fx->bodies && fx->inventory && fx->combat);
  fx->host.actors = fx->actors;
  fx->host.bodies = fx->bodies;
  fx->host.callbacks = fx->callbacks;
  fx->host.combat = fx->combat;
  fx->host.inventory = fx->inventory;
  fx->host.now = host_now;
  fx->host.frame_seconds = host_frame_seconds;
  fx->host.gravity = host_gravity;
  fx->host.random = host_random;
  fx->host.schedule = host_noop_schedule;
  fx->host.touch_triggers = host_noop_actor;
  fx->host.trace = host_trace;
  fx->host.point_contents = host_point_contents;
  fx->host.in_pvs = host_true2;
  fx->host.in_phs = host_true2;
  fx->host.areas_connected = host_true2;
  fx->host.nearby = host_nearby;
  fx->host.players = host_players;
  fx->host.world_actor = host_world;
  fx->host.is_player = host_is_player;
  fx->host.is_monster = host_is_monster;
  fx->host.inline_model_bounds = host_inline_bounds;
  fx->host.set_solid = host_set_solid;
  fx->host.set_motion = host_set_motion;
  fx->host.set_area_portal = host_set_portal;
  fx->host.emit = host_emit;
  fx->host.player_view_state = host_view;
  fx->host.key_consumed = host_key_consumed;
  fx->host.prepare_level_change = host_prepare_change;
  fx->host.transition = host_transition;
  fx->host.diagnostic = host_diagnostic;
  static const char *const names[MAX_PLAYERS] = {"red", "redmate", "blue", "bluemate", "p4", "p5", "p6", "p7"};
  fx->world = qa_session_actor_registry_allocate_at_source(fx->actors, "q2:game", 0, "q2:worldspawn");
  qa_body_state world_body = {0};
  qa_shared_body_table_create_body(fx->bodies, fx->world, world_body);
  fx->player_count = player_count;
  for (int i = 0; i < player_count; i++) {
    fx->owned[i] = qa_session_actor_registry_allocate(fx->actors, "q3:character", "q3:sarge");
    qa_q2mp_player_state_init(&fx->players[i], i, 0);
    fx->players[i].name = names[i];
    fx->players[i].skin = "male/grunt";
    static char infos[MAX_PLAYERS][128];
    snprintf(infos[i], sizeof infos[i], "\\name\\%s\\skin\\male/grunt", names[i]);
    fx->players[i].userinfo = infos[i];
    qa_body_state body = {0};
    body.origin = (qa_vec3){i * 512.0, 0, 64};
    body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    qa_shared_body_table_create_body(fx->bodies, fx->owned[i], body);
    qa_combat_state combat = {0};
    combat.health = 100;
    combat.mass = 200;
    combat.can_take_damage = true;
    combat.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(fx->combat, fx->owned[i], combat, NULL, NULL);
    qa_shared_inventory_table_create_entries(fx->inventory, fx->owned[i], NULL, 0);
    memset(&fx->weapon_states[i], 0, sizeof fx->weapon_states[i]);
    fx->weapon_states[i].animation.phase = "ready";
    fx->weapon_states[i].weapon = "blaster";
    fx->weapon_states[i].primary_handoff = "active";
  }
  fx->weapons.get_state = w_get_state;
  fx->weapons.get_input = w_get_input;
  fx->weapons.register_weapon = w_register;
  fx->weapons.set_source_rules = w_set_rules;
  fx->weapons.request_weapon = w_request;
  fx->weapons.kick = w_kick;
  fx->weapons.generic_classic = w_generic;
  fx->weapons.generic_rerelease = w_generic;
  fx->weapons.silencer_shots = w_silencer;
  fx->weapons.player_noise = w_noise;
  fx->weapons.definition = w_definition;
  fx->weapons.ammo = w_ammo;
  fx->weapons.no_ammo = w_no_ammo;
  fx->weapons.project = w_project;
  fx->weapons.ammo_changed = w_ammo_changed;
  qa_q2_item_hooks item_hooks = {0};
  item_hooks.weapon_picked = item_hook_noop;
  item_hooks.silencer = item_silencer;
  item_hooks.power_armor = item_power;
  fx->items = qa_q2_item_module_create(item_hooks);
  CHECK(fx->items != NULL);
}

static void fixture_start_services(struct fixture *fx, const qa_q2_spawn_module *modules, size_t module_count) {
  qa_q2_game_options options = {0};
  options.edition = QA_Q2_EDITION_CLASSIC;
  options.map_name = "q2ctf1";
  options.skill = 1;
  options.mode = QA_Q2_MODE_DEATHMATCH;
  options.deathmatch_flags = 0;
  options.max_clients = 8;
  options.provider = "q2:game";
  options.campaign = "q2:campaign";
  options.combat_provider = "q2:combat";
  options.inventory_provider = "q2:inventory";
  options.movement_provider = "q3:movement";
  fx->services = qa_q2_entity_services_create(&fx->host, options, modules, module_count);
  CHECK(fx->services != NULL);
  fx->game = qa_q2_entity_services_game(fx->services);
}

static void fixture_advance(struct fixture *fx, double time) {
  fx->now = time;
  size_t n = 0;
  qa_q2_entity *const *all = fx->game->entities(fx->game->ctx, &n);
  qa_q2_entity **copy = malloc((n == 0 ? 1 : n) * sizeof *copy);
  for (size_t i = 0; i < n; i++) copy[i] = all[i];
  for (size_t i = 0; i < n; i++) {
    qa_q2_entity *entity = copy[i];
    if (entity->has_next_think && entity->next_think <= fx->now && entity->think.fn != NULL) {
      qa_q2_think think = entity->think;
      entity->has_next_think = false;
      think.fn(think.ctx, entity, fx->game);
    }
  }
  free(copy);
}

static qa_q2_entity *fixture_spawn(struct fixture *fx, const char *classname, double x) {
  qa_q2_spawn_fields fields = {0};
  fields.ordinal = 1;
  fields.classname = classname;
  char origin[64];
  snprintf(origin, sizeof origin, "%.0f 0 64", x);
  qa_q2_spawn_set(&fields, "classname", classname);
  qa_q2_spawn_set(&fields, "origin", origin);
  return fx->game->spawn(fx->game->ctx, fields);
}

static qa_q2_entity *fixture_player_entity(struct fixture *fx, int slot) {
  return qa_q2_game_entity_of(fx->game, &fx->owned[slot].id);
}

static void fixture_touch(qa_q2_entity *entity, qa_q2_entity *player, qa_q2_game_services *g) {
  CHECK(entity->touch.fn != NULL);
  qa_touch_contact contact = {0};
  contact.self = entity->actor;
  contact.other = player->actor.id;
  entity->touch.fn(entity->touch.ctx, entity, g, contact);
}

static qa_q2_ctf_hooks ctf_hooks(struct fixture *fx) {
  qa_q2_ctf_hooks hooks = {0};
  hooks.items = fx->items;
  hooks.weapons = &fx->weapons;
  hooks.player = hook_player;
  hooks.set_skin = hook_set_skin;
  hooks.spawn_player = hook_spawn_player_ctf;
  hooks.observer = hook_observer;
  hooks.teleport = hook_teleport;
  hooks.chase = hook_chase;
  hooks.set_grapple_prediction = hook_prediction;
  hooks.gravity = hook_gravity;
  hooks.emit = hook_emit_ctf;
  hooks.end_level = hook_end_level;
  hooks.kick = hook_kick_actor;
  hooks.set_deathmatch_flags = hook_set_flags;
  hooks.chat_allowed = hook_chat;
  return hooks;
}

static qa_lmctf_hooks lmctf_hooks(struct fixture *fx) {
  qa_lmctf_hooks hooks = {0};
  hooks.items = fx->items;
  hooks.weapons = &fx->weapons;
  hooks.player = hook_player;
  hooks.set_skin = hook_set_skin;
  hooks.spawn_player = hook_spawn_player_plain;
  hooks.observer = hook_observer;
  hooks.teleport = hook_teleport;
  hooks.chase = hook_chase;
  hooks.set_grapple_prediction = hook_prediction;
  hooks.gravity = hook_gravity;
  hooks.emit = hook_emit_lmctf;
  hooks.end_level = hook_end_level;
  hooks.kick = hook_kick_actor;
  hooks.set_deathmatch_flags = hook_set_flags;
  hooks.chat_allowed = hook_chat;
  return hooks;
}

/* ---------------- rules defaults (settings bind sources) ---------------- */
static void test_rules_defaults(void) {
  qa_q2_ctf_rules rules;
  qa_create_q2_ctf_rules(&rules);
  CHECK(strcmp(rules.force_join, "") == 0);
  CHECK(rules.competition == 0 && rules.match_lock && rules.election_percentage == 66);
  CHECK(rules.match_minutes == 20 && rules.setup_minutes == 10 && rules.start_seconds == 20);
  CHECK(rules.capture_limit == 0 && !rules.instant_weapons);
  CHECK(rules.warp_count == 5 && strcmp(rules.warp_list[0], "q2ctf1") == 0 && strcmp(rules.warp_list[4], "q2ctf5") == 0);
  qa_q2_ctf_rules_destroy(&rules);

  qa_lmctf_rules lm;
  qa_create_lmctf_rules(&lm);
  CHECK(lm.ctf_flags == 0 && lm.ref_flags == 0 && lm.runes == 15 && lm.skin_set == 0);
  CHECK(!lm.flag_init && lm.disabled_weapons == 0);
  CHECK(lm.time_limit_minutes == 0 && lm.frag_limit == 0 && lm.map_count == 0);
  CHECK(!lm.fast_switch && !lm.auto_lock && lm.countdown_seconds == 15 && lm.quad_seconds == 30);
  qa_lmctf_rules_destroy(&lm);

  /* LMCTF_RUNES order matches settings/server/lmctf.c expectations. */
  CHECK(strcmp(QA_LMCTF_RUNES[0].kind, "damage") == 0 && QA_LMCTF_RUNES[0].bit == 1);
  CHECK(strcmp(QA_LMCTF_RUNES[1].kind, "haste") == 0 && QA_LMCTF_RUNES[1].bit == 4);
  CHECK(strcmp(QA_LMCTF_RUNES[2].kind, "resist") == 0 && QA_LMCTF_RUNES[2].bit == 2);
  CHECK(strcmp(QA_LMCTF_RUNES[3].kind, "regen") == 0 && QA_LMCTF_RUNES[3].bit == 8);
  CHECK(strcmp(QA_LMCTF_RUNES[4].kind, "vampire") == 0 && QA_LMCTF_RUNES[4].bit == 16);
  CHECK(strcmp(QA_LMCTF_RUNES[0].name, "Damage Artifact") == 0);
  CHECK(strcmp(QA_LMCTF_RUNES[4].name, "Vampire Artifact") == 0);

  CHECK(strcmp(qa_ctf_team_name(1), "RED") == 0);
  CHECK(strcmp(qa_ctf_team_name(2), "BLUE") == 0);
  CHECK(strcmp(qa_ctf_team_name(0), "UNKNOWN") == 0);
  CHECK(qa_other_ctf_team(1) == 2 && qa_other_ctf_team(2) == 1);
}

/* ---------------- horde (tests/gameplay/q2/horde.test.ts) ---------------- */
static void test_horde(void) {
  struct fixture fx;
  fixture_init(&fx, 1);
  qa_q2_horde *horde = qa_q2_horde_create();
  CHECK(horde != NULL);
  fx.modules[0] = qa_q2_horde_as_spawn(horde);
  fixture_start_services(&fx, fx.modules, 1);
  qa_q2_entity *player = qa_q2_entity_services_attach_player(fx.services, fx.owned[0]);
  CHECK(player != NULL);
  player->classname = "player";
  qa_q2_entity *manager = fixture_spawn(&fx, "horde_manager", 0);
  qa_q2_entity *point = fixture_spawn(&fx, "info_monster_start", 64);
  CHECK(qa_q2_horde_manager(horde) == manager);
  CHECK(point != NULL);
  qa_q2_entity stray_entity = {0};
  stray_entity.classname = "player";
  CHECK(!qa_q2_horde_spawn(horde, &stray_entity, fx.game));

  qa_q2_think think = manager->think;
  CHECK(think.fn != NULL);
  fx.now = 1;
  think.fn(think.ctx, manager, fx.game);
  think.fn(think.ctx, manager, fx.game);
  CHECK(qa_q2_horde_wave(horde) == 1);
  size_t n = 0;
  qa_q2_entity *const *all = fx.game->entities(fx.game->ctx, &n);
  int monsters = 0;
  for (size_t i = 0; i < n; i++)
    if (strncmp(all[i]->classname, "monster_", 8) == 0) monsters++;
  CHECK(monsters > 0 && monsters <= 12);
  int first_wave = monsters;

  for (size_t i = 0; i < n; i++) {
    if (strncmp(all[i]->classname, "monster_", 8) != 0) continue;
    qa_combat_state combat = {0};
    combat.health = 0;
    qa_gameplay_authority_create_state(fx.combat, all[i]->actor, combat, NULL, NULL);
  }
  think.fn(think.ctx, manager, fx.game);
  for (int i = 0; i < 12; i++) think.fn(think.ctx, manager, fx.game);
  CHECK(qa_q2_horde_wave(horde) >= 2);
  think.fn(think.ctx, manager, fx.game);
  all = fx.game->entities(fx.game->ctx, &n);
  monsters = 0;
  for (size_t i = 0; i < n; i++)
    if (strncmp(all[i]->classname, "monster_", 8) == 0) monsters++;
  CHECK(monsters > first_wave);

  qa_q2_horde_checkpoint saved;
  CHECK(qa_q2_horde_capture(horde, &saved));
  qa_q2_horde *restored = qa_q2_horde_create();
  qa_q2_horde_restore(restored, fx.game, saved);
  CHECK(qa_q2_horde_wave(restored) == saved.wave);
  CHECK(qa_q2_horde_manager(restored) != NULL);
  CHECK(strcmp(qa_q2_horde_manager(restored)->classname, "horde_manager") == 0);
  free(saved.monsters);
  qa_q2_horde_destroy(restored);
  qa_q2_horde_destroy(horde);
}

/* ---------------- CTF (core.test.ts key cases) ---------------- */
static qa_q2_ctf *ctf_fixture(struct fixture *fx) {
  fixture_init(fx, 3);
  qa_q2_ctf *ctf = qa_q2_ctf_create(ctf_hooks(fx), NULL, 0, NULL);
  CHECK(ctf != NULL);
  cur_ctf = ctf;
  fx->modules[0] = qa_q2_ctf_as_spawn(ctf);
  fx->modules[1] = qa_q2_item_module_as_spawn(fx->items);
  fixture_start_services(fx, fx->modules, 2);
  fixture_spawn(fx, "info_player_team1", 0);
  fixture_spawn(fx, "info_player_team2", 1024);
  fixture_spawn(fx, "info_player_deathmatch", 512);
  for (int i = 0; i < 3; i++) {
    qa_q2_entity *entity = qa_q2_entity_services_attach_player(fx->services, fx->owned[i]);
    CHECK(entity != NULL);
    qa_q2_ctf_admitted(ctf, entity, fx->game);
    qa_q2_ctf_join(ctf, entity, fx->game, i == 2 ? 2 : 1, false);
  }
  return ctf;
}

static void test_ctf_flags(void) {
  struct fixture fx;
  qa_q2_ctf *ctf = ctf_fixture(&fx);
  qa_q2_entity *red_base = fixture_spawn(&fx, "item_flag_team1", 0);
  qa_q2_entity *blue_base = fixture_spawn(&fx, "item_flag_team2", 1024);
  fixture_advance(&fx, 0.2);
  fixture_advance(&fx, 0.3);
  qa_q2_entity *red = fixture_player_entity(&fx, 0);
  qa_q2_entity *mate = fixture_player_entity(&fx, 1);
  qa_q2_entity *blue = fixture_player_entity(&fx, 2);
  fixture_touch(blue_base, red, fx.game);
  fixture_touch(red_base, blue, fx.game);
  CHECK(qa_shared_inventory_table_count(fx.inventory, red->actor.id, "q2:item_flag_team2") == 1);
  CHECK(!blue_base->visible);
  fixture_advance(&fx, 1);
  qa_q2_ctf_flags_drop(qa_q2_ctf_get_flags(ctf), blue, fx.game);
  size_t n = 0;
  qa_q2_entity *const *all = fx.game->entities(fx.game->ctx, &n);
  qa_q2_entity *dropped = NULL;
  for (size_t i = 0; i < n; i++)
    if (strcmp(all[i]->classname, "item_flag_team1") == 0 && all[i] != red_base) dropped = all[i];
  CHECK(dropped != NULL);
  if (dropped == NULL) return;
  fixture_touch(dropped, blue, fx.game);
  CHECK(qa_shared_inventory_table_count(fx.inventory, blue->actor.id, "q2:item_flag_team1") == 0);
  fixture_touch(dropped, mate, fx.game);
  CHECK(strcmp(qa_q2_ctf_flags_state(qa_q2_ctf_get_flags(ctf), fx.game, 1), "base") == 0);
  fixture_touch(red_base, red, fx.game);
  CHECK(qa_q2_ctf_get_context(ctf)->match.team1 == 1);
  CHECK(qa_shared_inventory_table_count(fx.inventory, red->actor.id, "q2:item_flag_team2") == 0);
  CHECK(fx.players[0].score == 15);
  CHECK(fx.players[1].score == 12);
  CHECK(fx.players[2].score == 0);
  CHECK(strcmp(qa_q2_ctf_flags_state(qa_q2_ctf_get_flags(ctf), fx.game, 2), "base") == 0);

  qa_q2_ctf_presentation_scoreboard(qa_q2_ctf_get_presentation(ctf), red, fx.game);
  CHECK(fx.ctf_event_count > 0);
  qa_q2_ctf_event scoreboard = fx.ctf_events[fx.ctf_event_count - 1];
  CHECK(strcmp(scoreboard.kind, "scoreboard") == 0);
  CHECK(scoreboard.totals[0] == 27 && scoreboard.totals[1] == 0);
  CHECK(strlen(scoreboard.layout) < 1000);

  const char *const say_args[2] = {"%h", "%t"};
  int prints_before = fx.print_count;
  CHECK(qa_q2_ctf_command(ctf, red, fx.game, "say_team", say_args, 2));
  int chat = 0;
  for (int i = prints_before; i < fx.print_count; i++)
    if (strcmp(fx.print_levels[i], "chat") == 0) chat++;
  CHECK(chat == 2);
  qa_q2_ctf_destroy(ctf);
}

static void test_ctf_match_and_checkpoint(void) {
  struct fixture fx;
  qa_q2_ctf *ctf = ctf_fixture(&fx);
  qa_q2_entity *red = fixture_player_entity(&fx, 0);
  qa_q2_entity *mate = fixture_player_entity(&fx, 1);
  qa_q2_entity *blue = fixture_player_entity(&fx, 2);
  qa_q2_ctf_get_context(ctf)->match.phase = "setup";
  qa_q2_ctf_get_context(ctf)->match.match_time = 600;
  qa_q2_ctf_match *match = qa_q2_ctf_get_match(ctf);
  CHECK(qa_q2_ctf_match_ready(match, red, fx.game, true));
  CHECK(qa_q2_ctf_match_ready(match, mate, fx.game, true));
  CHECK(qa_q2_ctf_match_ready(match, blue, fx.game, true));
  CHECK(strcmp(qa_q2_ctf_get_context(ctf)->match.phase, "pregame") == 0);
  fx.now = 20;
  qa_q2_ctf_check_rules(ctf, fx.game);
  CHECK(strcmp(qa_q2_ctf_get_context(ctf)->match.phase, "game") == 0);
  CHECK(fx.players[0].dead);
  fx.now = 24;
  qa_q2_ctf_before_player(ctf, red, fx.game);
  qa_q2_ctf_before_player(ctf, mate, fx.game);
  qa_q2_ctf_before_player(ctf, blue, fx.game);
  CHECK(!fx.players[0].dead);
  qa_q2_ctf_player_state *red_state = qa_ctf_player(qa_q2_ctf_get_context(ctf), red->actor.id);
  CHECK(red_state->has_ghost_code);
  double ghost = red_state->ghost_code;
  qa_q2_ctf_observer(ctf, red, fx.game);
  CHECK(qa_q2_ctf_match_restore_ghost(match, red, fx.game, ghost));
  CHECK(qa_ctf_player(qa_q2_ctf_get_context(ctf), red->actor.id)->team == 1);

  free(qa_q2_ctf_get_context(ctf)->rules.admin_password);
  qa_q2_ctf_get_context(ctf)->rules.admin_password = strdup("private");
  CHECK(qa_q2_ctf_match_admin(match, red, fx.game, "private"));
  CHECK(!qa_q2_ctf_match_warp(match, red, fx.game, "../../evil"));
  CHECK(fx.destination_count == 0);
  CHECK(qa_q2_ctf_match_warp(match, red, fx.game, "Q2CTF3"));
  CHECK(fx.destination_count == 1 && strcmp(fx.destinations[0], "q2ctf3") == 0);
  qa_q2_ctf_admin_settings settings = {15, 5, 10, true, true, false, true, false};
  CHECK(qa_q2_ctf_match_configure(match, red, fx.game, settings));
  CHECK(((int)fx.deathmatch_flags & 20) == 20);

  qa_q2_ctf_checkpoint captured;
  qa_q2_ctf_capture(ctf, &captured);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_ctf_checkpoint(&captured, &length);
  CHECK(bytes != NULL);
  bool leaked = false;
  for (size_t i = 0; i + 7 <= length; i++)
    if (memcmp(bytes + i, "private", 7) == 0) leaked = true;
  CHECK(!leaked);
  qa_q2_ctf_checkpoint decoded;
  qa_decode_q2_ctf_checkpoint(bytes, length, &decoded);
  CHECK(decoded.version == 1);
  CHECK(strcmp(decoded.match.phase, "game") == 0);
  CHECK(decoded.player_count == 3);
  CHECK(decoded.rules.match_minutes == 15);
  free(bytes);
  qa_q2_ctf_checkpoint_destroy(&decoded);
  qa_q2_ctf_checkpoint_destroy(&captured);

  CHECK(qa_q2_ctf_match_begin_election(match, mate, fx.game, "map", "q2ctf2"));
  CHECK(!qa_q2_ctf_match_vote(match, mate, fx.game, true));
  CHECK(qa_q2_ctf_match_vote(match, blue, fx.game, true));
  CHECK(fx.destination_count >= 2 && strcmp(fx.destinations[fx.destination_count - 1], "q2ctf2") == 0);
  qa_q2_ctf_destroy(ctf);
}

/* ---------------- LMCTF ---------------- */
static void test_lmctf_runtime(void) {
  struct fixture fx;
  fixture_init(&fx, 4);
  qa_lmctf_rules rules;
  qa_create_lmctf_rules(&rules);
  free(rules.ref_password);
  rules.ref_password = strdup("test-ref");
  rules.auto_lock = true;
  static const char *const map_names[1] = {"lmctf09"};
  rules.map_list = malloc(sizeof map_names);
  memcpy(rules.map_list, map_names, sizeof map_names);
  rules.map_count = 1;
  qa_q2_lmctf *lmctf = qa_q2_lmctf_create(lmctf_hooks(&fx), &rules, NULL, NULL);
  CHECK(lmctf != NULL);
  qa_lmctf_rules_destroy(&rules);
  fx.modules[0] = qa_q2_lmctf_as_spawn(lmctf);
  fx.modules[1] = qa_q2_item_module_as_spawn(fx.items);
  fixture_start_services(&fx, fx.modules, 2);
  for (int i = 0; i < 4; i++) {
    qa_q2_entity *entity = qa_q2_entity_services_attach_player(fx.services, fx.owned[i]);
    entity->classname = "player";
    qa_q2_lmctf_admitted(lmctf, entity, fx.game);
  }
  qa_q2_entity *first = fixture_player_entity(&fx, 0);
  const char *const team_red[1] = {"red"};
  const char *const team_blue[1] = {"blue"};
  CHECK(qa_q2_lmctf_command(lmctf, first, fx.game, "team", team_red, 1));
  CHECK(qa_q2_lmctf_command(lmctf, fixture_player_entity(&fx, 1), fx.game, "team", team_blue, 1));
  const char *const match_args[1] = {"lmctf09"};
  CHECK(qa_q2_lmctf_command(lmctf, first, fx.game, "match", match_args, 1));
  CHECK(qa_lmctf_match_pending_map(qa_q2_lmctf_get_match(lmctf)) == NULL);
  const char *const ref_args[1] = {"test-ref"};
  CHECK(qa_q2_lmctf_command(lmctf, first, fx.game, "referee", ref_args, 1));
  qa_lmctf_player_state *state = qa_lmctf_player(qa_q2_lmctf_get_context(lmctf), first->actor.id);
  CHECK(((int)state->extra_flags & 2) == 2);
  const char *const bad_map[1] = {"not-a-map"};
  CHECK(qa_q2_lmctf_command(lmctf, first, fx.game, "match", bad_map, 1));
  CHECK(qa_lmctf_match_pending_map(qa_q2_lmctf_get_match(lmctf)) == NULL);
  qa_lmctf_player_state_set_stat(state, "score", 123);
  CHECK(qa_q2_lmctf_command(lmctf, first, fx.game, "match", match_args, 1));
  const qa_lmctf_map_change *pending = qa_lmctf_match_pending_map(qa_q2_lmctf_get_match(lmctf));
  CHECK(pending != NULL && strcmp(pending->map, "lmctf09") == 0 && pending->countdown);
  CHECK(qa_lmctf_player_state_stat(state, "score", 0) == 0);

  qa_lmctf_travel travel;
  qa_q2_lmctf_capture_travel(lmctf, &travel);
  CHECK(travel.countdown);
  CHECK(travel.player_count == 4);
  /* The next source registers its items again, so it gets a fresh
   * item module like the TS composition does. */
  qa_q2_item_hooks item_hooks2 = {0};
  item_hooks2.weapon_picked = item_hook_noop;
  item_hooks2.silencer = item_silencer;
  item_hooks2.power_armor = item_power;
  qa_lmctf_hooks next_hooks = lmctf_hooks(&fx);
  next_hooks.items = qa_q2_item_module_create(item_hooks2);
  CHECK(next_hooks.items != NULL);
  qa_q2_lmctf *next = qa_q2_lmctf_create(next_hooks, &travel.rules, &travel, NULL);
  CHECK(next != NULL);
  qa_lmctf_match_checkpoint capture = qa_lmctf_match_capture(qa_q2_lmctf_get_match(next));
  CHECK(!capture.has_pending_map);
  /* A fresh source gets fresh services, as in the TS application. */
  fx.modules[0] = qa_q2_lmctf_as_spawn(next);
  fx.modules[1] = qa_q2_item_module_as_spawn(next_hooks.items);
  fixture_start_services(&fx, fx.modules, 2);
  qa_q2_lmctf_post_spawn(next, fx.game);
  capture = qa_lmctf_match_capture(qa_q2_lmctf_get_match(next));
  CHECK(strcmp(capture.phase, "countdown") == 0);
  CHECK(capture.remaining == 15);
  CHECK(capture.teams_locked);
  qa_lmctf_travel_destroy(&travel);
  qa_q2_lmctf_destroy(next);
  qa_q2_lmctf_destroy(lmctf);
}

static void test_lmctf_vote(void) {
  struct fixture fx;
  fixture_init(&fx, 4);
  qa_q2_lmctf *lmctf = qa_q2_lmctf_create(lmctf_hooks(&fx), NULL, NULL, NULL);
  CHECK(lmctf != NULL);
  fx.modules[0] = qa_q2_lmctf_as_spawn(lmctf);
  fx.modules[1] = qa_q2_item_module_as_spawn(fx.items);
  fixture_start_services(&fx, fx.modules, 2);
  for (int i = 0; i < 4; i++) {
    qa_q2_entity *entity = qa_q2_entity_services_attach_player(fx.services, fx.owned[i]);
    entity->classname = "player";
    qa_q2_lmctf_admitted(lmctf, entity, fx.game);
  }
  qa_lmctf_vote *vote = qa_q2_lmctf_get_vote(lmctf);
  qa_lmctf_vote_start(vote, fx.owned[0].id, fx.game);
  double started = 0;
  CHECK(qa_lmctf_vote_started_at(vote, &started));
  qa_lmctf_vote_ballot(vote, fx.owned[1].id, fx.game, true);
  qa_lmctf_vote_ballot(vote, fx.owned[2].id, fx.game, true);
  qa_lmctf_vote_ballot(vote, fx.owned[3].id, fx.game, false);
  fx.now = 31;
  qa_lmctf_vote_frame(vote, fx.game);
  CHECK(!qa_lmctf_vote_started_at(vote, &started));
  CHECK(fx.destination_count == 1); /* 3 of 4 = 75% passes -> endLevel(null) */
  qa_q2_lmctf_destroy(lmctf);
}

int main(void) {
  test_rules_defaults();
  test_horde();
  test_ctf_flags();
  test_ctf_match_and_checkpoint();
  test_lmctf_runtime();
  test_lmctf_vote();
  if (failures) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok %d checks\n", checks);
  return 0;
}


