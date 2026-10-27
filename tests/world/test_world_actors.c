/*
 * Port of key cases from tests/world/gameplay/authority.test.ts,
 * body-storage.test.ts, and mod-composition.test.ts (registry, source
 * slots, bodies, mod operations, inventory, transitions, armor, the
 * scheduler, spatial index, and one authoritative Q1 damage call).
 * TypeScript toThrow cases run in a child process and must abort.
 * GPL-2.0-or-later.
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

#include "qa/contracts/identity.h"
#include "qa/world/actors.h"
#include "qa/world/gameplay.h"
#include "qa/world/gameplay/mod_composition.h"
#include "qa/world/scheduler.h"
#include "qa/world/spatial/index.h"

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

static qa_body_state make_body(double x) {
  qa_body_state state;
  memset(&state, 0, sizeof state);
  state.origin.x = x;
  state.bounds.min.x = -16; state.bounds.min.y = -16; state.bounds.min.z = -24;
  state.bounds.max.x = 16; state.bounds.max.y = 16; state.bounds.max.z = 32;
  return state;
}

static void test_registry(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "first"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  CHECK(actors != NULL);
  qa_owned_actor old = qa_session_actor_registry_allocate_at_source(actors, "q2:game", 7, "q2:soldier");
  qa_owned_actor at;
  CHECK(qa_session_actor_registry_at_source(actors, "q2:game", 7, &at));
  CHECK(qa_same_owned_actor(at, old));
  qa_actor_observation_result observed = qa_session_actor_registry_observe(actors, old.id);
  CHECK(observed.has_value && strcmp(observed.value.definition, "q2:soldier") == 0);
  /* TS also rejects a rebuilt owned handle by object identity; the C
   * contracts compare owned handles by value (see
   * src/persistence/source_items.c same_owned), so the ported stale
   * case is a double release (stale generation). */
  CHECK_ABORTS((qa_session_actor_registry_release(actors, old),
                qa_session_actor_registry_release(actors, old)));
  qa_session_actor_registry_release(actors, old);
  CHECK(!qa_session_actor_registry_is_live(actors, old.id));
  qa_owned_actor replacement =
      qa_session_actor_registry_allocate_at_source(actors, "q2:game", 7, "q2:infantry");
  CHECK(replacement.id.slot == old.id.slot);
  CHECK(replacement.id.generation > old.id.generation);
  size_t checkpoint_count = 0, source_count = 0;
  const qa_actor_slot_checkpoint *checkpoints =
      qa_session_actor_registry_checkpoint(actors, &checkpoint_count);
  const qa_source_actor_checkpoint *sources =
      qa_session_actor_registry_source_checkpoint(actors, &source_count);
  CHECK(source_count == 1);
  qa_identity_owner restore_identities;
  CHECK(qa_identity_owner_create(&restore_identities, "restore"));
  qa_session_actor_registry *restored = qa_session_actor_registry_restore(
      &restore_identities, checkpoints, checkpoint_count, sources, source_count, 65536);
  CHECK(restored != NULL);
  qa_owned_actor loaded;
  CHECK(qa_session_actor_registry_at_source(restored, "q2:game", 7, &loaded));
  /* Fresh session: restored actor gets the reserved (next) generation in
   * its own registry, so the saved generation resolves through
   * resolveSaved rather than raw id equality. */
  qa_saved_actor_id saved = {(double)replacement.id.slot, (double)replacement.id.generation};
  qa_owned_actor resolved;
  CHECK(qa_session_actor_registry_resolve_saved(restored, saved, &resolved));
  CHECK(qa_same_owned_actor(resolved, loaded));
  CHECK(!qa_session_actor_registry_is_live(restored, replacement.id));
  qa_session_actor_registry_close(restored);
  qa_session_actor_registry_close(actors);
  qa_session_actor_registry_destroy(restored);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&restore_identities);
  qa_identity_owner_destroy(&identities);
}

