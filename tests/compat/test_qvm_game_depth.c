/*
 * Depth checks for the primary QVM game subsystems (src/compat/qvm/game-*.ts),
 * reproducing tests/compat/qvm/game-inventory.test.ts and the TS try/catch
 * contract of game-combat-scope.ts: a damage frame catches its own cancellation
 * at proceed and returns its result, while other errors propagate with the
 * frame's finally cleanup. Every callback checks its sentinel context.
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

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/game_combat_scope.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/game_inventory.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/module.h"
#include "qa/content/mounts.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"

/* TS Q3GameMemory view of an interpreted allocation (compat/qvm/game_memory.h). */
#define QA_GAME(m) ((const qa_qvm_game_memory[]){qa_qvm_memory_game_memory(m)})

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

#define CHECK_ABORTS(...)                                                    \
  do {                                                                       \
    checks++;                                                                \
    fflush(stdout);                                                          \
    fflush(stderr);                                                          \
    pid_t pid = fork();                                                      \
    if (pid == 0) {                                                          \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);               \
      __VA_ARGS__;                                                           \
      _exit(0);                                                              \
    }                                                                        \
    int status = 0;                                                          \
    waitpid(pid, &status, 0);                                                \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {             \
      failures++;                                                            \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,          \
              __LINE__, #__VA_ARGS__);                                       \
    }                                                                        \
  } while (0)

static int sentinel_value;
static void *const SENTINEL = &sentinel_value;
typedef struct fixture fixture;
typedef struct scope_world scope_world;
static fixture *current_fixture;
static scope_world *current_scope;

/* ---------------- fixture: TS fixture() of game-inventory.test.ts ---------------- */

static void put_u8(uint8_t *out, size_t *at, uint8_t value) { out[(*at)++] = value; }
static void put_i32(uint8_t *out, size_t *at, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) out[(*at)++] = (uint8_t)(bits >> (8 * i));
}

struct fixture {
  uint8_t bytes[64];
  size_t length;
  char *digest;
  qa_resolved_qvm_artifact artifact;
  qa_qvm_module module;
  qa_qvm_game_data data;
  bool live;
  double capacity;
  qa_qvm_public_inventory_profile profile;
  qa_qvm_inventory_weapon weapons[2];
  qa_qvm_inventory_options options;
  qa_inventory_state_binding binding;
};

static fixture *fixture_of(void *ctx) {
  CHECK(ctx == SENTINEL);
  return current_fixture;
}

static double fixture_client(void *ctx) {
  fixture *f = fixture_of(ctx);
  if (!f->live) qa_range_error("Retired player");
  return 0;
}

static double fixture_capacity(void *ctx, qa_qvm_memory *memory, double weapon, qa_qvm_capacity_context context) {
  (void)memory;
  (void)weapon;
  (void)context;
  return fixture_of(ctx)->capacity;
}

static void fixture_artifact(fixture *f) {
  uint8_t code[16];
  size_t at = 0;
  put_u8(code, &at, QA_QVM_OP_ENTER);
  put_i32(code, &at, 0);
  put_u8(code, &at, QA_QVM_OP_CONST);
  put_i32(code, &at, 0);
  put_u8(code, &at, QA_QVM_OP_LEAVE);
  put_i32(code, &at, 0);
  size_t length = 0;
  const int32_t header[8] = {0x12721444, 3, 32, (int32_t)at, 32 + (int32_t)at, 0, 0, 16384};
  for (int i = 0; i < 8; i++) put_i32(f->bytes, &length, header[i]);
  memcpy(f->bytes + length, code, at);
  f->length = length + at;
  f->digest = qa_digest_bytes(f->bytes, f->length);
  qa_qvm_resolve_artifact_options options;
  memset(&options, 0, sizeof options);
  options.module.id = "test:inventory";
  options.module.artifact_path = "vm/qagame.qvm";
  options.module.revision = "test";
  options.module.digest = f->digest;
  options.role = "qagame";
  options.bytes = f->bytes;
  options.length = f->length;
  CHECK(qa_qvm_resolve_artifact(options, &f->artifact));
}

