/*
 * Port of tests/guest/floating-point/execution.test.ts,
 * tests/guest/floating-point/trigonometric.test.ts, and the formatFloat
 * expectations checked against bun oracle output (see float cases below),
 * plus register/arithmetic/unwind/plan cases derived from the TS sources.
 * GPL-2.0-or-later.
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
#include "qa/guest/core/registers.h"
#include "qa/guest/elf/unwind.h"
#include "qa/guest/floating_point/binary.h"
#include "qa/guest/floating_point/sse.h"
#include "qa/guest/floating_point/x87.h"
#include "qa/guest/runtime/common/format/float.h"
#include "qa/guest/x64/plan.h"
#include "qa/guest/x86/arithmetic.h"

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

/* Flat test memory: 64 KiB at guest offset 0. */
static uint8_t g_mem_bytes[0x10000];
static void mem_bounds(qa_guest_core_address a, size_t len) {
  if (a.byte_offset > sizeof g_mem_bytes || len > sizeof g_mem_bytes - (size_t)a.byte_offset)
    qa_range_error("test memory out of bounds");
}
static void mem_copy_fn(void *ctx, qa_guest_core_address a, uint8_t *dest, size_t len) {
  (void)ctx; mem_bounds(a, len); memcpy(dest, g_mem_bytes + a.byte_offset, len);
}
static void mem_write_fn(void *ctx, qa_guest_core_address a, const uint8_t *bytes, size_t len) {
  (void)ctx; mem_bounds(a, len); memcpy(g_mem_bytes + a.byte_offset, bytes, len);
}
static qa_mapped_guest_memory make_memory(int pointer_bytes) {
  qa_mapped_guest_memory memory;
  memset(&memory, 0, sizeof memory);
  memory.pointer_bytes = pointer_bytes;
  memory.managed_sparse = true;
  memory.copy = mem_copy_fn;
  memory.write = mem_write_fn;
  return memory;
}
static qa_guest_core_address addr_at(uint64_t offset) { qa_guest_core_address a = {offset}; return a; }

typedef struct fixture {
  qa_guest_processor_state state;
  qa_mapped_guest_memory memory;
  qa_guest_core_address address; /* 0x2000 */
} fixture;
static void fixture_init(fixture *f) {
  memset(g_mem_bytes, 0, sizeof g_mem_bytes);
  qa_guest_processor_initial_state initial = {QA_GUEST_X86_64, 0x1000, 0x3000, 0, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&f->state, &initial);
  f->memory = make_memory(8);
  f->address = addr_at(0x2000);
}
/* execution.test.ts run() helper. */
static qa_numeric_result fixture_run(fixture *f, int opcode, int modrm, int secondary, bool has_secondary,
                                     qa_numeric_prefix prefix, int immediate, bool has_immediate) {
  qa_numeric_instruction ins;
  memset(&ins, 0, sizeof ins);
  ins.opcode = opcode;
  ins.secondary_opcode = secondary; ins.has_secondary_opcode = has_secondary;
  ins.modrm = modrm; ins.has_modrm = true;
  ins.register_index = (modrm >> 3) & 7;
  ins.has_operand = true;
  if (modrm >= 0xc0) { ins.operand.kind = QA_OPERAND_REGISTER; ins.operand.index = modrm & 7; }
  else { ins.operand.kind = QA_OPERAND_MEMORY; ins.operand.address = f->address; }
  ins.prefix = prefix;
  ins.operand_bits = 32;
  ins.immediate = immediate; ins.has_immediate = has_immediate;
  qa_numeric_context ctx;
  qa_numeric_context_init(&ctx, &f->state, &f->memory, &ins);
  qa_numeric_result result;
  qa_execute_numeric_instruction(&result, &ctx);
  return result;
}
static qa_numeric_result run2(fixture *f, int opcode, int modrm) {
  return fixture_run(f, opcode, modrm, 0, false, QA_PREFIX_NONE, 0, false);
}
static void mem_write_bits(fixture *f, const qa_bigint *value, size_t n) {
  uint8_t bytes[16];
  qa_fp_write_bits(bytes, value, n);
  qa_memory_write(&f->memory, f->address, bytes, n);
}
static void mem_write_u64_bits(fixture *f, uint64_t value, size_t n) {
  qa_bigint b; qa_bigint_init(&b); qa_bigint_set_u64(&b, value);
  mem_write_bits(f, &b, n);
  qa_bigint_free(&b);
}
static void mem_write_hex_bits(fixture *f, const char *hex, size_t n) {
  qa_bigint b; qa_bigint_init(&b); qa_bigint_set_hex(&b, hex);
  mem_write_bits(f, &b, n);
  qa_bigint_free(&b);
}
static uint64_t mem_read_u64_at(fixture *f, size_t n) {
  uint8_t bytes[16];
  qa_memory_copy(&f->memory, f->address, bytes, n);
  qa_bigint b; qa_bigint_init(&b); qa_fp_read_bits(&b, bytes, n);
  uint64_t v = qa_bigint_as_uint_n(&b, 64);
  qa_bigint_free(&b);
  return v;
}
static bool encode_equals_hex(const qa_binary_value *value, int width, const char *hex) {
  qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, value, width);
  qa_bigint expect; qa_bigint_init(&expect); qa_bigint_set_hex(&expect, hex);
  bool eq = qa_bigint_cmp(&bits, &expect) == 0;
  if (!eq) {
    char *got = qa_bigint_to_string(&bits, 16);
    fprintf(stderr, "  encode: got %s want %s\n", got ? got : "?", hex);
    free(got);
  }
  qa_bigint_free(&bits); qa_bigint_free(&expect);
  return eq;
}
static bool x87_top_equals_hex(qa_guest_x87_state *state, const char *hex) {
  qa_binary_value value;
  qa_read_x87_register(&value, state, 0);
  bool eq = encode_equals_hex(&value, 80, hex);
  qa_binary_value_free(&value);
  return eq;
}
static void set_xmm_hex(fixture *f, int index, const char *hex, int bytes) {
  qa_bigint b; qa_bigint_init(&b); qa_bigint_set_hex(&b, hex);
  qa_fp_write_bits(f->state.simd.xmm + index * 16, &b, (size_t)bytes);
  qa_bigint_free(&b);
}
static bool xmm_equals_hex(fixture *f, int index, int bytes, const char *hex) {
  qa_bigint b; qa_bigint_init(&b); qa_fp_read_bits(&b, f->state.simd.xmm + index * 16, (size_t)bytes);
  qa_bigint expect; qa_bigint_init(&expect); qa_bigint_set_hex(&expect, hex);
  bool eq = qa_bigint_cmp(&b, &expect) == 0;
  qa_bigint_free(&b); qa_bigint_free(&expect);
  return eq;
}

