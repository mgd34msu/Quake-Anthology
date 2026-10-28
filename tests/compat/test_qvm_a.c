/* Port of key TS cases for compat/qvm first-half modules
 * (tests/compat/qvm/entity-tokens.test.ts and qvm.test.ts bytecode
 * builders, plus record layouts from the TS sources).
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
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
#include "qa/compat/qvm/bot_navigation_syscalls.h"
#include "qa/compat/qvm/client_browser_syscalls.h"
#include "qa/compat/qvm/client_script_syscalls.h"
#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/entity_tokens.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/memory.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
/* --- w6c-qvm-deep-01 additions --- */
#include "qa/compat/qvm/game_combat.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/legacy_bot_abi.h"

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
  /* TS switch(word) matches nothing for NaN, infinities, huge or
   * fractional words, and the enum gaps (46..199 game, 90..99 cgame/ui). */
  CHECK(!qa_qvm_decode_game_import(NAN, &game));
  CHECK(!qa_qvm_decode_game_import(INFINITY, &game));
  CHECK(!qa_qvm_decode_game_import(-INFINITY, &game));
  CHECK(!qa_qvm_decode_game_import(1e300, &game));
  CHECK(!qa_qvm_decode_game_import(-0.5, &game));
  CHECK(!qa_qvm_decode_game_import(45.9, &game));
  CHECK(!qa_qvm_decode_game_import(46, &game));
  CHECK(qa_qvm_decode_game_import(581, &game) &&
        game == QA_QVM_GAME_IMPORT_BOTLIB_PC_SOURCE_FILE_AND_LINE);
  CHECK(!qa_qvm_decode_game_import(582, &game));
  CHECK(!qa_qvm_decode_cgame_import(NAN, &cgame));
  CHECK(!qa_qvm_decode_cgame_import(90, &cgame));
  CHECK(qa_qvm_decode_cgame_import(111, &cgame) && cgame == QA_QVM_CGAME_IMPORT_CG_ACOS);
  CHECK(!qa_qvm_decode_ui_import(88, &ui));
  CHECK(!qa_qvm_decode_ui_import(-1e300, &ui));
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

/* --- w6c-qvm-deep-01: legacy-bot-abi, entity-record legacy profile,
 * game-data bounds, game-combat validators, image branch target --- */
