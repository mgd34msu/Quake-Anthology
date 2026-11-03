/* Row-audit regressions for Q2 foundation weapons, alternate fly and entity
 * parsing. Expected values come from running the TypeScript under bun on the
 * same inputs (scratch oracle q2rows_oracle.ts):
 *   parse errors      -> "Q2 entity 0: expected opening brace" ...
 *   fireHit (rerelease) impact.x = 7.7927979570883279 (Math.hypot, not sqrt)
 *   classic fireRail through 18 piercing actors damages 18, each once
 *   alternate-fly visibility trace = binary32 sums (25.399999618530273 ...)
 * Every callback checks the sentinel ctx it was registered with.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/alternate_fly.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/core/random/q2_rerelease.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    checks++;                                                                    \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static char HOST_SENTINEL, GAME_SENTINEL, HOOKS_SENTINEL;
static int ctx_errors = 0;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) ctx_errors++; } while (0)

/* ---------------- real shared world tables ---------------- */
#define MAX_ACTORS 40
static struct {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_owned_actor owned[MAX_ACTORS]; /* index = registry slot */
  bool live;
} R;

static qa_owned_actor make_owned(int64_t slot) {
  if (slot < 0 || slot >= MAX_ACTORS || R.owned[slot].id.slot != slot) { fprintf(stderr, "no actor at slot %lld\n", (long long)slot); abort(); }
  return R.owned[slot];
}
static qa_actor_id make_id(int64_t slot) { return make_owned(slot).id; }
static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }

/* ---------------- world state shared by the fakes ---------------- */
static struct {
  int64_t monster_min;           /* slots >= this are monsters */
  /* rail: actors returned in order unless excluded/ignored */
  int64_t rail_first, rail_count;
  int traces;
  int damage_count; int64_t damaged[64]; qa_vec3 last_point;
  int visible_traces; qa_vec3 visible_start, visible_end;
  qa_q2_entity *enemy_entity;
} W;

static qa_body_state make_body(qa_vec3 origin, qa_vec3 min, qa_vec3 max) {
  qa_body_state b; memset(&b, 0, sizeof b);
  b.origin = origin; b.bounds.min = min; b.bounds.max = max;
  return b;
}
/* TS bodies.create for a test actor. */
static void put_body(int64_t slot, qa_body_state body) { qa_shared_body_table_create_body(R.bodies, make_owned(slot), body); }
static bool fake_is_monster(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx, HOST_SENTINEL); return actor.slot >= W.monster_min; }
static bool fake_is_player(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx, HOST_SENTINEL); (void)actor; return false; }
static double fake_now(void *ctx) { EXPECT_CTX(ctx, HOST_SENTINEL); return 1; }
static double fake_frame(void *ctx) { EXPECT_CTX(ctx, HOST_SENTINEL); return 0.025; }
static double fake_random(void *ctx) { EXPECT_CTX(ctx, HOST_SENTINEL); return 0.5; }
static double fake_contents(void *ctx, qa_vec3 p) { EXPECT_CTX(ctx, HOST_SENTINEL); (void)p; return 0; }
static void fake_emit(void *ctx, qa_q2_presentation_event e) { EXPECT_CTX(ctx, HOST_SENTINEL); (void)e; }

static qa_trace_result miss(qa_vec3 end) {
  qa_trace_result t; memset(&t, 0, sizeof t);
  t.kind = "q2"; t.fraction = 1; t.end = end; t.hit.kind = QA_TRACE_HIT_NONE;
  return t;
}
static bool excluded(const qa_q2_trace_request *r, int64_t slot) {
  for (size_t i = 0; i < r->exclude_count; i++) if (r->exclude[i].slot == slot) return true;
  return false;
}
static qa_trace_result fake_trace(void *ctx, qa_q2_trace_request r) {
  EXPECT_CTX(ctx, HOST_SENTINEL);
  W.traces++;
  if ((uint32_t)r.mask == 25u) { W.visible_traces++; W.visible_start = r.start; W.visible_end = r.end; }
  if (W.rail_count > 0 && W.traces <= 100) {
    for (int64_t slot = W.rail_first; slot < W.rail_first + W.rail_count; slot++) {
      if ((r.ignore && r.ignore->slot == slot) || excluded(&r, slot)) continue;
      qa_trace_result t = miss(v3((double)slot, 0, 0));
      t.fraction = 0.5; t.hit.kind = QA_TRACE_HIT_ACTOR; t.hit.actor = make_id(slot);
      return t;
    }
  }
  return miss(r.end);
}

