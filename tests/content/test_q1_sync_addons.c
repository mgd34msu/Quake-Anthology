/*
 * Checks for the Q1 add-on TypeScript drift of 2026-11-02 (src/content/q1/addons):
 * CTF impulse 99, counter messages for a non-player activator, sourceFogPush,
 * horde corpse fading, lava man damage effects and the MG3 nightmare frames.
 * Expected values come from bun scripts over the TS tree (run 2026-11-02 in the
 * agent scratch area, oracle/ next to harness.ts, a copy of the world() harness
 * of tests/gameplay/q1-addons/common.test.ts):
 *   ctf_impulse99.ts  Q1Ctf.prototype.impulse on a stub CtfState
 *   drift.ts          counter, fog, horde and frame sections
 *   lava.ts           game.damageSourceEffects in an MG3 world
 *   frames.ts         the MG1 sourceFrames composition
 *   ring.ts           items/common.ts ringEffect (TS changed 04:53, during this sync)
 * The C side runs over the real Q1 game, Q1Base and world tables
 * (q1_addon_fixture.h, random fixed at 0.4). Every host callback checks a
 * sentinel ctx. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_addon_fixture.h"
#include "qa/content/q1/addons/monsters/ordinary/source_frames.h"
#include "qa/content/q1/base/animation.h"

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

/* --- world: game + base + registerQ1CampaignAddons + one attached player --- */
typedef struct world {
  q1_addon_fixture af;
  qa_owned_actor player;
} world;