static void test_registers(void) {
  fixture f; fixture_init(&f);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RSP, 64, false) == 0x3000);
  qa_integer_register_file_write(&f.state.registers, QA_REG_RAX, 64, 0x1122334455667788ull, false);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 8, true) == 0x77); /* AH */
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 32, false) == 0x55667788u);
  qa_integer_register_file_write(&f.state.registers, QA_REG_RAX, 32, 0xdeadbeefull, false);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 64, false) == 0xdeadbeefull); /* upper cleared */
  uint8_t snap[128];
  CHECK(qa_integer_register_file_checkpoint(&f.state.registers, snap, sizeof snap) == 128);
  qa_integer_register_file_write(&f.state.registers, QA_REG_RAX, 64, 0, false);
  qa_integer_register_file_restore(&f.state.registers, snap, sizeof snap);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 64, false) == 0xdeadbeefull);
  CHECK(f.state.x87.tag_word == 0xffff);
  CHECK(f.state.simd.xmm_len == 256);

  qa_guest_processor_initial_state i386 = {QA_GUEST_I386, 0, 0, 0x2, 0x37f, 0x1f80, 0xffff};
  qa_guest_processor_state s32;
  qa_create_managed_guest_processor_state(&s32, &i386);
  CHECK(s32.simd.xmm_len == 128);
  CHECK(qa_managed_guest_processor(&s32));
  CHECK_ABORTS(qa_integer_register_file_read(&s32.registers, QA_REG_R8, 32, false));
  CHECK_ABORTS(qa_integer_register_file_read(&s32.registers, QA_REG_RSP, 8, false)); /* SPL in i386 */
  CHECK_ABORTS(qa_integer_register_file_read(&f.state.registers, QA_REG_RSP, 8, true)); /* no high-byte */

  qa_processor_flags flags;
  qa_processor_flags_init(&flags, 0x100000000ull | 0x41u);
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_CARRY));
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_ZERO));
  qa_processor_flags_set(&flags, QA_FLAG_CARRY, false);
  CHECK(qa_processor_flags_value(&flags) == (0x100000000ull | 0x40u));
  qa_processor_flags_restore_words(&flags, 0x800u, 0x5u);
  CHECK(qa_processor_flags_value(&flags) == ((5ull << 32) | 0x800u));
  CHECK(strcmp(qa_guest_register_name(QA_REG_R15), "r15") == 0);
  CHECK(strcmp(qa_guest_flag_name(QA_FLAG_AUXILIARY_CARRY), "auxiliary-carry") == 0);
}

static void test_arithmetic(void) {
  qa_processor_flags flags;
  qa_processor_flags_init(&flags, 0);
  /* 0x7f + 1 at width 8: sign+overflow, no carry; parity of 0x80 is 0. */
  CHECK(qa_x86_alu(QA_ALU_ADD, 8, 0x7f, 1, &flags) == 0x80);
  CHECK((qa_processor_flags_value(&flags) & 0x8d5u) == 0x890u);
  /* 0xff + 1: carry+zero+aux+parity(0). */
  CHECK(qa_x86_alu(QA_ALU_ADD, 8, 0xff, 1, &flags) == 0);
  CHECK((qa_processor_flags_value(&flags) & 0x8d5u) == 0x55u);
  /* 64-bit wrap add. */
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_alu(QA_ALU_ADD, 64, UINT64_MAX, 1, &flags) == 0);
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_CARRY));
  /* sub borrow: 0 - 1 at 16 bits -> 0xffff, carry+sign+parity+aux */
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_alu(QA_ALU_SUB, 16, 0, 1, &flags) == 0xffff);
  CHECK((qa_processor_flags_value(&flags) & 0x8d5u) == 0x95u);
  /* logic clears carry/overflow/aux */
  qa_processor_flags_init(&flags, 0x8d5u);
  CHECK(qa_x86_alu(QA_ALU_AND, 32, 0xf0f0u, 0x0ff0u, &flags) == 0x00f0u);
  CHECK((qa_processor_flags_value(&flags) & 0x8d5u) == 0x14u); /* AF kept (mask 0x8c5) + parity of 0xf0 */
  /* shifts */
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_shift(QA_SHIFT_SHL, 8, 0x81, 1, &flags) == 0x02);
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_CARRY));
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_shift(QA_SHIFT_SAR, 8, 0x80, 7, &flags) == 0xff);
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_shift(QA_SHIFT_ROL, 16, 0x8001, 1, &flags) == 0x0003);
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_CARRY));
  qa_processor_flags_init(&flags, 1); /* carry set */
  CHECK(qa_x86_shift(QA_SHIFT_RCL, 8, 0x80, 1, &flags) == 0x01);
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_CARRY));
  /* masked count 32 at width 8: no-op */
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_shift(QA_SHIFT_SHL, 8, 0x55, 32, &flags) == 0x55);
  /* signed multiply overflow: 0x100 * 0x100 at 8 bits (operands sign-extended: 0,0 -> 0) and 16x16 */
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_signed_multiply(16, 300, 300, &flags) == (90000 & 0xffffu));
  CHECK(qa_processor_flags_get(&flags, QA_FLAG_OVERFLOW));
  qa_processor_flags_init(&flags, 0);
  CHECK(qa_x86_signed_multiply(16, (uint64_t)-2 & 0xffffu, 3, &flags) == 0xfffa);
  CHECK(!qa_processor_flags_get(&flags, QA_FLAG_OVERFLOW));
  /* conditions */
  qa_processor_flags_init(&flags, 0x41u); /* carry+zero */
  CHECK(qa_x86_condition(6, &flags));
  CHECK(!qa_x86_condition(7, &flags));
  CHECK(qa_x86_condition(4, &flags));
  CHECK_ABORTS((void)qa_x86_condition(16, &flags));
  qa_alu_operation parsed;
  CHECK(qa_alu_operation_parse("sbb", &parsed) && parsed == QA_ALU_SBB);
  CHECK(strcmp(qa_shift_operation_name(QA_SHIFT_RCR), "rcr") == 0);
}

