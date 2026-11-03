/* C checks for tests/compat/qvm/{qvm,committed-stores,item-catalog,legacy-bots,
 * guest-game-data}.test.ts and TS-oracle values (bun) for operations, import
 * decoding and retail QVM execution. Function-hook checks live in
 * test_qvm_hooks_depth.c. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/guest_memory.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/item_catalog.h"
#include "qa/compat/qvm/legacy_bot_abi.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/memory_syscalls.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/compat/qvm/snap_vector_syscalls.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/archive.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"

/* TS Q3GameMemory view of an interpreted allocation (compat/qvm/game_memory.h). */
#define QA_GAME(m) ((const qa_qvm_game_memory[]){qa_qvm_memory_game_memory(m)})

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
  do {                                                                              \
    checks++;                                                                       \
    if (!(cond)) {                                                                  \
      failures++;                                                                   \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                               \
  } while (0)
#define CHECK_ABORTS(...)                                                           \
  do {                                                                              \
    checks++;                                                                       \
    fflush(stderr);                                                                 \
    pid_t pid = fork();                                                             \
    if (pid == 0) {                                                                 \
      (void)prctl(PR_SET_DUMPABLE, 0);                                              \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);                      \
      __VA_ARGS__;                                                                  \
      _exit(0);                                                                     \
    }                                                                               \
    int status = 0;                                                                 \
    waitpid(pid, &status, 0);                                                       \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                    \
      failures++;                                                                   \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__);          \
    }                                                                               \
  } while (0)

/* ---- shared helpers ---- */

#define SENTINEL 0x5eed1234u
typedef struct op { int32_t opcode; bool has; int32_t operand; } op;
#define O(x) {QA_QVM_OP_##x, false, 0}
#define OA(x, v) {QA_QVM_OP_##x, true, (v)}

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = 0;
  for (int i = 0; i < 4; i++) bits |= (uint32_t)bytes[offset + (size_t)i] << (8 * i);
  int32_t out;
  memcpy(&out, &bits, 4);
  return out;
}

/* TS bytecode(): header words and OP_ARG byte operands. */
static size_t build(uint8_t *out, const op *ops, size_t count, int32_t bss, const int32_t *data, size_t data_words) {
  size_t length = 0;
  uint8_t *code = out + 32;
  for (size_t i = 0; i < count; i++) {
    code[length++] = (uint8_t)ops[i].opcode;
    if (ops[i].has) {
      if (ops[i].opcode == QA_QVM_OP_ARG) code[length++] = (uint8_t)ops[i].operand;
      else { put_i32(code, length, ops[i].operand); length += 4; }
    }
  }
  int32_t header[8] = {0x12721444, (int32_t)count, 32, (int32_t)length, 32 + (int32_t)length,
                       (int32_t)(data_words * 4), 0, bss - (int32_t)(data_words * 4)};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  for (size_t i = 0; i < data_words; i++) put_i32(out, 32 + length + i * 4, data[i]);
  return 32 + length + data_words * 4;
}

typedef struct throw_job { void (*fn)(void *); void *ctx; } throw_job;
static bool throws_with(void (*fn)(void *), void *ctx, const char *needle) {
  char message[512];
  if (qa_range_error_catch(fn, ctx, message, sizeof message)) return false;
  if (needle != NULL && strstr(message, needle) == NULL) {
    fprintf(stderr, "  unexpected message: %s (wanted %s)\n", message, needle);
    return false;
  }
  return true;
}

static uint32_t fnv_bytes(const uint8_t *bytes, size_t length) {
  uint32_t h = 0x811c9dc5u;
  for (size_t i = 0; i < length; i++) { h ^= bytes[i]; h *= 0x01000193u; }
  return h;
}
static void fnv_word(uint32_t *h, int32_t value) {
  uint32_t v = (uint32_t)value;
  for (int i = 0; i < 4; i++) { *h ^= (v >> (8 * i)) & 255u; *h *= 0x01000193u; }
}

static const double zero_args[10] = {0};

static double unexpected_trap(void *ctx, qa_qvm_syscall *call) {
  (void)ctx; (void)call;
  qa_range_error("Unexpected syscall");
}

typedef struct vm {
  qa_qvm_image image;
  qa_qvm_interpreter *interp;
} vm;

static void vm_open(vm *out, const op *ops, size_t count, int32_t bss, const int32_t *data, size_t data_words,
                    qa_qvm_interpreter_system_call system_call, const char *semantics) {
  static uint8_t bytes[65536];
  size_t length = build(bytes, ops, count, bss, data, data_words);
  CHECK(qa_qvm_parse(bytes, length, "<qvm>", &out->image));
  out->interp = qa_qvm_interpreter_create(&out->image, system_call, NULL, NULL, semantics, NULL);
  CHECK(out->interp != NULL);
}
static void vm_close(vm *v) {
  qa_qvm_interpreter_destroy(v->interp);
  qa_qvm_image_destroy(&v->image);
}
static size_t vm_length(vm *v) {
  size_t length = 0;
  (void)qa_qvm_interpreter_get_memory(v->interp, &length);
  return length;
}
static uint8_t *vm_bytes(vm *v) { return qa_qvm_interpreter_get_memory(v->interp, NULL); }

/* ---- oracle hashes (bun on the TS modules) ---- */

static const int32_t oracle_words[] = {0, 1, -1, 2, 7, -7, 31, 32, 33, -32, 0x7fffffff, INT32_MIN, 0x3f800000,
                                       (int32_t)0xbf800000u, 0x7f800000, (int32_t)0xff800000u, 0x7fc00000,
                                       0x7fc00001, (int32_t)0xffc00000u, 0x7f800001, 1, INT32_MIN, 0x4f000000,
                                       (int32_t)0xcf000000u, 0x4effffff, 0x40490fdb, 0x12345678, 0x0000ff80};
#define ORACLE_WORDS (sizeof oracle_words / sizeof oracle_words[0])

typedef struct binary_job { qa_qvm_opcode op; int32_t a, b, result; } binary_job;
static void binary_body(void *ctx) {
  binary_job *job = ctx;
  job->result = qa_qvm_evaluate_binary(job->op, job->a, job->b);
}

