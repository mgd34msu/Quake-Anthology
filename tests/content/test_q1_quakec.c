/*
 * Port of key cases from tests/compat/qc/{armor-stage,pickup-stage,
 * weapon-stage,id1-projectiles,mod-pickups}.test.ts plus the Q1
 * foundation entity-services getters, over synthetic QC programs
 * (the TS integration cases run pinned progs.dat artifacts through
 * the full compat VM, which has no C counterpart yet).
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

#include "qa/content/q1/foundation.h"
#include "qa/content/q1/quakec/armor_points.h"
#include "qa/content/q1/quakec/armor_stage.h"
#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/damage_call.h"
#include "qa/content/q1/quakec/damage_scale.h"
#include "qa/content/q1/quakec/id1_attacks.h"
#include "qa/content/q1/quakec/id1_damage.h"
#include "qa/content/q1/quakec/id1_environment.h"
#include "qa/content/q1/quakec/id1_pickups.h"
#include "qa/content/q1/quakec/id1_program.h"
#include "qa/content/q1/quakec/id1_projectiles.h"
#include "qa/content/q1/quakec/pickup_callers.h"
#include "qa/content/q1/quakec/pickup_stage.h"
#include "qa/content/q1/quakec/weapon_stage.h"
#include "qa/content/q1/quakec/weapon_stage_declaration.h"
#include "qa/world/actors/callbacks.h"

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

#define NQ_DIGEST "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580"
#define QW_DIGEST "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830"

static double num_store(void *ctx, double v) { (void)ctx; return (double)(float)v; }
static double num_add(void *ctx, double a, double b) { (void)ctx; return a + b; }
static double num_sub(void *ctx, double a, double b) { (void)ctx; return a - b; }
static double num_mul(void *ctx, double a, double b) { (void)ctx; return a * b; }
static double num_div(void *ctx, double a, double b) { (void)ctx; return a / b; }
static double num_sqrt(void *ctx, double v) { (void)ctx; return sqrt(v); }
static qa_numeric_operations numeric = {.store = num_store, .add = num_add,
                                        .subtract = num_sub, .multiply = num_mul,
                                        .divide = num_div, .square_root = num_sqrt};
static qa_actor_id make_actor(int64_t slot, int64_t generation) {
  qa_actor_id id;
  memset(&id, 0, sizeof id);
  id.slot = slot;
  id.generation = generation;
  return id;
}

static void put_float(uint8_t *bytes, int64_t word, float value) {
  memcpy(bytes + (size_t)word * 4, &value, 4);
}
static void put_int(uint8_t *bytes, int64_t word, int32_t value) {
  memcpy(bytes + (size_t)word * 4, &value, 4);
}

/* --- damage call layout / read / project --- */
typedef struct exec_log { int runs; } exec_log;
static void exec_run(void *ctx, qa_q1qc_prepare_fn prepare, void *prepare_ctx) {
  ((exec_log *)ctx)->runs++;
  if (prepare != NULL) prepare(prepare_ctx, NULL);
}
static void exec_noop(void *ctx, const int32_t words[3]) { (void)ctx; (void)words; }

static void test_damage_call(void) {
  qa_q1qc_damage_call_layout *layout = qa_q1qc_standard_damage_call_layout(117, 1580);
  CHECK(layout->function_index == 117 && layout->parameter_count == 4);
  CHECK(strcmp(layout->parameters[3], "float") == 0);
  CHECK(layout->roles[QA_Q1QC_ROLE_AMOUNT].items[0].frame_word == 1583);
  uint8_t *globals = calloc(256, 4);
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.globals = (qa_q1qc_words){globals, 256 * 4};
  machine.numeric = numeric;
  qa_q1qc_words_set_int(&machine.globals, 4, 111);
  qa_q1qc_words_set_int(&machine.globals, 7, 222);
  qa_q1qc_words_set_int(&machine.globals, 10, 333);
  qa_q1qc_words_set_float(&machine.globals, 13, 42.5);
  qa_q1qc_damage_call_values values = qa_q1qc_read_damage_call(&machine, layout);
  CHECK(values.self == 111 && values.inflictor == 222 && values.attacker == 333);
  CHECK(values.amount == (double)(float)42.5);
  exec_log log = {0};
  qa_q1qc_function_execution execute = {&log, exec_run, exec_noop, exec_noop};
  qa_q1qc_damage_call_values effective = {444, 222, 333, 10};
  qa_q1qc_project_damage_call(&machine, layout, values, effective, execute);
  CHECK(log.runs == 1);
  CHECK(qa_q1qc_words_int(&machine.globals, 4) == 444);
  CHECK(qa_q1qc_words_float(&machine.globals, 13) == 10);
  qa_q1qc_damage_call_layout_destroy(layout);
  free(globals);
}

/* --- armor points --- */
static void test_armor_points(void) {
  qa_q1qc_program program;
  memset(&program, 0, sizeof program);
  program.source = "test";
  program.api_kind = "q1-netquake";
  program.digest = NQ_DIGEST;
  qa_q1qc_empty_armor pinned = qa_q1qc_empty_armor_for(&program, NULL);
  CHECK(pinned.has && strcmp(pinned.item, "q1:item_armorInv") == 0);
  CHECK(pinned.absorption == (double)(float)0.8);
  qa_regular_armor_state state = qa_q1qc_empty_armor_apply(pinned, 150);
  CHECK(state.kind == QA_REGULAR_ARMOR_Q1 && state.points == 150);
  qa_mod_qc_empty_armor declared = {"q1:item_armor1", 0.3};
  qa_q1qc_empty_armor custom = qa_q1qc_empty_armor_for(&program, &declared);
  CHECK(custom.has && strcmp(custom.item, "q1:item_armor1") == 0);
  qa_mod_qc_empty_armor bad = {"q1:item_shells", 0.5};
  CHECK_ABORTS((void)qa_q1qc_empty_armor_for(&program, &bad));
  qa_mod_qc_empty_armor negative = {"q1:item_armor2", -1};
  CHECK_ABORTS((void)qa_q1qc_empty_armor_for(&program, &negative));
}

/* --- pinned id1 binding + statement access --- */
static void test_id1_program(void) {
  qa_q1qc_program program;
  memset(&program, 0, sizeof program);
  program.source = "test";
  program.api_kind = "q1-quakeworld";
  program.digest = QW_DIGEST;
  const qa_q1_id1_program_binding *binding = qa_q1_id1_program_binding_get(&program, NULL);
  CHECK(strcmp(binding->kind, "quakeworld") == 0 && binding->damage_index == 83);
  CHECK(binding->environment_count == 19 && binding->attacks != NULL);
  CHECK(binding->attacks->axe_damage_count == 2);
  qa_q1qc_statement store = {QA_Q1QC_OP_STORE_V, 40, 50, 0};
  qa_q1qc_statement_access access = qa_q1qc_statement_access_of(store);
  CHECK(access.read_count == 3 && access.write_count == 3 && access.write[0] == 50);
  qa_q1qc_statement call = {QA_Q1QC_OP_CALL2, 99, 0, 0};
  access = qa_q1qc_statement_access_of(call);
  CHECK(access.read_count == 7 && access.read[0] == 99 && access.read[1] == 4);
  CHECK(qa_q1qc_signed_branch(0xfffd) == -3);
}