static void test_w6c_qvm_deep_01(void) {
  qa_qvm_game_import decoded;
  CHECK(qa_qvm_decode_legacy_game_import(200, &decoded) &&
        decoded == QA_QVM_GAME_IMPORT_BOTLIB_SETUP);
  CHECK(qa_qvm_decode_legacy_game_import(572, &decoded) &&
        decoded == QA_QVM_GAME_IMPORT_BOTLIB_AI_PREDICT_VISIBLE_POSITION);
  CHECK(qa_qvm_decode_legacy_game_import(0, &decoded) &&
        decoded == QA_QVM_GAME_IMPORT_G_PRINT);
  CHECK(!qa_qvm_decode_legacy_game_import(41, &decoded));
  CHECK(!qa_qvm_decode_legacy_game_import(212, &decoded));
  CHECK(!qa_qvm_decode_legacy_game_import(402, &decoded));
  CHECK(!qa_qvm_decode_legacy_game_import(200.5, &decoded));
  CHECK(!qa_qvm_decode_legacy_game_import(NAN, &decoded));
  CHECK(!qa_qvm_decode_legacy_game_import(1e20, &decoded));
  CHECK(!qa_qvm_decode_legacy_game_import(-200, &decoded));

  CHECK(qa_qvm_entity_state_bytes_for("q3-1.16n-base") == 204);
  uint8_t legacy_bytes[204] = {0};
  qa_qvmx_view legacy_view = {legacy_bytes, sizeof legacy_bytes};
  qa_qvm_entity_state legacy = qa_qvm_borrow_entity_state(legacy_view, "q3-1.16n-base");
  qa_qvm_entity_state_set_generic1(&legacy, 0);
  CHECK(qa_qvm_entity_state_get_generic1(&legacy) == 0);
  qa_qvm_entity_state_destroy(&legacy);
  CHECK_ABORTS(qa_qvm_entity_state bad =
                   qa_qvm_borrow_entity_state(legacy_view, "q3-1.16n-base");
               qa_qvm_entity_state_set_generic1(&bad, 1));

  qa_qvm_game_data data;
  qa_qvm_game_data_init(&data, NULL, NULL);
  CHECK(qa_qvm_game_data_get_num_clients(&data) == 64);
  qa_qvm_game_data_set_client_count(&data, 1);
  CHECK(qa_qvm_game_data_get_num_clients(&data) == 1);
  CHECK_ABORTS(qa_qvm_game_data_set_client_count(&data, 0));
  CHECK_ABORTS(qa_qvm_game_data_set_client_count(&data, 65));
  CHECK_ABORTS(qa_qvm_game_data_set_client_count(&data, 1.5));

  const double positions[] = {0, 2, 1};
  qa_qvm_validate_combat_positions(positions, 3, 3);
  const double duplicate[] = {0, 0};
  CHECK_ABORTS(qa_qvm_validate_combat_positions(duplicate, 2, 2));
  CHECK_ABORTS(qa_qvm_validate_combat_positions(positions, 3, 63));
  CHECK_ABORTS(qa_qvm_validate_combat_positions(positions, 3, 2));
  qa_qvm_damage_flags masks = {1, 2, 4, 8, 16};
  CHECK(qa_qvm_canonical_damage_flags(masks, 1 | 4 | 16) == (1 | 4 | 16));
  CHECK(qa_qvm_canonical_damage_flags(masks, 0) == 0);
  qa_qvm_damage_flags shifted = {16, 8, 4, 2, 1};
  CHECK(qa_qvm_canonical_damage_flags(shifted, 16) == 1);
  CHECK(qa_qvm_canonical_damage_flags(shifted, 1) == 16);

  const operation branch_ops[] = {{QA_QVM_OP_ENTER, true, 8},
                                  {QA_QVM_OP_CONST, true, 1},
                                  {QA_QVM_OP_EQ, true, 99},
                                  {QA_QVM_OP_LEAVE, true, 8}};
  uint8_t branch_source[256];
  size_t branch_length =
      build_qvm(branch_source, branch_ops, sizeof branch_ops / sizeof branch_ops[0], NULL, 0);
  CHECK_ABORTS(qa_qvm_image broken;
               (void)qa_qvm_parse(branch_source, branch_length, "branch.qvm", &broken));
}

/* TS client-state.test.ts "updating a legacy borrowed command
 * preserves private buttons and padding". */
static void test_user_command_legacy(void) {
  uint8_t bytes[24];
  memset(bytes, 0x6b, sizeof bytes);
  qa_qvmx_view view = {bytes, sizeof bytes};
  qa_qvmx_wire_user_command command;
  memset(&command, 0, sizeof command);
  command.server_time = 300;
  command.angles[0] = 21;
  command.angles[1] = -34;
  command.angles[2] = 55;
  command.buttons = 2053;
  command.weapon = 7;
  command.forwardmove = -90;
  command.rightmove = 31;
  command.upmove = 22;
  qa_qvm_write_user_command(view, &command, "q3-1.16n-base", NULL);
  CHECK(bytes[4] == 133);
  bytes[4] = 229;
  qa_qvmx_wire_user_command original = qa_qvm_read_user_command(view, "q3-1.16n-base");
  original.buttons = (double)((int32_t)original.buttons & ~1);
  original.forwardmove = 0;
  qa_qvm_write_user_command(view, &original, "q3-1.16n-base", "update");
  qa_qvmx_wire_user_command back = qa_qvm_read_user_command(view, "q3-1.16n-base");
  CHECK(back.buttons == 2052 && back.forwardmove == 0 && back.server_time == 300);
  CHECK(back.weapon == 7 && back.rightmove == 31 && back.upmove == 22 && back.angles[1] == -34);
  CHECK(bytes[4] == 228);
  CHECK(bytes[6] == 0x6b && bytes[7] == 0x6b && bytes[23] == 0x6b);
  qa_qvm_write_user_command(view, &command, "q3-1.16n-base", NULL);
  CHECK(bytes[4] == 133);
  uint8_t small[23] = {0};
  qa_qvmx_view short_view = {small, sizeof small};
  CHECK_ABORTS(qa_qvm_write_user_command(short_view, &command, "q3-modern", NULL));
  CHECK_ABORTS((void)qa_qvm_read_user_command(short_view, "q3-modern"));
}

