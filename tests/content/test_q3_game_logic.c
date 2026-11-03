/*
 * C checks for src/content/q3/base/game/{combat,missile,mover,save-state}.ts.
 *  - test_combat_*: tests/gameplay/q3/base/combat.test.ts. The retail q3dm1 scene is replaced by a
 *    scripted spatial table: the TS case fires into empty space at (5000,5000,5000), so no map
 *    geometry reaches its assertions.
 *  - test_graph_*: tests/persistence/q3-native-graph.test.ts.
 *  - test_mover_*, test_missile_*: expected values come from running the TypeScript MoverRuntime and
 *    MissileRuntime over the same scripted fixture, since TS has no dedicated test file for them.
 * entity.health/takedamage and ps.stats forward to the authority, as in TS.
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

#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/q3/base/game/ground.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/game/mover.h"
#include "qa/content/q3/base/game/save_reader.h"
#include "qa/content/q3/base/game/save_state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/movement/q3/constants.h"
#include "qa/persistence/save_image.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/gameplay/policies.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { checks++; pid_t pid = fork(); if (pid == 0) { (void)prctl(PR_SET_DUMPABLE, 0); if (!freopen("/dev/null", "w", stderr)) _exit(1); __VA_ARGS__; _exit(0); } \
    int status = 0; waitpid(pid, &status, 0); if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)
#define CHECK_V(v, a, b, c) CHECK((v).x == (double)(float)(a) && (v).y == (double)(float)(b) && (v).z == (double)(float)(c))
static qa_vec3 V(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }

typedef struct trace_script { qa_q3_base_actor_trace_result items[8]; size_t count, next; } trace_script;
typedef struct fixture {
  qa_identity_owner identity; qa_session_actor_registry *actors; bool owns_registry;
  qa_shared_body_table *bodies; qa_actor_callback_table *callbacks; qa_gameplay_authority *combat; qa_shared_inventory_table *inventory;
  qa_q3_base_entity_records *records; qa_q3_entity_pool pool; qa_q3_base_combat_bridge bridge;
  qa_q3_base_actor_spatial_queries spatial; qa_q3_base_server_world world;
  double time, previous; int schedules; bool has_due; double due; qa_actor_id due_actor;
  trace_script traces; qa_actor_id area[8]; size_t area_count; int fired; qa_q3_entity_callback think_cb;
  bool ambient; double team;
} fixture;

static qa_bounds fx_abs(void *ctx, qa_owned_actor a, qa_body_state s) { (void)ctx; return qa_translated_body_bounds(a, s); }
static void fx_on_link(void *ctx, qa_linked_body b) { (void)ctx; (void)b; }
static void fx_on_unlink(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }
static void fx_impulse(void *ctx, qa_owned_actor a, qa_vec3 i, qa_provider_id m) {
  (void)m; fixture *f = ctx; qa_body_state_result b = qa_shared_body_table_read(f->bodies, a.id); if (!b.has_value) abort();
  b.value.velocity = V((float)(b.value.velocity.x + i.x), (float)(b.value.velocity.y + i.y), (float)(b.value.velocity.z + i.z));
  qa_shared_body_table_write(f->bodies, a, b.value);
}
static void fx_before(void *ctx, qa_owned_actor a, qa_damage_decision d) { (void)a; fixture *f = ctx; qa_q3_base_combat_bridge_before_reaction(&f->bridge, d); }
static void fx_confirmed(void *ctx, qa_damage_outcome o) { (void)ctx; (void)o; }
static void fx_schedule(void *ctx, qa_owned_actor a, bool has, double due) { fixture *f = ctx; f->schedules++; f->has_due = has; f->due = due; f->due_actor = a.id; }
static qa_frame_context frame_at(double time) {
  qa_frame_context c; memset(&c, 0, sizeof c); c.frame = trunc(time / 100); c.time.kind = QA_SOURCE_TIME_MILLISECONDS; c.time.value = time;
  c.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS; c.elapsed.value = 100; return c;
}
static void fx_run_think(void *ctx, qa_owned_actor a, double t) { fixture *f = ctx; (void)qa_actor_callback_table_think(f->callbacks, a, frame_at(t)); }
static bool fx_is_player(void *ctx, qa_actor_id a) { (void)ctx; (void)a; return false; }
static qa_q3_game_entity *fx_foreign(void *ctx, qa_actor_id a) { (void)ctx; (void)a; return NULL; }
static bool fx_damage_call(void *ctx, qa_q3_damage_call *out) { return qa_q3_base_combat_bridge_current_call(&((fixture *)ctx)->bridge, out); }
static double fx_time(void *ctx) { return ((fixture *)ctx)->time; }
static void fx_print(void *ctx, const char *m) { (void)ctx; (void)m; }
static void fx_link(void *ctx, qa_q3_game_entity *e) {
  fixture *f = ctx; qa_owned_actor a = qa_q3_game_entity_actor(e); qa_vec3 origin = qa_q3_base_entity_shared_current_origin(&e->r);
  qa_shared_body_table_link(f->bodies, a, &origin); qa_q3_base_entity_shared_capture_link(&e->r);
}
static void fx_unlink_number(void *ctx, double number) {
  fixture *f = ctx; qa_q3_game_entity *e = qa_q3_entity_pool_at(&f->pool, number); if (qa_q3_game_entity_inuse(e)) qa_shared_body_table_unlink(f->bodies, qa_q3_game_entity_actor(e));
}
static void fx_pool_unlink(void *ctx, qa_q3_game_entity *e) { fx_unlink_number(ctx, e->slot); }
static qa_q3_base_actor_trace_result fx_trace(void *ctx, qa_q3_base_actor_trace_query q) {
  fixture *f = ctx;
  if (f->traces.next < f->traces.count) return f->traces.items[f->traces.next++];
  qa_q3_base_actor_trace_result t; memset(&t, 0, sizeof t); t.fraction = 1; t.end = q.end; t.solidity = QA_Q3_BASE_SOLID_CLEAR; t.hit_kind = QA_Q3_BASE_HIT_NONE; return t;
}
static const qa_actor_id *fx_area(void *ctx, qa_bounds b, double m, size_t *count) { (void)b; (void)m; fixture *f = ctx; *count = f->area_count; return f->area; }
static bool fx_contact(void *ctx, qa_bounds b, qa_actor_id a, bool c) { (void)ctx; (void)b; (void)a; (void)c; return false; }
static qa_victim_armor_context fx_armor_ctx(void *ctx, qa_damage_request r) { (void)ctx; (void)r; qa_victim_armor_context c; memset(&c, 0, sizeof c); c.screen_facing_dot = 1; c.arithmetic = "binary32"; return c; }
static double fx_zero(void *ctx) { (void)ctx; return 0; }
static double fx_game_type(void *ctx) { return ((fixture *)ctx)->team; }
static bool fx_false(void *ctx) { (void)ctx; return false; }
static double fx_knock(void *ctx) { (void)ctx; return 1000; }
static void fx_hurt_carrier(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) { (void)ctx; (void)t; (void)a; }
static bool fx_log_acc(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) { (void)ctx; (void)t; (void)a; return true; }
static bool fx_is_live(void *ctx, qa_actor_id a) { return qa_session_actor_registry_is_live(((fixture *)ctx)->actors, a); }
static void fx_apply(void *ctx, qa_actor_id t, double a, double k, qa_vec3 d, qa_vec3 p, bool r) { (void)ctx; (void)t; (void)a; (void)k; (void)d; (void)p; (void)r; abort(); }
static void fx_feedback(void *ctx, qa_q3_damage_call call, qa_damage_decision d) { fixture *f = ctx; qa_q3_damage_feedback(qa_q3_base_combat_bridge_context(&f->bridge), call, d); }
static void fx_foreign_feedback(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a, qa_damage_decision d) { fixture *f = ctx; qa_q3_foreign_damage_feedback(qa_q3_base_combat_bridge_context(&f->bridge), t, a, d); }
static bool fx_parent(void *ctx, qa_actor_id a, qa_actor_id *o) { (void)ctx; (void)a; (void)o; return false; }
static bool fx_obelisk(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) { (void)ctx; (void)t; (void)a; return false; }
static void fx_invuln(void *ctx, qa_q3_game_entity *t, qa_vec3 d, qa_vec3 p) { (void)ctx; (void)t; (void)d; (void)p; }
static qa_q3_game_combat_context *fx_combat(void *ctx) { return qa_q3_base_combat_bridge_context(&((fixture *)ctx)->bridge); }
static void fx_think_count(void *ctx, qa_q3_game_entity *self) { fixture *f = ctx; f->fired++; self->count++; }

static void fx_open(fixture *f, qa_q3_base_product product, double max_clients, qa_session_actor_registry *actors) {
  memset(f, 0, sizeof *f); f->time = 1000; f->previous = 900;
  if (actors) f->actors = actors;
  else { if (!qa_identity_owner_create(&f->identity, "q3-game-logic")) abort(); f->actors = qa_session_actor_registry_create(&f->identity, 2048); f->owns_registry = true; }
  qa_body_link_hooks hooks; memset(&hooks, 0, sizeof hooks); hooks.absolute_bounds = fx_abs; hooks.on_link = fx_on_link; hooks.on_unlink = fx_on_unlink;
  f->bodies = qa_shared_body_table_create(f->actors, hooks);
  f->callbacks = qa_actor_callback_table_create(f->actors);
  qa_gameplay_hooks g; memset(&g, 0, sizeof g); g.ctx = f; g.impulse = fx_impulse; g.before_reaction = fx_before; g.confirmed = fx_confirmed;
  f->combat = qa_gameplay_authority_create(f->actors, f->callbacks, g);
  f->inventory = qa_shared_inventory_table_create(f->actors);
  qa_q3_base_record_host rh; memset(&rh, 0, sizeof rh);
  rh.actors = f->actors; rh.bodies = f->bodies; rh.combat = f->combat; rh.inventory = f->inventory; rh.callbacks = f->callbacks; rh.ctx = f;
  rh.schedule = fx_schedule; rh.run_think = fx_run_think; rh.damage_call = fx_damage_call; rh.foreign = fx_foreign; rh.is_player = fx_is_player;
  f->records = qa_q3_base_entity_records_create(rh, "q3:logic", product);
  qa_q3_entity_pool_options po; memset(&po, 0, sizeof po);
  po.records = qa_q3_base_entity_records_pool_table(f->records); po.product = product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  po.max_clients = max_clients; po.ctx = f; po.time = fx_time; po.print = fx_print; po.link = fx_link; po.unlink = fx_pool_unlink;
  qa_q3_entity_pool_init(&f->pool, po);
  f->world.ctx = f; f->world.link = fx_link; f->world.unlink = fx_unlink_number;
  f->spatial.ctx = f; f->spatial.trace_actor = fx_trace; f->spatial.area_actors = fx_area; f->spatial.contact_actor = fx_contact;
  qa_q3_base_combat_bridge_host h; memset(&h, 0, sizeof h);
  h.product = product; h.authority = f->combat; h.entities = &f->pool; h.records = f->records; h.spatial = &f->spatial;
  h.weapon_provider = "q3:weapons"; h.combat_provider = "q3:combat"; h.inventory_provider = "q3:inventory"; h.movement_provider = "q3:movement"; h.ctx = f;
  h.armor_context = fx_armor_ctx; h.time = fx_time; h.intermission_queued = fx_zero; h.game_type = fx_game_type; h.friendly_fire = fx_false; h.knockback = fx_knock;
  h.check_hurt_carrier = fx_hurt_carrier; h.log_accuracy_hit = fx_log_acc; h.is_live = fx_is_live; h.apply_damage = fx_apply; h.damage_feedback = fx_feedback;
  h.foreign_damage_feedback = fx_foreign_feedback; h.projectile_parent = fx_parent; h.check_obelisk_attack = fx_obelisk; h.invulnerability_effect = fx_invuln;
  qa_q3_base_combat_bridge_init(&f->bridge, h);
  qa_q3_base_combat_bridge_bind_game_services(&f->bridge);
  qa_combat_policy policy = qa_q3_base_combat_bridge_policy(&f->bridge);
  (void)qa_gameplay_authority_register(f->combat, policy);
  qa_q3_entity_think_fn think = fx_think_count; qa_q3_entity_callback cb; memcpy(&cb.fn, &think, sizeof cb.fn); cb.ctx = f;
  f->think_cb = qa_q3_callback_family_register(&f->pool.callbacks.think, "test:think", cb);
}
/* TS entity.health / entity.takedamage setters. */
static void set_vital(fixture *f, qa_q3_game_entity *e, double health) {
  qa_q3_game_entity_set_health(e, health); qa_q3_game_entity_set_takedamage(e, true);
  qa_owned_actor a = qa_q3_game_entity_actor(e);
  qa_combat_trait_changes t; memset(&t, 0, sizeof t); t.has_can_take_damage = true; t.can_take_damage = true;
  qa_gameplay_authority_set_traits(f->combat, a, t); qa_gameplay_authority_set_health(f->combat, a, health);
  if (e->client) qa_q3_base_player_state_record_set_health(&e->client->ps, health);
}
static void set_armor(fixture *f, qa_q3_game_entity *e, size_t slot, double points) {
  qa_q3_base_player_state_slots_set(&e->client->ps.stats, slot, points);
  qa_regular_armor_state initial; memset(&initial, 0, sizeof initial); initial.kind = QA_REGULAR_ARMOR_Q3; initial.points = points; initial.protection = (float)0.66;
  qa_gameplay_authority_set_regular_points(f->combat, qa_q3_game_entity_actor(e), points, &initial);
}
static void set_bounds(fixture *f, qa_q3_game_entity *e, qa_vec3 mins, qa_vec3 maxs) {
  qa_q3_base_entity_shared_set_mins(&e->r, mins); qa_q3_base_entity_shared_set_maxs(&e->r, maxs); e->r.model.kind = 1; qa_owned_actor a = qa_q3_game_entity_actor(e);
  qa_body_state_result b = qa_shared_body_table_read(f->bodies, a.id); b.value.bounds.min = mins; b.value.bounds.max = maxs; qa_shared_body_table_write(f->bodies, a, b.value);
}
static void place(fixture *f, qa_q3_game_entity *e, qa_vec3 origin) { qa_q3_set_origin(e, origin); fx_link(f, e); }
static double health_of(fixture *f, qa_q3_game_entity *e) { return qa_gameplay_authority_read(f->combat, qa_q3_game_entity_actor(e).id).value.health; }
static qa_vec3 velocity_of(fixture *f, qa_q3_game_entity *e) { return qa_shared_body_table_read(f->bodies, qa_q3_game_entity_actor(e).id).value.velocity; }
static qa_vec3 body_origin(fixture *f, qa_q3_game_entity *e) { return qa_shared_body_table_read(f->bodies, qa_q3_game_entity_actor(e).id).value.origin; }
static void script_plane(fixture *f, double fraction, qa_vec3 end, qa_vec3 normal, double surface, qa_q3_base_actor_hit_kind kind, qa_actor_id actor) {
  qa_q3_base_actor_trace_result t; memset(&t, 0, sizeof t); t.fraction = fraction; t.end = end; t.solidity = QA_Q3_BASE_SOLID_CLEAR; t.contact_is_plane = true;
  t.contact_plane.normal = normal; t.contents = 1; t.surface_flags = surface; t.hit_kind = kind; t.hit_actor = actor;
  f->traces.count = 1; f->traces.next = 0; f->traces.items[0] = t;
}
static qa_actor_id no_actor(void) { qa_actor_id a; memset(&a, 0, sizeof a); return a; }