/* --- synthetic T_Damage program shared by scale/layout tests --- */
typedef struct damage_program {
  qa_q1qc_program program;
  qa_q1qc_statement *statements;
  qa_q1qc_definition *globals;
  qa_q1qc_definition *fields;
  qa_q1qc_function *functions;
  int64_t *sizes;
  uint8_t *initial;
} damage_program;
static void build_damage_program(damage_program *built) {
  memset(built, 0, sizeof *built);
  built->statements = calloc(16, sizeof *built->statements);
  built->initial = calloc(4096, 1);
  put_float(built->initial, 50, 2.0f);
  built->globals = calloc(8, sizeof *built->globals);
  built->globals[0] = (qa_q1qc_definition){"entity", 0, false, 100, "targ"};
  built->globals[1] = (qa_q1qc_definition){"entity", 0, false, 101, "inflictor"};
  built->globals[2] = (qa_q1qc_definition){"entity", 0, false, 102, "attacker"};
  built->globals[3] = (qa_q1qc_definition){"float", 0, false, 103, "dmg"};
  built->globals[4] = (qa_q1qc_definition){"float", 0, false, 50, "scale_const"};
  built->globals[5] = (qa_q1qc_definition){"float", 0, false, 40, "time"};
  built->globals[6] = (qa_q1qc_definition){"entity", 0, false, 28, "self"};
  built->globals[7] = (qa_q1qc_definition){"function", 0, false, 60, "T_Damage"};
  put_int(built->initial, 60, 1);
  built->sizes = calloc(4, sizeof *built->sizes);
  for (int i = 0; i < 4; i++) built->sizes[i] = 1;
  built->functions = calloc(2, sizeof *built->functions);
  built->functions[0].name = "";
  built->functions[0].file = "";
  built->functions[1] = (qa_q1qc_function){1, 1, 100, 10, "T_Damage", "combat.qc",
                                           built->sizes, 4, false};
  built->statements[1] = (qa_q1qc_statement){QA_Q1QC_OP_MUL_F, 103, 50, 103};
  built->statements[2] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_F, 103, 103, 0};
  built->statements[3] = (qa_q1qc_statement){QA_Q1QC_OP_RETURN, 0, 0, 0};
  built->program = (qa_q1qc_program){"test.qc", "q1-netquake", built->statements, 4,
                                     built->globals, 8, NULL, 0, built->functions, 2,
                                     NULL, 0, built->initial, 4096, 0, 0, "sha256:other"};
}
static void free_damage_program(damage_program *built) {
  free(built->statements); free(built->globals); free(built->fields);
  free(built->functions); free(built->sizes); free(built->initial);
}
static qa_mod_source_call damage_call(qa_mod_callback_value *args) {
  args[0] = (qa_mod_callback_value){"input", "self", 0, NULL, {0, 0, 0}};
  args[1] = (qa_mod_callback_value){"input", "inflictor", 0, NULL, {0, 0, 0}};
  args[2] = (qa_mod_callback_value){"input", "attacker", 0, NULL, {0, 0, 0}};
  args[3] = (qa_mod_callback_value){"input", "amount", 0, NULL, {0, 0, 0}};
  qa_mod_source_call call = {"T_Damage", args, 4, NULL, 0};
  return call;
}
static double scale_region_execute(void *ctx, qa_q1qc_inline_region region, int64_t count) {
  (void)region; (void)count;
  qa_q1qc_machine *machine = ctx;
  /* Emulates the qualified MulF: staged amount (word 13) scales into
   * the frame damage word (103), which withQcSourceCall returns. */
  double staged = qa_q1qc_words_float(&machine->globals, 13);
  qa_q1qc_words_set_float(&machine->globals, 103, staged * 2);
  return qa_q1qc_words_float(&machine->globals, 103);
}

static void test_damage_scale(void) {
  damage_program built;
  build_damage_program(&built);
  qa_q1qc_damage_call_layout *layout =
      qa_q1qc_damage_call_layout_create(&built.program, NULL);
  CHECK(layout->roles[QA_Q1QC_ROLE_AMOUNT].items[0].frame_word == 103);
  qa_q1qc_damage_call_layout_destroy(layout);
  qa_mod_callback_value args[4];
  qa_mod_source_call call = damage_call(args);
  qa_qc_statement proof[2] = {{QA_Q1QC_OP_MUL_F, 103, 50, 103}, {QA_Q1QC_OP_STORE_F, 103, 103, 0}};
  qa_mod_qc_damage_scale source = {NULL, "T_Damage", 1, 2, 103, proof, 2};
  qa_q1qc_damage_scale *scale = qa_q1qc_damage_scale_create(&built.program, &call, &source);
  CHECK(scale != NULL && strcmp(scale->kind, "multiplier") == 0);
  CHECK(scale->region.entry == 1 && scale->region.standalone_saved == 103);
  CHECK(scale->constant_count == 1 && scale->constants[0].word == 50);
  float two = 2.0f;
  int32_t two_bits;
  memcpy(&two_bits, &two, 4);
  CHECK(scale->constants[0].bits == two_bits);
  CHECK(qa_q1qc_damage_scale_create(&built.program, &call, NULL) == NULL);
  qa_mod_qc_damage_scale bad = source;
  bad.damage = 999;
  CHECK_ABORTS((void)qa_q1qc_damage_scale_create(&built.program, &call, &bad));
  uint8_t *globals = calloc(512, 4);
  put_float(globals, 50, 2.0f);
  qa_q1qc_entity_memory entities = {1024, 0, 256, 16, 16, calloc(16, 1024)};
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.ctx = &machine;
  machine.program = &built.program;
  machine.globals = (qa_q1qc_words){globals, 512 * 4};
  machine.entities = &entities;
  machine.numeric = numeric;
  machine.execute_region = scale_region_execute;
  qa_actor_id owner = make_actor(3, 1);
  double multiplier = qa_q1qc_evaluate_damage_scale(&machine, scale, owner, 4096, 0);
  CHECK(multiplier == 2);
  CHECK(qa_q1qc_evaluate_damage_amount(&machine, scale, owner, 4096, 0, 3) == 6);
  /* Scratch words are restored after evaluation. */
  CHECK(qa_q1qc_words_float(&machine.globals, 103) == 0);
  qa_q1qc_words_set_float(&machine.globals, 50, 3);
  CHECK_ABORTS((void)qa_q1qc_evaluate_damage_scale(&machine, scale, owner, 4096, 0));
  qa_q1qc_damage_scale_destroy(scale);
  free(entities.bytes);
  free(globals);
  free_damage_program(&built);
}

