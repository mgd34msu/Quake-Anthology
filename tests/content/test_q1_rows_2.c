/*
 * C checks for src/content/q1 rows (quakec, foundation text). Expected
 * values come from the TypeScript under bun on the same inputs
 * (oracle_b_rows.ts in the porting scratchpad):
 *   A "A" 9 12.25 7 3.5
 *   B "B" 3 "progs.dat: Original QC damage amount is not finite and nonnegative" 5 77 1.5
 *     "B2" 6 5
 *   C "C" 1 [{"kind":"input","name":"self"}] [{"kind":"input","name":"self"}]
 *   D "D" 4096 0 2.5 11 22
 *   E "E" "progs.dat: QW pickup statement 1643 differs from its qualified artifact"
 *   F "F" "progs.dat: missing function "
 *   G "G" " x"
 * TS finally blocks run when a throw is caught above them; the C ports
 * restore the same state through qa_cleanup when qa_try catches a raise.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/text.h"
#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/damage_call.h"
#include "qa/content/q1/quakec/damage_scale.h"
#include "qa/content/q1/quakec/pickup_stage.h"
#include "qa/content/q1/quakec/weapon_stage.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int sentinel;
#define QW_DIGEST "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830"

static double num_store(void *ctx, double v) { CHECK(ctx == &sentinel); return (double)(float)v; }
static double num_add(void *ctx, double a, double b) { CHECK(ctx == &sentinel); return a + b; }
static double num_sub(void *ctx, double a, double b) { CHECK(ctx == &sentinel); return a - b; }
static double num_mul(void *ctx, double a, double b) { CHECK(ctx == &sentinel); return a * b; }
static double num_div(void *ctx, double a, double b) { CHECK(ctx == &sentinel); return a / b; }
static double num_sqrt(void *ctx, double v) { CHECK(ctx == &sentinel); return sqrt(v); }
static qa_numeric_operations numeric(void) {
  qa_numeric_operations n = {.ctx = &sentinel, .store = num_store, .add = num_add, .subtract = num_sub,
                             .multiply = num_mul, .divide = num_div, .square_root = num_sqrt};
  return n;
}
static qa_actor_id make_actor(int64_t slot, int64_t generation) {
  qa_actor_id id;
  memset(&id, 0, sizeof id);
  id.slot = slot;
  id.generation = generation;
  return id;
}

/* --- A: projectQcDamageCall restores declared globals when execute throws --- */
typedef struct project_case {
  qa_q1qc_machine *machine;
  const qa_q1qc_damage_call_layout *layout;
  double inside_self, inside_amount;
} project_case;
static void throwing_run(void *ctx, qa_q1qc_prepare_fn prepare, void *prepare_ctx) {
  project_case *c = ((void **)ctx)[1];
  CHECK(((void **)ctx)[0] == &sentinel);
  prepare(prepare_ctx, c->machine);
  c->inside_self = qa_q1qc_words_int(&c->machine->globals, 100);
  c->inside_amount = qa_q1qc_words_float(&c->machine->globals, 101);
  qa_range_error("boom");
}
static void no_words(void *ctx, const int32_t words[3]) { (void)words; CHECK(((void **)ctx)[0] == &sentinel); }
static void project_body(void *ctx) {
  project_case *c = ctx;
  void *exec_ctx[2] = {&sentinel, c};
  qa_q1qc_function_execution execute = {exec_ctx, throwing_run, no_words, no_words};
  qa_q1qc_damage_call_values captured = {7, 2, 3, 3.5}, effective = {9, 2, 3, 12.25};
  qa_q1qc_project_damage_call(c->machine, c->layout, captured, effective, execute);
}
static void test_project_finally(void) {
  uint8_t *globals = calloc(256, 4);
  qa_q1qc_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.globals = (qa_q1qc_words){globals, 256 * 4};
  qa_q1qc_words_set_int(&machine.globals, 100, 7);
  qa_q1qc_words_set_float(&machine.globals, 101, 3.5);
  qa_q1qc_words_set_int(&machine.globals, 7, 2);
  qa_q1qc_words_set_int(&machine.globals, 10, 3);
  qa_q1qc_damage_role_location self = {.is_global = true, .word = 100};
  qa_q1qc_damage_role_location inflictor = {.is_global = false, .index = 1, .frame_word = 0};
  qa_q1qc_damage_role_location attacker = {.is_global = false, .index = 2, .frame_word = 0};
  qa_q1qc_damage_role_location amount = {.is_global = true, .word = 101};
  qa_q1qc_damage_call_layout layout;
  memset(&layout, 0, sizeof layout);
  layout.function_index = 1;
  layout.roles[QA_Q1QC_ROLE_SELF] = (qa_q1qc_role_locations){&self, 1};
  layout.roles[QA_Q1QC_ROLE_INFLICTOR] = (qa_q1qc_role_locations){&inflictor, 1};
  layout.roles[QA_Q1QC_ROLE_ATTACKER] = (qa_q1qc_role_locations){&attacker, 1};
  layout.roles[QA_Q1QC_ROLE_AMOUNT] = (qa_q1qc_role_locations){&amount, 1};
  project_case c = {&machine, &layout, 0, 0};
  qa_caught_error caught;
  CHECK(!qa_try(project_body, &c, &caught));
  CHECK(strcmp(caught.message, "boom") == 0);
  CHECK(c.inside_self == 9 && c.inside_amount == 12.25);
  CHECK(qa_q1qc_words_int(&machine.globals, 100) == 7);
  CHECK(qa_q1qc_words_float(&machine.globals, 101) == 3.5);
  free(globals);
}

