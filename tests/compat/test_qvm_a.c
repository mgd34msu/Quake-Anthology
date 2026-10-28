/* Port of key TS cases for compat/qvm first-half modules
 * (tests/compat/qvm/entity-tokens.test.ts and qvm.test.ts bytecode
 * builders, plus record layouts from the TS sources).
 * GPL-2.0-or-later. */
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

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/bot_navigation_records.h"
#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/entity_tokens.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/memory.h"

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

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  bytes[offset] = (uint8_t)bits;
  bytes[offset + 1] = (uint8_t)(bits >> 8);
  bytes[offset + 2] = (uint8_t)(bits >> 16);
  bytes[offset + 3] = (uint8_t)(bits >> 24);
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                  ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
  return (int32_t)bits;
}
static float get_f32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)get_i32(bytes, offset);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static void test_abi(void) {
  qa_qvm_game_import game;
  CHECK(qa_qvm_decode_game_import(0, &game) && game == QA_QVM_GAME_IMPORT_G_PRINT);
  CHECK(qa_qvm_decode_game_import(37, &game) && game == QA_QVM_GAME_IMPORT_G_GET_ENTITY_TOKEN);
  CHECK(!qa_qvm_decode_game_import(-1, &game));
  CHECK(!qa_qvm_decode_game_import(9999, &game));
  CHECK(!qa_qvm_decode_game_import(1.5, &game));
  qa_qvm_cgame_import cgame;
  CHECK(qa_qvm_decode_cgame_import(86, &cgame) &&
        cgame == QA_QVM_CGAME_IMPORT_CG_GET_ENTITY_TOKEN);
  CHECK(!qa_qvm_decode_cgame_import(9999, &cgame));
  qa_qvm_ui_import ui;
  CHECK(qa_qvm_decode_ui_import(0, &ui));
  CHECK(!qa_qvm_decode_ui_import(-3, &ui));
}

static void test_entity_tokens(void) {
  const char *text =
      "// component additions\n{ \"classname\" \"target_position\" }\n/* next */ { \"origin\" \"1 2 3\" }";
  qa_qvm_entity_tokens first, second;
  CHECK(qa_qvm_entity_tokens_init(&first, text, strlen(text)));
  CHECK(qa_qvm_entity_tokens_init(&second, text, strlen(text)));
  CHECK(strcmp(qa_qvm_entity_tokens_entity_token(&first).token, "{") == 0);
  CHECK(strcmp(qa_qvm_entity_tokens_entity_token(&first).token, "classname") == 0);
  qa_save_value *saved = qa_qvm_entity_tokens_capture(&first);
  CHECK(saved != NULL);
  qa_qvm_entity_tokens restored;
  CHECK(qa_qvm_entity_tokens_init(&restored, text, strlen(text)));
  qa_qvm_entity_tokens_restore(&restored, saved);
  qa_qvm_entity_token_result from_restored = qa_qvm_entity_tokens_entity_token(&restored);
  qa_qvm_entity_token_result from_first = qa_qvm_entity_tokens_entity_token(&first);
  CHECK(strcmp(from_restored.token, from_first.token) == 0);
  CHECK(from_restored.ended == from_first.ended);
  CHECK(strcmp(qa_qvm_entity_tokens_entity_token(&restored).token, "}") == 0);
  CHECK(strcmp(qa_qvm_entity_tokens_entity_token(&second).token, "{") == 0);
  qa_save_value_free(saved);
  qa_qvm_entity_tokens_destroy(&restored);
  qa_qvm_entity_tokens_destroy(&second);

  /* Mismatched source / missing state / oversized cursor abort. */
  qa_save_value *state = qa_qvm_entity_tokens_capture(&first);
  CHECK_ABORTS(qa_qvm_entity_tokens other; qa_qvm_entity_tokens_init(&other, "x ", 2);
               qa_qvm_entity_tokens_restore(&other, state));
  CHECK_ABORTS(qa_qvm_entity_tokens_restore(&first, NULL));
  qa_save_value_free(state);
  qa_qvm_entity_tokens_destroy(&first);

  qa_qvm_entity_tokens empty;
  CHECK(qa_qvm_entity_tokens_init(&empty, "", 0));
  qa_qvm_entity_tokens_restore(&empty, NULL);
  qa_qvm_entity_token_result end = qa_qvm_entity_tokens_entity_token(&empty);
  CHECK(strcmp(end.token, "") == 0 && end.ended);
  qa_qvm_entity_tokens_destroy(&empty);
}