/* --- declared pickup stages over a small synthetic program --- */
static qa_qc_pickup_caller_declaration pickup_declaration(qa_qc_statement *proof,
                                                          qa_pickup_resource *resource,
                                                          qa_qc_pickup_scalar *count,
                                                          qa_qc_pickup_region *region) {
  *resource = (qa_pickup_resource){QA_PICKUP_RESOURCE_INVENTORY, QA_PROTECTION_REGULAR,
                                   "q1:ammo/shells"};
  *count = (qa_qc_pickup_scalar){true, NULL, 60};
  proof[0] = (qa_qc_statement){QA_Q1QC_OP_MUL_F, 50, 51, 52};
  proof[1] = (qa_qc_statement){QA_Q1QC_OP_STORE_F, 52, 60, 0};
  proof[2] = (qa_qc_statement){QA_Q1QC_OP_IF_NOT, 60, 2, 0};
  *region = (qa_qc_pickup_region){2, 4, proof, 3, "decision", 60, 1};
  qa_qc_pickup_caller_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.function = "touch_it";
  declaration.descriptor.kind = "constant";
  declaration.descriptor.constant_item =
      (qa_qc_pickup_item){"q1:ammo/shells", resource, count};
  declaration.regions = region;
  declaration.region_count = 1;
  return declaration;
}
static void build_pickup_program(qa_q1qc_program *program, qa_q1qc_statement *statements,
                                 qa_q1qc_function *functions, uint8_t *initial) {
  memset(statements, 0, 16 * sizeof *statements);
  statements[2] = (qa_q1qc_statement){QA_Q1QC_OP_MUL_F, 50, 51, 52};
  statements[3] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_F, 52, 60, 0};
  statements[4] = (qa_q1qc_statement){QA_Q1QC_OP_IF_NOT, 60, 2, 0};
  memset(functions, 0, 2 * sizeof *functions);
  functions[0].name = "";
  functions[0].file = "";
  functions[1] = (qa_q1qc_function){1, 1, 100, 0, "touch_it", "items.qc", NULL, 0, false};
  *program = (qa_q1qc_program){"test.qc", "q1-netquake", statements, 8, NULL, 0, NULL, 0,
                               functions, 2, NULL, 0, initial, 1024, 0, 0, "sha256:other"};
}
static void test_declared_pickups(void) {
  qa_q1qc_statement *statements = calloc(16, sizeof *statements);
  qa_q1qc_function *functions = calloc(2, sizeof *functions);
  uint8_t *initial = calloc(1024, 1);
  qa_q1qc_program program;
  build_pickup_program(&program, statements, functions, initial);
  qa_qc_statement proof[3];
  qa_pickup_resource resource;
  qa_qc_pickup_scalar count;
  qa_qc_pickup_region region;
  qa_qc_pickup_caller_declaration declaration =
      pickup_declaration(proof, &resource, &count, &region);
  size_t stage_count = 0;
  qa_q1qc_pickup_stage *stages =
      qa_q1qc_declared_pickup_stages(&program, &declaration, 1, &stage_count);
  CHECK(stage_count == 1 && stages[0].function_index == 1);
  CHECK(stages[0].value_count == 1 && stages[0].values[0].has_count);
  CHECK(stages[0].region_count == 1 &&
        strcmp(stages[0].regions[0].operation.kind, "decision") == 0 &&
        stages[0].regions[0].operation.word == 60 &&
        stages[0].regions[0].operation.accepted == 1);
  qa_q1qc_pickup_stages_destroy(stages, stage_count);
  qa_qc_pickup_caller_declaration dup[2] = {declaration, declaration};
  CHECK_ABORTS((void)qa_q1qc_declared_pickup_stages(&program, dup, 2, &stage_count));
  /* Pinned stages: unknown digest yields none; pinned digest with a
   * truncated program aborts on the first missing caller. */
  size_t count2 = 99;
  CHECK(qa_q1qc_pickup_stages(&program, &count2) == NULL && count2 == 0);
  program.digest = NQ_DIGEST;
  CHECK_ABORTS((void)qa_q1qc_pickup_stages(&program, &count2));
  free(statements); free(functions); free(initial);
}

/* --- declared armor stage over a small synthetic program --- */
static void test_armor_stage(void) {
  qa_q1qc_statement *statements = calloc(8, sizeof *statements);
  statements[2] = (qa_q1qc_statement){QA_Q1QC_OP_LOAD_F, 100, 300, 104};
  statements[3] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_F, 104, 104, 0};
  statements[4] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_F, 104, 104, 0};
  statements[5] = (qa_q1qc_statement){QA_Q1QC_OP_SUB_F, 103, 108, 109};
  statements[6] = (qa_q1qc_statement){QA_Q1QC_OP_RETURN, 0, 0, 0};
  qa_q1qc_definition globals[4] = {{"entity", 0, false, 100, "targ"},
                                   {"float", 0, false, 103, "dmg"},
                                   {"float", 0, false, 108, "save"},
                                   {"field", 0, false, 300, "armortype"}};
  qa_q1qc_function functions[2];
  memset(functions, 0, sizeof functions);
  functions[0].name = "";
  functions[1] = (qa_q1qc_function){1, 1, 100, 10, "T_Damage", "combat.qc", NULL, 0, false};
  uint8_t *initial = calloc(4096, 1);
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 8, globals, 4, NULL, 0,
                             functions, 2, NULL, 0, initial, 4096, 0, 0, "sha256:other"};
  qa_qc_statement proof[4] = {{QA_Q1QC_OP_LOAD_F, 100, 300, 104},
                              {QA_Q1QC_OP_STORE_F, 104, 104, 0},
                              {QA_Q1QC_OP_STORE_F, 104, 104, 0},
                              {QA_Q1QC_OP_SUB_F, 103, 108, 109}};
  qa_mod_qc_armor_stage declared;
  memset(&declared, 0, sizeof declared);
  declared.function = "T_Damage";
  declared.entry = 2; declared.exit = 5;
  declared.target = 100; declared.damage = 103; declared.saved = 108;
  declared.statements = proof;
  declared.statement_count = 4;
  qa_q1qc_armor_stage *stage = qa_q1qc_armor_stage_create(&program, &declared);
  CHECK(stage != NULL);
  CHECK(stage->region.function_index == 1 && stage->region.entry == 2 && stage->region.exit == 5);
  CHECK(stage->region.replaceable);
  CHECK(!stage->region.has_standalone);
  qa_q1qc_armor_stage_destroy(stage);
  CHECK(qa_q1qc_armor_stage_create(&program, NULL) == NULL);
  qa_mod_qc_armor_stage bad = declared;
  bad.saved = 103;
  CHECK_ABORTS((void)qa_q1qc_armor_stage_create(&program, &bad));
  free(statements); free(initial);
}