static qa_body_state game_body(void *ctx, qa_owned_actor actor) {
  EXPECT_CTX(ctx, GAME_SENTINEL);
  qa_body_state_result body = qa_shared_body_table_read(R.bodies, actor.id);
  if (!body.has_value) { fprintf(stderr, "game.body on a missing body\n"); abort(); }
  return body.value;
}
static qa_damage_outcome game_damage(void *ctx, qa_actor_id target, qa_q2_entity *inflictor, qa_actor_id inflictor_id,
                                     const qa_actor_id *attacker, double amount, double knockback, qa_vec3 direction,
                                     qa_vec3 point, qa_vec3 normal, double means, double flags, qa_item_id weapon) {
  EXPECT_CTX(ctx, GAME_SENTINEL);
  (void)inflictor; (void)inflictor_id; (void)attacker; (void)amount; (void)knockback; (void)direction; (void)normal;
  (void)means; (void)flags; (void)weapon;
  if (W.damage_count < 64) W.damaged[W.damage_count] = target.slot;
  W.damage_count++; W.last_point = point;
  qa_damage_outcome o; memset(&o, 0, sizeof o); return o;
}
static qa_q2_weapon_target_result game_weapon_target(void *ctx, qa_actor_id actor) {
  EXPECT_CTX(ctx, GAME_SENTINEL); (void)actor;
  qa_q2_weapon_target_result r; memset(&r, 0, sizeof r); return r;
}
static qa_q2_entity *game_entity(void *ctx, const qa_actor_id *actor) {
  EXPECT_CTX(ctx, GAME_SENTINEL);
  return actor && W.enemy_entity && actor->slot == W.enemy_entity->actor.id.slot ? W.enemy_entity : NULL;
}
static void game_move(void *ctx, qa_q2_entity *e, qa_q2_body_changes c, bool link) {
  EXPECT_CTX(ctx, GAME_SENTINEL); (void)e; (void)c; (void)link;
}

static void hook_emit(void *ctx, qa_q2w_event e) { EXPECT_CTX(ctx, HOOKS_SENTINEL); (void)e; }
static void hook_noise(void *ctx, qa_actor_id a, qa_vec3 o, bool s) { EXPECT_CTX(ctx, HOOKS_SENTINEL); (void)a; (void)o; (void)s; }

