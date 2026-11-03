/*
 * C checks for the 2026-11-02 TS drift ported into src/content/q1/base and
 * src/content/q1/quakec. Expected values come from bun runs of the TS
 * (scripts kept outside the tree, in the porter's scratch area):
 *   base_oracle.ts      world scenarios on a fixture equal to
 *                       tests/content/q1_world_fixture.h (open trace,
 *                       random() 0.4 unless stated, rerelease skill 1).
 *   obituary_oracle.ts  q1Obituary variants/log and classicObituaryText.
 *   env_oracle.ts       Id1Environment over a structural NetQuake program
 *                       with a foreign target (foreignReference).
 * Each constant below names the oracle line that printed it. Every callback
 * registered here checks a sentinel ctx. GPL-2.0-or-later.
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

#include "q1_world_fixture.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/messages.h"
#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q1/base/player.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/precache.h"
#include "qa/content/q1/quakec/id1_environment.h"
#include "qa/foundation/js.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);          \
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
/* bun prints float64 values with round-trip digits; binary32-derived values match exactly. */
static bool near_value(double a, double b) { return fabs(a - b) <= 1e-9 * (fabs(b) > 1 ? fabs(b) : 1); }
#define NEAR(a, b) near_value((double)(a), (double)(b))
#define VEC_IS(v, X, Y, Z) (NEAR((v).x, (X)) && NEAR((v).y, (Y)) && NEAR((v).z, (Z)))

static int sentinel;

/* ---------- world ---------- */
typedef struct sync_world {
  q1_world_fixture fx;
  qa_q1_campaign_state campaign;
  qa_q1_base *base;
  qa_owned_actor player;
  /* damage_allowed probe (TS GameplayHooks.damageAllowed) */
  int *probe_ctx;
  const char *weapons[8]; size_t weapon_count;
} sync_world;

static void world_create_options(sync_world *w, qa_q1_options options) {
  memset(w, 0, sizeof *w);
  CHECK(q1_world_fixture_create(&w->fx, options));
  qa_q1_campaign_state_init(&w->campaign, 0, 1);
  qa_q1_base_options base_options;
  memset(&base_options, 0, sizeof base_options);
  base_options.campaign = &w->campaign.binding;
  w->base = qa_q1_register_base(w->fx.game, &base_options);
  CHECK(w->base != NULL);
  w->player = q1_world_fixture_admit_player(&w->fx, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(w->fx.game, w->player, "shotgun", false, NULL);
}
static void world_create(sync_world *w) { world_create_options(w, q1_world_fixture_options()); }
static void world_destroy(sync_world *w) {
  CHECK(w->fx.ctx_errors == 0);
  qa_q1_base_destroy(w->base);
  q1_world_fixture_destroy(&w->fx);
}
static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static void set_origin(sync_world *w, qa_q1_actor *entity, qa_vec3 origin) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  qa_q1_set_body(w->fx.game, entity, &patch);
}
static qa_q1_base_monster *monster_of(sync_world *w, const char *classname, qa_vec3 origin) {
  qa_q1_actor *entity = qa_q1_create(w->fx.game, classname, NULL, NULL);
  set_origin(w, entity, origin);
  qa_q1_spawn_entity(w->fx.game, entity);
  qa_q1_base_monster *monster = qa_q1_creatures_monsters_get(qa_q1_base_creatures(w->base), entity->actor);
  CHECK(monster != NULL);
  return monster;
}
static size_t find_all(sync_world *w, const char *classname, qa_q1_actor **out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < w->fx.game->entity_count && count < capacity; i++)
    if (strcmp(w->fx.game->entities[i]->classname, classname) == 0) out[count++] = w->fx.game->entities[i];
  return count;
}
static qa_vec3 body_origin(sync_world *w, qa_q1_actor *entity) { return qa_q1_get_body(w->fx.game, entity).origin; }
static qa_vec3 body_velocity(sync_world *w, qa_q1_actor *entity) { return qa_q1_get_body(w->fx.game, entity).velocity; }
static double health_of(sync_world *w, qa_actor_id actor) { return qa_q1_health(w->fx.game, actor); }
static bool nightmare_on(void *ctx) { CHECK(ctx == &sentinel); return true; }
static bool nightmare_off(void *ctx) { CHECK(ctx == &sentinel); return false; }
static void set_nightmare(sync_world *w, bool (*fn)(void *ctx)) {
  w->fx.game->source_bloody_nightmare.ctx = &sentinel;
  w->fx.game->source_bloody_nightmare.fn = fn;
}
static void set_map_name(qa_q1_services *game, const char *name) {
  const size_t length = strlen(name) + 1;
  char *copy = malloc(length);
  CHECK(copy != NULL);
  if (copy == NULL) return;
  memcpy(copy, name, length);
  free(game->map_name);
  game->map_name = copy;
}
static size_t event_list(sync_world *w, size_t from, char out[][64], size_t capacity) {
  size_t count = 0;
  for (size_t i = from; i < w->fx.event_count && count < capacity; i++) {
    const qa_q1_event *event = &w->fx.events[i];
    const char *detail = strcmp(event->kind, "sound") == 0 ? event->path : strcmp(event->kind, "effect") == 0 ? event->effect : "";
    (void)snprintf(out[count++], 64, "%s:%s", event->kind, detail != NULL ? detail : "");
  }
  return count;
}