/* --- B: evaluateQcDamageAmount restores scratch and staging on a negative result --- */
static void put_float(uint8_t *bytes, int64_t word, float value) { memcpy(bytes + (size_t)word * 4, &value, 4); }
static void put_int(uint8_t *bytes, int64_t word, int32_t value) { memcpy(bytes + (size_t)word * 4, &value, 4); }
typedef struct scale_case {
  qa_q1qc_machine machine;
  qa_q1qc_damage_scale *scale;
  double staged, result;
  bool negative;
} scale_case;
static double region_execute(void *ctx, qa_q1qc_inline_region region, int64_t count) {
  (void)region; (void)count;
  scale_case *c = ctx;
  c->staged = qa_q1qc_words_float(&c->machine.globals, 13);
  qa_q1qc_words_set_float(&c->machine.globals, 103, c->staged * 2);
  return c->negative ? -1 : qa_q1qc_words_float(&c->machine.globals, 103);
}
static void scale_body(void *ctx) {
  scale_case *c = ctx;
  c->result = qa_q1qc_evaluate_damage_amount(&c->machine, c->scale, make_actor(3, 1), 4096, 0, 3);
}
static void test_scale_finally(void) {
  qa_q1qc_statement statements[4] = {{0}, {QA_Q1QC_OP_MUL_F, 103, 50, 103}, {QA_Q1QC_OP_STORE_F, 103, 103, 0},
                                     {QA_Q1QC_OP_RETURN, 0, 0, 0}};
  uint8_t *initial = calloc(4096, 1);
  put_float(initial, 50, 2.0f);
  put_int(initial, 60, 1);
  qa_q1qc_definition globals[8] = {{"entity", 0, false, 100, "targ"}, {"entity", 0, false, 101, "inflictor"},
                                   {"entity", 0, false, 102, "attacker"}, {"float", 0, false, 103, "dmg"},
                                   {"float", 0, false, 50, "scale_const"}, {"float", 0, false, 40, "time"},
                                   {"entity", 0, false, 28, "self"}, {"function", 0, false, 60, "T_Damage"}};
  int64_t sizes[4] = {1, 1, 1, 1};
  qa_q1qc_function functions[2] = {{0, 0, 0, 0, "", "", NULL, 0, false},
                                   {1, 1, 100, 10, "T_Damage", "combat.qc", sizes, 4, false}};
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 4, globals, 8, NULL, 0, functions, 2,
                             NULL, 0, initial, 4096, 0, 0, "sha256:other"};
  qa_mod_callback_value args[4] = {{"input", "self", 0, NULL, {0, 0, 0}}, {"input", "inflictor", 0, NULL, {0, 0, 0}},
                                   {"input", "attacker", 0, NULL, {0, 0, 0}}, {"input", "amount", 0, NULL, {0, 0, 0}}};
  qa_mod_source_call call = {"T_Damage", args, 4, NULL, 0};
  qa_qc_statement proof[2] = {{QA_Q1QC_OP_MUL_F, 103, 50, 103}, {QA_Q1QC_OP_STORE_F, 103, 103, 0}};
  qa_mod_qc_damage_scale source = {NULL, "T_Damage", 1, 2, 103, proof, 2};
  scale_case c;
  memset(&c, 0, sizeof c);
  c.scale = qa_q1qc_damage_scale_create(&program, &call, &source);
  CHECK(c.scale != NULL && c.scale->scratch_count == 1 && c.scale->scratch[0] == 103);
  uint8_t *words = calloc(512, 4);
  qa_q1qc_entity_memory entities = {1024, 0, 256, 16, 16, calloc(16, 1024)};
  c.machine.ctx = &c;
  c.machine.program = &program;
  c.machine.globals = (qa_q1qc_words){words, 512 * 4};
  c.machine.entities = &entities;
  c.machine.numeric = numeric();
  c.machine.execute_region = region_execute;
  qa_q1qc_words_set_float(&c.machine.globals, 50, 2);
  qa_q1qc_words_set_float(&c.machine.globals, 103, 5);
  qa_q1qc_words_set_int(&c.machine.globals, 4, 77);
  qa_q1qc_words_set_float(&c.machine.globals, 13, 1.5);
  c.negative = true;
  qa_caught_error caught;
  CHECK(!qa_try(scale_body, &c, &caught));
  CHECK(strcmp(caught.message, "progs.dat: Original QC damage amount is not finite and nonnegative") == 0);
  CHECK(c.staged == 3);
  CHECK(qa_q1qc_words_float(&c.machine.globals, 103) == 5);
  CHECK(qa_q1qc_words_int(&c.machine.globals, 4) == 77);
  CHECK(qa_q1qc_words_float(&c.machine.globals, 13) == 1.5);
  c.negative = false;
  CHECK(qa_try(scale_body, &c, &caught));
  CHECK(c.result == 6 && qa_q1qc_words_float(&c.machine.globals, 103) == 5);
  qa_q1qc_damage_scale_destroy(c.scale);
  free(entities.bytes);
  free(words);
  free(initial);
}

