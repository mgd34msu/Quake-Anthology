/* TS drift packet D6 (guest): checks for behaviour TS gained after the port's
 * baseline. Expected values come from tests/compat/oracles/d6_drift_oracle.ts
 * (run under bun against ../quake-anthology-ts) and from
 * tests/guest/floating-point/x87-extended.test.ts and
 * tests/guest/runtime/windows/qsort.test.ts. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/core/cpuid.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/core/registers.h"
#include "qa/guest/floating_point/binary.h"
#include "qa/guest/floating_point/sse.h"
#include "qa/guest/floating_point/x87.h"
#include "qa/guest/runtime/windows/crt.h"

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

/* --- ucrtQuickSort --- */
typedef struct sort_rig { const int *keys; int *identity; int comparisons; } sort_rig;
static int sort_compare(void *ctx, int64_t left, int64_t right) {
  sort_rig *r = ctx;
  r->comparisons++;
  return r->keys[r->identity[left]] - r->keys[r->identity[right]];
}
static void sort_swap(void *ctx, int64_t left, int64_t right) {
  sort_rig *r = ctx;
  const int held = r->identity[left];
  r->identity[left] = r->identity[right];
  r->identity[right] = held;
}
static int sort_keys(const int *keys, int length, int *identity) {
  for (int i = 0; i < length; i++) identity[i] = i;
  sort_rig r = {keys, identity, 0};
  qa_ucrt_quick_sort(length, sort_compare, sort_swap, &r);
  return r.comparisons;
}
static void test_qsort(void) {
  int identity[4096];
  /* qsort.test.ts: runs of eight or fewer use the selection sort, which moves the last maximum to the end first. */
  { const int keys[] = {1, 1, 1, 1}; const int want[] = {1, 2, 3, 0};
    sort_keys(keys, 4, identity); CHECK(memcmp(identity, want, sizeof want) == 0); }
  { const int keys[] = {2, 1, 2, 1}; const int want[] = {1, 3, 2, 0};
    sort_keys(keys, 4, identity); CHECK(memcmp(identity, want, sizeof want) == 0); }
  /* Oracle: exact permutations and comparison counts. */
  { const int keys[] = {3, 1, 2}; const int want[] = {1, 2, 0};
    CHECK(sort_keys(keys, 3, identity) == 3); CHECK(memcmp(identity, want, sizeof want) == 0); }
  { const int keys[] = {5, 3, 9, 1, 1, 7, 2, 8, 6, 4, 0, 3}; const int want[] = {10, 4, 3, 6, 1, 11, 9, 0, 8, 5, 7, 2};
    CHECK(sort_keys(keys, 12, identity) == 39); CHECK(memcmp(identity, want, sizeof want) == 0); }
  { const int keys[] = {2, 2, 2, 1, 1, 1, 0, 0, 0, 2, 1}; const int want[] = {8, 7, 6, 4, 10, 3, 5, 1, 9, 0, 2};
    CHECK(sort_keys(keys, 11, identity) == 41); CHECK(memcmp(identity, want, sizeof want) == 0); }
  /* qsort.test.ts: every length and key pattern sorts, permuting without loss. */
  uint32_t seed = 7;
  int keys[140];
  bool sorted_ok = true;
  for (int length = 0; length < 140; length++) {
    const int ranges[] = {2, 5, 1000};
    for (int ri = 0; ri < 3; ri++) {
      for (int i = 0; i < length; i++) {
        seed = (uint32_t)(seed * 1103515245u + 12345u);
        keys[i] = (int)((seed >> 8) % (uint32_t)ranges[ri]);
      }
      sort_keys(keys, length, identity);
      bool seen[140] = {false};
      for (int i = 0; i < length; i++) {
        if (identity[i] < 0 || identity[i] >= length || seen[identity[i]]) sorted_ok = false;
        else seen[identity[i]] = true;
        if (i > 0 && keys[identity[i - 1]] > keys[identity[i]]) sorted_ok = false;
      }
    }
  }
  CHECK(sorted_ok);
  /* Already ordered and reversed inputs stay within a quicksort comparison budget. */
  int ascending[4096], descending[4096];
  for (int i = 0; i < 4096; i++) { ascending[i] = i; descending[i] = 4095 - i; }
  CHECK(sort_keys(ascending, 4096, identity) < 4096 * 24);
  CHECK(sort_keys(descending, 4096, identity) < 4096 * 24);
}

