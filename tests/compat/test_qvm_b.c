/* Port of key TS cases for compat/qvm second-half modules
 * (tests/compat/qvm behaviors). GPL-2.0-or-later. */
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

#include "qa/compat/qvm/legacy_bot_syscalls.h"
#include "qa/compat/qvm/legacy_client_abi.h"
#include "qa/compat/qvm/legacy_presentation.h"
#include "qa/compat/qvm/math_syscalls.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/memory_syscalls.h"
#include "qa/compat/qvm/mod_actors.h"
#include "qa/compat/qvm/mod_objectives.h"
#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/compat/qvm/script_record.h"
#include "qa/compat/qvm/snap_vector_syscalls.h"
#include "qa/compat/qvm/symbols.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/compat/qvm/trace_record.h"
#include "qa/core/numeric.h"

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

static int32_t f32_word(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return (int32_t)bits;
}

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  bytes[offset] = (uint8_t)bits;
  bytes[offset + 1] = (uint8_t)(bits >> 8);
  bytes[offset + 2] = (uint8_t)(bits >> 16);
  bytes[offset + 3] = (uint8_t)(bits >> 24);
}

static void put_f32(uint8_t *bytes, size_t offset, float value) {
  put_i32(bytes, offset, f32_word(value));
}

static void test_operations(void) {
  CHECK(qa_qvm_evaluate_unary(QA_QVMY_OP_SEX8, 0x80) == -128);
  CHECK(qa_qvm_evaluate_unary(QA_QVMY_OP_SEX16, 0x8000) == -32768);
  CHECK(qa_qvm_evaluate_unary(QA_QVMY_OP_NEGI, 5) == -5);
  CHECK(qa_qvm_evaluate_unary(QA_QVMY_OP_CVIF, 3) == f32_word(3.0f));
  CHECK(qa_qvm_evaluate_unary(QA_QVMY_OP_CVFI, f32_word(2.9f)) == 2);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_ADD, INT32_MAX, 1) == INT32_MIN);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_DIVI, -7, 2) == -3);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_MODI, -7, 2) == -1);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_DIVU, -1, 2) == 2147483647);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_MULI, 65536, 65536) == 0);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_LSH, 1, 31) == INT32_MIN);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_RSHU, -1, 1) == 2147483647);
  CHECK(qa_qvm_evaluate_binary(QA_QVMY_OP_ADDF, f32_word(1.5f), f32_word(2.25f)) ==
        f32_word(3.75f));
  CHECK(qa_qvm_evaluate_branch(QA_QVMY_OP_LTU, -1, 1) == false);
  CHECK(qa_qvm_evaluate_branch(QA_QVMY_OP_LTI, -1, 1) == true);
  CHECK(qa_qvm_evaluate_branch(QA_QVMY_OP_EQF, f32_word(1.0f), f32_word(1.0f)) == true);
  /* Landed opcode alias must be the same value. */
  CHECK(QA_QVMY_OP_ENTER == QA_QVM_OP_ENTER);
  CHECK_ABORTS((void)qa_qvm_evaluate_binary(QA_QVMY_OP_DIVI, 1, 0));
  CHECK_ABORTS((void)qa_qvm_evaluate_binary(QA_QVMY_OP_DIVI, INT32_MIN, -1));
  CHECK_ABORTS((void)qa_qvm_evaluate_binary(QA_QVMY_OP_LSH, 1, 32));
  CHECK_ABORTS((void)qa_qvm_evaluate_binary(QA_QVMY_OP_LSH, 1, -1));
  CHECK_ABORTS((void)qa_qvm_evaluate_unary(QA_QVMY_OP_ADD, 1));
  CHECK_ABORTS((void)qa_qvm_evaluate_branch(QA_QVMY_OP_ADD, 1, 1));
}

