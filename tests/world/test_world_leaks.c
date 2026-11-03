/*
 * World-module ownership under LeakSanitizer: combat continuations dropped
 * by the apply loop, combat policy release, and the shared inventory table's
 * owned item ids. Expected outcomes come from the TS GameplayAuthority and
 * SharedInventoryTable under bun (build/agents/q1-sync/bun/world_leaks.ts,
 * labels "impulse", "armor", "ids"). Run under -fsanitize=address with leak
 * detection on: a lost continuation state or item id fails the binary.
 * GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/policies.h"

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

#define SENTINEL_MAGIC 0x4c45414bu
typedef struct world {
  unsigned magic;
  const char *release_at; /* "impulse" | "armor" */
  qa_session_actor_registry *actors;
  qa_owned_actor target;
  qa_victim_armor_policy native;
  int impulses, armor_calls, context_calls;
} world;
static world *world_of(void *ctx) {
  world *w = ctx;
  if (w == NULL || w->magic != SENTINEL_MAGIC) {
    fprintf(stderr, "test_world_leaks: callback received a foreign ctx\n");
    abort();
  }
  return w;
}

static void hook_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) {
  world *w = world_of(ctx);
  (void)actor; (void)impulse; (void)movement;
  w->impulses++;
  if (!strcmp(w->release_at, "impulse")) qa_session_actor_registry_release(w->actors, w->target);
}
static void hook_before(void *ctx, qa_owned_actor actor, qa_damage_decision decision) {
  (void)world_of(ctx); (void)actor; (void)decision;
}
static void hook_confirmed(void *ctx, qa_damage_outcome outcome) { (void)world_of(ctx); (void)outcome; }

static qa_q1_combat_context walk_context(void *ctx, qa_damage_request request, qa_combat_state target,
                                         const qa_combat_state *attacker) {
  world *w = world_of(ctx);
  (void)request; (void)target; (void)attacker;
  w->context_calls++;
  qa_q1_combat_context context;
  memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  context.walk = true;
  context.has_momentum_direction = true;
  context.momentum_direction = (qa_vec3){1, 0, 0};
  return context;
}
static qa_victim_armor_context armor_context(void *ctx, qa_damage_request request) {
  (void)world_of(ctx); (void)request;
  qa_victim_armor_context context;
  memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  context.screen_facing_dot = 1;
  return context;
}
/* The victim armor policy releases the target mid stage when asked to. */
static qa_armor_result releasing_armor(void *ctx, qa_damage_request request, qa_armor_state armor, double damage,
                                       qa_armor_damage_flags flags) {
  world *w = world_of(ctx);
  w->armor_calls++;
  if (!strcmp(w->release_at, "armor")) qa_session_actor_registry_release(w->actors, w->target);
  return w->native.call(w->native.ctx, request, armor, damage, flags);
}

static size_t mutation_count(qa_damage_outcome outcome, qa_damage_mutation_kind kind) {
  size_t count = 0;
  for (size_t i = 0; i < outcome.decision.mutation_count; i++)
    if (outcome.decision.mutations[i].kind == kind) count++;
  return count;
}

/* The apply loop leaves early when the target is released: after committing
 * a source continuation's impulse ("impulse") or after an armor stage
 * ("armor"). The dropped continuation state is discarded, not leaked. */