static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) {
  (void)c; (void)a;
  return (qa_bounds){qa_add3_double(b.origin, b.bounds.min), qa_add3_double(b.origin, b.bounds.max)};
}
static void link_noop(void *c, qa_linked_body b) { (void)c; (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { (void)c; (void)a; }

static void world_teardown(void) {
  if (!R.live) return;
  qa_shared_inventory_table_destroy(R.inventory);
  qa_gameplay_authority_destroy(R.combat);
  qa_actor_callback_table_destroy(R.callbacks);
  qa_shared_body_table_destroy(R.bodies);
  qa_session_actor_registry_close(R.actors);
  qa_session_actor_registry_destroy(R.actors);
  qa_identity_owner_destroy(&R.identities);
  memset(&R, 0, sizeof R);
}
/* Every test actor gets the combat state the old stub reported for any
 * actor: health 10, damageable. */
static void world_setup(void) {
  world_teardown();
  qa_identity_owner_create(&R.identities, "q2-rows");
  R.actors = qa_session_actor_registry_create(&R.identities, 4096);
  qa_body_link_hooks link_hooks = {0};
  link_hooks.absolute_bounds = link_bounds; link_hooks.on_link = link_noop; link_hooks.on_unlink = unlink_noop;
  R.bodies = qa_shared_body_table_create(R.actors, link_hooks);
  R.callbacks = qa_actor_callback_table_create(R.actors);
  qa_gameplay_hooks hooks = {0};
  R.combat = qa_gameplay_authority_create(R.actors, R.callbacks, hooks);
  R.inventory = qa_shared_inventory_table_create(R.actors);
  R.live = true;
  for (int i = 0; i < MAX_ACTORS; i++) {
    qa_owned_actor actor = qa_session_actor_registry_allocate(R.actors, "q2:test", "q2:actor");
    if (actor.id.slot < 0 || actor.id.slot >= MAX_ACTORS) break;
    R.owned[actor.id.slot] = actor;
    qa_combat_state combat; memset(&combat, 0, sizeof combat);
    combat.health = 10; combat.can_take_damage = true;
    combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(R.combat, actor, combat, NULL, NULL);
  }
}

static qa_q2_foundation_host HOST;
static qa_q2_game_services GAME;
static void setup(qa_q2_edition edition) {
  memset(&W, 0, sizeof W);
  world_setup();
  W.monster_min = 2;
  memset(&HOST, 0, sizeof HOST);
  HOST.ctx = &HOST_SENTINEL;
  HOST.actors = R.actors; HOST.bodies = R.bodies; HOST.callbacks = R.callbacks;
  HOST.combat = R.combat; HOST.inventory = R.inventory;
  HOST.now = fake_now; HOST.frame_seconds = fake_frame; HOST.random = fake_random;
  HOST.trace = fake_trace; HOST.point_contents = fake_contents; HOST.is_player = fake_is_player;
  HOST.is_monster = fake_is_monster; HOST.emit = fake_emit;
  memset(&GAME, 0, sizeof GAME);
  GAME.ctx = &GAME_SENTINEL; GAME.host = &HOST;
  GAME.options.edition = edition; GAME.options.mode = QA_Q2_MODE_SINGLEPLAYER;
  GAME.body = game_body; GAME.damage = game_damage; GAME.weapon_target = game_weapon_target;
  GAME.entity = game_entity; GAME.move = game_move;
}
static qa_q2w_ballistics make_ballistics(void) {
  qa_q2w_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &HOOKS_SENTINEL; hooks.emit = hook_emit; hooks.noise = hook_noise;
  qa_q2w_ballistics b;
  CHECK(qa_q2w_ballistics_init(&b, hooks));
  return b;
}

/* TS fireHit (rerelease) measures range with Math.hypot. */
static void test_fire_hit_rerelease_hypot(void) {
  setup(QA_Q2_EDITION_RERELEASE);
  put_body(1, make_body(v3(0, 0, 0), v3(-1, -1, -1), v3(1, 1, 1)));
  put_body(2, make_body(v3(2.74, 4.76, 9.25), v3(-1, -1, -1), v3(1, 1, 1)));
  qa_q2w_ballistics b = make_ballistics();
  qa_q2_entity self; memset(&self, 0, sizeof self);
  self.actor = make_owned(1); self.has_enemy = true; self.enemy = make_id(2);
  CHECK(qa_q2w_ballistics_fire_hit(&b, &self, &GAME, v3(100, 0, 0), 5, 4));
  CHECK(W.damage_count == 1);
  CHECK(W.last_point.x == 7.7927979570883279);
  CHECK(W.last_point.y == 0 && W.last_point.z == 0);
  /* TS bodies.write(enemy, {...velocity}): the kick reached the shared body. */
  qa_body_state_result kicked = qa_shared_body_table_read(R.bodies, make_id(2));
  CHECK(kicked.has_value && (kicked.value.velocity.x != 0 || kicked.value.velocity.y != 0 || kicked.value.velocity.z != 0));
  qa_q2w_ballistics_destroy(&b);
}

/* TS classic fireRail keeps every pierced actor excluded (no 16 cap). */
static void test_classic_rail_exclusion(void) {
  setup(QA_Q2_EDITION_CLASSIC);
  W.monster_min = 10; W.rail_first = 10; W.rail_count = 18;
  put_body(1, make_body(v3(0, 0, 0), v3(-16, -16, -24), v3(16, 16, 32)));
  qa_q2w_ballistics b = make_ballistics();
  qa_q2w_ballistics_fire_rail(&b, make_owned(1), &GAME, v3(0, 0, 0), v3(1, 0, 0), 100, 0);
  CHECK(W.damage_count == 18);
  bool unique = true;
  for (int i = 0; i < W.damage_count && i < 64; i++)
    for (int j = i + 1; j < W.damage_count && j < 64; j++) if (W.damaged[i] == W.damaged[j]) unique = false;
  CHECK(unique);
  qa_q2w_ballistics_destroy(&b);
}

/* TS alternateFlyStep builds the visibility trace with the binary32 add. */
static void test_alternate_fly_visibility_points(void) {
  setup(QA_Q2_EDITION_RERELEASE);
  qa_q2_rerelease_random random;
  qa_q2_rerelease_random_init(&random, 1);
  HOST.rerelease_random = &random;
  qa_body_state flyer = make_body(v3(0.1, 0.2, 0.3), v3(-16, -16, -16), v3(16, 16, 16));
  flyer.velocity = v3(1, 0, 0);
  put_body(3, flyer);
  put_body(4, make_body(v3(100.1, 0.7, 1.3), v3(-16, -16, -24), v3(16, 16, 32)));
  qa_q2_entity enemy; memset(&enemy, 0, sizeof enemy);
  enemy.actor = make_owned(4); enemy.view_height = 22.7;
  W.enemy_entity = &enemy;
  qa_q2_entity entity; memset(&entity, 0, sizeof entity);
  entity.actor = make_owned(3); entity.has_enemy = true; entity.enemy = make_id(4);
  entity.flags = 1; entity.view_height = 25.1;
  qa_q2m_state *state = calloc(1, sizeof *state);
  CHECK(state != NULL);
  state->alt.fly_pinned = true; state->alt.fly_position_time = 100;
  state->alt.fly_acceleration = 10; state->alt.fly_speed = 100;
  state->alt.fly_min_distance = 32; state->alt.fly_max_distance = 64;
  qa_q2m_context context; memset(&context, 0, sizeof context);
  context.entity = &entity; context.game = &GAME; context.state = state;
  CHECK(qa_q2m_alternate_fly_step(&context));
  CHECK(W.visible_traces == 1);
  CHECK(W.visible_start.x == 0.10000000149011612 && W.visible_start.y == 0.20000000298023224);
  CHECK(W.visible_start.z == 25.399999618530273);
  CHECK(W.visible_end.x == 100.09999847412109 && W.visible_end.y == 0.69999998807907104);
  CHECK(W.visible_end.z == 24.0);
  free(state);
}

/* TS parse errors are plain Errors that startup selection catches. */
static void test_parse_entities_checked(void) {
  const char *inputs[] = {"x", "{ \"a\" \"b\"", "{ \"a\" }"};
  const char *messages[] = {"Q2 entity 0: expected opening brace", "Q2 entity 0: EOF without closing brace",
                            "Q2 entity 0: missing value for a"};
  for (size_t i = 0; i < 3; i++) {
    qa_q2_spawn_fields *fields = (qa_q2_spawn_fields *)&inputs; size_t count = 99;
    char message[128];
    CHECK(qa_q2_parse_entities_checked(inputs[i], QA_Q2_EDITION_CLASSIC, &fields, &count, message, sizeof message)
          == QA_Q2_PARSE_MALFORMED);
    CHECK(strcmp(message, messages[i]) == 0);
    CHECK(fields == NULL && count == 0);
  }
  qa_q2_spawn_fields *fields = NULL; size_t count = 0; char message[64];
  CHECK(qa_q2_parse_entities_checked("{ \"classname\" \"worldspawn\" }", QA_Q2_EDITION_CLASSIC, &fields, &count,
                                     message, sizeof message) == QA_Q2_PARSE_OK);
  CHECK(count == 1 && strcmp(fields[0].classname, "worldspawn") == 0);
  qa_q2_entities_free(fields, count);
}

int main(void) {
  test_fire_hit_rerelease_hypot();
  test_classic_rail_exclusion();
  test_alternate_fly_visibility_points();
  test_parse_entities_checked();
  world_teardown();
  CHECK(ctx_errors == 0);
  printf("test_q2_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