static void fixture_init(fixture *f) {
  memset(f, 0, sizeof *f);
  current_fixture = f;
  fixture_artifact(f);
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = &f->artifact;
  options.host = qa_qvm_reject_syscall_host;
  qa_qvm_module_init(&f->module, &options);
  qa_qvm_game_data_init(&f->data, QA_GAME(f->module.memory), qa_qvm_module_get_abi_profile(&f->module));
  qa_qvm_game_data_set_client_count(&f->data, 1);
  qa_qvm_game_data_locate(&f->data, 64, 1, 808, 2048, 776);
  f->live = true;
  f->capacity = 50;
  f->profile.module = f->artifact.module;
  f->profile.abi_profile = qa_qvm_module_get_abi_profile(&f->module);
  f->profile.weapons_offset = 204;
  f->profile.ammo_offset = 376;
  f->profile.ctx = SENTINEL;
  f->profile.capacity = fixture_capacity;
  f->weapons[0] = (qa_qvm_inventory_weapon){3, "q3:weapon/shotgun", "q3:ammo/shotgun"};
  f->weapons[1] = f->weapons[0];
  f->options.module = &f->module;
  f->options.data = &f->data;
  f->options.profile.public_profile = &f->profile;
  f->options.weapons.list = f->weapons;
  f->options.weapons.count = 1;
  f->options.ctx = SENTINEL;
  f->options.client = fixture_client;
  f->binding = qa_qvm_inventory_binding(f->options);
}

static void fixture_destroy(fixture *f) {
  qa_qvm_inventory_binding_destroy(&f->binding);
  qa_qvm_game_data_destroy(&f->data);
  qa_qvm_module_destroy(&f->module);
  qa_resolved_qvm_artifact_destroy(&f->artifact);
  free(f->digest);
}

static qa_qvm_writable_view player_view(fixture *f) {
  qa_qvm_writable_view view;
  qa_qvm_game_data_public_player_bytes(&f->data, 0, &view);
  return view;
}

static bool find_entry(fixture *f, const char *item, qa_inventory_entry *out) {
  size_t count = 0;
  const qa_inventory_entry *entries = f->binding.read(f->binding.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, item) == 0) {
      *out = entries[i];
      return true;
    }
  return false;
}

typedef struct write_observation {
  fixture *f;
  double observed[4];
  size_t count;
} write_observation;

static write_observation *active_observation;
static void observe_ammo_count(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  CHECK(ctx == SENTINEL);
  qa_inventory_entry entry;
  if (active_observation->count < 4 && find_entry(active_observation->f, "q3:ammo/shotgun", &entry))
    active_observation->observed[active_observation->count++] = entry.count;
}

static void write_entry(fixture *f, qa_inventory_entry entry) { f->binding.write(f->binding.ctx, entry); }

static void write_ammo_wrong_capacity(void) {
  fixture f;
  fixture_init(&f);
  qa_inventory_entry ammo;
  find_entry(&f, "q3:ammo/shotgun", &ammo);
  ammo.count = 4;
  ammo.capacity = 50;
  f.capacity = 20;
  write_entry(&f, ammo);
}