/* --- C: derived rerelease client calls keep their own self argument --- */
static void test_derived_client_self(void) {
  qa_q1qc_statement statements[3] = {{QA_Q1QC_OP_RETURN, 0, 0, 0}, {QA_Q1QC_OP_RETURN, 0, 0, 0},
                                     {QA_Q1QC_OP_RETURN, 200, 0, 0}};
  qa_q1qc_definition globals[1] = {{"entity", 0, false, 200, "spot"}};
  int64_t one[1] = {1};
  qa_q1qc_function functions[3] = {{0, 0, 0, 0, "", "", NULL, 0, false},
                                   {1, 1, 100, 1, "PutClientInServer", "client.qc", one, 1, false},
                                   {2, 2, 110, 1, "SelectSpawnPoint", "client.qc", one, 1, false}};
  uint8_t *initial = calloc(4096, 1);
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 3, globals, 1, NULL, 0, functions, 3,
                             NULL, 0, initial, 4096, 0, 0, "sha256:other"};
  qa_q1qc_weapon_stage *stage = qa_q1qc_derived_weapon_stage(&program);
  /* Scrub the stack the derivation used, so a dangling argument pointer reads garbage. */
  volatile char scrub[4096];
  memset((char *)scrub, 0x5a, sizeof scrub);
  CHECK(stage->has_client && stage->client.spawn.function_index == 1);
  const qa_q1qc_client_stage_call *calls[2] = {&stage->client.spawn, &stage->client.select_spawn};
  for (int i = 0; i < 2; i++) {
    CHECK(calls[i]->call.argument_count == 1);
    CHECK(calls[i]->call.arguments == calls[i]->arg_storage);
    CHECK(strcmp(calls[i]->call.arguments[0].kind, "input") == 0);
    CHECK(strcmp(calls[i]->call.arguments[0].name, "self") == 0);
  }
  qa_q1qc_weapon_stage_destroy(stage);
  free(initial);
}

