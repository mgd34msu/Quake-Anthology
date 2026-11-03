/*
 * C checks for src/content/q1 rows: admitEntity binds the foreign-shambler
 * damage adjustment in the real combat authority, on both create and
 * attachExisting, binds the actor callbacks in the callback table, then
 * runs host.registerEntity; authority bindDamageAdjustment/Admission keep
 * the closure's context. Fixture values from q1/foundation/shambler-damage.ts.
 * GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#include "q1_world_fixture.h"
#include "qa/content/q1/foundation/shambler_damage.h"

/* The authority probe: GameplayHooks.damageAllowed sees the request after
 * the actor's damage adjustment, and refuses it so nothing is applied. */
static struct {
  int registered, step, order_bind, order_callbacks, order_registered, probes;
  double amount, knockback;
  qa_q1_actor *registering;
} H;
static bool probe_allowed(void *ctx, qa_damage_request request) {
  (void)ctx; H.probes++; H.amount = request.amount; H.knockback = request.knockback; return false;
}
static qa_damage_request request_of(qa_q1_actor *entity, qa_attack_cause_kind kind, double means, const char *delivery) {
  qa_damage_request r; memset(&r, 0, sizeof r);
  r.target = entity->actor.id; r.amount = 101; r.knockback = 33; r.delivery = delivery;
  r.attack.combat_provider = "q1:combat"; r.attack.time.kind = QA_SOURCE_TIME_SECONDS;
  r.attack.cause.kind = kind; r.attack.cause.means_of_death = means;
  return r;
}
/* Applies through the authority; true when the bound adjustment changed the request. */
static bool adjusted(q1_world_fixture *fx, qa_q1_actor *entity, qa_attack_cause_kind kind, double means, const char *delivery) {
  H.amount = H.knockback = -1;
  (void)qa_gameplay_authority_apply(fx->combat, request_of(entity, kind, means, delivery), NULL, NULL);
  return !(H.amount == 101 && H.knockback == 33);
}
static bool bound_use_called;
static void probe_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)entity; (void)other; (void)activator; bound_use_called = true;
}
static void register_entity(void *ctx, qa_q1_actor *entity, qa_q1_services *services) {
  q1_world_fixture *fx = q1_world_fixture_of(ctx);
  CHECK(services == fx->game);
  H.registered++; H.order_registered = ++H.step;
  /* Both earlier admission steps are visible when registerEntity runs. */
  qa_q1_monster monster; memset(&monster, 0, sizeof monster); monster.species = "shambler";
  qa_q1_monster *saved = entity->monster; entity->monster = &monster;
  if (adjusted(fx, entity, QA_ATTACK_CAUSE_Q2, 8, "direct")) H.order_bind = ++H.step;
  entity->monster = saved;
  qa_q1_bound_use use = entity->use;
  entity->use.fn = probe_use; bound_use_called = false;
  (void)qa_actor_callback_table_use(fx->callbacks, entity->actor, NULL, NULL);
  entity->use = use;
  if (bound_use_called) H.order_callbacks = ++H.step;
}

static void check_admitted(q1_world_fixture *fx, qa_q1_actor *entity) {
  CHECK(H.order_bind > 0); CHECK(H.order_callbacks > 0); CHECK(H.registered == 1);
  CHECK(H.order_registered < H.order_bind && H.order_registered < H.order_callbacks);
  /* No monster state: entity.monster?.species is undefined. */
  CHECK(!adjusted(fx, entity, QA_ATTACK_CAUSE_Q2, 8, "direct"));
  qa_q1_monster monster; memset(&monster, 0, sizeof monster);
  entity->monster = &monster; monster.species = "ogre";
  CHECK(!adjusted(fx, entity, QA_ATTACK_CAUSE_Q2, 8, "direct"));
  monster.species = "shambler";
  CHECK(!adjusted(fx, entity, QA_ATTACK_CAUSE_Q1, 6, "radius"));
  CHECK(!adjusted(fx, entity, QA_ATTACK_CAUSE_ENVIRONMENT, 0, "radius"));
  CHECK(!adjusted(fx, entity, QA_ATTACK_CAUSE_Q2, 7, "direct"));
  CHECK(adjusted(fx, entity, QA_ATTACK_CAUSE_Q2, 8, "direct"));
  CHECK(H.amount == 50.5 && H.knockback == 16.5);
  CHECK(adjusted(fx, entity, QA_ATTACK_CAUSE_Q3, 6, "direct"));
  CHECK(adjusted(fx, entity, QA_ATTACK_CAUSE_Q3, 1, "radius"));
  entity->monster = NULL;
}