static void open_world(world *w, qa_q1ad_program program, bool campaign) {
  memset(w, 0, sizeof *w);
  qa_q1_options options = q1_world_fixture_options();
  options.campaign = program == QA_Q1AD_MG1 ? "q1:mg1" : program == QA_Q1AD_MG3 ? "q1:mg3" : "q1:ctf";
  q1af_open_world(&w->af, &options);
  w->af.context = campaign ? qa_q1ad_register_q1_campaign_addons(w->af.base, program, q1af_services())
                           : qa_q1ad_context_create(w->af.base, program, q1af_services());
  if (w->af.context == NULL) abort();
  w->player = q1_world_fixture_admit_player(&w->af.fx, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(w->af.fx.game, w->player, "axe", true, NULL);
}
static void close_world(world *w) { q1af_close(&w->af); }

/* The "message" event texts emitted since event index `from`. */
static size_t messages_since(world *w, size_t from, const char **out, size_t capacity) {
  size_t count = 0;
  for (size_t i = from; i < w->af.fx.event_count; i++)
    if (strcmp(w->af.fx.events[i].kind, "message") == 0) {
      if (count < capacity) out[count] = w->af.fx.events[i].text;
      count++;
    }
  return count;
}

/* --- CTF services (sentinel ctx) --- */
static double ctf_impulse;
static int ctf_consumed;
static const char *cs_name(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return "Player"; }
static bool cs_is_bot(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return false; }
static double cs_score(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return 0; }
static void cs_add_score(void *ctx, qa_actor_id actor, double delta) { (void)actor; (void)delta; Q1AF_CTX(ctx); }
static double cs_captures(void *ctx, const char *team) { (void)team; Q1AF_CTX(ctx); return 0; }
static void cs_add_capture(void *ctx, const char *team) { (void)team; Q1AF_CTX(ctx); }
static qa_q1ad_ctf_input cs_input(void *ctx, qa_actor_id actor) {
  (void)actor; Q1AF_CTX(ctx);
  qa_q1ad_ctf_input input;
  memset(&input, 0, sizeof input);
  input.impulse = ctf_impulse;
  return input;
}
static void cs_consume_impulse(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); ctf_consumed++; }
static bool cs_observer(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return false; }
static void cs_set_observer(void *ctx, qa_actor_id actor, bool observer) { (void)actor; (void)observer; Q1AF_CTX(ctx); }
static void cs_respawn(void *ctx, qa_actor_id actor, qa_q1_actor *spot) { (void)actor; (void)spot; Q1AF_CTX(ctx); }
static void cs_disconnect(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static void cs_colors(void *ctx, qa_actor_id actor, double shirt, double pants) { (void)actor; (void)shirt; (void)pants; Q1AF_CTX(ctx); }
static bool cs_prompt_supported(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return true; }
static void cs_prompt(void *ctx, qa_actor_id actor, const char *title, const qa_q1ad_ctf_prompt_choice *choices, size_t count) {
  (void)actor; (void)title; (void)choices; (void)count; Q1AF_CTX(ctx);
}
static void cs_clear_prompt(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static void cs_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  (void)actor; (void)origin; (void)angles; (void)velocity; (void)until; Q1AF_CTX(ctx);
}
static void cs_select_grapple(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static qa_item_id cs_selected_weapon(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return "q1:weapon/shotgun"; }
static qa_item_id cs_selected_ammo(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return "q1:ammo/shells"; }
static void cs_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) { (void)actor; (void)acquired; Q1AF_CTX(ctx); }
static void cs_haste(void *ctx, qa_actor_id actor, bool enabled) { (void)actor; (void)enabled; Q1AF_CTX(ctx); }
static void cs_status(void *ctx, qa_actor_id actor, qa_q1ad_ctf_status status) { (void)actor; (void)status; Q1AF_CTX(ctx); }
static void cs_log(void *ctx, qa_actor_id actor, const char *action) { (void)actor; (void)action; Q1AF_CTX(ctx); }

static void set_player_origin(world *w, qa_vec3 origin) {
  qa_body_state_result read = qa_shared_body_table_read(w->af.fx.bodies, w->player.id);
  if (!read.has_value) abort();
  qa_body_state body = read.value;
  qa_actor_id ground;
  if (body.ground != NULL) { ground = *body.ground; body.ground = &ground; }
  body.origin = origin;
  qa_shared_body_table_write(w->af.fx.bodies, w->player, body);
}

/* ctf/index.ts impulse 99 (TS 04:40): `You are at '${x.toFixed(1).padStart(5)} ...'\n`, not centered.
 * Oracle: ctf_impulse99.ts. */
static void test_ctf_impulse_99(void) {
  world w;
  open_world(&w, QA_Q1AD_CTF, false);
  qa_q1ad_ctf_services services = {
      .ctx = &q1af_sentinel, .name = cs_name, .is_bot = cs_is_bot, .score = cs_score, .add_score = cs_add_score,
      .captures = cs_captures, .add_capture = cs_add_capture, .input = cs_input, .consume_impulse = cs_consume_impulse,
      .observer = cs_observer, .set_observer = cs_set_observer, .respawn = cs_respawn, .disconnect = cs_disconnect,
      .colors = cs_colors, .prompt_supported = cs_prompt_supported, .prompt = cs_prompt, .clear_prompt = cs_clear_prompt,
      .teleport = cs_teleport, .select_grapple = cs_select_grapple, .selected_weapon = cs_selected_weapon,
      .selected_ammo = cs_selected_ammo, .weapon_changed = cs_weapon_changed, .haste = cs_haste, .status = cs_status,
      .log = cs_log};
  qa_q1ad_ctf *ctf = qa_q1ad_register_ctf(w.af.context, services, NULL);
  CHECK(ctf != NULL);
  static const struct { qa_vec3 origin; const char *text; } CASES[] = {
      {{1.25, -3.05, 1234.5678}, "You are at '  1.3  -3.0 1234.6'\n"},
      {{0, -0.04, 100000}, "You are at '  0.0  -0.0 100000.0'\n"},
      {{-12.345, 7, 0.05}, "You are at '-12.3   7.0   0.1'\n"},
  };
  for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    set_player_origin(&w, CASES[i].origin);
    ctf_impulse = 99;
    ctf_consumed = 0;
    size_t from = w.af.fx.event_count;
    CHECK(qa_q1ad_ctf_impulse(ctf, w.player.id));
    CHECK(ctf_consumed == 1);
    const char *texts[4];
    CHECK(messages_since(&w, from, texts, 4) == 1);
    CHECK(strcmp(texts[0], CASES[i].text) == 0);
    for (size_t e = from; e < w.af.fx.event_count; e++)
      if (strcmp(w.af.fx.events[e].kind, "message") == 0) CHECK(!w.af.fx.events[e].center);
  }
  /* An impulse CTF does not own is left to the arsenal and not consumed. */
  ctf_impulse = 7;
  ctf_consumed = 0;
  CHECK(!qa_q1ad_ctf_impulse(ctf, w.player.id));
  CHECK(ctf_consumed == 0);
  qa_q1ad_ctf_destroy(ctf);
  CHECK(w.af.fx.ctx_errors == 0);
  close_world(&w);
}