static void test_entity_token_syscall(void) {
  uint8_t *bytes = calloc(4096, 1);
  qa_qvm_memory guest;
  qa_qvm_memory_init(&guest, bytes, 4096, NULL, NULL);
  qa_qvm_entity_tokens source;
  const char *text = "\"long token\" final";
  CHECK(qa_qvm_entity_tokens_init(&source, text, strlen(text)));
  uint8_t words[12] = {0};
  put_i32(words, 0, QA_QVM_GAME_IMPORT_G_GET_ENTITY_TOKEN);
  put_i32(words, 4, 128);
  put_i32(words, 8, 5);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.base.words = words;
  call.base.words_length = sizeof words;
  call.base.memory = bytes;
  call.base.memory_length = 4096;
  call.base.guest = &guest;
  call.guest = &guest;
  call.kind = "engine";
  call.role = "qagame";
  call.code = QA_QVM_GAME_IMPORT_G_GET_ENTITY_TOKEN;
  memset(bytes + 128, 0xcc, 32);
  qa_qvm_host_result out = -1;
  CHECK(qa_qvm_entity_token_syscall(&call, qa_qvm_entity_tokens_as_services(&source), &out));
  CHECK(out == 1);
  char *read = qa_qvm_memory_read_string(&guest, 128);
  CHECK(strcmp(read, "long") == 0);
  free(read);
  CHECK(bytes[133] == 0xcc);
  put_i32(words, 8, 32);
  CHECK(qa_qvm_entity_token_syscall(&call, qa_qvm_entity_tokens_as_services(&source), &out));
  CHECK(out == 1);
  read = qa_qvm_memory_read_string(&guest, 128);
  CHECK(strcmp(read, "final") == 0);
  free(read);
  qa_qvm_entity_tokens restored;
  CHECK(qa_qvm_entity_tokens_init(&restored, text, strlen(text)));
  qa_save_value *saved = qa_qvm_entity_tokens_capture(&source);
  qa_qvm_entity_tokens_restore(&restored, saved);
  qa_save_value_free(saved);
  CHECK(qa_qvm_entity_token_syscall(&call, qa_qvm_entity_tokens_as_services(&restored), &out));
  CHECK(out == 0);
  read = qa_qvm_memory_read_string(&guest, 128);
  CHECK(strcmp(read, "") == 0);
  free(read);
  /* Wrong role is not handled (TS null). */
  call.role = "cgame";
  CHECK(!qa_qvm_entity_token_syscall(&call, qa_qvm_entity_tokens_as_services(&restored), &out));
  qa_qvm_entity_tokens_destroy(&restored);
  qa_qvm_entity_tokens_destroy(&source);
  qa_qvm_memory_destroy(&guest);
  free(bytes);
}

static void test_entity_record(void) {
  uint8_t bytes[208] = {0};
  qa_qvmx_view view = {bytes, sizeof bytes};
  qa_qvm_entity_state state = qa_qvm_borrow_entity_state(view, NULL);
  qa_qvm_entity_state_set_number(&state, 5);
  qa_qvm_entity_state_set_e_type(&state, 3);
  CHECK(get_i32(bytes, 0) == 5);
  CHECK(get_i32(bytes, 4) == 3);
  qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&state);
  pos.delta.x = 1.5;
  pos.delta.y = -2.25;
  pos.delta.z = 3.0;
  qa_qvm_entity_state_set_pos(&state, pos);
  CHECK(get_f32(bytes, 36) == 1.5f);
  CHECK(get_f32(bytes, 40) == -2.25f);
  CHECK(get_f32(bytes, 44) == 3.0f);
  CHECK(qa_qvm_entity_state_get_number(&state) == 5);
  qa_qvm_entity_state_destroy(&state);
  CHECK(qa_qvm_entity_state_bytes_for(NULL) == 208);
  uint8_t small[100] = {0};
  qa_qvmx_view short_view = {small, sizeof small};
  CHECK_ABORTS(qa_qvm_entity_state bad = qa_qvm_borrow_entity_state(short_view, NULL);
               qa_qvm_entity_state_destroy(&bad));
}

static void test_bot_goal(void) {
  uint8_t bytes[56];
  memset(bytes, 0xcc, sizeof bytes);
  qa_qvmx_view view = {bytes, sizeof bytes};
  qa_qvmx_bot_goal goal = {{1, 2, 3}, 7, {-1, -2, -3}, {4, 5, 6}, 11, 12, 13, 14};
  qa_qvm_write_bot_goal(view, &goal, "full");
  CHECK(get_f32(bytes, 0) == 1.0f && get_f32(bytes, 8) == 3.0f);
  CHECK(get_i32(bytes, 12) == 7);
  CHECK(get_f32(bytes, 16) == -1.0f && get_f32(bytes, 28) == 4.0f);
  CHECK(get_i32(bytes, 40) == 11 && get_i32(bytes, 44) == 12);
  CHECK(get_i32(bytes, 48) == 13 && get_i32(bytes, 52) == 14);
  qa_qvmx_bot_goal back = qa_qvm_read_bot_goal(view);
  CHECK(back.origin.y == 2 && back.area == 7 && back.entity == 11 && back.item_info == 14);
  uint8_t small[55] = {0};
  qa_qvmx_view short_view = {small, sizeof small};
  CHECK_ABORTS(qa_qvm_write_bot_goal(short_view, &goal, "full"));
}