/* --- source slots (cooldown + QW overwrite) --- */
typedef struct slot_storage {
  uint8_t free_flags[3];
  double freed_at[3];
  double count;
  double now;
  qa_session_actor_registry *actors;
  int64_t unlinked[8];
  size_t unlinked_count;
} slot_storage;
static double storage_count(void *ctx) { return ((slot_storage *)ctx)->count; }
static void storage_set_count(void *ctx, double count) { ((slot_storage *)ctx)->count = count; }
static qa_source_slot_state storage_read(void *ctx, int64_t slot) {
  slot_storage *storage = ctx;
  qa_source_slot_state state;
  state.free = storage->free_flags[slot] == 1;
  state.freed_at.kind = QA_SOURCE_TIME_SECONDS;
  state.freed_at.value = storage->freed_at[slot];
  return state;
}
static void storage_initialize(void *ctx, int64_t slot, qa_owned_actor actor) {
  (void)actor;
  ((slot_storage *)ctx)->free_flags[slot] = 0;
}
static void storage_clear(void *ctx, int64_t slot, qa_source_time now) {
  slot_storage *storage = ctx;
  storage->free_flags[slot] = 1;
  storage->freed_at[slot] = now.value;
}
static bool storage_can_free(void *ctx, int64_t slot) {
  (void)ctx;
  return slot != 0;
}
static qa_source_time storage_now(void *ctx) {
  qa_source_time now = {QA_SOURCE_TIME_SECONDS, ((slot_storage *)ctx)->now};
  return now;
}
static void storage_unlink(void *ctx, qa_owned_actor actor) {
  slot_storage *storage = ctx;
  qa_actor_source_result source =
      qa_session_actor_registry_source_of(storage->actors, actor.id);
  if (source.has_value) storage->unlinked[storage->unlinked_count++] = source.slot;
}
static void storage_exhausted(void *ctx, int64_t slot) {
  (void)ctx;
  (void)slot;
}

static void test_source_slots(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "slots"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  slot_storage storage;
  memset(&storage, 0, sizeof storage);
  storage.count = 1;
  storage.now = 10;
  storage.actors = actors;
  qa_source_actor_slots_options options;
  memset(&options, 0, sizeof options);
  options.provider = "qw:game";
  options.capacity = 3;
  options.lifetime = qa_quake_edict_lifetime(1, "qw-last-slot");
  options.storage.ctx = &storage;
  options.storage.count = storage_count;
  options.storage.set_count = storage_set_count;
  options.storage.read = storage_read;
  options.storage.initialize = storage_initialize;
  options.storage.clear_freed = storage_clear;
  options.storage.can_free = storage_can_free;
  options.ctx = &storage;
  options.now = storage_now;
  options.unlink = storage_unlink;
  options.exhausted = storage_exhausted;
  qa_source_actor_slots *slots = qa_source_actor_slots_create(actors, options);
  CHECK(slots != NULL);
  qa_owned_actor world = qa_source_actor_slots_bind_existing(slots, 0, "qw:world");
  qa_owned_actor first = qa_source_actor_slots_allocate(slots, "qw:rocket");
  qa_owned_actor last = qa_source_actor_slots_allocate(slots, "qw:nail");
  CHECK(qa_source_actor_slots_free(slots, first));
  storage.now = 10.5; /* exactly at the boundary: not yet reusable */
  qa_owned_actor overwrite = qa_source_actor_slots_allocate(slots, "qw:grenade");
  qa_owned_actor at_two;
  CHECK(qa_source_actor_slots_at(slots, 2, &at_two) && qa_same_owned_actor(at_two, overwrite));
  CHECK(!qa_session_actor_registry_is_live(actors, last.id));
  storage.now = 10.5001;
  qa_owned_actor rocket = qa_source_actor_slots_allocate(slots, "qw:rocket");
  qa_actor_source_result source = qa_session_actor_registry_source_of(actors, rocket.id);
  CHECK(source.has_value && source.slot == 1);
  CHECK(!qa_source_actor_slots_free(slots, world));
  CHECK(qa_session_actor_registry_is_live(actors, world.id));
  CHECK(storage.unlinked_count == 3 && storage.unlinked[0] == 1 && storage.unlinked[1] == 2 &&
        storage.unlinked[2] == 0);
  qa_source_actor_slots_destroy(slots);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

/* --- bodies --- */
typedef struct link_log { double counts[8]; size_t count; } link_log;
static qa_bounds hooks_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx;
  return qa_translated_body_bounds(actor, state);
}
static void hooks_link(void *ctx, qa_linked_body body) {
  link_log *log = ctx;
  log->counts[log->count++] = body.link_count;
}
static void hooks_unlink(void *ctx, qa_actor_id actor) {
  (void)ctx;
  (void)actor;
}
static void test_bodies(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "body"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  link_log log;
  memset(&log, 0, sizeof log);
  qa_body_link_hooks hooks = {&log, hooks_bounds, hooks_link, hooks_unlink};
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, hooks);
  qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q1:game", "q1:player");
  qa_shared_body_table_create_body(bodies, actor, make_body(0));
  qa_shared_body_table_link(bodies, actor, NULL);
  qa_shared_body_table_write(bodies, actor, make_body(100));
  qa_body_state_result read = qa_shared_body_table_read(bodies, actor.id);
  CHECK(read.has_value && read.value.origin.x == 100);
  qa_linked_body_result linked = qa_shared_body_table_linked(bodies, actor.id);
  CHECK(linked.has_value && linked.value.absolute_bounds.min.x == -16);
  qa_shared_body_table_link(bodies, actor, NULL);
  linked = qa_shared_body_table_linked(bodies, actor.id);
  CHECK(linked.has_value && linked.value.absolute_bounds.min.x == 84);
  CHECK(log.count == 2 && log.counts[0] == 1 && log.counts[1] == 2);
  CHECK_ABORTS(qa_shared_body_table_create_body(bodies, actor, make_body(0)));
  qa_session_actor_registry_release(actors, actor);
  CHECK(!qa_shared_body_table_read(bodies, actor.id).has_value);
  qa_shared_body_table_destroy(bodies);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

