/*
 * C checks for src/content/q3 rows completed from the symbol audit:
 * death.ts and personal-portal.ts dropContext, save-level.ts restoreQ3Level,
 * arsenal.ts stepQ3Arsenal effect order (expected sequence captured from the
 * TypeScript function), snapshot-state.ts convertPlayerState over the game
 * mirrors, client-effects.ts damageFeedback and sendPendingPredictableEvents.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q3/base/game/death.h"
#include "qa/content/q3/base/game/level.h"
#include "qa/content/q3/base/game/personal_portal.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/content/q3/base/game/save_level.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "q3_fake_entity.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/core/math.h"
#include "qa/movement/q3/constants.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { ENTITY_COUNT = 160 };
static qa_q3_game_entity entities[ENTITY_COUNT];
static q3_fake_entity bodies[ENTITY_COUNT];
static qa_q3_game_client clients[64];
static bool clients_live;
static qa_q3_entity_pool pool;
static int released, links, touches, flag_thinks, team_checks, random_calls, frames;
static int host_sentinel, item_sentinel;
static double frame_game_type;

static qa_q3_game_entity *rec_get(void *ctx, double slot) { (void)ctx; return slot >= 0 && slot < ENTITY_COUNT ? &entities[(int)slot] : NULL; }
static qa_q3_game_entity *rec_activate(void *ctx, double slot) {
  (void)ctx; int i = (int)slot;
  q3_fake_entity_init(&entities[i], &bodies[i], slot); bodies[i].active = true; return &entities[i];
}
static qa_q3_game_client *rec_client(void *ctx, double slot) { (void)ctx; return &clients[(int)slot]; }
static void rec_release(void *ctx, qa_q3_game_entity *e) { (void)ctx; bodies[(int)e->slot].active = false; released++; }
static void rec_deactivate(void *ctx, double slot) { (void)ctx; (void)slot; }
static double pool_time(void *ctx) { (void)ctx; return 1000; }
static void pool_print(void *ctx, const char *m) { (void)ctx; (void)m; }
static void pool_link(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; links++; }
static void pool_unlink(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; }

static void pool_setup(qa_q3_base_product product) {
  for (int i = 0; i < ENTITY_COUNT; i++) bodies[i].active = false;
  if (clients_live) for (int i = 0; i < 64; i++) qa_q3_game_client_destroy(&clients[i]);
  for (int i = 0; i < 64; i++) qa_q3_game_client_init(&clients[i], product);
  clients_live = true;
  qa_q3_entity_pool_options o; memset(&o, 0, sizeof o);
  o.records.get = rec_get; o.records.activate = rec_activate; o.records.client = rec_client;
  o.records.release = rec_release; o.records.deactivate_client = rec_deactivate;
  o.product = product; o.max_clients = 8; o.time = pool_time; o.print = pool_print;
  o.link = pool_link; o.unlink = pool_unlink;
  qa_q3_entity_pool_init(&pool, o);
  released = links = touches = flag_thinks = team_checks = random_calls = frames = 0;
}

/* Items are the shared/items.ts tables; indices from bun:
 * itemList("baseq3") rocket launcher 12, quad 28; missionpack red flag 34, red cube 47. */
static const qa_q3_base_item_definition *rocket_item(void) { return qa_q3_base_find_item_for_weapon(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_WP_ROCKET_LAUNCHER); }
static const qa_q3_base_item_definition *quad_item(void) { return qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_PW_QUAD); }
static const qa_q3_base_item_definition *red_flag_item(void) { return qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, QA_Q3_PW_REDFLAG); }
static const qa_q3_base_item_definition *red_cube_item(void) { return qa_q3_base_find_item(QA_Q3_BASE_PRODUCT_MISSIONPACK, "Red Cube"); }

