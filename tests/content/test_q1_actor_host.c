/*
 * C checks for createQ1ActorHost (src/app/bootstrap/simulation/source-hosts.ts)
 * now that it returns the one Q1FoundationHost (qa_q1_host): the spread
 * binding methods keep their own receiver, so every forwarded member must
 * reach the binding with the binding's ctx (sentinel), while trace/contents
 * are replaced by the world queries. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "qa/app/bootstrap/simulation/source_hosts.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int sentinel;
static int calls;
static double b_random(void *ctx) { CHECK(ctx == &sentinel); calls++; return 0.25; }
static bool b_walk_move(void *ctx, qa_owned_actor actor, double yaw, double distance) {
  (void)actor; CHECK(ctx == &sentinel); calls++; return yaw == 90 && distance == 8;
}
static void b_emit(void *ctx, qa_q1_event event) { CHECK(ctx == &sentinel); CHECK(strcmp(event.kind, "lightstyle") == 0); calls++; }
static qa_actor_id PLAYERS[2];
static const qa_actor_id *b_players(void *ctx, size_t *count) { CHECK(ctx == &sentinel); calls++; *count = 2; return PLAYERS; }
static const char *b_classname(void *ctx, qa_actor_id actor) { (void)actor; CHECK(ctx == &sentinel); calls++; return "player"; }
static bool b_check_client(void *ctx, qa_owned_actor observer, qa_actor_id *out) {
  (void)observer; CHECK(ctx == &sentinel); calls++; *out = PLAYERS[1]; return true;
}
static void b_powerup(void *ctx, qa_owned_actor actor, qa_q1_powerup powerup, double expires) {
  (void)actor; CHECK(ctx == &sentinel); CHECK(strcmp(powerup, "quad") == 0 && expires == 30); calls++;
}

int main(void) {
  memset(PLAYERS, 0, sizeof PLAYERS);
  PLAYERS[0].slot = 1;
  PLAYERS[1].slot = 2;
  qa_q1_host bindings;
  memset(&bindings, 0, sizeof bindings);
  bindings.ctx = &sentinel;
  bindings.random = b_random;
  bindings.walk_move = b_walk_move;
  bindings.emit = b_emit;
  bindings.players = b_players;
  bindings.classname = b_classname;
  bindings.check_client = b_check_client;
  bindings.powerup = b_powerup;
  qa_app_sim_actor_host_world world;
  memset(&world, 0, sizeof world);
  qa_app_sim_q1_actor_host_env env;
  const qa_q1_host host = qa_app_sim_create_q1_actor_host(&env, bindings, world);
  CHECK(host.ctx == &env);
  CHECK(host.trace != NULL && host.contents != NULL);
  /* Absent optional bindings stay absent (TS spread of an undefined member). */
  CHECK(host.set_gravity == NULL && host.monster_target == NULL && host.register_entity == NULL);
  CHECK(host.random(host.ctx) == 0.25);
  qa_owned_actor actor;
  memset(&actor, 0, sizeof actor);
  actor.id = PLAYERS[0];
  CHECK(host.walk_move(host.ctx, actor, 90, 8));
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "lightstyle";
  host.emit(host.ctx, event);
  size_t count = 0;
  const qa_actor_id *players = host.players(host.ctx, &count);
  CHECK(count == 2 && players == PLAYERS);
  CHECK(strcmp(host.classname(host.ctx, PLAYERS[0]), "player") == 0);
  qa_actor_id seen;
  memset(&seen, 0, sizeof seen);
  CHECK(host.check_client(host.ctx, actor, &seen) && qa_same_actor(seen, PLAYERS[1]));
  host.powerup(host.ctx, actor, "quad", 30);
  CHECK(calls == 7);
  printf("test_q1_actor_host: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