/* --- weapon stage: derived + declared --- */
static void build_weapon_program(qa_q1qc_program *program, qa_q1qc_statement *statements,
                                 qa_q1qc_function *functions, uint8_t *initial) {
  memset(statements, 0, 8 * sizeof *statements);
  memset(functions, 0, 4 * sizeof *functions);
  for (int i = 0; i < 4; i++) { functions[i].name = ""; functions[i].file = ""; }
  functions[1] = (qa_q1qc_function){1, 1, 100, 0, "W_WeaponFrame", "weapons.qc", NULL, 0, false};
  functions[2] = (qa_q1qc_function){2, 2, 110, 0, "player_shot1", "player.qc", NULL, 0, false};
  *program = (qa_q1qc_program){"test.qc", "q1-netquake", statements, 3, NULL, 0, NULL, 0,
                               functions, 3, NULL, 0, initial, 1024, 0, 0, "sha256:other"};
}
static void test_weapon_stage(void) {
  qa_q1qc_statement *statements = calloc(8, sizeof *statements);
  qa_q1qc_function *functions = calloc(4, sizeof *functions);
  uint8_t *initial = calloc(1024, 1);
  qa_q1qc_program program;
  build_weapon_program(&program, statements, functions, initial);
  qa_q1qc_weapon_stage *derived = qa_q1qc_derived_weapon_stage(&program);
  CHECK(derived->dispatcher == 1);
  CHECK(derived->continuation_count == 1 && derived->continuations[0] == 2);
  CHECK(!derived->has_client && derived->repeat_count == 0);
  qa_q1qc_weapon_stage_destroy(derived);
  const char *continuations[1] = {"player_shot1"};
  qa_qc_weapon_stage_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.dispatcher = "W_WeaponFrame";
  declaration.continuations = (qa_string_view_list){continuations, 1};
  qa_q1qc_weapon_stage *declared = qa_q1qc_declared_weapon_stage(&program, &declaration);
  CHECK(declared->dispatcher == 1 && declared->continuation_count == 1);
  qa_q1qc_weapon_stage_destroy(declared);
  const char *self_continuation[1] = {"W_WeaponFrame"};
  declaration.continuations = (qa_string_view_list){self_continuation, 1};
  CHECK_ABORTS((void)qa_q1qc_declared_weapon_stage(&program, &declaration));
  free(statements); free(functions); free(initial);
}

/* --- SaveReader declaration ports --- */
static qa_save_value *obj_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, child)) qa_save_value_free(child);
  return object;
}
static void test_readers(void) {
  qa_save_value *root = qa_save_value_object();
  obj_set(root, "dispatcher", qa_save_value_string("W_WeaponFrame"));
  qa_save_value *continuations = qa_save_value_array();
  (void)qa_save_value_push(continuations, qa_save_value_string("player_shot1"));
  obj_set(root, "continuations", continuations);
  obj_set(root, "repeats", qa_save_value_array());
  qa_save_value *client = qa_save_value_object();
  obj_set(client, "spawn", qa_save_value_string("PutClientInServer"));
  obj_set(client, "selectSpawn", qa_save_value_string("SelectSpawnPoint"));
  qa_save_value *objectives = qa_save_value_object();
  obj_set(objectives, "kind", qa_save_value_string("none"));
  obj_set(client, "objectives", objectives);
  obj_set(root, "client", client);
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, root, "test"));
  qa_qc_primary_weapon_stage_declaration primary = qa_q1qc_read_primary_weapon_stage(&reader);
  CHECK(strcmp(primary.stage.dispatcher, "W_WeaponFrame") == 0);
  CHECK(primary.stage.continuations.count == 1 &&
        strcmp(primary.stage.continuations.values[0], "player_shot1") == 0);
  CHECK(!primary.client_spawn.is_call &&
        strcmp(primary.client_spawn.function, "PutClientInServer") == 0);
  CHECK(strcmp(primary.client_objectives.kind, "none") == 0);
  qa_q1qc_primary_weapon_stage_destroy(&primary);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(root);

  qa_save_value *caller = qa_save_value_object();
  obj_set(caller, "function", qa_save_value_string("touch_it"));
  qa_save_value *descriptor = qa_save_value_object();
  obj_set(descriptor, "kind", qa_save_value_string("constant"));
  obj_set(descriptor, "item", qa_save_value_string("q1:ammo/shells"));
  qa_save_value *resource = qa_save_value_object();
  obj_set(resource, "kind", qa_save_value_string("inventory"));
  obj_set(resource, "item", qa_save_value_string("q1:ammo/shells"));
  obj_set(descriptor, "resource", resource);
  qa_save_value *count = qa_save_value_object();
  obj_set(count, "kind", qa_save_value_string("global"));
  obj_set(count, "word", qa_save_value_number(60));
  obj_set(descriptor, "count", count);
  obj_set(caller, "descriptor", descriptor);
  qa_save_value *regions = qa_save_value_array();
  qa_save_value *region = qa_save_value_object();
  obj_set(region, "entry", qa_save_value_number(2));
  obj_set(region, "exit", qa_save_value_number(4));
  qa_save_value *words = qa_save_value_array();
  const double proof[3][4] = {{QA_Q1QC_OP_MUL_F, 50, 51, 52},
                              {QA_Q1QC_OP_STORE_F, 52, 60, 0},
                              {QA_Q1QC_OP_IF_NOT, 60, 2, 0}};
  const char *keys[4] = {"opcode", "a", "b", "c"};
  for (int i = 0; i < 3; i++) {
    qa_save_value *word = qa_save_value_object();
    for (int k = 0; k < 4; k++) obj_set(word, keys[k], qa_save_value_number(proof[i][k]));
    (void)qa_save_value_push(words, word);
  }
  obj_set(region, "statements", words);
  qa_save_value *operation = qa_save_value_object();
  obj_set(operation, "kind", qa_save_value_string("decision"));
  obj_set(operation, "word", qa_save_value_number(60));
  obj_set(operation, "accepted", qa_save_value_number(1));
  obj_set(region, "operation", operation);
  (void)qa_save_value_push(regions, region);
  obj_set(caller, "regions", regions);
  CHECK(qa_save_reader_init(&reader, caller, "test"));
  qa_qc_pickup_caller_declaration declaration = qa_q1qc_read_pickup_caller(&reader);
  CHECK(strcmp(declaration.function, "touch_it") == 0);
  CHECK(declaration.descriptor.constant_item.resource != NULL &&
        declaration.descriptor.constant_item.count != NULL &&
        declaration.descriptor.constant_item.count->word == 60);
  CHECK(declaration.region_count == 1 && declaration.regions[0].statement_count == 3);
  qa_q1qc_statement *statements = calloc(16, sizeof *statements);
  qa_q1qc_function *functions = calloc(2, sizeof *functions);
  uint8_t *initial = calloc(1024, 1);
  qa_q1qc_program program;
  build_pickup_program(&program, statements, functions, initial);
  size_t stage_count = 0;
  qa_q1qc_pickup_stage *stages =
      qa_q1qc_declared_pickup_stages(&program, &declaration, 1, &stage_count);
  CHECK(stage_count == 1 && stages[0].values[0].has_count);
  qa_q1qc_pickup_stages_destroy(stages, stage_count);
  qa_q1qc_pickup_caller_declaration_destroy(&declaration);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(caller);
  free(statements); free(functions); free(initial);
}