static void item_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) { (void)s; (void)o; (void)c; CHECK(ctx == &item_sentinel); touches++; }
static void flag_think(void *ctx, qa_q3_game_entity *s) { (void)s; CHECK(ctx == &item_sentinel); flag_thinks++; }
static void team_check(void *ctx, qa_q3_game_entity *s) { (void)s; CHECK(ctx == &item_sentinel); team_checks++; }
static qa_q3_entity_callback callback_of(const void *fn) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = &item_sentinel; return cb; }
static double host_random(void *ctx) { CHECK(ctx == &host_sentinel); random_calls++; return 0.5; }
static double host_crandom(void *ctx) { CHECK(ctx == &host_sentinel); return 0; }
static qa_q3_death_frame death_frame(void *ctx) {
  CHECK(ctx == &host_sentinel); frames++;
  qa_q3_death_frame f = {1000, frame_game_type, 0, 0, true}; return f;
}
static qa_q3_game_entity *no_obelisk(void *ctx) { (void)ctx; return NULL; }
static double cube_timeout(void *ctx) { (void)ctx; return 30; }
static void world_link(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; }
static void world_unlink(void *ctx, double n) { (void)ctx; (void)n; }
static qa_q3_base_server_world world_mirror(void) {
  qa_q3_base_server_world w; memset(&w, 0, sizeof w); w.link = world_link; w.unlink = world_unlink; return w;
}

static qa_q3_death_host death_host(qa_q3_base_product product, qa_q3_base_player_state_slots *scores, qa_q3_death_animation_sequence *sequence) {
  qa_q3_death_host h; memset(&h, 0, sizeof h);
  qa_q3_entity_touch_fn t = item_touch; qa_q3_entity_think_fn f = flag_think, c = team_check;
  h.ctx = &host_sentinel; h.product = product; h.pool = &pool; h.team_scores = scores; h.death_animations = sequence;
  h.frame = death_frame; h.random_value = host_random; h.crandom = host_crandom;
  h.neutral_obelisk = no_obelisk; h.cube_timeout_seconds = cube_timeout;
  h.touch_item = callback_of(&t); h.dropped_flag_think = callback_of(&f); h.check_dropped_team_item = callback_of(&c);
  h.world = world_mirror();
  return h;
}

