/*
 * Port of tests/guest/core/memory.test.ts, tests/guest/x86/cpu.test.ts,
 * tests/guest/x64/cpu.test.ts (non-managed cases), tests/guest/abi/
 * calls.test.ts (minus compat native-combat), tests/guest/elf/tls.test.ts
 * and tests/guest/pe/loader.test.ts (+ fixture.ts). Fault expectations
 * that TS writes as toThrow outside a CPU run abort in C and are checked
 * in a forked child (CHECK_ABORTS). Corpus/managed-block cases are not
 * ported (see report). GPL-2.0-or-later.
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

#include "qa/foundation/support.h"
#include "qa/guest/core.h"
#include "qa/guest/x86.h"
#include "qa/guest/x64.h"
#include "qa/guest/abi.h"
#include "qa/guest/elf.h"
#include "qa/guest/pe.h"

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

static qa_guest_module_ref mod(const char *id) {
  qa_guest_module_ref m = {id, "authored-fixture", "digest", "1"};
  return m;
}
static qa_guest_core_address A(uint64_t v) { qa_guest_core_address a = {v}; return a; }
static qa_guest_core_address at(qa_sparse_guest_memory *mem, uint64_t raw) {
  qa_guest_core_address out;
  if (!qa_sparse_pointer(mem, raw, &out)) qa_range_error("test address must be nonnull");
  return out;
}
static void mem_new(qa_sparse_guest_memory *mem, const char *id, int pointer_bytes) {
  qa_sparse_guest_memory_options opts;
  memset(&opts, 0, sizeof opts);
  opts.module = mod(id);
  opts.pointer_bytes = pointer_bytes;
  qa_sparse_guest_memory_init(mem, &opts);
}
static qa_guest_core_address smap(qa_sparse_guest_memory *mem, uint64_t base, size_t len,
                             qa_guest_permissions perm, const uint8_t *bytes, size_t blen) {
  qa_guest_map_options o;
  memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = len; o.permissions = perm; o.bytes = bytes; o.bytes_length = blen;
  return qa_sparse_map(mem, &o);
}
static uint64_t r64(qa_sparse_guest_memory *mem, uint64_t a) { return qa_sparse_read_u64(mem, at(mem, a)); }
static uint32_t r32(qa_sparse_guest_memory *mem, uint64_t a) { return qa_sparse_read_u32(mem, at(mem, a)); }

typedef struct test_ext {
  qa_sparse_guest_memory *mem;
  qa_guest_mapping buf[64];
} test_ext;
static qa_guest_core_address ext_map(void *ctx, uint64_t base, size_t len, qa_guest_permissions perm,
                                const char *label, const uint8_t *bytes, size_t blen) {
  test_ext *e = (test_ext *)ctx;
  qa_guest_map_options o;
  memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = len; o.permissions = perm; o.label = label; o.bytes = bytes; o.bytes_length = blen;
  return qa_sparse_map(e->mem, &o);
}
static qa_guest_core_address ext_alloc(void *ctx, size_t len, bool ha, uint64_t al, bool hp,
                                  qa_guest_permissions perm, const char *label) {
  test_ext *e = (test_ext *)ctx;
  qa_guest_allocation_options o;
  memset(&o, 0, sizeof o);
  o.byte_length = len; o.alignment = ha ? al : 0; o.permissions = perm; o.has_permissions = hp; o.label = label;
  return qa_sparse_allocate(e->mem, &o);
}
static void ext_unmap(void *ctx, qa_guest_core_address a, size_t len) { qa_sparse_unmap(((test_ext *)ctx)->mem, a, len); }
static void ext_protect(void *ctx, qa_guest_core_address a, size_t len, qa_guest_permissions p) {
  qa_sparse_protect(((test_ext *)ctx)->mem, a, len, p);
}
static const qa_guest_mapping *ext_mappings(void *ctx, size_t *count) {
  test_ext *e = (test_ext *)ctx;
  *count = qa_sparse_mappings(e->mem, e->buf, 64);
  if (*count > 64) *count = 64;
  return e->buf;
}
static void ext_move(void *ctx, qa_guest_core_address d, qa_guest_core_address s, size_t n) {
  qa_sparse_move(((test_ext *)ctx)->mem, d, s, n);
}
static void ext_fill(void *ctx, qa_guest_core_address a, size_t n, int v) {
  qa_sparse_fill(((test_ext *)ctx)->mem, a, n, (uint32_t)v);
}
static int64_t ext_find_zero(void *ctx, qa_guest_core_address a, size_t m) {
  return qa_sparse_find_zero(((test_ext *)ctx)->mem, a, m);
}
static void make_ext(qa_mapped_memory_ext *ext, test_ext *storage, qa_sparse_guest_memory *mem) {
  memset(storage, 0, sizeof *storage);
  storage->mem = mem;
  memset(ext, 0, sizeof *ext);
  ext->ctx = storage;
  ext->map = ext_map; ext->allocate = ext_alloc; ext->unmap = ext_unmap;
  ext->protect = ext_protect; ext->mappings = ext_mappings;
  ext->move = ext_move; ext->fill = ext_fill; ext->find_zero = ext_find_zero;
}

/* ---------- core/memory.test.ts ---------- */
static void test_core_flags_registers(void) {
  static const qa_guest_flag flags[15] = {QA_FLAG_CARRY, QA_FLAG_PARITY, QA_FLAG_AUXILIARY_CARRY,
      QA_FLAG_ZERO, QA_FLAG_SIGN, QA_FLAG_TRAP, QA_FLAG_INTERRUPT, QA_FLAG_DIRECTION,
      QA_FLAG_OVERFLOW, QA_FLAG_RESUME, QA_FLAG_VIRTUAL_8086, QA_FLAG_ALIGNMENT_CHECK,
      QA_FLAG_VIRTUAL_INTERRUPT, QA_FLAG_VIRTUAL_INTERRUPT_PENDING, QA_FLAG_IDENTIFICATION};
  static const int bits[15] = {0, 2, 4, 6, 7, 8, 9, 10, 11, 16, 17, 18, 19, 20, 21};
  static const uint64_t initials[3] = {0, UINT64_MAX, 0x123456789abcdef0ull};
  for (int k = 0; k < 3; k++) {
    for (int i = 0; i < 15; i++) {
      qa_processor_flags f;
      qa_processor_flags_init(&f, initials[k]);
      uint64_t mask = 1ull << bits[i];
      CHECK(qa_processor_flags_get(&f, flags[i]) == ((initials[k] & mask) != 0));
      qa_processor_flags_set(&f, flags[i], true);
      CHECK(qa_processor_flags_value(&f) == (initials[k] | mask));
      qa_processor_flags_set(&f, flags[i], false);
      CHECK(qa_processor_flags_value(&f) == (initials[k] & ~mask));
      qa_processor_flags_set_value(&f, initials[k]);
      CHECK(qa_processor_flags_value(&f) == initials[k]);
    }
  }
  qa_integer_register_file regs;
  qa_integer_register_file_init(&regs, QA_GUEST_X86_64);
  qa_integer_register_file_write(&regs, QA_REG_RAX, 64, 0x123456789abcdef0ull, false);
  qa_integer_register_file_write(&regs, QA_REG_RAX, 8, 0x11, true);
  CHECK(qa_integer_register_file_read(&regs, QA_REG_RAX, 64, false) == 0x123456789abc11f0ull);
  qa_integer_register_file_write(&regs, QA_REG_RAX, 32, 0xfedcba98ull, false);
  CHECK(qa_integer_register_file_read(&regs, QA_REG_RAX, 64, false) == 0xfedcba98ull);
  qa_integer_register_file_write(&regs, QA_REG_R15, 64, UINT64_MAX, false);
  CHECK(qa_integer_register_file_read(&regs, QA_REG_R15, 64, false) == UINT64_MAX);
  uint8_t snap[128];
  CHECK(qa_integer_register_file_checkpoint(&regs, snap, sizeof snap) == 128);
  qa_integer_register_file restored;
  qa_integer_register_file_init(&restored, QA_GUEST_X86_64);
  qa_integer_register_file_restore(&restored, snap, sizeof snap);
  CHECK(qa_integer_register_file_read(&restored, QA_REG_R15, 64, false) == UINT64_MAX);
  qa_integer_register_file i386;
  qa_integer_register_file_init(&i386, QA_GUEST_I386);
  CHECK_ABORTS((void)qa_integer_register_file_read(&i386, QA_REG_R8, 32, false));
  CHECK_ABORTS((void)qa_integer_register_file_read(&regs, QA_REG_RSP, 8, true));
  CHECK_ABORTS((void)qa_integer_register_file_checkpoint(&regs, snap, 1));
  qa_guest_processor_initial_state init = {QA_GUEST_X86_64, 0xf123456789abcdefull, 0x20000, 2, 0x37f, 0x1f80, 0xffff};
  qa_guest_processor_state cpu;
  qa_create_guest_processor_state(&cpu, &init);
  qa_processor_flags_set(&cpu.flags, QA_FLAG_CARRY, true);
  CHECK(qa_processor_flags_value(&cpu.flags) == 3);
  CHECK(qa_integer_register_file_read(&cpu.registers, QA_REG_RSP, 64, false) == 0x20000);
  CHECK(qa_wrap_guest_pointer(UINT64_MAX, 4) == 0xffffffffull);
  CHECK(qa_signed_guest_pointer(0xffffffffull, 4) == -1);
  CHECK(qa_add_guest_pointer(0xffffffffull, 1, 4) == 0);
}

