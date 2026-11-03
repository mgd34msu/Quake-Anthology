/* content/q1 gate full read (second pass: quakec, equipment, caps): checks for
 * the divergences the read fixed. Expected values come from
 * tests/content/oracles/q1_gate_review_b.ts (bun). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/content/q1/foundation/text.h"
#include "qa/content/q1/quakec/damage_call.h"
#include "qa/content/q1/quakec/damage_scale.h"
#include "qa/content/q1/quakec/id1_program.h"
#include "qa/foundation/support.h"

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

#define PROP(k, v) {(char *)(k), (char *)(v)}
static const qa_bounds WORLD = {{-512, -512, -512}, {512, 512, 512}};

/* 1. q1EntityString: one TS unit per byte of the fixedByteString text
 *    (oracle escape_bytes 65,92,169,10,66,92,67), and no length limit
 *    (long_message_length 3001, long_message_newline 1500). */
static void entity_string_bytes_and_length(void) {
  static const unsigned char input[] = {0x41, 0x5c, 0xc3, 0xa9, 0x5c, 0x6e, 0x42, 0x5c, 0xe9, 0x43, 0};
  static const unsigned char expected[] = {65, 92, 169, 10, 66, 92, 67, 0};
  char out[16];
  qa_q1_entity_string((const char *)input, out, sizeof out);
  CHECK(strcmp(out, (const char *)expected) == 0);

  char *long_value = malloc(3003);
  CHECK(long_value != NULL);
  memset(long_value, 'x', 1500);
  long_value[1500] = '\\';
  long_value[1501] = 'n';
  memset(long_value + 1502, 'y', 1500);
  long_value[3002] = '\0';
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  qa_q1_entity_property props[] = {PROP("classname", "worldspawn"), PROP("message", long_value)};
  const qa_q1_entity list[] = {{props, 2}};
  qa_q1_spawn_report report;
  CHECK(qa_q1_foundation_spawn_entities(fx.game, list, 1, &WORLD, 1, "gate", NULL, 0, &report));
  qa_q1_spawn_report_free(&report);
  const qa_q1_actor *spawned = NULL;
  for (size_t i = 0; i < fx.game->entity_count; i++)
    if (strcmp(fx.game->entities[i]->classname, "worldspawn") == 0) spawned = fx.game->entities[i];
  CHECK(spawned != NULL && strlen(spawned->message) == 3001);
  CHECK(spawned != NULL && strchr(spawned->message, '\n') - spawned->message == 1500);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
  free(long_value);
}

/* 2. backpackMessage lists every recognised extra (no limit of 8 entries). */
static void backpack_message_extras(void) {
  static const char *const kinds[3] = {"rogue:ammo/lava-nails", "rogue:ammo/multi-rockets", "rogue:ammo/plasma"};
  qa_q1_backpack_extra extra[12];
  for (int i = 0; i < 12; i++) extra[i] = (qa_q1_backpack_extra){kinds[i % 3], i + 1};
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = "rocketlauncher";
  contents.shells = 5;
  contents.rockets = 3;
  contents.extra = extra;
  contents.extra_count = 12;
  contents.has_extra = true;
  qa_q1_backpack_message classic = qa_q1_backpack_message_make(&contents, "classic", true);
  CHECK(classic.text != NULL &&
        strcmp(classic.text,
               "You get the Rocket Launcher, 5 shells, 3 rockets, 1 lava nails, 2 multi rockets, 3 plasma balls, "
               "4 lava nails, 5 multi rockets, 6 plasma balls, 7 lava nails, 8 multi rockets, 9 plasma balls, "
               "10 lava nails, 11 multi rockets, 12 plasma balls") == 0);
  CHECK(classic.part_count == 0);
  qa_q1_backpack_message_free(&classic);
  qa_q1_backpack_message rerelease = qa_q1_backpack_message_make(&contents, "rerelease", true);
  CHECK(strcmp(rerelease.text, "$qc_backpack_got") == 0);
  CHECK(rerelease.part_count == 30);
  const qa_q1_message_part *last = &rerelease.parts[rerelease.part_count - 1];
  CHECK(strcmp(last->text, "$qc_backpack_plasma_balls") == 0 && last->arg_count == 1 &&
        last->args[0].is_number && last->args[0].number == 12);
  qa_q1_backpack_message_free(&rerelease);
}