/* --- mod operations --- */
typedef struct mod_log {
  char entries[16][64];
  size_t count;
} mod_log;
static void mod_push(mod_log *log, const char *text) {
  (void)snprintf(log->entries[log->count++], sizeof log->entries[0], "%s", text);
}
static void transform_offset(void *ctx, const void *request, void *out) {
  mod_push(ctx, "offset");
  *(double *)out = *(const double *)request + 3;
}
static void transform_scale(void *ctx, const void *request, void *out) {
  mod_push(ctx, "scale");
  *(double *)out = *(const double *)request * 2;
}
static void replace_plus_one(void *ctx, const void *request, qa_mod_next next, void *out) {
  (void)ctx;
  double value = *(const double *)request + 1;
  next.call(next.ctx, &value, out);
}
static void observe_first(void *ctx, const void *request, const void *result) {
  char text[64];
  (void)snprintf(text, sizeof text, "first:%.0f:%.0f", *(const double *)request,
                 *(const double *)result);
  mod_push(ctx, text);
}
static void observe_second(void *ctx, const void *request, const void *result) {
  char text[64];
  (void)snprintf(text, sizeof text, "second:%.0f:%.0f", *(const double *)request,
                 *(const double *)result);
  mod_push(ctx, text);
}
static void mod_canonical(void *ctx, const void *request, void *out) {
  mod_push(ctx, "canonical");
  *(double *)out = *(const double *)request;
}
static qa_mod_operation_registration make_registration(
    const char *provider, const char *id, int64_t order, qa_mod_registration_kind kind) {
  qa_mod_operation_registration registration;
  memset(&registration, 0, sizeof registration);
  registration.identity.provider = provider;
  registration.identity.id = id;
  registration.identity.order = order;
  registration.kind = kind;
  return registration;
}
static void test_mod_operation(void) {
  qa_mod_operation operation;
  CHECK(qa_mod_operation_init(&operation, "test.amount", sizeof(double), sizeof(double)));
  mod_log log;
  memset(&log, 0, sizeof log);
  qa_mod_operation_registration reg;
  reg = make_registration("q1:mod", "scale:amount", 2, QA_MOD_TRANSFORM);
  reg.ctx = &log; reg.transform = transform_scale;
  (void)qa_mod_operation_register(&operation, reg);
  reg = make_registration("q3:mod", "offset:amount", 1, QA_MOD_TRANSFORM);
  reg.ctx = &log; reg.transform = transform_offset;
  (void)qa_mod_operation_register(&operation, reg);
  reg = make_registration("q2:mod", "replace:amount", 0, QA_MOD_REPLACE);
  reg.replace = replace_plus_one;
  int64_t remove_id = qa_mod_operation_register(&operation, reg);
  reg = make_registration("q1:mod", "report:amount", 2, QA_MOD_OBSERVE);
  reg.ctx = &log; reg.observe = observe_second;
  (void)qa_mod_operation_register(&operation, reg);
  reg = make_registration("q3:mod", "report:amount", 1, QA_MOD_OBSERVE);
  reg.ctx = &log; reg.observe = observe_first;
  (void)qa_mod_operation_register(&operation, reg);
  double input = 4, output = 0;
  qa_mod_canonical canonical = {&log, mod_canonical};
  qa_mod_operation_dispatch(&operation, &input, canonical, NULL, &output);
  CHECK(output == 15);
  CHECK(log.count == 5 && strcmp(log.entries[0], "offset") == 0 &&
        strcmp(log.entries[1], "scale") == 0 && strcmp(log.entries[2], "canonical") == 0 &&
        strcmp(log.entries[3], "first:15:15") == 0 && strcmp(log.entries[4], "second:15:15") == 0);
  reg = make_registration("q3:other", "replace:amount", 0, QA_MOD_REPLACE);
  reg.replace = replace_plus_one;
  CHECK_ABORTS((void)qa_mod_operation_register(&operation, reg));
  reg = make_registration("q1:mod", "scale:amount", 0, QA_MOD_TRANSFORM);
  reg.transform = transform_scale;
  CHECK_ABORTS((void)qa_mod_operation_register(&operation, reg));
  qa_mod_operation_unregister(&operation, remove_id);
  qa_mod_operation_clear(&operation);
  CHECK(!qa_mod_operation_active(&operation));
  qa_mod_operation_dispatch(&operation, &input, canonical, NULL, &output);
  CHECK(output == 4);
  qa_mod_operation_destroy(&operation);
}