/* tests/gameplay/q3/base/combat.test.ts */
static void foreign_hit(fixture *f, qa_q3_game_entity *attacker, qa_q3_game_entity *victim, double amount, double flags, bool radius) {
  qa_damage_request r; memset(&r, 0, sizeof r); qa_actor_id a = qa_q3_game_entity_actor(attacker).id;
  r.attack.sequence = 12; r.attack.time.kind = QA_SOURCE_TIME_SECONDS; r.attack.time.value = 0.1; r.attack.attacker = &a; r.attack.inflictor = &a;
  r.attack.weapon = "q2:weapon_shotgun"; r.attack.weapon_provider = "q2:official"; r.attack.combat_provider = "q3:combat"; r.attack.inventory_provider = "q3:inventory"; r.attack.movement_provider = "q3:movement";
  r.attack.cause.kind = QA_ATTACK_CAUSE_Q2; r.attack.cause.means_of_death = 2; r.attack.cause.damage_flags = flags;
  r.target = qa_q3_game_entity_actor(victim).id; r.amount = amount; r.knockback = 2; r.direction = V(2, 0, 0); r.point = qa_q3_base_entity_shared_current_origin(&victim->r); r.delivery = radius ? "radius" : "direct";
  (void)qa_gameplay_authority_apply(f->combat, r, NULL, NULL);
}
typedef struct dispatch_op { fixture *f; qa_q3_game_entity *attacker, *victim; } dispatch_op;
static qa_damage_outcome dispatch_foreign(void *ctx) { dispatch_op *o = ctx; foreign_hit(o->f, o->attacker, o->victim, 8, 0, false); qa_damage_outcome none; memset(&none, 0, sizeof none); return none; }
static void test_combat_authority(void) {
  fixture fx, *f = &fx; fx_open(f, QA_Q3_BASE_PRODUCT_BASEQ3, 2, NULL); f->time = 100;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_game_entity *attacker = qa_q3_entity_pool_activate_client(&f->pool, 0), *victim = qa_q3_entity_pool_activate_client(&f->pool, 1);
  qa_q3_init_game_entity(attacker); qa_q3_init_game_entity(victim);
  set_vital(f, attacker, 100); set_vital(f, victim, 100);
  qa_q3_game_client *ca = qa_q3_entity_pool_client_at(&f->pool, 0), *cv = qa_q3_entity_pool_client_at(&f->pool, 1);
  qa_q3_base_player_state_slots_set(&ca->ps.stats, (size_t)schema->max_health, 100);
  set_armor(f, victim, (size_t)schema->armor, 100);
  qa_q3_game_combat_context *c = qa_q3_base_combat_bridge_context(&f->bridge);
  qa_q3_damage_participant pa = qa_q3_participant_entity(attacker), pv = qa_q3_participant_entity(victim);
  qa_vec3 dir = V(2, 0, 0), point = V(0, 0, 0);
  qa_q3_combat_damage(c, pv, &pa, &pa, &dir, &point, 50, 0, 3);
  CHECK(dir.x == 1); /* G_Damage normalizes its caller's direction. */
  CHECK(health_of(f, victim) == 83);
  CHECK(qa_q3_base_player_state_slots_get(&cv->ps.stats, (size_t)schema->armor) == 67);
  CHECK(cv->damage_blood == 17); CHECK(cv->damage_armor == 33);
  CHECK(velocity_of(f, victim).x == 250);
  qa_q3_base_player_state_slots_set(&cv->ps.powerups, QA_Q3_PW_BATTLESUIT, 10000);
  qa_vec3 dir2 = V(1, 0, 0);
  qa_q3_combat_damage(c, pv, &pa, &pa, &dir2, NULL, 100, QA_Q3_DAMAGE_RADIUS, 7);
  CHECK(health_of(f, victim) == 83);
  CHECK(((int)cv->ps.external_event & 255) == 62);
  attacker->s.e_type = victim->s.e_type = QA_Q3_BASE_ET_PLAYER;
  qa_q3_base_player_state_slots_set(&cv->ps.powerups, QA_Q3_PW_BATTLESUIT, 0);
  set_armor(f, victim, (size_t)schema->armor, 0);
  cv->ps.pm_time = 0;
  double hits = qa_q3_base_player_state_slots_get(&ca->ps.persistant, QA_Q3_BASE_PERS_HITS), blood = cv->damage_blood, knockback = cv->damage_knockback;
  foreign_hit(f, attacker, victim, 8, 0, false);
  CHECK(qa_q3_game_entity_health(victim) == 75);
  CHECK(qa_q3_base_player_state_slots_get(&ca->ps.persistant, QA_Q3_BASE_PERS_HITS) == hits + 1);
  CHECK(cv->damage_blood == blood + 8); CHECK(cv->damage_knockback == knockback + 8); CHECK(cv->ps.pm_time == 50);
  CHECK(cv->last_hurt_client == attacker->s.number); CHECK(cv->last_hurt_mod == 0);
  CHECK(cv->damage_from.x == 1 && cv->damage_from.y == 0 && cv->damage_from.z == 0);
  cv->ps.pm_time = 0; double velocity = velocity_of(f, victim).x;
  foreign_hit(f, attacker, victim, 8, 8, false);
  CHECK(cv->ps.pm_time == 0); CHECK(cv->damage_knockback == knockback + 8); CHECK(velocity_of(f, victim).x == velocity);
  set_armor(f, victim, (size_t)schema->armor, 100);
  double armor_blood = cv->damage_blood, armor_saved = cv->damage_armor;
  foreign_hit(f, attacker, victim, 1, 0, false);
  CHECK(cv->damage_blood == armor_blood); CHECK(cv->damage_armor == armor_saved + 1);
  CHECK(qa_q3_base_player_state_slots_get(&ca->ps.persistant, QA_Q3_BASE_PERS_HITS) == hits + 3);
  victim->flags = (int)victim->flags | QA_Q3_GAME_FLAG_GODMODE;
  qa_q3_base_player_state_slots_set(&cv->ps.powerups, QA_Q3_PW_BATTLESUIT, 10000); cv->ps.pm_time = 0;
  double event = cv->ps.external_event;
  foreign_hit(f, attacker, victim, 30, 0, false);
  CHECK(cv->ps.pm_time == 60); CHECK(cv->ps.external_event == event);
  CHECK(qa_q3_base_player_state_slots_get(&ca->ps.persistant, QA_Q3_BASE_PERS_HITS) == hits + 3);
  victim->flags = (int)victim->flags & ~QA_Q3_GAME_FLAG_GODMODE; cv->ps.pm_time = 0;
  foreign_hit(f, attacker, victim, 100, 1, true);
  CHECK(cv->ps.pm_time == 200); CHECK(((int)cv->ps.external_event & 255) == 62);
  CHECK(cv->damage_blood == armor_blood);
  CHECK(qa_q3_base_player_state_slots_get(&ca->ps.persistant, QA_Q3_BASE_PERS_HITS) == hits + 3);
  qa_q3_base_player_state_slots_set(&cv->ps.powerups, QA_Q3_PW_BATTLESUIT, 0); cv->ps.pm_time = 0;
  qa_q3_damage_call call = {victim, pa, pa, NULL, NULL, 1, QA_Q3_DAMAGE_NO_KNOCKBACK, 7};
  dispatch_op op = {f, attacker, victim}; (void)c->dispatch(c->services_ctx, call, dispatch_foreign, &op);
  CHECK(cv->ps.pm_time == 50); CHECK(cv->last_hurt_mod == 0);
  CHECK(qa_q3_base_player_state_slots_get(&ca->ps.persistant, QA_Q3_BASE_PERS_HITS) == hits + 4);
  /* Grenade expiry through the scheduled think. */
  qa_q3_missile_runtime missiles; qa_q3_missile_host mh; memset(&mh, 0, sizeof mh);
  mh.ctx = f; mh.product = QA_Q3_BASE_PRODUCT_BASEQ3; mh.combat = fx_combat; mh.actors = f->actors; mh.bodies = f->bodies; mh.spatial = &f->spatial; mh.world = f->world;
  qa_q3_missile_runtime_init(&missiles, mh);
  place(f, attacker, V(5000, 5000, 5000));
  qa_q3_missile_direction gd = {1, 0, 0};
  qa_q3_game_entity *grenade = qa_q3_missile_runtime_fire_grenade(&missiles, attacker, qa_q3_base_entity_shared_current_origin(&attacker->r), &gd);
  CHECK(f->has_due && f->due == 2600 && qa_same_actor(f->due_actor, qa_q3_game_entity_actor(grenade).id));
  CHECK(grenade->s.pos.delta.x == 700);
  f->time = 2600;
  CHECK(qa_actor_callback_table_think(f->callbacks, qa_q3_game_entity_actor(grenade), frame_at(2600)));
  CHECK(grenade->free_after_event);
  CHECK(((int)grenade->s.event & 255) == 51);
  qa_q3_missile_runtime_close(&missiles);
}
/* Non-entity target, intermission, requires-services, callback ctx. */
static double g_seen_amount; static void *g_seen_ctx;
static void sentinel_apply(void *ctx, qa_actor_id t, double a, double k, qa_vec3 d, qa_vec3 p, bool r) { (void)t; (void)k; (void)d; (void)p; (void)r; g_seen_ctx = ctx; g_seen_amount = a; }
static void test_combat_context_edges(void) {
  fixture fx, *f = &fx; fx_open(f, QA_Q3_BASE_PRODUCT_BASEQ3, 2, NULL);
  qa_q3_game_entity *e = qa_q3_entity_pool_activate_client(&f->pool, 0); qa_q3_init_game_entity(e); set_vital(f, e, 100);
  qa_q3_game_combat_context bare; memset(&bare, 0, sizeof bare); bare.entities = &f->pool; int sentinel = 0; bare.ctx = &sentinel; bare.apply_damage = sentinel_apply;
  qa_q3_combat_damage(&bare, qa_q3_participant_entity(e), NULL, NULL, NULL, NULL, 40, 0, 1);
  CHECK(g_seen_ctx == &sentinel && g_seen_amount == 40);
  bare.intermission_queued = 5; g_seen_amount = 0;
  qa_q3_combat_damage(&bare, qa_q3_participant_entity(e), NULL, NULL, NULL, NULL, 40, 0, 1);
  CHECK(g_seen_amount == 0);
  bare.intermission_queued = 0;
  CHECK_ABORTS(qa_q3_combat_can_damage(&bare, qa_q3_participant_entity(e), V(0, 0, 0)));
  CHECK(qa_q3_check_armor(e, 0, 0) == 0);
}