static void test_death_drop_context(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  static int32_t score_words[4]; static qa_q3_base_player_state_slots scores;
  (void)qa_q3_base_player_state_slots_init(&scores, 4, score_words, 4, true, NULL, NULL, NULL);
  static qa_q3_death_animation_sequence sequence; qa_q3_death_animation_sequence_init(&sequence);
  qa_q3_death_runtime runtime; frame_game_type = 0;
  qa_q3_death_runtime_init(&runtime, death_host(QA_Q3_BASE_PRODUCT_BASEQ3, &scores, &sequence));
  qa_q3_game_entity *victim = qa_q3_entity_pool_at(&pool, 0);
  victim->client = &clients[0]; victim->s.weapon = 5;
  qa_q3_base_player_state_slots_set(&clients[0].ps.ammo, 5, 3);
  qa_q3_base_player_state_slots_set(&clients[0].ps.powerups, QA_Q3_PW_QUAD, 4500);
  rec_activate(NULL, 0); victim->client = &clients[0]; victim->s.weapon = 5;
  double before = pool.num_entities; int before_links = links;
  qa_q3_death_toss_client_items(&runtime, victim);
  CHECK(random_calls == 2); CHECK(links == before_links + 2); CHECK(frames == 1);
  qa_q3_game_entity *weapon = NULL, *quad = NULL;
  for (double i = 0; i < pool.num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(&pool, i);
    if (!qa_q3_game_entity_inuse(e) || !e->item) continue;
    if (e->item == rocket_item()) weapon = e;
    if (e->item == quad_item()) quad = e;
  }
  CHECK(before <= pool.num_entities); CHECK(weapon != NULL); CHECK(quad != NULL);
  if (weapon && quad) {
    CHECK(weapon->s.modelindex == 12); CHECK(quad->s.modelindex == 28);
    CHECK(weapon->s.pos.time == 1000); CHECK(weapon->s.pos.delta.z == 200);
    CHECK(weapon->s.pos.delta.x == (double)(float)150);
    CHECK(quad->count == 3); /* trunc((4500 - 1000) / 1000) */
    CHECK(quad->s.pos.delta.x == qa_scale3(qa_qvm_angle_vectors((qa_vec3){0, 45, 0}).forward, 150).x);
    CHECK(quad->nextthink == 31000);
    CHECK(((int)quad->flags & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0);
    CHECK(quad->touch.ctx == &item_sentinel);
  }
  /* GT_TEAM stops after the weapon. */
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3); frame_game_type = QA_Q3_BASE_GT_TEAM;
  qa_q3_death_runtime_init(&runtime, death_host(QA_Q3_BASE_PRODUCT_BASEQ3, &scores, &sequence));
  victim = rec_activate(NULL, 0); victim->client = &clients[0]; victim->s.weapon = 5;
  qa_q3_base_player_state_slots_set(&clients[0].ps.ammo, 5, 3);
  qa_q3_base_player_state_slots_set(&clients[0].ps.powerups, QA_Q3_PW_QUAD, 4500);
  qa_q3_death_toss_client_items(&runtime, victim);
  CHECK(random_calls == 1);
  /* tossClientCubes launches through the same context with no Drop_Item random. */
  pool_setup(QA_Q3_BASE_PRODUCT_MISSIONPACK); frame_game_type = QA_Q3_BASE_GT_HARVESTER;
  qa_q3_death_runtime_init(&runtime, death_host(QA_Q3_BASE_PRODUCT_MISSIONPACK, &scores, &sequence));
  victim = rec_activate(NULL, 0); victim->client = &clients[0]; clients[0].sess.session_team = 1; clients[0].ps.generic1 = 4;
  qa_q3_entity_pool_free(&pool, qa_q3_entity_pool_spawn(&pool)); /* entitiesFree needs a free slot past the clients */
  qa_q3_death_toss_client_cubes(&runtime, victim);
  CHECK(clients[0].ps.generic1 == 0); CHECK(random_calls == 0);
  bool found = false;
  for (double i = 0; i < pool.num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(&pool, i);
    if (qa_q3_game_entity_inuse(e) && e->item == red_cube_item()) {
      found = true; CHECK(e->spawnflags == 1); CHECK(e->nextthink == 31000);
      CHECK(e->s.pos.delta.z == 200);
    }
  }
  CHECK(found);
}

static double portal_model(void *ctx, const char *n) { (void)n; CHECK(ctx == &host_sentinel); return 7; }
static void test_portal_drop_context(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  static qa_q3_game_combat_context combat; memset(&combat, 0, sizeof combat);
  combat.entities = &pool; combat.product = QA_Q3_BASE_PRODUCT_MISSIONPACK; combat.time = 1000; combat.game_type = QA_Q3_BASE_GT_CTF;
  qa_q3_personal_portal_host h; memset(&h, 0, sizeof h);
  qa_q3_entity_touch_fn t = item_touch; qa_q3_entity_think_fn f = flag_think, c = team_check;
  h.ctx = &host_sentinel; h.combat = &combat; h.world = world_mirror(); h.model_index = portal_model; h.random_value = host_random;
  h.spatial_is_world = true;
  h.touch_item = callback_of(&t); h.dropped_flag_think = callback_of(&f); h.check_dropped_team_item = callback_of(&c);
  qa_q3_personal_portal_runtime runtime; qa_q3_personal_portal_runtime_init(&runtime, h);
  qa_q3_game_entity *player = rec_activate(NULL, 0); player->client = &clients[0];
  qa_q3_game_entity_set_health(player, 100); clients[0].sess.session_team = 3;
  qa_q3_base_player_state_slots_set(&clients[0].ps.powerups, QA_Q3_PW_REDFLAG, 9000);
  qa_q3_game_entity *destination = qa_q3_entity_pool_spawn(&pool);
  qa_q3_game_entity_set_classname(destination, "hi_portal destination"); destination->count = 5;
  qa_q3_game_entity *source = qa_q3_entity_pool_spawn(&pool); source->count = 5;
  qa_q3_entity_callback touch = qa_q3_callback_family_resolve(&pool.callbacks.touch, "q3.base.game.personal-portal.portalEnable.touch");
  qa_q3_entity_touch_fn fn; memcpy(&fn, &touch.fn, sizeof fn);
  qa_touch_contact contact; memset(&contact, 0, sizeof contact);
  fn(touch.ctx, source, qa_q3_participant_entity(player), contact);
  CHECK(random_calls == 1); CHECK(team_checks == 1);
  CHECK(qa_q3_base_player_state_slots_get(&clients[0].ps.powerups, QA_Q3_PW_REDFLAG) == 0);
  bool found = false;
  for (double i = 0; i < pool.num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(&pool, i);
    if (qa_q3_game_entity_inuse(e) && e->item == red_flag_item()) {
      found = true; CHECK(e->s.modelindex == 34); CHECK(e->think.ctx == &item_sentinel);
      CHECK(e->s.pos.time == 1000);
    }
  }
  CHECK(found);
}