/* --- attacks / projectiles / environment over pinned digests --- */
static qa_q1qc_machine *machine_of(void *ctx) { return ctx; }
static void name_functions(qa_q1qc_function *functions, size_t count) {
  for (size_t i = 0; i < count; i++) {
    functions[i].name = "";
    functions[i].file = "";
    functions[i].index = (int64_t)i;
  }
}
static void test_attacks_projectiles(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "quakec"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  qa_q1qc_function *functions = calloc(16, sizeof *functions);
  name_functions(functions, 16);
  const char *names[8] = {"W_FireRocket", "W_FireGrenade", "W_FireSpikes", "W_FireSuperSpikes",
                          "W_FireLightning", "T_Damage", "T_RadiusDamage", "launch_spike"};
  for (int i = 0; i < 8; i++) functions[i + 1].name = names[i];
  qa_q1qc_definition fields[1] = {{"entity", 0, false, 5, "owner"}};
  uint8_t *initial = calloc(1024, 1);
  qa_q1qc_program program = {"test.qc", "q1-netquake", NULL, 0, NULL, 0, fields, 1,
                             functions, 16, NULL, 0, initial, 1024, 0, 0, NQ_DIGEST};
  qa_q1qc_entity_memory entities = {1024, 0, 256, 16, 16, calloc(16, 1024)};
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.program = &program;
  machine.entities = &entities;
  machine.numeric = numeric;
  /* slots stay NULL: the exercised paths return before consulting them. */
  qa_q1qc_source source = {&program, &entities, actors, NULL, "q1:official", NULL, NULL};
  qa_q1_id1_sync_attacks *attacks = qa_q1_id1_sync_attacks_create(source, machine_of, &machine);
  qa_q1_id1_sync_attacks_assert_idle(attacks);
  qa_q1_id1_damage_call call;
  memset(&call, 0, sizeof call);
  call.call = (qa_q1qc_call_site){117, 999, 1};
  qa_q1_id1_synchronous_attack attack;
  CHECK(!qa_q1_id1_sync_attacks_resolve(attacks, call, &attack));
  qa_q1_id1_sync_attacks_destroy(attacks);
  qa_q1_id1_projectile_attacks *projectiles =
      qa_q1_id1_projectile_attacks_create(source, machine_of, &machine);
  size_t captured = 99;
  qa_q1_id1_projectile_capture *entries =
      qa_q1_id1_projectile_attacks_capture(projectiles, &captured);
  CHECK(captured == 0);
  free(entries);
  qa_q1_id1_projectile_attack projectile;
  call.call = (qa_q1qc_call_site){999, 1, 1};
  CHECK(!qa_q1_id1_projectile_attacks_resolve(projectiles, call, &projectile));
  qa_q1_id1_projectile_attacks_destroy(projectiles);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
  free(entities.bytes); free(functions); free(initial);
}
static void test_environment(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "quakec-env"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  qa_q1qc_statement *statements = calloc(11000, sizeof *statements);
  const int64_t sites[9] = {6446, 6489, 6509, 6935, 10462, 8690, 9589, 10736, 10877};
  for (int i = 0; i < 9; i++)
    statements[sites[i]] = (qa_q1qc_statement){QA_Q1QC_OP_CALL4, 520, 0, 0};
  qa_q1qc_function *functions = calloc(500, sizeof *functions);
  name_functions(functions, 500);
  functions[239].name = "WaterMove";
  functions[243].name = "PlayerPostThink";
  functions[434].name = "hurt_touch";
  functions[375].name = "door_blocked";
  functions[397].name = "secret_blocked";
  functions[448].name = "plat_crush";
  functions[451].name = "train_blocked";
  uint8_t *initial = calloc(1024, 1);
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 11000, NULL, 0, NULL, 0,
                             functions, 500, NULL, 0, initial, 1024, 0, 0, NQ_DIGEST};
  qa_q1qc_entity_memory entities = {1024, 0, 256, 16, 16, calloc(16, 1024)};
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.program = &program;
  machine.entities = &entities;
  machine.numeric = numeric;
  qa_q1qc_source source = {&program, &entities, actors, NULL, "q1:official", NULL, NULL};
  qa_q1_id1_environment *environment = qa_q1_id1_environment_create(source, machine_of, &machine);
  qa_q1_id1_damage_call call;
  memset(&call, 0, sizeof call);
  call.call = (qa_q1qc_call_site){117, 999, 1};
  qa_q1_id1_environmental_damage damage;
  CHECK(!qa_q1_id1_environment_resolve(environment, call, NULL, &damage));
  qa_q1_id1_environment_destroy(environment);
  statements[6446].a = 1;
  CHECK_ABORTS((void)qa_q1_id1_environment_create(source, machine_of, &machine));
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
  free(entities.bytes); free(statements); free(functions); free(initial);
}