static void test_operations_oracle(void) {
  const qa_qvm_opcode unary[] = {QA_QVM_OP_SEX8, QA_QVM_OP_SEX16, QA_QVM_OP_NEGI, QA_QVM_OP_NEGF, QA_QVM_OP_CVIF, QA_QVM_OP_CVFI};
  uint32_t h = 0x811c9dc5u;
  for (size_t o = 0; o < 6; o++)
    for (size_t i = 0; i < ORACLE_WORDS; i++) fnv_word(&h, qa_qvm_evaluate_unary(unary[o], oracle_words[i]));
  CHECK(h == 3767011587u);
  const qa_qvm_opcode binary[] = {QA_QVM_OP_ADD, QA_QVM_OP_SUB, QA_QVM_OP_DIVI, QA_QVM_OP_DIVU, QA_QVM_OP_MODI,
                                  QA_QVM_OP_MODU, QA_QVM_OP_MULI, QA_QVM_OP_MULU, QA_QVM_OP_BAND, QA_QVM_OP_BOR,
                                  QA_QVM_OP_BXOR, QA_QVM_OP_LSH, QA_QVM_OP_RSHI, QA_QVM_OP_RSHU, QA_QVM_OP_ADDF,
                                  QA_QVM_OP_SUBF, QA_QVM_OP_DIVF, QA_QVM_OP_MULF};
  h = 0x811c9dc5u;
  int skipped = 0;
  for (size_t o = 0; o < 18; o++)
    for (size_t a = 0; a < ORACLE_WORDS; a++)
      for (size_t b = 0; b < ORACLE_WORDS; b++) {
        binary_job job = {binary[o], oracle_words[a], oracle_words[b], 0};
        char message[256];
        if (qa_range_error_catch(binary_body, &job, message, sizeof message)) fnv_word(&h, job.result);
        else { fnv_word(&h, (int32_t)0xdeadbeefu); skipped++; }
      }
  CHECK(h == 1172723831u);
  CHECK(skipped == 116);
  h = 0x811c9dc5u;
  for (int o = QA_QVM_OP_EQ; o <= QA_QVM_OP_GEF; o++)
    for (size_t a = 0; a < ORACLE_WORDS; a++)
      for (size_t b = 0; b < ORACLE_WORDS; b++)
        fnv_word(&h, qa_qvm_evaluate_branch((qa_qvm_opcode)o, oracle_words[a], oracle_words[b]) ? 1 : 0);
  CHECK(h == 4227076389u);
  /* The compiled module's x86 shifts mask the count with CL (TS shiftCount). */
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_LSH, 1, 33) == 2);
  CHECK(qa_qvm_evaluate_binary(QA_QVM_OP_RSHU, INT32_MIN, -1) == 1);
  CHECK(qa_qvmx_evaluate_binary(QA_QVM_OP_RSHI, -64, 36) == -4);
}

static void test_decoders_oracle(void) {
  uint32_t h = 0x811c9dc5u;
  for (int w = -5; w < 700; w++) {
    qa_qvm_game_import game;
    qa_qvm_cgame_import cgame;
    qa_qvm_ui_import ui;
    fnv_word(&h, qa_qvm_decode_game_import(w, &game) ? (int32_t)game : -1);
    fnv_word(&h, qa_qvm_decode_cgame_import(w, &cgame) ? (int32_t)cgame : -1);
    fnv_word(&h, qa_qvm_decode_ui_import(w, &ui) ? (int32_t)ui : -1);
    fnv_word(&h, qa_qvm_decode_legacy_game_import(w, &game) ? (int32_t)game : -1);
  }
  CHECK(h == 425739646u);
  /* legacy-bots.test.ts decoder lines. */
  qa_qvm_game_import decoded;
  CHECK(qa_qvm_decode_legacy_game_import(408, &decoded) && decoded == QA_QVM_GAME_IMPORT_BOTLIB_EA_SELECT_WEAPON);
  CHECK(!qa_qvm_decode_legacy_game_import(300, &decoded));
}

/* ---- qvm.test.ts ---- */

static const op recursive_program[] = {
    OA(ENTER, 16), OA(LOCAL, 24), O(LOAD4), OA(CONST, 0), OA(EQ, 7),
    OA(CONST, 77), OA(LEAVE, 16),
    OA(LOCAL, 28), O(LOAD4), OA(ARG, 8), OA(CONST, -6), O(CALL), OA(LEAVE, 16),
};

typedef struct nested_ctx { uint32_t magic; int before, after; } nested_ctx;
static double nested_trap(void *ctx, qa_qvm_syscall *call) {
  nested_ctx *nested = ctx;
  CHECK(nested->magic == SENTINEL);
  nested->before++;
  CHECK(qa_qvmx_view_i32(&call->words, 0) == 5);
  CHECK(qa_qvmx_view_i32(&call->words, 4) == 123);
  double args[10] = {1};
  double value = call->invoke(call->ctx, args, 10, true, 0, NULL);
  nested->after++;
  CHECK(value == 77);
  return value + 1;
}