/* --- virtualCpuid --- */
static void test_cpuid(void) {
  qa_cpuid_result r = qa_virtual_cpuid(0, false);
  CHECK(r.eax == 7 && r.ebx == 1801549137u && r.ecx == 1953719669u && r.edx == 1733514341u);
  r = qa_virtual_cpuid(1, true);
  CHECK(r.eax == 1536 && r.ebx == 65536 && r.ecx == 0 && r.edx == 100696321u);
  r = qa_virtual_cpuid(1, false);
  CHECK(r.edx == 100696065u);
  r = qa_virtual_cpuid(0x80000000u, false);
  CHECK(r.eax == 2147483649u && r.ebx == 0 && r.ecx == 0 && r.edx == 0);
  r = qa_virtual_cpuid(0x80000001u, false);
  CHECK(r.eax == 0 && r.ecx == 1 && r.edx == 536870912u);
  r = qa_virtual_cpuid(2, true);
  CHECK(r.eax == 0 && r.ebx == 0 && r.ecx == 0 && r.edx == 0);
}

/* --- SparseGuestMemory drift: quota, scalar access, write deliveries, allocator image --- */
static const qa_guest_module_ref d6_module = {"test:d6", "d6", "sha256:aa", "1"};
static void init_memory(qa_sparse_guest_memory *memory, uint64_t maximum) {
  qa_sparse_guest_memory_options options;
  memset(&options, 0, sizeof options);
  options.module = d6_module;
  options.pointer_bytes = 4;
  options.maximum_mapped_bytes = maximum;
  qa_sparse_guest_memory_init(memory, &options);
}
static qa_guest_core_address map_at(qa_sparse_guest_memory *memory, uint64_t base, size_t length) {
  qa_guest_map_options options;
  memset(&options, 0, sizeof options);
  options.base = base;
  options.byte_length = length;
  options.permissions = QA_PERM_READ_WRITE;
  options.label = "m";
  return qa_sparse_map(memory, &options);
}
static qa_guest_core_address at(uint64_t offset) { qa_guest_core_address a = {offset}; return a; }
typedef struct quota_rig { qa_sparse_guest_memory *memory; } quota_rig;
static void quota_map(void *ctx) { (void)map_at(((quota_rig *)ctx)->memory, 0x20000, 0x2000); }
typedef struct seen_rig { int calls; qa_guest_written_range ranges[4]; } seen_rig;
static void seen_write(void *ctx, const qa_guest_written_range *ranges, size_t count) {
  seen_rig *s = ctx;
  if (s->calls < 4 && count > 0) s->ranges[s->calls] = ranges[0];
  s->calls++;
}
static qa_guest_core_address pooled(qa_sparse_guest_memory *memory) {
  qa_guest_allocation_options options;
  memset(&options, 0, sizeof options);
  options.byte_length = 4096;
  options.alignment = 4096;
  options.pooled = true;
  options.label = "p";
  return qa_sparse_allocate(memory, &options);
}
typedef struct restore_rig { qa_sparse_guest_memory *memory; qa_guest_allocator_image *image; } restore_rig;
static void restore_body(void *ctx) { restore_rig *r = ctx; qa_sparse_restore_allocator(r->memory, r->image); }
static void test_memory(void) {
  {
    qa_sparse_guest_memory memory;
    init_memory(&memory, 0x3000);
    (void)map_at(&memory, 0x10000, 0x2000);
    quota_rig rig = {&memory};
    qa_caught_error caught;
    CHECK(!qa_try(quota_map, &rig, &caught));
    CHECK(strcmp(caught.message, "test:d6: map at 0x20000 (8192 bytes): guest mappings would exceed 12288 bytes") == 0);
    qa_sparse_unmap(&memory, at(0x10000), 0x1000);
    (void)map_at(&memory, 0x20000, 0x2000);
    qa_guest_mapping mappings[4];
    CHECK(qa_sparse_mappings(&memory, mappings, 4) == 2);
    CHECK(mappings[0].base == 69632 && mappings[0].byte_length == 4096);
    CHECK(mappings[1].base == 131072 && mappings[1].byte_length == 8192);
    qa_sparse_guest_memory_destroy(&memory);
  }
  {
    qa_sparse_guest_memory memory;
    init_memory(&memory, 0);
    qa_guest_core_address a = map_at(&memory, 0x10000, 0x1000);
    CHECK(qa_sparse_scalar_read(&memory, 0x10004, 4) == -1);
    qa_sparse_write_u32(&memory, at(a.byte_offset + 4), 0xdeadbeefu);
    CHECK(qa_sparse_scalar_read(&memory, 0x10004, 4) == 3735928559LL);
    CHECK(qa_sparse_scalar_read(&memory, 0x10004, 2) == 48879);
    CHECK(qa_sparse_scalar_read(&memory, 0x10007, 1) == 222);
    CHECK(qa_sparse_scalar_read(&memory, 0x10ffe, 4) == -1);
    CHECK(qa_sparse_scalar_write(&memory, 0x10008, 2, 0x12345));
    CHECK(qa_sparse_read_u32(&memory, at(a.byte_offset + 8)) == 9029);
    CHECK(!qa_sparse_scalar_write(&memory, 0x20000, 4, 1));
    seen_rig seen = {0};
    qa_guest_write_observer_handle handle = qa_sparse_observe_writes(&memory, at(a.byte_offset + 0x100), 8, seen_write, &seen);
    const uint64_t before = qa_sparse_write_deliveries(&memory);
    qa_sparse_write_u32(&memory, at(a.byte_offset + 0x200), 1);
    CHECK(qa_sparse_write_deliveries(&memory) - before == 0);
    qa_sparse_write_u32(&memory, at(a.byte_offset + 0x104), 1);
    CHECK(qa_sparse_scalar_write(&memory, 0x10102, 4, 7));
    CHECK(qa_sparse_write_deliveries(&memory) - before == 2);
    CHECK(seen.calls == 2);
    CHECK(seen.ranges[0].byte_offset == 4 && seen.ranges[0].byte_length == 4);
    CHECK(seen.ranges[1].byte_offset == 2 && seen.ranges[1].byte_length == 4);
    qa_sparse_unobserve_writes(&memory, handle);
    qa_sparse_guest_memory_destroy(&memory);
  }
  {
    qa_sparse_guest_memory memory;
    init_memory(&memory, 0);
    qa_guest_core_address pages[3];
    for (int i = 0; i < 3; i++) pages[i] = pooled(&memory);
    CHECK(pages[0].byte_offset == 65536 && pages[1].byte_offset == 69632 && pages[2].byte_offset == 73728);
    qa_guest_allocation_options plain;
    memset(&plain, 0, sizeof plain);
    plain.byte_length = 100;
    plain.label = "plain";
    (void)qa_sparse_allocate(&memory, &plain);
    qa_sparse_unmap(&memory, pages[1], 4096);
    qa_guest_allocator_image image;
    qa_sparse_allocator_image(&memory, &image);
    CHECK(image.hint_count == 2);
    CHECK(image.hints[0].alignment == 4096 && image.hints[0].base == 327680 && image.hints[0].byte_length == 262144);
    CHECK(image.hints[1].alignment == 16 && image.hints[1].base == 327780 && image.hints[1].byte_length == 100);
    CHECK(image.slab_count == 1 && image.slabs[0].base == 65536 && image.slabs[0].next == 3);
    CHECK(image.slabs[0].used.length == 64);
    CHECK(image.slabs[0].used.bytes[0] == 1 && image.slabs[0].used.bytes[1] == 0 && image.slabs[0].used.bytes[2] == 1 && image.slabs[0].used.bytes[3] == 0);
    CHECK(image.free_page_count == 1 && image.free_pages[0] == 1);
    CHECK(pooled(&memory).byte_offset == 69632);
    qa_sparse_restore_allocator(&memory, &image);
    CHECK(pooled(&memory).byte_offset == 69632);
    qa_guest_allocator_image invalid = image;
    const double bad_page[] = {999};
    invalid.free_pages = bad_page;
    restore_rig rig = {&memory, &invalid};
    qa_caught_error caught;
    CHECK(!qa_try(restore_body, &rig, &caught));
    CHECK(strcmp(caught.name, "RangeError") == 0 && strcmp(caught.message, "Invalid guest allocator free page") == 0);
    qa_guest_allocator_image_free(&image);
    qa_sparse_guest_memory_destroy(&memory);
  }
}