static qa_qvmx_browser_ping ping_null_slot(void *ctx, double index, double size,
                                           qa_qvm_browser_write write) {
  (void)ctx;
  (void)index;
  (void)size;
  write.call(write.ctx, NULL); /* TS getPing passes null only to the callback */
  qa_qvmx_browser_ping ping = {"", 7};
  return ping;
}
static qa_qvmx_browser_ping ping_address_slot(void *ctx, double index, double size,
                                              qa_qvm_browser_write write) {
  (void)ctx;
  (void)index;
  (void)size;
  write.call(write.ctx, "1.2.3.4");
  qa_qvmx_browser_ping ping = {"1.2.3.4", 42};
  return ping;
}

/* UI trap 48: the null-address callback clears exactly one byte. */
static void test_browser_ping(void) {
  uint8_t *bytes = calloc(4096, 1);
  qa_qvm_memory guest;
  qa_qvm_memory_init(&guest, bytes, 4096, NULL, NULL);
  uint8_t words[20] = {0};
  put_i32(words, 0, 48);
  put_i32(words, 4, 0);
  put_i32(words, 8, 128);
  put_i32(words, 12, 32);
  put_i32(words, 16, 256);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.base.words = words;
  call.base.words_length = sizeof words;
  call.base.memory = bytes;
  call.base.memory_length = 4096;
  call.base.guest = &guest;
  call.guest = &guest;
  call.kind = "engine";
  call.role = "ui";
  call.code = 48;
  qa_qvmx_browser_view browser;
  memset(&browser, 0, sizeof browser);
  browser.get_ping = ping_null_slot;
  memset(bytes + 128, 0xcc, 32);
  qa_qvm_host_result out = -1;
  CHECK(qa_qvm_client_browser_syscall(&call, browser, &out));
  CHECK(out == 0);
  CHECK(bytes[128] == 0 && bytes[129] == 0xcc);
  CHECK(get_i32(bytes, 256) == 7);
  browser.get_ping = ping_address_slot;
  CHECK(qa_qvm_client_browser_syscall(&call, browser, &out));
  char *read = qa_qvm_memory_read_string(&guest, 128);
  CHECK(strcmp(read, "1.2.3.4") == 0);
  free(read);
  CHECK(get_i32(bytes, 256) == 42);
  qa_qvm_memory_destroy(&guest);
  free(bytes);
}

static void script_noop_assert(void *ctx) { (void)ctx; }
static void script_noop_print(void *ctx, const char *text) {
  (void)ctx;
  (void)text;
}
static bool publish_called = false;
static void fake_publish(void *ctx, qa_ui_script_source_reader *reader) {
  (void)ctx;
  (void)reader;
  publish_called = true;
}

/* TS readers Map: fractional/NaN handles miss instead of truncating. */
static void test_script_handles(void) {
  qa_qvm_client_script_services services;
  memset(&services, 0, sizeof services);
  services.assert_current = script_noop_assert;
  services.print = script_noop_print;
  qa_qvm_client_scripts *scripts = qa_qvm_client_scripts_create(services);
  CHECK(scripts != NULL);
  CHECK(qa_qvm_client_scripts_free(scripts, NAN) == 0);
  CHECK(qa_qvm_client_scripts_free(scripts, 1.5) == 0);
  CHECK(qa_qvm_client_scripts_free(scripts, -1) == 0);
  CHECK(qa_qvm_client_scripts_free(scripts, 64) == 0);
  qa_qvm_script_position position;
  CHECK(!qa_qvm_client_scripts_position(scripts, NAN, &position));
  CHECK(!qa_qvm_client_scripts_position(scripts, 2.5, &position));
  publish_called = false;
  qa_qvm_script_publish publish = {NULL, fake_publish};
  CHECK(qa_qvm_client_scripts_read(scripts, 1.5, publish) == 0 && !publish_called);
  CHECK(qa_qvm_client_scripts_read(scripts, INFINITY, publish) == 0 && !publish_called);
  qa_qvm_client_scripts_destroy(scripts);
}