static void test_inventory_source_counters(void) {
  fixture f;
  fixture_init(&f);
  qa_qvm_writable_view ps = player_view(&f);
  qa_qvm_writable_view_set_int32(&ps, 204, 1 << 2, true);
  qa_qvm_writable_view_set_int32(&ps, 388, -1, true);
  qa_qvm_writable_view_set_int32(&ps, 392, 731, true);
  qa_inventory_entry ammo;
  CHECK(find_entry(&f, "q3:ammo/shotgun", &ammo));
  CHECK(ammo.count == -1 && ammo.capacity == 50 && ammo.has_count_policy &&
        strcmp(ammo.count_policy.kind, "source-counter") == 0 &&
        strcmp(ammo.count_policy.arithmetic, "int32") == 0);
  write_observation observation = {&f, {0}, 0};
  active_observation = &observation;
  qa_qvm_write_range range = {2048 + 388, 4};
  size_t watch = qa_qvm_memory_observe_writes(f.module.memory, &range, 1, observe_ammo_count, SENTINEL,
                                              NULL, NULL);
  qa_inventory_entry lowered = ammo;
  lowered.count = -3;
  write_entry(&f, lowered);
  qa_qvm_memory_writes_unobserve(&f.module.memory->writes, watch);
  CHECK(observation.count == 1 && observation.observed[0] == -3);
  CHECK(qa_qvm_writable_view_get_int32(&ps, 392, true) == 731);
  qa_inventory_entry weapon = {"q3:weapon/shotgun", 1, 1, false, {NULL, NULL}};
  write_entry(&f, weapon);
  CHECK(qa_qvm_writable_view_get_int32(&ps, 204, true) == ((1 << 2) | (1 << 3)));
  weapon.count = 0;
  write_entry(&f, weapon);
  CHECK(qa_qvm_writable_view_get_int32(&ps, 204, true) == (1 << 2));
  qa_inventory_entry raised = ammo;
  raised.count = 99;
  write_entry(&f, raised);
  f.capacity = 20;
  qa_inventory_entry reread;
  CHECK(find_entry(&f, "q3:ammo/shotgun", &reread) && reread.count == 99 && reread.capacity == 20);
  CHECK(f.binding.mutable_capacity == NULL);
  CHECK_ABORTS(write_ammo_wrong_capacity());
  CHECK(qa_qvm_writable_view_get_int32(&ps, 388, true) == 99);
  fixture_destroy(&f);
}

static void read_retired(void) {
  fixture f;
  fixture_init(&f);
  f.live = false;
  qa_inventory_entry entry;
  (void)find_entry(&f, "q3:ammo/shotgun", &entry);
}

static void read_after_module_retired(void) {
  fixture f;
  fixture_init(&f);
  qa_qvm_module_retire(&f.module);
  qa_inventory_entry entry;
  (void)find_entry(&f, "q3:ammo/shotgun", &entry);
}

static void test_inventory_relocation(void) {
  fixture f;
  fixture_init(&f);
  qa_qvm_writable_view ps = player_view(&f);
  qa_qvm_writable_view_set_int32(&ps, 388, 41, true);
  qa_qvm_game_data_state original = qa_qvm_game_data_checkpoint(&f.data);
  qa_qvm_game_data_locate(&f.data, 64, 1, 808, 4096, 776);
  qa_qvm_writable_view relocated = player_view(&f);
  qa_qvm_writable_view_set_int32(&relocated, 388, 17, true);
  qa_inventory_entry entry;
  CHECK(find_entry(&f, "q3:ammo/shotgun", &entry) && entry.count == 17);
  qa_qvm_game_data_restore(&f.data, original);
  CHECK(find_entry(&f, "q3:ammo/shotgun", &entry) && entry.count == 41);
  CHECK_ABORTS(read_retired());
  CHECK_ABORTS(read_after_module_retired());
  fixture_destroy(&f);
}

static void other_revision(void) {
  fixture f;
  fixture_init(&f);
  f.profile.module.revision = "other";
  (void)qa_qvm_inventory_binding(f.options);
}
static void duplicated_weapons(void) {
  fixture f;
  fixture_init(&f);
  f.options.weapons.count = 2;
  (void)qa_qvm_inventory_binding(f.options);
}
static void weapon_count_two(void) {
  fixture f;
  fixture_init(&f);
  write_entry(&f, (qa_inventory_entry){"q3:weapon/shotgun", 2, 1, false, {NULL, NULL}});
}
static void ammo_without_policy(void) {
  fixture f;
  fixture_init(&f);
  write_entry(&f, (qa_inventory_entry){"q3:ammo/shotgun", 1, 50, false, {NULL, NULL}});
}
static void unadmitted_item(void) {
  fixture f;
  fixture_init(&f);
  write_entry(&f, (qa_inventory_entry){"q3:ammo/other", 1, 50, false, {NULL, NULL}});
}
static void ammo_count(double count) {
  fixture f;
  fixture_init(&f);
  qa_inventory_entry ammo;
  find_entry(&f, "q3:ammo/shotgun", &ammo);
  ammo.count = count;
  write_entry(&f, ammo);
}