static bool binary_values_equal(const qa_binary_value *a, const qa_binary_value *b) {
  if (a->kind != b->kind || a->sign != b->sign) return false;
  if (a->kind == QA_BINARY_FINITE)
    return qa_bigint_cmp(&a->coefficient, &b->coefficient) == 0 && a->exponent == b->exponent && a->denormal == b->denormal;
  if (a->kind == QA_BINARY_NAN)
    return qa_bigint_cmp(&a->payload, &b->payload) == 0 && a->signaling == b->signaling;
  return true;
}
static void test_binary(void) {
  qa_binary_value one = qa_fp_from_i64(1), three = qa_fp_from_i64(3), two = qa_fp_from_i64(2);
  qa_binary_result result;
  qa_fp_arithmetic(&result, QA_FP_DIVIDE, &one, &three, qa_binary64_format, QA_FP_NEAREST, QA_FP_NAN_X87);
  CHECK(encode_equals_hex(&result.value, 64, "3fd5555555555555"));
  qa_binary_value_free(&result.value);
  qa_fp_square_root(&result, &two, qa_binary64_format, QA_FP_NEAREST);
  CHECK(encode_equals_hex(&result.value, 64, "3ff6a09e667f3bcd"));
  qa_binary_value_free(&result.value);
  qa_bigint big; qa_bigint_init(&big); qa_bigint_set_hex(&big, "8000000000000000"); /* 1<<63 */
  qa_binary_value big_value = qa_fp_from_integer(&big);
  qa_bigint_free(&big);
  qa_fp_arithmetic(&result, QA_FP_ADD, &big_value, &one, qa_binary80_format, QA_FP_NEAREST, QA_FP_NAN_X87);
  CHECK(encode_equals_hex(&result.value, 80, "403e8000000000000001"));
  qa_binary_value_free(&result.value);
  qa_binary_value_free(&big_value);
  /* exact subnormal add keeps denormal-operand flag */
  qa_bigint one_bit; qa_bigint_init(&one_bit); qa_bigint_set_u64(&one_bit, 1);
  qa_binary_value smallest = qa_fp_decode_binary(&one_bit, 32);
  qa_bigint_free(&one_bit);
  qa_fp_arithmetic(&result, QA_FP_ADD, &smallest, &smallest, qa_binary32_format, QA_FP_NEAREST, QA_FP_NAN_SSE);
  CHECK(encode_equals_hex(&result.value, 32, "2"));
  CHECK(result.flags == 2);
  qa_binary_value_free(&result.value);
  /* cancellation under round-down yields -0 */
  qa_fp_arithmetic(&result, QA_FP_SUBTRACT, &one, &one, qa_binary80_format, QA_FP_DOWN, QA_FP_NAN_X87);
  CHECK(encode_equals_hex(&result.value, 80, "80000000000000000000"));
  qa_binary_value_free(&result.value);
  qa_binary_value_free(&smallest);
  qa_binary_value_free(&one); qa_binary_value_free(&three); qa_binary_value_free(&two);
  /* decodeBinary32 matches decodeBinary(32) */
  static const uint32_t words[] = {0, 1, 0x007fffff, 0x00800000, 0x3f7fffff, 0x3f800000, 0x3f800001,
                                   0x7f7fffff, 0x7f800000, 0x7f800001, 0x7fbfffff, 0x7fc00000, 0x7fc12345, 0x7fffffff};
  for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
    for (int s = 0; s < 2; s++) {
      uint32_t word = words[i] | (s ? 0x80000000u : 0u);
      qa_binary_value fast = qa_fp_decode_binary32(word);
      qa_bigint bits; qa_bigint_init(&bits); qa_bigint_set_u64(&bits, word);
      qa_binary_value general = qa_fp_decode_binary(&bits, 32);
      qa_bigint_free(&bits);
      CHECK(binary_values_equal(&fast, &general));
      qa_binary_value_free(&fast); qa_binary_value_free(&general);
    }
  }
}