static char captured_tokens[16][128];
static size_t captured_count = 0;
static void capture_publish(void *ctx, qa_ui_script_source_reader *reader) {
  (void)ctx;
  if (captured_count >= 16) return;
  char text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(qa_ui_script_source_reader_raw_token(reader), text);
  size_t length = strlen(text);
  if (length > 127) length = 127;
  memcpy(captured_tokens[captured_count], text, length);
  captured_tokens[captured_count++][length] = '\0';
}

static void collect_tokens(qa_qvm_client_scripts *scripts, double handle,
                           qa_qvm_script_publish publish) {
  /* publish also runs on the final EOF read; drop that capture. */
  while (qa_qvm_client_scripts_read(scripts, handle, publish) == 1) {
  }
  if (captured_count > 0) captured_count--;
}

static bool write_text(const char *root, const char *name, const char *text) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s", root, name);
  FILE *file = fopen(path, "w");
  if (file == NULL) return false;
  bool ok = fwrite(text, 1, strlen(text), file) == strlen(text);
  fclose(file);
  return ok;
}

/* tests/compat/qvm/client-script.test.ts fixture flow + checkpoint. */
static void test_script_flow(void) {
  char root_template[] = "/tmp/qvm-scripts-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  if (root == NULL) return;
  CHECK(write_text(root, "root.menu",
                   "#define TWICE(x) ((x)+(x))\n\"before\"\n#include \"nested.inc\"\n"
                   "#define KEEP 1\n\"after\"\nVALUE TWICE(3)\n"));
  CHECK(write_text(root, "nested.inc", "\"middle\"\n#include \"deep.inc\"\n"));
  CHECK(write_text(root, "deep.inc", "\"deep\"\n"));
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity =
      qa_create_mount_identity("mount:parser:loose", "q3:classic:baseq3:installed", 1);
  mount.as.loose.root_path = root;
  qa_mount_id order[1] = {"mount:parser:loose"};
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:parser:1";
  plan.mounts = &mount;
  plan.mount_count = 1;
  plan.default_order = order;
  plan.default_order_count = 1;
  qa_content_mounted *mounts = qa_open_mount_plan(&plan, NULL);
  CHECK(mounts != NULL);
  qa_ui_script_global_defines *globals =
      qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_qvm_client_script_services services;
  memset(&services, 0, sizeof services);
  services.mounts = mounts;
  services.globals = globals;
  services.assert_current = script_noop_assert;
  services.print = script_noop_print;
  qa_qvm_client_scripts *scripts = qa_qvm_client_scripts_create(services);
  CHECK(scripts != NULL);
  CHECK(qa_qvm_client_scripts_add_define(scripts, "VALUE 7") == 1);
  double handle = qa_qvm_client_scripts_load(scripts, "root.menu");
  CHECK(handle == 1);
  captured_count = 0;
  qa_qvm_script_publish publish = {NULL, capture_publish};
  collect_tokens(scripts, handle, publish);
  const char *expected[11] = {"\"beforemiddledeepafter\"", "7", "(", "(", "3", ")",
                              "+", "(", "3", ")", ")"};
  CHECK(captured_count == 11);
  for (size_t i = 0; i < 11 && i < captured_count; i++)
    CHECK(strcmp(captured_tokens[i], expected[i]) == 0);
  qa_qvm_script_position position;
  CHECK(qa_qvm_client_scripts_position(scripts, handle, &position));
  CHECK(strcmp(position.filename, "root.menu") == 0);
  CHECK(position.line > 1);
  CHECK(qa_qvm_client_scripts_free(scripts, handle) == 1);
  CHECK(qa_qvm_client_scripts_free(scripts, handle) == 0);
  publish_called = false;
  qa_qvm_script_publish fake = {NULL, fake_publish};
  CHECK(qa_qvm_client_scripts_read(scripts, handle, fake) == 0 && !publish_called);

  /* Checkpoint keeps the original slot and macro lookahead. */
  double discarded = qa_qvm_client_scripts_load(scripts, "root.menu");
  double kept = qa_qvm_client_scripts_load(scripts, "root.menu");
  CHECK(discarded == 1 && kept == 2);
  CHECK(qa_qvm_client_scripts_free(scripts, discarded) == 1);
  captured_count = 0;
  CHECK(qa_qvm_client_scripts_read(scripts, kept, publish) == 1);
  qa_save_value *checkpoint = qa_qvm_client_scripts_capture(scripts);
  CHECK(checkpoint != NULL);
  qa_ui_script_global_defines *restored_globals =
      qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_qvm_client_script_services restored_services = services;
  restored_services.globals = restored_globals;
  qa_qvm_client_scripts *restored = qa_qvm_client_scripts_create(restored_services);
  qa_qvm_client_scripts_restore(restored, checkpoint);
  char left[16][128], right[16][128];
  captured_count = 0;
  collect_tokens(restored, kept, publish);
  size_t left_count = captured_count;
  memcpy(left, captured_tokens, sizeof left);
  captured_count = 0;
  collect_tokens(scripts, kept, publish);
  memcpy(right, captured_tokens, sizeof right);
  CHECK(left_count == captured_count && left_count == 10);
  for (size_t i = 0; i < left_count && i < captured_count; i++)
    CHECK(strcmp(left[i], right[i]) == 0);
  if (left_count == 10) CHECK(strcmp(left[0], "7") == 0);
  CHECK(qa_qvm_client_scripts_position(restored, kept, &position));
  qa_save_value_free(checkpoint);
  qa_qvm_client_scripts_destroy(restored);
  qa_ui_script_global_defines_destroy(restored_globals);
  qa_qvm_client_scripts_close_all(scripts);
  qa_qvm_client_scripts_destroy(scripts);
  qa_ui_script_global_defines_destroy(globals);
  qa_content_mounted_destroy(mounts);
  char path[256];
  const char *names[3] = {"root.menu", "nested.inc", "deep.inc"};
  for (int i = 0; i < 3; i++) {
    snprintf(path, sizeof path, "%s/%s", root, names[i]);
    remove(path);
  }
  rmdir(root);
}