/* --- inventory + armor + transitions --- */
static void test_inventory(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "inventory"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q1:game", "q1:player");
  qa_inventory_entry entries[2];
  memset(entries, 0, sizeof entries);
  entries[0].item = "q1:shells"; entries[0].count = 10; entries[0].capacity = 100;
  entries[1].item = "q1:shotgun"; entries[1].count = 0; entries[1].capacity = 1;
  qa_shared_inventory_table_create_entries(inventory, actor, entries, 2);
  CHECK(qa_shared_inventory_table_count(inventory, actor.id, "q1:shells") == 10);
  CHECK(qa_shared_inventory_table_give(inventory, actor, "q1:shells", 5) == 5);
  CHECK(qa_shared_inventory_table_give(inventory, actor, "q1:shells", 1000) == 85);
  CHECK(qa_shared_inventory_table_count(inventory, actor.id, "q1:shells") == 100);
  CHECK(!qa_shared_inventory_table_consume(inventory, actor, "q1:shells", 101));
  CHECK(qa_shared_inventory_table_consume(inventory, actor, "q1:shells", 40));
  CHECK(qa_shared_inventory_table_count(inventory, actor.id, "q1:shells") == 60);
  CHECK(qa_shared_inventory_table_give(inventory, actor, "q1:absent", 1) == 0);
  CHECK_ABORTS((void)qa_inventory_give(entries[0], -1));
  qa_shared_inventory_table_destroy(inventory);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

static void test_armor(void) {
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.regular.kind = QA_REGULAR_ARMOR_Q1;
  armor.regular.points = 100;
  armor.regular.absorption = 0.8;
  armor.regular.item = "q1:armor/red";
  armor.powered.kind = "none";
  qa_armor_damage_flags flags;
  memset(&flags, 0, sizeof flags);
  qa_victim_armor_context context;
  memset(&context, 0, sizeof context);
  context.screen_facing_dot = 1;
  context.arithmetic = "binary32";
  context.has_q2 = true;
  context.q2_product = "classic";
  context.q2_alive = true;
  qa_armor_result result = qa_absorb_native_armor(armor, 50, flags, context);
  CHECK(result.regular_saved == 40);
  CHECK(result.armor.regular.points == 60);
  flags.no_armor = true;
  result = qa_absorb_native_armor(armor, 50, flags, context);
  CHECK(result.regular_saved == 0 && result.power_saved == 0);
}

typedef struct transition_log { int applied; } transition_log;
static void transition_apply(void *ctx, qa_transition_decision decision) {
  (void)decision;
  ((transition_log *)ctx)->applied++;
}
static void test_transitions(void) {
  transition_log log = {0};
  qa_shared_transition_coordinator *coordinator =
      qa_shared_transition_coordinator_create(transition_apply, &log);
  qa_mission_gate gates[1] = {{"q1:gate", false}};
  qa_transition_intent intents[1];
  memset(intents, 0, sizeof intents);
  intents[0].kind = "campaign-level";
  intents[0].campaign = "q1:campaign";
  intents[0].map = "q1:e1m2";
  intents[0].spawn_point = "";
  intents[0].gates = gates;
  intents[0].gate_count = 1;
  qa_transition_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = "campaign";
  mode.campaign = "q1:campaign";
  int64_t id = 0;
  qa_transition_decision decision =
      qa_shared_transition_coordinator_resolve(coordinator, mode, intents, 1, &id);
  CHECK(strcmp(decision.kind, "stay") == 0 && decision.blocked_count == 1);
  gates[0].satisfied = true;
  decision = qa_shared_transition_coordinator_resolve(coordinator, mode, intents, 1, &id);
  CHECK(strcmp(decision.kind, "travel") == 0 && strcmp(decision.map, "q1:e1m2") == 0);
  qa_shared_transition_coordinator_commit(coordinator, id, decision);
  CHECK(log.applied == 1);
  CHECK_ABORTS(qa_shared_transition_coordinator_commit(coordinator, id, decision));
  qa_shared_transition_coordinator_destroy(coordinator);
}

/* --- authority: one authoritative Q1 hit --- */
typedef struct hook_log { int confirmed; int before; } hook_log;
static void hook_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) {
  (void)ctx; (void)actor; (void)impulse; (void)movement;
}
static void hook_before(void *ctx, qa_owned_actor actor, qa_damage_decision decision) {
  (void)actor; (void)decision;
  ((hook_log *)ctx)->before++;
}
static void hook_confirmed(void *ctx, qa_damage_outcome outcome) {
  (void)outcome;
  ((hook_log *)ctx)->confirmed++;
}
static qa_q1_combat_context q1_context(void *ctx, qa_damage_request request,
                                       qa_combat_state target, const qa_combat_state *attacker) {
  (void)ctx; (void)request; (void)target; (void)attacker;
  qa_q1_combat_context context;
  memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  return context;
}
static qa_victim_armor_context armor_context(void *ctx, qa_damage_request request) {
  (void)ctx; (void)request;
  qa_victim_armor_context context;
  memset(&context, 0, sizeof context);
  context.screen_facing_dot = 1;
  context.arithmetic = "binary32";
  context.has_q2 = true;
  context.q2_product = "classic";
  context.q2_alive = true;
  return context;
}
static void test_authority(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "authority"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  hook_log log = {0, 0};
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &log;
  hooks.impulse = hook_impulse;
  hooks.before_reaction = hook_before;
  hooks.confirmed = hook_confirmed;
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, hooks);
  qa_q1_combat_policy_options options;
  memset(&options, 0, sizeof options);
  options.id = "q1:combat";
  options.context = q1_context;
  options.armor = qa_native_victim_armor(armor_context, NULL);
  (void)qa_gameplay_authority_register(combat, qa_create_q1_combat_policy(options));
  qa_owned_actor target = qa_session_actor_registry_allocate(actors, "q2:game", "q2:monster");
  qa_combat_state initial;
  memset(&initial, 0, sizeof initial);
  initial.health = 100;
  initial.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  initial.armor.powered.kind = "none";
  initial.mass = 200;
  initial.can_take_damage = true;
  qa_gameplay_authority_create_state(combat, target, initial, NULL, NULL);
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = target.id;
  request.amount = 30;
  request.delivery = "direct";
  request.attack.sequence = 1;
  request.attack.time.kind = QA_SOURCE_TIME_SECONDS;
  request.attack.time.value = 1;
  request.attack.weapon = "q3:weapon/railgun";
  request.attack.weapon_provider = "q3:weapons";
  request.attack.combat_provider = "q1:combat";
  request.attack.inventory_provider = "q2:inventory";
  request.attack.movement_provider = "q1:movement";
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q3;
  request.attack.cause.means_of_death = 10;
  qa_damage_outcome outcome = qa_gameplay_authority_apply(combat, request, NULL, NULL);
  CHECK(strcmp(outcome.kind, "committed") == 0);
  CHECK(outcome.decision.applied_damage == 30);
  qa_combat_state_result after = qa_gameplay_authority_read(combat, target.id);
  CHECK(after.has_value && after.value.health == 70);
  CHECK(log.confirmed == 1 && log.before == 1);
  qa_gameplay_authority_destroy(combat);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