/* triggers.ts counterMessage: SPAWNFLAG 8388608 broadcasts for a non-player activator.
 * Oracle: drift.ts "counter" lines (count 3 used once: "$qc_two_more", count 2). */
static void test_counter_without_player(void) {
  static const qa_q1ad_program PROGRAMS[2] = {QA_Q1AD_MG1, QA_Q1AD_MG3};
  for (size_t p = 0; p < 2; p++)
    for (int flagged = 0; flagged < 2; flagged++) {
      world w;
      open_world(&w, PROGRAMS[p], true);
      const char *fields[] = {"target", "nothing", "count", "3", "spawnflags", flagged ? "8388608" : "0"};
      qa_q1_actor *counter = q1af_spawn_fields(&w.af, "trigger_counter", fields, 3);
      size_t from = w.af.fx.event_count;
      q1af_use(&w.af, counter, NULL);
      const char *texts[4];
      size_t count = messages_since(&w, from, texts, 4);
      CHECK(count == (flagged ? 1u : 0u));
      if (flagged && count == 1) CHECK(strcmp(texts[0], "$qc_two_more") == 0);
      CHECK(counter->count == 2);
      CHECK(w.af.fx.ctx_errors == 0);
      close_world(&w);
    }
}

/* effects.ts push/sourceFogPush (MG1 and MG3): duration 0, false for a missing info_fog.
 * Oracle: drift.ts "fog" lines. */
static void test_source_fog_push(void) {
  static const qa_q1ad_program PROGRAMS[2] = {QA_Q1AD_MG1, QA_Q1AD_MG3};
  for (size_t p = 0; p < 2; p++) {
    world w;
    open_world(&w, PROGRAMS[p], true);
    qa_q1_services *game = w.af.fx.game;
    CHECK(game->source_fog_push.fn != NULL);
    const char *fog_fields[] = {"targetname", "fogA", "fog_density", "0.25", "fog_color", "128 64 255"};
    (void)q1af_spawn_fields(&w.af, "info_fog", fog_fields, 3);
    const char *holder_fields[] = {"fog_info_entity", "fogA"};
    qa_q1_actor *holder = q1af_spawn_fields(&w.af, "info_notnull", holder_fields, 1);
    const char *none_fields[] = {"fog_info_entity", "missing"};
    qa_q1_actor *none = q1af_spawn_fields(&w.af, "info_notnull", none_fields, 1);
    w.af.fog_events = 0;
    CHECK(game->source_fog_push.fn(game->source_fog_push.ctx, w.player.id, holder));
    CHECK(w.af.fog_events == 1);
    const qa_q1ad_event fog = w.af.last_event;
    CHECK(fog.kind == QA_Q1AD_EVENT_FOG);
    CHECK(fog.has_player && qa_same_actor(fog.player, w.player.id));
    CHECK(fog.density == 0.25);
    CHECK(fog.color_vec.x == 0.501960813999176 && fog.color_vec.y == 0.250980406999588 && fog.color_vec.z == 1);
    CHECK(fog.duration == 0 && fog.sky_factor == 0);
    CHECK(!game->source_fog_push.fn(game->source_fog_push.ctx, w.player.id, none));
    CHECK(w.af.fog_events == 1);
    CHECK(qa_q1ad_player_number(w.af.context, w.player.id, "fog_color_z") == 1);
    CHECK(w.af.fx.ctx_errors == 0);
    close_world(&w);
  }
}