static qa_qvmx_host_prediction fake_predict(void *ctx,
                                            qa_qvmx_bot_movement_prediction_query query) {
  (void)query;
  return *(const qa_qvmx_host_prediction *)ctx;
}
static qa_bounds fake_presence_bounds(void *ctx, double presence) {
  (void)ctx;
  (void)presence;
  qa_bounds bounds = {{0, 0, 0}, {0, 0, 10}};
  return bounds;
}
static double fake_entity_number(void *ctx, qa_actor_id actor) {
  (void)ctx;
  (void)actor;
  return 0;
}

/* Math.max(0, NaN) is NaN in TS, so success/time propagate it. */
static void test_movement_prediction_max(void) {
  qa_qvmx_host_prediction result;
  memset(&result, 0, sizeof result);
  result.stop_event = 0;
  result.frames = 10;
  result.seconds = 1.5;
  result.has_end_area = true;
  result.end_area = 3;
  qa_qvmx_bot_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  navigation.ctx = &result;
  navigation.predict_client_movement = fake_predict;
  navigation.presence_bounds = fake_presence_bounds;
  qa_qvmx_bot_movement_prediction_query query;
  memset(&query, 0, sizeof query);
  query.frame_time = 0.05;
  query.max_frames = NAN;
  query.presence = 2;
  qa_qvm_entity_number_fn entity_number = {NULL, fake_entity_number};
  qa_qvm_aas_movement_prediction prediction =
      qa_qvm_bot_movement_prediction(&navigation, query, entity_number);
  CHECK(!prediction.success);
  CHECK(prediction.time == 1.5 && prediction.frames == 10 && prediction.end_area == 3);
  result.stop_event = 1;
  result.seconds = NAN;
  prediction = qa_qvm_bot_movement_prediction(&navigation, query, entity_number);
  CHECK(prediction.success);
  CHECK(isnan(prediction.time));
  CHECK(prediction.frames == 9);
}

int main(void) {
  test_abi();
  test_w6c_qvm_deep_01();
  test_entity_tokens();
  test_entity_token_syscall();
  test_entity_record();
  test_bot_goal();
  test_user_command_legacy();
  test_browser_ping();
  test_script_handles();
  test_script_flow();
  test_movement_prediction_max();
  test_interpreter();
  printf("test_qvm_a: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
