/* TS drift packet D6 (compat): checks for behaviour TS gained after the
 * port's baseline. Expected values come from
 * tests/compat/oracles/d6_drift_oracle.ts (bun against ../quake-anthology-ts)
 * and from tests/compat/q2/guest-console.test.ts,
 * tests/compat/qvm/operations-shift.test.ts and tests/compat/qvm/runaway.test.ts.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/q2/classic/host.h"
#include "qa/compat/q2/classic/printf.h"
#include "qa/compat/q2/guest_console.h"
#include "qa/compat/q2/rerelease/module.h"
#include "qa/compat/q3/native/game_syscalls.h"
#include "qa/compat/q3/native/memory.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/legacy_client_abi.h"
#include "qa/compat/qvm/legacy_presentation.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/memory_syscalls.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/operations.h"
#include "qa/core/common_error.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/support.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* --- guest-console.ts --- */
static void console_case(const char *text, const char *want_text, const char *want_refused) {
  qa_compat_q2_guest_console_filter filter = qa_compat_q2_filter_guest_console(text);
  char *refused = qa_compat_q2_guest_console_refused_list(&filter);
  CHECK(strcmp(filter.text, want_text) == 0);
  CHECK(strcmp(refused, want_refused) == 0);
  free(refused);
  qa_compat_q2_guest_console_filter_free(&filter);
}
static void test_guest_console(void) {
  console_case("gamemap \"base1\"\n", "gamemap \"base1\"\n", "");
  console_case("kick 3; quit", "kick 3\n", "quit");
  console_case("say \"a;quit\"\nset rcon_password x", "say \"a;quit\"\n", "set");
  console_case("MAP q2dm1", "MAP q2dm1\n", "");
  console_case("  ;;\n", "", "");
  console_case("echo hi;wait;BIND x \"y\"", "echo hi\nwait\n", "bind");
}

/* --- fitRereleaseText / fitClassicUserinfo --- */
static void fit_case(const char *text, size_t capacity, const char *want) {
  char *fitted = qa_compat_q2_fit_rerelease_text(text, capacity);
  CHECK(strcmp(fitted, want) == 0);
  free(fitted);
}
static void test_fit_text(void) {
  fit_case("abcdef", 4, "abc");
  fit_case("ab\xc3\xa9" "cd", 4, "ab");
  fit_case("a\xf0\x9f\x98\x80" "b", 4, "a");
  fit_case("a\xf0\x9f\x98\x80" "b", 5, "a\xef\xbf\xbd");
  fit_case("a\xf0\x9f\x98\x80" "b", 6, "a\xf0\x9f\x98\x80");
  fit_case("short", 256, "short");
  char long_text[601];
  memset(long_text, 'x', 600);
  long_text[600] = '\0';
  char *fitted = qa_compat_q2_fit_classic_userinfo(long_text, 512);
  CHECK(strlen(fitted) == 511);
  free(fitted);
}