/* 3. Classic deathmatch selection walks every info_player_deathmatch
 *    (oracle dm_pick_1025 1024, dm_pick_1105 4). */
static void deathmatch_points_unbounded(void) {
  qa_q1_options options = q1_world_fixture_options();
  options.edition = "classic";
  options.deathmatch = 1;
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, options));
  qa_q1_entity_property props[] = {PROP("classname", "worldspawn")};
  const qa_q1_entity list[] = {{props, 1}};
  qa_q1_spawn_report report;
  CHECK(qa_q1_foundation_spawn_entities(fx.game, list, 1, &WORLD, 1, "gate", NULL, 0, &report));
  qa_q1_spawn_report_free(&report);
  qa_q1_actor **points = malloc(1100 * sizeof *points);
  CHECK(points != NULL);
  for (int i = 0; i < 1100; i++) points[i] = qa_q1_create(fx.game, "info_player_deathmatch", NULL, NULL);
  qa_q1_campaign_state campaign;
  qa_q1_campaign_state_init(&campaign, 0, 1);
  qa_q1_spawn_selector *selector = qa_q1_spawn_selector_create(fx.game, campaign.binding);
  CHECK(selector != NULL);
  qa_q1_actor *picked = NULL;
  for (int i = 0; i < 1025; i++) picked = qa_q1_spawn_selector_select(selector, false);
  CHECK(picked == points[1024]);
  for (int i = 0; i < 80; i++) picked = qa_q1_spawn_selector_select(selector, false);
  CHECK(picked == points[4]);
  qa_q1_spawn_selector_destroy(selector);
  free(points);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* ---------- quakec ---------- */
static void put_float(uint8_t *bytes, int64_t word, float value) { memcpy(bytes + word * 4, &value, 4); }
static void put_int(uint8_t *bytes, int64_t word, int32_t value) { memcpy(bytes + word * 4, &value, 4); }
static qa_mod_callback_value input_value(const char *name) {
  return (qa_mod_callback_value){"input", name, 0, NULL, {0, 0, 0}};
}
static bool message_is(const qa_caught_error *caught, const char *expected) {
  return strcmp(caught->message, expected) == 0;
}

/* 4. qcDamageScale's power test is Number.isInteger(Math.log2(c)); an infinite
 *    divisor is not a power of two (oracle log2_infinity_integer false; scale_infinity
 *    "test.qc: Unsupported QC damage scale: division requires an exactly representable reciprocal"). */