static void test_inventory_rejections(void) {
  CHECK_ABORTS(other_revision());
  CHECK_ABORTS(duplicated_weapons());
  CHECK_ABORTS(weapon_count_two());
  CHECK_ABORTS(ammo_without_policy());
  CHECK_ABORTS(unadmitted_item());
  const double counts[] = {0.5, 2147483648.0, -2147483649.0, INFINITY};
  for (int i = 0; i < 4; i++) CHECK_ABORTS(ammo_count(counts[i]));
}

/* ---------------- game-combat-scope.ts: catch at proceed ---------------- */

#define HEALTH 724
#define PAIN 700
#define DIE 704

struct scope_world {
  fixture base; /* artifact bytes */
  qa_qvm_game game;
  qa_qvm_damage_scopes *scopes;
  qa_identity_owner owner;
  qa_owned_actor actor;
  int mode; /* 0 cancel own frame, 1 unrelated error */
  qa_damage_mutation stored[4];
  size_t stored_count;
  int32_t pointer;
};

static scope_world *scope_of(void *ctx) {
  CHECK(ctx == SENTINEL);
  return current_scope;
}

static qa_armor_state scope_armor(void *ctx, double slot) {
  (void)scope_of(ctx);
  (void)slot;
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  armor.powered.kind = "none";
  return armor;
}
static bool scope_live(void *ctx, qa_owned_actor actor) {
  (void)scope_of(ctx);
  (void)actor;
  return true;
}
static void scope_stored(void *ctx, qa_damage_mutation mutation) {
  scope_world *world = scope_of(ctx);
  if (world->stored_count < 4) world->stored[world->stored_count++] = mutation;
}
static void scope_before_reaction(void *ctx, qa_source_damage_result result) {
  (void)scope_of(ctx);
  (void)result;
}

typedef struct fake_call {
  qa_qvm_module_function_call call;
  uint8_t words[8];
  scope_world *world;
} fake_call;

static qa_qvm_cancellation_scope fake_scope(void *ctx) {
  (void)ctx;
  qa_qvm_cancellation_scope scope = {(const void *)0x51};
  return scope;
}
static void fake_cancel(void *ctx, qa_qvm_cancellation_scope scope) {
  (void)ctx;
  CHECK(scope.token == (const void *)0x51);
  qa_range_error("QVM function scope cancelled");
}
static int32_t fake_proceed(void *ctx) {
  fake_call *fake = ctx;
  scope_world *world = fake->world;
  qa_qvm_writable_view entity;
  qa_qvm_game_data_entity_bytes(&world->game.data, 0, &entity);
  qa_qvm_writable_view_set_int32(&entity, HEALTH, 70, true);
  if (world->mode == 0) {
    qa_qvm_damage_frame *frame = qa_qvm_damage_scopes_current(world->scopes, world->pointer);
    CHECK(frame != NULL);
    qa_qvm_damage_scopes_cancel(world->scopes, frame, &fake->call);
  }
  qa_range_error("unrelated source failure");
}

typedef struct run_job {
  scope_world *world;
  fake_call *call;
  qa_source_damage_result result;
} run_job;

static void run_body(void *ctx) {
  run_job *job = ctx;
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = job->world->actor.id;
  request.amount = 30;
  request.delivery = "direct";
  request.attack.movement_provider = "q3:movement";
  qa_source_damage_observer observer = {SENTINEL, scope_stored, scope_before_reaction};
  job->result = qa_qvm_damage_scopes_run(job->world->scopes, &job->call->call, job->world->actor, 0, request,
                                         observer);
}