/* --- scheduler + spatial --- */
typedef struct think_log { int calls; double time; } think_log;
static void think_call(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)self;
  think_log *log = ctx;
  log->calls++;
  log->time = frame.time.value;
}
static qa_think_callback_result think_resolve(void *ctx, qa_provider_id provider,
                                              qa_callback_id callback) {
  (void)provider; (void)callback;
  qa_think_callback_result result;
  result.has_value = true;
  result.value.ctx = ctx;
  result.value.call = think_call;
  return result;
}
static void test_scheduler(void) {
  qa_clock_profile q1_profile;
  memset(&q1_profile, 0, sizeof q1_profile);
  q1_profile.kind = QA_CLOCK_Q1_NETQUAKE;
  qa_frame_context frame;
  memset(&frame, 0, sizeof frame);
  frame.time.kind = QA_SOURCE_TIME_SECONDS; frame.time.value = 5;
  frame.elapsed.kind = QA_SOURCE_TIME_SECONDS; frame.elapsed.value = 0.5;
  qa_source_time due = {QA_SOURCE_TIME_SECONDS, 5.4};
  qa_source_time_result time = qa_think_callback_time(q1_profile, due, frame);
  CHECK(time.has_value && time.value.value == 5.4);
  due.value = 6;
  CHECK(!qa_think_callback_time(q1_profile, due, frame).has_value);

  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "scheduler"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q1:game", "q1:monster");
  think_log log = {0, 0};
  qa_scheduler_options options;
  memset(&options, 0, sizeof options);
  options.actors = qa_session_actor_registry_as_contract(actors);
  options.ordering.kind = QA_FRAME_ORDERING_NATIVE;
  options.ordering.clock = q1_profile;
  options.ctx = &log;
  options.resolve = think_resolve;
  qa_frame_scheduler *scheduler = qa_frame_scheduler_create(options);
  CHECK(scheduler != NULL);
  qa_think_timing timing;
  memset(&timing, 0, sizeof timing);
  timing.due.kind = QA_SOURCE_TIME_SECONDS;
  timing.due.value = 5;
  timing.boundary = QA_THINK_AFTER_PHYSICS;
  timing.order.provider = "q1:game";
  timing.order.actor = actor.id;
  timing.order.sequence = 0;
  qa_frame_scheduler_schedule(scheduler, actor, "q1:think", timing);
  CHECK(qa_frame_scheduler_pending(scheduler, actor.id).has_value);
  qa_think_result boundary =
      qa_frame_scheduler_run(scheduler, actor.id, frame, QA_THINK_BEFORE_PHYSICS);
  CHECK(!boundary.ran && strcmp(boundary.reason, "boundary") == 0);
  qa_think_result ran = qa_frame_scheduler_run(scheduler, actor.id, frame, QA_THINK_AFTER_PHYSICS);
  CHECK(ran.ran && ran.invocations == 1 && log.calls == 1 && log.time == 5);
  qa_frame_scheduler_close(scheduler);
  qa_frame_scheduler_destroy(scheduler);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

static void test_spatial(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "spatial"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q1:game", "q1:item");
  qa_bounds world = {{-100, -100, -100}, {100, 100, 100}};
  qa_spatial_index *index = qa_spatial_index_create(world);
  qa_linked_body body;
  memset(&body, 0, sizeof body);
  body.actor = actor.id;
  body.state = make_body(0);
  body.absolute_bounds.min.x = -16; body.absolute_bounds.max.x = 16;
  body.absolute_bounds.min.y = -16; body.absolute_bounds.max.y = 16;
  qa_actor_collision collision;
  memset(&collision, 0, sizeof collision);
  collision.family = "q1";
  collision.shape_kind = "box";
  collision.role = "trigger";
  qa_spatial_index_link(index, body, collision);
  size_t count = 0;
  const qa_spatial_actor *found = qa_spatial_index_query(index, world, "trigger", &count);
  CHECK(count == 1 && qa_same_actor(found[0].body.actor, actor.id));
  (void)qa_spatial_index_query(index, world, "solid", &count);
  CHECK(count == 0);
  qa_spatial_index_unlink(index, actor.id);
  (void)qa_spatial_index_query(index, world, "both", &count);
  CHECK(count == 0);
  qa_spatial_index_destroy(index);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  test_registry();
  test_source_slots();
  test_bodies();
  test_mod_operation();
  test_inventory();
  test_armor();
  test_transitions();
  test_authority();
  test_scheduler();
  test_spatial();
  if (failures != 0) {
    fprintf(stderr, "FAILURES %d\n", failures);
    return EXIT_FAILURE;
  }
  printf("PASS %d/%d tests/world/test_world_actors.c\n", checks, checks);
  return EXIT_SUCCESS;
}