static void test_x87_execution(void) {
  /* 2^53 + 1 in x87 64-bit significand (execution.test.ts) */
  {
    fixture f; fixture_init(&f);
    mem_write_u64_bits(&f, 1ull << 53, 8);
    CHECK(run2(&f, 0xdf, 0x28).kind == QA_NUMERIC_EXECUTED); /* FILD64 */
    CHECK(run2(&f, 0xd9, 0xe8).kind == QA_NUMERIC_EXECUTED); /* FLD1 */
    CHECK(run2(&f, 0xde, 0xc1).kind == QA_NUMERIC_EXECUTED); /* FADDP */
    CHECK(x87_top_equals_hex(&f.state.x87, "40348000000000000400"));
    mem_write_u64_bits(&f, 0x4340000000000000ull, 8);
    CHECK(run2(&f, 0xdc, 0x20).kind == QA_NUMERIC_EXECUTED); /* FSUB m64 */
    CHECK(qa_read_x87_return(&f.state.x87, QA_X87_FLOAT64) == 1.0);
  }
  /* precision control */
  {
    static const struct { uint32_t control; double expected; uint32_t flags; } cases[3] = {
      {0x37f, 16777217.0, 0}, {0x7f, 16777216.0, 32}, {0x87f, 16777218.0, 32}};
    for (int i = 0; i < 3; i++) {
      fixture f; fixture_init(&f);
      f.state.x87.control_word = cases[i].control;
      qa_write_x87_return(&f.state.x87, 16777216.0, QA_X87_FLOAT64);
      CHECK(run2(&f, 0xd9, 0xe8).kind == QA_NUMERIC_EXECUTED);
      CHECK(run2(&f, 0xde, 0xc1).kind == QA_NUMERIC_EXECUTED);
      CHECK(qa_read_x87_return(&f.state.x87, QA_X87_FLOAT64) == cases[i].expected);
      CHECK((f.state.x87.status_word & 32u) == cases[i].flags);
    }
  }
  /* midpoint stores under all rounding modes */
  {
    static const uint32_t expected[4] = {0x3f800000u, 0x3f800000u, 0x3f800001u, 0x3f800000u};
    for (int mode = 0; mode < 4; mode++) {
      fixture f; fixture_init(&f);
      f.state.x87.control_word = 0x37fu | ((uint32_t)mode << 10);
      qa_write_x87_return(&f.state.x87, 1.0 + 5.960464477539063e-8 /* 2^-24 */, QA_X87_FLOAT64);
      CHECK(run2(&f, 0xd9, 0x18).kind == QA_NUMERIC_EXECUTED); /* FSTP m32 */
      CHECK(mem_read_u64_at(&f, 4) == expected[mode]);
      CHECK(f.state.x87.tag_word == 0xffffu);
    }
    fixture f; fixture_init(&f);
    qa_write_x87_return(&f.state.x87, -0.0, QA_X87_FLOAT64);
    CHECK(run2(&f, 0xd9, 0xfa).kind == QA_NUMERIC_EXECUTED); /* FSQRT */
    double returned = qa_read_x87_return(&f.state.x87, QA_X87_FLOAT64);
    CHECK(returned == 0.0 && signbit(returned));
  }
  /* raw80 load/store preserves sNaN payload and noncanonical forms */
  {
    fixture f; fixture_init(&f);
    mem_write_hex_bits(&f, "ffff8000000000000123", 10);
    CHECK(run2(&f, 0xdb, 0x28).kind == QA_NUMERIC_EXECUTED);
    CHECK((f.state.x87.status_word & 1u) == 0);
    CHECK(run2(&f, 0xdb, 0x38).kind == QA_NUMERIC_EXECUTED);
    uint8_t bytes[10]; qa_memory_copy(&f.memory, f.address, bytes, 10);
    qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, 10);
    qa_bigint expect; qa_bigint_init(&expect); qa_bigint_set_hex(&expect, "ffff8000000000000123");
    CHECK(qa_bigint_cmp(&bits, &expect) == 0);
    qa_bigint_free(&bits); qa_bigint_free(&expect);
  }
  {
    static const char *forms[2] = {"00008000000000000001", "40000000000000000123"};
    for (int i = 0; i < 2; i++) {
      fixture f; fixture_init(&f);
      mem_write_hex_bits(&f, forms[i], 10);
      CHECK(run2(&f, 0xdb, 0x28).kind == QA_NUMERIC_EXECUTED);
      CHECK(run2(&f, 0xdb, 0x38).kind == QA_NUMERIC_EXECUTED);
      uint8_t bytes[10]; qa_memory_copy(&f.memory, f.address, bytes, 10);
      qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, 10);
      qa_bigint expect; qa_bigint_init(&expect); qa_bigint_set_hex(&expect, forms[i]);
      bool eq = qa_bigint_cmp(&bits, &expect) == 0;
      if (!eq) { char *g = qa_bigint_to_string(&bits, 16); fprintf(stderr, " raw80 got %s want %s\n", g, forms[i]); free(g); }
      CHECK(eq);
      qa_bigint_free(&bits); qa_bigint_free(&expect);
    }
  }
  /* unmasked store overflow leaves memory/TOP unchanged */
  {
    fixture f; fixture_init(&f);
    qa_write_x87_return(&f.state.x87, ldexp(1.0, 128), QA_X87_FLOAT64);
    f.state.x87.control_word &= ~8u;
    mem_write_u64_bits(&f, 0x12345678u, 4);
    uint32_t top_bits = f.state.x87.status_word & 0x3800u;
    CHECK(run2(&f, 0xd9, 0x18).kind == QA_NUMERIC_EXECUTED);
    CHECK(qa_memory_read_u32(&f.memory, f.address) == 0x12345678u);
    CHECK((f.state.x87.status_word & 0x3800u) == top_bits);
    CHECK((f.state.x87.status_word & (8u | 32u | 0x200u)) == 8u);
  }
  /* stack fault deferred until WAIT */
  {
    fixture f; fixture_init(&f);
    f.state.x87.control_word &= ~1u;
    uint8_t before[80]; memcpy(before, f.state.x87.registers, 80);
    qa_numeric_instruction ins; memset(&ins, 0, sizeof ins);
    ins.opcode = 0xd8; ins.modrm = 0xc1; ins.has_modrm = true;
    ins.has_operand = true; ins.operand.kind = QA_OPERAND_REGISTER; ins.operand.index = 1;
    ins.prefix = QA_PREFIX_NONE; ins.operand_bits = 32;
    qa_numeric_context ctx; qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
    qa_numeric_result result; qa_execute_numeric_instruction(&result, &ctx);
    CHECK(result.kind == QA_NUMERIC_EXECUTED);
    CHECK((f.state.x87.status_word & 0x80c1u) == 0x80c1u);
    CHECK(memcmp(f.state.x87.registers, before, 80) == 0);
    CHECK(f.state.x87.tag_word == 0xffffu);
    ins.opcode = 0x9b; ins.has_modrm = false; ins.has_operand = false;
    qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
    qa_execute_numeric_instruction(&result, &ctx);
    CHECK(result.kind == QA_NUMERIC_EXCEPTION && result.vector == 16);
    CHECK(strcmp(result.detail, "Pending x87 floating-point exception") == 0);
  }
}