static void test_damage_scope_catches_own_cancellation(void) {
  scope_world world;
  memset(&world, 0, sizeof world);
  current_scope = &world;
  fixture_artifact(&world.base);
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = &world.base.artifact;
  options.host = qa_qvm_reject_syscall_host;
  qa_qvm_game_init(&world.game, &options);
  qa_qvm_game_data_set_client_count(&world.game.data, 1);
  qa_qvm_game_data_locate(&world.game.data, 64, 1, 808, 2048, 776);
  CHECK(qa_identity_owner_create(&world.owner, "damage-scope"));
  world.actor = qa_identity_owner_owned_actor(&world.owner, qa_identity_owner_actor(&world.owner, 1, 1), "test:game");
  qa_qvm_damage_scope_options scope_options;
  memset(&scope_options, 0, sizeof scope_options);
  scope_options.game = &world.game;
  scope_options.health = HEALTH;
  scope_options.target_argument = 0;
  scope_options.points_stat = 3;
  scope_options.reactions.pain = PAIN;
  scope_options.reactions.die = DIE;
  scope_options.reactions.pain_call = (qa_qvm_reaction_call){3, 0, 2};
  scope_options.reactions.die_call = (qa_qvm_reaction_call){5, 0, 3};
  scope_options.ctx = SENTINEL;
  scope_options.armor = scope_armor;
  scope_options.live = scope_live;
  world.scopes = qa_qvm_damage_scopes_create(scope_options);
  qa_qvm_writable_view entity;
  qa_qvm_game_data_entity_bytes(&world.game.data, 0, &entity);
  qa_qvm_writable_view_set_int32(&entity, HEALTH, 100, true);
  world.pointer = 64; /* entitiesWord + slot * stride */
  fake_call fake;
  memset(&fake, 0, sizeof fake);
  fake.world = &world;
  fake.words[0] = 64;
  fake.call.words = fake.words;
  fake.call.words_length = 8;
  fake.call.execution = "synchronous";
  fake.call.call_ctx = &fake;
  fake.call.cancellation_scope = fake_scope;
  fake.call.cancel_function = fake_cancel;
  fake.call.proceed = fake_proceed;
  run_job job = {&world, &fake, {0, NULL}};
  qa_caught_error caught;
  world.mode = 0;
  CHECK(qa_try(run_body, &job, &caught)); /* the own cancellation stops at proceed */
  CHECK(job.result.applied_damage == 30 && strcmp(job.result.reaction, "none") == 0);
  CHECK(world.stored_count == 1 && world.stored[0].kind == QA_DAMAGE_MUTATION_HEALTH &&
        world.stored[0].health_before == 100 && world.stored[0].health_after == 70);
  CHECK(qa_qvm_damage_scopes_current(world.scopes, 64) == NULL);
  CHECK(!qa_qvm_memory_get_observes_writes(world.game.module.memory));
  /* Any other error leaves through run's finally. */
  qa_qvm_writable_view_set_int32(&entity, HEALTH, 100, true);
  world.mode = 1;
  world.stored_count = 0;
  CHECK(!qa_try(run_body, &job, &caught) && strcmp(caught.message, "unrelated source failure") == 0);
  CHECK(world.stored_count == 1);
  CHECK(qa_qvm_damage_scopes_current(world.scopes, 64) == NULL);
  CHECK(!qa_qvm_memory_get_observes_writes(world.game.module.memory));
  /* The scope stack stays usable after both exits. */
  qa_qvm_writable_view_set_int32(&entity, HEALTH, 100, true);
  world.mode = 0;
  world.stored_count = 0;
  CHECK(qa_try(run_body, &job, &caught) && job.result.applied_damage == 30);
  qa_qvm_damage_scopes_destroy(world.scopes);
  qa_identity_owner_destroy(&world.owner);
  qa_qvm_game_data_destroy(&world.game.data);
  qa_qvm_module_destroy(&world.game.module);
  qa_resolved_qvm_artifact_destroy(&world.base.artifact);
  free(world.base.digest);
}

int main(void) {
  test_inventory_source_counters();
  test_inventory_relocation();
  test_inventory_rejections();
  test_damage_scope_catches_own_cancellation();
  printf("test_qvm_game_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
