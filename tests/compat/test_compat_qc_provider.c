/*
 * Port of tests/compat/qc/mod-provider.test.ts. The TS cases are
 * skipIf-gated on a local Copper progs.dat; the items-overlap case
 * is constructible from a synthetic program (below), but the two
 * Copper execution cases (T_Heal through SessionMods and the
 * impulse-helper spawn checkpoint cycle) have no asset-free
 * equivalent and are skipped here, as they are in TS without the
 * file. The provider invoke/checkpoint cases are the asset-free
 * counterpart of the helper case's invoke/checkpoint path.
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

#include "qa/compat/qc/mod_items.h"
#include "qa/compat/qc/mod_provider.h"
#include "qa/core/numeric.h"
#include "qa/world/actors/callbacks.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                                  \
  } while (0)

#define CHECK_ABORTS(...)                                              \
  do {                                                                 \
    checks++;                                                          \
    pid_t pid = fork();                                                \
    if (pid == 0) {                                                    \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                               \
      __VA_ARGS__;                                                     \
      _exit(0);                                                        \
    }                                                                  \
    int status = 0;                                                    \
    waitpid(pid, &status, 0);                                          \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {       \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,    \
              __LINE__, #__VA_ARGS__);                                 \
    }                                                                  \
  } while (0)

#define DIGEST \
  "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static qa_compat_qc_definition FIELDS[] = {
    {"float", 3, false, 0, (char *)"health"},
    {"float", 3, false, 1, (char *)"ammo_shells"},
    {"float", 3, false, 2, (char *)"ammo_nails"},
    {"float", 3, false, 3, (char *)"max_health"},
    {"vector", 2, false, 4, (char *)"origin"},
    {"float", 3, false, 7, (char *)"armorvalue"},
    {"vector", 2, false, 8, (char *)"angles"},
    {"vector", 2, false, 11, (char *)"view_ofs"},
    /* createQcSourceSlotStorage (entity-host.ts) requires these. */
    {"string", 1, false, 14, (char *)"model"},
    {"float", 3, false, 15, (char *)"takedamage"},
    {"float", 3, false, 16, (char *)"modelindex"},
    {"float", 3, false, 17, (char *)"colormap"},
    {"float", 3, false, 18, (char *)"skin"},
    {"float", 3, false, 19, (char *)"frame"},
    {"float", 3, false, 20, (char *)"solid"},
    {"float", 3, false, 21, (char *)"nextthink"},
};
static qa_compat_qc_definition GLOBALS[] = {
    {"entity", 6, false, 40, (char *)"self"},
    {"float", 3, false, 41, (char *)"time"},
    {"float", 3, false, 42, (char *)"frametime"},
};
static int64_t ECHO_SIZES[] = {1};
static qa_compat_qc_function FUNCTIONS[] = {
    {0, 0, 0, 0, (char *)"", (char *)"", NULL, 0, false},
    {1, 1, 28, 1, (char *)"echo", (char *)"test.qc", ECHO_SIZES, 1, false},
};
static qa_compat_qc_statement STATEMENTS[] = {
    {QA_QC_OP_DONE, 0, 0, 0},
    {QA_QC_OP_RETURN, 28, 0, 0},
};
static uint8_t INITIAL_GLOBALS[256];
static uint8_t STRINGS[] = {0};

static qa_compat_qc_program program = {
    (char *)"test.qc",
    {QA_QUAKEC_API_Q1_NETQUAKE, 6, 5927},
    STATEMENTS, 2,
    GLOBALS, 3,
    FIELDS, 16,
    FUNCTIONS, 2,
    STRINGS, 1,
    INITIAL_GLOBALS, 256,
    22, 0, (char *)DIGEST,
};

static qa_mod_callback_declaration base_declaration(void) {
  qa_mod_callback_declaration decl;
  memset(&decl, 0, sizeof decl);
  decl.version = 1;
  decl.runtime = "quakec";
  decl.program.path = "progs.dat";
  decl.program.digest = DIGEST;
  return decl;
}