typedef struct scale_attempt {
  const qa_q1qc_program *program;
  const qa_mod_source_call *call;
  const qa_mod_qc_damage_scale *source;
} scale_attempt;
static void run_scale(void *ctx) {
  scale_attempt *attempt = ctx;
  qa_q1qc_damage_scale_destroy(qa_q1qc_damage_scale_create(attempt->program, attempt->call, attempt->source));
}
static void damage_scale_infinite_divisor(void) {
  uint8_t *initial = calloc(4096, 1);
  put_float(initial, 50, INFINITY);
  put_int(initial, 60, 1);
  qa_q1qc_definition globals[8] = {
    {"entity", 0, false, 100, "targ"}, {"entity", 0, false, 101, "inflictor"},
    {"entity", 0, false, 102, "attacker"}, {"float", 0, false, 103, "dmg"},
    {"float", 0, false, 50, "scale_const"}, {"float", 0, false, 40, "time"},
    {"entity", 0, false, 28, "self"}, {"function", 0, false, 60, "T_Damage"}};
  int64_t sizes[4] = {1, 1, 1, 1};
  qa_q1qc_function functions[2] = {{0, 0, 0, 0, "", "", NULL, 0, false},
                                   {1, 1, 100, 10, "T_Damage", "combat.qc", sizes, 4, false}};
  qa_q1qc_statement statements[4] = {{QA_Q1QC_OP_DONE, 0, 0, 0},
                                     {QA_Q1QC_OP_DIV_F, 103, 50, 103},
                                     {QA_Q1QC_OP_STORE_F, 103, 103, 0},
                                     {QA_Q1QC_OP_RETURN, 0, 0, 0}};
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 4, globals, 8, NULL, 0, functions, 2,
                             NULL, 0, initial, 4096, 0, 0, "sha256:other"};
  qa_mod_callback_value args[4] = {input_value("self"), input_value("inflictor"), input_value("attacker"),
                                   input_value("amount")};
  qa_mod_source_call call = {"T_Damage", args, 4, NULL, 0};
  qa_qc_statement proof[2] = {{QA_Q1QC_OP_DIV_F, 103, 50, 103}, {QA_Q1QC_OP_STORE_F, 103, 103, 0}};
  qa_mod_qc_damage_scale source = {NULL, "T_Damage", 1, 2, 103, proof, 2};
  scale_attempt attempt = {&program, &call, &source};
  qa_caught_error caught;
  CHECK(!qa_try(run_scale, &attempt, &caught));
  CHECK(message_is(&caught, "test.qc: Unsupported QC damage scale: division requires an exactly representable reciprocal"));
  /* A finite power of two still qualifies (log2_quarter_integer true). */
  put_float(initial, 50, 0.25f);
  CHECK(qa_try(run_scale, &attempt, &caught));
  free(initial);
}

/* 5. qcDamageCallLayout tracks every declared global: an amount alias of the 70th
 *    attacker global is an overlap (oracle layout_overlap). */
typedef struct layout_attempt { const qa_q1qc_program *program; const qa_mod_source_call *call; } layout_attempt;
static void run_layout(void *ctx) {
  layout_attempt *attempt = ctx;
  qa_q1qc_damage_call_layout_destroy(qa_q1qc_damage_call_layout_create(attempt->program, attempt->call));
}
static void damage_call_many_globals(void) {
  qa_q1qc_definition globals[75];
  char names[71][24];
  size_t global_count = 0;
  globals[global_count++] = (qa_q1qc_definition){"entity", 0, false, 100, "targ"};
  globals[global_count++] = (qa_q1qc_definition){"entity", 0, false, 101, "inflictor"};
  globals[global_count++] = (qa_q1qc_definition){"entity", 0, false, 102, "attacker"};
  globals[global_count++] = (qa_q1qc_definition){"float", 0, false, 103, "dmg"};
  qa_mod_named_value declared[71];
  for (int i = 0; i < 70; i++) {
    (void)snprintf(names[i], sizeof names[i], "attacker_alias%d", i);
    globals[global_count++] = (qa_q1qc_definition){"entity", 0, false, 200 + i, names[i]};
    declared[i] = (qa_mod_named_value){names[i], input_value("attacker")};
  }
  globals[global_count++] = (qa_q1qc_definition){"float", 0, false, 269, "amount_alias"};
  declared[70] = (qa_mod_named_value){"amount_alias", input_value("amount")};
  int64_t sizes[4] = {1, 1, 1, 1};
  qa_q1qc_function functions[2] = {{0, 0, 0, 0, "", "", NULL, 0, false},
                                   {1, 1, 100, 10, "T_Damage", "combat.qc", sizes, 4, false}};
  qa_q1qc_statement statements[2] = {{QA_Q1QC_OP_DONE, 0, 0, 0}, {QA_Q1QC_OP_RETURN, 0, 0, 0}};
  uint8_t *initial = calloc(4096, 1);
  qa_q1qc_program program = {"test.qc", "q1-netquake", statements, 2, globals, global_count, NULL, 0,
                             functions, 2, NULL, 0, initial, 4096, 0, 0, "sha256:other"};
  qa_mod_callback_value args[4] = {input_value("self"), input_value("inflictor"), input_value("attacker"),
                                   input_value("amount")};
  qa_mod_source_call call = {"T_Damage", args, 4, declared, 71};
  layout_attempt attempt = {&program, &call};
  qa_caught_error caught;
  CHECK(!qa_try(run_layout, &attempt, &caught));
  CHECK(message_is(&caught, "test.qc: Unsupported QC damage call: distinct damage roles overlap one source global"));
  free(initial);
}