/* --- D: invokeQcClientStage restores self/other when the source call throws --- */
typedef struct invoke_case { qa_q1qc_machine machine; double seen[3]; qa_q1qc_client_stage_call stage; } invoke_case;
static void throwing_execute(void *ctx, int64_t function_index, int64_t count) {
  (void)count;
  invoke_case *c = ctx;
  CHECK(function_index == 1);
  c->seen[0] = qa_q1qc_words_int(&c->machine.globals, 28);
  c->seen[1] = qa_q1qc_words_int(&c->machine.globals, 29);
  c->seen[2] = qa_q1qc_words_float(&c->machine.globals, 31);
  qa_range_error("boom");
}
static int64_t reference_of(void *ctx, const qa_actor_id *actor) {
  CHECK(ctx == &sentinel);
  return actor == NULL ? 0 : 4096;
}
static void invoke_body(void *ctx) {
  invoke_case *c = ctx;
  (void)qa_q1qc_invoke_client_stage(&c->machine, &c->stage, make_actor(5, 1), 2.5, reference_of, &sentinel);
}
static void test_invoke_finally(void) {
  qa_q1qc_definition globals[3] = {{"entity", 0, false, 28, "self"}, {"entity", 0, false, 29, "other"},
                                   {"float", 0, false, 31, "time"}};
  qa_q1qc_program program;
  memset(&program, 0, sizeof program);
  program.source = "progs.dat";
  program.globals = globals;
  program.global_count = 3;
  uint8_t *words = calloc(256, 4);
  invoke_case c;
  memset(&c, 0, sizeof c);
  c.machine.ctx = &c;
  c.machine.program = &program;
  c.machine.globals = (qa_q1qc_words){words, 256 * 4};
  c.machine.execute = throwing_execute;
  c.stage.function_index = 1;
  c.stage.call = (qa_mod_source_call){"PutClientInServer", NULL, 0, NULL, 0};
  qa_q1qc_words_set_int(&c.machine.globals, 28, 11);
  qa_q1qc_words_set_int(&c.machine.globals, 29, 22);
  qa_caught_error caught;
  CHECK(!qa_try(invoke_body, &c, &caught));
  CHECK(c.seen[0] == 4096 && c.seen[1] == 0 && c.seen[2] == 2.5);
  CHECK(qa_q1qc_words_int(&c.machine.globals, 28) == 11);
  CHECK(qa_q1qc_words_int(&c.machine.globals, 29) == 22);
  free(words);
}

/* --- E: QW pickup checks report in TS order (armor join last) --- */
typedef struct pickup_case { qa_q1qc_program *program; } pickup_case;
static void pickup_body(void *ctx) {
  size_t count = 0;
  qa_q1qc_pickup_stage *stages = qa_q1qc_pickup_stages(((pickup_case *)ctx)->program, &count);
  qa_q1qc_pickup_stages_destroy(stages, count);
}
static void test_qw_pickup_order(void) {
  qa_q1qc_statement *statements = calloc(2711, sizeof *statements);
  statements[1147] = (qa_q1qc_statement){QA_Q1QC_OP_LOAD_F, 29, 181, 1373};
  statements[1151] = (qa_q1qc_statement){QA_Q1QC_OP_GE, 1375, 1376, 1377};
  statements[1154] = (qa_q1qc_statement){QA_Q1QC_OP_ADDRESS, 29, 181, 1378};
  statements[1166] = (qa_q1qc_statement){QA_Q1QC_OP_STORE_PF, 1387, 1380, 0};
  statements[1167] = (qa_q1qc_statement){QA_Q1QC_OP_ADDRESS, 28, 103, 1388};
  qa_q1qc_function *functions = calloc(131, sizeof *functions);
  for (int i = 0; i < 131; i++) functions[i] = (qa_q1qc_function){i, 0, 0, 0, "", "", NULL, 0, false};
  functions[98] = (qa_q1qc_function){98, 1115, 1350, 3, "armor_touch", "items.qc", NULL, 0, false};
  functions[113] = (qa_q1qc_function){113, 1627, 1592, 2, "ammo_touch", "items.qc", NULL, 0, false};
  functions[106] = (qa_q1qc_function){106, 1312, 1451, 7, "weapon_touch", "items.qc", NULL, 0, false};
  functions[130] = (qa_q1qc_function){130, 2412, 1929, 7, "BackpackTouch", "items.qc", NULL, 0, false};
  functions[105] = (qa_q1qc_function){105, 1300, 1442, 4, "Deathmatch_Weapon", "items.qc", NULL, 0, false};
  functions[104] = (qa_q1qc_function){104, 1280, 1434, 0, "WeaponCode", "items.qc", NULL, 0, false};
  qa_q1qc_program program;
  memset(&program, 0, sizeof program);
  program.source = "progs.dat";
  program.api_kind = "q1-quakeworld";
  program.statements = statements;
  program.statement_count = 2711;
  program.functions = functions;
  program.function_count = 131;
  program.digest = QW_DIGEST;
  pickup_case c = {&program};
  qa_caught_error caught;
  CHECK(!qa_try(pickup_body, &c, &caught));
  CHECK(strcmp(caught.message, "progs.dat: QW pickup statement 1643 differs from its qualified artifact") == 0);
  free(functions);
  free(statements);
}