/* --- x87-extended.test.ts --- */
typedef struct x87_run {
  qa_guest_processor_state state;
  qa_mapped_guest_memory memory;
  qa_numeric_result result;
} x87_run;
static qa_binary_value from_double(double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof bits);
  qa_bigint raw;
  qa_bigint_init(&raw);
  qa_bigint_set_u64(&raw, bits);
  qa_binary_value decoded = qa_fp_decode_binary(&raw, 64);
  qa_bigint_free(&raw);
  return decoded;
}
/* Operands are listed from ST(0) downward. */
static void run_x87(x87_run *r, int modrm, int count, const qa_binary_value *stack) {
  qa_guest_processor_initial_state initial = {QA_GUEST_I386, 0, 0, 0, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&r->state, &initial);
  memset(&r->memory, 0, sizeof r->memory);
  r->memory.pointer_bytes = 4;
  for (int i = count - 1; i >= 0; i--) qa_push_x87(&r->state.x87, &stack[i]);
  qa_numeric_instruction ins;
  memset(&ins, 0, sizeof ins);
  ins.opcode = 0xd9;
  ins.modrm = modrm;
  ins.has_modrm = true;
  ins.register_index = (modrm >> 3) & 7;
  ins.has_operand = true;
  ins.operand.kind = QA_OPERAND_REGISTER;
  ins.operand.index = modrm & 7;
  ins.prefix = QA_PREFIX_NONE;
  ins.operand_bits = 32;
  qa_numeric_context ctx;
  qa_numeric_context_init(&ctx, &r->state, &r->memory, &ins);
  qa_execute_numeric_instruction(&r->result, &ctx);
}
static double st0(x87_run *r) { return qa_read_x87_return(&r->state.x87, QA_X87_FLOAT64); }
/* toBeCloseTo(expected, 15): |actual - expected| < 10^-15 / 2. */
static bool close15(double actual, double expected) { return fabs(actual - expected) < 5e-16; }
static void test_x87_extended(void) {
  x87_run r;
  qa_binary_value v[2];
  /* FSCALE multiplies by two to the truncated ST(1). */
  v[0] = from_double(3); v[1] = from_double(2.9); run_x87(&r, 0xfd, 2, v);
  CHECK(r.result.kind == QA_NUMERIC_EXECUTED && st0(&r) == 12);
  v[1] = from_double(-1.9); run_x87(&r, 0xfd, 2, v); CHECK(st0(&r) == 1.5);
  v[0] = from_double(-0.75); v[1] = from_double(0); run_x87(&r, 0xfd, 2, v); CHECK(st0(&r) == -0.75);
  /* FPREM truncates and FPREM1 rounds the quotient, reporting its low bits in C0, C3 and C1. */
  v[0] = from_double(10); v[1] = from_double(3); run_x87(&r, 0xf8, 2, v);
  CHECK(st0(&r) == 1 && (r.state.x87.status_word & 0x4700u) == 0x4200u);
  v[0] = from_double(11); run_x87(&r, 0xf5, 2, v);
  CHECK(st0(&r) == -1 && (r.state.x87.status_word & 0x4700u) == 0x100u);
  v[0] = from_double(5); v[1] = from_double(2); run_x87(&r, 0xf5, 2, v);
  CHECK(st0(&r) == 1 && (r.state.x87.status_word & 0x4700u) == 0x4000u);
  /* FPREM reports an incomplete reduction when the exponents are far apart. */
  v[0] = from_double(ldexp(1, 100) + ldexp(1, 60)); v[1] = from_double(3); run_x87(&r, 0xf8, 2, v);
  CHECK((r.state.x87.status_word & 0x400u) == 0x400u && st0(&r) < ldexp(1, 100));
  /* F2XM1, FYL2X and FYL2XP1. */
  v[0] = from_double(1); run_x87(&r, 0xf0, 1, v); CHECK(st0(&r) == 1);
  v[0] = from_double(-1); run_x87(&r, 0xf0, 1, v); CHECK(st0(&r) == -0.5);
  v[0] = from_double(0.5); run_x87(&r, 0xf0, 1, v); CHECK(close15(st0(&r), 0.41421356237309515));
  v[0] = from_double(8); v[1] = from_double(1); run_x87(&r, 0xf1, 2, v); CHECK(st0(&r) == 3);
  v[0] = from_double(0.5); v[1] = from_double(2); run_x87(&r, 0xf1, 2, v); CHECK(st0(&r) == -2);
  v[0] = from_double(10); v[1] = from_double(1); run_x87(&r, 0xf1, 2, v);
  CHECK(close15(st0(&r), 3.321928094887362) && (r.state.x87.status_word & 32u) == 32u);
  v[0] = from_double(0.5); v[1] = from_double(1); run_x87(&r, 0xf9, 2, v); CHECK(close15(st0(&r), 0.5849625007211562));
  v[0] = from_double(0); v[1] = from_double(1); run_x87(&r, 0xf1, 2, v);
  CHECK(st0(&r) == -INFINITY && (r.state.x87.status_word & 4u) == 4u);
  /* FPTAN, FSINCOS and FXTRACT push their second result. */
  v[0] = from_double(0.5); run_x87(&r, 0xf2, 1, v);
  CHECK(st0(&r) == 1); qa_pop_x87(&r.state.x87); CHECK(close15(st0(&r), 0.5463024898437905));
  v[0] = from_double(1); run_x87(&r, 0xfb, 1, v);
  CHECK(close15(st0(&r), 0.5403023058681398)); qa_pop_x87(&r.state.x87); CHECK(close15(st0(&r), 0.8414709848078965));
  { qa_bigint big; qa_bigint_init(&big); qa_bigint_set_u64(&big, 1ull << 63); v[0] = qa_fp_from_integer(&big); qa_bigint_free(&big); }
  run_x87(&r, 0xf2, 1, v); CHECK((r.state.x87.status_word & 0x400u) == 0x400u);
  v[0] = from_double(12); run_x87(&r, 0xf4, 1, v);
  CHECK(st0(&r) == 1.5); qa_pop_x87(&r.state.x87); CHECK(st0(&r) == 3);
}

int main(void) {
  test_qsort();
  test_cpuid();
  test_memory();
  test_x87_extended();
  printf("guest drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
