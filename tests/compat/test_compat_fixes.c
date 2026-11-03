/*
 * Compat small-defect fixes. Expected values from bun:
 *  - G_ENTITIES_IN_BOX, maximum -1, owner returning [1, 1, 1]
 *    (src/compat/qvm/server-game-syscalls.ts): result 3, areaEntities called once.
 *  - withRereleaseWeaponProvisioning (src/compat/q2/rerelease/
 *    weapon-behavior-profile.ts): the `finally` restores the captured cvars and
 *    refreshes when run throws; tests/compat/q2/rerelease/weapon-behavior.test.ts.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(cond)) {                                                                     \
      failures++;                                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                                  \
  } while (0)

static int sentinel;
#define SENTINEL ((void *)&sentinel)

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = 0;
  for (int i = 0; i < 4; i++) bits |= (uint32_t)bytes[offset + (size_t)i] << (8 * i);
  return (int32_t)bits;
}

/* ---- item 11 ---- */
typedef struct area_owner { int queries; int32_t last_maximum; } area_owner;
static area_owner OWNER;
static size_t three_ones(void *ctx, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity) {
  CHECK(ctx == SENTINEL);
  (void)bounds;
  OWNER.queries++;
  OWNER.last_maximum = maximum;
  for (size_t i = 0; i < 3 && i < capacity; i++) out[i] = 1;
  return 3;
}
typedef struct area_call { qa_qvm_host_call *call; qa_qvm_server_game_services *services; int32_t out; } area_call;
static void run_area_call(void *ctx) {
  area_call *c = ctx;
  (void)qa_qvm_server_game_syscall(c->call, c->services, &c->out);
}
static void test_area_entities_queries_once(void) {
  static uint8_t bytes[16384];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_server_spatial_operations spatial;
  memset(&spatial, 0, sizeof spatial);
  spatial.ctx = SENTINEL;
  spatial.area_entities = three_ones;
  qa_qvm_server_game_services services;
  memset(&services, 0, sizeof services);
  services.spatial = &spatial;
  uint8_t words[32];
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = "qagame";
  call.code = QA_QVM_GAME_IMPORT_G_ENTITIES_IN_BOX;
  call.guest = &memory;
  call.base.words = words;
  call.base.words_length = sizeof words;
  call.base.guest = &memory;
  const int32_t maxima[] = {-1, INT32_MIN, 2048};
  for (size_t i = 0; i < sizeof maxima / sizeof maxima[0]; i++) {
    memset(words, 0, sizeof words);
    put_i32(words, 4, 128);
    put_i32(words, 8, 140);
    put_i32(words, 12, 512);
    put_i32(words, 16, maxima[i]);
    memset(&OWNER, 0, sizeof OWNER);
    int32_t out = -1;
    CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
    CHECK(out == 3);
    CHECK(OWNER.queries == 1);
    CHECK(OWNER.last_maximum == maxima[i]);
    CHECK(get_i32(bytes, 512) == 1 && get_i32(bytes, 516) == 1 && get_i32(bytes, 520) == 1);
  }
  /* TS: three entries at 16380 overrun the 16384-byte allocation and throw
   * from guest.view after one query. */
  memset(words, 0, sizeof words);
  put_i32(words, 4, 128);
  put_i32(words, 8, 140);
  put_i32(words, 12, 16380);
  put_i32(words, 16, -1);
  memset(&OWNER, 0, sizeof OWNER);
  area_call c = {&call, &services, 0};
  qa_caught_error caught;
  CHECK(!qa_try(run_area_call, &c, &caught));
  CHECK(OWNER.queries == 1);
  qa_qvm_memory_destroy(&memory);
}

/* ---- item 1 ---- */
typedef struct provisioning {
  void *sentinel;
  qa_cvar_registry *source;
  double observed[4];
  size_t observed_count;
} provisioning;
static void provisioning_refresh(void *ctx) {
  provisioning *p = ctx;
  CHECK(p->sentinel == SENTINEL);
  if (p->observed_count < 4) p->observed[p->observed_count++] = qa_cvar_registry_variable_value(p->source, "cheats");
}
static void *provisioning_throw(void *ctx) {
  provisioning *p = ctx;
  CHECK(p->sentinel == SENTINEL);
  CHECK(qa_cvar_registry_variable_value(p->source, "cheats") == 1);
  qa_raise("Error", 0, 0, "authored command failure");
}
static void provisioning_call(void *ctx) {
  provisioning *p = ctx;
  static const qa_native_weapon_cvar cvars[] = {{"cheats", "1"}};
  (void)qa_compat_q2_with_rerelease_weapon_provisioning(cvars, 1, p->source, p, provisioning_refresh,
                                                        provisioning_throw);
}
static void test_provisioning_finally(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "native-provisioning-fixes"));
  qa_cvar_registry_options o;
  memset(&o, 0, sizeof o);
  o.dialect = QA_COMMAND_DIALECT_Q2_RERELEASE;
  o.context.session = identity.session;
  o.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *source = qa_cvar_registry_create(&o);
  qa_cvar_snapshot r;
  qa_cvar_registry_register(source, "cheats", "0", QA_Q2_CVAR_FLAG_SERVER_INFO, &r);
  qa_cvar_read_free(&r);
  provisioning p;
  memset(&p, 0, sizeof p);
  p.sentinel = SENTINEL;
  p.source = source;
  qa_caught_error caught;
  CHECK(!qa_try(provisioning_call, &p, &caught));
  CHECK(strcmp(caught.name, "Error") == 0 && strcmp(caught.message, "authored command failure") == 0);
  /* finally: restoreSaveState then refresh, after the provisioning refresh. */
  CHECK(p.observed_count == 2 && p.observed[0] == 1 && p.observed[1] == 0);
  CHECK(qa_cvar_registry_variable_value(source, "cheats") == 0);
  qa_cvar_registry_destroy(source);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_area_entities_queries_once();
  test_provisioning_finally();
  if (failures) {
    fprintf(stderr, "%d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_compat_fixes: %d checks ok\n", checks);
  return 0;
}