/* --- horde (sentinel ctx) --- */
static double hs_dead_flag(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return 0; }
static bool hs_no_target(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return false; }
static bool hs_is_bot(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return false; }
static void hs_respawn_teammate(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); }
static void hs_add_score(void *ctx, qa_actor_id player, double delta) { (void)player; (void)delta; Q1AF_CTX(ctx); }
static void hs_restart_session(void *ctx, const char *map, double flags) { (void)map; (void)flags; Q1AF_CTX(ctx); }

/* horde/index.ts fadeCorpses from the player afterPhysics: 0.1 s scan gate, alpha 1 - (time - fadeAt),
 * removal at alpha <= 0, nothing while the horde cvar is 0. Oracle: drift.ts "horde" and "horde-off". */
static void test_horde_fade_corpses(void) {
  world w;
  open_world(&w, QA_Q1AD_MG1, true);
  qa_q1ad_horde *horde = qa_q1ad_register_horde(w.af.context, (qa_q1ad_horde_services){
      .ctx = &q1af_sentinel, .dead_flag = hs_dead_flag, .no_target = hs_no_target, .is_bot = hs_is_bot,
      .respawn_teammate = hs_respawn_teammate, .add_score = hs_add_score, .restart_session = hs_restart_session});
  CHECK(horde != NULL);
  qa_q1_services *game = w.af.fx.game;
  w.af.horde_cvar = 1;
  qa_q1_actor *corpse = q1af_create(&w.af, "monster_army");
  qa_q1ad_set_number(w.af.context, corpse, "horde.fadeAt", 10);
  qa_q1_actor *early = q1af_create(&w.af, "monster_army");
  qa_q1ad_set_number(w.af.context, early, "horde.fadeAt", 20);
  qa_q1_player_state *player = qa_q1_player(game, w.player.id);
  CHECK(player != NULL);
  const qa_q1_player_extension *extension = &horde->player_extension;
  static const struct { double time, alpha; bool live; int events; double event_alpha; } STEPS[] = {
      {10.25, 0.75, true, 1, 0.75},
      {10.3, 0.75, true, 0, 0},
      {10.36, 0.6400003433227539, true, 1, 0.6400003433227539},
      {11.5, 0.6400003433227539, false, 0, 0},
  };
  for (size_t i = 0; i < sizeof STEPS / sizeof STEPS[0]; i++) {
    qa_q1_services_set_time(game, STEPS[i].time);
    w.af.alpha_events = 0;
    extension->after_physics(extension->ctx, game, player, 0);
    CHECK(w.af.alpha_events == STEPS[i].events);
    if (STEPS[i].events == 1) CHECK(w.af.last_event.kind == QA_Q1AD_EVENT_ALPHA && w.af.last_event.alpha == STEPS[i].event_alpha);
    CHECK(qa_q1_actor_number(corpse, "alpha") == STEPS[i].alpha);
    CHECK(qa_q1_live(game, corpse) == STEPS[i].live);
    CHECK(qa_q1_actor_number(early, "alpha") == 0);
  }
  w.af.horde_cvar = 0;
  qa_q1_actor *late = q1af_create(&w.af, "monster_army");
  qa_q1ad_set_number(w.af.context, late, "horde.fadeAt", 1);
  qa_q1_services_set_time(game, 20);
  extension->after_physics(extension->ctx, game, player, 0);
  CHECK(qa_q1_actor_number(late, "alpha") == 0);
  CHECK(qa_q1_live(game, late));
  qa_q1ad_horde_destroy(horde);
  CHECK(w.af.fx.ctx_errors == 0);
  close_world(&w);
}