/* --- F: a declared repeat's release callee resolves by name first --- */
typedef struct declared_case { qa_q1qc_program *program; const qa_qc_weapon_stage_declaration *declared; } declared_case;
static void declared_body(void *ctx) {
  declared_case *c = ctx;
  qa_q1qc_weapon_stage_destroy(qa_q1qc_declared_weapon_stage(c->program, c->declared));
}
static void test_declared_callee_name(void) {
  qa_q1qc_statement statements[8] = {{0}, {0}, {QA_Q1QC_OP_LOAD_F, 28, 5, 300}, {QA_Q1QC_OP_IF_NOT, 300, 3, 0},
                                     {QA_Q1QC_OP_CALL0, 60, 0, 0}, {QA_Q1QC_OP_RETURN, 0, 0, 0}, {0}, {0}};
  qa_q1qc_function functions[4] = {{0, 0, 0, 0, "", "", NULL, 0, false},
                                   {1, 1, 100, 0, "W_WeaponFrame", "weapons.qc", NULL, 0, false},
                                   {2, 2, 300, 1, "player_shot1", "player.qc", NULL, 0, false},
                                   {3, 7, 400, 0, "", "", NULL, 0, false}};
  uint8_t *initial = calloc(4096, 1);
  put_int(initial, 60, 3);
  qa_q1qc_program program = {"progs.dat", "q1-netquake", statements, 8, NULL, 0, NULL, 0, functions, 4,
                             NULL, 0, initial, 4096, 0, 0, "sha256:other"};
  const char *continuations[1] = {"player_shot1"};
  qa_qc_statement proof[2] = {{QA_Q1QC_OP_LOAD_F, 28, 5, 300}, {QA_Q1QC_OP_IF_NOT, 300, 3, 0}};
  qa_qc_weapon_stage_repeat repeat = {"player_shot1", 2, 3, 300, 1, proof, 2};
  qa_qc_weapon_stage_declaration declared = {"W_WeaponFrame", {continuations, 1}, &repeat, 1};
  declared_case c = {&program, &declared};
  qa_caught_error caught;
  CHECK(!qa_try(declared_body, &c, &caught));
  CHECK(strcmp(caught.message, "progs.dat: missing function ") == 0);
  free(initial);
}

/* --- G: placeholder indexes never wrap --- */
static void test_text_index(void) {
  qa_q1_msg_arg args[1] = {{false, 0, "x"}};
  char out[128];
  qa_q1_classic_text("{18446744073709551616} {0}", args, 1, out, sizeof out);
  CHECK(strcmp(out, " x") == 0);
}

int main(void) {
  test_project_finally();
  test_scale_finally();
  test_derived_client_self();
  test_invoke_finally();
  test_qw_pickup_order();
  test_declared_callee_name();
  test_text_index();
  printf("test_q1_rows_2: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