static void test_validate_basics(void) {
  qa_mod_actor_field fields[] = {
      {.field = "health", .binding = "health"},
  };
  qa_mod_callback_declaration decl = base_declaration();
  decl.actor_fields = fields;
  decl.actor_field_count = 1;
  qa_compat_qc_validate_mod(&program, decl);

  qa_mod_callback_declaration wrong_digest = decl;
  wrong_digest.program.digest =
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, wrong_digest));

  qa_mod_actor_field missing[] = {{.field = "nope", .binding = "private"}};
  qa_mod_callback_declaration missing_decl = decl;
  missing_decl.actor_fields = missing;
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, missing_decl));

  qa_mod_actor_field wrong_type[] = {{.field = "origin", .binding = "health"}};
  qa_mod_callback_declaration wrong_type_decl = decl;
  wrong_type_decl.actor_fields = wrong_type;
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, wrong_type_decl));

  qa_mod_actor_field overlap[] = {
      {.field = "health", .binding = "private"},
      {.field = "health", .binding = "private"},
  };
  qa_mod_callback_declaration overlap_decl = decl;
  overlap_decl.actor_fields = overlap;
  overlap_decl.actor_field_count = 2;
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, overlap_decl));

  qa_mod_named_string cvars[] = {{"a", "1"}, {"a", "2"}};
  qa_mod_callback_declaration cvar_decl = decl;
  cvar_decl.cvars = cvars;
  cvar_decl.cvar_count = 2;
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, cvar_decl));

  qa_mod_qc_clients too_many;
  memset(&too_many, 0, sizeof too_many);
  too_many.maximum = 8191;
  qa_mod_callback_declaration clients_decl = decl;
  clients_decl.clients = &too_many;
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, clients_decl));

  qa_qc_mod_client_presentation presentation = {"replace-vitals", "none"};
  qa_mod_callback_declaration pres_decl = decl;
  pres_decl.client_presentation = &presentation;
  CHECK_ABORTS(qa_compat_qc_validate_mod(&program, pres_decl));
}

/* TS case 1: shared capacity fields are valid while count and
 * capacity aliases of count storage are invalid. */
static void test_items_overlap(void) {
  qa_mod_actor_field fields[] = {
      {.field = "ammo_shells", .binding = "private"},
      {.field = "ammo_nails", .binding = "private"},
      {.field = "max_health", .binding = "private"},
  };
  qa_mod_qc_item_definition definitions[] = {
      {.item = "test:shells", .label = "Shells", .admission = "add"},
      {.item = "test:nails", .label = "Nails", .admission = "add"},
  };
  qa_mod_qc_item_storage storage[] = {
      {.kind = "counter",
       .field = "ammo_shells",
       .item = "test:shells",
       .capacity = {false, 0, "max_health"}},
      {.kind = "counter",
       .field = "ammo_nails",
       .item = "test:nails",
       .capacity = {false, 0, "max_health"}},
  };
  qa_mod_qc_items items = {definitions, 2, storage, 2, NULL};
  qa_mod_qc_clients clients;
  memset(&clients, 0, sizeof clients);
  clients.maximum = 1;
  qa_mod_callback_declaration decl = base_declaration();
  decl.actor_fields = fields;
  decl.actor_field_count = 3;
  decl.clients = &clients;
  decl.items = &items;
  qa_compat_qc_validate_items(&program, decl);
  qa_compat_qc_validate_mod(&program, decl);

  for (int i = 0; i < 2; i++) {
    qa_mod_qc_item_storage aliased[2] = {storage[0], storage[1]};
    aliased[1].field = i == 0 ? "ammo_shells" : "max_health";
    qa_mod_qc_items bad_items = items;
    bad_items.storage = aliased;
    qa_mod_callback_declaration bad = decl;
    bad.items = &bad_items;
    CHECK_ABORTS(qa_compat_qc_validate_items(&program, bad));
  }
}

static qa_bounds abs_bounds(void *ctx, qa_owned_actor a, qa_body_state s) {
  (void)ctx;
  return qa_translated_body_bounds(a, s);
}
static void on_link(void *ctx, qa_linked_body b) { (void)ctx; (void)b; }
static void on_unlink(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }
static qa_source_time fixed_time(void *ctx) {
  (void)ctx;
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, 3};
}
static double random_integer(void *ctx) {
  return (double)qa_q3_random_next_integer(ctx);
}
static double random_unit(void *ctx) { return qa_q3_random_next_unit(ctx); }
static qa_random_state random_checkpoint(void *ctx) {
  return qa_q3_random_checkpoint(ctx);
}
static void random_restore(void *ctx, qa_random_state state) {
  qa_q3_random_restore(ctx, state);
}