static void test_core_memory(void) {
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:guest", 8);
  uint8_t initial[2] = {9, 8};
  qa_guest_core_address base = smap(&mem, 0xf123456789abc000ull, 64, QA_PERM_READ_WRITE, initial, 2);
  initial[0] = 255;
  CHECK(qa_sparse_read_u8(&mem, base) == 9);
  uint8_t *whole = qa_sparse_borrow(&mem, base, 64);
  uint8_t *alias = qa_sparse_borrow(&mem, qa_sparse_offset(&mem, base, 4), 8);
  whole[4] = 0; whole[5] = 0; whole[6] = 0x80; whole[7] = 0x3f; /* 1.0f */
  float fv;
  memcpy(&fv, alias, 4);
  CHECK(fv == 1.0f);
  alias[2] = 0; alias[3] = 0x40; /* 2.0f */
  CHECK(qa_sparse_read_f32(&mem, qa_sparse_offset(&mem, base, 4)) == 2.0);
  qa_guest_core_address dst = qa_sparse_offset(&mem, base, 24);
  qa_sparse_write_pointer(&mem, dst, true, qa_sparse_offset(&mem, base, 63));
  qa_guest_core_address got;
  CHECK(qa_sparse_read_pointer(&mem, dst, &got) && got.byte_offset == 0xf123456789abc03full);
  qa_sparse_write_pointer(&mem, dst, false, A(0));
  CHECK(!qa_sparse_read_pointer(&mem, dst, &got));
  qa_guest_mapping maps[8];
  size_t n = qa_sparse_mappings(&mem, maps, 8);
  size_t total = 0;
  for (size_t i = 0; i < n; i++) total += maps[i].byte_length;
  CHECK(total == 64);
  qa_guest_core_address boundary = smap(&mem, 0x1ffffffffffffeull, 4, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u32(&mem, boundary, 0x12345678);
  CHECK(qa_sparse_read_u8(&mem, qa_sparse_offset(&mem, boundary, 3)) == 0x12);
  CHECK_ABORTS((void)qa_sparse_read_u8(&mem, qa_sparse_offset(&mem, boundary, 4)));
  qa_sparse_guest_memory_destroy(&mem);

  /* Cross-mapping accesses + atomic reject. */
  mem_new(&mem, "test:guest", 4);
  qa_guest_core_address start = smap(&mem, 0x1000, 3, QA_PERM_READ_WRITE, NULL, 0);
  qa_guest_core_address tail = smap(&mem, 0x1003, 3, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u32(&mem, qa_sparse_offset(&mem, start, 1), 0x12345678);
  uint8_t six[6];
  qa_sparse_copy(&mem, start, six, 6);
  static const uint8_t want6[6] = {0, 0x78, 0x56, 0x34, 0x12, 0};
  CHECK(memcmp(six, want6, 6) == 0);
  CHECK(qa_sparse_read_u32(&mem, qa_sparse_offset(&mem, start, 1)) == 0x12345678u);
  CHECK_ABORTS((void)qa_sparse_borrow(&mem, start, 6));
  qa_sparse_protect(&mem, tail, 3, QA_PERM_READ);
  uint8_t fill99[6];
  memset(fill99, 99, 6);
  CHECK_ABORTS(qa_sparse_write(&mem, start, fill99, 6));
  CHECK(qa_sparse_read_u8(&mem, start) == 0);
  uint8_t fb[3];
  qa_sparse_protect(&mem, start, 3, QA_PERM_EXECUTE);
  qa_sparse_fetch(&mem, start, fb, 3);
  CHECK(fb[0] == 0 && fb[1] == 0x78 && fb[2] == 0x56);
  CHECK_ABORTS(qa_sparse_copy(&mem, start, fb, 1));
  qa_sparse_guest_memory_destroy(&mem);

  /* Pointer creation vs wrapping; allocation reuse. */
  mem_new(&mem, "test:guest", 4);
  qa_guest_core_address tmp;
  CHECK(!qa_sparse_pointer(&mem, 0, &tmp));
  CHECK_ABORTS((void)qa_sparse_pointer(&mem, 0x100000000ull, &tmp));
  qa_guest_core_address last = smap(&mem, 0xffffffffull, 1, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u8(&mem, last, 7);
  CHECK(qa_sparse_read_u8(&mem, last) == 7);
  CHECK_ABORTS((void)qa_sparse_offset(&mem, last, 1));
  qa_guest_allocation_options ao;
  memset(&ao, 0, sizeof ao);
  ao.byte_length = 32; ao.alignment = 32;
  qa_guest_core_address block = qa_sparse_allocate(&mem, &ao);
  qa_sparse_unmap(&mem, block, 32);
  CHECK(qa_sparse_allocate(&mem, &ao).byte_offset == block.byte_offset);
  qa_sparse_guest_memory_destroy(&mem);

  /* Scalar encodings + cross-mapping fault atomicity + fetch sequence. */
  mem_new(&mem, "test:guest", 8);
  qa_guest_core_address b0 = smap(&mem, 0x10000, 4, QA_PERM_READ_WRITE, NULL, 0);
  qa_guest_core_address b1 = smap(&mem, 0x10004, 12, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u8(&mem, b0, 257);
  CHECK(qa_sparse_read_u8(&mem, b0) == 1);
  qa_sparse_write_i16(&mem, b0, -32769);
  CHECK(qa_sparse_read_u16(&mem, b0) == 0x7fff);
  qa_sparse_write_u32(&mem, b0, 0xffffffffu);
  CHECK(qa_sparse_read_u32(&mem, b0) == 0xffffffffu);
  qa_sparse_write_u64_words(&mem, b0, 0xffffffffu, 0x12345678u);
  uint32_t lo = 0, hi = 0;
  qa_sparse_read_u64_words(&mem, b0, &lo, &hi);
  CHECK(lo == 0xffffffffu && hi == 0x12345678u);
  CHECK(qa_sparse_read_u64(&mem, b0) == 0x12345678ffffffffull);
  qa_sparse_write_f32(&mem, b0, -0.0);
  CHECK(qa_sparse_read_u32(&mem, b0) == 0x80000000u);
  uint8_t before[16];
  qa_sparse_copy(&mem, b0, before, 16);
  qa_sparse_protect(&mem, b1, 12, QA_PERM_READ);
  CHECK_ABORTS(qa_sparse_write_u64(&mem, b0, 42));
  uint8_t after[16];
  qa_sparse_copy(&mem, b0, after, 16);
  CHECK(memcmp(before, after, 16) == 0);
  qa_sparse_guest_memory_destroy(&mem);

  mem_new(&mem, "test:guest", 8);
  static const uint8_t code3[3] = {1, 2, 3};
  qa_guest_core_address cb = smap(&mem, 0x1000, 3, QA_PERM_READ_EXECUTE, code3, 3);
  qa_guest_core_address ca = qa_sparse_map_alias(&mem, 0x2000, 3, QA_PERM_READ_WRITE, NULL, cb);
  qa_guest_fetch_sequence seq;
  qa_sparse_fetch_sequence_init(&seq, &mem, cb.byte_offset);
  CHECK(qa_guest_fetch_sequence_next(&seq) == 1);
  qa_sparse_write_u8(&mem, qa_sparse_offset(&mem, ca, 1), 9);
  CHECK(qa_guest_fetch_sequence_next(&seq) == 9);
  qa_sparse_protect(&mem, cb, 3, QA_PERM_READ);
  CHECK_ABORTS((void)qa_guest_fetch_sequence_next(&seq));
  qa_sparse_protect(&mem, cb, 3, QA_PERM_EXECUTE);
  CHECK(qa_guest_fetch_sequence_next(&seq) == 3);
  CHECK(qa_sparse_fetch_byte(&mem, cb.byte_offset) == 1);
  CHECK_ABORTS((void)qa_sparse_fetch_byte(&mem, 0));
  qa_sparse_guest_memory_destroy(&mem);
}

/* Observer capture shared by alias/range cases. */
typedef struct obs_log {
  qa_sparse_guest_memory *mem;
  qa_guest_core_address base;
  int values[8];
  int count;
  qa_guest_written_range ranges[4][2];
  size_t range_counts[4];
} obs_log;
static void obs_i32(void *ctx, const qa_guest_written_range *r, size_t n) {
  obs_log *o = (obs_log *)ctx;
  (void)r; (void)n;
  if (o->count < 8) o->values[o->count++] = qa_sparse_read_i32(o->mem, o->base);
}
static void obs_ranges(void *ctx, const qa_guest_written_range *r, size_t n) {
  obs_log *o = (obs_log *)ctx;
  if (o->count < 4) {
    o->range_counts[o->count] = n;
    for (size_t i = 0; i < n && i < 2; i++) o->ranges[o->count][i] = r[i];
  }
  o->count++;
}
static void test_core_observers_alias(void) {
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:guest", 8);
  qa_guest_allocation_options ao;
  memset(&ao, 0, sizeof ao);
  ao.byte_length = 16;
  qa_guest_core_address base = qa_sparse_allocate(&mem, &ao);
  qa_guest_core_address alias = qa_sparse_map_alias(&mem, 0x200000, 16, QA_PERM_READ_WRITE, NULL, base);
  obs_log log;
  memset(&log, 0, sizeof log);
  log.mem = &mem; log.base = base;
  qa_guest_write_observer_handle h = qa_sparse_observe_writes(&mem, base, 4, obs_i32, &log);
  qa_sparse_write_i32(&mem, qa_sparse_offset(&mem, base, 4), 9);
  qa_sparse_write_i32(&mem, alias, 3);
  CHECK(log.count == 1 && log.values[0] == 3);
  qa_sparse_unobserve_writes(&mem, h);
  qa_sparse_write_i32(&mem, alias, 8);
  CHECK(log.count == 1);
  qa_sparse_guest_memory_destroy(&mem);

  mem_new(&mem, "test:guest", 8);
  qa_guest_core_address first = smap(&mem, 0x10000, 8, QA_PERM_READ_WRITE, NULL, 0);
  qa_guest_core_address second = smap(&mem, 0x10008, 8, QA_PERM_READ_WRITE, NULL, 0);
  qa_guest_core_address al2 = qa_sparse_map_alias(&mem, 0x20000, 8, QA_PERM_READ_WRITE, NULL, second);
  memset(&log, 0, sizeof log);
  log.mem = &mem;
  qa_guest_write_observer_handle h2 =
      qa_sparse_observe_writes(&mem, qa_sparse_offset(&mem, first, 4), 8, obs_ranges, &log);
  qa_sparse_write_u8(&mem, al2, 9);
  CHECK(log.count == 1 && log.range_counts[0] == 1 && log.ranges[0][0].byte_offset == 4 &&
        log.ranges[0][0].byte_length == 1);
  static const uint8_t w4[4] = {2, 3, 9, 4};
  qa_sparse_write(&mem, qa_sparse_offset(&mem, first, 6), w4, 4);
  CHECK(log.count == 2 && log.range_counts[1] == 2 && log.ranges[1][0].byte_offset == 2 &&
        log.ranges[1][0].byte_length == 2 && log.ranges[1][1].byte_offset == 4 &&
        log.ranges[1][1].byte_length == 2);
  qa_sparse_unobserve_writes(&mem, h2);
  qa_sparse_guest_memory_destroy(&mem);

  /* Vector read + alias + protect fault retains (managed). */
  qa_sparse_guest_memory_options opts;
  memset(&opts, 0, sizeof opts);
  opts.module = mod("test:guest");
  opts.pointer_bytes = 8;
  qa_sparse_guest_memory_init_managed(&mem, &opts);
  qa_guest_core_address v = smap(&mem, 0x1000, 12, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_f32(&mem, v, -0.0);
  qa_sparse_write_f32(&mem, qa_sparse_offset(&mem, v, 4), 1.5);
  qa_sparse_write_f32(&mem, qa_sparse_offset(&mem, v, 8), -2.25);
  qa_vec3 vec = qa_sparse_read_f32_vector(&mem, v);
  CHECK(signbit(vec.x) && vec.x == 0 && vec.y == 1.5f && vec.z == -2.25f);
  qa_guest_core_address va = qa_sparse_map_alias(&mem, 0x2000, 4, QA_PERM_READ_WRITE, NULL,
                                            qa_sparse_offset(&mem, v, 8));
  float f42 = 42.5f;
  memcpy(qa_sparse_borrow(&mem, va, 4), &f42, 4);
  vec = qa_sparse_read_f32_vector(&mem, v);
  CHECK(vec.z == 42.5f);
  qa_sparse_protect(&mem, qa_sparse_offset(&mem, v, 8), 4, QA_PERM_EXECUTE);
  CHECK_ABORTS((void)qa_sparse_read_f32_vector(&mem, v));
  qa_sparse_guest_memory_destroy(&mem);
}

/* Callback table: nested invoke, revocation, checkpoint/restore. */
typedef struct cb_env {
  qa_sparse_guest_memory *mem;
  qa_guest_callback_table *table;
  qa_guest_core_address state;
  qa_guest_core_address nested_address;
  qa_guest_core_call_context context;
} cb_env;
static qa_call_result cb_nested(void *ctx, const qa_guest_core_call_context *c,
                                const qa_call_value *a, size_t n) {
  cb_env *e = (cb_env *)ctx;
  (void)c; (void)a; (void)n;
  qa_sparse_write_u32(e->mem, e->state, qa_sparse_read_u32(e->mem, e->state) + 7);
  return qa_call_void();
}
static qa_call_result cb_outer(void *ctx, const qa_guest_core_call_context *c,
                               const qa_call_value *a, size_t n) {
  cb_env *e = (cb_env *)ctx;
  (void)a; (void)n;
  qa_sparse_write_u32(e->mem, e->state, 10);
  qa_guest_core_call_context inner = e->context;
  inner.parent = c;
  (void)qa_guest_callback_table_invoke(e->table, e->nested_address, &inner, NULL, 0);
  CHECK(qa_sparse_read_u32(e->mem, e->state) == 17);
  return qa_call_void();
}
static qa_call_result cb_noop(void *ctx, const qa_guest_core_call_context *c,
                              const qa_call_value *a, size_t n) {
  (void)ctx; (void)c; (void)a; (void)n;
  return qa_call_void();
}
static const qa_guest_host_callback *cb_resolve(void *ctx, qa_callback_id id) {
  qa_guest_host_callback **pair = (qa_guest_host_callback **)ctx;
  if (strcmp(id, "test:nested") == 0) return pair[0];
  return pair[1];
}
static void test_core_callbacks(void) {
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:guest", 4);
  qa_guest_callback_table table;
  qa_guest_callback_table_init(&table, &mem);
  qa_guest_allocation_options ao;
  memset(&ao, 0, sizeof ao);
  ao.byte_length = 4;
  cb_env env;
  memset(&env, 0, sizeof env);
  env.mem = &mem; env.table = &table;
  env.state = qa_sparse_allocate(&mem, &ao);
  qa_guest_call_signature sig;
  memset(&sig, 0, sizeof sig);
  sig.abi.kind = "windows-i386"; sig.abi.image = "pe32"; sig.abi.pointer_bytes = 4; sig.abi.call = "cdecl";
  sig.result_is_void = true;
  qa_guest_host_callback nested;
  memset(&nested, 0, sizeof nested);
  nested.id = "test:nested"; nested.signature = sig; nested.ctx = &env; nested.invoke = cb_nested;
  qa_guest_host_callback outer;
  memset(&outer, 0, sizeof outer);
  outer.id = "test:outer"; outer.signature = sig; outer.ctx = &env; outer.invoke = cb_outer;
  env.nested_address = qa_guest_callback_table_bind(&table, &nested);
  memset(&env.context, 0, sizeof env.context);
  env.context.module = mod("test:guest");
  env.context.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST;
  env.context.callback_module = mod("test:guest");
  env.context.callback_address = env.nested_address;
  env.context.callback_abi = sig.abi;
  qa_guest_core_address outer_address = qa_guest_callback_table_bind(&table, &outer);
  (void)qa_guest_callback_table_invoke(&table, outer_address, &env.context, NULL, 0);
  qa_guest_callback_table_unbind(&table, "test:nested");
  CHECK_ABORTS((void)qa_guest_callback_table_invoke(&table, env.nested_address, &env.context, NULL, 0));
  CHECK(qa_guest_callback_table_bind(&table, &nested).byte_offset == env.nested_address.byte_offset);
  qa_guest_callback_table_unbind(&table, "test:outer");
  size_t saved_count = 0;
  qa_saved_host_callback_address *saved = qa_guest_callback_table_checkpoint(&table, &saved_count);
  CHECK(saved_count == 2);
  qa_guest_memory_snapshot snap;
  CHECK(qa_sparse_checkpoint(&mem, &snap));
  qa_sparse_guest_memory rmem;
  qa_guest_module_ref m = mod("test:guest");
  CHECK(qa_sparse_guest_memory_restore(&rmem, &m, &snap));
  qa_guest_host_callback r_nested = nested;
  r_nested.invoke = cb_noop;
  qa_guest_host_callback *pair[2] = {&r_nested, &outer};
  qa_guest_callback_table rtable;
  qa_guest_callback_table_restore(&rtable, &rmem, saved, saved_count, cb_resolve, pair);
  CHECK(qa_guest_callback_table_resolve(&rtable, at(&rmem, env.nested_address.byte_offset)) == &r_nested);
  qa_guest_core_address r_outer;
  CHECK(qa_guest_callback_table_address(&rtable, "test:outer", &r_outer));
  CHECK_ABORTS((void)qa_guest_callback_table_resolve(&rtable, r_outer));
  free(saved);
  qa_guest_memory_snapshot_free(&snap);
  qa_guest_callback_table_destroy(&rtable);
  qa_sparse_guest_memory_destroy(&rmem);
  qa_guest_callback_table_destroy(&table);
  qa_sparse_guest_memory_destroy(&mem);
}

/* ---------- x86/cpu.test.ts ---------- */
typedef struct i386_machine {
  qa_sparse_guest_memory mem;
  qa_guest_processor_state state;
  qa_i386_cpu cpu;
  qa_guest_core_address ret;
} i386_machine;
static void i386_init(i386_machine *m, const uint8_t *code, size_t n) {
  mem_new(&m->mem, "test:i386", 4);
  smap(&m->mem, 0x1000, n, QA_PERM_READ_EXECUTE, code, n);
  smap(&m->mem, 0x3000, 0x1000, QA_PERM_READ_WRITE, NULL, 0);
  smap(&m->mem, 0x8000, 0x2000, QA_PERM_READ_WRITE, NULL, 0);
  qa_guest_processor_initial_state init = {QA_GUEST_I386, 0x1000, 0x9000, 2, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&m->state, &init);
  m->ret = qa_x86_guest_address(&m->mem, 0x7000, QA_ACCESS_READ, 1);
  qa_sparse_write_u32(&m->mem, qa_x86_guest_address(&m->mem, 0x9000, QA_ACCESS_WRITE, 4), 0x7000);
  qa_i386_cpu_init(&m->cpu, &m->state, &m->mem, NULL, NULL, NULL, NULL);
}
static void i386_fini(i386_machine *m) { qa_sparse_guest_memory_destroy(&m->mem); }
static uint64_t rr32(i386_machine *m, qa_guest_register r) {
  return qa_integer_register_file_read(&m->state.registers, r, 32, false);
}
static void wr32(i386_machine *m, qa_guest_register r, uint64_t v) {
  qa_integer_register_file_write(&m->state.registers, r, 32, v, false);
}
static qa_guest_core_address ga32(i386_machine *m, uint64_t v) {
  return qa_x86_guest_address(&m->mem, v, QA_ACCESS_READ, 1);
}
static bool host5000(void *ctx, qa_guest_core_address a) { (void)ctx; return a.byte_offset == 0x5000; }

static void test_x86_cpu(void) {
  i386_machine m;
  static const uint8_t c1[] = {0xb8, 0x78, 0x56, 0x34, 0x12, 0xb4, 0xab, 0x66, 0xb8, 0xef, 0xbe, 0x0f, 0xbe, 0xc8, 0xc3};
  i386_init(&m, c1, sizeof c1);
  CHECK(qa_i386_cpu_run(&m.cpu, 20, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RAX) == 0x1234beefull);
  CHECK(rr32(&m, QA_REG_RCX) == 0xffffffefull);
  CHECK(rr32(&m, QA_REG_RSP) == 0x9004);
  i386_fini(&m);

  static const uint8_t c2[] = {0xb0, 0x7f, 0x04, 0x01};
  i386_init(&m, c2, sizeof c2);
  CHECK(qa_i386_cpu_run(&m.cpu, 2, false, A(0)).kind == QA_STOP_BUDGET);
  CHECK(qa_integer_register_file_read(&m.state.registers, QA_REG_RAX, 8, false) == 0x80);
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_OVERFLOW));
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_AUXILIARY_CARRY));
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_SIGN));
  CHECK(!qa_processor_flags_get(&m.state.flags, QA_FLAG_CARRY));
  CHECK(!qa_processor_flags_get(&m.state.flags, QA_FLAG_PARITY));
  i386_fini(&m);
  static const uint8_t c2b[] = {0xf9, 0xb8, 0xff, 0xff, 0xff, 0xff, 0x40, 0xc3};
  i386_init(&m, c2b, sizeof c2b);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RAX) == 0);
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_CARRY));
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_ZERO));
  i386_fini(&m);

  static const uint8_t c3[] = {0xb0, 1, 0xf8, 0xc0, 0xc0, 8, 0xd0, 0xd0, 0xc3};
  i386_init(&m, c3, sizeof c3);
  CHECK(qa_i386_cpu_run(&m.cpu, 3, false, A(0)).kind == QA_STOP_BUDGET);
  CHECK(qa_integer_register_file_read(&m.state.registers, QA_REG_RAX, 8, false) == 1);
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_CARRY));
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(qa_integer_register_file_read(&m.state.registers, QA_REG_RAX, 8, false) == 3);
  CHECK(!qa_processor_flags_get(&m.state.flags, QA_FLAG_CARRY));
  i386_fini(&m);
  static const uint8_t c3b[] = {0xb9, 3, 0, 0, 0, 0xb8, 0, 0, 0, 0, 0x40, 0xe2, 0xfd, 0xc3};
  i386_init(&m, c3b, sizeof c3b);
  CHECK(qa_i386_cpu_run(&m.cpu, 20, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RAX) == 3 && rr32(&m, QA_REG_RCX) == 0);
  i386_fini(&m);
  static const uint8_t c3c[] = {0x66, 0xb8, 0, 0x80, 0x66, 0xd1, 0xf8, 0xc3};
  i386_init(&m, c3c, sizeof c3c);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(qa_integer_register_file_read(&m.state.registers, QA_REG_RAX, 16, false) == 0xc000);
  CHECK(!qa_processor_flags_get(&m.state.flags, QA_FLAG_OVERFLOW));
  CHECK(qa_processor_flags_get(&m.state.flags, QA_FLAG_SIGN));
  i386_fini(&m);

  static const uint8_t c4[] = {0x8b, 0x44, 0x8b, 0x10, 0x67, 0x66, 0x8b, 0x52, 0x04, 0x64, 0x8b, 0x35, 0x20, 0, 0, 0, 0xc3};
  i386_init(&m, c4, sizeof c4);
  wr32(&m, QA_REG_RBX, 0x3000); wr32(&m, QA_REG_RCX, 3);
  wr32(&m, QA_REG_RBP, 0x3000); wr32(&m, QA_REG_RSI, 0x40);
  m.state.segments[QA_SEG_FS].base = 0x3000;
  qa_sparse_write_u32(&m.mem, ga32(&m, 0x301c), 0x11223344);
  qa_sparse_write_u16(&m.mem, ga32(&m, 0x3044), 0xabcd);
  qa_sparse_write_u32(&m.mem, ga32(&m, 0x3020), 0x55667788);
  CHECK(qa_i386_cpu_run(&m.cpu, 20, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RAX) == 0x11223344ull);
  CHECK(rr32(&m, QA_REG_RDX) == 0xabcd);
  CHECK(rr32(&m, QA_REG_RSI) == 0x55667788ull);
  i386_fini(&m);
  static const uint8_t c4b[] = {0x8d, 0x14, 0x08, 0xc3};
  i386_init(&m, c4b, sizeof c4b);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RDX) == 0);
  i386_fini(&m);

  static const uint8_t c5[] = {0xe8, 0xfb, 0x3f, 0, 0, 0x83, 0xc0, 3, 0xc3};
  i386_init(&m, c5, sizeof c5);
  qa_i386_cpu hcpu;
  qa_i386_cpu_init(&hcpu, &m.state, &m.mem, host5000, NULL, NULL, NULL);
  CHECK(qa_i386_cpu_run(&hcpu, 10, true, m.ret).kind == QA_STOP_HOST_CALL);
  CHECK(m.state.instruction_pointer == 0x5000);
  CHECK(rr32(&m, QA_REG_RSP) == 0x8ffc);
  uint32_t saved = qa_sparse_read_u32(&m.mem, ga32(&m, 0x8ffc));
  CHECK(saved == 0x1005);
  wr32(&m, QA_REG_RAX, 39); wr32(&m, QA_REG_RSP, 0x9000);
  m.state.instruction_pointer = saved;
  CHECK(qa_i386_cpu_run(&hcpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RAX) == 42);
  i386_fini(&m);

  static const uint8_t c6[] = {0x8f, 0x04, 0x24};
  i386_init(&m, c6, sizeof c6);
  qa_sparse_write_u32(&m.mem, ga32(&m, 0x9000), 0x12345678);
  CHECK(qa_i386_cpu_run(&m.cpu, 1, false, A(0)).kind == QA_STOP_BUDGET);
  CHECK(rr32(&m, QA_REG_RSP) == 0x9004);
  CHECK(qa_sparse_read_u32(&m.mem, ga32(&m, 0x9004)) == 0x12345678u);
  i386_fini(&m);

  static const uint8_t c7[] = {0xf3, 0xa4, 0xc3};
  i386_init(&m, c7, sizeof c7);
  static const uint8_t src5[5] = {7, 8, 9, 10, 11};
  qa_sparse_write(&m.mem, ga32(&m, 0x3000), src5, 5);
  wr32(&m, QA_REG_RSI, 0x3000); wr32(&m, QA_REG_RDI, 0x3001); wr32(&m, QA_REG_RCX, 4);
  CHECK(qa_i386_cpu_run(&m.cpu, 2, true, m.ret).kind == QA_STOP_BUDGET);
  CHECK(m.state.instruction_pointer == 0x1000);
  CHECK(rr32(&m, QA_REG_RCX) == 2);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  uint8_t got5[5];
  qa_sparse_copy(&m.mem, ga32(&m, 0x3000), got5, 5);
  static const uint8_t want5[5] = {7, 7, 7, 7, 7};
  CHECK(memcmp(got5, want5, 5) == 0);
  i386_fini(&m);

  static const uint8_t c8[] = {0xfd, 0xf3, 0xa6, 0xc3};
  i386_init(&m, c8, sizeof c8);
  static const uint8_t a3[3] = {1, 2, 3}, b3[3] = {1, 9, 3};
  qa_sparse_write(&m.mem, ga32(&m, 0x3000), a3, 3);
  qa_sparse_write(&m.mem, ga32(&m, 0x3010), b3, 3);
  wr32(&m, QA_REG_RSI, 0x3002); wr32(&m, QA_REG_RDI, 0x3012); wr32(&m, QA_REG_RCX, 3);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RCX) == 1 && rr32(&m, QA_REG_RSI) == 0x3000);
  CHECK(!qa_processor_flags_get(&m.state.flags, QA_FLAG_ZERO));
  i386_fini(&m);

  static const uint8_t c9[] = {0xf0, 0x0f, 0xb1, 0x0b, 0xf0, 0x0f, 0xc1, 0x13, 0xc3};
  i386_init(&m, c9, sizeof c9);
  wr32(&m, QA_REG_RBX, 0x3000); wr32(&m, QA_REG_RAX, 5); wr32(&m, QA_REG_RCX, 20); wr32(&m, QA_REG_RDX, 3);
  qa_sparse_write_u32(&m.mem, ga32(&m, 0x3000), 5);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(qa_sparse_read_u32(&m.mem, ga32(&m, 0x3000)) == 23);
  CHECK(rr32(&m, QA_REG_RDX) == 20);
  i386_fini(&m);

  static const uint8_t c10[] = {0xb8, 0xf9, 0xff, 0xff, 0xff, 0x99, 0xb9, 3, 0, 0, 0, 0xf7, 0xf9, 0xc3};
  i386_init(&m, c10, sizeof c10);
  CHECK(qa_i386_cpu_run(&m.cpu, 20, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(rr32(&m, QA_REG_RAX) == 0xfffffffeull && rr32(&m, QA_REG_RDX) == 0xffffffffull);
  i386_fini(&m);
  static const uint8_t c10b[] = {0xf7, 0xf1};
  i386_init(&m, c10b, sizeof c10b);
  wr32(&m, QA_REG_RAX, 17);
  qa_guest_execution_stop st = qa_i386_cpu_run(&m.cpu, 1, false, A(0));
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_PROCESSOR && st.exception.vector == 0);
  CHECK(m.state.instruction_pointer == 0x1000 && rr32(&m, QA_REG_RAX) == 17);
  i386_fini(&m);

  static const uint8_t c11[] = {0xf0, 0x01, 0xc0};
  i386_init(&m, c11, sizeof c11);
  st = qa_i386_cpu_run(&m.cpu, 10, false, A(0));
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_PROCESSOR && st.exception.vector == 6);
  i386_fini(&m);
  static const uint8_t c11b[] = {0x90, 0x0f, 0xa2};
  i386_init(&m, c11b, sizeof c11b);
  st = qa_i386_cpu_run(&m.cpu, 10, false, A(0));
  CHECK(st.kind == QA_STOP_UNSUPPORTED && st.instructions == 1);
  CHECK(st.instruction_address.byte_offset == 0x1001);
  CHECK(st.instruction_bytes_len == 2 && st.instruction_bytes[0] == 0x0f && st.instruction_bytes[1] == 0xa2);
  i386_fini(&m);
  static const uint8_t c11c[] = {0x83, 0x03, 0x01};
  i386_init(&m, c11c, sizeof c11c);
  wr32(&m, QA_REG_RBX, 0x1000);
  uint64_t flags_before = qa_processor_flags_value(&m.state.flags);
  uint8_t regs_before[64];
  (void)qa_integer_register_file_checkpoint(&m.state.registers, regs_before, sizeof regs_before);
  CHECK(qa_i386_cpu_run(&m.cpu, 1, false, A(0)).kind == QA_STOP_EXCEPTION);
  uint8_t regs_after[64];
  (void)qa_integer_register_file_checkpoint(&m.state.registers, regs_after, sizeof regs_after);
  CHECK(memcmp(regs_before, regs_after, 64) == 0);
  CHECK(qa_processor_flags_value(&m.state.flags) == flags_before);
  CHECK(m.state.instruction_pointer == 0x1000);
  i386_fini(&m);

  static const uint8_t c12[] = {0xd9, 0xe8, 0xd9, 0xe8, 0xde, 0xc1, 0xd9, 0x1b, 0xc3};
  i386_init(&m, c12, sizeof c12);
  wr32(&m, QA_REG_RBX, 0x3000);
  CHECK(qa_i386_cpu_run(&m.cpu, 10, true, m.ret).kind == QA_STOP_RETURN);
  CHECK(qa_sparse_read_f32(&m.mem, ga32(&m, 0x3000)) == 2.0);
  CHECK(m.state.x87.tag_word == 0xffff);
  i386_fini(&m);
}