static void test_sse_execution(void) {
  /* scalar register move retains upper lanes; memory load clears them */
  {
    fixture f; fixture_init(&f);
    set_xmm_hex(&f, 0, "aaaaaaaaaaaaaaaabbbbbbbbcccccccc", 16);
    set_xmm_hex(&f, 1, "1111111122222222333333333f800000", 16);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x10, true, QA_PREFIX_F3, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 16, "aaaaaaaaaaaaaaaabbbbbbbb3f800000"));
    mem_write_u64_bits(&f, 0x80000000u, 4);
    CHECK(fixture_run(&f, 0x0f, 0x00, 0x10, true, QA_PREFIX_F3, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 16, "80000000"));
  }
  /* MOVUPS unaligned ok; MOVAPS faults first */
  {
    fixture f; fixture_init(&f);
    qa_guest_core_address unaligned = addr_at(0x2001);
    uint8_t fill[16]; memset(fill, 0xab, 16);
    qa_memory_write(&f.memory, unaligned, fill, 16);
    qa_numeric_instruction ins; memset(&ins, 0, sizeof ins);
    ins.opcode = 0x0f; ins.secondary_opcode = 0x10; ins.has_secondary_opcode = true;
    ins.modrm = 0; ins.has_modrm = true; ins.register_index = 0;
    ins.has_operand = true; ins.operand.kind = QA_OPERAND_MEMORY; ins.operand.address = unaligned;
    ins.prefix = QA_PREFIX_NONE; ins.operand_bits = 32;
    qa_numeric_context ctx; qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
    qa_numeric_result result; qa_execute_numeric_instruction(&result, &ctx);
    CHECK(result.kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 16, "abababababababababababababababab"));
    ins.secondary_opcode = 0x28;
    qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
    qa_execute_numeric_instruction(&result, &ctx);
    CHECK(result.kind == QA_NUMERIC_EXCEPTION);
    CHECK(xmm_equals_hex(&f, 0, 16, "abababababababababababababababab"));
  }
  /* MXCSR rounding + first-source NaN payload */
  {
    fixture f; fixture_init(&f);
    f.state.simd.mxcsr = 0x5f80;
    set_xmm_hex(&f, 0, "3f800000", 4); set_xmm_hex(&f, 1, "33800000", 4);
    memset(f.state.simd.xmm + 4, 0, 12); memset(f.state.simd.xmm + 16 + 4, 0, 12);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x58, true, QA_PREFIX_F3, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 4, "3f800001"));
    CHECK((f.state.simd.mxcsr & 32u) == 32u);
    set_xmm_hex(&f, 0, "ffc12345", 4); set_xmm_hex(&f, 1, "7fc54321", 4);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x58, true, QA_PREFIX_F3, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 4, "ffc12345"));
  }
  /* exceptions aggregate before destination changes */
  {
    fixture f; fixture_init(&f);
    set_xmm_hex(&f, 0, "3f8000003f8000003f8000003f800000", 16);
    set_xmm_hex(&f, 1, "3f8000003f800000000000003f800000", 16);
    f.state.simd.mxcsr &= ~(1u << 9);
    qa_numeric_result result = fixture_run(&f, 0x0f, 0xc1, 0x5e, true, QA_PREFIX_NONE, 0, false);
    CHECK(result.kind == QA_NUMERIC_EXCEPTION && result.vector == 19);
    CHECK(strcmp(result.detail, "Unmasked SIMD floating-point exception") == 0);
    CHECK(xmm_equals_hex(&f, 0, 16, "3f8000003f8000003f8000003f800000"));
    CHECK((f.state.simd.mxcsr & 4u) == 4u);
  }
  /* DAZ */
  {
    fixture f; fixture_init(&f);
    f.state.simd.mxcsr |= 64u;
    set_xmm_hex(&f, 0, "80000001", 4); set_xmm_hex(&f, 1, "3f800000", 4);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x59, true, QA_PREFIX_F3, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 4, "80000000"));
    CHECK((f.state.simd.mxcsr & 2u) == 0);
  }
  /* CVTTSD2SI truncation and indefinite */
  {
    fixture f; fixture_init(&f);
    set_xmm_hex(&f, 1, "bff3333333333333", 8);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x2c, true, QA_PREFIX_F2, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 32, false) == 0xffffffffu);
    set_xmm_hex(&f, 1, "7ff0000000000000", 8);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x2c, true, QA_PREFIX_F2, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 32, false) == 0x80000000u);
    CHECK((f.state.simd.mxcsr & 1u) == 1u);
  }
  /* packed integer wrap + compare masks */
  {
    fixture f; fixture_init(&f);
    set_xmm_hex(&f, 0, "7fffffff00000000ffffffffffffffff", 16);
    set_xmm_hex(&f, 1, "00000001000000010000000100000001", 16);
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0xfe, true, QA_PREFIX_66, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 16, "80000000000000010000000000000000"));
    CHECK(fixture_run(&f, 0x0f, 0xc1, 0x76, true, QA_PREFIX_66, 0, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(xmm_equals_hex(&f, 0, 16, "00000000ffffffff0000000000000000"));
  }
}