static void test_qvm_execution(void) {
  /* interpreted block copies retain source alignment checks (CommonError drop). */
  {
    const op ops[] = {OA(ENTER, 0), OA(CONST, 64), OA(CONST, 101), OA(BLOCK_COPY, 4), OA(CONST, 0), OA(LEAVE, 0)};
    vm v;
    vm_open(&v, ops, 6, 4096, NULL, 0, (qa_qvm_interpreter_system_call){NULL, unexpected_trap}, NULL);
    CHECK_ABORTS((void)qa_qvm_interpreter_invoke(v.interp, zero_args, 10, 0, NULL));
    vm_close(&v);
  }
  /* nested host entry finishes synchronously */
  {
    nested_ctx nested = {SENTINEL, 0, 0};
    vm v;
    vm_open(&v, recursive_program, 13, 4096, NULL, 0, (qa_qvm_interpreter_system_call){&nested, nested_trap}, NULL);
    double args[10] = {0, 123};
    CHECK(qa_qvm_interpreter_invoke(v.interp, args, 10, 0, NULL) == 78);
    CHECK(nested.before == 1 && nested.after == 1);
    CHECK(!qa_qvm_interpreter_get_is_active(v.interp));
    CHECK(qa_qvm_interpreter_get_stack_pointer(v.interp) == (double)vm_length(&v));
    vm_close(&v);
  }
  /* source restart restores data and BSS while retaining prepared code and aliases */
  {
    const op ops[] = {OA(ENTER, 0), OA(CONST, 4), O(LOAD4), OA(LEAVE, 0)};
    const int32_t data[] = {0, 12};
    static uint8_t source[256];
    size_t length = build(source, ops, 4, 4096, data, 2);
    vm v;
    CHECK(qa_qvm_parse(source, length, "<qvm>", &v.image));
    v.interp = qa_qvm_interpreter_create(&v.image, (qa_qvm_interpreter_system_call){NULL, unexpected_trap}, NULL, NULL, NULL, NULL);
    qa_qvm_memory alias;
    qa_qvm_memory_init(&alias, vm_bytes(&v), vm_length(&v), NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_memory_view(&alias, 4, 4, 0, &view);
    qa_qvm_writable_view_set_int32(&view, 0, 99, true);
    vm_bytes(&v)[300] = 8;
    CHECK(qa_qvm_interpreter_invoke(v.interp, zero_args, 10, 0, NULL) == 99);
    qa_qvm_data_image restart;
    CHECK(qa_qvm_parse_restart(source, length, NULL, &restart));
    qa_qvm_interpreter_restart(v.interp, &restart);
    qa_qvm_data_image_destroy(&restart);
    CHECK(qa_qvm_interpreter_invoke(v.interp, zero_args, 10, 0, NULL) == 12);
    CHECK(qa_qvm_writable_view_get_int32(&view, 0, true) == 12);
    CHECK(vm_bytes(&v)[300] == 0);
    qa_qvm_memory_destroy(&alias);
    vm_close(&v);
  }
}

/* A resolved bytecode module over `bytes`, as the TS tests build them. */
typedef struct module_fixture {
  uint8_t bytes[4096];
  size_t length;
  char digest[96];
  qa_resolved_qvm_artifact artifact;
  qa_qvm_module module;
} module_fixture;

static void module_open(module_fixture *f, const op *ops, size_t count, int32_t bss, const char *role,
                        const char *id, qa_qvm_host_fn host, void *host_ctx) {
  f->length = build(f->bytes, ops, count, bss, NULL, 0);
  char *digest = qa_digest_bytes(f->bytes, f->length);
  CHECK(digest != NULL);
  snprintf(f->digest, sizeof f->digest, "%s", digest);
  free(digest);
  qa_qvm_resolve_artifact_options options;
  memset(&options, 0, sizeof options);
  options.module.id = id;
  options.module.artifact_path = strcmp(role, "cgame") == 0 ? "vm/cgame.qvm" : "vm/qagame.qvm";
  options.module.digest = f->digest;
  options.module.revision = "test";
  options.role = role;
  options.bytes = f->bytes;
  options.length = f->length;
  CHECK(qa_qvm_resolve_artifact(options, &f->artifact));
  qa_qvm_module_options module_options;
  memset(&module_options, 0, sizeof module_options);
  module_options.artifact = &f->artifact;
  module_options.host = host != NULL ? host : qa_qvm_reject_syscall_host;
  module_options.host_ctx = host_ctx;
  qa_qvm_module_init(&f->module, &module_options);
}
static void module_close(module_fixture *f) {
  qa_qvm_module_destroy(&f->module);
  qa_resolved_qvm_artifact_destroy(&f->artifact);
}

typedef struct module_call_job { qa_qvm_module *module; const int32_t *words; size_t count; int64_t index; int32_t result; } module_call_job;
static void module_call_body(void *ctx) {
  module_call_job *job = ctx;
  job->result = qa_qvm_module_call(job->module, job->words, job->count, job->index);
}

static void test_qvm_modules(void) {
  /* production modules retain compiled return control when guest buffers overwrite frame padding */
  {
    const op ops[] = {OA(ENTER, 16), OA(CONST, 6), O(CALL), OA(CONST, 1), O(ADD), OA(LEAVE, 16),
                      OA(ENTER, 16), OA(LOCAL, 16), OA(CONST, 0), O(STORE4), OA(CONST, 240), O(BCOM), OA(LEAVE, 16)};
    static module_fixture f;
    module_open(&f, ops, 13, 4096, "qagame", "test:qagame", NULL, NULL);
    qa_qvm_interpreter *interp = qa_qvm_module_get_interpreter(&f.module);
    CHECK(interp != NULL);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == -240);
    size_t length = 0;
    (void)qa_qvm_interpreter_get_memory(interp, &length);
    CHECK(qa_qvm_interpreter_get_stack_pointer(interp) == (double)length);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == -240);
    qa_qvm_module_retire(&f.module);
    module_close(&f);
  }
  /* unknown engine traps fail instead of reporting successful zero */
  {
    const op ops[] = {OA(ENTER, 16), OA(CONST, -9001), O(CALL), OA(LEAVE, 16)};
    static module_fixture f;
    module_open(&f, ops, 4, 4096, "cgame", "test:cgame", NULL, NULL);
    module_call_job job = {&f.module, NULL, 0, 0, 0};
    CHECK(throws_with(module_call_body, &job, "Unbound cgame QVM syscall 9000"));
    CHECK(!qa_qvm_interpreter_get_is_active(qa_qvm_module_get_interpreter(&f.module)));
    module_close(&f);
  }
}

static double reject_retail(void *ctx, qa_qvm_syscall *call) {
  const char *role = ctx;
  qa_qvm_system_call system;
  qa_qvm_create_system_call(&system, role, NULL, NULL, NULL, NULL, NULL);
  qa_qvm_memory guest;
  qa_qvm_memory_init(&guest, call->memory, call->memory_length, NULL, NULL);
  qa_qvm_module_syscall entry;
  memset(&entry, 0, sizeof entry);
  entry.words = call->words.bytes;
  entry.words_length = call->words.length;
  entry.memory = call->memory;
  entry.memory_length = call->memory_length;
  entry.guest = &guest;
  return qa_qvm_system_call_invoke(&system, &entry);
}