/* --- classic printf size prefixes and stray specifiers --- */
static void layouts_case(const char *format, const char *const *want, size_t count) {
  qa_guest_value_layout layouts[16];
  size_t n = qa_compat_q2_classic_printf_layouts(format, layouts, 16);
  CHECK(n == count);
  for (size_t i = 0; i < n && i < count; i++) CHECK(!layouts[i].is_aggregate && strcmp(layouts[i].storage, want[i]) == 0);
}
static void test_printf(void) {
  const char *const a[] = {"int32", "int32", "int64", "int64", "int64"};
  layouts_case("%hd|%hhu|%lld|%I64x|%llu", a, 5);
  layouts_case("%5%z|%q|100%", NULL, 0);
  const char *const c[] = {"int32", "int32", "int32", "int64", "int32", "int32", "int32"};
  layouts_case("%I32d %Id %Ld %jd %zd %td %wd", c, 7);
  qa_guest_call_value values[6];
  memset(values, 0, sizeof values);
  values[0].kind = QA_GUEST_VALUE_INT32; values[0].number_value = 70000;
  values[1].kind = QA_GUEST_VALUE_INT32; values[1].number_value = -1;
  values[2].kind = QA_GUEST_VALUE_INT64; values[2].int64_value = -5;
  values[3].kind = QA_GUEST_VALUE_UINT64; values[3].uint64_value = 0xfedcba9876543210ull;
  values[4].kind = QA_GUEST_VALUE_INT64; values[4].int64_value = -1;
  values[5].kind = QA_GUEST_VALUE_UINT32; values[5].number_value = 0x12345678;
  char *text = qa_compat_q2_classic_printf(NULL, "%hd|%hhu|%lld|%I64x|%llu|%hx", values, 6);
  CHECK(strcmp(text, "4464|255|-5|fedcba9876543210|18446744073709551615|5678") == 0);
  free(text);
  qa_guest_call_value one;
  memset(&one, 0, sizeof one);
  one.kind = QA_GUEST_VALUE_INT32; one.number_value = 1;
  text = qa_compat_q2_classic_printf(NULL, "%5%z|%q|100%", &one, 1);
  CHECK(strcmp(text, "%z|%q|100%") == 0);
  free(text);
}

/* --- legacy client ABI and lenient presentation --- */
static void test_legacy(void) {
  size_t count = 9;
  const char *const bad[] = {"cs", "x"};
  CHECK(qa_qvm_legacy_client_command(bad, 2, "q3-1.16n-base", &count) == NULL && count == 0);
  const char *const unmapped[] = {"cs", "13", "a"};
  CHECK(qa_qvm_legacy_client_command(unmapped, 3, "q3-1.16n-base", &count) == NULL);
  const char *const kept[] = {"cs", "21", "a"};
  char **out = qa_qvm_legacy_client_command(kept, 3, "q3-1.16n-base", &count);
  CHECK(out != NULL && count == 3 && strcmp(out[1], "13") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  CHECK(qa_qvm_legacy_event_ex(200, "q3-1.16n-base", true, true) == 0);
  CHECK(qa_qvm_legacy_entity_type_ex(12, "q3-1.16n-base", true, true) == 0);
  CHECK(qa_qvm_legacy_entity_type_ex(13 + 200, "q3-1.16n-base", true, true) == 0);
  CHECK(qa_qvm_legacy_powerup_bits_ex(0x3ff, "q3-1.16n-base", true) == 511);
}

/* --- operations-shift.test.ts --- */
static void test_shift(void) {
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_LSH, 1, 32) == 1);
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_LSH, 1, 33) == 2);
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_LSH, 1, -1) == INT32_MIN);
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_RSHI, -8, 33) == -4);
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_RSHU, -1, 63) == 1);
}