/* ---------- x64/cpu.test.ts (non-managed cases) ---------- */
typedef struct x64_fix {
  qa_sparse_guest_memory mem;
  qa_guest_processor_state state;
  qa_x64_cpu cpu;
} x64_fix;
static void x64_init(x64_fix *f, const uint8_t *code, size_t n, uint64_t start) {
  mem_new(&f->mem, "test:x64", 8);
  smap(&f->mem, start, n, QA_PERM_READ_EXECUTE, code, n);
  smap(&f->mem, 0x30000 - 0x1000, 0x1008, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u64(&f->mem, at(&f->mem, 0x30000), 0x40000);
  qa_guest_processor_initial_state init = {QA_GUEST_X86_64, start, 0x30000, 2, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&f->state, &init);
  qa_x64_cpu_init(&f->cpu, &f->state, &f->mem, NULL, NULL, NULL);
}
static qa_guest_execution_stop x64_run(x64_fix *f, int64_t budget) {
  return qa_x64_cpu_run(&f->cpu, budget, true, A(0x40000));
}
static uint64_t xrr(x64_fix *f, qa_guest_register r, int w) {
  return qa_integer_register_file_read(&f->state.registers, r, w, false);
}
static void xwr(x64_fix *f, qa_guest_register r, uint64_t v) {
  qa_integer_register_file_write(&f->state.registers, r, 64, v, false);
}
static bool host60000(void *ctx, qa_guest_core_address a) { (void)ctx; return a.byte_offset == 0x60000; }

static void test_x64_cpu(void) {
  x64_fix f;
  static const uint8_t cpuid1[] = {0xb8, 1, 0, 0, 0, 0x0f, 0xa2, 0xc3};
  x64_init(&f, cpuid1, sizeof cpuid1, 0x10000);
  qa_processor_flags_set_value(&f.state.flags, 0x8d7);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RDX, 64) == 0x06008101ull);
  CHECK(xrr(&f, QA_REG_RCX, 64) == 0);
  CHECK(qa_processor_flags_value(&f.state.flags) == 0x8d7);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t cpuid7[] = {0xb8, 7, 0, 0, 0, 0x0f, 0xa2, 0xc3};
  x64_init(&f, cpuid7, sizeof cpuid7, 0x10000);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RBX, 64) == 0);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t cpuid_lock[] = {0xf0, 0x0f, 0xa2};
  x64_init(&f, cpuid_lock, sizeof cpuid_lock, 0x10000);
  qa_guest_execution_stop st = x64_run(&f, 100);
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_PROCESSOR && st.exception.vector == 6);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t movw[] = {0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb4, 0x12, 0x66, 0xb8, 0x34, 0x56, 0xc3};
  x64_init(&f, movw, sizeof movw, 0x10000);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 0xffffffffffff5634ull);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t movd[] = {0xb8, 0xef, 0xcd, 0xab, 0x89, 0xc3};
  x64_init(&f, movd, sizeof movd, 0x10000);
  xwr(&f, QA_REG_RAX, UINT64_MAX);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 0x89abcdefull);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t rex1[] = {0x40, 0xb4, 0x80};
  x64_init(&f, rex1, sizeof rex1, 0x10000);
  CHECK(x64_run(&f, 1).kind == QA_STOP_BUDGET);
  CHECK(xrr(&f, QA_REG_RSP, 64) == 0x30080);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t rex2[] = {0x40, 0x66, 0xb4, 0x12, 0xc3};
  x64_init(&f, rex2, sizeof rex2, 0x10000);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 0x1200);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t sib[] = {0x4f, 0x89, 0x4c, 0xa5, 0xf8, 0x4f, 0x8b, 0x54, 0xa5, 0xf8, 0xc3};
  x64_init(&f, sib, sizeof sib, 0x10000);
  smap(&f.mem, 0xffff800000010000ull, 64, QA_PERM_READ_WRITE, NULL, 0);
  xwr(&f, QA_REG_R13, 0xffff800000010000ull);
  xwr(&f, QA_REG_R12, 4);
  xwr(&f, QA_REG_R9, 0xfedcba9876543210ull);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(r64(&f.mem, 0xffff800000010008ull) == 0xfedcba9876543210ull);
  CHECK(xrr(&f, QA_REG_R10, 64) == 0xfedcba9876543210ull);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t riprel[] = {0xc7, 0x05, 0xf6, 0xff, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12, 0xc3};
  x64_init(&f, riprel, sizeof riprel, 0x10000);
  smap(&f.mem, 0x20000, 4, QA_PERM_READ_WRITE, NULL, 0);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(r32(&f.mem, 0x20000) == 0x12345678u);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t fslea[] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0x08, 0, 0, 0, 0x64, 0x48, 0x8d, 0x0c, 0x25, 0x08, 0, 0, 0, 0xc3};
  x64_init(&f, fslea, sizeof fslea, 0x10000);
  smap(&f.mem, 0x50000, 32, QA_PERM_READ_WRITE, NULL, 0);
  f.state.segments[QA_SEG_FS].base = 0x50000;
  qa_sparse_write_u64(&f.mem, at(&f.mem, 0x50008), 0x123456789abcdef0ull);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 0x123456789abcdef0ull);
  CHECK(xrr(&f, QA_REG_RCX, 64) == 8);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t carry[] = {0x48, 0x83, 0xc0, 1, 0x49, 0x83, 0xd0, 0, 0xc3};
  x64_init(&f, carry, sizeof carry, 0x10000);
  xwr(&f, QA_REG_RAX, UINT64_MAX);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 0 && xrr(&f, QA_REG_R8, 64) == 1);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t ovf[] = {0x48, 0x83, 0xc0, 1, 0xc3};
  x64_init(&f, ovf, sizeof ovf, 0x10000);
  xwr(&f, QA_REG_RAX, 0x7fffffffffffffffull);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(qa_processor_flags_get(&f.state.flags, QA_FLAG_OVERFLOW));
  CHECK(!qa_processor_flags_get(&f.state.flags, QA_FLAG_CARRY));
  CHECK(qa_processor_flags_get(&f.state.flags, QA_FLAG_SIGN));
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t loop[] = {0xb9, 3, 0, 0, 0, 0xe8, 1, 0, 0, 0, 0xc3, 0x48, 0x83, 0xc0, 2, 0xff, 0xc9, 0x75, 0xf8, 0xc3};
  x64_init(&f, loop, sizeof loop, 0x10000);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 6);
  CHECK(xrr(&f, QA_REG_RSP, 64) == 0x30008);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t icall[] = {0xff, 0xd0, 0xc3};
  x64_init(&f, icall, sizeof icall, 0x10000);
  xwr(&f, QA_REG_RAX, 0x60000);
  qa_x64_cpu hcpu;
  qa_x64_cpu_init(&hcpu, &f.state, &f.mem, host60000, NULL, NULL);
  CHECK(qa_x64_cpu_run(&hcpu, 10, true, A(0x40000)).kind == QA_STOP_HOST_CALL);
  CHECK(f.state.instruction_pointer == 0x60000);
  CHECK(xrr(&f, QA_REG_RSP, 64) == 0x30000 - 8);
  CHECK(r64(&f.mem, 0x30000 - 8) == 0x10002);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t muldiv[] = {0x48, 0xf7, 0xe1, 0x48, 0xf7, 0xf1, 0xc3};
  x64_init(&f, muldiv, sizeof muldiv, 0x10000);
  xwr(&f, QA_REG_RAX, UINT64_MAX);
  xwr(&f, QA_REG_RCX, 2);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == UINT64_MAX && xrr(&f, QA_REG_RDX, 64) == 0);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t idiv[] = {0x48, 0xf7, 0xf9};
  x64_init(&f, idiv, sizeof idiv, 0x10000);
  xwr(&f, QA_REG_RAX, 0x8000000000000000ull);
  xwr(&f, QA_REG_RDX, UINT64_MAX);
  xwr(&f, QA_REG_RCX, UINT64_MAX);
  st = x64_run(&f, 100);
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_PROCESSOR && st.exception.vector == 0);
  CHECK(f.state.instruction_pointer == 0x10000);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 0x8000000000000000ull);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t movsb[] = {0xf3, 0xa4, 0xc3};
  x64_init(&f, movsb, sizeof movsb, 0x10000);
  static const uint8_t five[5] = {1, 2, 3, 4, 5};
  smap(&f.mem, 0x50000, 8, QA_PERM_READ_WRITE, five, 5);
  xwr(&f, QA_REG_RSI, 0x50000); xwr(&f, QA_REG_RDI, 0x50001); xwr(&f, QA_REG_RCX, 4);
  CHECK(x64_run(&f, 2).kind == QA_STOP_BUDGET);
  CHECK(f.state.instruction_pointer == 0x10000 && xrr(&f, QA_REG_RCX, 64) == 2);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  uint8_t got5[5];
  qa_sparse_copy(&f.mem, at(&f.mem, 0x50000), got5, 5);
  static const uint8_t ones[5] = {1, 1, 1, 1, 1};
  CHECK(memcmp(got5, ones, 5) == 0);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t cmpsb[] = {0xf3, 0xa6, 0xc3};
  x64_init(&f, cmpsb, sizeof cmpsb, 0x10000);
  static const uint8_t seven[7] = {1, 2, 3, 0, 1, 9, 3};
  smap(&f.mem, 0x50000, 16, QA_PERM_READ_WRITE, seven, 7);
  xwr(&f, QA_REG_RSI, 0x50000); xwr(&f, QA_REG_RDI, 0x50004); xwr(&f, QA_REG_RCX, 3);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RCX, 64) == 1 && xrr(&f, QA_REG_RSI, 64) == 0x50002);
  CHECK(!qa_processor_flags_get(&f.state.flags, QA_FLAG_ZERO));
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t xadd[] = {0x48, 0x0f, 0xc1, 0x00, 0xc3};
  x64_init(&f, xadd, sizeof xadd, 0x10000);
  smap(&f.mem, 0x50000, 8, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u64(&f.mem, at(&f.mem, 0x50000), 7);
  xwr(&f, QA_REG_RAX, 0x50000);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 64) == 7);
  CHECK(r64(&f.mem, 0x50000) == 0x50007);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t wfault[] = {0x48, 0x83, 0x00, 1};
  x64_init(&f, wfault, sizeof wfault, 0x10000);
  static const uint8_t seven8[1] = {7};
  smap(&f.mem, 0x50000, 8, QA_PERM_READ, seven8, 1);
  xwr(&f, QA_REG_RAX, 0x50000);
  qa_processor_flags_set_value(&f.state.flags, 0x803);
  st = x64_run(&f, 100);
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_MEMORY);
  CHECK(st.exception.address.byte_offset == 0x50000 && st.exception.access == QA_ACCESS_WRITE);
  CHECK(qa_processor_flags_value(&f.state.flags) == 0x803);
  CHECK(f.state.instruction_pointer == 0x10000);
  CHECK(r64(&f.mem, 0x50000) == 7);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t badop[] = {0x0f, 0x05};
  x64_init(&f, badop, sizeof badop, 0x10000);
  st = x64_run(&f, 100);
  CHECK(st.kind == QA_STOP_UNSUPPORTED);
  CHECK(st.instruction_bytes_len == 2 && st.instruction_bytes[0] == 0x0f && st.instruction_bytes[1] == 0x05);
  CHECK(st.instruction_address.byte_offset == 0x10000);
  qa_sparse_guest_memory_destroy(&f.mem);
  static const uint8_t noncanon[] = {0x48, 0x8b, 0x00};
  x64_init(&f, noncanon, sizeof noncanon, 0x10000);
  xwr(&f, QA_REG_RAX, 0x0000800000000000ull);
  st = x64_run(&f, 100);
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_PROCESSOR && st.exception.vector == 13);
  CHECK(f.state.instruction_pointer == 0x10000);
  qa_sparse_guest_memory_destroy(&f.mem);

  static const uint8_t waitb[] = {0x9b, 0xc3};
  x64_init(&f, waitb, sizeof waitb, 0x10000);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  qa_sparse_guest_memory_destroy(&f.mem);
  x64_init(&f, waitb, sizeof waitb, 0x10000);
  f.state.x87.control_word &= ~1u;
  f.state.x87.status_word = 1;
  st = x64_run(&f, 100);
  CHECK(st.kind == QA_STOP_EXCEPTION && st.exception.kind == QA_EXCEPTION_PROCESSOR && st.exception.vector == 16);
  CHECK(f.state.instruction_pointer == 0x10000 && f.state.x87.status_word == 1);
  qa_sparse_guest_memory_destroy(&f.mem);

  /* Live operands / writable alias / remap (fresh-decode semantics). */
  static const uint8_t live[] = {0x8b, 0x43, 4, 0x83, 0xc0, 1, 0xc3};
  x64_init(&f, live, sizeof live, 0x10000);
  qa_guest_core_address data = smap(&f.mem, 0x50000, 16, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u32(&f.mem, qa_sparse_offset(&f.mem, data, 4), 10);
  qa_sparse_write_u32(&f.mem, qa_sparse_offset(&f.mem, data, 8), 20);
  xwr(&f, QA_REG_RBX, data.byte_offset);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 32) == 11);
  f.state.instruction_pointer = 0x10000;
  xwr(&f, QA_REG_RSP, 0x30000);
  xwr(&f, QA_REG_RBX, data.byte_offset + 4);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 32) == 21);
  qa_guest_core_address code = at(&f.mem, 0x10000);
  qa_guest_core_address calias = qa_sparse_map_alias(&f.mem, 0x60000, 7, QA_PERM_READ_WRITE, NULL, code);
  qa_sparse_borrow(&f.mem, calias, 7)[5] = 7;
  f.state.instruction_pointer = 0x10000;
  xwr(&f, QA_REG_RSP, 0x30000);
  xwr(&f, QA_REG_RBX, data.byte_offset);
  CHECK(x64_run(&f, 100).kind == QA_STOP_RETURN);
  CHECK(xrr(&f, QA_REG_RAX, 32) == 17);
  qa_sparse_guest_memory_destroy(&f.mem);

  /* Cursor immediates + canonical boundaries. */
  qa_guest_throw tc;
  qa_guest_throw_init(&tc, NULL);
  CHECK(qa_x64_canonical_address(&tc, 0) == 0);
  CHECK(qa_x64_canonical_address(&tc, (1ull << 47) - 1) == (1ull << 47) - 1);
  CHECK(qa_x64_canonical_address(&tc, 0xffff800000000000ull) == 0xffff800000000000ull);
  CHECK(qa_x64_canonical_address(&tc, UINT64_MAX) == UINT64_MAX);
  CHECK_ABORTS((void)qa_x64_canonical_address(&tc, 1ull << 47));
  CHECK_ABORTS((void)qa_x64_canonical_address(&tc, 0xffff7fffffffffffll));
  for (int w = 1; w <= 8; w *= 2) {
    uint8_t buf[9];
    buf[0] = 0x90;
    for (int i = 0; i < w; i++) buf[1 + i] = 0xff;
    x64_init(&f, buf, (size_t)(1 + w), 0x10000);
    qa_x64_decode_cursor cur;
    qa_x64_decode_cursor_init(&cur, &f.mem, &f.state, NULL);
    uint64_t expect = w == 8 ? UINT64_MAX : ((1ull << (w * 8)) - 1);
    CHECK(qa_x64_cursor_read_unsigned(&cur, w) == expect);
    CHECK(qa_x64_cursor_next_ip(&cur) == 0x10000 + (uint64_t)(w + 1));
    qa_sparse_guest_memory_destroy(&f.mem);
  }
  static const uint8_t sbuf[] = {0x90, 0xff, 0xff, 0xff, 0xff};
  x64_init(&f, sbuf, sizeof sbuf, 0x10000);
  qa_x64_decode_cursor cur;
  qa_x64_decode_cursor_init(&cur, &f.mem, &f.state, NULL);
  CHECK(qa_x64_cursor_read_signed(&cur, 4) == -1);
  qa_sparse_guest_memory_destroy(&f.mem);
}