static void test_retail_bytecode(void) {
  static const struct { const char *pack, *role; size_t length; int32_t result; uint32_t before, after; } cases[] = {
      {"baseq3/pak8.pk3", "qagame", 469796, -1, 2991576930u, 1339354450u},
      {"baseq3/pak8.pk3", "cgame", 325220, -1, 2130480291u, 953381220u},
      {"baseq3/pak8.pk3", "ui", 278308, 4, 1665415080u, 1533685636u},
      {"missionpack/pak0.pk3", "qagame", 547700, -1, 1991912837u, 1219254037u},
      {"missionpack/pak0.pk3", "cgame", 442304, -1, 1894228193u, 3142374182u},
      {"missionpack/pak0.pk3", "ui", 272040, 6, 1202471129u, 446156469u},
  };
  const char *home = getenv("HOME");
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char path[512], entry_path[64];
    snprintf(path, sizeof path, "%s/qfiles/Quake 3 Arena/%s", home != NULL ? home : "", cases[i].pack);
    if (access(path, R_OK) != 0) {
      fprintf(stderr, "skip retail %s: corpus missing\n", path);
      continue;
    }
    qa_archive_handle *archive = qa_open_archive(path, false, QA_ARCHIVE_FORMAT_PK3);
    CHECK(archive != NULL);
    snprintf(entry_path, sizeof entry_path, "vm/%s.qvm", cases[i].role);
    const qa_archive_entry *entry = NULL;
    CHECK(qa_archive_find_entries(archive, entry_path, QA_ARCHIVE_PATH_ASCII_INSENSITIVE, &entry, 1) == 1);
    size_t length = 0;
    uint8_t *source = qa_archive_read_entry_entry(archive, entry, &length);
    CHECK(source != NULL && length == cases[i].length);
    qa_qvm_image image;
    CHECK(qa_qvm_parse(source, length, entry_path, &image));
    qa_qvm_interpreter *interp = qa_qvm_interpreter_create(
        &image, (qa_qvm_interpreter_system_call){(void *)cases[i].role, reject_retail}, NULL, NULL, "interpreted", NULL);
    size_t memory_length = 0;
    uint8_t *memory = qa_qvm_interpreter_get_memory(interp, &memory_length);
    CHECK(fnv_bytes(memory, memory_length) == cases[i].before);
    double args[10] = {strcmp(cases[i].role, "ui") == 0 ? 0 : strcmp(cases[i].role, "cgame") == 0 ? 5 : 999};
    CHECK(qa_qvm_interpreter_invoke(interp, args, 10, 0, NULL) == cases[i].result);
    CHECK(fnv_bytes(memory, memory_length) == cases[i].after);
    CHECK(qa_qvm_interpreter_invoke_async(interp, args, 10, 0) == cases[i].result);
    qa_qvm_interpreter_destroy(interp);
    qa_qvm_image_destroy(&image);
    free(source);
    qa_archive_close(archive);
    qa_archive_destroy(archive);
  }
}

/* ---- committed-stores.test.ts ---- */

#define MAX_EVENTS 16
typedef struct event_log {
  uint32_t magic;
  int count;
  int64_t offsets[MAX_EVENTS];
  uint8_t before[MAX_EVENTS][16], after[MAX_EVENTS][16];
  size_t lengths[MAX_EVENTS];
  int64_t sequences[MAX_EVENTS];
} event_log;

static void log_event(void *ctx, const qa_qvm_committed_write *event) {
  event_log *log = ctx;
  CHECK(log->magic == SENTINEL);
  for (size_t r = 0; r < event->range_count && log->count < MAX_EVENTS; r++) {
    int n = log->count++;
    log->offsets[n] = event->ranges[r].byte_offset;
    log->lengths[n] = event->ranges[r].length;
    log->sequences[n] = event->sequence;
    memcpy(log->before[n], event->ranges[r].before, event->ranges[r].length < 16 ? event->ranges[r].length : 16);
    memcpy(log->after[n], event->ranges[r].after, event->ranges[r].length < 16 ? event->ranges[r].length : 16);
  }
}

typedef struct view_job { qa_qvm_writable_view *view; int64_t offset; uint32_t value; int width; } view_job;
static void view_set_body(void *ctx) {
  view_job *job = ctx;
  if (job->width == 1) qa_qvm_writable_view_set_int8(job->view, job->offset, (int32_t)job->value);
  else qa_qvm_writable_view_set_uint32(job->view, job->offset, job->value, true);
}

typedef struct bulk_job { qa_qvm_memory *memory; int kind; int64_t a, b, c; } bulk_job;
static void bulk_body(void *ctx) {
  bulk_job *job = ctx;
  static const uint8_t one[1] = {0};
  if (job->kind == 0) qa_qvm_memory_write_bytes(job->memory, job->a, one, 1);
  else if (job->kind == 1) qa_qvm_memory_copy_bytes(job->memory, job->a, job->b, job->c);
  else if (job->kind == 2) { qa_qvm_writable_view v; qa_qvm_memory_data_view(job->memory, job->a, job->b, &v); }
  else { qa_qvm_writable_view v; qa_qvm_memory_view(job->memory, job->a, job->b, 0, &v); }
}

typedef struct drain_ctx {
  uint32_t magic;
  qa_qvm_memory *memory;
  qa_qvm_writable_view *view;
  char order[4][32];
  int order_count;
  bool late;
  bool nested_rejected;
} drain_ctx;

static void late_listener(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  drain_ctx *drain = ctx;
  CHECK(drain->magic == SENTINEL);
  drain->late = true;
}
static void reentrant_store_body(void *ctx) {
  drain_ctx *drain = ctx;
  qa_qvm_writable_view_set_int32(drain->view, 0, 99, true);
}
static void first_listener(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  drain_ctx *drain = ctx;
  CHECK(drain->magic == SENTINEL);
  snprintf(drain->order[drain->order_count++], 32, "first");
  drain->nested_rejected = throws_with(reentrant_store_body, drain, "bookkeeping");
  (void)qa_qvm_memory_observe_writes(drain->memory, &(qa_qvm_write_range){0, 4}, 1, late_listener, drain, NULL, NULL);
  qa_range_error("publication failure");
}
static void second_listener(void *ctx, const qa_qvm_committed_write *event) {
  drain_ctx *drain = ctx;
  CHECK(drain->magic == SENTINEL);
  snprintf(drain->order[drain->order_count++], 32, "second:%d", event->ranges[0].after[0]);
}
static void store_seven_body(void *ctx) {
  drain_ctx *drain = ctx;
  qa_qvm_writable_view_set_int32(drain->view, 0, 7, true);
}