static void test_legacy_client_abi(void) {
  const char *argv[] = {"cs", "20", "value"};
  size_t count = 0;
  char **out = qa_qvm_legacy_client_command(argv, 3, "q3-1.16n-base", &count);
  CHECK(count == 3 && strcmp(out[1], "12") == 0 && strcmp(out[2], "value") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  out = qa_qvm_legacy_client_command(argv, 3, "q3-modern", &count);
  CHECK(strcmp(out[1], "20") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *other[] = {"print", "20"};
  out = qa_qvm_legacy_client_command(other, 2, "q3-1.16n-base", &count);
  CHECK(strcmp(out[0], "print") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *unmapped[] = {"cs", "16"};
  size_t ignored = 0;
  CHECK_ABORTS((void)qa_qvm_legacy_client_command(unmapped, 2, "q3-1.16n-base", &ignored));
  const char *bad[] = {"cs", "x"};
  CHECK_ABORTS((void)qa_qvm_legacy_client_command(bad, 2, "q3-1.16n-base", &ignored));
}

static void test_legacy_presentation(void) {
  /* events table: modern 47 (EV_BULLET_HIT_FLESH) is legacy 48. */
  CHECK(qa_qvm_legacy_event(47, "q3-1.16n-base", false) == 48);
  CHECK(qa_qvm_legacy_event(48, "q3-1.16n-base", true) == 47);
  CHECK(qa_qvm_legacy_event(47 | 0x100, "q3-1.16n-base", false) == (48 | 0x100));
  CHECK(qa_qvm_legacy_event(47, "q3-modern", false) == 47);
  CHECK_ABORTS((void)qa_qvm_legacy_event(200, "q3-1.16n-base", false));
  CHECK(qa_qvm_legacy_entity_type(5, "q3-1.16n-base", false) == 5);
  CHECK_ABORTS((void)qa_qvm_legacy_entity_type(12, "q3-1.16n-base", true));
  double values[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  double out[16];
  qa_qvm_legacy_persistent(values, 16, "q3-1.16n-base", out);
  CHECK(out[0] == 0 && out[4] == 4 && out[5] == 0 && out[6] == 7 && out[13] == 11);
  CHECK(qa_qvm_legacy_configstring(12, "q3-1.16n-base") == 20);
  CHECK(qa_qvm_legacy_configstring(30, "q3-1.16n-base") == 30);
  CHECK_ABORTS((void)qa_qvm_legacy_configstring(20, "q3-1.16n-base"));
  CHECK(qa_qvm_legacy_powerup_bits(0x1ff, "q3-1.16n-base") == 0x1ff);
  CHECK_ABORTS((void)qa_qvm_legacy_powerup_bits(0x200, "q3-1.16n-base"));
  /* BotActionFlag RESPAWN=0x8 -> legacy 4; attack/use bits 0..1 pass. */
  CHECK(qa_qvm_legacy_bot_action_flags(0x8 | 0x2) == (4 | 0x2));
  CHECK(qa_qvm_legacy_bot_action_flags(0x10) == 8);        /* JUMP */
  CHECK(qa_qvm_legacy_bot_action_flags(0x20) == 8);        /* MOVE_UP shares */
  CHECK(qa_qvm_legacy_bot_action_flags(0x80000) == 4096);  /* WALK */
}

static void test_math_and_memory_syscalls(void) {
  uint8_t bytes[4096];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);

  uint8_t words[16];
  int32_t out = -1;
  put_i32(words, 0, 106); /* sqrt */
  put_f32(words, 4, 16.0f);
  CHECK(qa_qvm_math_syscall("game", words, sizeof words, &out) && out == f32_word(4.0f));
  put_i32(words, 0, 107); /* floor: game role does not own it */
  put_f32(words, 4, 1.5f);
  CHECK(!qa_qvm_math_syscall("game", words, sizeof words, &out));
  CHECK(qa_qvm_math_syscall("cgame", words, sizeof words, &out) && out == f32_word(1.0f));
  put_i32(words, 0, 999);
  CHECK(!qa_qvm_math_syscall("game", words, sizeof words, &out));

  /* memset trap 100 at guest 64, count 4, value from source word. */
  put_i32(words, 0, 100);
  put_i32(words, 4, 64);
  put_i32(words, 8, 0x1ab);
  put_i32(words, 12, 4);
  CHECK(qa_qvm_memory_syscall("game", words, sizeof words, &memory, &out) && out == 0);
  CHECK(bytes[64] == 0xab && bytes[67] == 0xab);
  /* memcpy trap 101 guest 64 -> 128. */
  put_i32(words, 0, 101);
  put_i32(words, 4, 128);
  put_i32(words, 8, 64);
  put_i32(words, 12, 4);
  CHECK(qa_qvm_memory_syscall("game", words, sizeof words, &memory, &out) && out == 0);
  CHECK(bytes[128] == 0xab && bytes[131] == 0xab);
  /* Overlapping memcpy aborts. */
  uint8_t overlap[16];
  put_i32(overlap, 0, 101);
  put_i32(overlap, 4, 66);
  put_i32(overlap, 8, 64);
  put_i32(overlap, 12, 4);
  qa_qvm_memory forked;
  CHECK_ABORTS(qa_qvm_memory_init(&forked, bytes, sizeof bytes, NULL, NULL);
               (void)qa_qvm_memory_syscall("game", overlap, sizeof overlap, &forked, &out));
  /* strncpy trap 102 returns the destination word and zero-pads. */
  memcpy(bytes + 256, "hi", 3);
  put_i32(words, 0, 102);
  put_i32(words, 4, 512);
  put_i32(words, 8, 256);
  put_i32(words, 12, 5);
  CHECK(qa_qvm_memory_syscall("game", words, sizeof words, &memory, &out) && out == 512);
  CHECK(bytes[512] == 'h' && bytes[513] == 'i' && bytes[514] == 0 && bytes[516] == 0);
  /* Null strncpy source aborts. */
  uint8_t null_src[16];
  put_i32(null_src, 0, 102);
  put_i32(null_src, 4, 512);
  put_i32(null_src, 8, 0);
  put_i32(null_src, 12, 5);
  CHECK_ABORTS(qa_qvm_memory_init(&forked, bytes, sizeof bytes, NULL, NULL);
               (void)qa_qvm_memory_syscall("game", null_src, sizeof null_src, &forked, &out));

  /* SnapVector trap 42 (game): nearest-even, 2.5 -> 2, 3.5 -> 4. */
  put_f32(bytes, 1024, 2.5f);
  put_f32(bytes, 1028, 3.5f);
  put_f32(bytes, 1032, -1.4f);
  put_i32(words, 0, 42);
  put_i32(words, 4, 1024);
  CHECK(qa_qvm_snap_vector_syscall("game", words, sizeof words, &memory, &out) && out == 0);
  float snapped;
  uint32_t bits;
  bits = (uint32_t)bytes[1024] | ((uint32_t)bytes[1025] << 8) | ((uint32_t)bytes[1026] << 16) |
         ((uint32_t)bytes[1027] << 24);
  memcpy(&snapped, &bits, sizeof snapped);
  CHECK(snapped == 2.0f);
  CHECK(!qa_qvm_snap_vector_syscall("ui", words, sizeof words, &memory, &out));

  /* Non-power-of-two allocation aborts. */
  uint8_t odd[100];
  CHECK_ABORTS(qa_qvm_memory_init(&forked, odd, sizeof odd, NULL, NULL));
  qa_qvm_memory_destroy(&memory);
}

static void objective_current(void *ctx) { *(int *)ctx += 1; }

static void test_objectives(void) {
  uint8_t bytes[1024];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_mod_objective_address direct;
  memset(&direct, 0, sizeof direct);
  direct.address = 16;
  qa_qvm_validate_objective_address(1024, &direct);
  int calls = 0;
  CHECK(qa_qvm_resolve_objective_address(&memory, 1024, &direct, objective_current, &calls) ==
        16);
  CHECK(calls == 1);
  /* Global pointer: word at 16 holds 64; offset 8 resolves to 72. */
  put_i32(bytes, 16, 64);
  qa_qvm_mod_objective_address global;
  memset(&global, 0, sizeof global);
  global.is_global_pointer = true;
  global.pointer.address = 16;
  global.pointer.offset = 8;
  CHECK(qa_qvm_resolve_objective_address(&memory, 1024, &global, objective_current, &calls) ==
        72);
  /* A fractional offset makes the summed address non-integral: abort
   * (the C draft previously truncated the offset to int64 first). */
  qa_qvm_mod_objective_address fractional = global;
  fractional.pointer.offset = 4.5;
  int ignored = 0;
  CHECK_ABORTS((void)qa_qvm_resolve_objective_address(&memory, 1024, &fractional,
                                                      objective_current, &ignored));
  /* Null source pointer aborts. */
  put_i32(bytes, 16, 0);
  CHECK_ABORTS((void)qa_qvm_resolve_objective_address(&memory, 1024, &global,
                                                      objective_current, &ignored));
  /* Misaligned direct address aborts in validate. */
  qa_qvm_mod_objective_address misaligned;
  memset(&misaligned, 0, sizeof misaligned);
  misaligned.address = 6;
  CHECK_ABORTS(qa_qvm_validate_objective_address(1024, &misaligned));
  qa_qvm_memory_destroy(&memory);
}

static uint8_t *symbol_allocate(void *ctx, size_t size, const char *resource) {
  (void)ctx;
  (void)resource;
  return calloc(1, size);
}

static void test_records_and_symbols(void) {
  CHECK(qa_qvm_player_state_bytes("q3-modern") == 468);
  CHECK(qa_qvm_player_state_bytes("q3-1.16n-base") == 444);

  /* Trace record layout: fraction at 8, endpos at 12, entityNum at 52. */
  uint8_t trace_bytes[QA_QVM_TRACE_BYTES];
  memset(trace_bytes, 0, sizeof trace_bytes);
  qa_qvm_trace_record record;
  memset(&record, 0, sizeof record);
  record.result.fraction = 0.5;
  record.result.end = (qa_vec3){1, 2, 3};
  record.result.plane.plane.normal = (qa_vec3){0, 0, 1};
  record.result.plane.plane.distance = 4;
  record.result.plane.type = 2;
  record.result.plane.signbits = 0;
  record.entity_num = 1023;
  qa_qvm_write_trace(trace_bytes, sizeof trace_bytes, &record);
  float fraction;
  uint32_t bits = (uint32_t)trace_bytes[8] | ((uint32_t)trace_bytes[9] << 8) |
                  ((uint32_t)trace_bytes[10] << 16) | ((uint32_t)trace_bytes[11] << 24);
  memcpy(&fraction, &bits, sizeof fraction);
  CHECK(fraction == 0.5f);
  CHECK(trace_bytes[40] == 2);
  CHECK((int32_t)((uint32_t)trace_bytes[52] | ((uint32_t)trace_bytes[53] << 8) |
                  ((uint32_t)trace_bytes[54] << 16) | ((uint32_t)trace_bytes[55] << 24)) == 1023);

  /* Symbols with no records report the TS sentinels. */
  qa_qvm_symbols symbols;
  qa_qvm_symbols_init(&symbols, NULL, 0, symbol_allocate, NULL, NULL, NULL);
  char text[64];
  qa_qvm_symbols_value_to_symbol(&symbols, 42, text, sizeof text);
  CHECK(strcmp(text, "NO SYMBOLS") == 0);
  CHECK(qa_qvm_symbols_symbol_to_value(&symbols, "main") == 0);
  CHECK(qa_qvm_symbols_get_count(&symbols) == 0);
  qa_qvm_symbols_destroy(&symbols);
}

static void test_mod_actors_validate(void) {
  qa_qvm_mod_callback_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  /* Neither callbacks nor combat: validates cleanly. */
  qa_qvm_validate_mod_actors(NULL, &declaration);

  qa_qvm_mod_actor_record record;
  memset(&record, 0, sizeof record);
  record.id = "entity";
  record.address = 0;
  record.stride = 64;
  record.capacity = 4;
  qa_qvm_mod_source_callbacks callbacks;
  memset(&callbacks, 0, sizeof callbacks);
  callbacks.has_touch = true;
  callbacks.touch = 8;
  qa_qvm_mod_source_actors source;
  memset(&source, 0, sizeof source);
  source.callbacks = &callbacks;
  declaration.actor_records = &record;
  declaration.actor_record_count = 1;
  declaration.entity_record = "entity";
  declaration.source_actors = &source;
  qa_qvm_validate_mod_actors(NULL, &declaration);

  /* Callback offset past the record stride aborts. */
  qa_qvm_mod_source_callbacks bad_callbacks = callbacks;
  bad_callbacks.touch = 64;
  qa_qvm_mod_source_actors bad_source = source;
  bad_source.callbacks = &bad_callbacks;
  qa_qvm_mod_callback_declaration bad_declaration = declaration;
  bad_declaration.source_actors = &bad_source;
  CHECK_ABORTS(qa_qvm_validate_mod_actors(NULL, &bad_declaration));

  /* Combat with a zero flag mask aborts (entry must first be OP_ENTER). */
  qa_qvmy_region_instruction instructions[1] = {{QA_QVM_OP_ENTER, 0, 0, 0}};
  qa_qvmy_artifact artifact;
  memset(&artifact, 0, sizeof artifact);
  artifact.instructions = instructions;
  artifact.instruction_count = 1;
  qa_qvm_mod_combat combat;
  memset(&combat, 0, sizeof combat);
  combat.abi = "q3-g-damage";
  combat.fields.entry = 0;
  combat.fields.health = 0;
  combat.fields.takedamage = 4;
  combat.fields.flags = 12;
  combat.fields.godmode = 0;
  combat.fields.no_knockback = 1;
  qa_qvm_mod_callback_declaration combat_declaration = declaration;
  combat_declaration.combat = &combat;
  CHECK_ABORTS(qa_qvm_validate_mod_actors(&artifact, &combat_declaration));
  /* A non-function damage entry aborts. */
  instructions[0].opcode = QA_QVM_OP_PUSH;
  combat.fields.godmode = 1;
  CHECK_ABORTS(qa_qvm_validate_mod_actors(&artifact, &combat_declaration));
}

static void test_reject_syscall(void) {
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.role = "qagame";
  call.code = 777;
  CHECK_ABORTS(qa_qvm_reject_syscall(&call));
}

int main(void) {
  test_operations();
  test_legacy_client_abi();
  test_legacy_presentation();
  test_math_and_memory_syscalls();
  test_objectives();
  test_records_and_symbols();
  test_mod_actors_validate();
  test_reject_syscall();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok: %d checks passed\n", checks);
  return 0;
}