/* ---------- abi/calls.test.ts (minus compat native-combat) ---------- */
static qa_abi_value_layout lay_scalar(qa_abi_storage s) {
  qa_abi_value_layout l;
  memset(&l, 0, sizeof l);
  l.storage = s;
  return l;
}
static qa_abi_value_layout lay_record(qa_abi_layout *layout, qa_abi_field_layout *fields,
                                      size_t nfields, int64_t len, int64_t align, int pb) {
  memset(layout, 0, sizeof *layout);
  layout->id = "test:record";
  layout->byte_length = len;
  layout->alignment = align;
  layout->pointer_bytes = pb;
  layout->fields = fields;
  layout->field_count = nfields;
  qa_abi_value_layout l;
  memset(&l, 0, sizeof l);
  l.is_aggregate = true;
  l.layout = *layout;
  return l;
}
typedef struct abi_fix {
  qa_sparse_guest_memory mem;
  test_ext ext_store;
  qa_mapped_memory_ext ext;
  qa_guest_processor_state state;
  qa_guest_cpu cpu;
  qa_x86_abi_adapter adapter;
} abi_fix;
static void abi_init(abi_fix *f, qa_guest_native_abi abi) {
  mem_new(&f->mem, "test:abi", (int)abi.pointer_bytes);
  static const uint8_t retb[1] = {0xc3};
  smap(&f->mem, 0x1000, 4096, QA_PERM_READ_EXECUTE, retb, 1);
  smap(&f->mem, 0x10000, 65536, QA_PERM_READ_WRITE, NULL, 0);
  make_ext(&f->ext, &f->ext_store, &f->mem);
  qa_guest_processor_initial_state init = {abi.pointer_bytes == 4 ? QA_GUEST_I386 : QA_GUEST_X86_64,
                                           0x1000, 0x20000, 2, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&f->state, &init);
  memset(&f->cpu, 0, sizeof f->cpu);
  f->cpu.state = &f->state;
  f->cpu.memory = &f->mem.base;
  f->cpu.memory_ext = &f->ext;
  f->cpu.module = mod("test:abi");
  qa_x86_abi_adapter_init(&f->adapter, abi);
}
static uint64_t abi_enter(abi_fix *f, const qa_guest_call_signature *sig,
                          const qa_call_value *args, size_t n) {
  qa_x86_abi_adapter_enter(&f->adapter, &f->cpu, qa_guest_pointer(&f->mem.base, 0x1000), sig, args, n,
                           qa_guest_pointer(&f->mem.base, 0x1001));
  return qa_integer_register_file_read(&f->state.registers, QA_REG_RSP,
                                       f->mem.base.pointer_bytes == 4 ? 32 : 64, false);
}
static qa_guest_call_signature mk_sig(qa_guest_native_abi abi, const qa_abi_value_layout *params,
                                      size_t n, bool void_result, qa_abi_value_layout result, bool variadic) {
  qa_guest_call_signature s;
  memset(&s, 0, sizeof s);
  s.abi = abi; s.parameters = params; s.parameter_count = n;
  s.result_is_void = void_result; s.result = result; s.variadic = variadic;
  return s;
}
static bool call_eq(const qa_call_value *a, const qa_call_value *b) {
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_CALL_VOID: return true;
    case QA_CALL_INT32: case QA_CALL_UINT32: case QA_CALL_FLOAT32: case QA_CALL_FLOAT64:
      return memcmp(&a->number, &b->number, sizeof a->number) == 0;
    case QA_CALL_INT64: return a->int64_value == b->int64_value;
    case QA_CALL_UINT64: return a->uint64_value == b->uint64_value;
    case QA_CALL_POINTER:
      return a->pointer_has == b->pointer_has && (!a->pointer_has || a->pointer.byte_offset == b->pointer.byte_offset);
    case QA_CALL_AGGREGATE:
      return a->bytes_len == b->bytes_len && memcmp(a->bytes, b->bytes, a->bytes_len) == 0;
  }
  return false;
}

