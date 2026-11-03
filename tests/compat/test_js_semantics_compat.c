/* JS-semantics regressions in compat/qvm and compat/qc conversions. Expected
 * outcomes are the TS behavior printed under bun:
 *   ["ENTER","CONST","LEAVE","ENTER","CONST","LEAVE"][3.5] is undefined, so a
 *   fractional or NaN private invocation index finds no OP_ENTER and throws;
 *   64.5 % 4 !== 0, so a fractional counter address fails the alignment test;
 *   signedWord(2.5), signedWord(2147483648), signedWord(NaN) throw RangeError,
 *   signedWord(-5) is -5;
 *   (Math.trunc(1e30) & 256) !== 0 is false.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qc/world_host.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

typedef struct op { int32_t opcode; bool has; int32_t operand; } op;
#define O(x) {QA_QVM_OP_##x, false, 0}
#define OA(x, v) {QA_QVM_OP_##x, true, (v)}

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}

static size_t build(uint8_t *out, const op *ops, size_t count, int32_t bss) {
  size_t length = 0;
  uint8_t *code = out + 32;
  for (size_t i = 0; i < count; i++) {
    code[length++] = (uint8_t)ops[i].opcode;
    if (ops[i].has) {
      put_i32(code, length, ops[i].operand);
      length += 4;
    }
  }
  int32_t header[8] = {0x12721444, (int32_t)count, 32, (int32_t)length, 32 + (int32_t)length, 0, 0, bss};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  return 32 + length;
}

static double unexpected_trap(void *ctx, qa_qvm_syscall *call) {
  (void)ctx;
  (void)call;
  qa_range_error("Unexpected syscall");
}

typedef struct vm { qa_qvm_image image; qa_qvm_interpreter *interp; } vm;
static void vm_open(vm *out, const op *ops, size_t count, int32_t bss) {
  static uint8_t bytes[65536];
  size_t length = build(bytes, ops, count, bss);
  CHECK(qa_qvm_parse(bytes, length, "<qvm>", &out->image));
  out->interp = qa_qvm_interpreter_create(&out->image, (qa_qvm_interpreter_system_call){NULL, unexpected_trap},
                                          NULL, NULL, "interpreted", NULL);
  CHECK(out->interp != NULL);
}
static void vm_close(vm *v) {
  qa_qvm_interpreter_destroy(v->interp);
  qa_qvm_image_destroy(&v->image);
}

static bool throws_with(void (*fn)(void *), void *ctx, const char *needle) {
  char message[512];
  if (qa_range_error_catch(fn, ctx, message, sizeof message)) return false;
  if (strstr(message, needle) == NULL) {
    fprintf(stderr, "  unexpected message: %s (wanted %s)\n", message, needle);
    return false;
  }
  return true;
}

static const double no_args[1] = {0};
typedef struct invoke_job { qa_qvm_interpreter *interp; double index; double result; } invoke_job;
static void invoke_body(void *ctx) {
  invoke_job *job = ctx;
  job->result = qa_qvm_interpreter_invoke(job->interp, no_args, 0, job->index, NULL);
}

/* Fractional and NaN private invocation indices: C truncated 3.5 to the OP_ENTER at 3. */
static void test_invocation_index(void) {
  const op ops[] = {OA(ENTER, 16), OA(CONST, 0), OA(LEAVE, 16),
                    OA(ENTER, 16), OA(CONST, 77), OA(LEAVE, 16)};
  vm v;
  vm_open(&v, ops, 6, 4096);
  invoke_job ok = {v.interp, 3, 0};
  invoke_body(&ok);
  CHECK(ok.result == 77);
  const double bad[] = {3.5, NAN, 4, -1};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    invoke_job job = {v.interp, bad[i], 0};
    CHECK(throws_with(invoke_body, &job, "QVM private invocation requires an original function entry"));
  }
  vm_close(&v);
}

typedef struct counter_job { qa_qvm_interpreter *interp; double address; double result; } counter_job;
static double counter_execute(void *ctx) {
  counter_job *job = ctx;
  return qa_qvm_interpreter_invoke(job->interp, no_args, 0, 0, NULL);
}
static void counter_body(void *ctx) {
  counter_job *job = ctx;
  double functions[1] = {0};
  job->result = qa_qvm_interpreter_evaluate_counter(job->interp, job->address, 0, functions, 1,
                                                    counter_execute, job, NULL);
}

/* A fractional counter address: C truncated 64.5 to the aligned word 64. */
static void test_counter_address(void) {
  const op ops[] = {OA(ENTER, 16), OA(CONST, 64), OA(CONST, 64), O(LOAD4),
                    OA(CONST, 5), O(ADD), O(STORE4), OA(CONST, 0), OA(LEAVE, 16)};
  vm v;
  vm_open(&v, ops, 9, 2049);
  const double bad[] = {64.5, NAN};
  for (size_t i = 0; i < 2; i++) {
    counter_job job = {v.interp, bad[i], 0};
    CHECK(throws_with(counter_body, &job, "one live aligned source word"));
  }
  vm_close(&v);
}

typedef struct word_job { double value; int32_t result; } word_job;
static void word_body(void *ctx) {
  word_job *job = ctx;
  job->result = qa_qvm_host_word(job->value);
}
static void test_host_word(void) {
  word_job ok = {-5, 0};
  word_body(&ok);
  CHECK(ok.result == -5);
  word_job low = {-2147483648.0, 0};
  word_body(&low);
  CHECK(low.result == INT32_MIN);
  const double bad[] = {2.5, 2147483648.0, NAN};
  for (size_t i = 0; i < 3; i++) {
    word_job job = {bad[i], 0};
    CHECK(throws_with(word_body, &job, "QVM host value must be a signed 32-bit integer"));
  }
}

/* (Math.trunc(flags) & 256) on a guest float far outside int64: no item padding. */
static void test_link_bounds(void) {
  const qa_numeric_operations *n = qa_create_numeric_operations(&qa_q1_donor_profile);
  qa_body_state state;
  memset(&state, 0, sizeof state);
  qa_bounds item = qa_compat_qc_link_bounds(state, 256, n);
  qa_bounds huge = qa_compat_qc_link_bounds(state, 1e30, n);
  CHECK(item.min.x == -15);
  CHECK(huge.min.x == -1);
}

int main(void) {
  test_invocation_index();
  test_counter_address();
  test_host_word();
  test_link_bounds();
  printf("test_js_semantics_compat: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