/* heavy/lava-man.ts registerLavaManDamage over the folded game.damageSourceEffects. Oracle: lava.ts. */
static qa_damage_request damage_request(qa_actor_id target, const qa_actor_id *attacker) {
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = target;
  request.amount = 100;
  request.delivery = "direct";
  request.attack.attacker = attacker;
  request.attack.inflictor = attacker;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q1;
  request.attack.cause.death_type = "direct";
  return request;
}
static void test_lava_man_damage(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, true);
  qa_q1_services *game = w.af.fx.game;
  qa_q1_actor *lava = q1af_create(&w.af, "monster_lava_man");
  qa_q1_actor *boss = q1af_create(&w.af, "monster_boss");
  qa_q1_actor *other = q1af_create(&w.af, "monster_ogre");
  qa_q1_player_state *player = qa_q1_player(game, w.player.id);
  CHECK(player != NULL);
  const qa_q1_damage_source_effects effects = qa_q1_damage_source_effects_of(game);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  const qa_actor_id player_id = w.player.id, boss_id = boss->actor.id;
  static const struct { const char *weapon; double lava, other; } WEAPONS[] = {
      {"rocketlauncher", 26.64000129699707, 33.3}, {"lightning", 33.3, 33.3},
      {"mg3:laser", 33.3, 33.3}, {"shotgun", 26.64000129699707, 33.3},
  };
  for (size_t i = 0; i < sizeof WEAPONS / sizeof WEAPONS[0]; i++) {
    player->weapon = WEAPONS[i].weapon;
    CHECK(effects.after_armor(effects.ctx, damage_request(lava->actor.id, &player_id), 33.3, combat, &combat) == WEAPONS[i].lava);
    CHECK(effects.after_armor(effects.ctx, damage_request(other->actor.id, &player_id), 33.3, combat, &combat) == WEAPONS[i].other);
  }
  CHECK(effects.after_armor(effects.ctx, damage_request(lava->actor.id, NULL), 33.3, combat, NULL) == 26.64000129699707);
  CHECK(effects.after_armor(effects.ctx, damage_request(lava->actor.id, &boss_id), 50, combat, &combat) == 40);
  CHECK(!effects.before_health(effects.ctx, damage_request(lava->actor.id, &boss_id), 10, combat, &combat));
  CHECK(effects.before_health(effects.ctx, damage_request(lava->actor.id, &player_id), 10, combat, &combat));
  CHECK(effects.before_health(effects.ctx, damage_request(other->actor.id, &boss_id), 10, combat, &combat));
  CHECK(effects.before_health(effects.ctx, damage_request(lava->actor.id, NULL), 10, combat, NULL));
  CHECK(w.af.fx.ctx_errors == 0);
  close_world(&w);
}

/* ordinary/nightmare.ts mg3NightmareFrames, composed into the MG3 sourceFrames only.
 * Oracle: drift.ts "frame" lines; frames.ts shows MG1 composes neither name (its BaseMonster
 * falls back to the id1 frame). */
static void check_frame(const qa_q1_monster_frame_table *table, const char *name, double frame, const char *next,
                        qa_q1_monster_ai mode, double distance, bool action) {
  const qa_q1_monster_frame *entry = qa_q1_monster_frame_table_get(table, name);
  CHECK(entry != NULL);
  if (entry == NULL) return;
  CHECK(entry->frame == frame && strcmp(entry->next, next) == 0);
  CHECK(entry->operation_count == (action ? 2u : 1u));
  if (entry->operation_count < 1) return;
  CHECK(entry->operations[0].kind == QA_Q1_OP_AI && entry->operations[0].mode == mode && entry->operations[0].distance == distance);
  if (action && entry->operation_count == 2)
    CHECK(entry->operations[1].kind == QA_Q1_OP_ACTION && strcmp(entry->operations[1].name, name) == 0);
}
static void test_nightmare_frames(void) {
  check_frame(&qa_q1am_ordinary_mg3_frames, "sham_smash12", 46, "sham_run1", QA_Q1_AI_CHARGE, 4, true);
  check_frame(&qa_q1am_ordinary_mg3_frames, "ogre_nail1", 61, "ogre_nail2", QA_Q1_AI_FACE, 0, true);
  check_frame(&qa_q1am_ordinary_mg3_frames, "ogre_nail5", 64, "ogre_nail6", QA_Q1_AI_FACE, 0, true);
  CHECK(!qa_q1_monster_frame_table_has(&qa_q1am_ordinary_mg1_frames, "sham_smash12"));
  CHECK(!qa_q1_monster_frame_table_has(&qa_q1am_ordinary_mg1_frames, "ogre_nail1"));
}