/* --- id1 damage binding over the pinned NQ layout --- */
static void hook_noop_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse,
                              qa_provider_id movement) {
  (void)ctx; (void)actor; (void)impulse; (void)movement;
}
static void hook_noop_before(void *ctx, qa_owned_actor actor, qa_damage_decision decision) {
  (void)ctx; (void)actor; (void)decision;
}
static void hook_noop_confirmed(void *ctx, qa_damage_outcome outcome) {
  (void)ctx; (void)outcome;
}
static qa_damage_request resolve_zero(void *ctx, qa_q1_id1_damage_call call) {
  (void)ctx; (void)call;
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  return request;
}
static double intercept_zero(void *ctx, qa_armor_stage_input input,
                             qa_armor_stage_original_fn original, void *original_ctx) {
  (void)ctx; (void)input; (void)original; (void)original_ctx;
  return 0;
}
static void test_id1_damage_binding(void) {
  qa_q1qc_statement *statements = calloc(2000, sizeof *statements);
  for (int i = 1431; i < 1455; i++)
    statements[i] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_F, 1584, 1584, 0};
  statements[1431] = (qa_q1qc_statement){QA_Q1QC_OP_LOAD_F, 1580, 400, 1584};
  statements[1455] = (qa_q1qc_statement){QA_Q1QC_OP_SUB_F, 1583, 1588, 1589};
  statements[1421] = (qa_q1qc_statement){QA_Q1QC_OP_LOAD_F, 1580, 163, 1590};
  statements[1426] = (qa_q1qc_statement){QA_Q1QC_OP_LOAD_F, 1582, 377, 1592};
  statements[1427] = (qa_q1qc_statement){QA_Q1QC_OP_GT, 1592, 31, 1593};
  statements[1428] = (qa_q1qc_statement){QA_Q1QC_OP_IF_NOT, 1593, 3, 0};
  statements[1442] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_PF, 213, 1600, 0};
  statements[1450] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_PF, 1607, 1601, 0};
  statements[1454] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_PF, 1610, 1608, 0};
  statements[1492] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_PV, 1655, 1643, 0};
  statements[1526] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_PF, 1677, 1675, 0};
  statements[1532] = (qa_q1qc_statement){QA_Q1QC_OP_CALL2, 1559, 0, 0};
  statements[1568] = (qa_q1qc_statement){QA_Q1QC_OP_CALL2, 1701, 0, 0};
  qa_q1qc_definition globals[4] = {{"entity", 0, false, 1580, "targ"},
                                   {"float", 0, false, 1583, "dmg"},
                                   {"float", 0, false, 1588, "save"},
                                   {"field", 0, false, 400, "armortype"}};
  qa_q1qc_definition fields[7] = {{"float", 0, false, 10, "health"},
                                  {"vector", 0, false, 11, "velocity"},
                                  {"float", 0, false, 14, "armorvalue"},
                                  {"float", 0, false, 15, "armortype"},
                                  {"float", 0, false, 16, "items"},
                                  {"function", 0, false, 17, "th_pain"},
                                  {"function", 0, false, 18, "th_die"}};
  int64_t *sizes = calloc(4, sizeof *sizes);
  for (int i = 0; i < 4; i++) sizes[i] = 1;
  qa_q1qc_function *functions = calloc(200, sizeof *functions);
  name_functions(functions, 200);
  functions[117] = (qa_q1qc_function){117, 1421, 1580, 10, "T_Damage", "combat.qc", sizes, 4, false};
  uint8_t *initial = calloc(8000, 1);
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 2000, globals, 4, fields, 7,
                             functions, 200, NULL, 0, initial, 8000, 0, 0, NQ_DIGEST};
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "quakec-damage"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.impulse = hook_noop_impulse;
  hooks.before_reaction = hook_noop_before;
  hooks.confirmed = hook_noop_confirmed;
  qa_gameplay_authority *authority = qa_gameplay_authority_create(actors, callbacks, hooks);
  qa_q1qc_entity_memory entities = {1024, 0, 256, 16, 16, calloc(16, 1024)};
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.program = &program;
  machine.entities = &entities;
  machine.numeric = numeric;
  qa_q1qc_source source = {&program, &entities, actors, NULL, "q1:official", NULL, NULL};
  qa_q1_id1_damage_binding *binding = qa_q1_id1_damage_binding_create(
      source, authority, machine_of, &machine, resolve_zero, NULL, NULL, NULL, NULL, NULL);
  CHECK(binding != NULL);
  const qa_q1qc_function_boundary *boundary = qa_q1_id1_damage_function_boundary(binding);
  CHECK(boundary->function_count == 1 && boundary->functions[0] == 117);
  const qa_q1qc_inline_boundary *inline_boundary = qa_q1_id1_damage_inline_boundary(binding);
  CHECK(inline_boundary->region_count == 2);
  CHECK(inline_boundary->regions[0].entry == 1426 && inline_boundary->regions[1].entry == 1431);
  double out = 0;
  CHECK(!qa_q1_id1_damage_multiplier_of(binding, make_actor(1, 1), 1024, 0, &out));
  uint8_t *bytes = calloc(256, 4);
  qa_q1qc_words words = {bytes, 256 * 4};
  qa_q1qc_words_set_float(&words, 16, 16384);
  qa_q1qc_words_set_float(&words, 14, 80);
  qa_q1qc_words_set_float(&words, 15, 0.6);
  qa_armor_state armor = qa_q1_id1_damage_read_armor(binding, &words);
  CHECK(armor.regular.kind == QA_REGULAR_ARMOR_Q1);
  CHECK(strcmp(armor.regular.item, "q1:item_armor2") == 0 && armor.regular.points == 80);
  qa_q1qc_words_set_float(&words, 16, 0);
  armor = qa_q1_id1_damage_read_armor(binding, &words);
  CHECK(armor.regular.kind == QA_REGULAR_ARMOR_NONE);
  qa_owned_actor player = qa_session_actor_registry_allocate(actors, "q1:official", "player");
  qa_source_armor_stage stage;
  CHECK(qa_q1_id1_damage_protection_stage(binding, player, QA_PROTECTION_REGULAR, &stage));
  qa_armor_stage_removal removal = stage.bind(stage.ctx, intercept_zero, NULL);
  CHECK_ABORTS((void)stage.bind(stage.ctx, intercept_zero, NULL));
  removal.remove(removal.ctx);
  qa_q1_id1_damage_binding_destroy(binding);
  qa_gameplay_authority_destroy(authority);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
  free(entities.bytes); free(bytes); free(statements); free(functions); free(sizes); free(initial);
}

/* --- foundation entity-services getters --- */
static qa_actor_id host_players[1];
static const qa_actor_id *host_players_fn(void *ctx, size_t *out_count) {
  (void)ctx;
  *out_count = 1;
  return host_players;
}
static bool host_source_target(void *ctx, qa_actor_id actor, qa_q1_source_target *out) {
  (void)ctx; (void)actor;
  *out = (qa_q1_source_target){true, false, false, false};
  return true;
}
static void test_entity_services(void) {
  qa_q1_services game;
  memset(&game, 0, sizeof game);
  qa_owned_actor owned1;
  memset(&owned1, 0, sizeof owned1);
  owned1.id = make_actor(1, 1);
  owned1.owner = "q1:official";
  qa_owned_actor owned2 = owned1;
  owned2.id = make_actor(2, 1);
  qa_q1_actor first, second;
  /* Built by hand: qa_q1_actor_init's text.c dependency has not
   * landed with the foundation slice yet. */
  memset(&first, 0, sizeof first);
  memset(&second, 0, sizeof second);
  first.actor = owned1;
  first.classname = "player";
  first.solid = "none";
  first.movement = "none";
  first.live = true;
  second.actor = owned2;
  second.classname = "func_door";
  second.live = true;
  second.movement = "push";
  second.solid = "slidebox";
  second.aimed_damage = true;
  second.body.origin = (qa_vec3){1, 2, 3};
  qa_q1_actor *entities[2] = {&first, &second};
  game.entities = entities;
  game.entity_count = 2;
  qa_q1_player_state player;
  memset(&player, 0, sizeof player);
  player.actor = owned1;
  qa_q1_player_state *players[1] = {&player};
  game.players = players;
  game.player_count = 1;
  host_players[0] = make_actor(9, 1);
  game.host.players = host_players_fn;
  qa_actor_id id1 = make_actor(1, 1), id2 = make_actor(2, 1), id9 = make_actor(9, 1);
  qa_actor_id unknown = make_actor(7, 1);
  CHECK(qa_q1_get_entity(&game, NULL) == NULL);
  CHECK(qa_q1_get_entity(&game, &id2) == &second);
  CHECK(qa_q1_get_entity(&game, &unknown) == NULL);
  qa_q1_body body = qa_q1_get_body(&game, &second);
  CHECK(body.origin.x == 1 && body.origin.y == 2 && body.origin.z == 3);
  CHECK_ABORTS({ second.live = false; (void)qa_q1_get_body(&game, &second); });
  second.live = false;
  CHECK(qa_q1_get_entity(&game, &id2) == NULL);
  second.live = true;
  qa_q1_source_target target = qa_q1_get_source_target(&game, id2);
  CHECK(target.aimed_damage && target.push && target.slidebox && !target.player);
  CHECK(qa_q1_get_source_target(&game, id1).player);
  CHECK(qa_q1_get_source_target(&game, id9).player);
  game.host.source_target = host_source_target;
  target = qa_q1_get_source_target(&game, id2);
  CHECK(target.aimed_damage && !target.push && !target.player);
}