static void put_n(qa_save_value *o, const char *k, double v) { qa_save_value_set(o, k, qa_save_value_number(v)); }
static void put_s(qa_save_value *o, const char *k, const char *v) { qa_save_value_set(o, k, qa_save_value_string(v)); }
static void put_b(qa_save_value *o, const char *k, bool v) { qa_save_value_set(o, k, qa_save_value_boolean(v)); }
static qa_save_value *vector_value(void) { qa_save_value *o = qa_save_value_object(); put_n(o, "x", 0); put_n(o, "y", 0); put_n(o, "z", 0); return o; }
static qa_save_value *numbers(size_t n, double v) { qa_save_value *a = qa_save_value_array(); for (size_t i = 0; i < n; i++) qa_save_value_push(a, qa_save_value_number(v)); return a; }
static qa_save_value *team_vote(double time, const char *s) {
  qa_save_value *o = qa_save_value_object(); put_n(o, "time", time); put_n(o, "yes", 2); put_n(o, "no", 3); put_s(o, "string", s); return o;
}
static void test_restore_level_votes(void) {
  static qa_q3_game_level level; qa_q3_game_level_init(&level);
  qa_save_value *root = qa_save_value_object(), *values = qa_save_value_object();
  static const char *const nums[] = {"time", "startTime", "warmupTime", "warmupModificationCount", "numConnectedClients", "numNonSpectatorClients",
    "numPlayingClients", "numVotingClients", "follow1", "follow2", "intermissionTime", "intermissionQueued", "exitTime", "frameNum", "previousTime", "frySound"};
  for (size_t i = 0; i < sizeof nums / sizeof *nums; i++) put_n(values, nums[i], 0);
  put_b(values, "restarted", false); put_b(values, "readyToExit", false); put_b(values, "newSession", false);
  qa_save_value_set(values, "intermissionOrigin", vector_value()); qa_save_value_set(values, "intermissionAngle", vector_value());
  qa_save_value_set(values, "changemap", qa_save_value_null());
  qa_save_value_set(root, "values", values);
  qa_save_value_set(root, "teamScores", numbers(4, 7));
  qa_save_value_set(root, "numTeamVotingClients", numbers(2, 2));
  qa_save_value_set(root, "sortedClients", numbers(QA_Q3_MAX_CLIENTS, 1));
  qa_save_value *vote = qa_save_value_object();
  put_n(vote, "time", 100); put_n(vote, "yes", 1); put_n(vote, "no", 4); put_s(vote, "string", "map q3dm1");
  put_s(vote, "displayString", "map q3dm1 display"); put_n(vote, "executeTime", 3100);
  qa_save_value_set(root, "vote", vote);
  qa_save_value *teams = qa_save_value_array();
  qa_save_value_push(teams, team_vote(11, "leader 1")); qa_save_value_push(teams, team_vote(22, "leader 2"));
  qa_save_value_set(root, "teamVotes", teams);
  qa_q3_restore_level(&level, root);
  CHECK(level.base.vote.time == 100); CHECK(strcmp(level.base.vote.string, "map q3dm1") == 0);
  CHECK(strcmp(level.base.vote.display_string, "map q3dm1 display") == 0);
  CHECK(level.base.vote.execute_time == 3100);
  CHECK(level.base.team_votes[0].time == 11); CHECK(level.base.team_votes[1].time == 22);
  CHECK(level.base.team_votes[1].yes == 2); CHECK(level.base.team_votes[1].no == 3);
  CHECK(strcmp(level.base.team_votes[0].string, "leader 1") == 0);
  CHECK(strcmp(level.base.team_votes[1].string, "leader 2") == 0);
  qa_q3_level_capture capture = qa_q3_capture_level(&level);
  CHECK(strcmp(capture.vote.display_string, "map q3dm1 display") == 0);
  CHECK(capture.vote.display_string != level.base.vote.display_string);
  CHECK(capture.vote.execute_time == 3100);
  qa_q3_level_capture_destroy(&capture);
  qa_save_value_free(root);
  qa_q3_game_level_clear(&level);
  CHECK(strcmp(level.base.vote.display_string, "") == 0); CHECK(strcmp(level.base.team_votes[0].string, "") == 0);
  qa_q3_game_level_destroy(&level);
}