/* --- runaway.test.ts --- */
static void put_i32(uint8_t *out, size_t offset, int32_t value) {
  for (int i = 0; i < 4; i++) out[offset + (size_t)i] = (uint8_t)((uint32_t)value >> (i * 8));
}
typedef struct op { qa_qvm_opcode opcode; bool has_operand; int32_t operand; } op;
static size_t build_qvm(uint8_t *out, const op *ops, size_t count, int32_t bss) {
  uint8_t code[64];
  size_t length = 0;
  for (size_t i = 0; i < count; i++) {
    code[length++] = (uint8_t)ops[i].opcode;
    if (ops[i].has_operand) { put_i32(code, length, ops[i].operand); length += 4; }
  }
  const int32_t header[8] = {0x12721444, (int32_t)count, 32, (int32_t)length, 32 + (int32_t)length, 0, 0, bss};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  memcpy(out + 32, code, length);
  return 32 + length;
}
static double no_trap(void *ctx, qa_qvm_syscall *call) { (void)ctx; (void)call; qa_range_error("Unexpected trap"); }
typedef struct run_rig { qa_qvm_interpreter *vm; } run_rig;
static void run_body(void *ctx) {
  const double args[10] = {0};
  (void)qa_qvm_interpreter_invoke(((run_rig *)ctx)->vm, args, 10, 0, NULL);
}
static bool fault(const op *ops, size_t count, qa_caught_error *caught) {
  uint8_t source[256];
  size_t length = build_qvm(source, ops, count, 4096);
  qa_qvm_image image;
  if (!qa_qvm_parse(source, length, "runaway.qvm", &image)) return false;
  qa_qvm_interpreter_system_call system_call = {NULL, no_trap};
  run_rig rig = {qa_qvm_interpreter_create(&image, system_call, NULL, NULL, "compiled", NULL)};
  const bool completed = qa_try(run_body, &rig, caught);
  qa_qvm_interpreter_destroy(rig.vm);
  return !completed;
}
typedef struct parse_rig { const uint8_t *bytes; size_t length; } parse_rig;
static void parse_body(void *ctx) {
  parse_rig *r = ctx;
  qa_qvm_image image;
  (void)qa_qvm_parse(r->bytes, r->length, "huge.qvm", &image);
}
static void test_runaway(void) {
  qa_caught_error caught;
  const op loop[] = {{QA_QVM_OP_ENTER, true, 64}, {QA_QVM_OP_CONST, true, 1}, {QA_QVM_OP_JUMP, false, 0}};
  CHECK(fault(loop, 3, &caught));
  if (strcmp(caught.name, "CommonError") != 0) fprintf(stderr, "caught %s %d: %s\n", caught.name, caught.code, caught.message);
  CHECK(strcmp(caught.name, "CommonError") == 0 && caught.code == QA_COMMON_ERROR_DROP && strstr(caught.message, "instruction budget") != NULL);
  const op recurse[] = {{QA_QVM_OP_ENTER, true, 0}, {QA_QVM_OP_CONST, true, 0}, {QA_QVM_OP_CALL, false, 0}};
  CHECK(fault(recurse, 3, &caught));
  CHECK(strcmp(caught.name, "CommonError") == 0 && caught.code == QA_COMMON_ERROR_DROP);
  uint8_t huge[64];
  const op enter[] = {{QA_QVM_OP_ENTER, true, 0}};
  size_t length = build_qvm(huge, enter, 1, 0x7fffffff);
  put_i32(huge, 16, 37);
  parse_rig rig = {huge, length};
  CHECK(!qa_try(parse_body, &rig, &caught) && strstr(caught.message, "hunk allocation") != NULL);
  /* vm_game, vm_cgame and vm_ui select the instruction semantics, with id's compiled default. */
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  CHECK(strcmp(qa_qvm_semantics_from_cvar(cvars, "qagame"), "compiled") == 0);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, "vm_game", "1", true, &snapshot)) qa_cvar_read_free(&snapshot);
  if (qa_cvar_registry_set(cvars, "vm_ui", "0", true, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(strcmp(qa_qvm_semantics_from_cvar(cvars, "qagame"), "interpreted") == 0);
  CHECK(strcmp(qa_qvm_semantics_from_cvar(cvars, "ui"), "compiled") == 0);
  CHECK(strcmp(qa_qvm_semantics_from_cvar(cvars, "cgame"), "compiled") == 0);
  qa_cvar_registry_destroy(cvars);
}

/* --- memory-syscalls.ts: overlapping memcpy behaves as memmove; a self-copy writes nothing --- */
static void test_memory_syscalls(void) {
  static uint8_t bytes[4096];
  for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t)i;
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_game_memory game = qa_qvm_memory_game_memory(&memory);
  uint8_t words[16];
  put_i32(words, 0, 101); put_i32(words, 4, 102); put_i32(words, 8, 100); put_i32(words, 12, 8);
  int32_t out = -1;
  CHECK(qa_qvm_memory_syscall("game", words, sizeof words, &game, &out) && out == 0);
  const uint8_t want[] = {100, 101, 100, 101, 102, 103, 104, 105, 106, 107};
  CHECK(memcmp(bytes + 100, want, sizeof want) == 0);
  put_i32(words, 4, 200); put_i32(words, 8, 200);
  CHECK(qa_qvm_memory_syscall("game", words, sizeof words, &game, &out) && bytes[200] == 200);
  qa_qvm_memory_destroy(&memory);
}