static void test_abi_calls(void) {
  const qa_guest_native_abi win64 = {"windows-x86-64", "pe32+", 8, "microsoft-x64"};
  const qa_guest_native_abi win32 = {"windows-i386", "pe32", 4, "cdecl"};
  const qa_guest_native_abi lin64 = {"linux-x86-64", "elf64", 8, "system-v-x86-64"};
  const qa_guest_native_abi lin32 = {"linux-i386", "elf32", 4, "system-v-i386"};
  qa_abi_value_layout li32 = lay_scalar(QA_STORAGE_INT32);
  qa_abi_value_layout lf32 = lay_scalar(QA_STORAGE_FLOAT32);
  qa_abi_value_layout lf64 = lay_scalar(QA_STORAGE_FLOAT64);
  qa_abi_value_layout lptr = lay_scalar(QA_STORAGE_POINTER);

  /* Plan follows signature edits; variadic layouts separate. */
  {
    qa_abi_value_layout param = lay_scalar(QA_STORAGE_INT32);
    qa_abi_value_layout params[1] = {param};
    qa_guest_call_signature call = mk_sig(win64, params, 1, false, li32, false);
    qa_abi_call_plan plan;
    CHECK(qa_plan_guest_call(&plan, &call, NULL, 0));
    CHECK(plan.argument_count == 1 && plan.arguments[0].locations[0].kind == QA_LOCATION_INTEGER);
    qa_abi_call_plan_free(&plan);
    params[0].storage = QA_STORAGE_FLOAT32;
    CHECK(qa_plan_guest_call(&plan, &call, NULL, 0));
    CHECK(plan.arguments[0].locations[0].kind == QA_LOCATION_SSE);
    qa_abi_call_plan_free(&plan);
    params[0] = li32;
    CHECK(qa_plan_guest_call(&plan, &call, NULL, 0));
    CHECK(plan.arguments[0].locations[0].kind == QA_LOCATION_INTEGER);
    qa_abi_call_plan_free(&plan);
    qa_abi_value_layout vparams[1] = {lptr};
    qa_guest_call_signature variadic = mk_sig(win64, vparams, 1, true, li32, true);
    qa_abi_value_layout vlayouts[2] = {lptr, lf64};
    CHECK(qa_plan_guest_call(&plan, &variadic, vlayouts, 2));
    CHECK(plan.argument_count == 2);
    qa_abi_call_plan_free(&plan);
    qa_abi_field_layout fields[1] = {{"value", 0, QA_STORAGE_INT32, 1}};
    qa_abi_layout layout;
    qa_abi_value_layout agg = lay_record(&layout, fields, 1, 4, 4, 8);
    qa_abi_value_layout aparams[1] = {agg};
    qa_guest_call_signature acall = mk_sig(lin64, aparams, 1, false, li32, false);
    CHECK(qa_plan_guest_call(&plan, &acall, NULL, 0));
    CHECK(plan.arguments[0].locations[0].kind == QA_LOCATION_INTEGER);
    qa_abi_call_plan_free(&plan);
    aparams[0].layout.byte_length = 24;
    CHECK(qa_plan_guest_call(&plan, &acall, NULL, 0));
    CHECK(plan.arguments[0].locations[0].kind == QA_LOCATION_STACK);
    qa_abi_call_plan_free(&plan);
  }

  /* Microsoft x64 mixed positional args + shadow space. */
  {
    abi_fix f;
    abi_init(&f, win64);
    qa_abi_value_layout params[6] = {li32, lf64, li32, lf32, li32, lf32};
    qa_guest_call_signature call = mk_sig(win64, params, 6, false, li32, false);
    qa_call_value values[6] = {qa_call_int32(11), qa_call_float64(2.5), qa_call_int32(33),
                               qa_call_float32(4.5), qa_call_int32(55), qa_call_float32(6.5)};
    uint64_t sp = abi_enter(&f, &call, values, 6);
    CHECK(((sp + 8) % 16) == 0);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RCX, 64, false) == 11);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_R8, 64, false) == 33);
    double d;
    memcpy(&d, f.state.simd.xmm + 16, 8);
    CHECK(d == 2.5);
    float fl;
    memcpy(&fl, f.state.simd.xmm + 48, 4);
    CHECK(fl == 4.5f);
    uint8_t shadow[32];
    qa_sparse_copy(&f.mem, qa_guest_pointer(&f.mem.base, sp + 8), shadow, 32);
    bool zero = true;
    for (int i = 0; i < 32; i++) zero = zero && shadow[i] == 0;
    CHECK(zero);
    CHECK(qa_sparse_read_i32(&f.mem, qa_guest_pointer(&f.mem.base, sp + 40)) == 55);
    CHECK(qa_sparse_read_f32(&f.mem, qa_guest_pointer(&f.mem.base, sp + 48)) == 6.5);
    qa_call_value *got = NULL;
    size_t gn = 0;
    CHECK(qa_x86_abi_adapter_arguments(&f.adapter, &f.cpu, &call, NULL, 0, &got, &gn));
    CHECK(gn == 6);
    for (size_t i = 0; i < gn && i < 6; i++) CHECK(call_eq(&got[i], &values[i]));
    for (size_t i = 0; i < gn; i++) qa_call_value_free(&got[i]);
    free(got);
    qa_sparse_guest_memory_destroy(&f.mem);
  }

  /* System V classify + indivisible aggregate rollback. */
  {
    qa_abi_field_layout pair_fields[1] = {{"a", 0, QA_STORAGE_UINT64, 2}};
    qa_abi_layout pair_layout;
    qa_abi_value_layout pair = lay_record(&pair_layout, pair_fields, 1, 16, 8, 8);
    qa_abi_value_layout params[7] = {li32, li32, li32, li32, li32, pair, li32};
    qa_guest_call_signature call = mk_sig(lin64, params, 7, false, li32, false);
    qa_abi_call_plan plan;
    CHECK(qa_plan_guest_call(&plan, &call, NULL, 0));
    CHECK(plan.arguments[5].location_count == 1 && plan.arguments[5].locations[0].kind == QA_LOCATION_STACK &&
          plan.arguments[5].locations[0].stack_offset == 8 && plan.arguments[5].locations[0].bytes == 16);
    CHECK(plan.arguments[6].locations[0].kind == QA_LOCATION_INTEGER &&
          plan.arguments[6].locations[0].reg == QA_REG_R9 && plan.arguments[6].locations[0].bytes == 4);
    qa_abi_call_plan_free(&plan);
    qa_abi_field_layout mixed_fields[2] = {{"number", 0, QA_STORAGE_FLOAT64, 1}, {"counter", 8, QA_STORAGE_UINT64, 1}};
    qa_abi_layout ml;
    qa_abi_value_layout mixed = lay_record(&ml, mixed_fields, 2, 16, 8, 8);
    qa_eightbyte_class classes[2];
    size_t ncls = 0;
    CHECK(qa_classify_system_v_aggregate(&mixed, classes, &ncls));
    CHECK(ncls == 2 && classes[0] == QA_EIGHTBYTE_SSE && classes[1] == QA_EIGHTBYTE_INTEGER);
    qa_abi_field_layout union_fields[2] = {{"number", 0, QA_STORAGE_FLOAT64, 1}, {"bits", 0, QA_STORAGE_UINT64, 1}};
    qa_abi_layout ul;
    qa_abi_value_layout uni = lay_record(&ul, union_fields, 2, 8, 8, 8);
    CHECK(qa_classify_system_v_aggregate(&uni, classes, &ncls));
    CHECK(ncls == 1 && classes[0] == QA_EIGHTBYTE_INTEGER);
    qa_abi_field_layout unaligned[1] = {{"unaligned", 1, QA_STORAGE_FLOAT64, 1}};
    qa_abi_layout al;
    qa_abi_value_layout un = lay_record(&al, unaligned, 1, 9, 1, 8);
    CHECK(!qa_classify_system_v_aggregate(&un, classes, &ncls));
  }

  /* System V variadic AL + independent integer allocation. */
  {
    abi_fix f;
    abi_init(&f, lin64);
    qa_abi_value_layout params[1] = {li32};
    qa_guest_call_signature call = mk_sig(lin64, params, 1, true, li32, true);
    qa_call_value values[4] = {qa_call_int32(9), qa_call_float32(1.5), qa_call_uint64(17), qa_call_float64(-2)};
    (void)abi_enter(&f, &call, values, 4);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 8, false) == 2);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RDI, 64, false) == 9);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RSI, 64, false) == 17);
    double d0, d1;
    memcpy(&d0, f.state.simd.xmm, 8);
    memcpy(&d1, f.state.simd.xmm + 16, 8);
    CHECK(d0 == 1.5 && d1 == -2);
    qa_sparse_guest_memory_destroy(&f.mem);
  }

  /* i386 uint64 EAX/EDX + subword widening + cdecl/stdcall cleanup. */
  {
    for (int k = 0; k < 2; k++) {
      abi_fix f;
      abi_init(&f, k == 0 ? win32 : lin32);
      qa_guest_call_signature call = mk_sig(k == 0 ? win32 : lin32, NULL, 0, false, lay_scalar(QA_STORAGE_UINT64), false);
      (void)abi_enter(&f, &call, NULL, 0);
      qa_call_result res = qa_call_uint64(0xf123456789abcdefull);
      qa_x86_abi_adapter_leave(&f.adapter, &f.cpu, &call, &res);
      CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 32, false) == 0x89abcdefull);
      CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RDX, 32, false) == 0xf1234567ull);
      qa_call_result back = qa_x86_abi_adapter_return_value(&f.adapter, &f.cpu, &call);
      qa_call_value want = qa_call_uint64(0xf123456789abcdefull);
      CHECK(call_eq(&back, &want));
      qa_sparse_guest_memory_destroy(&f.mem);
    }
    abi_fix f;
    abi_init(&f, win32);
    qa_abi_value_layout params[2] = {lay_scalar(QA_STORAGE_INT8), lay_scalar(QA_STORAGE_UINT16)};
    qa_guest_call_signature call = mk_sig(win32, params, 2, false, li32, false);
    uint8_t fill[256];
    memset(fill, 0x5a, sizeof fill);
    qa_sparse_write(&f.mem, qa_guest_pointer(&f.mem.base, 0x1ff00), fill, sizeof fill);
    qa_call_value values[2] = {qa_call_int32(-2), qa_call_uint32(0x1234)};
    uint64_t sp = abi_enter(&f, &call, values, 2);
    CHECK(qa_sparse_read_u32(&f.mem, qa_guest_pointer(&f.mem.base, sp + 4)) == 0xfffffffeu);
    CHECK(qa_sparse_read_u32(&f.mem, qa_guest_pointer(&f.mem.base, sp + 8)) == 0x1234u);
    qa_sparse_guest_memory_destroy(&f.mem);
  }

  /* values.ts round trip. */
  {
    abi_fix f;
    abi_init(&f, lin64);
    qa_abi_value_layout l = lay_scalar(QA_STORAGE_UINT32);
    qa_call_value v = qa_call_uint32(0xdeadbeefu);
    size_t len = 0;
    uint8_t *bytes = qa_encode_value(&l, &v, &f.mem.base, &len);
    CHECK(bytes != NULL && len == 4);
    qa_call_value back = qa_decode_value(&l, bytes, len, &f.mem.base);
    CHECK(call_eq(&back, &v));
    free(bytes);
    CHECK(qa_align_up(13, 8) == 16 && qa_align_down(13, 8) == 8);
    CHECK(qa_storage_bytes(QA_STORAGE_POINTER, 4) == 4);
    qa_sparse_guest_memory_destroy(&f.mem);
  }
}