/* Expected text captured by running stepQ3Arsenal over the same five steps. */
static const char *const ARSENAL_ORACLE[] = {
  "animation weapon:2,0,0->2,3,0 ammo:99 ev:23 weapon:2,3,0->2,3,100",
  "weapon:2,3,100->2,3,0 weapon:2,3,0->2,0,0",
  "ev:22 weapon:2,0,0->2,2,0 weapon:2,2,0->2,2,200 animation",
  "weapon:2,2,200->2,2,0 weapon:2,2,0->1,2,0 sel:q3:weapon/machinegun->q3:weapon/gauntlet weapon:1,2,0->1,1,0 weapon:1,1,0->1,1,250 animation",
  "weapon:1,1,250->1,1,0 weapon:1,1,0->1,0,0 animation",
};
static void describe(const qa_q3_arsenal_step *r, char *out, size_t cap) {
  size_t n = 0; out[0] = 0;
  for (size_t i = 0; i < r->base.effect_count; i++) {
    const qa_movement_effect *e = &r->base.effects[i];
    const char *sep = i ? " " : "";
    switch (e->kind) {
      case QA_MOVEMENT_EFFECT_WEAPON:
        n += (size_t)snprintf(out + n, cap - n, "%sweapon:%g,%g,%g->%g,%g,%g", sep, e->weapon_before.source_weapon, e->weapon_before.state,
            e->weapon_before.time_milliseconds, e->weapon_after.source_weapon, e->weapon_after.state, e->weapon_after.time_milliseconds);
        break;
      case QA_MOVEMENT_EFFECT_WEAPON_SELECTION: n += (size_t)snprintf(out + n, cap - n, "%ssel:%s->%s", sep, e->selection_before, e->selection_after); break;
      case QA_MOVEMENT_EFFECT_AMMO: n += (size_t)snprintf(out + n, cap - n, "%sammo:%g", sep, e->ammo_after); break;
      case QA_MOVEMENT_EFFECT_EVENT: n += (size_t)snprintf(out + n, cap - n, "%sev:%g", sep, e->value.event); break;
      default: n += (size_t)snprintf(out + n, cap - n, "%sanimation", sep); break;
    }
  }
}
static void test_arsenal_effect_order(void) {
  qa_arsenal_state arsenal = qa_q3_spawn_loadout("q3:test", "baseq3", false);
  qa_q3_arsenal_runtime_state runtime = qa_q3_spawn_arsenal_runtime("baseq3", 100, 0); runtime.respawned = false;
  qa_actor_animation_state animation; memset(&animation, 0, sizeof animation);
  animation.provider = "q3:test"; animation.state = qa_q3_spawn_animation();
  const struct { bool attack; double weapon, ms; } steps[] = {{true, 2, 16}, {false, 2, 100}, {false, 1, 16}, {false, 1, 200}, {false, 1, 250}};
  void *owned_ammo = (void *)arsenal.ammo;
  for (size_t i = 0; i < sizeof steps / sizeof *steps; i++) {
    qa_weapon_step_input input; memset(&input, 0, sizeof input);
    input.frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS; input.frame.elapsed.value = steps[i].ms;
    input.arsenal = arsenal; input.animation = animation; input.environment.health = 100;
    qa_q3_arsenal_controls controls = {steps[i].attack, false, steps[i].weapon};
    qa_q3_arsenal_step r = qa_q3_step_arsenal(input, runtime, controls, NULL, NULL);
    char text[1024]; describe(&r, text, sizeof text);
    if (strcmp(text, ARSENAL_ORACLE[i]) != 0) fprintf(stderr, "step %zu: %s\n", i, text);
    CHECK(strcmp(text, ARSENAL_ORACLE[i]) == 0);
    free(owned_ammo); owned_ammo = (void *)r.base.arsenal.ammo;
    arsenal = r.base.arsenal; animation = r.base.animation; runtime = r.runtime;
    free((void *)r.base.effects); free((void *)r.torso_animations);
  }
  free(owned_ammo);
}