/* --- foundation entity-services: attach/select/punch family --- */
static int emitted_events = 0;
static char last_transition_map[64] = "";
static void capture_emit(void *ctx, qa_q1_event event) {
  (void)ctx;
  (void)event;
  emitted_events++;
}
static void capture_transition(void *ctx, const char *map,
                               const qa_actor_id *cause) {
  (void)ctx;
  (void)cause;
  snprintf(last_transition_map, sizeof last_transition_map, "%s", map);
}
static int think_calls = 0;
static void think_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  (void)game;
  (void)entity;
  think_calls++;
}
static bool near(double a, double b) { return fabs(a - b) < 1e-6; }
static void test_entity_services_family(void) {
  CHECK(qa_q1_ammo_item("axe") == NULL);
  CHECK(strcmp(qa_q1_ammo_item("supershotgun"), "q1:ammo/shells") == 0);
  CHECK(strcmp(qa_q1_ammo_item("rogue:plasma"), "rogue:ammo/plasma") == 0);
  CHECK(qa_q1_ammo_item("ctf:grapple") == NULL);

  qa_vec3 punch = qa_q1_drop_punch((qa_vec3){3, 4, 0}, 0.1, &numeric);
  CHECK(near(punch.x, 2.4) && near(punch.y, 3.2) && punch.z == 0);
  punch = qa_q1_drop_punch((qa_vec3){3, 4, 0}, 10, &numeric);
  CHECK(punch.x == 0 && punch.y == 0 && punch.z == 0);
  punch = qa_q1_drop_punch((qa_vec3){0, 0, 0}, 0.1, &numeric);
  CHECK(punch.x == 0 && punch.y == 0 && punch.z == 0);

  qa_q1_services game;
  memset(&game, 0, sizeof game);
  game.host.emit = capture_emit;
  game.host.transition = capture_transition;
  qa_q1_services_set_time(&game, 1.5);
  CHECK(game.time == 1.5);
  CHECK(strcmp(qa_q1_services_provider(&game), "q1:official") == 0);

  qa_owned_actor owned;
  memset(&owned, 0, sizeof owned);
  owned.id = make_actor(1, 1);
  owned.owner = "q1:official";
  qa_q1_actor *entity = qa_q1_attach_existing(&game, owned, "player", NULL, NULL);
  CHECK(entity != NULL && game.entity_count == 1);
  CHECK_ABORTS((void)qa_q1_attach_existing(&game, owned, "player", NULL, NULL));
  entity->health = 80;
  CHECK(qa_q1_live(&game, entity));
  CHECK(qa_q1_health(&game, owned.id) == 80);
  qa_q1_set_health(&game, owned, 55);
  CHECK(entity->health == 55);
  qa_q1_set_origin(&game, entity, (qa_vec3){10, 20, 30});
  CHECK(entity->body.origin.x == 10 && entity->body.origin.z == 30);
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.velocity = (qa_vec3){1, 0, 0};
  qa_q1_set_body_patch(&game, entity, &patch, false, false, true, false, false);
  CHECK(entity->body.velocity.x == 1 && entity->body.origin.x == 10);

  qa_q1_player_state *player =
      qa_q1_attach_player(&game, owned, "shotgun", true, NULL);
  CHECK(player != NULL && player->max_health == 100);
  CHECK(player->air_finished == game.time + 12);
  CHECK(qa_q1_attach_player(&game, owned, "axe", false, NULL) == player);
  /* Fallback inventory: TS initializeWeaponInventory counts. */
  CHECK(qa_q1_inventory_count(&game, owned.id, "q1:ammo/shells") == 25);
  CHECK(qa_q1_inventory_count(&game, owned.id, "q1:weapon/shotgun") == 1);
  CHECK(qa_q1_inventory_count(&game, owned.id, "q1:weapon/nailgun") == 0);
  CHECK(qa_q1_inventory_give(&game, owned, "q1:ammo/shells", 1000) == 75);
  CHECK(qa_q1_inventory_count(&game, owned.id, "q1:ammo/shells") == 100);
  CHECK(qa_q1_inventory_consume(&game, owned, "q1:ammo/shells", 30));
  CHECK(!qa_q1_inventory_consume(&game, owned, "q1:ammo/shells", 1000));

  /* Selection: no nailgun until its item is owned. */
  CHECK(!qa_q1_weapon_available(&game, player, "nailgun", "best"));
  CHECK(!qa_q1_select_weapon(&game, owned, "nailgun"));
  qa_q1_inventory_give(&game, owned, "q1:weapon/nailgun", 1);
  qa_q1_inventory_give(&game, owned, "q1:ammo/nails", 10);
  CHECK(qa_q1_weapon_available(&game, player, "nailgun", "best"));
  CHECK(qa_q1_select_weapon(&game, owned, "nailgun"));
  CHECK(strcmp(player->weapon, "nailgun") == 0);
  CHECK(strcmp(qa_q1_services_weapon_model(&game, "nailgun", player),
               "progs/v_nail.mdl") == 0);
  CHECK(strcmp(qa_q1_choose_best(&game, owned), "nailgun") == 0);
  /* Registered replacement for a base weapon resolves model/ammo. */
  static const qa_q1_weapon_definition replacement = {
      .id = "nailgun", .ammo = "q1:ammo/cells", .model = "progs/v_custom.mdl"};
  qa_q1_replace_weapon(&game, &replacement);
  CHECK(strcmp(qa_q1_services_weapon_model(&game, "nailgun", NULL),
               "progs/v_custom.mdl") == 0);
  CHECK(strcmp(qa_q1_services_weapon_ammo(&game, "nailgun"), "q1:ammo/cells") == 0);
  static const qa_q1_weapon_definition foreign = {.id = "hipnotic:laser"};
  qa_q1_register_weapon(&game, &foreign);
  CHECK_ABORTS(qa_q1_register_weapon(&game, &foreign));
  static const qa_q1_weapon_definition base_dup = {.id = "axe"};
  CHECK_ABORTS(qa_q1_register_weapon(&game, &base_dup));

  /* Scheduling: fround'ed deadline, one-shot dispatch. */
  qa_q1_bound_action action = {"test_think", NULL, think_action};
  qa_q1_schedule_at(&game, entity, 2.25, action);
  CHECK(entity->next_think == (float)2.25);
  qa_q1_run_think(&game, entity);
  CHECK(think_calls == 1 && entity->next_think == -1);
  qa_q1_run_think(&game, entity);
  CHECK(think_calls == 1);
  qa_q1_schedule(&game, entity, 0.5, action);
  qa_q1_cancel(&game, entity);
  CHECK(entity->next_think == -1 && entity->think.fn == NULL);

  /* Powerups, punch, monster target, sound, find, presentations. */
  qa_q1_give_powerup(&game, player, "quad", 30);
  CHECK(qa_q1_powerup_expires(&game, owned.id, "quad") == (float)(game.time + 30));
  qa_q1_weapon_punch(&game, player, -2);
  CHECK(player->punch_angles.x == -2);
  qa_vec3 advanced;
  CHECK(qa_q1_advance_punch(&game, owned.id, 0.05, &numeric, &advanced));
  CHECK(near(advanced.x, -1.5));
  CHECK(!qa_q1_advance_punch(&game, make_actor(99, 1), 0.05, &numeric, &advanced));
  qa_q1_monster_target_result seen = qa_q1_monster_target(&game, owned.id);
  CHECK(seen.has_value && seen.value.view_height == 22 && !seen.value.invisible);
  int before = emitted_events;
  qa_q1_sound_defaults(&game, owned.id, "weapons/guncock.wav");
  qa_q1_message(&game, &owned.id, "hello", true, NULL, 0);
  qa_q1_effect(&game, "explosion", entity->body.origin, NULL, 1);
  CHECK(emitted_events == before + 3);
  CHECK_ABORTS(qa_q1_sound_defaults(&game, make_actor(99, 1), "x.wav"));
  entity->targetname = "door1";
  qa_q1_actor *found[2] = {NULL, NULL};
  CHECK(qa_q1_find(&game, "door1", found, 2) == 1 && found[0] == entity);
  CHECK(qa_q1_find(&game, "", found, 2) == 0);
  qa_q1_presentation presentations[2];
  CHECK(qa_q1_presentations(&game, presentations, 2) == 1);
  CHECK(strcmp(presentations[0].classname, "player") == 0);

  /* Intermission against an info_player_start spot. */
  qa_owned_actor spot_owned = owned;
  spot_owned.id = make_actor(2, 1);
  qa_q1_actor *spot =
      qa_q1_attach_existing(&game, spot_owned, "info_player_start", NULL, NULL);
  CHECK(spot != NULL);
  qa_q1_begin_intermission(&game, "e1m2", NULL);
  CHECK(game.has_intermission);
  CHECK(!qa_q1_request_intermission_exit(&game, game.time, true));
  CHECK(qa_q1_request_intermission_exit(
      &game, game.intermission_exit_after, true));
  CHECK(strcmp(last_transition_map, "q1:e1m2") == 0);
  CHECK(!game.has_intermission);

  /* Removal: released actors leave the maps (TS onRelease). */
  qa_q1_remove(&game, spot);
  CHECK(game.entity_count == 1);
  qa_q1_remove(&game, entity);
  CHECK(game.entity_count == 0 && game.player_count == 0);
  CHECK(qa_q1_get_entity(&game, &owned.id) == NULL);
}