/* ---------- elf/tls.test.ts ---------- */
static void w16(uint8_t *b, size_t o, uint16_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *b, size_t o, uint32_t v) {
  b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24);
}
static void elf_word(uint8_t *b, size_t o, uint64_t v, int width) {
  if (width == 8) { w32(b, o, (uint32_t)v); w32(b, o + 4, (uint32_t)(v >> 32)); }
  else w32(b, o, (uint32_t)v);
}
static void elf_segment(uint8_t *b, int width, int index, uint32_t type, uint64_t offset,
                        uint64_t fsz, uint64_t msz, uint64_t align) {
  bool wide = width == 8;
  size_t p = 64 + (size_t)index * (wide ? 56 : 32);
  w32(b, p, type);
  w32(b, p + (wide ? 4 : 24), 7);
  elf_word(b, p + (wide ? 8 : 4), offset, width);
  elf_word(b, p + (wide ? 16 : 8), offset, width);
  elf_word(b, p + (wide ? 32 : 16), fsz, width);
  elf_word(b, p + (wide ? 40 : 20), msz, width);
  elf_word(b, p + (wide ? 48 : 28), align, width);
}
static void elf_fixture(uint8_t *bytes, int width) {
  memset(bytes, 0, 4096);
  bool wide = width == 8;
  w32(bytes, 0, 0x464c457fu);
  bytes[4] = wide ? 2 : 1; bytes[5] = 1; bytes[6] = 1;
  w16(bytes, 16, 3); w16(bytes, 18, wide ? 62 : 3); w32(bytes, 20, 1);
  elf_word(bytes, wide ? 32 : 28, 64, width);
  w16(bytes, wide ? 52 : 40, wide ? 64 : 52);
  w16(bytes, wide ? 54 : 42, wide ? 56 : 32);
  w16(bytes, wide ? 56 : 44, 3);
  static const struct { uint32_t tag; uint64_t value; } tags[] = {
      {4, 0x4c0}, {5, 0x480}, {10, 22}, {6, 0x400}, {11, 0}, {7, 0x500}, {8, 0}, {9, 0},
      {12, 0x800}, {13, 0x810}, {25, 0x980}, {27, 0}, {26, 0x9a0}, {28, 0},
      {0x6ffffffcu, 0x680}, {0x6ffffffdu, 1}, {0x6ffffff0u, 0x4e0}, {0, 0}};
  const size_t ntags = sizeof tags / sizeof tags[0];
  elf_segment(bytes, width, 0, 1, 0, 4096, 4096 + 4096, 4096);
  elf_segment(bytes, width, 1, 2, 0x200, ntags * (size_t)width * 2, ntags * (size_t)width * 2, (uint64_t)width);
  elf_segment(bytes, width, 2, 7, 0x700, 16, 32, 16);
  for (size_t i = 0; i < ntags; i++) {
    uint64_t value = tags[i].value;
    if (tags[i].tag == 11) value = wide ? 24 : 16;
    if (tags[i].tag == 8) value = (uint64_t)width * 3 * 10;
    if (tags[i].tag == 9) value = (uint64_t)width * 3;
    if (tags[i].tag == 27 || tags[i].tag == 28) value = (uint64_t)width * 2;
    elf_word(bytes, 0x200 + i * (size_t)width * 2, tags[i].tag, width);
    elf_word(bytes, 0x200 + i * (size_t)width * 2 + (size_t)width, value, width);
  }
  memcpy(bytes + 0x480, "\0tls\0entry\0ELF_TEST_1\0", 22);
  w32(bytes, 0x4c0, 1); w32(bytes, 0x4c4, 3);
  static const struct { int index; uint32_t name; uint8_t type; uint64_t value; } syms[2] = {
      {1, 1, 6, 4}, {2, 5, 2, 0x800}};
  for (int i = 0; i < 2; i++) {
    size_t p = 0x400 + (size_t)syms[i].index * (wide ? 24 : 16);
    w32(bytes, p, syms[i].name);
    bytes[p + (wide ? 4 : 12)] = (uint8_t)(16 + syms[i].type);
    w16(bytes, p + (wide ? 6 : 14), 1);
    elf_word(bytes, p + (wide ? 8 : 4), syms[i].value, width);
    elf_word(bytes, p + (wide ? 16 : 8), 4, width);
  }
  w16(bytes, 0x4e2, 1); w16(bytes, 0x4e4, 2);
  w16(bytes, 0x680, 1); w16(bytes, 0x684, 2); w16(bytes, 0x686, 1);
  w32(bytes, 0x68c, 20); w32(bytes, 0x694, 11);
  typedef struct { uint64_t addr; uint32_t type; uint32_t sym; uint64_t add; } reloc;
  reloc relocs[10] = {{0x700, 8, 0, 0x800}, {0x900, wide ? 16 : 35, 0, 0}, {0x910, wide ? 17 : 36, 1, 7},
                      {0x920, wide ? 18 : 14, 1, 3}, {0x928, wide ? 23 : 37, 1, 5},
                      {0x980, 8, 0, 0x800}, {0x980 + (uint64_t)width, 8, 0, 0x810},
                      {0x9a0, 8, 0, 0x820}, {0x9a0 + (uint64_t)width, 8, 0, 0x830},
                      {0x930, wide ? 37 : 42, 0, 0x840}};
  for (int i = 0; i < 10; i++) {
    size_t p = 0x500 + (size_t)i * (size_t)width * 3;
    elf_word(bytes, p, relocs[i].addr, width);
    elf_word(bytes, p + (size_t)width,
             ((uint64_t)relocs[i].sym << (wide ? 32 : 8)) | relocs[i].type, width);
    elf_word(bytes, p + (size_t)width * 2, relocs[i].add, width);
  }
  memset(bytes + 0x800, 0xc3, 0x60);
}
typedef struct elf_env {
  qa_sparse_guest_memory *mem;
  int indirect_calls;
} elf_env;
static qa_guest_import_resolution elf_resolve(void *ctx, const qa_guest_import *imp,
                                              const qa_guest_image *requesting) {
  (void)ctx; (void)requesting;
  qa_guest_import_resolution r;
  memset(&r, 0, sizeof r);
  r.kind = QA_RESOLUTION_UNRESOLVED;
  r.import_ = *imp;
  r.detail = "authored image has no external imports";
  return r;
}
static qa_guest_core_address elf_indirect(void *ctx, qa_guest_core_address resolver_address) {
  elf_env *e = (elf_env *)ctx;
  CHECK(resolver_address.byte_offset == 0x20000000ull + 0x840);
  qa_guest_core_address got;
  CHECK(qa_sparse_read_pointer(e->mem, at(e->mem, 0x20000000ull + 0x700), &got));
  CHECK(got.byte_offset == 0x20000000ull + 0x800);
  e->indirect_calls++;
  return at(e->mem, 0x20000000ull + 0x850);
}
static bool elf_tls_resolve(void *ctx, const qa_guest_import *imp, const qa_guest_image *req,
                            qa_elf_tls_resolution *out) {
  (void)ctx; (void)imp; (void)req; (void)out;
  return false;
}
static void test_elf_tls_one(int width) {
  uint8_t bytes[4096];
  elf_fixture(bytes, width);
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:elf-tls", width);
  test_ext es;
  qa_mapped_memory_ext ext;
  make_ext(&ext, &es, &mem);
  elf_env env = {&mem, 0};
  qa_elf_tls_bindings tls;
  memset(&tls, 0, sizeof tls);
  tls.current.module_id = 7;
  tls.current.has_thread_pointer_offset = true;
  tls.current.thread_pointer_offset = -64;
  tls.resolve = elf_tls_resolve;
  qa_elf_load_options opts;
  memset(&opts, 0, sizeof opts);
  opts.bytes = bytes; opts.bytes_len = sizeof bytes;
  qa_guest_module_identity mid = mod("test:elf-tls");
  opts.module = mid;
  opts.memory = &mem.base;
  opts.memory_ext = &ext;
  opts.load_bias = 0x20000000;
  opts.resolver.ctx = NULL;
  opts.resolver.resolve = elf_resolve;
  opts.tls = &tls;
  opts.resolve_ctx = &env;
  opts.resolve_indirect = elf_indirect;
  qa_elf_guest_image image;
  CHECK(qa_load_elf(&image, &opts));
  const uint64_t bias = 0x20000000;
  CHECK((width == 8 ? r64(&mem, bias + 0x900) : r32(&mem, bias + 0x900)) == 7);
  CHECK((width == 8 ? r64(&mem, bias + 0x910) : r32(&mem, bias + 0x910)) == (uint64_t)(width == 8 ? 11 : 4));
  uint64_t raw920 = width == 8 ? r64(&mem, bias + 0x920) : r32(&mem, bias + 0x920);
  CHECK(qa_signed_guest_pointer(raw920, width) == -57);
  CHECK(qa_sparse_read_i32(&mem, at(&mem, bias + 0x928)) == (width == 8 ? -55 : 65));
  CHECK((width == 8 ? r64(&mem, bias + 0x930) : r32(&mem, bias + 0x930)) == bias + 0x850);
  CHECK(env.indirect_calls == 1);
  CHECK(image.image.has_tls);
  uint64_t tmpl = 0;
  memcpy(&tmpl, image.image.tls.initialized, (size_t)width);
  CHECK(tmpl == bias + 0x800);
  CHECK(image.image.tls.zero_fill_bytes == 16);
  CHECK(image.image.tls.alignment == 16);
  CHECK(image.tls_export_count == 1);
  if (image.tls_export_count == 1) {
    CHECK(strcmp(image.tls_exports[0].symbol.name, "tls") == 0);
    CHECK(image.tls_exports[0].symbol.version == NULL);
    CHECK(image.tls_exports[0].offset == 4 && image.tls_exports[0].byte_length == 4);
  }
  CHECK(image.image.export_count == 2);
  if (image.image.export_count == 2) {
    CHECK(strcmp(image.image.exports[0].symbol.name, "entry") == 0);
    CHECK(image.image.exports[0].symbol.version != NULL &&
          strcmp(image.image.exports[0].symbol.version, "ELF_TEST_1") == 0);
    CHECK(image.image.exports[1].symbol.version == NULL);
  }
  CHECK(image.image.initializer_count == 3);
  if (image.image.initializer_count == 3) {
    CHECK(image.image.initializers[0].byte_offset == bias + 0x800);
    CHECK(image.image.initializers[1].byte_offset == bias + 0x800);
    CHECK(image.image.initializers[2].byte_offset == bias + 0x810);
  }
  CHECK(image.image.finalizer_count == 3);
  if (image.image.finalizer_count == 3) {
    CHECK(image.image.finalizers[0].byte_offset == bias + 0x830);
    CHECK(image.image.finalizers[1].byte_offset == bias + 0x820);
    CHECK(image.image.finalizers[2].byte_offset == bias + 0x810);
  }
  qa_elf_guest_image_free(&image);
  qa_sparse_guest_memory_destroy(&mem);
}
static void test_elf_tls(void) {
  test_elf_tls_one(4);
  test_elf_tls_one(8);
  /* Missing TLS bindings aborts before mapping (TS toThrow). */
  uint8_t bytes[4096];
  elf_fixture(bytes, 8);
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:elf-tls", 8);
  test_ext es;
  qa_mapped_memory_ext ext;
  make_ext(&ext, &es, &mem);
  qa_elf_load_options opts;
  memset(&opts, 0, sizeof opts);
  opts.bytes = bytes; opts.bytes_len = sizeof bytes;
  opts.module = mod("test:elf-tls");
  opts.memory = &mem.base;
  opts.memory_ext = &ext;
  opts.load_bias = 0x20000000;
  opts.resolver.resolve = elf_resolve;
  qa_elf_guest_image image;
  CHECK_ABORTS((void)qa_load_elf(&image, &opts));
  qa_sparse_guest_memory_destroy(&mem);
}