static void test_mirror_player_state_conversion(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_base_player_state *ps = &clients[1].ps;
  ps->client_num = 1; qa_q3_base_player_state_record_set_health(ps, 50); ps->pm_type = 0; qa_q3_base_player_state_record_set_origin(ps, (qa_vec3){1.5, -2.5, 3.5}); qa_q3_base_player_state_record_set_velocity(ps, (qa_vec3){4, 5, 6});
  ps->viewangles = (qa_vec3){7.5, 8, 9}; ps->movement_dir = 3; ps->legs_anim = 22; ps->torso_anim = 11; ps->e_flags = 1 | 0x40;
  ps->weapon = 5; ps->ground_entity_num = 1022; ps->loop_sound = 9; ps->generic1 = 2;
  qa_q3_base_player_state_slots_set(&ps->powerups, 1, 100); qa_q3_base_player_state_slots_set(&ps->powerups, 3, 100);
  qa_q3_base_player_state_record_add_event(ps, 40, 6);
  qa_q3_entity_state_record s; memset(&s, 0, sizeof s); s.pos.time = 77; s.pos.duration = 88;
  qa_q3_game_player_state_to_entity_state(ps, &s, true, false, 0);
  CHECK(s.e_type == 1); CHECK(s.number == 1); CHECK(s.client_num == 1);
  CHECK(s.pos.base.x == 1 && s.pos.base.y == -2 && s.pos.base.z == 3); CHECK(s.pos.time == 77 && s.pos.duration == 88);
  CHECK(s.pos.delta.y == 5); CHECK(s.apos.base.x == 7); CHECK(s.angles2.y == 3);
  CHECK(s.legs_anim == 22 && s.torso_anim == 11); CHECK(s.e_flags == 0x40);
  CHECK(s.event == 40); CHECK(s.event_parm == 6); CHECK(ps->entity_event_sequence == 1);
  CHECK(s.weapon == 5 && s.ground_entity_num == 1022); CHECK(s.powerups == ((1 << 1) | (1 << 3)));
  CHECK(s.loop_sound == 9 && s.generic1 == 2);
  qa_q3_base_player_state_record_set_health(ps, -41); ps->external_event = 61; ps->external_event_parm = 2;
  qa_q3_game_player_state_to_entity_state(ps, &s, false, true, 1234);
  CHECK(s.e_type == 10); CHECK(s.pos.time == 1234 && s.pos.duration == 50); CHECK(s.pos.base.x == 1.5);
  CHECK(s.event == 61 && s.event_parm == 2); CHECK(((int)s.e_flags & 1) == 1);
}

