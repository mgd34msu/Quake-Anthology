/* qvmBotLibrarySyscall over the real BotLibrary: a port of
 * tests/compat/qvm/legacy-bots.test.ts, plus the modern-arity checks the
 * previous mirror test carried (setName client, not-found goal results).
 * Every callback checks a sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/bots/behavior/assets.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/bot_library_syscalls.h"
#include "qa/compat/qvm/legacy_bot_abi.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/syscalls.h"

static int failures = 0, checks = 0;
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
    fflush(NULL);                                                       \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      (void)!freopen("/dev/null", "w", stderr);                         \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                   \
  } while (0)

static int sentinel_value;
#define SENTINEL ((void *)&sentinel_value)

static qa_bot_asset_files files;
static uint8_t guest_bytes[4096];
static qa_qvm_memory guest;
static qa_bot_library_bots_a library;
static qa_qvm_bot_library_services services;
static qa_qvm_system_call legacy_call, modern_call;

typedef struct command_record { int64_t client; char text[128]; } command_record;
static command_record commands[16];
static size_t command_count;
static qa_qvmx_wire_user_command user_commands[4];
static size_t user_command_count;

static const uint8_t *read_file(void *ctx, const char *path, size_t *out_len) {
  CHECK(ctx == SENTINEL);
  for (size_t i = 0; i < files.count; i++)
    if (strcmp(files.files[i].path, path) == 0) {
      *out_len = files.files[i].length;
      return files.files[i].bytes;
    }
  return NULL;
}
static int64_t next_int(void *ctx) {
  CHECK(ctx == SENTINEL);
  return 0;
}
static double milliseconds(void *ctx) {
  CHECK(ctx == SENTINEL);
  return 0;
}
static void print(void *ctx, int severity, const char *text) {
  (void)severity;
  (void)text;
  CHECK(ctx == SENTINEL);
}
static void client_command(void *ctx, int64_t client, const char *command) {
  CHECK(ctx == SENTINEL);
  if (command_count < 16) {
    commands[command_count].client = client;
    snprintf(commands[command_count].text, sizeof commands[command_count].text, "%s", command);
    command_count++;
  }
}
static qa_bot_log_open_result open_log(void *ctx, const char *filename) {
  (void)filename;
  CHECK(ctx == SENTINEL);
  qa_bot_log_open_result result;
  memset(&result, 0, sizeof result);
  return result;
}
static void user_command(void *ctx, double client, qa_qvmx_wire_user_command command) {
  (void)client;
  CHECK(ctx == SENTINEL);
  if (user_command_count < 4) user_commands[user_command_count++] = command;
}
static qa_qvm_host_result host(void *ctx, const qa_qvm_host_call *call) {
  CHECK(ctx == SENTINEL);
  qa_qvm_host_result out = 0;
  if (qa_qvm_bot_library_syscall(call, &services, &out)) return out;
  qa_qvm_reject_syscall(call);
}
static void put_i32(uint8_t *bytes, size_t offset, int32_t value) { memcpy(bytes + offset, &value, 4); }
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  int32_t value;
  memcpy(&value, bytes + offset, 4);
  return value;
}
static qa_qvm_host_result invoke(const qa_qvm_system_call *system, const uint8_t *words, size_t length) {
  qa_qvm_module_syscall call;
  memset(&call, 0, sizeof call);
  call.words = words;
  call.words_length = length;
  call.memory = guest_bytes;
  call.memory_length = sizeof guest_bytes;
  call.guest = &guest;
  return qa_qvm_system_call_invoke(system, &call);
}
static qa_qvm_host_result call_on(const qa_qvm_system_call *system, int32_t code, int count, ...);
#include <stdarg.h>
static qa_qvm_host_result call_on(const qa_qvm_system_call *system, int32_t code, int count, ...) {
  uint8_t words[64];
  memset(words, 0, sizeof words);
  put_i32(words, 0, code);
  va_list args;
  va_start(args, count);
  for (int i = 0; i < count; i++) put_i32(words, 4 + (size_t)i * 4, va_arg(args, int32_t));
  va_end(args);
  return invoke(system, words, sizeof words);
}
static void write_string(int32_t at, const char *text) { qa_qvm_memory_write_string(&guest, at, text, 64); }
static void add_file(const char *name, const char *text) {
  char path[64];
  snprintf(path, sizeof path, "botfiles/%s", name);
  CHECK(qa_bot_asset_files_add(&files, path, (const uint8_t *)text, strlen(text)));
}

int main(void) {
  qa_bot_asset_files_init(&files);
  add_file("syn.c", "");
  add_file("rnd.c", "");
  add_file("match.c", "");
  add_file("rchat.c", "");
  add_file("test_t.c", "chat \"test\" { type \"hello\" { \"hello\"; } }");
  add_file("skill.c", "skill 1 { 0 1.0 } skill 4 { 0 4.0 } skill 5 { 0 5.0 }");
  qa_source_bot_library_options_bots_a options;
  memset(&options, 0, sizeof options);
  options.ctx = SENTINEL;
  options.read_file = read_file;
  options.random.next_int = next_int;
  options.random.ctx = SENTINEL;
  options.milliseconds = milliseconds;
  options.print = print;
  options.client_command = client_command;
  options.open_log = open_log;
  qa_bot_library_bots_a_init(&library, &options);
  memset(&services, 0, sizeof services);
  services.library = &library;
  services.ctx = SENTINEL;
  services.user_command = user_command;
  qa_qvm_memory_init(&guest, guest_bytes, sizeof guest_bytes, NULL, NULL);
  qa_qvm_create_system_call(&legacy_call, "qagame", host, SENTINEL, NULL, NULL, "q3-1.16n-base");
  qa_qvm_create_system_call(&modern_call, "qagame", host, SENTINEL, NULL, NULL, "q3-modern");

  /* legacy bot wrappers preserve action ordinals, bits, source chat ownership and usercmd layout */
  qa_bot_action_buffer_setup(&library.actions, 4);
  write_string(128, "Medkit");
  CHECK(call_on(&legacy_call, 402, 2, 2, 128) == 0);
  CHECK(command_count == 1 && commands[0].client == 2 && strcmp(commands[0].text, "use Medkit") == 0);
  command_count = 0;
  call_on(&legacy_call, 412, 1, 2);
  call_on(&legacy_call, 413, 1, 2);
  call_on(&legacy_call, 415, 1, 2);
  call_on(&legacy_call, 409, 1, 2);
  call_on(&legacy_call, 406, 1, 2);
  call_on(&legacy_call, 425, 3, 2, 0, 256);
  CHECK(get_i32(guest_bytes, 256 + 32) == (4 | 8 | 16 | 1024 | 2048));
  qa_bot_action_jump(&library.actions, 1);
  put_i32(guest_bytes, 256, 425);
  put_i32(guest_bytes, 260, 1);
  put_i32(guest_bytes, 264, 0);
  put_i32(guest_bytes, 268, 256);
  for (size_t i = 16; i < 64; i += 4) put_i32(guest_bytes, 256 + i, 0);
  CHECK(invoke(&legacy_call, guest_bytes + 256, 64) == 0);
  CHECK(get_i32(guest_bytes, 256 + 12) == 0);
  CHECK(get_i32(guest_bytes, 256 + 32) == 8);
  qa_qvm_game_import decoded;
  CHECK(qa_qvm_decode_legacy_game_import(408, &decoded) && decoded == QA_QVM_GAME_IMPORT_BOTLIB_EA_SELECT_WEAPON);
  CHECK(!qa_qvm_decode_legacy_game_import(300, &decoded));
  CHECK_ABORTS((void)call_on(&legacy_call, 300, 0));
  put_i32(guest_bytes, 512, 250);
  guest_bytes[516] = 129;
  guest_bytes[517] = 7;
  put_i32(guest_bytes, 520, 11);
  put_i32(guest_bytes, 524, 22);
  put_i32(guest_bytes, 528, 33);
  guest_bytes[532] = (uint8_t)(int8_t)-3;
  guest_bytes[533] = 4;
  guest_bytes[534] = 5;
  call_on(&legacy_call, 211, 2, 2, 512);
  CHECK(user_command_count == 1);
  CHECK(user_commands[0].server_time == 250 && user_commands[0].buttons == 2049 && user_commands[0].weapon == 7);
  CHECK(user_commands[0].angles[0] == 11 && user_commands[0].angles[1] == 22 && user_commands[0].angles[2] == 33);
  CHECK(user_commands[0].forwardmove == -3 && user_commands[0].rightmove == 4 && user_commands[0].upmove == 5);
  write_string(128, "skill.c");
  qa_qvm_host_result character = call_on(&legacy_call, 500, 2, 128, 3);
  CHECK(character > 0);
  CHECK(qa_bot_character_float(&library.characters, character, 0) == 3);
  qa_bot_chat_library_setup(&library.chat);
  int64_t chat = qa_bot_chat_library_allocate(&library.chat);
  CHECK(qa_bot_chat_library_load_chat_file(&library.chat, chat, qa_chat_text("test_t.c"), qa_chat_text("test")));
  write_string(128, "Legacy");
  call_on(&legacy_call, 524, 2, (int32_t)chat, 128);
  qa_bot_chat_library_initial_chat(&library.chat, chat, qa_chat_text("hello"), 0, NULL);
  call_on(&legacy_call, 516, 3, (int32_t)chat, 2, 1);
  CHECK(command_count == 1 && commands[0].client == 2 && strcmp(commands[0].text, "say_team hello") == 0);
  command_count = 0;

  /* Modern arity: GetChatMessage writes the queued message; level items are -1 and map locations 0
   * when no goal configuration is loaded. */
  qa_bot_chat_library_initial_chat(&library.chat, chat, qa_chat_text("hello"), 0, NULL);
  call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_CHAT_MESSAGE, 3, (int32_t)chat, 1024, 64);
  CHECK(strcmp((const char *)guest_bytes + 1024, "hello") == 0);
  write_string(160, "missing");
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_LEVEL_ITEM_GOAL, 3, 0, 160, 2048) == -1);
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_MAP_LOCATION_GOAL, 2, 160, 2048) == 0);
  /* LIBVAR set/get round trip and the BotLib integer characteristic. */
  write_string(192, "bot_testvar");
  write_string(224, "42");
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_LIBVAR_SET, 2, 192, 224) == 0);
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_LIBVAR_GET, 3, 192, 1100, 16) == 0);
  CHECK(strcmp((const char *)guest_bytes + 1100, "42") == 0);
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_INTEGER, 2, (int32_t)character, 0) == 3);
  /* StringContains over guest texts (TS stringContains). */
  write_string(256, "Hello");
  write_string(288, "LL");
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_AI_STRING_CONTAINS, 3, 256, 288, 0) == 2);
  CHECK(call_on(&modern_call, QA_QVM_GAME_IMPORT_BOTLIB_AI_STRING_CONTAINS, 3, 256, 288, 1) == -1);

  qa_bot_library_shutdown_bots_a(&library);
  qa_bot_library_bots_a_destroy(&library);
  qa_bot_asset_files_destroy(&files);
  printf("test_qvm_bot_syscalls: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