static void test_dropped_continuation(const char *release_at) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "leaks"));
  world w;
  memset(&w, 0, sizeof w);
  w.magic = SENTINEL_MAGIC;
  w.release_at = release_at;
  w.actors = qa_session_actor_registry_create(&identities, 4096);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(w.actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &w;
  hooks.impulse = hook_impulse;
  hooks.before_reaction = hook_before;
  hooks.confirmed = hook_confirmed;
  qa_gameplay_authority *combat = qa_gameplay_authority_create(w.actors, callbacks, hooks);
  w.native = qa_native_victim_armor(armor_context, &w);
  qa_q1_combat_policy_options options;
  memset(&options, 0, sizeof options);
  options.id = "q1:combat";
  options.ctx = &w;
  options.context = walk_context;
  options.armor = (qa_victim_armor_policy){&w, releasing_armor};
  const qa_combat_policy policy = qa_create_q1_combat_policy(options);
  (void)qa_gameplay_authority_register(combat, policy);
  w.target = qa_session_actor_registry_allocate(w.actors, "q1:game", "q1:monster");
  qa_combat_state initial;
  memset(&initial, 0, sizeof initial);
  initial.health = 100;
  initial.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  initial.armor.powered.kind = "none";
  initial.mass = 200;
  initial.can_take_damage = true;
  qa_gameplay_authority_create_state(combat, w.target, initial, NULL, NULL);
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = w.target.id;
  request.amount = 30;
  request.delivery = "direct";
  request.direction = (qa_vec3){1, 0, 0};
  request.attack.sequence = 1;
  request.attack.time.kind = QA_SOURCE_TIME_SECONDS;
  request.attack.time.value = 1;
  request.attack.weapon = "q1:weapon/shotgun";
  request.attack.weapon_provider = "q1:weapons";
  request.attack.combat_provider = "q1:combat";
  request.attack.inventory_provider = "q1:inventory";
  request.attack.movement_provider = "q1:movement";
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q1;
  const qa_damage_outcome outcome = qa_gameplay_authority_apply(combat, request, NULL, NULL);
  const bool impulse = !strcmp(release_at, "impulse");
  CHECK(!strcmp(outcome.kind, "committed"));
  CHECK(outcome.decision.applied_damage == 0 && !strcmp(outcome.decision.reaction, "none"));
  CHECK(outcome.decision.mutation_count == (impulse ? 1u : 0u));
  CHECK(mutation_count(outcome, QA_DAMAGE_MUTATION_IMPULSE) == (impulse ? 1u : 0u));
  CHECK(w.impulses == (impulse ? 1 : 0) && w.armor_calls == (impulse ? 2 : 1));
  CHECK(!qa_session_actor_registry_is_live(w.actors, w.target.id));
  CHECK(w.context_calls > 0);
  qa_gameplay_authority_destroy(combat);
  qa_combat_policy_free(policy);
  qa_victim_armor_policy_free(w.native);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(w.actors);
  qa_identity_owner_destroy(&identities);
}

/* The shared inventory table owns the item ids it is given (TS strings are
 * values): callers may free theirs at once. */
static void test_inventory_owns_ids(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "ids"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 4096);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  const qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q1:game", "q1:player");
  char *shells = strdup("q1:ammo/shells");
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = shells;
  entry.count = 10;
  entry.capacity = 100;
  qa_shared_inventory_table_create_entries(inventory, actor, &entry, 1);
  memset(shells, 'x', strlen(shells));
  free(shells);
  char *gold = strdup("q1:key/gold");
  entry.item = gold;
  entry.count = 1;
  entry.capacity = 1;
  qa_shared_inventory_table_configure(inventory, actor, entry, NULL, NULL);
  free(gold);
  /* Replacing an entry keeps the table's own id. */
  char *again = strdup("q1:ammo/shells");
  entry.item = again;
  entry.count = 10;
  entry.capacity = 100;
  qa_shared_inventory_table_configure(inventory, actor, entry, NULL, NULL);
  free(again);
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(inventory, actor.id, &count);
  CHECK(count == 2); /* ids.entries */
  CHECK(count == 2 && !strcmp(entries[0].item, "q1:ammo/shells") && entries[0].count == 10 && entries[0].capacity == 100);
  CHECK(count == 2 && !strcmp(entries[1].item, "q1:key/gold") && entries[1].count == 1 && entries[1].capacity == 1);
  CHECK(qa_shared_inventory_table_count(inventory, actor.id, "q1:ammo/shells") == 10); /* ids.shells */
  CHECK(qa_shared_inventory_table_count(inventory, actor.id, "q1:key/gold") == 1);     /* ids.gold */
  qa_shared_inventory_table_destroy(inventory);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  test_dropped_continuation("impulse");
  test_dropped_continuation("armor");
  test_inventory_owns_ids();
  printf("test_world_leaks: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