/* items/common.ts ringEffect: a placed ring hums 8 units up once; a respawning (SPAWNED_ITEM)
 * draught hums at its origin on its first regenerate only. Oracle: ring.ts. */
static size_t count_class(world *w, const char *classname, qa_q1_actor **last) {
  size_t count = 0;
  qa_q1_services *game = w->af.fx.game;
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, classname) == 0) { count++; if (last != NULL) *last = game->entities[i]; }
  return count;
}
static bool same_vec(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static void test_ring_effect(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, true);
  qa_q1_services *game = w.af.fx.game;
  const char *placed_fields[] = {"origin", "10 20 30"};
  qa_q1_actor *placed = q1af_spawn_fields(&w.af, "item_draught_insight", placed_fields, 1);
  CHECK(placed->count == 1);
  CHECK(q1af_count_events(&w.af, "ambient") == 1);
  const qa_q1_event *hum = q1af_first_event(&w.af, "ambient");
  CHECK(hum != NULL && same_vec(hum->origin, (qa_vec3){10, 20, 38}) && strcmp(hum->path, "ambience/hum1.wav") == 0 &&
        hum->volume == 0.7 && hum->attenuation == 3);
  qa_q1_actor *tele = NULL;
  CHECK(count_class(&w, "particle_tele", &tele) == 1);
  CHECK(tele != NULL && same_vec(qa_q1_get_body(game, tele).origin, (qa_vec3){10, 20, 38}) &&
        qa_q1_actor_number(tele, "distance") == 48);
  const char *respawn_fields[] = {"origin", "-5 0 64", "spawnflags", "4"};
  qa_q1_actor *respawned = q1af_spawn_fields(&w.af, "item_draught_stupor", respawn_fields, 2);
  CHECK(respawned->count == 0);
  CHECK(q1af_count_events(&w.af, "ambient") == 1 && count_class(&w, "particle_tele", NULL) == 1);
  size_t from = w.af.fx.event_count;
  q1af_use(&w.af, respawned, NULL);
  CHECK(respawned->count == 1);
  CHECK(q1af_count_events(&w.af, "ambient") == 2);
  for (size_t i = from; i < w.af.fx.event_count; i++)
    if (strcmp(w.af.fx.events[i].kind, "ambient") == 0) CHECK(same_vec(w.af.fx.events[i].origin, (qa_vec3){-5, 0, 64}));
  CHECK(count_class(&w, "particle_tele", &tele) == 2);
  CHECK(tele != NULL && same_vec(qa_q1_get_body(game, tele).origin, (qa_vec3){-5, 0, 64}) &&
        qa_q1_actor_number(tele, "distance") == 48);
  q1af_use(&w.af, respawned, NULL);
  CHECK(respawned->count == 1);
  CHECK(q1af_count_events(&w.af, "ambient") == 2 && count_class(&w, "particle_tele", NULL) == 2);
  CHECK(w.af.fx.ctx_errors == 0);
  close_world(&w);
}

int main(void) {
  test_ctf_impulse_99();
  test_counter_without_player();
  test_source_fog_push();
  test_horde_fade_corpses();
  test_lava_man_damage();
  test_nightmare_frames();
  test_ring_effect();
  CHECK(q1af_ctx_errors == 0);
  CHECK(q1af_ctx_checks > 0);
  if (failures != 0) {
    fprintf(stderr, "test_q1_sync_addons: %d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("test_q1_sync_addons: %d checks, 0 failures\n", checks);
  return 0;
}