static void test_admit_entity(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  fx.game->host.register_entity = register_entity;
  fx.damage_allowed = probe_allowed;
  /* attachExisting over an actor already admitted to the shared tables. */
  memset(&H, 0, sizeof H);
  const qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(fx.actors, "q1:test", 40, "q1:test/ogre");
  qa_body_state body; memset(&body, 0, sizeof body); body.bounds = QA_Q1_POINT;
  qa_shared_body_table_create_body(fx.bodies, actor, body);
  qa_combat_state combat; memset(&combat, 0, sizeof combat);
  combat.health = 200; combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat.armor.powered.kind = "none"; combat.mass = 100;
  qa_gameplay_authority_create_state(fx.combat, actor, combat, NULL, NULL);
  qa_q1_actor *attached = qa_q1_attach_existing(fx.game, actor, "monster_ogre", NULL, NULL);
  CHECK(attached->actor.id.slot == actor.id.slot);
  check_admitted(&fx, attached);
  /* create runs the same admission. */
  memset(&H, 0, sizeof H);
  qa_q1_actor *created = qa_q1_create(fx.game, "misc_explobox", NULL, NULL);
  check_admitted(&fx, created);
  /* Every admission binds the adjustment: TS has no optional seam. */
  memset(&H, 0, sizeof H);
  qa_q1_actor *third = qa_q1_create(fx.game, "misc_explobox", NULL, NULL);
  CHECK(H.order_bind > 0 && H.registered == 1);
  (void)third;
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* Authority: a sentinel that differs from binding.ctx proves the closure context arrives. */
static int adjust_sentinel, admit_sentinel, binding_sentinel, seen_adjust, seen_admit;
static qa_damage_adjustment_result adjust_with_ctx(void *ctx, qa_damage_request request) {
  (void)request; if (ctx == &adjust_sentinel) seen_adjust++;
  qa_damage_adjustment_result r; memset(&r, 0, sizeof r); return r;
}
static const char *admit_with_ctx(void *ctx, qa_damage_request request) {
  (void)request; if (ctx == &admit_sentinel) seen_admit++;
  return "handled";
}
static qa_combat_state read_state(void *ctx) {
  CHECK(ctx == &binding_sentinel); qa_combat_state s; memset(&s, 0, sizeof s); s.health = 100; s.can_take_damage = true; s.armor.regular.kind = QA_REGULAR_ARMOR_NONE; s.armor.powered.kind = "none"; return s;
}
static void write_health(void *ctx, double health) { (void)ctx; (void)health; }
static void write_armor(void *ctx, qa_armor_state armor) { (void)ctx; (void)armor; }
static void write_traits(void *ctx, qa_combat_traits traits) { (void)ctx; (void)traits; }
static void hook_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) { (void)ctx; (void)actor; (void)impulse; (void)movement; }
static void hook_before(void *ctx, qa_owned_actor actor, qa_damage_decision decision) { (void)ctx; (void)actor; (void)decision; }
static qa_q1_combat_context q1_context(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker) {
  (void)ctx; (void)request; (void)target; (void)attacker;
  qa_q1_combat_context context; memset(&context, 0, sizeof context); return context;
}
static qa_victim_armor_context armor_context(void *ctx, qa_damage_request request) {
  (void)ctx; (void)request;
  qa_victim_armor_context context; memset(&context, 0, sizeof context); context.screen_facing_dot = 1; context.arithmetic = "binary32"; return context;
}
static void hook_confirmed(void *ctx, qa_damage_outcome outcome) { (void)ctx; (void)outcome; }
static void test_authority_closure_context(void) {
  qa_identity_owner identity; CHECK(qa_identity_owner_create(&identity, "q1-rows"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 64);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.impulse = hook_impulse; hooks.before_reaction = hook_before; hooks.confirmed = hook_confirmed;
  qa_gameplay_authority *authority = qa_gameplay_authority_create(actors, callbacks, hooks);
  qa_q1_combat_policy_options policy_options; memset(&policy_options, 0, sizeof policy_options);
  policy_options.id = "q1:combat"; policy_options.context = q1_context; policy_options.armor = qa_native_victim_armor(armor_context, NULL);
  const qa_combat_policy policy = qa_create_q1_combat_policy(policy_options);
  (void)qa_gameplay_authority_register(authority, policy);
  qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q1:test", "q1:test/a");
  qa_combat_state_binding binding; memset(&binding, 0, sizeof binding);
  binding.ctx = &binding_sentinel; binding.read = read_state; binding.write_health = write_health;
  binding.write_armor = write_armor; binding.write_traits = write_traits;
  qa_gameplay_authority_bind(authority, actor, binding);
  qa_gameplay_authority_bind_damage_adjustment(authority, actor, adjust_with_ctx, &adjust_sentinel);
  qa_gameplay_authority_bind_damage_admission(authority, actor, admit_with_ctx, &admit_sentinel);
  qa_damage_request request; memset(&request, 0, sizeof request);
  request.target = actor.id; request.amount = 5; request.delivery = "direct";
  request.attack.sequence = 1; request.attack.time.kind = QA_SOURCE_TIME_SECONDS; request.attack.time.value = 1;
  request.attack.weapon = "q1:weapon"; request.attack.weapon_provider = "q1:w"; request.attack.combat_provider = "q1:combat";
  request.attack.inventory_provider = "q1:i"; request.attack.movement_provider = "q1:m";
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q1;
  (void)qa_gameplay_authority_apply(authority, request, NULL, NULL);
  CHECK(seen_admit == 1); /* "handled" stops before adjust */
  CHECK(seen_adjust == 0);
  qa_gameplay_authority_destroy(authority);
  free(policy.ctx); /* no policy release in the world module (TS GC) */
  free(policy_options.armor.ctx);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
}

/* TS callbacks.ts: the bound callback carries the registered name. A name
 * formatted in a caller's stack buffer must not dangle in the bound slot. */
static void named_noop(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; (void)game; (void)entity; }
static void test_bound_callback_owns_name(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  qa_q1_callback_handlers handlers; memset(&handlers, 0, sizeof handlers);
  handlers.action = named_noop;
  qa_q1_callback_registry_register(fx.game->named, "rows_stack_named", handlers);
  qa_q1_actor *entity = qa_q1_create(fx.game, "info_null", NULL, NULL);
  char buffer[32];
  (void)snprintf(buffer, sizeof buffer, "rows_%s_named", "stack");
  entity->think = qa_q1_callback_registry_action(fx.game->named, entity, buffer);
  memset(buffer, 'x', sizeof buffer - 1); buffer[sizeof buffer - 1] = '\0';
  const char *name = qa_q1_action_name(entity->think);
  CHECK(name != NULL && name != buffer && !strcmp(name, "rows_stack_named"));
  q1_world_fixture_destroy(&fx);
}

int main(void) {
  test_admit_entity();
  test_authority_closure_context();
  test_bound_callback_owns_name();
  printf("test_q1_rows: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