/* 6. derivedBindings is keyed by the TS QcProgram. Two C mirrors of one program
 *    (same borrowed initial_globals) share the declared pinned call
 *    (oracle pinned_declared_seen true), and a different declaration is refused
 *    (pinned_other_declaration "progs.dat: QC program already has a different
 *    qualified combat call"). */
typedef struct binding_attempt { const qa_q1qc_program *program; const qa_mod_source_call *call; } binding_attempt;
static void run_binding(void *ctx) {
  binding_attempt *attempt = ctx;
  (void)qa_q1_id1_program_binding_get(attempt->program, attempt->call);
}
static void pinned_binding_identity(void) {
  qa_q1qc_function *functions = calloc(118, sizeof *functions);
  for (int i = 0; i < 118; i++) functions[i] = (qa_q1qc_function){i, 0, 0, 0, "", "", NULL, 0, false};
  int64_t sizes[4] = {1, 1, 1, 1};
  functions[117] = (qa_q1qc_function){117, 1421, 1580, 10, "T_Damage", "combat.qc", sizes, 4, false};
  qa_q1qc_definition globals[5] = {
    {"entity", 0, false, 1580, "targ"}, {"entity", 0, false, 1581, "inflictor"},
    {"entity", 0, false, 1582, "attacker"}, {"float", 0, false, 1583, "damage"},
    {"float", 0, false, 300, "gate_time"}};
  uint8_t *initial = calloc(8192, 1);
  const qa_q1qc_program first = {"test.qc", "q1-netquake", NULL, 0, globals, 5, NULL, 0, functions, 118,
                                 NULL, 0, initial, 8192, 0, 0,
                                 "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580"};
  qa_q1qc_program *second = malloc(sizeof *second);
  *second = first;
  qa_mod_callback_value args[4] = {input_value("self"), input_value("inflictor"), input_value("attacker"),
                                   input_value("amount")};
  qa_mod_source_call declared = {"T_Damage", args, 4, NULL, 0};
  binding_attempt attempt = {&first, &declared};
  qa_caught_error caught;
  CHECK(qa_try(run_binding, &attempt, &caught));
  const qa_q1_id1_program_binding *seen = qa_q1_id1_program_binding_get(second, NULL);
  CHECK(seen->damage_call != NULL && seen->damage_call->has_declaration);
  qa_mod_named_value time_global[1] = {{"gate_time", input_value("time")}};
  qa_mod_source_call other = {"T_Damage", args, 4, time_global, 1};
  attempt = (binding_attempt){second, &other};
  CHECK(!qa_try(run_binding, &attempt, &caught));
  CHECK(message_is(&caught, "progs.dat: QC program already has a different qualified combat call"));
  free(second);
  free(functions);
  /* initial stays allocated: it is the program identity the binding cache keeps
   * for the process, as the TS WeakMap keeps its entry while the program lives. */
}

int main(void) {
  entity_string_bytes_and_length();
  backpack_message_extras();
  deathmatch_points_unbounded();
  damage_scale_infinite_divisor();
  damage_call_many_globals();
  pinned_binding_identity();
  printf("test_q1_gate_review_b: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