static double vector_trap(void *ctx, qa_qvm_syscall *call) { (void)ctx; (void)call; return 0; }

static void test_committed_stores(void) {
  /* retained writable views publish detached committed intersections */
  {
    static uint8_t bytes[64];
    memset(bytes, 0, sizeof bytes);
    qa_qvm_memory memory;
    qa_qvm_memory_init(&memory, bytes, 64, NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(&memory, 0, 32, &view);
    qa_qvm_writable_view_set_uint32(&view, 0, 0x11223344u, true);
    event_log log;
    memset(&log, 0, sizeof log);
    log.magic = SENTINEL;
    qa_qvm_write_range ranges[] = {{1, 2}, {2, 2}};
    size_t watch = qa_qvm_memory_observe_writes(&memory, ranges, 2, log_event, &log, NULL, NULL);
    qa_qvm_writable_view_set_uint32(&view, 0, 0xaabbccddu, true);
    CHECK(log.count == 1 && log.offsets[0] == 1 && log.lengths[0] == 3);
    CHECK(memcmp(log.before[0], (uint8_t[]){0x33, 0x22, 0x11}, 3) == 0);
    CHECK(memcmp(log.after[0], (uint8_t[]){0xcc, 0xbb, 0xaa}, 3) == 0);
    qa_qvm_writable_view_set_uint8(&view, 12, 5);
    CHECK(log.count == 1);
    qa_qvm_writable_view_set_uint16(&view, 1, 0x1234, false);
    CHECK(bytes[1] == 0x12 && bytes[2] == 0x34);
    view_job bad = {&view, 31, 4, 4};
    CHECK(throws_with(view_set_body, &bad, "Offset is outside the bounds"));
    CHECK(log.count == 2);
    qa_qvm_memory_writes_unobserve(&memory.writes, watch);
    qa_qvm_memory_writes_unobserve(&memory.writes, watch);
    qa_qvm_writable_view_set_uint32(&view, 0, 0, true);
    CHECK(log.count == 2);
    qa_qvm_memory_close(&memory);
    view_job closed = {&view, 0, 1, 1};
    CHECK(throws_with(view_set_body, &closed, "retired"));
    bulk_job write = {&memory, 0, 0, 0, 0};
    CHECK(throws_with(bulk_body, &write, "retired"));
    qa_qvm_memory_destroy(&memory);
  }
  /* publication drains live listeners, prevents reentrant stores, and invalidates watches */
  {
    static uint8_t bytes[64];
    memset(bytes, 0, sizeof bytes);
    qa_qvm_memory memory;
    qa_qvm_memory_init(&memory, bytes, 64, NULL, NULL);
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(&memory, 0, 4, &view);
    drain_ctx drain;
    memset(&drain, 0, sizeof drain);
    drain.magic = SENTINEL;
    drain.memory = &memory;
    drain.view = &view;
    qa_qvm_write_range range = {0, 4};
    (void)qa_qvm_memory_observe_writes(&memory, &range, 1, first_listener, &drain, NULL, NULL);
    (void)qa_qvm_memory_observe_writes(&memory, &range, 1, second_listener, &drain, NULL, NULL);
    CHECK(throws_with(store_seven_body, &drain, "publication failure"));
    CHECK(drain.nested_rejected);
    CHECK(drain.order_count == 2 && strcmp(drain.order[0], "first") == 0 && strcmp(drain.order[1], "second:7") == 0);
    CHECK(!drain.late);
    CHECK(qa_qvm_writable_view_get_int32(&view, 0, true) == 7);
    qa_qvm_memory_clear_write_observers(&memory);
    qa_qvm_writable_view_set_int32(&view, 0, 8, true);
    CHECK(drain.order_count == 2);
    size_t later = qa_qvm_memory_observe_writes(&memory, &range, 1, late_listener, &drain, NULL, NULL);
    qa_qvm_memory_writes_unobserve(&memory.writes, later);
    qa_qvm_writable_view_set_int32(&view, 0, 9, true);
    CHECK(!drain.late);
    qa_qvm_memory_destroy(&memory);
  }
  /* raw-offset bulk writes include zero, retain overlapping copy semantics and validate first */
  {
    static uint8_t bytes[32];
    for (int i = 0; i < 32; i++) bytes[i] = (uint8_t)i;
    qa_qvm_memory memory;
    qa_qvm_memory_init(&memory, bytes, 32, NULL, NULL);
    event_log log;
    memset(&log, 0, sizeof log);
    log.magic = SENTINEL;
    qa_qvm_write_range all = {0, 32};
    (void)qa_qvm_memory_observe_writes(&memory, &all, 1, log_event, &log, NULL, NULL);
    qa_qvm_memory_copy_bytes(&memory, 2, 0, 6);
    CHECK(memcmp(bytes, (uint8_t[]){0, 1, 0, 1, 2, 3, 4, 5}, 8) == 0);
    CHECK(log.offsets[0] == 2 && log.lengths[0] == 6);
    CHECK(memcmp(log.before[0], (uint8_t[]){2, 3, 4, 5, 6, 7}, 6) == 0);
    CHECK(memcmp(log.after[0], (uint8_t[]){0, 1, 2, 3, 4, 5}, 6) == 0);
    qa_qvm_memory_write_bytes(&memory, 0, bytes + 2, 4);
    CHECK(memcmp(bytes, (uint8_t[]){0, 1, 2, 3}, 4) == 0);
    qa_qvm_memory_fill_bytes(&memory, 0, 2, 257);
    CHECK(bytes[0] == 1 && bytes[1] == 1);
    bulk_job copy = {&memory, 1, 30, 0, 3};
    CHECK(throws_with(bulk_body, &copy, NULL));
    CHECK(log.count == 3);
    bulk_job negative = {&memory, 2, -1, 1, 0};
    CHECK(throws_with(bulk_body, &negative, NULL));
    bulk_job null_view = {&memory, 3, 0, 1, 0};
    CHECK(throws_with(bulk_body, &null_view, NULL));
    qa_qvm_memory_destroy(&memory);
  }
  /* memory and numeric intrinsics publish exact writes with aliased scalar inputs */
  {
    static uint8_t bytes[256];
    memset(bytes, 0, sizeof bytes);
    qa_qvm_memory memory;
    qa_qvm_memory_init(&memory, bytes, 256, NULL, NULL);
    qa_qvm_memory_write_bytes(&memory, 32, (uint8_t[]){4, 3, 2, 1}, 4);
    event_log log;
    memset(&log, 0, sizeof log);
    log.magic = SENTINEL;
    qa_qvm_write_range range = {64, 40};
    (void)qa_qvm_memory_observe_writes(&memory, &range, 1, log_event, &log, NULL, NULL);
    uint8_t words[16];
    int32_t result = 0;
    put_i32(words, 0, 101); put_i32(words, 4, 64); put_i32(words, 8, 32); put_i32(words, 12, 4);
    CHECK(qa_qvm_memory_syscall("game", words, 16, QA_GAME(&memory), &result));
    CHECK(log.count == 1 && memcmp(log.after[0], (uint8_t[]){4, 3, 2, 1}, 4) == 0);
    put_i32(words, 0, 100); put_i32(words, 4, 64); put_i32(words, 8, 7); put_i32(words, 12, 4);
    CHECK(qa_qvm_memory_syscall("game", words, 16, QA_GAME(&memory), &result));
    CHECK(log.count == 2 && memcmp(log.after[1], (uint8_t[]){7, 7, 7, 7}, 4) == 0);
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(&memory, 0, 256, &view);
    qa_qvm_writable_view_set_float32(&view, 64, 1.5, true);
    qa_qvm_writable_view_set_float32(&view, 68, 2.5, true);
    qa_qvm_writable_view_set_float32(&view, 72, -1.5, true);
    log.count = 0;
    put_i32(words, 0, 42); put_i32(words, 4, 64);
    CHECK(qa_qvm_snap_vector_syscall("game", words, 8, QA_GAME(&memory), &result));
    CHECK(log.count == 3 && log.offsets[0] == 64 && log.offsets[1] == 68 && log.offsets[2] == 72);
    CHECK(qa_qvm_writable_view_get_float32(&view, 64, true) == 2 && qa_qvm_writable_view_get_float32(&view, 68, true) == 2 &&
          qa_qvm_writable_view_get_float32(&view, 72, true) == -2);
    qa_qvm_memory_destroy(&memory);
  }
  /* bytecode stores report masked aligned destinations and both source block-copy orders */
  for (int compiled = 0; compiled < 2; compiled++) {
    const op ops[] = {OA(ENTER, 0), OA(CONST, 65), OA(CONST, 0xaa), O(STORE1),
                      OA(CONST, 67), OA(CONST, 0xbbcc), O(STORE2), OA(CONST, 71), OA(CONST, 0x11223344), O(STORE4),
                      OA(CONST, 80), OA(CONST, 64), OA(BLOCK_COPY, 8), OA(CONST, 0), OA(LEAVE, 0)};
    vm v;
    vm_open(&v, ops, 15, 4096, NULL, 0, (qa_qvm_interpreter_system_call){NULL, vector_trap}, compiled ? "compiled" : "interpreted");
    event_log log;
    memset(&log, 0, sizeof log);
    log.magic = SENTINEL;
    qa_qvm_write_range range = {64, 24};
    (void)qa_qvm_memory_observe_writes(qa_qvm_interpreter_get_guest(v.interp), &range, 1, log_event, &log, NULL, NULL);
    (void)qa_qvm_interpreter_invoke(v.interp, zero_args, 10, 0, NULL);
    if (compiled) {
      CHECK(log.count == 4 && log.offsets[0] == 65 && log.offsets[1] == 66 && log.offsets[2] == 68 && log.offsets[3] == 80);
    } else {
      CHECK(log.count == 5 && log.offsets[0] == 65 && log.offsets[1] == 66 && log.offsets[2] == 68 &&
            log.offsets[3] == 84 && log.offsets[4] == 80);
    }
    CHECK(memcmp(vm_bytes(&v) + 80, (uint8_t[]){0, 0xaa, 0xcc, 0xbb, 0x44, 0x33, 0x22, 0x11}, 8) == 0);
    static uint8_t copy[65536];
    memcpy(copy, vm_bytes(&v), vm_length(&v));
    qa_qvm_interpreter_restore_data(v.interp, copy, vm_length(&v));
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(qa_qvm_interpreter_get_guest(v.interp), 64, 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, 1, true);
    CHECK(log.count == (compiled ? 4 : 5));
    /* A raw interpreter view publishes through the same owner. */
    qa_qvm_write_range again = {64, 4};
    (void)qa_qvm_memory_observe_writes(qa_qvm_interpreter_get_guest(v.interp), &again, 1, log_event, &log, NULL, NULL);
    qa_qvmx_view raw = qa_qvm_interpreter_pointer(v.interp, 64);
    qa_qvmx_view_set_i32(&raw, 0, 77);
    CHECK(log.count == (compiled ? 5 : 6));
    vm_close(&v);
  }
}

typedef struct retired_job { qa_qvm_interpreter *interp; int kind; const qa_qvm_image *image; const uint8_t *data; size_t length; } retired_job;
static void retired_body(void *ctx) {
  retired_job *job = ctx;
  if (job->kind == 0) qa_qvm_interpreter_restore_data(job->interp, job->data, job->length);
  else if (job->kind == 1) {
    qa_qvm_data_image image = {job->image->initialized_data, job->image->initialized_length, job->image->allocated_data_length};
    qa_qvm_interpreter_restart(job->interp, &image);
  } else (void)qa_qvm_interpreter_invoke(job->interp, zero_args, 10, 0, NULL);
}

/* module, syscall and guest bridge share one observer owner and publication rejects VM entry */
typedef struct share_ctx {
  uint32_t magic;
  module_fixture *fixture;
  event_log log;
  bool call_rejected, invoke_rejected, store_rejected;
} share_ctx;

static int32_t share_host(void *ctx, const qa_qvm_host_call *call) {
  share_ctx *share = ctx;
  CHECK(share->magic == SENTINEL);
  CHECK(call->guest == share->fixture->module.memory);
  qa_qvm_writable_view view;
  qa_qvm_memory_view(call->guest, 64, 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, 12, true);
  return 0;
}
static void share_call_body(void *ctx) {
  share_ctx *share = ctx;
  (void)qa_qvm_module_call(&share->fixture->module, NULL, 0, 0);
}
static void share_invoke_body(void *ctx) {
  share_ctx *share = ctx;
  (void)qa_qvm_interpreter_invoke(qa_qvm_module_get_interpreter(&share->fixture->module), zero_args, 10, 0, NULL);
}
static void share_store_body(void *ctx) {
  share_ctx *share = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(share->fixture->module.memory, 64, 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, 99, true);
}
static void noop_observer(void *ctx, qa_qvm_module_function_call *call, int64_t base) { (void)ctx; (void)call; (void)base; }
static void share_publish(void *ctx, const qa_qvm_committed_write *event) {
  share_ctx *share = ctx;
  CHECK(share->magic == SENTINEL);
  log_event(&share->log, event);
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = share->fixture->artifact.module;
  reference.instruction_index = 0;
  size_t id = qa_qvm_module_observe_function(&share->fixture->module, &reference, noop_observer, NULL);
  qa_qvm_module_unbind(&share->fixture->module, id);
  share->call_rejected = throws_with(share_call_body, share, "bookkeeping");
  share->invoke_rejected = throws_with(share_invoke_body, share, NULL);
  share->store_rejected = throws_with(share_store_body, share, "bookkeeping");
}
typedef struct observe_job { module_fixture *fixture; } observe_job;
static void observe_after_retire_body(void *ctx) {
  observe_job *job = ctx;
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = job->fixture->artifact.module;
  (void)qa_qvm_module_observe_function(&job->fixture->module, &reference, noop_observer, NULL);
}
static void observe_interpreter_body(void *ctx) {
  observe_job *job = ctx;
  qa_qvm_function_observer observer = {NULL, NULL};
  (void)qa_qvm_interpreter_observe_function(qa_qvm_module_get_interpreter(&job->fixture->module), 0, observer);
}
typedef struct borrow_job { qa_qvmx_view view; } borrow_job;
static void borrow_store_body(void *ctx) {
  borrow_job *job = ctx;
  qa_qvmx_view_set_i32(&job->view, 0, 0);
}

static void test_committed_owner(void) {
  {
    const op ops[] = {OA(ENTER, 16), OA(CONST, -1), O(CALL), OA(LEAVE, 16)};
    static module_fixture f;
    static share_ctx share;
    memset(&share, 0, sizeof share);
    share.magic = SENTINEL;
    share.fixture = &f;
    share.log.magic = SENTINEL;
    module_open(&f, ops, 4, 4096, "qagame", "test:stores", share_host, &share);
    qa_qvm_write_range range = {64, 4};
    (void)qa_qvm_memory_observe_writes(f.module.memory, &range, 1, share_publish, &share, NULL, NULL);
    (void)qa_qvm_module_call(&f.module, NULL, 0, 0);
    CHECK(share.call_rejected && share.invoke_rejected && share.store_rejected);
    const qa_guest_memory *guest = f.module.guest_memory;
    qa_guest_address_result address = guest->pointer(guest->ctx, 64);
    CHECK(address.has_value);
    uint8_t value[4] = {14, 0, 0, 0};
    guest->write(guest->ctx, address.value, (qa_byte_view){value, 4});
    CHECK(share.log.count == 2 && memcmp(share.log.before[1], (uint8_t[]){12, 0, 0, 0}, 4) == 0);
    borrow_job retained = {qa_qvm_guest_memory_borrow(guest->ctx, address.value, 4)};
    qa_qvm_module_retire(&f.module);
    CHECK(throws_with(borrow_store_body, &retained, "retired"));
    observe_job job = {&f};
    CHECK(throws_with(observe_after_retire_body, &job, "retired"));
    CHECK(throws_with(observe_interpreter_body, &job, "retired"));
    module_close(&f);
  }
  /* retired allocations reject raw interpreter restore and restart before changing bytes */
  {
    const op ops[] = {OA(ENTER, 0), OA(CONST, 0), OA(LEAVE, 0)};
    vm v;
    vm_open(&v, ops, 3, 4096, NULL, 0, (qa_qvm_interpreter_system_call){NULL, unexpected_trap}, NULL);
    static uint8_t changed[4096];
    memcpy(changed, vm_bytes(&v), vm_length(&v));
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(qa_qvm_interpreter_get_guest(v.interp), 0, 1, &view);
    qa_qvm_writable_view_set_uint8(&view, 0, 27);
    changed[0] = 123;
    qa_qvm_memory_close(qa_qvm_interpreter_get_guest(v.interp));
    retired_job restore = {v.interp, 0, &v.image, changed, vm_length(&v)};
    CHECK(throws_with(retired_body, &restore, "retired"));
    CHECK(vm_bytes(&v)[0] == 27);
    retired_job restart = {v.interp, 1, &v.image, NULL, 0};
    CHECK(throws_with(retired_body, &restart, "retired"));
    CHECK(vm_bytes(&v)[0] == 27);
    retired_job invoke = {v.interp, 2, NULL, NULL, 0};
    CHECK(throws_with(retired_body, &invoke, "retired"));
    CHECK(vm_bytes(&v)[0] == 27);
    vm_close(&v);
  }
}

/* ---- item-catalog.test.ts ---- */

static int32_t catalog_string(uint8_t *data, int32_t *next, const char *value) {
  int32_t start = *next;
  memcpy(data + start, value, strlen(value) + 1);
  *next += (int32_t)strlen(value) + 1;
  return start;
}

typedef struct catalog_job { const uint8_t *data; size_t length; qa_qvm_item_layout layout; } catalog_job;
static void catalog_body(void *ctx) {
  catalog_job *job = ctx;
  qa_qvm_catalog_items items;
  if (qa_qvm_read_item_catalog(job->data, job->length, job->layout, false, &items)) qa_qvm_catalog_items_destroy(&items);
}

static void test_item_catalog(void) {
  qa_qvm_item_layout layout;
  memset(&layout, 0, sizeof layout);
  layout.address_is_number = true;
  layout.address = 16;
  layout.count_is_number = true;
  layout.count = 2;
  layout.stride = 20;
  layout.field_class_name = 4;
  layout.field_pickup_name = 12;
  layout.field_type = 0;
  layout.field_tag = 8;
  layout.weapon_type = 7;
  layout.ammo_type = 9;
  static uint8_t data[256];
  memset(data, 0, sizeof data);
  int32_t next = 80;
  const struct { int32_t type; const char *name, *label; } rows[] = {{7, "weapon_arc", "Arc Thrower"}, {9, "ammo_arc", "Arc Charges"}};
  for (int i = 0; i < 2; i++) {
    size_t offset = 16 + (size_t)i * 20;
    put_i32(data, offset, rows[i].type);
    put_i32(data, offset + 4, catalog_string(data, &next, rows[i].name));
    put_i32(data, offset + 8, 14);
    put_i32(data, offset + 12, catalog_string(data, &next, rows[i].label));
  }
  qa_qvm_catalog_items items;
  CHECK(qa_qvm_read_item_catalog(data, sizeof data, layout, false, &items));
  CHECK(items.count == 2 && strcmp(items.entries[0].class_name, "weapon_arc") == 0 &&
        strcmp(items.entries[0].pickup_name, "Arc Thrower") == 0 && items.entries[0].type == 7 && items.entries[0].tag == 14);
  CHECK(strcmp(items.entries[1].class_name, "ammo_arc") == 0 && items.entries[1].type == 9);
  qa_qvm_catalog_items_destroy(&items);
  put_i32(data, 16, 3);
  put_i32(data, 24, 0);
  qa_qvm_catalog_records records;
  CHECK(qa_qvm_read_item_records(data, sizeof data, layout, NULL, 0, NULL, &records));
  CHECK(records.count == 2 && records.entries[0].index == 0 && records.entries[0].address == 16 &&
        records.entries[0].type == 3 && records.entries[0].tag == 0);
  qa_qvm_catalog_records_destroy(&records);
  CHECK(qa_qvm_read_item_catalog(data, sizeof data, layout, false, &items));
  CHECK(items.count == 1 && strcmp(items.entries[0].class_name, "ammo_arc") == 0);
  qa_qvm_catalog_items_destroy(&items);
  catalog_job big = {data, sizeof data, layout};
  big.layout.count = 100;
  CHECK(throws_with(catalog_body, &big, "exceeds initialized"));

  /* Live source catalogs drop their cache when an original store changes them
   * (QvmSourceItemCatalog: observeWrites over the read ranges). */
  static uint8_t bytes[4096];
  memset(bytes, 0, sizeof bytes);
  memcpy(bytes, data, sizeof data);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_item_layout live = layout;
  live.has_source = true;
  live.source = "live";
  qa_qvm_source_item_catalog catalog;
  qa_qvm_source_item_catalog_init_observed(&catalog, &memory, live, NULL, 0);
  const qa_qvm_catalog_records *cached = NULL;
  CHECK(qa_qvm_source_item_catalog_records(&catalog, &cached) && cached->count == 2 && cached->entries[0].type == 3);
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(&memory, 0, 4096, &view);
  qa_qvm_writable_view_set_int32(&view, 200, 1, true); /* outside every read range */
  CHECK(catalog.has_records);
  qa_qvm_writable_view_set_int32(&view, 16, 5, true);
  CHECK(!catalog.has_records);
  CHECK(qa_qvm_source_item_catalog_records(&catalog, &cached) && cached->entries[0].type == 5);
  qa_qvm_source_item_catalog_close(&catalog);
  qa_qvm_source_item_catalog_destroy(&catalog);
  qa_qvm_memory_destroy(&memory);
}

/* ---- guest-game-data.test.ts: fresh legacy snapshots initialize source-only counters ---- */

static void test_legacy_player_write(void) {
  static uint8_t bytes[1024];
  memset(bytes, 0xa5, 444);
  qa_qvm_memory memory;
  qa_qvm_memory_init(&memory, bytes, sizeof bytes, NULL, NULL);
  qa_qvm_writable_view fresh;
  qa_qvm_memory_data_view(&memory, 0, 444, &fresh);
  double stats[16] = {0}, persistent[16], powerups[16] = {0}, ammo[16] = {0};
  for (int i = 0; i < 16; i++) persistent[i] = 100 + i;
  persistent[0] = 50;
  persistent[6] = 70;
  persistent[13] = 110;
  qa_q3_player_state state;
  memset(&state, 0, sizeof state);
  state.velocity.x = 10;
  state.velocity.y = 20;
  state.velocity.z = 30;
  state.stats = (qa_double_view){stats, 16};
  state.persistent = (qa_double_view){persistent, 16};
  state.powerups = (qa_double_view){powerups, 16};
  state.ammo = (qa_double_view){ammo, 16};
  qa_qvm_write_player_state(&fresh, &state, "q3-1.16n-base", NULL);
  const int zeroed[] = {5, 6, 12, 13, 14, 15};
  for (int i = 0; i < 6; i++) CHECK(get_i32(bytes, 248 + (size_t)zeroed[i] * 4) == 0);
  CHECK(get_i32(bytes, 248) == 50);
  CHECK(get_i32(bytes, 248 + 7 * 4) == 70);
  CHECK(get_i32(bytes, 248 + 11 * 4) == 110);
  CHECK(qa_qvm_writable_view_get_float32(&fresh, 32, true) == 10);
  CHECK(qa_qvm_writable_view_get_float32(&fresh, 40, true) == 30);
  qa_qvm_memory_destroy(&memory);
}

int main(void) {
  test_operations_oracle();
  test_decoders_oracle();
  test_qvm_execution();
  test_qvm_modules();
  test_retail_bytecode();
  test_committed_stores();
  test_committed_owner();
  test_item_catalog();
  test_legacy_player_write();
  printf("%d checks, %d failures\n", checks, failures);
  fflush(stdout);
  return failures == 0 ? 0 : 1;
}