static void test_provider_invoke(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "qc-provider-test"));
  qa_session_actor_registry *actors =
      qa_session_actor_registry_create(&identities, 1024);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_body_link_hooks body_hooks = {NULL, abs_bounds, on_link, on_unlink};
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, body_hooks);
  qa_shared_inventory_table *inventory =
      qa_shared_inventory_table_create(actors);
  qa_gameplay_hooks combat_hooks;
  memset(&combat_hooks, 0, sizeof combat_hooks);
  qa_gameplay_authority *combat =
      qa_gameplay_authority_create(actors, callbacks, combat_hooks);
  CHECK(actors && callbacks && bodies && inventory && combat);

  qa_owned_actor target =
      qa_session_actor_registry_allocate(actors, "q2:game",
                                         "q2:monster_soldier");

  qa_mod_actor_field fields[] = {
      {.field = "health", .binding = "health"},
      {.field = "max_health",
       .binding = "constant",
       .constant_value = {.kind = "float", .number_value = 100}},
  };
  qa_mod_callback_value arguments[] = {{.kind = "input", .name = "amount"}};
  qa_mod_named_value globals[] = {
      {"self", {.kind = "input", .name = "self"}},
      {"time", {.kind = "input", .name = "time"}},
  };
  qa_mod_callback callbacks_decl[] = {
      {.call = {"echo", arguments, 1, globals, 2},
       .binding = {"test:echo", "damage", "observe", NULL}},
  };
  qa_mod_callback_declaration decl = base_declaration();
  decl.actor_fields = fields;
  decl.actor_field_count = 2;
  decl.callbacks = callbacks_decl;
  decl.callback_count = 1;

  qa_mod_host_services services;
  memset(&services, 0, sizeof services);
  services.actors = actors;
  services.bodies = bodies;
  services.combat = combat;
  services.inventory = inventory;
  services.callbacks = callbacks;
  services.seed = 17;
  services.time = fixed_time;

  qa_q3_random q3;
  qa_q3_random_init(&q3, 17, 0);
  qa_compat_qc_mod_random random = {
      {&q3, random_integer, random_unit, random_checkpoint}, random_restore};

  qa_module_identity module = {"mod:test", "progs.dat", DIGEST, "test"};
  qa_compat_qc_mod_provider provider;
  CHECK(qa_compat_qc_mod_provider_init(&provider, &program, module, decl,
                                       &services, &random, NULL, NULL, NULL,
                                       0));
  qa_actor_id self = target.id;
  qa_compat_qc_mod_input_entry inputs[3];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &self;
  inputs[1].name = "amount";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = 15.5;
  inputs[2].name = "time";
  inputs[2].value.kind = "float";
  inputs[2].value.number_value = 3;
  double result = qa_compat_qc_mod_provider_invoke(
      &provider, callbacks_decl[0].call, inputs, 3, NULL);
  CHECK(result == (double)(float)15.5);

  int64_t reference = qa_compat_qc_mod_provider_reference(&provider, &self);
  CHECK(reference != 0);
  qa_actor_id round_trip =
      qa_compat_qc_mod_provider_actor(&provider, reference);
  CHECK(qa_same_actor(round_trip, self));
  CHECK(qa_compat_qc_mod_provider_reference(&provider, NULL) == 0);
  CHECK_ABORTS(qa_compat_qc_mod_provider_actor(&provider, 0));

  /* The constant field was projected at reference() time. */
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(
      &provider.entities, reference);
  CHECK(qa_compat_qc_words_float(words, 3) == 100.0);

  const qa_guest_checkpoint *saved =
      qa_compat_qc_mod_provider_checkpoint(&provider);
  CHECK(saved != NULL);
  inputs[1].value.number_value = 7.25;
  CHECK(qa_compat_qc_mod_provider_invoke(&provider, callbacks_decl[0].call,
                                         inputs, 3, NULL) ==
        (double)(float)7.25);
  qa_compat_qc_mod_provider_restore(&provider, saved);
  CHECK(qa_compat_qc_mod_provider_invoke(&provider, callbacks_decl[0].call,
                                         inputs, 3, NULL) ==
        (double)(float)7.25);

  qa_compat_qc_mod_provider_close(&provider);
  CHECK_ABORTS(qa_compat_qc_mod_provider_invoke(&provider,
                                                callbacks_decl[0].call, inputs,
                                                3, NULL));
  qa_compat_qc_mod_provider_destroy(&provider);

  qa_gameplay_authority_destroy(combat);
  qa_shared_inventory_table_destroy(inventory);
  qa_shared_body_table_destroy(bodies);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  test_validate_basics();
  test_items_overlap();
  test_provider_invoke();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok (%d checks)\n", checks);
  return 0;
}