static qa_q1_trace trace_open(void *ctx, qa_q1_trace_request request) {
  (void)ctx;
  qa_q1_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = 1;
  trace.end = request.end;
  return trace;
}
static int use_calls = 0;
static void use_count(void *ctx, qa_q1_services *game, qa_q1_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx;
  (void)game;
  (void)entity;
  (void)other;
  (void)activator;
  use_calls++;
}
static void test_entity_services_runtime(void) {
  qa_q1_services game;
  qa_q1_host host;
  memset(&host, 0, sizeof host);
  host.emit = capture_emit;
  host.transition = capture_transition;
  host.trace = trace_open;
  qa_q1_options options;
  memset(&options, 0, sizeof options);
  options.has_max_clients = true;
  options.max_clients = 4;
  CHECK(qa_q1_services_init(&game, host, options));
  CHECK(game.named != NULL && game.precaches != NULL);
  CHECK(game.base_team_health && game.sight_time == -1);
  CHECK(game.next_dynamic_slot == 5);
  CHECK(strcmp(qa_q1_services_precache_sound(&game, "misc/test.wav"),
               "misc/test.wav") == 0);

  qa_q1_actor *gib = qa_q1_create(&game, "gib", NULL, NULL);
  CHECK(gib->actor.id.slot == 5 && game.next_dynamic_slot == 6);
  double world_ordinal = 0, third = 30;
  qa_q1_actor *world = qa_q1_create(&game, "worldspawn", NULL, &world_ordinal);
  CHECK(world->actor.id.slot == 0);
  qa_q1_actor *sourced = qa_q1_create(&game, "func_door", NULL, &third);
  CHECK(sourced->actor.id.slot == 34);

  /* calcMove: travel time, velocity, SUB_CalcMoveDone think. */
  qa_q1_bound_action done = {"door_hit_top", NULL, think_action};
  CHECK_ABORTS(qa_q1_calc_move(&game, sourced, (qa_vec3){100, 0, 0}, 0, done));
  qa_q1_calc_move(&game, sourced, (qa_vec3){100, 0, 0}, 50, done);
  CHECK(sourced->has_move && sourced->body.velocity.x == 50);
  CHECK(sourced->next_think == 2);
  CHECK(sourced->think.name != NULL &&
        strcmp(sourced->think.name, "SUB_CalcMoveDone") == 0);

  /* useTargets: bound use dispatch + killtarget removal. */
  qa_q1_actor *lever = qa_q1_create(&game, "func_button", NULL, NULL);
  qa_q1_actor *door = qa_q1_create(&game, "func_door", NULL, NULL);
  qa_q1_actor *victim = qa_q1_create(&game, "monster_army", NULL, NULL);
  free(lever->target);
  lever->target = strdup("t1");
  free(lever->killtarget);
  lever->killtarget = strdup("k1");
  free(door->targetname);
  door->targetname = strdup("t1");
  free(victim->targetname);
  victim->targetname = strdup("k1");
  door->use = (qa_q1_bound_use){"count_use", NULL, use_count};
  qa_q1_callback_handlers use_handlers;
  memset(&use_handlers, 0, sizeof use_handlers);
  use_handlers.use = use_count;
  qa_q1_callback_registry_register(game.named, "count_use", use_handlers);
  size_t before_count = game.entity_count;
  qa_q1_use_targets(&game, lever, NULL);
  CHECK(use_calls == 1);
  CHECK(!victim->live && game.entity_count == before_count - 1);

  /* Damage fallback (no combat authority): direct health adjust. */
  door->health = 100;
  door->damageable = true;
  CHECK(qa_q1_damage(&game, door->actor.id, gib->actor.id, NULL, 30, NULL,
                     "direct", "") == 30);
  CHECK(door->health == 70);
  qa_q1_set_origin(&game, gib, (qa_vec3){0, 0, 0});
  qa_q1_set_origin(&game, door, (qa_vec3){10, 0, 0});
  qa_q1_radius_damage(&game, gib->actor.id, NULL, 100, NULL, NULL, "");
  CHECK(door->health == 70 - 95);

  /* Clone: source state, fields, body, and named think rebind. */
  qa_q1_actor_field_set(door, "message", "open");
  door->think =
      qa_q1_callback_registry_action(game.named, door, "SUB_Remove");
  door->next_think = 9;
  qa_q1_actor *clone = qa_q1_clone_entity(&game, door);
  CHECK(clone != door && strcmp(clone->classname, "func_door") == 0);
  CHECK(strcmp(qa_q1_actor_text(clone, "message"), "open") == 0);
  CHECK(clone->body.origin.x == 10 && clone->health == door->health);
  CHECK(clone->think.name != NULL &&
        strcmp(clone->think.name, "SUB_Remove") == 0);
  qa_q1_services_free(&game);
}

int main(void) {
  test_damage_call();
  test_armor_points();
  test_id1_program();
  test_damage_scale();
  test_declared_pickups();
  test_armor_stage();
  test_weapon_stage();
  test_readers();
  test_attacks_projectiles();
  test_environment();
  test_id1_damage_binding();
  test_entity_services();
  test_entity_services_family();
  test_entity_services_runtime();
  if (failures == 0) printf("ok: %d checks\n", checks);
  return failures == 0 ? 0 : 1;
}