static void test_predictable_events(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  static qa_q3_game_combat_context combat; memset(&combat, 0, sizeof combat);
  combat.entities = &pool; combat.time = 1000;
  qa_q3_ta_event_context context = {&combat};
  qa_q3_base_player_state *ps = &clients[2].ps;
  ps->client_num = 2; qa_q3_base_player_state_record_set_health(ps, 80); ps->weapon = 7; qa_q3_base_player_state_record_set_origin(ps, (qa_vec3){10, 20, 30});
  ps->external_event = 55;
  qa_q3_ta_send_pending_predictable_events(context, ps);
  CHECK(pool.num_entities == 64 || released == 0);
  double spawned_before = 0;
  for (double i = 0; i < pool.num_entities; i++) if (qa_q3_game_entity_inuse(qa_q3_entity_pool_at(&pool, i))) spawned_before++;
  CHECK(spawned_before == 0);
  ps->external_event = 0;
  qa_q3_base_player_state_record_add_event(ps, 23, 0); /* EV_FIRE_WEAPON */
  ps->external_event = 55;
  qa_q3_ta_send_pending_predictable_events(context, ps);
  /* The external event is hidden during conversion, so the pending event is consumed. */
  CHECK(ps->entity_event_sequence == 1);
  CHECK(ps->external_event == 55);
  qa_q3_game_entity *temporary = NULL;
  for (double i = 0; i < pool.num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(&pool, i);
    if (qa_q3_game_entity_inuse(e)) temporary = e;
  }
  CHECK(temporary != NULL);
  if (temporary) {
    CHECK(temporary->s.weapon == 7); CHECK(temporary->s.client_num == 2); CHECK(temporary->s.other_entity_num == 2);
    CHECK(temporary->s.e_type == QA_Q3_BASE_ET_EVENTS + 23); CHECK(((int)temporary->s.e_flags & 0x10) != 0);
    CHECK(temporary->s.pos.base.y == 20); CHECK(temporary->s.number == temporary->slot);
  }
}

static void test_damage_feedback_fields(void) {
  pool_setup(QA_Q3_BASE_PRODUCT_BASEQ3);
  static qa_q3_game_combat_context combat; memset(&combat, 0, sizeof combat);
  combat.entities = &pool; combat.time = 1000;
  qa_q3_ta_event_context context = {&combat};
  qa_q3_game_entity *player = rec_activate(NULL, 3); player->client = &clients[3];
  qa_q3_game_client *c = &clients[3];
  c->damage_blood = 20; c->damage_armor = 5; c->damage_from_world = true;
  qa_q3_ta_damage_feedback(context, player);
  CHECK(c->ps.damage_pitch == 255 && c->ps.damage_yaw == 255); CHECK(c->ps.damage_count == 25);
  CHECK(c->ps.damage_event == 1); CHECK(c->damage_blood == 0 && c->damage_from_world == false);
  c->damage_blood = 400; c->damage_from = (qa_vec3){0, 1, 0};
  qa_q3_ta_damage_feedback(context, player);
  CHECK(c->ps.damage_yaw == 64); CHECK(c->ps.damage_pitch == 0); CHECK(c->ps.damage_count == 255);
  CHECK(c->ps.damage_event == 1); /* pain debounce holds until time passes 1700 */
}

int main(void) {
  test_death_drop_context();
  test_portal_drop_context();
  test_restore_level_votes();
  test_arsenal_effect_order();
  test_mirror_player_state_conversion();
  test_predictable_events();
  test_damage_feedback_fields();
  printf("test_q3_rows: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