/* trigonometric.test.ts fixture (i386 state, explicit operand). */
static void trig_fixture(fixture *f, const qa_binary_value *value, uint32_t control) {
  memset(g_mem_bytes, 0, sizeof g_mem_bytes);
  qa_guest_processor_initial_state initial = {QA_GUEST_I386, 0, 0, 0, (uint32_t)control, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&f->state, &initial);
  f->memory = make_memory(4);
  f->address = addr_at(0x2000);
  qa_push_x87(&f->state.x87, value);
}
static qa_numeric_result trig_run(fixture *f, bool cosine) {
  qa_numeric_instruction ins; memset(&ins, 0, sizeof ins);
  ins.opcode = 0xd9; ins.modrm = cosine ? 0xff : 0xfe; ins.has_modrm = true;
  ins.register_index = 7;
  ins.has_operand = true; ins.operand.kind = QA_OPERAND_REGISTER; ins.operand.index = cosine ? 7 : 6;
  ins.prefix = QA_PREFIX_NONE; ins.operand_bits = 32;
  qa_numeric_context ctx; qa_numeric_context_init(&ctx, &f->state, &f->memory, &ins);
  qa_numeric_result result; qa_execute_numeric_instruction(&result, &ctx);
  return result;
}
static void test_trigonometric(void) {
  {
    fixture f; qa_binary_value neg_zero = qa_fp_zero(1);
    trig_fixture(&f, &neg_zero, 0x37f); qa_binary_value_free(&neg_zero);
    CHECK(trig_run(&f, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(x87_top_equals_hex(&f.state.x87, "80000000000000000000"));
  }
  {
    fixture f; qa_binary_value z = qa_fp_zero(0);
    trig_fixture(&f, &z, 0x37f); qa_binary_value_free(&z);
    CHECK(trig_run(&f, true).kind == QA_NUMERIC_EXECUTED);
    CHECK(x87_top_equals_hex(&f.state.x87, "3fff8000000000000000"));
  }
  {
    qa_bigint bits; qa_bigint_init(&bits); qa_bigint_set_hex(&bits, "4000c90fdaa22168c235");
    qa_binary_value pi = qa_fp_decode_binary(&bits, 80); qa_bigint_free(&bits);
    fixture f; trig_fixture(&f, &pi, 0x37f); qa_binary_value_free(&pi);
    CHECK(trig_run(&f, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(x87_top_equals_hex(&f.state.x87, "bfbf8000000000000000"));
  }
  {
    qa_binary_value one = qa_fp_from_i64(1);
    fixture f; trig_fixture(&f, &one, 0x7f); qa_binary_value_free(&one);
    CHECK(trig_run(&f, false).kind == QA_NUMERIC_EXECUTED);
    CHECK(x87_top_equals_hex(&f.state.x87, "3ffed76aa47848677021"));
    CHECK((f.state.x87.status_word & 32u) == 32u);
  }
  /* out of range at |x| >= 2^63 */
  {
    qa_bigint bits; qa_bigint_init(&bits); qa_bigint_set_hex(&bits, "8000000000000000");
    qa_binary_value big = qa_fp_from_integer(&bits); qa_bigint_free(&bits);
    fixture f; trig_fixture(&f, &big, 0x37f);
    qa_binary_value before; qa_read_x87_register(&before, &f.state.x87, 0);
    qa_bigint bb; qa_bigint_init(&bb); qa_fp_encode_binary(&bb, &before, 80); qa_binary_value_free(&before);
    CHECK(trig_run(&f, false).kind == QA_NUMERIC_EXECUTED);
    qa_binary_value after; qa_read_x87_register(&after, &f.state.x87, 0);
    qa_bigint ab; qa_bigint_init(&ab); qa_fp_encode_binary(&ab, &after, 80); qa_binary_value_free(&after);
    CHECK(qa_bigint_cmp(&bb, &ab) == 0);
    qa_bigint_free(&bb); qa_bigint_free(&ab);
    CHECK((f.state.x87.status_word & 0x400u) == 0x400u);
    qa_binary_value_free(&big);
  }
  /* FLDPI under all modes (PC=24-bit control 0x7f) */
  for (int mode = 0; mode < 4; mode++) {
    fixture f;
    memset(g_mem_bytes, 0, sizeof g_mem_bytes);
    qa_guest_processor_initial_state initial = {QA_GUEST_I386, 0, 0, 0, 0x7fu | ((uint32_t)mode << 10), 0x1f80, 0xffff};
    qa_create_guest_processor_state(&f.state, &initial);
    f.memory = make_memory(4);
    qa_numeric_instruction ins; memset(&ins, 0, sizeof ins);
    ins.opcode = 0xd9; ins.modrm = 0xeb; ins.has_modrm = true; ins.register_index = 5;
    ins.has_operand = true; ins.operand.kind = QA_OPERAND_REGISTER; ins.operand.index = 3;
    ins.prefix = QA_PREFIX_NONE; ins.operand_bits = 32;
    qa_numeric_context ctx; qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
    qa_numeric_result result; qa_execute_numeric_instruction(&result, &ctx);
    CHECK(result.kind == QA_NUMERIC_EXECUTED);
    CHECK(x87_top_equals_hex(&f.state.x87, mode == 1 || mode == 3 ? "4000c90fdaa22168c234" : "4000c90fdaa22168c235"));
    CHECK((f.state.x87.status_word & 0x220u) == 0);
  }
  /* FPATAN quadrants */
  {
    static const struct { int64_t x; const char *expected; } cases[2] = {
      {1, "3ffec90fdaa22168c235"}, {-1, "400096cbe3f9990e91a8"}};
    for (int i = 0; i < 2; i++) {
      for (int neg = 0; neg < 2; neg++) {
        fixture f;
        memset(g_mem_bytes, 0, sizeof g_mem_bytes);
        qa_guest_processor_initial_state initial = {QA_GUEST_I386, 0, 0, 0, 0x7f, 0x1f80, 0xffff};
        qa_create_guest_processor_state(&f.state, &initial);
        f.memory = make_memory(4);
        qa_binary_value y = qa_fp_from_i64(neg ? -1 : 1), x = qa_fp_from_i64(cases[i].x);
        qa_push_x87(&f.state.x87, &y); qa_push_x87(&f.state.x87, &x);
        qa_binary_value_free(&y); qa_binary_value_free(&x);
        uint32_t prev_top = f.state.x87.status_word & 0x3800u;
        qa_numeric_instruction ins; memset(&ins, 0, sizeof ins);
        ins.opcode = 0xd9; ins.modrm = 0xf3; ins.has_modrm = true; ins.register_index = 6;
        ins.has_operand = true; ins.operand.kind = QA_OPERAND_REGISTER; ins.operand.index = 3;
        ins.prefix = QA_PREFIX_NONE; ins.operand_bits = 32;
        qa_numeric_context ctx; qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
        qa_numeric_result result; qa_execute_numeric_instruction(&result, &ctx);
        CHECK(result.kind == QA_NUMERIC_EXECUTED);
        qa_binary_value top; qa_read_x87_register(&top, &f.state.x87, 0);
        if (neg) {
          qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &top, 80);
          qa_bigint expect; qa_bigint_init(&expect); qa_bigint_set_hex(&expect, cases[i].expected);
          qa_bigint sign_bit; qa_bigint_init(&sign_bit);
          { qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1); qa_bigint_shl(&sign_bit, &one, 79); qa_bigint_free(&one); }
          qa_bigint_or(&expect, &expect, &sign_bit);
          CHECK(qa_bigint_cmp(&bits, &expect) == 0);
          qa_bigint_free(&bits); qa_bigint_free(&expect); qa_bigint_free(&sign_bit);
        } else CHECK(encode_equals_hex(&top, 80, cases[i].expected));
        qa_binary_value_free(&top);
        CHECK((f.state.x87.status_word & 0x3800u) == ((prev_top + 0x800u) & 0x3800u));
        CHECK((f.state.x87.status_word & 32u) == 32u);
      }
    }
  }
  /* FPATAN finite ratios against host atan2 (TS toBeCloseTo 14 digits) */
  {
    static const int ys[4] = {-99, -3, 1, 7};
    static const int xs[4] = {-31, -2, 1, 17};
    for (int yi = 0; yi < 4; yi++) for (int xi = 0; xi < 4; xi++) {
      fixture f;
      memset(g_mem_bytes, 0, sizeof g_mem_bytes);
      qa_guest_processor_initial_state initial = {QA_GUEST_I386, 0, 0, 0, 0x37f, 0x1f80, 0xffff};
      qa_create_guest_processor_state(&f.state, &initial);
      f.memory = make_memory(4);
      qa_binary_value y = qa_fp_from_i64(ys[yi]), x = qa_fp_from_i64(xs[xi]);
      qa_push_x87(&f.state.x87, &y); qa_push_x87(&f.state.x87, &x);
      qa_binary_value_free(&y); qa_binary_value_free(&x);
      qa_numeric_instruction ins; memset(&ins, 0, sizeof ins);
      ins.opcode = 0xd9; ins.modrm = 0xf3; ins.has_modrm = true; ins.register_index = 6;
      ins.has_operand = true; ins.operand.kind = QA_OPERAND_REGISTER; ins.operand.index = 3;
      ins.prefix = QA_PREFIX_NONE; ins.operand_bits = 32;
      qa_numeric_context ctx; qa_numeric_context_init(&ctx, &f.state, &f.memory, &ins);
      qa_numeric_result result; qa_execute_numeric_instruction(&result, &ctx);
      CHECK(result.kind == QA_NUMERIC_EXECUTED);
      double got = qa_read_x87_return(&f.state.x87, QA_X87_FLOAT64);
      double want = atan2((double)ys[yi], (double)xs[xi]);
      if (!(fabs(got - want) < 5e-14)) fprintf(stderr, " atan2(%d,%d): got %.17g want %.17g\n", ys[yi], xs[xi], got, want);
      CHECK(fabs(got - want) < 5e-14);
    }
  }
}

static void check_format(const char *hex64, char conv, bool has_prec, int prec, qa_format_rounding mode,
                         bool windows, bool extended, const char *expected) {
  qa_bigint bits; qa_bigint_init(&bits); qa_bigint_set_hex(&bits, hex64);
  qa_binary_value value = qa_fp_decode_binary(&bits, extended ? 80 : 64);
  qa_bigint_free(&bits);
  char *text = qa_format_float(&value, conv, has_prec, prec, false, mode, windows, extended, 2);
  int ok = text != NULL && strcmp(text, expected) == 0;
  if (!ok) fprintf(stderr, " format %s '%c': got %s want %s\n", hex64, conv, text ? text : "(null)", expected);
  CHECK(ok);
  free(text);
  qa_binary_value_free(&value);
}
static void test_format(void) {
  /* Expected strings produced by running the TS formatFloat under bun. */
  check_format("3ff0000000000000", 'f', false, 0, QA_FORMAT_NEAREST, false, false, "1.000000");
  check_format("3ff0000000000000", 'e', false, 0, QA_FORMAT_NEAREST, false, false, "1.000000e+00");
  check_format("3ff0000000000000", 'g', false, 0, QA_FORMAT_NEAREST, false, false, "1");
  check_format("3ff0000000000000", 'a', false, 0, QA_FORMAT_NEAREST, false, false, "0x1p+0");
  check_format("400921fb54442d18", 'f', true, 2, QA_FORMAT_NEAREST, false, false, "3.14");
  check_format("40fe240000000000", 'e', true, 3, QA_FORMAT_NEAREST, false, false, "1.235e+05");
  check_format("3e80000000000000", 'g', false, 0, QA_FORMAT_NEAREST, false, false, "1.19209e-07");
  check_format("3ff0000000000000", 'a', true, 3, QA_FORMAT_NEAREST, false, false, "0x1.000p+0");
  check_format("3fe0000000000000", 'f', true, 0, QA_FORMAT_LEGACY_NEAREST, false, false, "1");
  check_format("3fe0000000000000", 'f', true, 0, QA_FORMAT_NEAREST, false, false, "0");
  check_format("3fff8000000000000000", 'a', false, 0, QA_FORMAT_NEAREST, false, true, "0x8p-3");
  qa_binary_value inf = qa_fp_zero(0); inf.kind = QA_BINARY_INFINITY;
  char *text = qa_format_float(&inf, 'f', false, 0, false, QA_FORMAT_NEAREST, false, false, 2);
  CHECK(text != NULL && strcmp(text, "inf") == 0);
  free(text);
  qa_binary_value nan_v = qa_fp_indefinite();
  text = qa_format_float(&nan_v, 'F', false, 0, false, QA_FORMAT_NEAREST, true, false, 2);
  CHECK(text != NULL && strcmp(text, "NAN(IND)") == 0);
  free(text);
  qa_binary_value_free(&nan_v);
  qa_binary_value_free(&inf);
  CHECK(strcmp(qa_format_rounding_name(QA_FORMAT_LEGACY_NEAREST), "legacy-nearest") == 0);
}

static void put_u32_le(uint8_t *p, size_t off, uint32_t v) {
  p[off] = (uint8_t)v; p[off + 1] = (uint8_t)(v >> 8); p[off + 2] = (uint8_t)(v >> 16); p[off + 3] = (uint8_t)(v >> 24);
}
static void test_unwind(void) {
  /* Hand-built .eh_frame: CIE v1 "zR" (pcrel|sdata4) + one FDE. */
  uint8_t bytes[64];
  memset(bytes, 0, sizeof bytes);
  size_t p = 0;
  put_u32_le(bytes, p, 16); p += 4;          /* CIE length (record = 4 + 16) */
  put_u32_le(bytes, p, 0); p += 4;           /* CIE id */
  bytes[p++] = 1;                            /* version */
  bytes[p++] = 'z'; bytes[p++] = 'R'; bytes[p++] = 0;
  bytes[p++] = 1;                            /* code alignment (uleb) */
  bytes[p++] = 0x7f;                         /* data alignment (sleb -1) */
  bytes[p++] = 16;                           /* return register (v1 ubyte) */
  bytes[p++] = 1;                            /* augmentation size */
  bytes[p++] = 0x1b;                         /* R: pcrel | sdata4 */
  while (p < 20) bytes[p++] = 0;             /* pad to record end */
  size_t fde = p;
  put_u32_le(bytes, p, 20); p += 4;          /* FDE length */
  put_u32_le(bytes, p, (uint32_t)(p - 0)); p += 4; /* CIE displacement: id_pos - 0 */
  /* initial_location at file offset p; section address 0x1000 => field addr 0x1000+p; want PC 0x4000 */
  put_u32_le(bytes, p, (uint32_t)(0x4000 - (0x1000 + (int)p))); p += 4;
  put_u32_le(bytes, p, 0x20); p += 4;        /* range (sdata4) */
  bytes[p++] = 0;                            /* FDE augmentation size */
  while (p < fde + 4 + 20) bytes[p++] = 0;
  qa_elf_section sections[1] = {{".eh_frame", 0, 0x1000, 0, p}};
  qa_elf_inspection elf = {8, NULL, 0, sections, 1};
  qa_mapped_guest_memory memory = make_memory(8);
  qa_guest_unwind_region *regions = NULL;
  size_t count = 0;
  CHECK(qa_read_elf_unwind(&elf, bytes, p, &memory, 0, &regions, &count));
  CHECK(count == 1);
  if (count == 1) {
    CHECK(regions[0].start.byte_offset == 0x4000);
    CHECK(regions[0].end.byte_offset == 0x4020);
    CHECK(regions[0].format == QA_UNWIND_ELF_EH_FRAME);
    CHECK(regions[0].metadata_len == p);
  }
  qa_elf_unwind_regions_free(regions, count);
  /* truncated DWARF aborts */
  uint8_t bad[8]; memset(bad, 0xff, sizeof bad);
  qa_elf_section bad_sections[1] = {{".eh_frame", 0, 0x1000, 0, sizeof bad}};
  qa_elf_inspection bad_elf = {8, NULL, 0, bad_sections, 1};
  CHECK_ABORTS((void)qa_read_elf_unwind(&bad_elf, bad, sizeof bad, &memory, 0, &regions, &count));
  CHECK(strcmp(qa_guest_unwind_format_name(QA_UNWIND_ELF_DEBUG_FRAME), "elf-debug-frame") == 0);
}

static qa_x64_operand reg_op(qa_guest_register reg, int width) {
  qa_x64_operand op; memset(&op, 0, sizeof op);
  op.kind = QA_X64_OPERAND_REGISTER; op.reg = reg; op.width = width; op.address_bits = 64;
  return op;
}
static qa_x64_operand mem_op(int width, int64_t displacement) {
  qa_x64_operand op; memset(&op, 0, sizeof op);
  op.kind = QA_X64_OPERAND_MEMORY; op.width = width; op.displacement = displacement; op.address_bits = 64;
  return op;
}
static void test_plan(void) {
  fixture f; fixture_init(&f);
  qa_x64_plan_operation op;
  qa_x64_semantic_plan plan;
  /* move immediate -> rax */
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_MOVE;
  op.destination = reg_op(QA_REG_RAX, 64);
  op.source.is_immediate = true; op.source.immediate = 0x2a;
  qa_make_x64_plan(&plan, &op, 0x1005, false);
  CHECK(!plan.ends_block);
  CHECK(qa_execute_x64_plan(&plan, &f.memory, &f.state).kind == QA_X64_ADVANCE);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 64, false) == 0x2a);
  /* move rax -> [0x2000] ends block */
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_MOVE;
  op.destination = mem_op(64, 0x2000);
  op.source.is_immediate = false; op.source.operand = reg_op(QA_REG_RAX, 64);
  qa_make_x64_plan(&plan, &op, 0x1005, false);
  CHECK(plan.ends_block);
  CHECK(qa_execute_x64_plan(&plan, &f.memory, &f.state).kind == QA_X64_ADVANCE);
  CHECK(qa_memory_read_u64(&f.memory, addr_at(0x2000)) == 0x2a);
  /* alu add immediate sets flags; cmp does not end block */
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_ALU; op.alu_operation = QA_ALU_ADD;
  op.destination = reg_op(QA_REG_RAX, 64);
  op.source.is_immediate = true; op.source.immediate = 1;
  qa_make_x64_plan(&plan, &op, 0x1005, false);
  CHECK(qa_execute_x64_plan(&plan, &f.memory, &f.state).kind == QA_X64_ADVANCE);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RAX, 64, false) == 0x2b);
  /* conditional branch on zero flag (clear after add) -> advance; force zero -> branch */
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_BRANCH; op.has_condition = true; op.condition = 4; op.displacement = 0x10;
  qa_make_x64_plan(&plan, &op, 0x2000, false);
  CHECK(plan.ends_block);
  CHECK(qa_execute_x64_plan(&plan, &f.memory, &f.state).kind == QA_X64_ADVANCE);
  qa_processor_flags_set(&f.state.flags, QA_FLAG_ZERO, true);
  qa_x64_flow flow = qa_execute_x64_plan(&plan, &f.memory, &f.state);
  CHECK(flow.kind == QA_X64_BRANCH && flow.target == 0x2010);
  /* push/pop roundtrip */
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_PUSH; op.width = 64;
  op.source.is_immediate = true; op.source.immediate = 0xfeed;
  qa_make_x64_plan(&plan, &op, 0x2000, false);
  uint64_t rsp_before = qa_integer_register_file_read(&f.state.registers, QA_REG_RSP, 64, false);
  CHECK(qa_execute_x64_plan(&plan, &f.memory, &f.state).kind == QA_X64_ADVANCE);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RSP, 64, false) == rsp_before - 8);
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_POP; op.width = 64; op.destination = reg_op(QA_REG_RBX, 64);
  qa_make_x64_plan(&plan, &op, 0x2000, false);
  CHECK(qa_execute_x64_plan(&plan, &f.memory, &f.state).kind == QA_X64_ADVANCE);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RBX, 64, false) == 0xfeed);
  CHECK(qa_integer_register_file_read(&f.state.registers, QA_REG_RSP, 64, false) == rsp_before);
  /* lock on register move aborts (X64ProcessorFault(6)) */
  memset(&op, 0, sizeof op);
  op.kind = QA_X64_PLAN_MOVE; op.destination = reg_op(QA_REG_RAX, 64);
  op.source.is_immediate = true;
  qa_make_x64_plan(&plan, &op, 0x2000, true);
  CHECK_ABORTS((void)qa_execute_x64_plan(&plan, &f.memory, &f.state));
  CHECK(strcmp(qa_x64_plan_kind_name(QA_X64_PLAN_NUMERIC_MEMORY), "numeric-memory") == 0);
}

int main(void) {
  test_registers();
  test_arithmetic();
  test_binary();
  test_x87_execution();
  test_sse_execution();
  test_trigonometric();
  test_format();
  test_unwind();
  test_plan();
  printf("test_guest_leaves: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