/* ---------- pe/loader.test.ts (+ fixture.ts) ---------- */
static size_t pe_raw(uint32_t rva) {
  return rva < 0x2000 ? rva - 0x1000 + 0x400
         : rva < 0x3000 ? rva - 0x2000 + 0x600
         : rva < 0x4000 ? rva - 0x3000 + 0xe00
                        : rva - 0x4000 + 0x1000;
}
static void pe_fixture(uint8_t *bytes, int width) {
  memset(bytes, 0, 0x1200);
  const uint64_t base = width == 4 ? 0x10000000ull : 0x180000000ull;
  const size_t optional = 0x98;
  const int optional_size = width == 4 ? 224 : 240;
  const size_t dirs = optional + (width == 4 ? 96 : 112);
  w16(bytes, 0, 0x5a4d); w32(bytes, 0x3c, 0x80); w32(bytes, 0x80, 0x4550);
  w16(bytes, 0x84, width == 4 ? 0x14c : 0x8664); w16(bytes, 0x86, 4);
  w16(bytes, 0x94, (uint16_t)optional_size); w16(bytes, 0x96, 0x2002);
  w16(bytes, optional, width == 4 ? 0x10b : 0x20b); w32(bytes, optional + 16, 0x1000);
  if (width == 4) w32(bytes, optional + 28, (uint32_t)base);
  else { w32(bytes, optional + 24, (uint32_t)base); w32(bytes, optional + 28, (uint32_t)(base >> 32)); }
  w32(bytes, optional + 32, 0x1000); w32(bytes, optional + 36, 0x200);
  w32(bytes, optional + 56, 0x5000); w32(bytes, optional + 60, 0x400);
  w32(bytes, dirs - 4, 16);
  static const struct { const char *name; uint32_t rva, size, raw, raw_size, flags; } secs[4] = {
      {".text", 0x1000, 0x80, 0x400, 0x200, 0x60000020},
      {".rdata", 0x2000, 0x800, 0x600, 0x800, 0x40000040},
      {".data", 0x3000, 0x1000, 0xe00, 0x200, 0xc0000040},
      {".reloc", 0x4000, 0x200, 0x1000, 0x200, 0x42000040}};
  for (int i = 0; i < 4; i++) {
    size_t at_off = optional + (size_t)optional_size + (size_t)i * 40;
    memcpy(bytes + at_off, secs[i].name, strlen(secs[i].name) + 1);
    w32(bytes, at_off + 8, secs[i].size); w32(bytes, at_off + 12, secs[i].rva);
    w32(bytes, at_off + 16, secs[i].raw_size); w32(bytes, at_off + 20, secs[i].raw);
    w32(bytes, at_off + 36, secs[i].flags);
  }
  memset(bytes + 0x400, 0xc3, 0x80);
#define PE_DIR(idx, rva, size) do { w32(bytes, dirs + (idx) * 8, (rva)); w32(bytes, dirs + (idx) * 8 + 4, (size)); } while (0)
#define PE_DWORD(rva, v) w32(bytes, pe_raw(rva), (v))
#define PE_PTR(rva, v) do { if (width == 4) w32(bytes, pe_raw(rva), (uint32_t)(v)); \
    else { w32(bytes, pe_raw(rva), (uint32_t)(v)); w32(bytes, pe_raw(rva) + 4, (uint32_t)((uint64_t)(v) >> 32)); } } while (0)
#define PE_TEXT(rva, s) memcpy(bytes + pe_raw(rva), (s), strlen(s) + 1)
  PE_DIR(0, 0x2000, 0x100);
  PE_DWORD(0x200c, 0x2090); PE_DWORD(0x2010, 8); PE_DWORD(0x2014, 2); PE_DWORD(0x2018, 2);
  PE_DWORD(0x201c, 0x2040); PE_DWORD(0x2020, 0x2050); PE_DWORD(0x2024, 0x2058);
  PE_DWORD(0x2040, 0x1010); PE_DWORD(0x2044, 0x2080); PE_DWORD(0x2050, 0x2060); PE_DWORD(0x2054, 0x2070);
  w16(bytes, pe_raw(0x2058), 0); w16(bytes, pe_raw(0x205a), 1);
  PE_TEXT(0x2060, "GetGameAPI"); PE_TEXT(0x2070, "Forward"); PE_TEXT(0x2080, "other.#8"); PE_TEXT(0x2090, "authored.dll");
  PE_DIR(1, 0x2100, 40);
  PE_DWORD(0x2100, 0x2140); PE_DWORD(0x210c, 0x2180); PE_DWORD(0x2110, 0x2160);
  uint64_t ordinal_flag = (1ull << (width * 8 - 1)) | 7;
  PE_PTR(0x2140, 0x21a0); PE_PTR(0x2140 + width, ordinal_flag);
  PE_PTR(0x2160, 0x21a0); PE_PTR(0x2160 + width, ordinal_flag);
  PE_TEXT(0x2180, "guest.dll"); PE_TEXT(0x21a2, "Target");
  PE_DIR(9, 0x2200, (uint32_t)(width * 4 + 8));
  PE_PTR(0x2200, base + 0x3000); PE_PTR(0x2200 + width, base + 0x3004);
  PE_PTR(0x2200 + width * 2, base + 0x3020); PE_PTR(0x2200 + width * 3, base + 0x2240);
  PE_DWORD(0x2200 + width * 4, 12); PE_DWORD(0x2200 + width * 4 + 4, 0x00300000);
  PE_PTR(0x2240, base + 0x1020); PE_PTR(0x2240 + width, base + 0x1030);
  bytes[pe_raw(0x3000)] = 9; bytes[pe_raw(0x3000) + 1] = 8;
  bytes[pe_raw(0x3000) + 2] = 7; bytes[pe_raw(0x3000) + 3] = 6;
  PE_PTR(0x3010, base + 0x1050);
  const int config_size = width == 4 ? 92 : 148;
  PE_DIR(10, 0x2280, (uint32_t)config_size); PE_DWORD(0x2280, (uint32_t)config_size);
  const uint32_t cookie = 0x2280 + (width == 4 ? 60 : 88);
  const uint32_t check_rva = 0x2280 + (width == 4 ? 72 : 112);
  const uint32_t dispatch = 0x2280 + (width == 4 ? 76 : 120);
  PE_PTR(cookie, base + 0x3040); PE_PTR(check_rva, base + 0x3060); PE_PTR(dispatch, base + 0x3070);
  PE_DWORD(0x2280 + (width == 4 ? 88 : 144), 0x100);
  if (width == 8) {
    PE_DIR(3, 0x2380, 12); PE_DWORD(0x2380, 0x1000); PE_DWORD(0x2384, 0x1040); PE_DWORD(0x2388, 0x23a0);
    static const uint8_t uw[8] = {1, 4, 1, 0, 4, 0x32, 0, 0};
    memcpy(bytes + pe_raw(0x23a0), uw, 8);
  }
  const int rtype = width == 4 ? 3 : 10;
  uint16_t rdata_relocs[10];
  const uint32_t rrvas[9] = {0x2200, 0x2200 + (uint32_t)width, 0x2200 + (uint32_t)width * 2,
                             0x2200 + (uint32_t)width * 3, 0x2240, 0x2240 + (uint32_t)width,
                             cookie, check_rva, dispatch};
  for (int i = 0; i < 9; i++) rdata_relocs[i] = (uint16_t)((rtype << 12) | (int)(rrvas[i] - 0x2000));
  rdata_relocs[9] = 0;
  uint16_t data_relocs[6];
  int nd = 0;
  data_relocs[nd++] = (uint16_t)((rtype << 12) | 0x10);
  if (width == 4) {
    w16(bytes, pe_raw(0x3018), 0x1122); w16(bytes, pe_raw(0x301a), 0x3344); w16(bytes, pe_raw(0x301c), 0x2000);
    data_relocs[nd++] = 0x1018; data_relocs[nd++] = 0x201a; data_relocs[nd++] = 0x401c; data_relocs[nd++] = 0x8123;
  }
  data_relocs[nd++] = 0;
  size_t reloc_bytes = 0;
  const struct { uint32_t page; const uint16_t *entries; int count; } blocks[2] = {
      {0x2000, rdata_relocs, 10}, {0x3000, data_relocs, nd}};
  for (int bi = 0; bi < 2; bi++) {
    size_t sz = 8 + (size_t)blocks[bi].count * 2;
    PE_DWORD(0x4000 + (uint32_t)reloc_bytes, blocks[bi].page);
    PE_DWORD(0x4004 + (uint32_t)reloc_bytes, (uint32_t)sz);
    for (int i = 0; i < blocks[bi].count; i++)
      w16(bytes, pe_raw(0x4008 + (uint32_t)reloc_bytes + (uint32_t)i * 2), blocks[bi].entries[i]);
    reloc_bytes += sz;
  }
  PE_DIR(5, 0x4000, (uint32_t)reloc_bytes);