/* TS qvm.test.ts bytes() builder. */
typedef struct operation { qa_qvm_opcode opcode; bool has_operand; int32_t operand; } operation;
static size_t build_qvm(uint8_t *out, const operation *ops, size_t op_count,
                        const int32_t *words, size_t word_count) {
  uint8_t code[1024];
  size_t code_length = 0;
  for (size_t i = 0; i < op_count; i++) {
    code[code_length++] = (uint8_t)ops[i].opcode;
    if (ops[i].has_operand) {
      if (ops[i].opcode == QA_QVM_OP_ARG) code[code_length++] = (uint8_t)ops[i].operand;
      else { put_i32(code, code_length, ops[i].operand); code_length += 4; }
    }
  }
  int32_t header[8] = {0x12721444, (int32_t)op_count, 32, (int32_t)code_length,
                       32 + (int32_t)code_length, (int32_t)(word_count * 4), 0,
                       4096 - (int32_t)(word_count * 4)};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  memcpy(out + 32, code, code_length);
  for (size_t i = 0; i < word_count; i++) put_i32(out, 32 + code_length + i * 4, words[i]);
  return 32 + code_length + word_count * 4;
}

static bool trap_called = false;
static double reject_trap(void *ctx, qa_qvm_syscall *call) {
  (void)ctx;
  (void)call;
  trap_called = true;
  return 0;
}

static void test_interpreter(void) {
  /* qvm.test.ts "production modules retain compiled return control". */
  const operation ops[] = {
      {QA_QVM_OP_ENTER, true, 16}, {QA_QVM_OP_CONST, true, 6}, {QA_QVM_OP_CALL, false, 0},
      {QA_QVM_OP_CONST, true, 1},  {QA_QVM_OP_ADD, false, 0},  {QA_QVM_OP_LEAVE, true, 16},
      {QA_QVM_OP_ENTER, true, 16}, {QA_QVM_OP_LOCAL, true, 16}, {QA_QVM_OP_CONST, true, 0},
      {QA_QVM_OP_STORE4, false, 0}, {QA_QVM_OP_CONST, true, 240}, {QA_QVM_OP_BCOM, false, 0},
      {QA_QVM_OP_LEAVE, true, 16}};
  uint8_t source[512];
  size_t length = build_qvm(source, ops, sizeof ops / sizeof ops[0], NULL, 0);
  qa_qvm_image image;
  CHECK(qa_qvm_parse(source, length, "test.qvm", &image));
  CHECK(image.instruction_count == 13);
  qa_qvm_interpreter_system_call system_call = {NULL, reject_trap};
  /* QvmModule constructs its interpreter with "compiled" semantics. */
  qa_qvm_interpreter *interp =
      qa_qvm_interpreter_create(&image, system_call, NULL, NULL, "compiled", NULL);
  CHECK(interp != NULL);
  trap_called = false;
  /* TS module.call([]) pads to the ten public vmMain argument words. */
  const double arguments[10] = {0};
  CHECK(qa_qvm_interpreter_invoke(interp, arguments, 10, 0, NULL) == -240);
  size_t memory_length = 0;
  (void)qa_qvm_interpreter_get_memory(interp, &memory_length);
  CHECK(qa_qvm_interpreter_get_stack_pointer(interp) == (double)memory_length);
  CHECK(qa_qvm_interpreter_invoke(interp, arguments, 10, 0, NULL) == -240);
  CHECK(!trap_called);
  qa_qvm_interpreter_destroy(interp);
  qa_qvm_image_destroy(&image);

  /* Bad magic aborts like the TS QvmHeaderError. */
  uint8_t bad[64] = {0};
  CHECK_ABORTS(qa_qvm_image broken; (void)qa_qvm_parse(bad, sizeof bad, "bad.qvm", &broken));
}

int main(void) {
  test_abi();
  test_entity_tokens();
  test_entity_token_syscall();
  test_entity_record();
  test_bot_goal();
  test_interpreter();
  printf("test_qvm_a: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