/* MoverRuntime: values from the TS oracle. */
typedef struct mover_log { int uses, last_use_slot; int portal_open, portal_closed; int flag_returns; } mover_log;
typedef struct mover_fx { fixture *f; mover_log log; } mover_fx;
static qa_q3_game_combat_context *mv_combat(void *ctx) { return fx_combat(((mover_fx *)ctx)->f); }
static void mv_use(void *ctx, qa_q3_game_entity *e, qa_q3_use_participant a) { (void)a; mover_fx *m = ctx; m->log.uses++; m->log.last_use_slot = (int)e->slot; }
static void mv_portal(void *ctx, qa_q3_game_entity *e, bool open) { (void)e; mover_fx *m = ctx; if (open) m->log.portal_open++; else m->log.portal_closed++; }
static void mv_flag(void *ctx, qa_q3_game_entity *e) { (void)e; ((mover_fx *)ctx)->log.flag_returns++; }
static qa_q3_game_entity *ma_native(void *ctx, qa_actor_id a) { return qa_q3_base_entity_records_native_by_actor(((mover_fx *)ctx)->f->records, true, a); }
static qa_q3_damage_participant ma_participant(void *ctx, qa_actor_id a) { return qa_q3_base_entity_records_damage_inflictor(((mover_fx *)ctx)->f->records, true, a); }
static bool ma_observe(void *ctx, qa_actor_id id, qa_q3_shared_mover_body *out) {
  fixture *f = ((mover_fx *)ctx)->f; qa_owned_actor a;
  if (!qa_session_actor_registry_resolve_owned(f->actors, id, &a)) return false;
  qa_body_state_result s = qa_shared_body_table_read(f->bodies, id); qa_linked_body_result l = qa_shared_body_table_linked(f->bodies, id);
  if (!s.has_value || !l.has_value) return false;
  qa_q3_game_entity *n = qa_q3_base_entity_records_native_by_actor(f->records, true, id);
  out->actor = a; out->kind = n && n->client ? QA_Q3_MOVER_BODY_PLAYER : QA_Q3_MOVER_BODY_MOVABLE; out->state = s.value; out->absolute_bounds = l.value.absolute_bounds; out->clip_mask = 1; return true;
}
static void ma_write(void *ctx, qa_owned_actor a, qa_vec3 origin, const qa_actor_id *ground) {
  fixture *f = ((mover_fx *)ctx)->f; qa_body_state_result b = qa_shared_body_table_read(f->bodies, a.id);
  b.value.origin = origin; b.value.ground = ground; qa_shared_body_table_write(f->bodies, a, b.value);
}
static void ma_link(void *ctx, qa_owned_actor a) { qa_shared_body_table_link(((mover_fx *)ctx)->f->bodies, a, NULL); }
static void ma_release(void *ctx, qa_owned_actor a) { qa_session_actor_registry_release(((mover_fx *)ctx)->f->actors, a); }
static int g_blocked_slot;
static void cb_blocked(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant o) { (void)ctx; (void)self; g_blocked_slot = o.entity ? (int)o.entity->slot : -2; }
static void test_movers(void) {
  fixture fx, *f = &fx; fx_open(f, QA_Q3_BASE_PRODUCT_BASEQ3, 2, NULL);
  static qa_q3_config_string_registry config; memset(&config, 0, sizeof config);
  mover_fx mf; memset(&mf, 0, sizeof mf); mf.f = f;
  qa_q3_mover_host mh; memset(&mh, 0, sizeof mh);
  mh.ctx = &mf; mh.product = QA_Q3_BASE_PRODUCT_BASEQ3; mh.combat = mv_combat; mh.world = f->world; mh.spatial = &f->spatial; mh.bodies = f->bodies; mh.previous_time = f->previous;
  mh.use_targets = mv_use; mh.adjust_area_portal_state = mv_portal; mh.return_dropped_flag = mv_flag;
  mh.actors.ctx = &mf; mh.actors.native = ma_native; mh.actors.participant = ma_participant; mh.actors.observe = ma_observe; mh.actors.write = ma_write; mh.actors.link = ma_link; mh.actors.release = ma_release;
  qa_q3_mover_runtime m; qa_q3_mover_runtime_init(&m, mh);
  qa_q3_game_entity *door = qa_q3_entity_pool_spawn(&f->pool);
  door->pos1 = V(0, 0, 0); door->pos2 = V(30, 40, 120); door->wait = 2000; door->sound_pos2 = 11; door->sound_1to2 = 12; door->sound_2to1 = 13; door->sound_pos1 = 14; door->sound_loop = 15;
  set_bounds(f, door, V(-10, -10, -10), V(10, 10, 10));
  /* initializeBinary without model2/noise so the config registry stays out; packing is checked below. */
  qa_q3_spawn_pair pairs[2] = {{(char *)"light", (char *)"300"}, {(char *)"color", (char *)"1 0.5 0.25"}};
  qa_q3_spawn_variables vars; qa_q3_spawn_variables_init(&vars, pairs, 2);
  qa_q3_mover_initialize_binary(&m, door, &vars);
  CHECK(door->s.constant_light == 1262452735); CHECK(door->s.pos.duration == 1300); CHECK(door->speed == 100);
  CHECK_V(door->s.pos.delta, 3000, 4000, 12000); CHECK(door->r.sv_flags == 128 && door->s.e_type == 4 && door->s.pos.type == 0);
  CHECK_V(qa_q3_base_entity_shared_current_origin(&door->r), 0, 0, 0);
  CHECK_V(body_origin(f, door), 0, 0, 0);
  f->time = 1000; qa_q3_mover_use_binary(&m, door, NULL, NULL);
  CHECK(door->mover_state == 2 && door->s.pos.time == 1050 && door->s.pos.type == 3);
  CHECK(door->s.pos.delta.x == 23.076923370361328 && door->s.pos.delta.y == 30.76923179626465 && door->s.pos.delta.z == 92.30769348144531);
  CHECK(door->s.event == 301 && door->s.event_parm == 12); CHECK(mf.log.portal_open == 1);
  CHECK_V(qa_q3_base_entity_shared_current_origin(&door->r), 0, 0, 0);
  qa_q3_damage_participant self = qa_q3_participant_entity(door);
  f->time = 1300; qa_q3_mover_use_binary(&m, door, &self, &self);
  CHECK(door->mover_state == 3 && door->s.pos.time == 250); CHECK_V(door->s.pos.base, 30, 40, 120);
  CHECK(door->s.pos.delta.x == -23.076923370361328 && door->s.pos.delta.z == -92.30769348144531);
  CHECK(qa_q3_base_entity_shared_current_origin(&door->r).x == 5.769227981567383 && qa_q3_base_entity_shared_current_origin(&door->r).y == 7.692302703857422 && qa_q3_base_entity_shared_current_origin(&door->r).z == 23.07691192626953);
  f->time = 1200; qa_q3_mover_use_binary(&m, door, &self, &self);
  CHECK(door->mover_state == 2 && door->s.pos.time == 850);
  CHECK(qa_q3_base_entity_shared_current_origin(&door->r).x == 8.076923370361328 && qa_q3_base_entity_shared_current_origin(&door->r).y == 10.769231796264648 && qa_q3_base_entity_shared_current_origin(&door->r).z == 32.30769348144531);
  door->mover_state = QA_Q3_MOVER_ONE_TO_TWO; f->time = 5000; door->has_activation = false; qa_q3_mover_reached_binary(&m, door);
  CHECK(door->mover_state == 1 && door->nextthink == 7000 && door->s.pos.type == 0); CHECK_V(door->s.pos.base, 30, 40, 120);
  CHECK(mf.log.uses == 1 && mf.log.last_use_slot == (int)door->slot); CHECK(door->has_activation && door->activation.entity == door);
  CHECK(door->think.fn != NULL);
  qa_q3_mover_use_binary(&m, door, &self, &self); CHECK(door->nextthink == 7000);
  f->time = 5200; qa_q3_mover_return_to_pos1(&m, door);
  CHECK(door->mover_state == 3 && door->s.loop_sound == 15 && door->s.event == 301 && door->s.event_parm == 13);
  door->mover_state = QA_Q3_MOVER_TWO_TO_ONE; mf.log.portal_closed = 0; qa_q3_mover_reached_binary(&m, door);
  CHECK(door->mover_state == 0 && mf.log.portal_closed == 1); CHECK_V(door->s.pos.base, 0, 0, 0);
  door->mover_state = QA_Q3_MOVER_POS1; CHECK_ABORTS(qa_q3_mover_reached_binary(&m, door));
  /* Rotating team push of a riding item. */
  qa_q3_game_entity *rot = qa_q3_entity_pool_spawn(&f->pool), *item = qa_q3_entity_pool_spawn(&f->pool);
  set_bounds(f, rot, V(-32, -32, -8), V(32, 32, 8)); place(f, rot, V(100, 0, 0));
  qa_q3_base_trajectory pos = {QA_Q3_BASE_TR_LINEAR, 1000, 0, V(100, 0, 0), V(10, 20, 0)}, apos = {QA_Q3_BASE_TR_LINEAR, 1000, 0, V(0, 0, 0), V(0, 90, 0)};
  rot->s.pos = pos; rot->s.apos = apos;
  item->s.e_type = QA_Q3_BASE_ET_ITEM; set_bounds(f, item, V(-4, -4, -4), V(4, 4, 4)); place(f, item, V(140, 10, 20));
  qa_actor_id rot_actor = qa_q3_game_entity_actor(rot).id; qa_q3_write_ground(item, &rot_actor, &f->pool);
  f->area[0] = rot_actor; f->area[1] = qa_q3_game_entity_actor(item).id; f->area_count = 2; f->time = 2000;
  qa_q3_mover_run_team(&m, rot);
  CHECK_V(qa_q3_base_entity_shared_current_origin(&rot->r), 110, 20, 0); CHECK_V(qa_q3_base_entity_shared_current_angles(&rot->r), 0, 90, 0);
  CHECK_V(item->s.pos.base, 130, 70, 20); CHECK_V(qa_q3_base_entity_shared_current_origin(&item->r), 130, 70, 20); CHECK(qa_q3_rides(item, rot));
  /* Blocked team: restore, retime by frame length, blocked callback. */
  qa_q3_game_entity *pusher = qa_q3_entity_pool_spawn(&f->pool), *blocker = qa_q3_entity_pool_spawn(&f->pool);
  qa_q3_entity_blocked_fn bfn = cb_blocked; qa_q3_entity_callback bcb; memcpy(&bcb.fn, &bfn, sizeof bcb.fn); bcb.ctx = f;
  qa_q3_callback_family_register(&f->pool.callbacks.blocked, "t:b", bcb); pusher->blocked = qa_q3_callback_family_resolve(&f->pool.callbacks.blocked, "t:b");
  set_bounds(f, pusher, V(-10, -10, -10), V(10, 10, 10)); place(f, pusher, V(0, 0, 0));
  qa_q3_base_trajectory ppos = {QA_Q3_BASE_TR_LINEAR, 1000, 0, V(0, 0, 0), V(50, 0, 0)}; pusher->s.pos = ppos;
  blocker->s.e_type = QA_Q3_BASE_ET_ITEM; set_bounds(f, blocker, V(-4, -4, -4), V(4, 4, 4)); place(f, blocker, V(45, 0, 0));
  f->area[0] = qa_q3_game_entity_actor(blocker).id; f->area_count = 1; f->time = 2000; m.host.previous_time = 1900;
  qa_q3_base_actor_trace_result solid; memset(&solid, 0, sizeof solid); solid.solidity = QA_Q3_BASE_SOLID_START_SOLID; solid.contents = 1; solid.hit_kind = QA_Q3_BASE_HIT_WORLD;
  f->traces.count = 3; f->traces.next = 0; f->traces.items[0] = f->traces.items[1] = f->traces.items[2] = solid;
  g_blocked_slot = -1; qa_q3_mover_run_team(&m, pusher);
  CHECK(g_blocked_slot == (int)blocker->slot); CHECK(pusher->s.pos.time == 1100); CHECK_V(qa_q3_base_entity_shared_current_origin(&pusher->r), 45, 0, 0); CHECK_V(blocker->s.pos.base, 45, 0, 0); CHECK(f->traces.next == 3);
  /* Foreign rider. */
  qa_q3_game_entity *lift = qa_q3_entity_pool_spawn(&f->pool); qa_owned_actor foreign = qa_session_actor_registry_allocate(f->actors, "q1:base", "q1:thing");
  set_bounds(f, lift, V(-10, -10, -10), V(10, 10, 10)); place(f, lift, V(0, 0, 0));
  qa_q3_base_trajectory lpos = {QA_Q3_BASE_TR_LINEAR, 2900, 0, V(0, 0, 0), V(0, 0, 80)}; lift->s.pos = lpos;
  qa_actor_id lift_actor = qa_q3_game_entity_actor(lift).id;
  qa_body_state fb; memset(&fb, 0, sizeof fb); fb.origin = V(1, 2, 20); fb.bounds.min = V(-4, -4, -4); fb.bounds.max = V(4, 4, 4); fb.ground = &lift_actor;
  qa_shared_body_table_create_body(f->bodies, foreign, fb); qa_shared_body_table_link(f->bodies, foreign, NULL);
  f->area[0] = foreign.id; f->area_count = 1; f->time = 3000; m.host.previous_time = 2900;
  qa_q3_mover_run_team(&m, lift);
  qa_body_state_result rode = qa_shared_body_table_read(f->bodies, foreign.id);
  CHECK_V(rode.value.origin, 1, 2, 28); CHECK(rode.value.ground != NULL); CHECK_V(qa_q3_base_entity_shared_current_origin(&lift->r), 0, 0, 8);
  /* Blocked door versus a clientless entity pops and frees it. */
  qa_q3_game_entity *door2 = qa_q3_entity_pool_spawn(&f->pool), *junk = qa_q3_entity_pool_spawn(&f->pool);
  place(f, junk, V(1.5, 2.5, 3.5)); junk->s.origin = V(1.5, 2.5, 3.5); double jslot = junk->slot; f->time = 3000;
  qa_q3_mover_blocked_door(&m, door2, qa_q3_participant_entity(junk));
  CHECK(!qa_q3_game_entity_inuse(qa_q3_entity_pool_at(&f->pool, jslot))); CHECK(strcmp(qa_q3_game_entity_classname(qa_q3_entity_pool_at(&f->pool, jslot)), "freed") == 0);
  /* TEAMSLAVE run is a no-op; foreign entities abort. */
  rot->flags = QA_Q3_GAME_FLAG_TEAMSLAVE; double before = qa_q3_base_entity_shared_current_origin(&rot->r).x; f->time = 9000; qa_q3_mover_run(&m, rot); CHECK(qa_q3_base_entity_shared_current_origin(&rot->r).x == before);
  qa_q3_game_entity foreign_entity; qa_q3_game_entity_binding nb; memset(&nb, 0, sizeof nb); qa_q3_game_entity_init(&foreign_entity, 70, nb);
  CHECK_ABORTS(qa_q3_mover_run(&m, &foreign_entity));
  /* Callback families carry the TS ids. */
  CHECK(qa_q3_callback_family_resolve(&f->pool.callbacks.use, "q3.base.game.mover.initializeBinary.use").ctx == &m);
  CHECK(qa_q3_callback_family_resolve(&f->pool.callbacks.reached, "q3.base.game.mover.initializeBinary.reached").ctx == &m);
  CHECK(qa_q3_callback_family_resolve(&f->pool.callbacks.think, "q3.base.game.mover.reachedBinary.think").ctx == &m);
  qa_q3_spawn_variables_destroy(&vars);
}
/* MissileRuntime: values from the TS oracle. */
static double ms_timeout(void *ctx) { (void)ctx; return 20000; }
static double ms_random(void *ctx) { (void)ctx; return 0.25; }
static double ms_crandom(void *ctx) { (void)ctx; return -0.5; }
static double ms_sound(void *ctx, const char *p) { (void)ctx; (void)p; return 33; }
static qa_q3_reflection_result ms_invuln(void *ctx, qa_q3_game_entity *t, qa_vec3 d, qa_vec3 p) { (void)ctx; (void)t; (void)d; (void)p; qa_q3_reflection_result r; memset(&r, 0, sizeof r); return r; }
static qa_actor_id resolve_same(void *ctx, qa_saved_actor_id saved) { return qa_session_actor_registry_reference_saved(ctx, saved, "current"); }
static void test_missiles(void) {
  fixture fx, *f = &fx; fx_open(f, QA_Q3_BASE_PRODUCT_MISSIONPACK, 2, NULL);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  qa_q3_missionpack_missile_services services; memset(&services, 0, sizeof services);
  services.ctx = f; services.prox_mine_timeout = ms_timeout; services.random.ctx = f; services.random.random = ms_random; services.random.crandom = ms_crandom; services.sound_index = ms_sound; services.invulnerability_impact = ms_invuln;
  qa_q3_missile_host mh; memset(&mh, 0, sizeof mh);
  mh.ctx = f; mh.product = QA_Q3_BASE_PRODUCT_MISSIONPACK; mh.previous_time = f->previous; mh.combat = fx_combat; mh.actors = f->actors; mh.bodies = f->bodies; mh.spatial = &f->spatial; mh.world = f->world; mh.has_missionpack = true; mh.missionpack = services;
  qa_q3_missile_runtime m; qa_q3_missile_runtime_init(&m, mh);
  qa_q3_game_entity *shooter = qa_q3_entity_pool_activate_client(&f->pool, 0), *victim = qa_q3_entity_pool_activate_client(&f->pool, 1);
  qa_q3_init_game_entity(shooter); qa_q3_init_game_entity(victim); shooter->s.e_type = victim->s.e_type = QA_Q3_BASE_ET_PLAYER;
  qa_q3_game_client *cs = qa_q3_entity_pool_client_at(&f->pool, 0), *cv = qa_q3_entity_pool_client_at(&f->pool, 1);
  place(f, shooter, V(0, 0, 0)); set_bounds(f, victim, V(-15, -15, -24), V(15, 15, 32)); place(f, victim, V(500, 0, 0));
  set_vital(f, shooter, 100); set_vital(f, victim, 100); qa_q3_base_player_state_slots_set(&cs->ps.stats, (size_t)schema->max_health, 100);
  qa_q3_missile_direction dir = {3, 4, 0};
  qa_q3_game_entity *rocket = qa_q3_missile_runtime_fire_rocket(&m, shooter, V(1.5, 2.5, 3.5), &dir);
  CHECK(dir.x == 0.6000000238418579 && dir.y == 0.800000011920929 && dir.z == 0);
  CHECK(rocket->s.pos.type == 2 && rocket->s.pos.time == 950); CHECK_V(rocket->s.pos.base, 1.5, 2.5, 3.5); CHECK_V(rocket->s.pos.delta, 540, 720, 0);
  CHECK(rocket->nextthink == 16000); CHECK(rocket->s.apos.base.y == 53.13010025024414 && rocket->s.apos.type == 0);
  CHECK(strcmp(qa_q3_game_entity_classname(rocket), "rocket") == 0);
  CHECK(rocket->damage == 100 && rocket->splash_damage == 100 && rocket->splash_radius == 120 && rocket->method_of_death == 6 && rocket->splash_method_of_death == 7);
  CHECK(rocket->clipmask == 100663297 && rocket->r.sv_flags == 128 && rocket->r.owner_num == 0);
  qa_actor_id owner_out; CHECK(qa_q3_missile_runtime_owner_of(&m, qa_q3_game_entity_actor(rocket).id, &owner_out) && qa_same_actor(owner_out, qa_q3_game_entity_actor(shooter).id));
  f->time = 1100; m.host.previous_time = 1000; qa_q3_missile_runtime_run(&m, rocket);
  CHECK(qa_q3_base_entity_shared_current_origin(&rocket->r).x == 82.5 && qa_q3_base_entity_shared_current_origin(&rocket->r).y == 110.50000762939453 && qa_q3_base_entity_shared_current_origin(&rocket->r).z == 3.5);
  CHECK_V(velocity_of(f, rocket), 540, 720, 0); CHECK(rocket->s.apos.base.y == 53.13010025024414);
  f->time = 1200; m.host.previous_time = 1100;
  script_plane(f, 0.5, V(150.25, 200.75, 3.5), V(0, 0, 1), 0x1000, QA_Q3_BASE_HIT_WORLD, no_actor());
  qa_q3_missile_runtime_run(&m, rocket);
  CHECK(rocket->free_after_event && rocket->s.e_type == 0 && rocket->s.event == 308 && rocket->s.event_parm == 5 && rocket->s.pos.type == 0);
  CHECK_V(rocket->s.pos.base, 150, 200, 3); CHECK_V(qa_q3_base_entity_shared_current_origin(&rocket->r), 150, 200, 3);
  /* Grenade bounce. */
  qa_q3_missile_direction g = {1, 0, 0}; f->time = 2000;
  qa_q3_game_entity *grenade = qa_q3_missile_runtime_fire_grenade(&m, shooter, V(0, 0, 100), &g);
  CHECK(grenade->s.e_flags == 32 && grenade->nextthink == 4500 && grenade->s.pos.type == 5 && grenade->s.pos.time == 1950); CHECK_V(grenade->s.pos.delta, 700, 0, 0);
  f->time = 2100; m.host.previous_time = 2000;
  script_plane(f, 0.25, V(50, 0, 90), V(0, 0, 1), 0, QA_Q3_BASE_HIT_WORLD, no_actor());
  qa_q3_missile_runtime_run(&m, grenade);
  CHECK(grenade->s.pos.delta.x == 454.9999694824219 && grenade->s.pos.delta.y == 0 && grenade->s.pos.delta.z == 39);
  CHECK_V(grenade->s.pos.base, 50, 0, 91); CHECK(grenade->s.pos.time == 2100 && grenade->s.event == 300 && grenade->s.pos.type == 5); CHECK_V(qa_q3_base_entity_shared_current_origin(&grenade->r), 50, 0, 91);
  CHECK(velocity_of(f, grenade).x == 454.9999694824219 && velocity_of(f, grenade).z == 39);
  f->time = 3000; qa_q3_missile_runtime_explode(&m, grenade);
  CHECK(grenade->s.event == 563 && grenade->s.event_parm == 5 && grenade->free_after_event && grenade->s.e_type == 0); CHECK_V(grenade->s.pos.base, 459, 0, -197);
  /* Grapple hook attaches to a player. */
  qa_q3_missile_direction h = {1, 0, 0}; f->time = 4000;
  qa_q3_game_entity *hook = qa_q3_missile_runtime_fire_grapple(&m, shooter, V(0, 0, 0), &h);
  CHECK(hook->method_of_death == 28 && hook->s.other_entity_num == 0 && cs->hook == hook && hook->parent == shooter); CHECK_V(hook->s.pos.delta, 800, 0, 0);
  f->time = 4100; m.host.previous_time = 4000;
  script_plane(f, 0.5, V(40, 0, 0), V(-1, 0, 0), 0, QA_Q3_BASE_HIT_ACTOR, qa_q3_game_entity_actor(victim).id);
  qa_q3_missile_runtime_run(&m, hook);
  CHECK(hook->s.e_type == 11 && hook->nextthink == 4200 && cs->ps.pm_flags == 2048); CHECK_V(qa_q3_base_entity_shared_current_origin(&hook->r), 500, 0, 4); CHECK_V(cs->ps.grapple_point, 500, 0, 4);
  place(f, victim, V(600.5, 10.5, -3.5)); qa_q3_missile_runtime_hook_think(&m, hook);
  CHECK_V(qa_q3_base_entity_shared_current_origin(&hook->r), 600, 10, 1); CHECK_V(cs->ps.grapple_point, 600, 10, 1);
  /* Save round trip: the TS capture has three entries and only the hook is attached. */
  qa_save_value *saved = qa_q3_missile_runtime_capture_save_state(&m);
  CHECK(saved && saved->item_count == 3);
  CHECK(strcmp(qa_save_value_get(qa_save_value_get(saved->items[2], "attachment"), "kind")->text, "player") == 0);
  CHECK(strcmp(qa_save_value_get(qa_save_value_get(saved->items[0], "attachment"), "kind")->text, "none") == 0);
  qa_q3_missile_runtime_restore_save_state(&m, saved, resolve_same, f->actors);
  qa_save_value *again = qa_q3_missile_runtime_capture_save_state(&m); CHECK(again && again->item_count == 3);
  size_t l1 = 0, l2 = 0; uint8_t *b1 = qa_encode_checkpoint_value(saved, &l1), *b2 = qa_encode_checkpoint_value(again, &l2);
  CHECK(l1 == l2 && memcmp(b1, b2, l1) == 0); free(b1); free(b2); qa_save_value_free(again); qa_save_value_free(saved);
  qa_owned_actor hook_actor = qa_q3_game_entity_actor(hook); qa_q3_missile_runtime_hook_free(&m, hook);
  CHECK(cs->hook == NULL && cs->ps.pm_flags == 0 && !qa_session_actor_registry_is_live(f->actors, hook_actor.id));
  CHECK(!qa_q3_missile_runtime_owner_of(&m, hook_actor.id, &owner_out));
  /* Plasma direct hit with splash. */
  qa_q3_missile_direction pd = {1, 0, 0}; f->time = 5000; cv->ps.pm_time = 0;
  qa_q3_game_entity *plasma = qa_q3_missile_runtime_fire_plasma(&m, shooter, V(0, 0, 0), &pd);
  double hits_before = cs->accuracy_hits; f->time = 5100; m.host.previous_time = 5000;
  f->area[0] = qa_q3_game_entity_actor(victim).id; f->area[1] = qa_q3_game_entity_actor(shooter).id; f->area_count = 2;
  script_plane(f, 0.5, V(40.5, 0, 0), V(-1, 0, 0), 0, QA_Q3_BASE_HIT_ACTOR, qa_q3_game_entity_actor(victim).id);
  qa_q3_missile_runtime_run(&m, plasma);
  CHECK(cs->accuracy_hits - hits_before == 1 && plasma->s.event == 306 && plasma->s.event_parm == 143 && plasma->s.other_entity_num == 1);
  CHECK(health_of(f, victim) == 80 && cv->damage_blood == 20 && cv->last_hurt_client == 0 && cv->last_hurt_mod == 8 && cv->ps.pm_time == 50);
  CHECK_V(velocity_of(f, victim), 100, 0, 0); CHECK_V(plasma->s.pos.base, 40, 0, 0); CHECK(health_of(f, shooter) == 100);
  /* Prox mine: stick, arm, trigger entity, die, detonate. */
  f->area_count = 0; cs->sess.session_team = 1; qa_q3_missile_direction p = {0, 1, 0}; f->time = 6000;
  qa_q3_game_entity *mine = qa_q3_missile_runtime_fire_prox(&m, shooter, V(0, 0, 0), &p);
  CHECK(mine->s.generic1 == 1 && mine->nextthink == 9000 && mine->splash_damage == 100 && mine->splash_radius == 150 && mine->parent == shooter); CHECK_V(mine->s.pos.delta, 0, 700, 0);
  f->time = 6100; m.host.previous_time = 6000;
  script_plane(f, 0.5, V(0, 30.5, 2.5), V(0, -1, 0), 7, QA_Q3_BASE_HIT_WORLD, no_actor());
  qa_q3_missile_runtime_run(&m, mine);
  CHECK_V(qa_q3_base_entity_shared_current_origin(&mine->r), 0, 30, 2); CHECK_V(mine->s.pos.base, 0, 30, 2); CHECK(mine->s.pos.type == 0 && mine->s.event == 322 && mine->s.event_parm == 7 && mine->nextthink == 8100);
  CHECK_V(mine->s.angles, 90, 270, 0); CHECK_V(mine->movedir, 0, -1, 0); CHECK_V(qa_q3_base_entity_shared_mins(&mine->r), -4, -4, -4); CHECK_V(qa_q3_base_entity_shared_maxs(&mine->r), 4, 4, 4);
  f->time = 8100; CHECK(qa_actor_callback_table_think(f->callbacks, qa_q3_game_entity_actor(mine), frame_at(8100)));
  qa_q3_game_entity *trig = mine->activator; CHECK(trig != NULL);
  CHECK(mine->nextthink == 28100 && qa_q3_game_entity_takedamage(mine) && qa_q3_game_entity_health(mine) == 1 && mine->s.loop_sound == 33);
  CHECK(qa_gameplay_authority_read(f->combat, qa_q3_game_entity_actor(mine).id).value.can_take_damage);
  CHECK(strcmp(qa_q3_game_entity_classname(trig), "proxmine_trigger") == 0); CHECK_V(qa_q3_base_entity_shared_maxs(&trig->r), 150, 150, 150); CHECK(trig->r.contents == 1073741824);
  CHECK(qa_q3_missile_runtime_is_proximity_trigger(&m, trig)); CHECK_V(trig->s.pos.base, 0, 30, 2);
  qa_q3_entity_die_fn die; memcpy(&die, &mine->die.fn, sizeof die); qa_q3_damage_participant none; memset(&none, 0, sizeof none); die(mine->die.ctx, mine, none, none, 0, 0);
  CHECK(mine->nextthink == 8101);
  qa_owned_actor trig_actor = qa_q3_game_entity_actor(trig); f->time = 8200;
  CHECK(qa_actor_callback_table_think(f->callbacks, qa_q3_game_entity_actor(mine), frame_at(8200)));
  CHECK(mine->free_after_event && mine->s.event == 563 && !qa_session_actor_registry_is_live(f->actors, trig_actor.id));
  /* Prox mine sticks to a player. */
  qa_q3_missile_direction q = {1, 0, 0}; f->time = 9000;
  qa_q3_game_entity *pm = qa_q3_missile_runtime_fire_prox(&m, shooter, V(0, 0, 0), &q);
  f->time = 9100; m.host.previous_time = 9000;
  script_plane(f, 0.5, V(40, 0, 0), V(-1, 0, 0), 0, QA_Q3_BASE_HIT_ACTOR, qa_q3_game_entity_actor(victim).id);
  qa_q3_missile_runtime_run(&m, pm);
  CHECK(cv->ps.e_flags == 2 && victim->activator == pm && pm->r.sv_flags == 129 && pm->s.e_flags == 128 && pm->s.pos.type == 2 && pm->nextthink == 19100); CHECK_V(pm->s.pos.delta, 0, 0, 0);
  /* Nail. */
  f->time = 9500;
  qa_q3_game_entity *nail = qa_q3_missile_runtime_fire_nail(&m, shooter, V(1, 2, 3), V(1, 0, 0), V(0, -1, 0), V(0, 0, 1));
  CHECK_V(nail->s.pos.delta, 1004, 0, -30); CHECK(nail->s.pos.time == 9500 && nail->damage == 20 && nail->method_of_death == 23 && nail->nextthink == 19500);
  /* Baseq3 runtime refuses missionpack-only weapons. */
  qa_q3_missile_runtime_close(&m);
  fixture bx, *b = &bx; fx_open(b, QA_Q3_BASE_PRODUCT_BASEQ3, 2, NULL);
  qa_q3_missile_host bh; memset(&bh, 0, sizeof bh); bh.ctx = b; bh.product = QA_Q3_BASE_PRODUCT_BASEQ3; bh.combat = fx_combat; bh.actors = b->actors; bh.bodies = b->bodies; bh.spatial = &b->spatial; bh.world = b->world;
  qa_q3_missile_runtime bm; qa_q3_missile_runtime_init(&bm, bh);
  qa_q3_game_entity *bs = qa_q3_entity_pool_activate_client(&b->pool, 0); qa_q3_missile_direction bd = {1, 0, 0};
  CHECK_ABORTS(qa_q3_missile_runtime_fire_prox(&bm, bs, V(0, 0, 0), &bd));
  qa_q3_game_entity *bh2 = qa_q3_missile_runtime_fire_grapple(&bm, bs, V(0, 0, 0), &bd); CHECK(bh2->method_of_death == 23);
  CHECK_ABORTS(qa_q3_missile_runtime_run(&bm, bs));
  /* Owner release frees the hook, as actors.onRelease does in TS. */
  qa_owned_actor bh2_actor = qa_q3_game_entity_actor(bh2);
  qa_q3_entity_pool_deactivate_client(&b->pool, 0);
  CHECK(!qa_session_actor_registry_is_live(b->actors, bh2_actor.id));
  qa_q3_missile_runtime_close(&bm);
}
/* tests/persistence/q3-native-graph.test.ts */
static qa_q3_graph_services services_of(fixture *f) { qa_q3_graph_services g; memset(&g, 0, sizeof g); g.records = f->records; g.pool = &f->pool; g.actors = f->actors; return g; }
static void test_graph_round_trip(void) {
  fixture sx, *s = &sx; fx_open(s, QA_Q3_BASE_PRODUCT_BASEQ3, 1, NULL); s->time = 5000;
  qa_q3_game_entity *player = qa_q3_entity_pool_activate_client(&s->pool, 0), *item = qa_q3_entity_pool_spawn(&s->pool);
  qa_q3_game_client *sc = qa_q3_entity_pool_client_at(&s->pool, 0);
  item->think = s->think_cb; qa_q3_game_entity_set_nextthink(item, 8000); item->enemy = player; player->client = sc;
  qa_q3_game_entity_bind_client_name(player, sc); free(sc->pers.netname); sc->pers.netname = strdup("saved name");
  qa_q3_base_player_state_slots_set(&sc->ps.stats, 7, 531); qa_q3_base_player_state_slots_set(&sc->ps.ammo, 0, -19);
  sc->hook = item; qa_q3_game_entity_set_health(player, 83); qa_q3_base_entity_shared_set_current_origin(&player->r, V(12, 34, 56));
  qa_q3_game_entity *dead = qa_q3_entity_pool_spawn(&s->pool); double dead_slot = dead->slot; qa_q3_entity_pool_free(&s->pool, dead); item->chain = dead;
  qa_q3_base_entity_shared_set_absmin(&item->r, V(-22, -23, -24));
  qa_owned_actor stale = qa_session_actor_registry_allocate(s->actors, "q1:base", "q1:entity"); qa_session_actor_registry_release(s->actors, stale);
  item->activation = qa_q3_base_entity_records_damage_inflictor(s->records, true, stale.id); item->has_activation = true;
  int source_schedules = s->schedules; (void)source_schedules;
  qa_owned_actor player_actor = qa_q3_game_entity_actor(player); double item_slot = item->slot;

  qa_q3_graph_state captured = qa_q3_capture_q3_graph(services_of(s));
  qa_save_value *tree = qa_q3_graph_state_to_value(&captured); size_t length = 0; uint8_t *bytes = qa_encode_checkpoint_value(tree, &length);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length); CHECK(decoded != NULL);
  qa_q3_graph_state data = qa_q3_read_complete_graph(decoded);

  size_t cp_n = 0, src_n = 0;
  const qa_actor_slot_checkpoint *cp = qa_session_actor_registry_checkpoint(s->actors, &cp_n);
  const qa_source_actor_checkpoint *src = qa_session_actor_registry_source_checkpoint(s->actors, &src_n);
  qa_identity_owner restored_identity; CHECK(qa_identity_owner_create(&restored_identity, "q3-restored"));
  qa_session_actor_registry *actors = qa_session_actor_registry_restore(&restored_identity, cp, cp_n, src, src_n, 2048); CHECK(actors != NULL);
  fixture tx, *t = &tx; fx_open(t, QA_Q3_BASE_PRODUCT_BASEQ3, 1, actors); t->time = 5000;
  qa_q3_prepare_q3_graph(services_of(t), &data);
  size_t obs_n = 0; const qa_actor_observation *obs = qa_session_actor_registry_observations(s->actors, &obs_n);
  for (size_t i = 0; i < obs_n; i++) {
    qa_owned_actor restored; CHECK(qa_session_actor_registry_resolve_saved(actors, qa_saved_actor_id_of(obs[i].id), &restored));
    qa_body_state_result body = qa_shared_body_table_read(s->bodies, obs[i].id); qa_combat_state_result combat = qa_gameplay_authority_read(s->combat, obs[i].id);
    if (body.has_value) { body.value.ground = NULL; qa_shared_body_table_create_body(t->bodies, restored, body.value); }
    if (combat.has_value) qa_gameplay_authority_create_state(t->combat, restored, combat.value, NULL, NULL);
    if (qa_shared_inventory_table_has(s->inventory, obs[i].id)) qa_shared_inventory_table_create_entries(t->inventory, restored, NULL, 0);
  }
  int schedules_before = t->schedules;
  qa_q3_restore_q3_graph(services_of(t), &data);
  qa_q3_game_entity *ritem = qa_q3_entity_pool_at(&t->pool, item_slot), *rplayer = qa_q3_entity_pool_at(&t->pool, 0);
  qa_q3_game_client *rc = qa_q3_entity_pool_client_at(&t->pool, 0);
  CHECK(ritem != item); CHECK(ritem->enemy == rplayer); CHECK(!qa_same_actor(qa_q3_game_entity_actor(rplayer).id, player_actor.id));
  CHECK(ritem->think.fn == t->think_cb.fn && ritem->think.ctx == t); CHECK(ritem->nextthink == 8000); CHECK(t->schedules == schedules_before);
  CHECK(ritem->chain == qa_q3_entity_pool_at(&t->pool, dead_slot)); CHECK(ritem->chain->freetime == 5000);
  CHECK(qa_q3_entity_pool_spawn(&t->pool)->slot == dead_slot + 1);
  CHECK(qa_q3_game_entity_health(rplayer) == 83); CHECK(qa_q3_base_entity_shared_current_origin(&rplayer->r).x == 12 && qa_q3_base_entity_shared_current_origin(&rplayer->r).y == 34 && qa_q3_base_entity_shared_current_origin(&rplayer->r).z == 56);
  CHECK(qa_q3_base_player_state_slots_get(&rc->ps.stats, 7) == 531); CHECK(qa_q3_base_player_state_slots_get(&rc->ps.ammo, 0) == -19);
  CHECK(rc->hook == ritem); CHECK(strcmp(qa_q3_game_entity_classname(rplayer), "saved name") == 0);
  free(rc->pers.netname); rc->pers.netname = strdup("new name"); CHECK(strcmp(qa_q3_game_entity_classname(rplayer), "new name") == 0);
  CHECK(qa_q3_base_entity_shared_absmin(&ritem->r).x == -22 && qa_q3_base_entity_shared_absmin(&ritem->r).y == -23 && qa_q3_base_entity_shared_absmin(&ritem->r).z == -24);
  CHECK(ritem->has_activation && ritem->activation.entity == NULL); CHECK(!qa_session_actor_registry_is_live(actors, ritem->activation.actor));
  CHECK(qa_actor_callback_table_think(t->callbacks, qa_q3_game_entity_actor(ritem), frame_at(8000)));
  CHECK(t->fired == 1); CHECK(s->fired == 0); CHECK(ritem->count == 1);
  free(bytes); qa_save_value_free(decoded); qa_save_value_free(tree); qa_q3_graph_state_destroy(&captured); qa_q3_graph_state_destroy(&data);
}
static void test_graph_rejects(void) {
  fixture sx, *s = &sx; fx_open(s, QA_Q3_BASE_PRODUCT_BASEQ3, 1, NULL); s->time = 5000;
  qa_q3_game_entity *e = qa_q3_entity_pool_spawn(&s->pool);
  qa_q3_entity_callback unknown; int marker = 0; unknown.fn = s->think_cb.fn; unknown.ctx = &marker; e->think = unknown;
  CHECK_ABORTS(qa_q3_capture_q3_graph(services_of(s)));
  e->think = s->think_cb;
  qa_q3_graph_state state = qa_q3_capture_q3_graph(services_of(s));
  fixture tx, *t = &tx; fx_open(t, QA_Q3_BASE_PRODUCT_BASEQ3, 1, NULL);
  CHECK_ABORTS(qa_q3_callback_family_resolve(&t->pool.callbacks.think, "not:registered"));
  CHECK(strcmp(state.entities[(size_t)e->slot].think, "test:think") == 0);
  /* The incomplete reader's result restores without touching classnames. */
  qa_save_value *tree = qa_q3_graph_state_to_value(&state); qa_q3_graph_state partial = qa_q3_read_graph(tree);
  CHECK(!partial.complete); qa_q3_game_entity_set_classname(qa_q3_entity_pool_at(&t->pool, e->slot), "kept"); qa_q3_restore_graph(&t->pool, &partial);
  CHECK(strcmp(qa_q3_game_entity_classname(qa_q3_entity_pool_at(&t->pool, e->slot)), "kept") == 0);
  state.client_count = 3; CHECK_ABORTS(qa_q3_restore_q3_graph(services_of(t), &state)); state.client_count = QA_Q3_MAX_CLIENTS;
  qa_save_value_free(tree); qa_q3_graph_state_destroy(&partial); qa_q3_graph_state_destroy(&state);
}
static int g_foreign_thinks;
static void foreign_think(void *ctx, qa_owned_actor self, qa_frame_context frame) { (void)self; (void)frame; (*(int *)ctx)++; }
static void test_graph_borrowed(void) {
  fixture sx, *s = &sx; fx_open(s, QA_Q3_BASE_PRODUCT_BASEQ3, 1, NULL); s->time = 5000;
  qa_owned_actor foreign = qa_session_actor_registry_allocate(s->actors, "q1:base", "q1:entity");
  (void)qa_q3_base_entity_records_attach(s->records, 100, foreign, false);
  qa_q3_graph_state state = qa_q3_capture_q3_graph(services_of(s));
  size_t cp_n = 0, src_n = 0; const qa_actor_slot_checkpoint *cp = qa_session_actor_registry_checkpoint(s->actors, &cp_n);
  const qa_source_actor_checkpoint *src = qa_session_actor_registry_source_checkpoint(s->actors, &src_n);
  qa_identity_owner id2; CHECK(qa_identity_owner_create(&id2, "q3-borrowed-restore"));
  qa_session_actor_registry *actors = qa_session_actor_registry_restore(&id2, cp, cp_n, src, src_n, 2048);
  fixture tx, *t = &tx; fx_open(t, QA_Q3_BASE_PRODUCT_BASEQ3, 1, actors); t->time = 5000;
  qa_owned_actor actor; CHECK(qa_session_actor_registry_resolve_saved(actors, qa_saved_actor_id_of(foreign.id), &actor));
  qa_actor_callbacks cb; memset(&cb, 0, sizeof cb); g_foreign_thinks = 0; cb.ctx = &g_foreign_thinks; cb.think = foreign_think;
  qa_actor_callback_table_bind(t->callbacks, actor, cb);
  size_t obs_n = 0; const qa_actor_observation *obs = qa_session_actor_registry_observations(s->actors, &obs_n);
  qa_q3_prepare_q3_graph(services_of(t), &state);
  for (size_t i = 0; i < obs_n; i++) {
    qa_owned_actor restored; if (!qa_session_actor_registry_resolve_saved(actors, qa_saved_actor_id_of(obs[i].id), &restored)) continue;
    qa_body_state_result body = qa_shared_body_table_read(s->bodies, obs[i].id); qa_combat_state_result combat = qa_gameplay_authority_read(s->combat, obs[i].id);
    if (body.has_value) { body.value.ground = NULL; qa_shared_body_table_create_body(t->bodies, restored, body.value); }
    if (combat.has_value) qa_gameplay_authority_create_state(t->combat, restored, combat.value, NULL, NULL);
    if (qa_shared_inventory_table_has(s->inventory, obs[i].id)) qa_shared_inventory_table_create_entries(t->inventory, restored, NULL, 0);
  }
  int before = t->schedules;
  qa_q3_restore_q3_graph(services_of(t), &state);
  CHECK(qa_actor_callback_table_think(t->callbacks, actor, frame_at(100)));
  CHECK(g_foreign_thinks == 1);
  CHECK(qa_same_owned_actor(qa_q3_game_entity_actor(qa_q3_entity_pool_at(&t->pool, 100)), actor));
  CHECK(t->schedules == before);
  qa_q3_graph_state_destroy(&state);
}
int main(void) {
  test_combat_authority();
  test_combat_context_edges();
  test_movers();
  test_missiles();
  test_graph_round_trip();
  test_graph_rejects();
  test_graph_borrowed();
  printf("test_q3_game_logic: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