#undef PE_DIR
#undef PE_DWORD
#undef PE_PTR
#undef PE_TEXT
}
typedef struct pe_bind_ctx {
  qa_guest_core_address target;
  qa_guest_module_identity module;
  bool fail_ordinal;
} pe_bind_ctx;
static qa_guest_import_resolution pe_resolve_import(void *ctx, const qa_guest_import *imp,
                                                    const qa_guest_image *requesting) {
  pe_bind_ctx *bc = (pe_bind_ctx *)ctx;
  (void)requesting;
  qa_guest_import_resolution r;
  memset(&r, 0, sizeof r);
  if (bc->fail_ordinal && imp->symbol.is_ordinal) {
    r.kind = QA_RESOLUTION_UNRESOLVED;
    r.import_ = *imp;
    r.detail = "ordinal unavailable";
    return r;
  }
  r.kind = QA_RESOLUTION_GUEST;
  r.address = bc->target;
  r.module = bc->module;
  return r;
}
static void pe_map(qa_pe_image *out, const uint8_t *bytes, size_t len, qa_sparse_guest_memory *mem,
                   qa_mapped_memory_ext *ext, bool has_base, uint64_t base) {
  qa_map_pe_image_options o;
  memset(&o, 0, sizeof o);
  o.bytes = bytes; o.bytes_len = len;
  o.memory = &mem->base;
  o.memory_ext = ext;
  o.memory_module = mod("test:pe");
  o.has_base = has_base;
  o.base = base;
  qa_map_pe_image(out, &o);
}
static void test_pe_one(int width) {
  uint8_t bytes[0x1200], snapshot[0x1200];
  pe_fixture(bytes, width);
  memcpy(snapshot, bytes, sizeof bytes);
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:pe", width);
  test_ext es;
  qa_mapped_memory_ext ext;
  make_ext(&ext, &es, &mem);
  const uint64_t base = width == 4 ? 0x10120000ull : 0x1a0000000ull;
  qa_pe_image image;
  pe_map(&image, bytes, sizeof bytes, &mem, &ext, true, base);
  CHECK(strcmp(image.image.abi.image, width == 4 ? "pe32" : "pe32+") == 0);
  CHECK(image.image.has_entry_point && image.image.entry_point.byte_offset == base + 0x1000);
  CHECK(image.image.initializer_count == 0 && image.image.finalizer_count == 0);
  qa_guest_core_address got;
  CHECK(qa_sparse_read_pointer(&mem, qa_sparse_offset(&mem, image.image.base, 0x3010), &got) &&
        got.byte_offset == base + 0x1050);
  uint8_t zeros[0x200];
  qa_sparse_copy(&mem, qa_sparse_offset(&mem, image.image.base, 0x3200), zeros, sizeof zeros);
  bool allzero = true;
  for (size_t i = 0; i < sizeof zeros; i++) allzero = allzero && zeros[i] == 0;
  CHECK(allzero);
  uint8_t one = 0;
  qa_sparse_fetch(&mem, qa_sparse_offset(&mem, image.image.base, 0x1010), &one, 1);
  CHECK(one == 0xc3);
  uint8_t b90 = 0x90;
  CHECK_ABORTS(qa_sparse_write(&mem, qa_sparse_offset(&mem, image.image.base, 0x1010), &b90, 1));
  CHECK_ABORTS(qa_sparse_fetch(&mem, qa_sparse_offset(&mem, image.image.base, 0x3000), &one, 1));
  uint8_t hdr[4];
  qa_sparse_copy(&mem, qa_sparse_offset(&mem, image.image.base, 0x400), hdr, 4);
  CHECK(hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 0);
  CHECK(image.image.has_tls);
  static const uint8_t tls_init[4] = {9, 8, 7, 6};
  CHECK(image.image.tls.initialized_len == 4 && memcmp(image.image.tls.initialized, tls_init, 4) == 0);
  CHECK(image.image.tls.zero_fill_bytes == 12);
  CHECK(image.image.tls.alignment == 4);
  CHECK(image.image.tls.callback_count == 2 &&
        image.image.tls.callbacks[0].byte_offset == base + 0x1020 &&
        image.image.tls.callbacks[1].byte_offset == base + 0x1030);
  CHECK(image.has_tls_index && image.tls_index_address.byte_offset == base + 0x3020);
  CHECK(image.has_load_configuration);
  CHECK(image.load_configuration.has_security_cookie &&
        image.load_configuration.security_cookie_address.byte_offset == base + 0x3040);
  CHECK(image.load_configuration.has_guard_check &&
        image.load_configuration.guard_check_slot.byte_offset == base + 0x3060);
  CHECK(image.load_configuration.has_guard_dispatch &&
        image.load_configuration.guard_dispatch_slot.byte_offset == base + 0x3070);
  CHECK(image.load_configuration.guard_flags == 0x100);
  CHECK(image.image.unwind_count == (width == 8 ? 1u : 0u));
  if (width == 4) {
    CHECK(qa_sparse_read_u16(&mem, qa_sparse_offset(&mem, image.image.base, 0x3018)) == 0x1134);
    CHECK(qa_sparse_read_u16(&mem, qa_sparse_offset(&mem, image.image.base, 0x301a)) == 0x3344);
    CHECK(qa_sparse_read_u16(&mem, qa_sparse_offset(&mem, image.image.base, 0x301c)) == 0x2012);
  } else if (image.image.unwind_count == 1) {
    static const uint8_t uw[8] = {1, 4, 1, 0, 4, 0x32, 0, 0};
    CHECK(image.image.unwind[0].metadata_len == 8 &&
          memcmp(image.image.unwind[0].metadata, uw, 8) == 0);
    CHECK(image.image.unwind[0].start.byte_offset == base + 0x1000);
    CHECK(image.image.unwind[0].end.byte_offset == base + 0x1040);
  }
  CHECK(memcmp(bytes, snapshot, sizeof bytes) == 0);
  qa_pe_image_free(&image);
  qa_sparse_guest_memory_destroy(&mem);

  /* Imports: atomic bind + read-only IAT. */
  mem_new(&mem, "test:pe", width);
  make_ext(&ext, &es, &mem);
  pe_fixture(bytes, width);
  pe_map(&image, bytes, sizeof bytes, &mem, &ext, false, 0);
  static const uint8_t retb[1] = {0xc3};
  qa_guest_core_address target = smap(&mem, 0x50000000, 16, QA_PERM_READ_EXECUTE, retb, 1);
  CHECK(image.image.import_count == 2);
  if (image.image.import_count == 2) {
    CHECK(!image.image.imports[0].symbol.is_ordinal &&
          strcmp(image.image.imports[0].symbol.name, "Target") == 0);
    CHECK(image.image.imports[1].symbol.is_ordinal && image.image.imports[1].symbol.ordinal == 7);
  }
  qa_guest_core_address iat = qa_sparse_offset(&mem, image.image.base, 0x2160);
  uint8_t orig[16];
  qa_sparse_copy(&mem, iat, orig, (size_t)width * 2);
  pe_bind_ctx bc = {target, mod("test:pe"), true};
  qa_guest_import_resolver res = {&bc, pe_resolve_import};
  qa_guest_import_resolution *out = NULL;
  size_t outn = 0;
  CHECK_ABORTS(qa_bind_pe_imports(&image, &mem.base, &ext, res, &out, &outn));
  uint8_t now[16];
  qa_sparse_copy(&mem, iat, now, (size_t)width * 2);
  CHECK(memcmp(orig, now, (size_t)width * 2) == 0);
  bc.fail_ordinal = false;
  qa_bind_pe_imports(&image, &mem.base, &ext, res, &out, &outn);
  CHECK(outn == 2);
  free(out);
  CHECK(qa_sparse_read_pointer(&mem, iat, &got) && got.byte_offset == target.byte_offset);
  CHECK(qa_sparse_read_pointer(&mem, qa_sparse_offset(&mem, iat, width), &got) &&
        got.byte_offset == target.byte_offset);
  uint8_t wipe[8] = {0};
  CHECK_ABORTS(qa_sparse_write(&mem, iat, wipe, (size_t)width));
  qa_pe_image_free(&image);
  qa_sparse_guest_memory_destroy(&mem);
}
typedef struct pe_lookup_ctx { const qa_pe_image *other; const qa_pe_image *cycle; } pe_lookup_ctx;
static const qa_pe_image *pe_lookup_other(void *ctx, const char *library, const qa_pe_image *req) {
  pe_lookup_ctx *c = (pe_lookup_ctx *)ctx;
  (void)req;
  return strcmp(library, "other") == 0 ? c->other : NULL;
}
static const qa_pe_image *pe_lookup_null(void *ctx, const char *library, const qa_pe_image *req) {
  (void)ctx; (void)library; (void)req;
  return NULL;
}
static const qa_pe_image *pe_lookup_cycle(void *ctx, const char *library, const qa_pe_image *req) {
  pe_lookup_ctx *c = (pe_lookup_ctx *)ctx;
  (void)library; (void)req;
  return c->cycle;
}
static void test_pe_rest(void) {
  test_pe_one(4);
  test_pe_one(8);
  /* Exports + forwarders + cycles. */
  qa_sparse_guest_memory mem;
  mem_new(&mem, "test:pe", 4);
  test_ext es;
  qa_mapped_memory_ext ext;
  make_ext(&ext, &es, &mem);
  uint8_t bytes[0x1200];
  pe_fixture(bytes, 4);
  qa_pe_image image, other;
  pe_map(&image, bytes, sizeof bytes, &mem, &ext, false, 0);
  pe_fixture(bytes, 4);
  pe_map(&other, bytes, sizeof bytes, &mem, &ext, true, 0x20000000);
  pe_lookup_ctx lc = {&other, NULL};
  qa_guest_symbol_name fwd = {false, "Forward", NULL, 0};
  qa_pe_resolved_export r = qa_resolve_pe_export(&image, fwd, pe_lookup_other, &lc);
  CHECK(r.address.byte_offset == 0x20001010ull);
  qa_guest_symbol_name get = {false, "GetGameAPI", NULL, 0};
  r = qa_resolve_pe_export(&image, get, pe_lookup_null, NULL);
  CHECK(r.address.byte_offset == 0x10001010ull);
  CHECK_ABORTS((void)qa_resolve_pe_export(&image, fwd, pe_lookup_null, NULL));
  pe_fixture(bytes, 4);
  memcpy(bytes + 0x680, "other.Forward", strlen("other.Forward") + 1);
  qa_pe_image cycle;
  pe_map(&cycle, bytes, sizeof bytes, &mem, &ext, true, 0x30000000);
  lc.cycle = &cycle;
  CHECK_ABORTS((void)qa_resolve_pe_export(&cycle, fwd, pe_lookup_cycle, &lc));
  qa_pe_image_free(&cycle);
  qa_pe_image_free(&other);
  qa_pe_image_free(&image);
  qa_sparse_guest_memory_destroy(&mem);

  /* x64 unwind chains. */
  mem_new(&mem, "test:pe", 8);
  make_ext(&ext, &es, &mem);
  pe_fixture(bytes, 8);
  bytes[0x9a0] = 0x21; bytes[0x9a1] = 0; bytes[0x9a2] = 0; bytes[0x9a3] = 0;
  w32(bytes, 0x9a4, 0x1000); w32(bytes, 0x9a8, 0x1040); w32(bytes, 0x9ac, 0x23b0);
  bytes[0x9b0] = 9; bytes[0x9b1] = 0; bytes[0x9b2] = 0; bytes[0x9b3] = 0;
  w32(bytes, 0x9b4, 0x1050); w32(bytes, 0x9b8, 0x12345678);
  qa_pe_image uimage;
  pe_map(&uimage, bytes, sizeof bytes, &mem, &ext, false, 0);
  CHECK(uimage.unwind_record_count >= 1);
  if (uimage.unwind_record_count >= 1 && uimage.unwind_records[0].chained != NULL) {
    CHECK(uimage.unwind_records[0].chained->handler_rva == 0x1050);
    CHECK(uimage.unwind_records[0].chained->handler_data_rva == 0x23b8);
  }
  CHECK(qa_sparse_read_u32(&mem, qa_sparse_offset(&mem, uimage.image.base, 0x23b8)) == 0x12345678u);
  qa_pe_image_free(&uimage);
  qa_sparse_guest_memory_destroy(&mem);
  w32(bytes, 0x9ac, 0x23a0);
  mem_new(&mem, "test:pe", 8);
  make_ext(&ext, &es, &mem);
  qa_pe_image cyc;
  CHECK_ABORTS(pe_map(&cyc, bytes, sizeof bytes, &mem, &ext, false, 0));
  qa_sparse_guest_memory_destroy(&mem);

  /* Rejected inputs abort before mapping. */
  mem_new(&mem, "test:pe", 4);
  make_ext(&ext, &es, &mem);
  static const uint8_t seventeen[1] = {17};
  qa_guest_core_address sentinel = smap(&mem, 0x10000, 8, QA_PERM_READ, seventeen, 1);
  size_t prior = qa_sparse_mappings(&mem, NULL, 0);
  pe_fixture(bytes, 4);
  qa_pe_image bad;
  CHECK_ABORTS(pe_map(&bad, bytes, 96, &mem, &ext, true, 0x20000000));
  pe_fixture(bytes, 4);
  w32(bytes, 0x1ac, 0x1000);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, true, 0x20000000));
  pe_fixture(bytes, 4);
  w16(bytes, 0x1008, 0x7200);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, true, 0x20000000));
  pe_fixture(bytes, 4);
  w32(bytes, 0x120, 0); w32(bytes, 0x124, 0);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, true, 0x20000000));
  pe_fixture(bytes, 4);
  w32(bytes, 0x160, 0x2100); w32(bytes, 0x164, 40);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, true, 0x20000000));
  pe_fixture(bytes, 4);
  w32(bytes, 0x840, 0x10003000);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, true, 0x20000000));
  pe_fixture(bytes, 4);
  w32(bytes, 0x104, 20);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, true, 0x20000000));
  CHECK(qa_sparse_mappings(&mem, NULL, 0) == prior);
  uint8_t sv = 0;
  qa_sparse_copy(&mem, sentinel, &sv, 1);
  CHECK(sv == 17);
  qa_pe_file pf;
  pe_fixture(bytes, 8);
  CHECK_ABORTS(qa_parse_pe(&pf, bytes, 0x1100));
  pe_fixture(bytes, 8);
  CHECK_ABORTS(pe_map(&bad, bytes, sizeof bytes, &mem, &ext, false, 0));
  qa_sparse_guest_memory_destroy(&mem);
}

int main(void) {
  test_core_flags_registers();
  test_core_memory();
  test_core_observers_alias();
  test_core_callbacks();
  test_x86_cpu();
  test_x64_cpu();
  test_abi_calls();
  test_elf_tls();
  test_pe_rest();
  printf("test_guest_cpu: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}