/* ---------- messages / obituaries (obituary_oracle.ts) ---------- */
static double rolls[4]; static size_t roll_index;
static double roll(void *ctx) { CHECK(ctx == &sentinel); return roll_index < 4 ? rolls[roll_index++] : 0.9; }
static qa_actor_id id_of(int64_t slot) { qa_actor_id id; memset(&id, 0, sizeof id); id.slot = slot; id.generation = 1; return id; }
static qa_q1_obituary_actor person(int64_t slot, const char *name, const char *classname, const char *weapon) {
  qa_q1_obituary_actor p;
  memset(&p, 0, sizeof p);
  p.actor = id_of(slot); p.name = name; p.classname = classname; p.weapon = weapon;
  p.is_player = strcmp(classname, "player") == 0; p.is_monster = strncmp(classname, "monster_", 8) == 0;
  p.health = -10; p.water_type = "empty"; p.kill_string = "";
  return p;
}
static qa_q1_obituary obit(const char *edition, const char *variant, qa_q1_obituary_actor victim,
                           const qa_q1_obituary_actor *attacker, const qa_q1_obituary_actor *owner,
                           double teamplay, const char *death_type, double r0, double r1) {
  qa_q1_obituary_input input;
  memset(&input, 0, sizeof input);
  input.edition = edition; input.variant = variant; input.victim = victim; input.attacker = attacker;
  input.telefrag_owner = owner; input.teamplay = teamplay; input.death_type = death_type;
  input.random = roll; input.random_ctx = &sentinel;
  rolls[0] = r0; rolls[1] = r1; rolls[2] = rolls[3] = 0.9; roll_index = 0;
  return qa_q1_obituary_compute(&input);
}
static bool log_is(const qa_q1_obituary *out, const char *what, const qa_actor_id *versus) {
  return out->has_log && strcmp(out->log_what, what) == 0 && out->log_has_versus == (versus != NULL) &&
         (versus == NULL || qa_same_actor(out->log_versus, *versus));
}
static void test_messages_and_obituaries(void) {
  char text[128];
  const char *args[2] = {"Vic", "Att"};
  /* hook1 "Vic was hooked by Att\n"; changed "Vic changed teams\n" */
  CHECK(strcmp(qa_q1_classic_obituary_text("$qc_death_hook1", args, 2, text, sizeof text), "Vic was hooked by Att\n") == 0);
  CHECK(strcmp(qa_q1_classic_obituary_text("$qc_ks_changed_teams", args, 1, text, sizeof text), "Vic changed teams\n") == 0);
  const qa_actor_id a = id_of(2);
  qa_q1_obituary_actor victim = person(1, "Vic", "player", "rocketlauncher");
  qa_q1_obituary out;
  /* ctf.teamchange: $qc_ks_tried_change_teams, -1, log teamchange */
  out = obit("rerelease", "ctf", victim, &victim, NULL, 64, "ctf:teamchange", 0.4, 0);
  CHECK(out.has_message && strcmp(out.text, "$qc_ks_tried_change_teams") == 0 && out.score_delta == -1 && log_is(&out, "teamchange", NULL));
  /* ctf.teamchange.classic: "Vic changed teams\n" */
  out = obit("classic", "ctf", victim, &victim, NULL, 0, "ctf:teamchange", 0.4, 0);
  CHECK(out.has_message && strcmp(out.text, "Vic changed teams\n") == 0 && log_is(&out, "teamchange", NULL));
  /* ctf.teamchange.novariant: $qc_suicide_bored, log rocket */
  out = obit("rerelease", NULL, victim, &victim, NULL, 0, "ctf:teamchange", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_suicide_bored") == 0 && log_is(&out, "rocket", NULL));
  /* suicide.grenade: $qc_suicide_pin, log grenade */
  qa_q1_obituary_actor grenadier = victim; grenadier.weapon = "grenadelauncher";
  out = obit("rerelease", NULL, grenadier, &grenadier, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_suicide_pin") == 0 && log_is(&out, "grenade", NULL));
  /* grapple (classic, rolls 0.4, 0.7): "Vic was hooked by Att\n", +1, log hook vs attacker */
  qa_q1_obituary_actor hooker = person(2, "Att", "player", "ctf:grapple");
  out = obit("classic", NULL, victim, &hooker, NULL, 0, "", 0.4, 0.7);
  CHECK(strcmp(out.text, "Vic was hooked by Att\n") == 0 && out.score_delta == 1 && qa_same_actor(out.score_actor, a) && log_is(&out, "hook", &a));
  /* grapple.rerelease (0.4, 0.2): $qc_death_hook1 args [Vic, Att] */
  out = obit("rerelease", NULL, victim, &hooker, NULL, 0, "", 0.4, 0.2);
  CHECK(strcmp(out.text, "$qc_death_hook1") == 0 && out.argument_count == 2 && strcmp(out.arguments[1], "Att") == 0);
  /* rocket.quad.mg: $qc_death_rl2; rocket.quad: $qc_death_rl_quad1 */
  qa_q1_obituary_actor gibbed = victim; gibbed.health = -50;
  qa_q1_obituary_actor rocketeer = person(2, "Att", "player", "rocketlauncher"); rocketeer.quad_expires = 5;
  out = obit("rerelease", "mg", gibbed, &rocketeer, NULL, 0, "", 0.4, 0.1);
  CHECK(strcmp(out.text, "$qc_death_rl2") == 0 && log_is(&out, "rocket", &a));
  out = obit("rerelease", NULL, gibbed, &rocketeer, NULL, 0, "", 0.4, 0.1);
  CHECK(strcmp(out.text, "$qc_death_rl_quad1") == 0);
  /* ff.log: $qc_ff_glasses, args [Att], credited attacker -1, log nailgun vs attacker */
  qa_q1_obituary_actor teammate = victim; teammate.team = 3;
  qa_q1_obituary_actor nailer = person(2, "Att", "player", "nailgun"); nailer.team = 3;
  out = obit("rerelease", NULL, teammate, &nailer, NULL, 2, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_ff_glasses") == 0 && out.score_delta == -1 && qa_same_actor(out.score_actor, a) && log_is(&out, "nailgun", &a));
  /* mg.monster: $qc_ks_ogre, no log; .aflag: $mg3_qc_obit_rocketogre; .aflag.fallback: $qc_ks_knight; .unknown: no message */
  qa_q1_obituary_actor ogre = person(2, "", "monster_ogre", NULL);
  out = obit("rerelease", "mg", victim, &ogre, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_ks_ogre") == 0 && out.score_delta == -1 && !out.has_log);
  ogre.has_aflag = true; ogre.aflag = 2;
  out = obit("rerelease", "mg", victim, &ogre, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$mg3_qc_obit_rocketogre") == 0 && !out.has_log);
  qa_q1_obituary_actor knight = person(2, "", "monster_knight", NULL); knight.has_aflag = true; knight.aflag = 2;
  out = obit("rerelease", "mg", victim, &knight, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_ks_knight") == 0);
  qa_q1_obituary_actor boss = person(2, "", "monster_boss", NULL);
  out = obit("rerelease", "mg", victim, &boss, NULL, 0, "", 0.4, 0);
  CHECK(!out.has_message && out.has_score && out.score_delta == -1 && !out.has_log);
  /* mg.hazard (victim in water): $qc_ks_spiked; mg.brush: $qc_death_squish; mg.other (slime, 0.4 0.3): $qc_death_slime1 log slimed */
  qa_q1_obituary_actor wet = victim; wet.water_type = "water";
  qa_q1_obituary_actor shooter = person(2, "", "trap_spikeshooter", NULL);
  out = obit("rerelease", "mg", wet, &shooter, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_ks_spiked") == 0 && !out.has_log);
  qa_q1_obituary_actor door = person(2, "", "func_door", NULL); door.brush = true;
  out = obit("rerelease", "mg", victim, &door, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_death_squish") == 0 && !out.has_log);
  qa_q1_obituary_actor slimy = victim; slimy.water_type = "slime"; door.brush = false;
  out = obit("rerelease", "mg", slimy, &door, NULL, 0, "", 0.4, 0.3);
  CHECK(strcmp(out.text, "$qc_death_slime1") == 0 && log_is(&out, "slimed", NULL));
  /* classic.monster.log: "Vic was exploded by a Vore\n" log vore; .exit: noexit; .fireball: fireball */
  qa_q1_obituary_actor vore = person(2, "", "monster_shalrath", NULL);
  out = obit("classic", NULL, victim, &vore, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "Vic was exploded by a Vore\n") == 0 && log_is(&out, "vore", NULL));
  qa_q1_obituary_actor exit_trigger = person(2, "", "trigger_changelevel", NULL);
  out = obit("classic", NULL, victim, &exit_trigger, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "Vic tried to leave\n") == 0 && log_is(&out, "noexit", NULL));
  qa_q1_obituary_actor fireball = person(2, "", "misc_fireball", NULL);
  out = obit("classic", NULL, victim, &fireball, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "Vic ate a lavaball\n") == 0 && log_is(&out, "fireball", NULL));
  /* lava.log: $qc_death_lava1 melted; fall.log: $qc_death_fall falling */
  qa_q1_obituary_actor burnt = victim; burnt.water_type = "lava"; burnt.health = -20;
  out = obit("rerelease", NULL, burnt, NULL, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_death_lava1") == 0 && log_is(&out, "melted", NULL));
  out = obit("rerelease", NULL, victim, NULL, NULL, 0, "falling", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_death_fall") == 0 && log_is(&out, "falling", NULL));
  /* telefrag.log: credited owner +1, log telefrag vs owner */
  qa_q1_obituary_actor teledeath = person(3, "", "teledeath", NULL), owner = person(2, "Att", "player", NULL);
  out = obit("rerelease", NULL, victim, &teledeath, &owner, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_telefragged") == 0 && out.score_delta == 1 && log_is(&out, "telefrag", &a));
  /* killstring.log: $qc_ks_grunt log died */
  qa_q1_obituary_actor grunt = person(2, "", "monster_army", NULL); grunt.kill_string = "$qc_ks_grunt";
  out = obit("rerelease", NULL, victim, &grunt, NULL, 0, "", 0.4, 0);
  CHECK(strcmp(out.text, "$qc_ks_grunt") == 0 && log_is(&out, "died", NULL));
  /* nonplayer: no message, score or log */
  out = obit("rerelease", NULL, person(1, "", "monster_army", NULL), NULL, NULL, 0, "", 0.4, 0);
  CHECK(!out.has_message && !out.has_score && !out.has_log);
}

/* ---------- projectiles (base_oracle.ts) ---------- */
static void test_projectiles(void) {
  sync_world w;
  world_create(&w);
  qa_q1_services *game = w.fx.game;
  /* backpack.movementFlags 256, backpack.movement "toss" */
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.cells = 5;
  qa_q1_actor *pack = qa_q1_drop_backpack(game, v3(10, 20, 30), &contents, NULL);
  CHECK(pack != NULL && pack->movement_flags == 256 && strcmp(pack->movement, "toss") == 0);
  /* wizard2: classname "wizard_spike", model "progs/w_spike2.mdl", projectile "spike" */
  qa_q1_actor *spike = qa_q1_launch_spike(game, &w.player.id, v3(0, 0, 0), v3(600, 0, 0), "wizard2");
  CHECK(strcmp(spike->classname, "wizard_spike") == 0 && strcmp(spike->model, "progs/w_spike2.mdl") == 0 &&
        strcmp(spike->projectile, "spike") == 0);
  world_destroy(&w);

  /* laser.effects 8, laserAlt.effects 0; alt touch events [effect:blood], health 85;
   * laser touch events [sound:enforcer/enfstop.wav, effect:blood], health 70 */
  world_create(&w);
  game = w.fx.game;
  qa_owned_actor target = q1_world_fixture_admit_player(&w.fx, v3(100, 0, 0), 100);
  qa_q1_actor *laser = qa_q1_launch_laser(game, NULL, v3(0, 0, 0), v3(1, 0, 0));
  qa_q1_actor *alt = qa_q1_launch_laser_alt(game, NULL, v3(0, 0, 0), v3(1, 0, 0));
  CHECK(laser->effects == 8 && alt->effects == 0);
  CHECK(strcmp(alt->classname, "enforcer_laser") == 0 && strcmp(alt->model, "progs/laser.mdl") == 0);
  CHECK(alt->touch.fn != NULL && strcmp(qa_q1_touch_name(alt->touch), "base:laser_alt_touch") == 0);
  char events[8][64];
  size_t from = w.fx.event_count;
  alt->touch.fn(alt->touch.ctx, game, alt, target.id, NULL, NULL);
  size_t count = event_list(&w, from, events, 8);
  CHECK(count == 1 && strcmp(events[0], "effect:blood") == 0);
  CHECK(health_of(&w, target.id) == 85);
  from = w.fx.event_count;
  laser->touch.fn(laser->touch.ctx, game, laser, target.id, NULL, NULL);
  count = event_list(&w, from, events, 8);
  CHECK(count == 2 && strcmp(events[0], "sound:enforcer/enfstop.wav") == 0 && strcmp(events[1], "effect:blood") == 0);
  CHECK(health_of(&w, target.id) == 70);
  world_destroy(&w);
}

/* castLightning: lightning.weapons ["q1:weapon/lightning"], lightning.health 90 */
static qa_actor_id lightning_target;
static qa_q1_trace lightning_trace(void *ctx, qa_q1_trace_request request) {
  (void)q1_world_fixture_of(ctx);
  qa_q1_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = request.monsters ? 0.5 : 1;
  trace.end = request.end;
  trace.in_open = true;
  trace.has_actor = request.monsters;
  if (request.monsters) trace.actor = lightning_target;
  return trace;
}
static bool record_weapon(void *ctx, qa_damage_request request) {
  sync_world *w = ctx;
  CHECK(w->probe_ctx == &sentinel);
  if (w->weapon_count < 8) w->weapons[w->weapon_count++] = request.attack.weapon;
  return true;
}
static void test_lightning(void) {
  sync_world w;
  world_create(&w);
  w.probe_ctx = &sentinel;
  qa_q1_base_monster *shambler = monster_of(&w, "monster_shambler", v3(0, 0, 0));
  qa_owned_actor target = q1_world_fixture_admit_player(&w.fx, v3(200, 0, 0), 100);
  lightning_target = target.id;
  qa_q1_base_monster_set_enemy(shambler, &target.id);
  w.fx.game->host.trace = lightning_trace;
  w.fx.damage_allowed = record_weapon;
  w.fx.damage_allowed_ctx = &w;
  qa_q1_cast_lightning(shambler);
  CHECK(w.weapon_count == 1 && w.weapons[0] != NULL && strcmp(w.weapons[0], "q1:weapon/lightning") == 0);
  CHECK(health_of(&w, target.id) == 90);
  world_destroy(&w);
}

/* ogre grenade: far {632, -1.9594349641362686e-15, 325}, near {..., 112.5},
 * mid {..., 200}, plain (no nightmare) {600, 0, 200}; random 0.75 for the nightmare runs. */
static void ogre_case(bool nightmare, qa_vec3 enemy_origin, qa_vec3 expected) {
  sync_world w;
  world_create(&w);
  if (nightmare) set_nightmare(&w, nightmare_on);
  qa_q1_base_monster *ogre = monster_of(&w, "monster_ogre", v3(0, 0, 0));
  (void)qa_q1_actor_field_set(ogre->entity, "projectiles", "1");
  if (nightmare) {
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_angles = true;
    patch.angles = v3(0, 90, 0);
    qa_q1_set_body(w.fx.game, ogre->entity, &patch);
  }
  qa_owned_actor enemy = q1_world_fixture_admit_player(&w.fx, enemy_origin, 100);
  qa_q1_base_monster_set_enemy(ogre, &enemy.id);
  if (nightmare) w.fx.random_value = 0.75;
  qa_q1_launch_ogre_grenade(ogre);
  qa_q1_actor *grenades[2];
  CHECK(find_all(&w, "ogre_grenade", grenades, 2) == 1);
  const qa_vec3 velocity = body_velocity(&w, grenades[0]);
  CHECK(VEC_IS(velocity, expected.x, expected.y, expected.z));
  if (!VEC_IS(velocity, expected.x, expected.y, expected.z))
    fprintf(stderr, "  ogre velocity %.17g %.17g %.17g\n", velocity.x, velocity.y, velocity.z);
  world_destroy(&w);
}
static void test_ogre_grenade(void) {
  ogre_case(true, v3(600, 0, 0), v3(632, -1.9594349641362686e-15, 325));
  ogre_case(true, v3(50, 0, 0), v3(632, -1.9594349641362686e-15, 112.5));
  ogre_case(true, v3(300, 0, 0), v3(632, -1.9594349641362686e-15, 200));
  ogre_case(false, v3(600, 0, 0), v3(600, 0, 200));
}

/* enforcer: enf.lasers 2, ammo_shells "1", alt velocity {559.7401733398438, 216.08096313476562, 0},
 * alt effects 0; second volley ammo_shells "0", alt2 velocity {577.6840209960938, 162.11471557617188, 0} */
static void test_enforcer(void) {
  sync_world w;
  world_create(&w);
  set_nightmare(&w, nightmare_on);
  qa_q1_base_monster *enforcer = monster_of(&w, "monster_enforcer", v3(0, 0, 0));
  qa_owned_actor enemy = q1_world_fixture_admit_player(&w.fx, v3(300, 100, 0), 100);
  qa_q1_base_monster_set_enemy(enforcer, &enemy.id);
  qa_q1_monster_action(enforcer, "enf_atk6");
  qa_q1_actor *lasers[4];
  CHECK(find_all(&w, "enforcer_laser", lasers, 4) == 2);
  const char *shells = qa_q1_actor_field_get(enforcer->entity, "ammo_shells");
  CHECK(shells != NULL && strcmp(shells, "1") == 0);
  CHECK(VEC_IS(body_velocity(&w, lasers[1]), 559.7401733398438, 216.08096313476562, 0));
  CHECK(lasers[1]->effects == 0 && lasers[0]->effects == 8);
  qa_q1_monster_action(enforcer, "enf_atk10");
  CHECK(find_all(&w, "enforcer_laser", lasers, 4) == 4);
  shells = qa_q1_actor_field_get(enforcer->entity, "ammo_shells");
  CHECK(shells != NULL && strcmp(shells, "0") == 0);
  CHECK(VEC_IS(body_velocity(&w, lasers[3]), 577.6840209960938, 162.11471557617188, 0));
  world_destroy(&w);
}

/* hell knight: plain origin {20,0,8} velocity {300,0,-3}; infected + nightmare origin {20,0,4} velocity {500,0,-5} */
static void hknight_case(bool infected, qa_vec3 origin, qa_vec3 velocity) {
  sync_world w;
  world_create(&w);
  if (infected) set_nightmare(&w, nightmare_on);
  qa_q1_base_monster *knight = monster_of(&w, "monster_hell_knight", v3(0, 0, 0));
  if (infected) (void)qa_q1_actor_field_set(knight->entity, "infected", "1");
  qa_owned_actor enemy = q1_world_fixture_admit_player(&w.fx, v3(300, 0, 0), 100);
  qa_q1_base_monster_set_enemy(knight, &enemy.id);
  qa_q1_monster_action(knight, "hknight_magica9");
  qa_q1_actor *spikes[2];
  CHECK(find_all(&w, "knight_spike", spikes, 2) == 1);
  CHECK(VEC_IS(body_origin(&w, spikes[0]), origin.x, origin.y, origin.z));
  CHECK(VEC_IS(body_velocity(&w, spikes[0]), velocity.x, velocity.y, velocity.z));
  world_destroy(&w);
}

/* wizard fast fire under the nightmare: wiz.count 2, models [w_spike, w_spike2],
 * second velocity {572.1441040039062, -46.05557632446289, -33.98576736450195} */
static void test_wizard(void) {
  sync_world w;
  world_create(&w);
  qa_q1_services *game = w.fx.game;
  set_nightmare(&w, nightmare_on);
  qa_q1_base_monster *wizard = monster_of(&w, "monster_wizard", v3(0, 0, 0));
  qa_owned_actor enemy = q1_world_fixture_admit_player(&w.fx, v3(400, 0, 0), 100);
  qa_q1_base_monster_set_enemy(wizard, &enemy.id);
  qa_q1_actor *timer = qa_q1_create(game, "wizard_fastfire", NULL, NULL);
  timer->has_owner = true;
  timer->owner = wizard->entity->actor.id;
  qa_q1_set_origin(game, timer, v3(0, 14, 30));
  const qa_q1_wizard_shot shot = {enemy.id, v3(0, -1, 0)};
  qa_q1_creatures_shots_set(qa_q1_base_creatures(w.base), timer->actor, shot);
  qa_q1_wizard_fast_fire(game, timer);
  qa_q1_actor *spikes[4];
  CHECK(find_all(&w, "wizard_spike", spikes, 4) == 2);
  CHECK(strcmp(spikes[0]->model, "progs/w_spike.mdl") == 0 && strcmp(spikes[1]->model, "progs/w_spike2.mdl") == 0);
  CHECK(VEC_IS(body_velocity(&w, spikes[1]), 572.1441040039062, -46.05557632446289, -33.98576736450195));
  world_destroy(&w);
}

/* demon jump touch: ungated health 56 (attack_finished 100 kept); item-start program with
 * attack_finished 100: health 100; with attack_finished -1: health 56, attack_finished 0.5 (time 0) */
static bool item_start(void *ctx, qa_q1_actor *entity, const char **place, const char **use) {
  (void)entity; (void)place; (void)use;
  CHECK(ctx == &sentinel);
  return false;
}
static void demon_case(bool gate, double finished, double health, double finished_after) {
  sync_world w;
  world_create(&w);
  if (gate) { w.fx.game->source_item_start.ctx = &sentinel; w.fx.game->source_item_start.fn = item_start; }
  qa_q1_base_monster *demon = monster_of(&w, "monster_demon1", v3(0, 0, 0));
  qa_owned_actor victim = q1_world_fixture_admit_player(&w.fx, v3(40, 0, 0), 100);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true;
  patch.velocity = v3(500, 0, 0);
  qa_q1_set_body(w.fx.game, demon->entity, &patch);
  demon->state->attack_finished = finished;
  qa_q1_monster_jump_touch(demon, victim.id);
  CHECK(health_of(&w, victim.id) == health);
  CHECK(demon->state->attack_finished == finished_after);
  CHECK(w.fx.game->time == 0);
  world_destroy(&w);
}

/* species: precache.plain [wizard, h_wizard, w_spike]; with a nightmare hook (returning false)
 * [wizard, h_wizard, w_spike2, w_spike] */
static void precache_case(bool hook, const char *const *expected, size_t expected_count) {
  sync_world w;
  world_create(&w);
  if (hook) set_nightmare(&w, nightmare_off);
  size_t before = 0, after = 0;
  (void)qa_q1_precache_models(w.fx.game->precaches, &before);
  qa_q1_precache_id1_monster(w.fx.game, "wizard");
  const char *const *models = qa_q1_precache_models(w.fx.game->precaches, &after);
  CHECK(after - before == expected_count);
  for (size_t i = 0; i < expected_count && before + i < after; i++) CHECK(strcmp(models[before + i], expected[i]) == 0);
  world_destroy(&w);
}

/* spawn selector, rerelease deathmatch with no info_player_deathmatch:
 * select.unforced null; select.forced "info_player_start" (that entity) */
static void test_spawn_selector(void) {
  sync_world w;
  qa_q1_options options = q1_world_fixture_options();
  options.deathmatch = 1;
  world_create_options(&w, options);
  qa_q1_actor *start = qa_q1_create(w.fx.game, "info_player_start", NULL, NULL);
  qa_q1_spawn_selector *selector = qa_q1_spawn_selector_create(w.fx.game, qa_q1_base_campaign(w.base));
  CHECK(selector != NULL);
  CHECK(qa_q1_spawn_selector_select(selector, false) == NULL);
  CHECK(qa_q1_spawn_selector_select(selector, true) == start);
  qa_q1_spawn_selector_destroy(selector);
  world_destroy(&w);
}

/* level rules: limits.mgdm4.true -> "mgdm1"; limits.mgdm2.true -> "mgdm3"; limits.mgdm2.false -> "e1m1" */
static void limits_case(const char *map, bool fix, const char *expected) {
  sync_world w;
  qa_q1_options options = q1_world_fixture_options();
  options.deathmatch = 1;
  world_create_options(&w, options);
  set_map_name(w.fx.game, map);
  w.fx.game->source_grenade_fix = fix;
  qa_q1_actor *change = qa_q1_create(w.fx.game, "trigger_changelevel", NULL, NULL);
  (void)qa_q1_actor_field_set(change, "map", "e1m1");
  qa_q1_level_rules *rules = qa_q1_base_level_rules(w.base);
  const double scores[1] = {5};
  CHECK(qa_q1_level_rules_check_limits(rules, 10, scores, 1, 0, 5));
  qa_save_value *captured = qa_q1_level_rules_capture(rules);
  qa_save_reader reader, field;
  CHECK(captured != NULL && qa_save_reader_init(&reader, captured, "rules"));
  CHECK(qa_save_reader_field(&reader, "nextMap", &field));
  CHECK(strcmp(qa_save_reader_string(&field), expected) == 0);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  world_destroy(&w);
}

/* BaseMonster sfx/gibDeath virtual dispatch (derived.* oracle lines):
 * first: pain [statue/khurt.wav]; die [statue/kdeath.wav], frame knight_die1, model progs/knight.mdl, gibCalls 1
 * second (override defers to super): die [player/udeath.wav], frame knight_pain1, model progs/h_knight.mdl, gibCalls 2 */
typedef struct statue_monster {
  qa_q1_base_monster base; /* first: TS `extends BaseMonster` */
  int *ctx_check;
  int gib_calls;
  char path[64];
} statue_monster;
static statue_monster *statue_of(qa_q1_base_monster *monster) {
  statue_monster *statue = (statue_monster *)monster;
  CHECK(statue->ctx_check == &sentinel);
  return statue;
}
static const char *statue_sfx(qa_q1_base_monster *monster, const char *path) {
  statue_monster *statue = statue_of(monster);
  if (strncmp(path, "knight/", 7) != 0) return path;
  (void)snprintf(statue->path, sizeof statue->path, "statue/%s", path + 7);
  return statue->path;
}
static bool statue_gib_death(qa_q1_base_monster *monster) {
  statue_monster *statue = statue_of(monster);
  statue->gib_calls++;
  return statue->gib_calls > 1 ? qa_q1_base_monster_gib_death_super(monster) : false;
}
static void statue_destroy(qa_q1_base_monster *monster) { free(statue_of(monster)); }
static const qa_q1_base_monster_ops STATUE_OPS = {.sfx = statue_sfx, .gib_death = statue_gib_death, .destroy = statue_destroy};
static void statue_case(bool second) {
  sync_world w;
  world_create(&w);
  qa_q1_services *game = w.fx.game;
  qa_q1_actor *entity = qa_q1_create(game, "monster_knight", NULL, NULL);
  const qa_q1_base_monster_species *knight = NULL;
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
    if (strcmp(QA_Q1_BASE_SPECIES[i].species, "knight") == 0) knight = &QA_Q1_BASE_SPECIES[i];
  statue_monster *statue = calloc(1, sizeof *statue);
  CHECK(knight != NULL && statue != NULL);
  if (knight == NULL || statue == NULL) { free(statue); world_destroy(&w); return; }
  CHECK(qa_q1_base_monster_init(&statue->base, game, entity, knight,
                                qa_q1_creatures_as_services(qa_q1_base_creatures(w.base)), NULL));
  statue->base.ops = &STATUE_OPS;
  statue->ctx_check = &sentinel;
  qa_q1_creatures_monsters_set(qa_q1_base_creatures(w.base), entity->actor, &statue->base);
  qa_q1_base_monster_spawn(&statue->base);
  if (second) statue->gib_calls = 1;
  char events[8][64];
  size_t from = w.fx.event_count;
  qa_q1_base_monster_pain(&statue->base, &w.player.id, 5);
  size_t count = event_list(&w, from, events, 8);
  CHECK(count == 1 && strcmp(events[0], "sound:statue/khurt.wav") == 0);
  qa_gameplay_authority_set_health(w.fx.combat, entity->actor, -50);
  from = w.fx.event_count;
  qa_q1_base_monster_die(&statue->base, &w.player.id);
  size_t sounds = 0;
  count = event_list(&w, from, events, 8);
  for (size_t i = 0; i < count; i++) {
    if (strncmp(events[i], "sound:", 6) != 0) continue;
    CHECK(strcmp(events[i] + 6, second ? "player/udeath.wav" : "statue/kdeath.wav") == 0);
    sounds++;
  }
  CHECK(sounds == 1);
  CHECK(strcmp(statue->base.current_frame, second ? "knight_pain1" : "knight_die1") == 0);
  CHECK(strcmp(entity->model, second ? "progs/h_knight.mdl" : "progs/knight.mdl") == 0);
  CHECK(statue->gib_calls == (second ? 2 : 1));
  world_destroy(&w);
}

/* ---------- id1 environment with a foreign target (env_oracle.ts) ----------
 * foreign {"cause":{"kind":"environment","hazard":"drown"},"time":12.5,"direction":{0,0,0},
 *          "point":{"x":1,"y":2.5,"z":-3},"knockback":0} references [1]
 * bare throws "id1 damage references an actor without a source projection"
 * stale throws "id1 environment references a stale actor" */
#define NQ_DIGEST "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580"
#define STRIDE 1024
typedef struct foreign_probe { int *ctx_check; qa_actor_id foreign; int foreign_calls, other_calls; } foreign_probe;
static int64_t foreign_reference(void *ctx, qa_actor_id actor) {
  foreign_probe *probe = ctx;
  CHECK(probe->ctx_check == &sentinel);
  if (qa_same_actor(actor, probe->foreign)) probe->foreign_calls++;
  else probe->other_calls++;
  return 3 * STRIDE;
}
static qa_q1qc_machine *machine_of(void *ctx) { return ctx; }
static void put_float(uint8_t *bytes, int64_t word, float value) { memcpy(bytes + (size_t)word * 4, &value, 4); }
static void put_int(uint8_t *bytes, int64_t word, int32_t value) { memcpy(bytes + (size_t)word * 4, &value, 4); }
static double env_num_store(void *ctx, double v) { (void)ctx; return (double)(float)v; }
static void test_environment_foreign(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "env"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  qa_q1qc_statement *statements = calloc(11000, sizeof *statements);
  const int64_t sites[9] = {6446, 6489, 6509, 6935, 10462, 8690, 9589, 10736, 10877};
  for (int i = 0; i < 9; i++) { statements[sites[i]].opcode = QA_Q1QC_OP_CALL4; statements[sites[i]].a = 520; }
  qa_q1qc_function *functions = calloc(500, sizeof *functions);
  for (size_t i = 0; i < 500; i++) { functions[i].index = (int64_t)i; functions[i].name = ""; functions[i].file = ""; }
  functions[239].name = "WaterMove"; functions[243].name = "PlayerPostThink"; functions[434].name = "hurt_touch";
  functions[375].name = "door_blocked"; functions[397].name = "secret_blocked"; functions[448].name = "plat_crush";
  functions[451].name = "train_blocked";
  const qa_q1qc_definition globals[4] = {{"entity", 0, false, 28, "self"}, {"entity", 0, false, 29, "other"},
                                         {"entity", 0, false, 30, "world"}, {"float", 0, false, 31, "time"}};
  const qa_q1qc_definition fields[2] = {{"vector", 0, false, 20, "origin"}, {"float", 0, false, 30, "movetype"}};
  uint8_t *initial = calloc(4096, 1);
  qa_q1qc_program program;
  memset(&program, 0, sizeof program);
  program.source = "test.qc"; program.api_kind = "q1-netquake";
  program.statements = statements; program.statement_count = 11000;
  program.globals = globals; program.global_count = 4;
  program.fields = fields; program.field_count = 2;
  program.functions = functions; program.function_count = 500;
  program.initial_globals = initial; program.initial_globals_length = 4096;
  program.digest = NQ_DIGEST;
  qa_q1qc_entity_memory entities = {STRIDE, 0, 256, 16, 16, calloc(16, STRIDE)};
  uint8_t *global_bytes = calloc(4096, 1);
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.program = &program;
  machine.entities = &entities;
  machine.globals = (qa_q1qc_words){global_bytes, 4096};
  machine.numeric.store = env_num_store;
  put_int(global_bytes, 520, 117);
  put_int(global_bytes, 28, 3 * STRIDE);
  put_int(global_bytes, 30, 0);
  put_float(global_bytes, 31, 12.5f);
  uint8_t *victim = entities.bytes + 3 * STRIDE;
  put_float(victim, 20, 1); put_float(victim, 21, 2.5f); put_float(victim, 22, -3);
  const qa_owned_actor world = qa_session_actor_registry_allocate_at_source(actors, "q1:official", 0, "worldspawn");
  const qa_owned_actor foreign = qa_session_actor_registry_allocate(actors, "q2:native", "player");
  foreign_probe probe = {&sentinel, foreign.id, 0, 0};
  qa_q1qc_source source;
  memset(&source, 0, sizeof source);
  source.program = &program; source.entities = &entities; source.actors = actors; source.provider = "q1:official";
  const qa_q1qc_foreign_source foreign_source = {&probe, NULL, foreign_reference};
  qa_q1_id1_environment *environment = qa_q1_id1_environment_create_foreign(source, &foreign_source, machine_of, &machine);
  qa_q1_id1_damage_call call;
  memset(&call, 0, sizeof call);
  call.call = (qa_q1qc_call_site){117, 239, 6446};
  call.target = foreign.id; call.inflictor = world.id; call.attacker = world.id; call.amount = 5;
  qa_q1_id1_environmental_damage damage;
  CHECK(qa_q1_id1_environment_resolve(environment, call, NULL, &damage));
  CHECK(damage.cause.kind == QA_ATTACK_CAUSE_ENVIRONMENT && strcmp(damage.cause.hazard, "drown") == 0);
  CHECK(damage.time == 12.5 && damage.knockback == 0);
  CHECK(VEC_IS(damage.direction, 0, 0, 0) && VEC_IS(damage.point, 1, 2.5, -3));
  CHECK(probe.foreign_calls == 1 && probe.other_calls == 0);
  /* Without foreignReference the foreign target aborts (TS QcProgramError reaches the top level). */
  qa_q1_id1_environment *bare = qa_q1_id1_environment_create(source, machine_of, &machine);
  CHECK_ABORTS((void)qa_q1_id1_environment_resolve(bare, call, NULL, &damage));
  qa_q1_id1_environment_destroy(bare);
  qa_session_actor_registry_release(actors, foreign);
  CHECK_ABORTS((void)qa_q1_id1_environment_resolve(environment, call, NULL, &damage));
  qa_q1_id1_environment_destroy(environment);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
  free(entities.bytes); free(statements); free(functions); free(initial); free(global_bytes);
}

/* Q1CharacterActor axe attack runs four frames (variant 1 from 125):
 * axe.frames [125,126,127,127,128,18] over frame(0..0.5) (binary32 time keeps 127 at 0.3). */
static void test_axe_animation(void) {
  sync_world w;
  world_create(&w);
  qa_q1_character_actor *character = qa_q1_character_actor_create(w.fx.game, w.player, NULL);
  CHECK(character != NULL);
  if (character != NULL) {
    qa_q1_character_input input;
    memset(&input, 0, sizeof input);
    input.axe_pose = true;
    input.water_type = "empty";
    (void)qa_q1_character_actor_frame(character, 0, input);
    const qa_q1_base_character_attack attack = {"axe", 1};
    qa_q1_character_actor_attack(character, attack);
    const double expected[6] = {125, 126, 127, 127, 128, 18};
    double frames[6];
    frames[0] = qa_q1_character_actor_presentation(character).frame;
    const double times[5] = {0.1, 0.2, 0.3, 0.4, 0.5};
    for (int i = 0; i < 5; i++) frames[i + 1] = qa_q1_character_actor_frame(character, times[i], input).frame;
    for (int i = 0; i < 6; i++) CHECK(frames[i] == expected[i]);
    qa_q1_character_actor_destroy(character);
  }
  world_destroy(&w);
}

/* misc_fireball: fireball.rerelease.speed 1000, fireball.classic.speed 0 (classname "fireball") */
static void fireball_case(const char *edition, double speed) {
  sync_world w;
  qa_q1_options options = q1_world_fixture_options();
  options.edition = edition;
  world_create_options(&w, options);
  qa_q1_actor *entity = qa_q1_create(w.fx.game, "misc_fireball", NULL, NULL);
  qa_q1_spawn_entity(w.fx.game, entity);
  CHECK(entity->speed == speed && strcmp(entity->classname, "fireball") == 0);
  world_destroy(&w);
}

int main(void) {
  test_messages_and_obituaries();
  test_projectiles();
  test_lightning();
  test_ogre_grenade();
  test_enforcer();
  hknight_case(false, v3(20, 0, 8), v3(300, 0, -3));
  hknight_case(true, v3(20, 0, 4), v3(500, 0, -5));
  test_wizard();
  demon_case(false, 100, 56, 100);
  demon_case(true, 100, 100, 100);
  demon_case(true, -1, 56, 0.5);
  static const char *const PLAIN[] = {"progs/wizard.mdl", "progs/h_wizard.mdl", "progs/w_spike.mdl"};
  static const char *const NIGHTMARE[] = {"progs/wizard.mdl", "progs/h_wizard.mdl", "progs/w_spike2.mdl", "progs/w_spike.mdl"};
  precache_case(false, PLAIN, 3);
  precache_case(true, NIGHTMARE, 4);
  test_spawn_selector();
  limits_case("mgdm4", true, "mgdm1");
  limits_case("mgdm2", true, "mgdm3");
  limits_case("mgdm2", false, "e1m1");
  statue_case(false);
  statue_case(true);
  test_environment_foreign();
  test_axe_animation();
  fireball_case("rerelease", 1000);
  fireball_case("classic", 0);
  printf("test_q1_sync_base: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