/* --- compat/q3/native: game-syscalls.ts and memory.ts --- */
typedef struct store_rig { qa_qvm_writable_view view; } store_rig;
static void store_body(void *ctx) { qa_qvm_writable_view_set_int32(&((store_rig *)ctx)->view, 0, 1, true); }
static void test_native(void) {
  qa_sparse_guest_memory sparse;
  qa_sparse_guest_memory_options options;
  memset(&options, 0, sizeof options);
  options.module = (qa_guest_module_ref){"test:native", "n", "sha256:bb", "1"};
  options.pointer_bytes = 4;
  qa_sparse_guest_memory_init(&sparse, &options);
  qa_guest_map_options map;
  memset(&map, 0, sizeof map);
  map.base = 0x10000; map.byte_length = 0x1000; map.permissions = QA_PERM_READ_WRITE; map.label = "rw";
  (void)qa_sparse_map(&sparse, &map);
  const uint8_t text[] = "hello";
  map.base = 0x20000; map.permissions = QA_PERM_READ; map.label = "ro"; map.bytes = text; map.bytes_length = sizeof text;
  (void)qa_sparse_map(&sparse, &map);
  qa_native_game_memory native;
  qa_native_game_memory_init(&native, &sparse);
  qa_qvm_game_memory game = qa_native_game_memory_game_memory(&native);
  int64_t address = 0;
  CHECK(qa_qvm_game_memory_address(&game, 0x10010, &address) && address == 0x10010);
  CHECK(!qa_qvm_game_memory_address(&game, 0, &address));
  CHECK(qa_qvm_game_memory_pointer_word(&game, true, 0x10010) == 0x10010 && qa_qvm_game_memory_pointer_word(&game, false, 0) == 0);
  qa_qvm_game_memory_write_string(&game, 0x10020, "abcdef", 4);
  char *read = qa_qvm_game_memory_read_string(&game, 0x10020);
  CHECK(strcmp(read, "abc") == 0);
  free(read);
  qa_qvm_game_region region;
  CHECK(qa_qvm_game_memory_pointer(&game, 0x20000, &region) && region.read_only && region.length == 0x1000 && memcmp(region.bytes, "hello", 6) == 0);
  store_rig rig;
  qa_qvm_game_memory_view(&game, 0x20000, 4, 0, &rig.view);
  CHECK(qa_qvm_writable_view_get_uint8(&rig.view, 0) == 'h');
  qa_caught_error caught;
  CHECK(!qa_try(store_body, &rig, &caught) && strcmp(caught.message, "Native module memory is read-only") == 0);
  /* nativeQ3GameCall decodes a trap exactly like an interpreted module's. */
  uint8_t words[64] = {0};
  put_i32(words, 0, 0);
  qa_qvm_host_call call;
  qa_native_q3_game_call(words, sizeof words, &game, NULL, 0, false, &call);
  CHECK(strcmp(call.kind, "engine") == 0 && strcmp(call.role, "qagame") == 0 && call.code == 0 && strcmp(call.abi_profile, "q3-modern") == 0);
  put_i32(words, 0, 12345);
  qa_native_q3_game_call(words, sizeof words, &game, NULL, 0, false, &call);
  CHECK(strcmp(call.kind, "extension") == 0 && call.code == 12345);
  qa_qvm_game_memory shared = qa_qvm_host_call_game_memory(&call);
  CHECK(shared.ctx == &native);
  qa_native_game_memory_destroy(&native);
  qa_sparse_guest_memory_destroy(&sparse);
}

int main(void) {
  test_guest_console();
  test_fit_text();
  test_printf();
  test_legacy();
  test_shift();
  test_runaway();
  test_memory_syscalls();
  test_native();
  printf("compat drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
