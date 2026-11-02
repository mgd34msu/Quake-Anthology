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
#include "qa/compat/qvm/regions.h"
#include "qa/compat/qvm/registry.h"
#include "qa/compat/qvm/script_record.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/raw_entities.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/compat/qvm/shared_entity_record.h"
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
  /* w8-qvmb-00: TS Number() trims whitespace and accepts radix forms. */
  const char *spaced[] = {"cs", " 21 "};
  out = qa_qvm_legacy_client_command(spaced, 2, "q3-1.16n-base", &count);
  CHECK(strcmp(out[1], "13") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *hex[] = {"cs", "0x15"};
  out = qa_qvm_legacy_client_command(hex, 2, "q3-1.16n-base", &count);
  CHECK(strcmp(out[1], "13") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *blank[] = {"cs", " "};
  out = qa_qvm_legacy_client_command(blank, 2, "q3-1.16n-base", &count);
  CHECK(strcmp(out[1], " ") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *huge[] = {"cs", "1e30"};
  out = qa_qvm_legacy_client_command(huge, 2, "q3-1.16n-base", &count);
  CHECK(strcmp(out[1], "1e30") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *signed_hex[] = {"cs", "+0x15"};
  CHECK_ABORTS((void)qa_qvm_legacy_client_command(signed_hex, 2, "q3-1.16n-base", &ignored));
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
  qa_qvm_instruction instructions[1] = {{.opcode = QA_QVM_OP_ENTER, .operand_width = 0, .operand = 0}};
  qa_resolved_qvm_artifact artifact;
  memset(&artifact, 0, sizeof artifact);
  artifact.image.instructions = instructions;
  artifact.image.instruction_count = 1;
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

static bool symbol_read_file(void *ctx, const char *path, qa_qvm_symbol_file *out) {
  (void)ctx;
  if (strcmp(path, "vm/qagame.map") != 0) return false;
  static const uint8_t text[] = "0 1a alpha\n0 2b beta\n0 3c gamma\n";
  out->terminated_bytes = text;
  out->terminated_length = sizeof text - 1;
  out->handle = NULL;
  return true;
}

static void symbol_free_file(void *ctx, qa_qvm_symbol_file *file) {
  (void)ctx;
  (void)file;
}

static void capture_print(void *ctx, const char *text) {
  char *buffer = ctx;
  strcat(buffer, text);
}

static void test_symbols_load_and_profile(void) {
  int32_t pointers[256];
  for (int i = 0; i < 256; i++) pointers[i] = i * 10;
  qa_qvm_symbols symbols;
  qa_qvm_symbols_init(&symbols, pointers, 256, symbol_allocate, NULL, NULL, NULL);
  char log[2048] = "";
  qa_qvm_symbol_files files = {NULL, symbol_read_file, symbol_free_file};
  qa_qvm_symbol_load_options options = {"qagame.qvm", 1, &files, capture_print, log};
  qa_qvm_symbols_load(&symbols, &options);
  CHECK(qa_qvm_symbols_get_count(&symbols) == 3);
  CHECK(strstr(log, "3 symbols parsed from vm/qagame.map\n") != NULL);
  /* Address 0x1a = 26 remaps through instructionPointers to 260. */
  CHECK(qa_qvm_symbols_symbol_to_value(&symbols, "alpha") == 260);
  CHECK(qa_qvm_symbols_symbol_to_value(&symbols, "beta") == 430);
  char text[64];
  qa_qvm_symbols_value_to_symbol(&symbols, 605, text, sizeof text);
  CHECK(strcmp(text, "gamma+5") == 0);
  qa_qvm_function_symbol alpha = qa_qvm_symbols_value_to_function_symbol(&symbols, 260);
  qa_qvm_function_symbol_set_profile_count(&alpha, 5);
  qa_qvm_function_symbol beta = qa_qvm_symbols_value_to_function_symbol(&symbols, 430);
  qa_qvm_function_symbol_set_profile_count(&beta, 5);
  /* Equal counts keep source order (TS sort is stable): alpha, beta. */
  char profile[2048] = "";
  qa_qvm_symbols_print_profile(&symbols, capture_print, profile, false);
  const char *alpha_at = strstr(profile, "alpha");
  const char *beta_at = strstr(profile, "beta");
  CHECK(alpha_at != NULL && beta_at != NULL && alpha_at < beta_at);
  CHECK(strstr(profile, "50%         5 alpha\n") != NULL);
  CHECK(strstr(profile, "          10 total\n") != NULL);
  /* Profile printing resets the counts. */
  CHECK(qa_qvm_function_symbol_get_profile_count(&alpha) == 0);
  qa_qvm_symbols_destroy(&symbols);
}

static void test_script_token(void) {
  uint8_t bytes[2048];
  memset(bytes, 0xcc, sizeof bytes);
  qa_qvm_memory_writes writes;
  qa_qvm_memory_writes_init(&writes, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_writable_view_init(&view, bytes, sizeof bytes, 0, (int64_t)sizeof bytes, &writes);
  /* Type 1 (string) strips the surrounding double quotes in place. */
  qa_ui_source_token_memory *token = qa_ui_source_token_memory_create_local();
  CHECK(token != NULL);
  qa_ui_source_token_write_string(token, "\"hello\"", 7);
  qa_ui_source_token_set_type(token, 1);
  qa_ui_source_token_set_subtype(token, 7);
  qa_ui_source_token_set_integer_value(token, 42);
  qa_ui_source_token_set_float_value(token, 1.5);
  qa_qvm_write_script_token_memory(&view, token);
  CHECK(memcmp(bytes + 16, "hello", 6) == 0);
  int32_t word = (int32_t)((uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8) |
                            ((uint32_t)bytes[6] << 16) | ((uint32_t)bytes[7] << 24));
  CHECK(word == 7);
  word = (int32_t)((uint32_t)bytes[8] | ((uint32_t)bytes[9] << 8) |
                   ((uint32_t)bytes[10] << 16) | ((uint32_t)bytes[11] << 24));
  CHECK(word == 42);
  /* An empty quoted string cannot be encoded. */
  qa_ui_source_token_write_string(token, "\"", 1);
  CHECK_ABORTS(qa_qvm_write_script_token_memory(&view, token));
  qa_ui_source_token_memory_destroy(token);
  /* The ScriptTokenRecord entry point maps the token kind. */
  qa_ui_script_location location = {"test", 1, 1};
  qa_ui_script_token *script_token = qa_ui_script_token_create(
      QA_UI_SCRIPT_KIND_NAME, "abc", 3, &location, "", 0, 0);
  CHECK(script_token != NULL);
  qa_ui_script_token_record record = {script_token, 7, 42, 1.5};
  memset(bytes, 0xcc, sizeof bytes);
  qa_qvm_write_script_token(&view, &record);
  word = (int32_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                   ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24));
  CHECK(word == 4);
  CHECK(memcmp(bytes + 16, "abc", 4) == 0);
  /* A subtype outside int32 aborts. */
  record.subtype = 2147483648LL;
  CHECK_ABORTS(qa_qvm_write_script_token(&view, &record));
  qa_ui_script_token_release(script_token);
  qa_qvm_memory_writes_destroy(&writes);
}

static void test_regions_and_registry(void) {
  /* ENTER 64; CONST 3; POP; CONST 4 (join target); <join>; LEAVE-ish */
  /* designated qa_qvm_instruction fields */
  qa_qvm_instruction instructions[6] = {
      {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 64},
      {.opcode = QA_QVM_OP_CONST, .operand_width = 4, .operand = 9},
      {.opcode = QA_QVM_OP_POP, .operand_width = 0, .operand = 0},
      {.opcode = QA_QVM_OP_CONST, .operand_width = 4, .operand = 1},
      {.opcode = QA_QVM_OP_POP, .operand_width = 0, .operand = 0},
      {.opcode = QA_QVM_OP_LEAVE, .operand_width = 4, .operand = 64},
  };
  CHECK(qa_qvm_qualify_region(instructions, 6, 0, 1, 5) == 64);
  /* Owner that is not ENTER aborts. */
  CHECK_ABORTS((void)qa_qvm_qualify_region(instructions, 6, 1, 2, 5));
  /* Join retaining a live operand aborts. */
  qa_qvm_instruction unbalanced[4] = {
      {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 64},
      {.opcode = QA_QVM_OP_CONST, .operand_width = 4, .operand = 1},
      {.opcode = QA_QVM_OP_CONST, .operand_width = 4, .operand = 2},
      {.opcode = QA_QVM_OP_LEAVE, .operand_width = 4, .operand = 64},
  };
  CHECK_ABORTS((void)qa_qvm_qualify_region(unbalanced, 4, 0, 1, 3));

  qa_qvm_vm_registry registry;
  qa_qvm_vm_registry_init(&registry, NULL, NULL, NULL, NULL);
  qa_qvm_vm_registration first = qa_qvm_vm_registry_reserve(&registry, "QAGame");
  qa_qvm_vm_registration again = qa_qvm_vm_registry_reserve(&registry, "qagame");
  CHECK(again.slot == first.slot && again.generation == first.generation);
  CHECK_ABORTS(qa_qvm_vm_registry_reserve(&registry, ""));
  (void)qa_qvm_vm_registry_reserve(&registry, "cgame");
  (void)qa_qvm_vm_registry_reserve(&registry, "ui");
  CHECK_ABORTS(qa_qvm_vm_registry_reserve(&registry, "extra"));
  qa_qvm_vm_registration_free(&first);
  qa_qvm_vm_binding binding = qa_qvm_vm_registration_get_binding(&first);
  CHECK(strcmp(binding.kind, "freed") == 0);
  qa_qvm_vm_registry_destroy(&registry);
}

static int observed_sequences[8];
static size_t observed_count = 0;

static void observe_publish(void *ctx, const qa_qvm_committed_write *event) {
  (void)ctx;
  if (observed_count < 8) observed_sequences[observed_count++] = (int)event->sequence;
}

static void test_memory_observe(void) {
  uint8_t bytes[1024];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_write_range range = {16, 8};
  observed_count = 0;
  size_t watch = qa_qvm_memory_observe_writes(&memory, &range, 1, observe_publish, NULL, NULL, NULL);
  qa_qvm_memory_fill_bytes(&memory, 20, 2, 0x5a);
  CHECK(observed_count == 1 && observed_sequences[0] == 1);
  CHECK(bytes[20] == 0x5a && bytes[21] == 0x5a);
  /* A write outside the watched range does not publish. */
  qa_qvm_memory_fill_bytes(&memory, 100, 2, 0x5a);
  CHECK(observed_count == 1);
  qa_qvm_memory_writes_unobserve(&memory.writes, watch);
  qa_qvm_memory_fill_bytes(&memory, 20, 2, 0x21);
  CHECK(observed_count == 1);
  qa_qvm_memory_destroy(&memory);
}

/* w8-qvmb-00: a publish callback may unsubscribe another watch; the
 * removed watch must be skipped (TS checks watch.active per delivery)
 * without disturbing the captures of the write in flight. */
typedef struct w8_watch_pair {
  qa_qvm_memory *memory;
  size_t other;
  int first_published;
  int second_published;
} w8_watch_pair;

static void w8_unsubscribing_publish(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  w8_watch_pair *pair = ctx;
  pair->first_published++;
  qa_qvm_memory_writes_unobserve(&pair->memory->writes, pair->other);
}

static void w8_second_publish(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  ((w8_watch_pair *)ctx)->second_published++;
}

static void test_memory_observe_unobserve_in_publish(void) {
  uint8_t bytes[1024];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_write_range range = {16, 8};
  w8_watch_pair pair = {&memory, 0, 0, 0};
  size_t first = qa_qvm_memory_observe_writes(&memory, &range, 1, w8_unsubscribing_publish, &pair, NULL, NULL);
  pair.other = qa_qvm_memory_observe_writes(&memory, &range, 1, w8_second_publish, &pair, NULL, NULL);
  /* The unsubscribing watch is delivered first and removes the
   * second, whose delivery for this same write must be skipped. */
  qa_qvm_memory_fill_bytes(&memory, 20, 2, 0x5a);
  CHECK(pair.first_published == 1 && pair.second_published == 0);
  qa_qvm_memory_fill_bytes(&memory, 20, 2, 0x33);
  CHECK(pair.first_published == 2 && pair.second_published == 0);
  qa_qvm_memory_writes_unobserve(&memory.writes, first);
  qa_qvm_memory_destroy(&memory);
  /* Overlapping same-allocation writeBytes copies as if snapshotted. */
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  memcpy(bytes + 64, "abcdefgh", 8);
  qa_qvm_memory_write_bytes(&memory, 66, bytes + 64, 6);
  CHECK(memcmp(bytes + 64, "ababcdef", 8) == 0);
  /* A wild relative offset aborts instead of overflowing the start. */
  CHECK_ABORTS((void)qa_qvm_memory_get_span(&memory, 64, 4, INT64_MAX));
  qa_qvm_memory_destroy(&memory);
}

static void test_player_state_roundtrip(void) {
  uint8_t bytes[1024];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory_writes writes;
  qa_qvm_memory_writes_init(&writes, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_writable_view_init(&view, bytes, sizeof bytes, 0, (int64_t)sizeof bytes, &writes);
  double slots[64];
  qa_q3_player_state state;
  memset(&state, 0, sizeof state);
  state.stats.values = slots;
  state.persistent.values = slots + 16;
  state.powerups.values = slots + 32;
  state.ammo.values = slots + 48;
  state.stats.count = state.persistent.count = state.powerups.count = state.ammo.count = 16;
  for (int i = 0; i < 64; i++) slots[i] = i;
  state.command_time_milliseconds = 1234;
  state.origin = (qa_vec3){1.5, -2.5, 3.25};
  state.ping_milliseconds = 42;
  qa_qvm_write_source_player_state(&view, &state, "q3-1.16n-base");
  double back_slots[64];
  qa_q3_player_state back;
  memset(&back, 0, sizeof back);
  back.stats.values = back_slots;
  back.persistent.values = back_slots + 16;
  back.powerups.values = back_slots + 32;
  back.ammo.values = back_slots + 48;
  qa_qvm_read_source_player_state(&view, "q3-1.16n-base", &back);
  CHECK(back.command_time_milliseconds == 1234);
  CHECK(back.origin.x == 1.5 && back.origin.y == -2.5 && back.origin.z == 3.25);
  /* Legacy layout writes ping at 440 and reports generic1 as zero. */
  CHECK(back.ping_milliseconds == 42 && back.generic1 == 0);
  CHECK(back_slots[16 + 7] == 23);
  /* w8-qvmb-00: DataView.setInt32 wraps out-of-range doubles. */
  state.command_time_milliseconds = 4294967297.0;
  qa_qvm_write_source_player_state(&view, &state, "q3-1.16n-base");
  qa_qvm_read_source_player_state(&view, "q3-1.16n-base", &back);
  CHECK(back.command_time_milliseconds == 1);
  qa_qvm_memory_writes_destroy(&writes);
}

/* --- w8-qvmb-01: G_ENTITIES_IN_BOX (TS server-game.test.ts) --- */
static size_t w8_area_entities(void *ctx, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity) {
  (void)ctx;
  (void)bounds;
  /* TS owner double: maximum 0 yields [], anything else [7, 9]. */
  if (maximum == 0) return 0;
  if (capacity > 0) out[0] = 7;
  if (capacity > 1) out[1] = 9;
  return 2;
}

static void test_server_game_entities_in_box(void) {
  uint8_t bytes[4096];
  memset(bytes, 0, sizeof bytes);
  put_f32(bytes, 128, 1.0f);
  put_f32(bytes, 140, 4.0f);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_server_spatial_operations spatial;
  memset(&spatial, 0, sizeof spatial);
  spatial.area_entities = w8_area_entities;
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
  int32_t out = -1;
  const int32_t maxima[] = {2, 2048, -1, INT32_MIN};
  for (size_t i = 0; i < sizeof maxima / sizeof maxima[0]; i++) {
    memset(words, 0, sizeof words);
    put_i32(words, 4, 128);
    put_i32(words, 8, 140);
    put_i32(words, 12, 512);
    put_i32(words, 16, maxima[i]);
    put_i32(bytes, 512, 0);
    put_i32(bytes, 516, 0);
    CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 2);
    int32_t first, second;
    memcpy(&first, bytes + 512, 4);
    memcpy(&second, bytes + 516, 4);
    CHECK(first == 7 && second == 9);
  }
  /* maximum 0: empty result, no output view is taken. */
  memset(words, 0, sizeof words);
  put_i32(words, 4, 128);
  put_i32(words, 8, 140);
  put_i32(words, 12, 0);
  put_i32(words, 16, 0);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  /* Owner returning more than a nonnegative maximum aborts. */
  uint8_t fork_words[32];
  memset(fork_words, 0, sizeof fork_words);
  put_i32(fork_words, 4, 128);
  put_i32(fork_words, 8, 140);
  put_i32(fork_words, 12, 512);
  put_i32(fork_words, 16, 1);
  qa_qvm_host_call fork_call = call;
  fork_call.base.words = fork_words;
  qa_qvm_memory forked;
  fork_call.guest = &forked;
  fork_call.base.guest = &forked;
  CHECK_ABORTS(qa_qvm_memory_init(&forked, bytes, sizeof bytes, NULL, NULL);
               (void)qa_qvm_server_game_syscall(&fork_call, &services, &out));
  /* Another role or kind is not this trap owner. */
  qa_qvm_host_call other = call;
  other.role = "cgame";
  CHECK(!qa_qvm_server_game_syscall(&other, &services, &out));
  qa_qvm_memory_destroy(&memory);
}
/* --- end w8-qvmb-01 --- */

/* --- undef-qvm-q3-items: qa_qvmx_view_* + Q3 item lifecycle --- */
#include "qa/compat/qvm/interpreter.h"
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/content/q3/team_arena/commands.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"

static void test_qvmx_views(void) {
  uint8_t bytes[8] = {0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0};
  qa_qvmx_view view = {bytes, sizeof bytes};
  CHECK(qa_qvmx_view_i32(&view, 0) == 0x12345678);
  CHECK(qa_qvmx_view_u16(&view, 0) == 0x5678);
  CHECK(qa_qvmx_view_u16(&view, 2) == 0x1234);
  CHECK(qa_qvmx_view_u8(&view, 3) == 0x12);
  qa_qvmx_view_set_f32(&view, 4, 1.5f);
  CHECK(qa_qvmx_view_f32(&view, 4) == 1.5f);
  CHECK(bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0xc0 && bytes[7] == 0x3f);
  qa_qvmx_view_set_i32(&view, 0, -2);
  CHECK(qa_qvmx_view_i32(&view, 0) == -2);
  CHECK(bytes[0] == 0xfe && bytes[1] == 0xff && bytes[2] == 0xff && bytes[3] == 0xff);
  qa_qvmx_view_set_u16(&view, 2, 0xabcd);
  CHECK(bytes[2] == 0xcd && bytes[3] == 0xab && qa_qvmx_view_u16(&view, 2) == 0xabcd);
  qa_qvmx_view_set_u8(&view, 7, 0x5a);
  CHECK(bytes[7] == 0x5a && qa_qvmx_view_u8(&view, 7) == 0x5a);
  qa_qvmx_view_check(&view, 8, 0); /* in bounds: offset == length, width 0 */
  CHECK_ABORTS((void)qa_qvmx_view_i32(&view, 5));
  CHECK_ABORTS((void)qa_qvmx_view_i32(&view, -1));
  CHECK_ABORTS((void)qa_qvmx_view_u16(&view, 7));
  CHECK_ABORTS((void)qa_qvmx_view_u8(&view, 8));
  CHECK_ABORTS(qa_qvmx_view_set_f32(&view, 6, 0.0f));
  CHECK_ABORTS(qa_qvmx_view_check(&view, 0, -1));
  qa_qvmx_view empty = {bytes, 0};
  CHECK_ABORTS((void)qa_qvmx_view_u8(&empty, 0));
}

static qa_q3_game_entity il_entities[160];
static qa_q3_game_client il_clients[64];
static bool il_active[160];
static qa_q3_entity_pool il_pool;
static struct {
  int links, logs, warns, sounds, uses, ranks;
  char log_text[128], warn_text[256];
  qa_q3_base_actor_trace_result trace_result;
} il_host;
static bool il_is_active(void *ctx) { return *(bool *)ctx; }
static qa_owned_actor il_actor(void *ctx) { (void)ctx; qa_owned_actor a; memset(&a, 0, sizeof a); return a; }
static qa_q3_game_entity *il_get(void *ctx, double slot) {
  (void)ctx; return slot >= 0 && slot < 160 ? &il_entities[(int)slot] : NULL;
}
static qa_q3_game_entity *il_activate(void *ctx, double slot) {
  (void)ctx; int i = (int)slot; qa_q3_game_entity_binding b; memset(&b, 0, sizeof b);
  b.ctx = &il_active[i]; b.active = il_is_active; b.actor = il_actor;
  qa_q3_game_entity_init(&il_entities[i], slot, b); il_active[i] = true; return &il_entities[i];
}
static qa_q3_game_client *il_client(void *ctx, double slot) { (void)ctx; return &il_clients[(int)slot]; }
static void il_release(void *ctx, qa_q3_game_entity *e) { (void)ctx; il_active[(int)e->slot] = false; }
static void il_noop_client(void *ctx, double slot) { (void)ctx; (void)slot; }
static double il_time(void *ctx) { (void)ctx; return 1000; }
static void il_print(void *ctx, const char *m) { (void)ctx; (void)m; }
static void il_link(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; il_host.links++; }
static void il_unlink(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; }
static void il_log(void *ctx, const char *m) {
  (void)ctx; il_host.logs++; snprintf(il_host.log_text, sizeof il_host.log_text, "%s", m);
}
static void il_warn(void *ctx, const char *m) {
  (void)ctx; il_host.warns++; snprintf(il_host.warn_text, sizeof il_host.warn_text, "%s", m);
}
static double il_sound(void *ctx, const char *path) { (void)ctx; (void)path; il_host.sounds++; return 7; }
static double il_rand(void *ctx) { (void)ctx; return 0; }
static double il_random(void *ctx) { (void)ctx; return 0.5; }
static const char *il_handicap(void *ctx, double n) { (void)ctx; (void)n; return "100"; }
static double il_team_pickup(void *ctx, qa_q3_game_entity *i, qa_q3_game_entity *p) {
  (void)ctx; (void)i; (void)p; return 9;
}
static void il_use_targets(void *ctx, qa_q3_game_entity *i, qa_q3_game_entity *a) {
  (void)ctx; (void)i; (void)a; il_host.uses++;
}
static qa_q3_base_server_trace_result il_trace(void *ctx, qa_q3_base_server_trace_query q) {
  (void)ctx; (void)q; qa_q3_base_server_trace_result r; memset(&r, 0, sizeof r); r.fraction = 1; return r;
}
static qa_q3_base_actor_trace_result il_trace_actor(void *ctx, qa_q3_base_actor_trace_query q) {
  (void)ctx; (void)q; return il_host.trace_result;
}
static void il_rank_sink(void *ctx, qa_q3_ranking_report report) {
  (void)ctx; (void)report; il_host.ranks++;
}
static bool il_warmup(void *ctx) { (void)ctx; return false; }

static qa_q3_game_item_definition_mirror il_items[29];
static qa_q3_item_lifecycle_context il_context;
static void il_setup(void) {
  static bool done; if (done) return; done = true;
  for (int i = 0; i < 64; i++) qa_q3_game_client_init(&il_clients[i], QA_Q3_GAME_PRODUCT_BASEQ3);
  qa_q3_entity_pool_options o; memset(&o, 0, sizeof o);
  o.records.get = il_get; o.records.activate = il_activate; o.records.client = il_client;
  o.records.release = il_release; o.records.deactivate_client = il_noop_client;
  o.product = QA_Q3_GAME_PRODUCT_BASEQ3; o.max_clients = 8; o.time = il_time; o.print = il_print;
  o.link = il_link; o.unlink = il_unlink;
  qa_q3_entity_pool_init(&il_pool, o);
  qa_q3_ranking_reports_attach(&il_pool.rankings, NULL, il_rank_sink, il_warmup);
  /* Mirror rows at the real shared/items indices (5=item_health, 28=item_quad). */
  il_items[5] = (qa_q3_game_item_definition_mirror){"item_health", "25 Health", 4, 0, 25, NULL};
  il_items[8] = (qa_q3_game_item_definition_mirror){"weapon_gauntlet", "Gauntlet", 1, 1, 0, NULL};
  il_items[10] = (qa_q3_game_item_definition_mirror){"weapon_machinegun", "Machinegun", 1, 2, 40, NULL};
  il_items[19] = (qa_q3_game_item_definition_mirror){"ammo_bullets", "Bullets", 2, 2, 50, NULL};
  il_items[28] = (qa_q3_game_item_definition_mirror){"item_quad", "Quad Damage", 5, 1, 30, NULL};
  qa_q3_item_pickup_set_item_list_mirror(QA_Q3_GAME_PRODUCT_BASEQ3, il_items, 29);
  memset(&il_context, 0, sizeof il_context);
  il_context.registry.product = QA_Q3_GAME_PRODUCT_BASEQ3;
  qa_q3_item_registry_init(&il_context.registry, il_items, 29);
  il_context.entities = &il_pool;
  il_context.product = QA_Q3_GAME_PRODUCT_BASEQ3;
  il_context.weapon_respawn_seconds = 5;
  il_context.team_weapon_respawn_seconds = 30;
  il_context.trace = il_trace; il_context.trace_actor = il_trace_actor;
  il_context.handicap_for_client = il_handicap; il_context.team_pickup = il_team_pickup;
  il_context.use_targets = il_use_targets; il_context.sound_index = il_sound;
  il_context.rand_value = il_rand; il_context.random = il_random;
  il_context.log = il_log; il_context.warn = il_warn;
}

static void test_item_lifecycle(void) {
  il_setup();
  qa_q3_game_entity *player = qa_q3_entity_pool_activate_client(&il_pool, 0);
  qa_q3_game_client *client = player->client;
  qa_q3_game_slots_set_mirror(&client->ps.stats, 6, 100); /* max health */
  client->pers.predict_item_pickup = true;

  qa_q3_game_entity *item = qa_q3_entity_pool_spawn(&il_pool);
  item->item = &il_items[5];
  item->spawnflags = 1; /* suspended: no drop trace */
  item->s.origin = (qa_vec3){4, 5, 6};
  int links_before = il_host.links;
  qa_q3_finish_spawning_item(item, il_context);
  CHECK(item->s.e_type == 2 && item->s.modelindex == 5 && item->s.modelindex2 == 0);
  CHECK(item->r.contents == (double)(int32_t)0x40000000);
  CHECK(item->r.mins.x == -15 && item->r.maxs.z == 15);
  CHECK(item->touch.fn != NULL && item->use.fn != NULL);
  CHECK(item->r.current_origin.x == 4 && item->r.current_origin.z == 6);
  CHECK(il_host.links == links_before + 1);

  qa_touch_contact contact; memset(&contact, 0, sizeof contact);
  qa_q3_damage_participant other = qa_q3_participant_entity(player);
  /* Full health: canItemBeGrabbed rejects, item stays live. */
  qa_q3_game_entity_set_health(player, 100);
  qa_q3_game_slots_set_mirror(&client->ps.stats, 0, 100);
  qa_q3_touch_item(item, other, contact, il_context);
  CHECK(item->r.contents == (double)(int32_t)0x40000000 && item->nextthink == 0);
  CHECK(qa_q3_game_entity_health(player) == 100 && il_host.logs == 0);

  /* Hurt player: pickup runs, item hides and schedules respawn in 35s. */
  qa_q3_game_entity_set_health(player, 50);
  qa_q3_game_slots_set_mirror(&client->ps.stats, 0, 50);
  qa_q3_touch_item(item, other, contact, il_context);
  CHECK(qa_q3_game_entity_health(player) == 75);
  CHECK(il_host.logs == 1 && strcmp(il_host.log_text, "Item: 0 item_health\n") == 0);
  CHECK(il_host.ranks > 0 && il_host.uses == 1);
  CHECK(item->r.contents == 0);
  CHECK(((int)item->s.e_flags & 0x80) != 0 && ((int)item->r.sv_flags & 1) != 0);
  CHECK(item->nextthink == 36000 && item->think.fn != NULL);

  /* Respawn restores visibility and clears the timer. */
  qa_q3_respawn_item(item, il_context);
  CHECK(item->r.contents == (double)(int32_t)0x40000000);
  CHECK(((int)item->s.e_flags & 0x80) == 0 && ((int)item->r.sv_flags & 1) == 0);
  CHECK(item->nextthink == 0);
  CHECK(((int)item->s.event & ~0x300) == 40); /* EV_ITEM_RESPAWN */

  /* Powerup spawn finish hides for 45s (crandom 0 at random()==0.5). */
  qa_q3_game_entity *quad = qa_q3_entity_pool_spawn(&il_pool);
  quad->item = &il_items[28];
  quad->spawnflags = 1;
  qa_q3_finish_spawning_item(quad, il_context);
  CHECK(quad->r.contents == 0 && ((int)quad->s.e_flags & 0x80) != 0);
  CHECK(quad->nextthink == 46000 && quad->think.fn != NULL);

  /* Startsolid drop trace warns and frees the item. */
  qa_q3_game_entity *stuck = qa_q3_entity_pool_spawn(&il_pool);
  stuck->item = &il_items[5];
  qa_q3_game_entity_set_classname(stuck, "item_health");
  il_host.trace_result.solidity = QA_Q3_BASE_SOLID_START_SOLID;
  qa_q3_finish_spawning_item(stuck, il_context);
  il_host.trace_result.solidity = QA_Q3_BASE_SOLID_CLEAR;
  CHECK(il_host.warns == 1);
  CHECK(strstr(il_host.warn_text, "FinishSpawningItem: item_health startsolid at") != NULL);
  CHECK(!qa_q3_game_entity_inuse(stuck));

  /* Error paths abort: itemless entity, product mismatch. */
  qa_q3_game_entity *bare = qa_q3_entity_pool_spawn(&il_pool);
  CHECK_ABORTS(qa_q3_respawn_item(bare, il_context));
  qa_q3_item_lifecycle_context mismatched = il_context;
  mismatched.product = QA_Q3_GAME_PRODUCT_MISSIONPACK;
  CHECK_ABORTS(qa_q3_respawn_item(item, mismatched));
  CHECK_ABORTS(qa_q3_touch_item(bare, other, contact, il_context));
}
/* --- q3-items-tail: registry, spawnItem, observeQ3Supply, TA give --- */
static char il_cs_value[64], il_cs_log[64];
static double il_cs_index;
static void il_set_cs(void *ctx, double index, const char *value) {
  (void)ctx; il_cs_index = index; snprintf(il_cs_value, sizeof il_cs_value, "%s", value);
}
static void il_cs_log_fn(void *ctx, const char *m) {
  (void)ctx; snprintf(il_cs_log, sizeof il_cs_log, "%s", m);
}
static bool il_enabled_flag;
static bool il_is_disabled(void *ctx) { (void)ctx; return !il_enabled_flag; }
static qa_q3_source_pickup_preview il_preview_selected(void *ctx, qa_q3_source_pickup_descriptor d) {
  (void)ctx; (void)d; qa_q3_source_pickup_preview p; memset(&p, 0, sizeof p);
  p.kind = QA_Q3_SOURCE_PREVIEW_SELECTED;
  p.offer.kind = QA_PICKUP_SUPPLY_AMMO; p.offer.grant.item = "q3:bullets"; p.offer.grant.amount = 50;
  p.preview.accepted = true; return p;
}
static qa_q3_source_pickup_preview il_preview_native(void *ctx, qa_q3_source_pickup_descriptor d) {
  (void)ctx; (void)d; qa_q3_source_pickup_preview p; memset(&p, 0, sizeof p);
  p.kind = QA_Q3_SOURCE_PREVIEW_NATIVE; return p;
}
static bool il_cvar_disabled;
static void il_send(void *ctx, double n, const char *t) { (void)ctx; (void)n; (void)t; }
static void il_set_cs27(void *ctx, double i, const char *t) { (void)ctx; (void)i; (void)t; }
static void il_append(void *ctx, const char *t) { (void)ctx; (void)t; }
static const char *il_get_cvar(void *ctx, const char *name) {
  (void)ctx; (void)name; return il_cvar_disabled ? "1" : "0";
}
static const char *il_get_userinfo(void *ctx, double n) { (void)ctx; (void)n; return ""; }
static void il_set_userinfo(void *ctx, double n, const char *t) { (void)ctx; (void)n; (void)t; }
/* q3-bootstrap-hosts: services for the constructed itemLifecycle host. */
static char *il_userinfo_owned(void *ctx, double n) {
  (void)ctx; (void)n;
  const char *text = "\\handicap\\85";
  char *copy = malloc(strlen(text) + 1);
  if (copy) memcpy(copy, text, strlen(text) + 1);
  return copy;
}
static double il_settings_integer(void *ctx, const char *name) {
  (void)ctx;
  if (strcmp(name, "g_weaponrespawn") == 0) return 5;
  if (strcmp(name, "g_weaponTeamRespawn") == 0) return 30;
  return 0;
}
static double il_debug_alloc(void *ctx) { (void)ctx; return 0; }

static void test_item_lifecycle_tail(void) {
  il_setup();
  qa_q3_item_registry *registry = &il_context.registry;
  /* Registry clear registers machinegun + gauntlet only (baseq3). */
  qa_q3_item_registry_clear(registry, 0);
  CHECK(qa_q3_item_registry_is_registered(registry, &il_items[10]));
  CHECK(qa_q3_item_registry_is_registered(registry, &il_items[8]));
  CHECK(!qa_q3_item_registry_is_registered(registry, &il_items[5]));
  qa_q3_item_registry_register(registry, &il_items[5]);
  CHECK(qa_q3_item_registry_is_registered(registry, &il_items[5]));
  static const qa_q3_game_item_definition_mirror foreign = {"item_x", "X", 4, 0, 1, NULL};
  CHECK_ABORTS(qa_q3_item_registry_register(registry, &foreign));
  /* Registry save publishes CS_ITEMS and logs the count. */
  double saved = qa_q3_item_registry_save(registry, NULL, il_set_cs, il_cs_log_fn);
  CHECK(saved == 3 && il_cs_index == 27 && strlen(il_cs_value) == 29);
  CHECK(il_cs_value[5] == '1' && il_cs_value[8] == '1' && il_cs_value[10] == '1' && il_cs_value[28] == '0');
  CHECK(strcmp(il_cs_log, "3 items registered\n") == 0);
  /* Save-state roundtrip into a fresh registry; bad tables abort. */
  qa_save_value *captured = qa_q3_item_registry_capture_save_state(registry);
  CHECK(captured != NULL);
  static qa_q3_item_registry restored;
  restored.product = QA_Q3_GAME_PRODUCT_BASEQ3;
  qa_q3_item_registry_init(&restored, il_items, 29);
  qa_q3_item_registry_restore_save_state(&restored, captured);
  CHECK(qa_q3_item_registry_is_registered(&restored, &il_items[5]));
  CHECK(!qa_q3_item_registry_is_registered(&restored, &il_items[28]));
  qa_save_value_free(captured);
  uint8_t short_table[2] = {1, 0};
  qa_save_value *bad_length = qa_save_value_bytes(short_table, 2);
  CHECK_ABORTS(qa_q3_item_registry_restore_save_state(&restored, bad_length));
  qa_save_value_free(bad_length);
  uint8_t bad_byte[29]; memset(bad_byte, 0, sizeof bad_byte); bad_byte[3] = 2;
  qa_save_value *bad_value = qa_save_value_bytes(bad_byte, 29);
  CHECK_ABORTS(qa_q3_item_registry_restore_save_state(&restored, bad_value));
  qa_save_value_free(bad_value);
  qa_q3_item_registry_destroy(&restored);
  /* Missionpack Harvester clear aborts when the cubes are missing. */
  static qa_q3_item_registry missionpack;
  missionpack.product = QA_Q3_GAME_PRODUCT_MISSIONPACK;
  qa_q3_item_registry_init(&missionpack, il_items, 29);
  CHECK_ABORTS(qa_q3_item_registry_clear(&missionpack, 7));
  qa_q3_item_registry_destroy(&missionpack);

  /* spawnItem reads keys, registers, schedules finish in two frames. */
  qa_q3_spawn_pair pairs[3] = {{"random", "1.5"}, {"wait", "7"}, {"noglobalsound", "3"}};
  qa_q3_spawn_variables vars; qa_q3_spawn_variables_init(&vars, pairs, 3);
  qa_q3_game_entity *spawned = qa_q3_entity_pool_spawn(&il_pool);
  int sounds_before = il_host.sounds;
  il_enabled_flag = true;
  qa_q3_spawn_item(spawned, &il_items[28], &vars, il_is_disabled, NULL, il_context);
  CHECK(spawned->item == &il_items[28] && spawned->random == 1.5 && spawned->wait == 7);
  CHECK(spawned->speed == 3 && il_host.sounds == sounds_before + 1);
  CHECK(spawned->nextthink == 1200 && spawned->think.fn != NULL && spawned->physics_bounce == 0.5);
  CHECK(qa_q3_item_registry_is_registered(registry, &il_items[28]));
  /* Disabled spawn registers but leaves the entity itemless. */
  qa_q3_game_entity *disabled = qa_q3_entity_pool_spawn(&il_pool);
  il_enabled_flag = false;
  qa_q3_spawn_item(disabled, &il_items[19], &vars, il_is_disabled, NULL, il_context);
  CHECK(disabled->item == NULL && qa_q3_item_registry_is_registered(registry, &il_items[19]));
  il_enabled_flag = true;
  qa_q3_spawn_variables_destroy(&vars);
  qa_q3_spawn_variables empty; qa_q3_spawn_variables_init(&empty, NULL, 0);
  qa_q3_game_entity *bad_spawn = qa_q3_entity_pool_spawn(&il_pool);
  CHECK_ABORTS(qa_q3_spawn_item(bad_spawn, &foreign, &empty, il_is_disabled, NULL, il_context));
  qa_q3_spawn_variables_destroy(&empty);

  /* observeQ3Supply: ready, respawning, and gate rejections. */
  qa_q3_game_entity *weapon = qa_q3_entity_pool_spawn(&il_pool);
  weapon->item = &il_items[10]; weapon->spawnflags = 1;
  qa_q3_finish_spawning_item(weapon, il_context);
  qa_q3_game_entity *recipient = qa_q3_entity_pool_activate_client(&il_pool, 2);
  qa_q3_game_entity_set_health(recipient, 50);
  qa_q3_item_lifecycle_context observed = il_context;
  observed.has_callbacks = true;
  observed.touch_callback = weapon->touch;
  static int respawn_anchor;
  observed.respawn_callback = (qa_q3_entity_callback){(void *)&respawn_anchor, NULL};
  observed.preview_pickup = il_preview_selected;
  qa_q3_supply_observation out;
  CHECK(qa_q3_observe_q3_supply(weapon, recipient, observed, &out));
  CHECK(strcmp(out.observation.availability_kind, "ready") == 0 && out.observation.eligible);
  CHECK(out.observation.offer.kind == QA_PICKUP_SUPPLY_AMMO && out.preview.accepted);
  weapon->r.contents = 0; weapon->think = observed.respawn_callback; weapon->nextthink = 45000;
  CHECK(qa_q3_observe_q3_supply(weapon, recipient, observed, &out));
  CHECK(strcmp(out.observation.availability_kind, "respawning") == 0 && out.observation.at_seconds == 45);
  observed.preview_pickup = il_preview_native;
  CHECK(!qa_q3_observe_q3_supply(weapon, recipient, observed, &out));
  observed.preview_pickup = il_preview_selected;
  observed.product = QA_Q3_GAME_PRODUCT_MISSIONPACK;
  CHECK(!qa_q3_observe_q3_supply(weapon, recipient, observed, &out));
  observed.product = QA_Q3_GAME_PRODUCT_BASEQ3;
  qa_q3_game_entity *health_item = qa_q3_entity_pool_spawn(&il_pool);
  health_item->item = &il_items[5];
  CHECK(!qa_q3_observe_q3_supply(health_item, recipient, observed, &out));

  /* TA give through the CONSTRUCTED hosts (q3-bootstrap-hosts):
   * qa_app_q3_item_lifecycle_host_init fills host.items (entities/
   * product/registry/host fns incl. preview_pickup), and
   * qa_app_q3_build_game_command_host assembles the TA command host. */
  static qa_app_q3_item_lifecycle_host built_items;
  qa_app_q3_item_lifecycle_services services; memset(&services, 0, sizeof services);
  services.trace = il_trace; services.trace_actor = il_trace_actor;
  services.team_pickup = il_team_pickup; services.use_targets = il_use_targets;
  services.sound_index = il_sound; services.rand_value = il_rand; services.random = il_random;
  services.log = il_log; services.warn = il_warn;
  services.get_userinfo = il_userinfo_owned;
  services.settings_integer = il_settings_integer;
  qa_app_q3_item_lifecycle_host_init(&built_items, &il_pool, QA_Q3_GAME_PRODUCT_BASEQ3,
      il_items, 29, services);
  qa_q3_item_lifecycle_context built = qa_app_q3_item_lifecycle_host_context(&built_items);
  CHECK(built.entities == &il_pool && built.product == QA_Q3_GAME_PRODUCT_BASEQ3);
  CHECK(built.registry.items == il_items && built.registry.count == 29);
  CHECK(built.game_type == 0 && built.weapon_respawn_seconds == 5
      && built.team_weapon_respawn_seconds == 30);
  CHECK(built.has_callbacks && built.touch_callback.fn != NULL && built.respawn_callback.fn != NULL);
  CHECK(strcmp(built.handicap_for_client(built.ctx, 0), "85") == 0);
  qa_q3_source_pickup_descriptor descriptor; memset(&descriptor, 0, sizeof descriptor);
  CHECK(built.preview_pickup(built.ctx, descriptor).kind == QA_Q3_SOURCE_PREVIEW_NATIVE);
  CHECK(built.admit_pickup(built.ctx, descriptor).kind == QA_Q3_SOURCE_PICKUP_NATIVE);

  /* TA give: real spawnItem+finish+touch through constructed host.items. */
  static qa_q3_ta_match_state give_state;
  static qa_q3_game_player_slots_mirror give_scores;
  static const qa_q3_ta_command_settings give_settings = {0, true, false, 0, false, false};
  memset(&give_state, 0, sizeof give_state);
  memset(&give_scores, 0, sizeof give_scores); give_scores.length = 4;
  qa_app_q3_game_command_host_parts parts; memset(&parts, 0, sizeof parts);
  parts.pool = &il_pool; parts.state = &give_state; parts.team_scores = &give_scores;
  parts.settings = &give_settings; parts.items = &built_items;
  parts.imports.send_server_command = il_send;
  parts.imports.set_configstring = il_set_cs27;
  parts.imports.append_console_command = il_append;
  parts.imports.get_cvar = il_get_cvar;
  parts.imports.get_userinfo = il_get_userinfo;
  parts.imports.set_userinfo = il_set_userinfo;
  parts.imports.log = il_print; parts.imports.print = il_print;
  qa_q3_ta_game_command_host give_host = qa_app_q3_build_game_command_host(parts);
  CHECK(give_host.spawn_item == NULL); /* TS GameCommandHost has no spawnItem */
  CHECK(give_host.items.entities == &il_pool && give_host.items.registry.items == il_items);
  qa_q3_ta_game_command_runtime give_runtime;
  qa_q3_ta_game_command_runtime_init(&give_runtime, give_host);
  qa_q3_game_entity *giver = qa_q3_entity_pool_activate_client(&il_pool, 3);
  giver->s.number = 3;
  qa_q3_game_client *give_client = giver->client;
  qa_q3_game_slots_set_mirror(&give_client->ps.stats, 6, 100);
  qa_q3_game_entity_set_health(giver, 50);
  qa_q3_game_slots_set_mirror(&give_client->ps.stats, 0, 50);
  il_cvar_disabled = false;
  const char *give_argv[3] = {"give", "25", "Health"};
  int logs_before = il_host.logs;
  qa_q3_ta_game_command_runtime_dispatch(&give_runtime, 3, give_argv, 3);
  CHECK(qa_q3_game_entity_health(giver) == 75);
  CHECK(il_host.logs == logs_before + 1 && strcmp(il_host.log_text, "Item: 3 item_health\n") == 0);
  /* Disabled give reaches finishSpawningItem itemless and aborts (TS throws). */
  il_cvar_disabled = true;
  CHECK_ABORTS(qa_q3_ta_game_command_runtime_dispatch(&give_runtime, 3, give_argv, 3));
  il_cvar_disabled = false;

  /* spawn.c hook: qa_app_q3_wire_spawn_item routes G_CallSpawn item
   * dispatch to qa_q3_spawn_item with the disable_<classname> closure. */
  qa_cvar_registry_options cvar_options; memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  qa_cvar_registry *spawn_cvars = qa_cvar_registry_create(&cvar_options);
  CHECK(spawn_cvars != NULL);
  qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(spawn_cvars, "disable_ammo_bullets", "1", true, &snapshot))
    qa_cvar_read_free(&snapshot);
  static qa_q3_game_memory spawn_memory;
  qa_q3_game_memory_init(&spawn_memory, NULL, il_debug_alloc, il_print);
  qa_q3_game_memory_initialize(&spawn_memory);
  qa_q3_spawn_context spawn_context; memset(&spawn_context, 0, sizeof spawn_context);
  spawn_context.pool = &il_pool; spawn_context.memory = &spawn_memory;
  spawn_context.product = QA_Q3_GAME_PRODUCT_BASEQ3; spawn_context.game_type = 0;
  spawn_context.items = il_items; spawn_context.item_count = 29;
  spawn_context.warn = il_warn;
  qa_app_q3_spawn_item_host spawn_host = {&built_items, spawn_cvars};
  qa_app_q3_wire_spawn_item(&spawn_context, &spawn_host);
  CHECK(spawn_context.spawn_item != NULL && spawn_context.ctx == (void *)&spawn_host);
  qa_q3_spawn_pair ammo_pairs[1] = {{"classname", "ammo_bullets"}};
  qa_q3_spawn_variables ammo_vars; qa_q3_spawn_variables_init(&ammo_vars, ammo_pairs, 1);
  qa_q3_spawn_outcome ammo_outcome = qa_q3_spawn_entity(&ammo_vars, &spawn_context);
  CHECK(ammo_outcome.kind == QA_Q3_SPAWN_DISPATCHED && ammo_outcome.route == QA_Q3_ROUTE_ITEM);
  CHECK(ammo_outcome.entity != NULL && ammo_outcome.entity->item == NULL); /* disabled */
  CHECK(qa_q3_item_registry_is_registered(&built_items.context.registry, &il_items[19]));
  qa_q3_spawn_variables_destroy(&ammo_vars);
  qa_q3_spawn_pair health_pairs[2] = {{"classname", "item_health"}, {"wait", "7"}};
  qa_q3_spawn_variables health_vars; qa_q3_spawn_variables_init(&health_vars, health_pairs, 2);
  qa_q3_spawn_outcome health_outcome = qa_q3_spawn_entity(&health_vars, &spawn_context);
  CHECK(health_outcome.kind == QA_Q3_SPAWN_DISPATCHED && health_outcome.route == QA_Q3_ROUTE_ITEM);
  CHECK(health_outcome.entity != NULL && health_outcome.entity->item == &il_items[5]);
  CHECK(health_outcome.entity->wait == 7 && health_outcome.entity->nextthink == 1200);
  qa_q3_spawn_variables_destroy(&health_vars);
  qa_cvar_registry_destroy(spawn_cvars);
  qa_app_q3_item_lifecycle_host_destroy(&built_items);
}
/* --- end undef-qvm-q3-items --- */

/* ----- mirrors-qvm-q3: retired mirror types use landed owners ----- */
static int32_t mirror_link_slot = -1;
static void mirror_link(void *ctx, int32_t slot) { (void)ctx; mirror_link_slot = slot; }
static void mirror_unlink(void *ctx, int32_t slot) { (void)ctx; mirror_link_slot = -100 - slot; }

static qa_guest_address_result mirror_guest_pointer(void *ctx, int64_t raw) {
  qa_guest_address_result result;
  memset(&result, 0, sizeof result);
  if (raw == 0) return result;
  result.has_value = true;
  result.value.address_space = ctx;
  result.value.byte_offset = raw & 16383;
  return result;
}
static qa_guest_address mirror_guest_offset(void *ctx, qa_guest_address address, int64_t displacement) {
  (void)ctx;
  address.byte_offset += displacement;
  return address;
}
static qa_byte_view mirror_guest_borrow(void *ctx, qa_guest_address address, double byte_length) {
  qa_byte_view view = {(const uint8_t *)ctx + address.byte_offset, (size_t)byte_length};
  return view;
}
static qa_actor_id_result mirror_current_actor(void *ctx, int64_t slot, int64_t byte_offset) {
  (void)ctx;
  (void)byte_offset;
  qa_actor_id_result result;
  memset(&result, 0, sizeof result);
  result.has_value = true;
  result.value.slot = slot;
  return result;
}

static char mirror_print_buffer[1024];
static void mirror_print(void *ctx, const char *text) {
  (void)ctx;
  strncat(mirror_print_buffer, text, sizeof mirror_print_buffer - strlen(mirror_print_buffer) - 1);
}
static double mirror_trap(void *ctx, qa_qvm_syscall *call) { (void)ctx; (void)call; return 0; }

typedef struct mirror_op { qa_qvm_opcode opcode; bool has_operand; int32_t operand; } mirror_op;
static size_t mirror_build_qvm(uint8_t *out, const mirror_op *ops, size_t op_count) {
  uint8_t code[256];
  size_t code_length = 0;
  for (size_t i = 0; i < op_count; i++) {
    code[code_length++] = (uint8_t)ops[i].opcode;
    if (ops[i].has_operand) { put_i32(code, code_length, ops[i].operand); code_length += 4; }
  }
  int32_t header[8] = {0x12721444, (int32_t)op_count, 32, (int32_t)code_length,
                       32 + (int32_t)code_length, 0, 0, 4096};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  memcpy(out + 32, code, code_length);
  return 32 + code_length;
}

static void test_mirror_retirement(void) {
  /* Shared entity: nested s is the landed qa_qvm_entity_state. */
  static uint8_t bytes[16384];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(&memory, 0, 516, &view);
  qa_qvm_shared_entity entity;
  qa_qvm_borrow_shared_entity(&view, NULL, &entity);
  qa_qvm_entity_collision_model inline_model = {"inline", 42};
  qa_qvm_shared_entity_set_model(&entity, inline_model);
  qa_qvm_entity_collision_model back = qa_qvm_shared_entity_get_model(&entity);
  CHECK(strcmp(back.kind, "inline") == 0 && back.index == 42);
  CHECK(qa_qvm_entity_state_get_modelindex(&entity.s) == 42);
  int32_t word160;
  memcpy(&word160, bytes + 160, 4);
  CHECK(word160 == 42);
  qa_qvm_entity_state_set_modelindex(&entity.s, 7);
  CHECK(qa_qvm_shared_entity_get_model(&entity).index == 7);
  qa_qvm_entity_collision_model capsule = {"capsule", 0};
  qa_qvm_shared_entity_set_model(&entity, capsule);
  CHECK(strcmp(qa_qvm_shared_entity_get_model(&entity).kind, "capsule") == 0);

  /* Game data: raw table and server-game slots over qa_qvm_game_data. */
  qa_qvm_game_data data;
  qa_qvm_game_data_init(&data, &memory, NULL);
  qa_qvm_game_data_locate(&data, 1024, 4, 516, 8192, 1024);
  CHECK(qa_qvm_game_data_number_from_pointer(&data, 1024 + 2 * 516) == 2);
  CHECK_ABORTS((void)qa_qvm_game_data_number_from_pointer(&data, 1024 + 2 * 516 + 4));

  qa_qvm_server_spatial_operations spatial;
  memset(&spatial, 0, sizeof spatial);
  spatial.link = mirror_link;
  spatial.unlink = mirror_unlink;
  qa_qvm_server_game_services services;
  memset(&services, 0, sizeof services);
  services.data = &data;
  services.spatial = &spatial;
  uint8_t words[16];
  memset(words, 0, sizeof words);
  put_i32(words, 4, 1024 + 3 * 516);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = "qagame";
  call.code = QA_QVM_GAME_IMPORT_G_LINKENTITY;
  call.guest = &memory;
  call.base.words = words;
  call.base.words_length = sizeof words;
  call.base.guest = &memory;
  int32_t out = -1;
  mirror_link_slot = -1;
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  CHECK(mirror_link_slot == 3);

  qa_guest_memory guest;
  memset(&guest, 0, sizeof guest);
  guest.ctx = bytes;
  guest.address_space = bytes;
  guest.pointer_bytes = 4;
  guest.pointer = mirror_guest_pointer;
  guest.offset = mirror_guest_offset;
  guest.borrow = mirror_guest_borrow;
  qa_raw_entity_table table;
  qa_qvm_raw_entity_table_context context;
  qa_qvm_raw_entity_table_init(&table, &context, &data, &memory, &guest, 4, mirror_current_actor, NULL);
  CHECK(table.count == 4 && table.stride_bytes == 516 && table.base.byte_offset == 1024);
  qa_raw_entity_view at = table.at_slot(table.ctx, 2);
  CHECK(at.slot == 2 && at.address.byte_offset == 1024 + 2 * 516);
  CHECK(at.bytes.bytes == bytes + 1024 + 2 * 516 && at.bytes.length == 516);
  qa_actor_id_result actor = at.current_actor(at.ctx);
  CHECK(actor.has_value && actor.value.slot == 2);
  free(at.ctx);
  qa_guest_address pointer = {bytes, 1024 + 516};
  qa_raw_entity_view from = table.from_pointer(table.ctx, pointer);
  CHECK(from.slot == 1);
  free(from.ctx);
  CHECK_ABORTS((void)table.at_slot(table.ctx, 4));

  /* Registry: interpreted binding holds the landed interpreter. */
  const mirror_op ops[] = {{QA_QVM_OP_ENTER, true, 16}, {QA_QVM_OP_CONST, true, 7}, {QA_QVM_OP_LEAVE, true, 16}};
  uint8_t source[512];
  size_t length = mirror_build_qvm(source, ops, sizeof ops / sizeof ops[0]);
  qa_qvm_image image;
  CHECK(qa_qvm_parse(source, length, "mirror.qvm", &image));
  qa_qvm_vm_registry registry;
  qa_qvm_vm_registry_init(&registry, mirror_print, NULL, NULL, NULL);
  qa_qvm_vm_registration registration = qa_qvm_vm_registry_reserve(&registry, "qagame");
  qa_qvm_interpreter_system_call system_call = {NULL, mirror_trap};
  qa_qvm_interpreter *interp = qa_qvm_interpreter_create(&image, system_call, NULL, &registration, NULL, NULL);
  CHECK(interp != NULL);
  qa_qvm_vm_binding binding = qa_qvm_vm_registration_get_binding(&registration);
  CHECK(strcmp(binding.kind, "interpreted") == 0 && binding.interpreter == interp);
  mirror_print_buffer[0] = '\0';
  qa_qvm_vm_registry_print_info(&registry, mirror_print, NULL);
  char needle[64];
  snprintf(needle, sizeof needle, "code length : %7lld",
           (long long)qa_qvm_interpreter_get_code_length(interp));
  CHECK(strstr(mirror_print_buffer, needle) != NULL);
  size_t memory_length = 0;
  (void)qa_qvm_interpreter_get_memory(interp, &memory_length);
  snprintf(needle, sizeof needle, "data length : %7lld", (long long)memory_length);
  CHECK(strstr(mirror_print_buffer, needle) != NULL);
  snprintf(needle, sizeof needle, "table length: %7lld",
           (long long)qa_qvm_interpreter_get_instruction_pointers_length(interp));
  CHECK(strstr(mirror_print_buffer, needle) != NULL);
  qa_qvm_interpreter_destroy(interp);
  qa_qvm_image_destroy(&image);
  qa_qvm_vm_registry_destroy(&registry);
  qa_qvm_memory_destroy(&memory);
}

/* qvmy-retire-2: switched APIs over landed owner types. */
#include "qa/compat/qvm/mod_presentation_checkpoint.h"

static const char *retire2_userinfo(void *ctx, double slot) {
  (void)ctx;
  return slot == 1 ? "name\\test" : "name\\other";
}

static void test_qvmy_retire_2(void) {
  /* Landed qa_qvm_instruction feeds region qualification directly. */
  const qa_qvm_instruction instructions[] = {
      {QA_QVM_OP_ENTER, 0, 4, 16},
      {QA_QVM_OP_CONST, 5, 4, 5},
      {.opcode = QA_QVM_OP_POP, .operand_width = 10, .operand = 0},
      {QA_QVM_OP_LEAVE, 11, 4, 16},
  };
  CHECK(qa_qvm_qualify_region(instructions, 4, 0, 1, 3) == 16);
  CHECK_ABORTS((void)qa_qvm_qualify_region(instructions, 4, 0, 0, 3));

  /* Landed snapshot/entity types through presentation capture. */
  qa_qvm_source_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.number = 9;
  snapshot.server_time = 1234;
  snapshot.flags = 5;
  uint8_t *mask = malloc(32);
  CHECK(mask != NULL);
  memset(mask, 0xa5, 32);
  snapshot.area_mask = mask;
  snapshot.area_mask_length = 32;
  double *slots = calloc(64, sizeof *slots);
  CHECK(slots != NULL);
  snapshot.player_state.stats.values = slots;
  snapshot.player_state.stats.count = 16;
  snapshot.player_state.persistent.values = slots + 16;
  snapshot.player_state.persistent.count = 16;
  snapshot.player_state.powerups.values = slots + 32;
  snapshot.player_state.powerups.count = 16;
  snapshot.player_state.ammo.values = slots + 48;
  snapshot.player_state.ammo.count = 16;
  snapshot.player_state.ping_milliseconds = 42;
  qa_qvm_entity_state_fields *entities = calloc(1, sizeof *entities);
  CHECK(entities != NULL);
  entities[0].number = 77;
  entities[0].e_type = 3;
  entities[0].origin = (qa_vec3){1, 2, 3};
  snapshot.entities = entities;
  snapshot.entity_count = 1;
  snapshot.server_command_sequence = 66;
  qa_qvm_presentation_snapshot captured;
  memset(&captured, 0, sizeof captured);
  qa_qvm_capture_presentation_snapshot(&snapshot, NULL, &captured);
  CHECK(captured.number == 9);
  CHECK(captured.byte_length == (size_t)qa_qvm_snapshot_bytes_for(NULL));
  int32_t word = 0;
  memcpy(&word, captured.bytes + 4, 4);
  CHECK(word == 42); /* ping */
  memcpy(&word, captured.bytes + 8, 4);
  CHECK(word == 1234); /* serverTime */
  CHECK(captured.bytes[12] == 0xa5);
  const size_t entity_offset = 48 + 468; /* ps bytes (modern) */
  memcpy(&word, captured.bytes + entity_offset - 4, 4);
  CHECK(word == 1); /* numEntities */
  memcpy(&word, captured.bytes + entity_offset, 4);
  CHECK(word == 77); /* entity number round-trips through owner writer */
  memcpy(&word, captured.bytes + captured.byte_length - 4, 4);
  CHECK(word == 66);
  qa_qvm_presentation_snapshot_free(&captured);
  qa_qvm_source_snapshot_free(&snapshot);
  CHECK(snapshot.entities == NULL && snapshot.area_mask == NULL);

  /* Server-game dispatches client-game and entity-token traps to the
   * landed syscalls directly. */
  static uint8_t bytes[16384];
  memset(bytes, 0, sizeof bytes);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_game_data data;
  qa_qvm_game_data_init(&data, &memory, NULL);
  qa_qvm_entity_tokens tokens;
  CHECK(qa_qvm_entity_tokens_init(&tokens, "hello world", strlen("hello world")));
  qa_qvm_server_game_services services;
  memset(&services, 0, sizeof services);
  services.data = &data;
  services.max_clients = 4;
  services.get_userinfo = retire2_userinfo;
  services.entity_tokens = qa_qvm_entity_tokens_as_services(&tokens);
  uint8_t words[32];
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = "qagame";
  call.guest = &memory;
  call.base.words = words;
  call.base.words_length = sizeof words;
  call.base.guest = &memory;
  int32_t out = -1;
  memset(words, 0, sizeof words);
  put_i32(words, 4, 1);    /* slot */
  put_i32(words, 8, 2048); /* buffer */
  put_i32(words, 12, 64);  /* buffer size */
  call.code = QA_QVM_GAME_IMPORT_G_GET_USERINFO;
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  CHECK(strcmp((const char *)bytes + 2048, "name\\test") == 0);
  memset(words, 0, sizeof words);
  put_i32(words, 4, 3072); /* buffer */
  put_i32(words, 8, 32);   /* buffer size */
  call.code = QA_QVM_GAME_IMPORT_G_GET_ENTITY_TOKEN;
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 1);
  CHECK(strcmp((const char *)bytes + 3072, "hello") == 0);
  qa_qvm_entity_tokens_destroy(&tokens);
  qa_qvm_memory_destroy(&memory);
}
/* ----- end mirrors-qvm-q3 ----- */

/* qvmy-retire-3: switched APIs over landed owner types. */
#include "qa/compat/qvm/primary_inventory_profile.h"
#include "qa/compat/qvm/primary_pickup_profile.h"

typedef struct retire3_call_state {
  int branch_calls;
  size_t branch_count;
  int64_t first_index;
  bool first_decision;
  int32_t proceed_result;
  int proceed_calls;
  int proceed_async_calls;
} retire3_call_state;

static void retire3_branches(void *ctx, const qa_qvmy_branch_binding *bindings, size_t count) {
  retire3_call_state *state = ctx;
  state->branch_calls++;
  state->branch_count = count;
  if (count != 0) {
    state->first_index = bindings[0].instruction_index;
    state->first_decision = bindings[0].decide(bindings[0].ctx, false, NULL, NULL);
  }
}

static int32_t retire3_proceed(void *ctx) {
  retire3_call_state *state = ctx;
  state->proceed_calls++;
  return state->proceed_result;
}

static int32_t retire3_proceed_async(void *ctx) {
  retire3_call_state *state = ctx;
  state->proceed_async_calls++;
  return state->proceed_result;
}

static void test_qvmy_retire_3(void) {
  /* Owner inventory union: constant capacity closure. */
  qa_qvm_inventory_profile inventory;
  qa_module_identity module = {"q3:test:game", "vm/qagame.qvm", "sha256:test", "1"};
  qa_qvm_primary_inventory_profile_constant(module, "q3-modern", 8, 64, 200, &inventory);
  CHECK(inventory.private_profile == NULL);
  CHECK(inventory.public_profile != NULL);
  CHECK(inventory.public_profile->weapons_offset == 8);
  CHECK(inventory.public_profile->ammo_offset == 64);
  qa_qvm_capacity_context context;
  memset(&context, 0, sizeof context);
  double capacity = inventory.public_profile->capacity(inventory.public_profile->ctx, NULL, 3, context);
  CHECK(capacity == 200);
  qa_qvm_primary_inventory_profile_free(&inventory);
  CHECK(inventory.public_profile == NULL && inventory.private_profile == NULL);

  /* Owner pickup grant eligible over qa_qvm_pickup_eligibility:
   * binds the captured branches, then proceeds (sync and async). */
  qa_qvm_pickup_branch branches[] = {{42, true}, {43, false}};
  qa_qvm_pickup_branches captured = {branches, 2};
  retire3_call_state state;
  memset(&state, 0, sizeof state);
  state.proceed_result = 7;
  qa_qvmy_function_call call;
  memset(&call, 0, sizeof call);
  call.execution = "synchronous";
  call.call_ctx = &state;
  call.branches = retire3_branches;
  call.proceed = retire3_proceed;
  call.proceed_async = retire3_proceed_async;
  qa_qvm_pickup_eligibility eligibility;
  memset(&eligibility, 0, sizeof eligibility);
  eligibility.call = &call;
  CHECK(qa_qvm_primary_pickup_grant_eligible(&captured, eligibility));
  CHECK(state.branch_calls == 1 && state.branch_count == 2);
  CHECK(state.first_index == 42 && state.first_decision);
  CHECK(state.proceed_calls == 1 && state.proceed_async_calls == 0);
  state.proceed_result = 0;
  call.execution = "asynchronous";
  CHECK(!qa_qvm_primary_pickup_grant_eligible(&captured, eligibility));
  CHECK(state.proceed_calls == 1 && state.proceed_async_calls == 1);
  /* NULL ctx is the no-override closure (builtin grants). */
  state.proceed_result = 1;
  call.execution = "synchronous";
  CHECK(qa_qvm_primary_pickup_grant_eligible(NULL, eligibility));
  CHECK(state.branch_count == 0);
}
/* ----- end qvmy-retire-3 ----- */

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
  test_symbols_load_and_profile();
  test_script_token();
  test_regions_and_registry();
  test_memory_observe();
  test_memory_observe_unobserve_in_publish(); /* w8-qvmb-00 */
  test_player_state_roundtrip();
  test_server_game_entities_in_box(); /* w8-qvmb-01 */
  test_qvmx_views(); /* undef-qvm-q3-items */
  test_item_lifecycle(); /* undef-qvm-q3-items */
  test_item_lifecycle_tail(); /* q3-items-tail */
  test_reject_syscall();
  test_mirror_retirement(); /* mirrors-qvm-q3 */
  test_qvmy_retire_2(); /* qvmy-retire-2 */
  test_qvmy_retire_3(); /* qvmy-retire-3 */
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok: %d checks passed\n", checks);
  return 0;
}
