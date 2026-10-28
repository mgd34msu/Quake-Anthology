/*
 * Port of tests/guest/runtime/format/{format,scan}.test.ts,
 * tests/guest/runtime/system-v/runtime.test.ts (authored cases) and
 * tests/guest/runtime/windows/services.test.ts. Expected-abort cases
 * (TS toThrow of uncaught errors) run in a forked child. The opaque
 * qa_guest_call_runner is backed by a static byte buffer (the runner
 * header exposes no allocator). Corpus/installed cases are not ported.
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

#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"
#include "qa/guest/abi/runner.h"
#include "qa/guest/core/callbacks.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/floating_point/binary.h"
#include "qa/guest/runtime/common/format.h"
#include "qa/guest/runtime/common/format/float.h"
#include "qa/guest/runtime/common/format/scan.h"
#include "qa/guest/runtime/common/memory.h"
#include "qa/guest/runtime/system_v.h"
#include "qa/guest/runtime/windows.h"
#include "qa/guest/x64/cpu.h"
#include "qa/guest/x86/cpu.h"

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

static qa_guest_core_address A(uint64_t v) { qa_guest_core_address a = {v}; return a; }
static qa_guest_module_identity test_module = {"test:guest-runtime", "authored-runtime",
                                               "digest", "1"};
static qa_guest_module_ref sparse_module = {"test:guest-runtime", "authored-runtime",
                                            "digest", "1"};

typedef struct test_ext {
  qa_sparse_guest_memory *mem;
  qa_guest_mapping buf[256];
} test_ext;
static qa_guest_core_address ext_map(void *ctx, uint64_t base, size_t len, qa_guest_permissions perm,
                                const char *label, const uint8_t *bytes, size_t blen) {
  test_ext *e = ctx;
  qa_guest_map_options o;
  memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = len; o.permissions = perm; o.label = label;
  o.bytes = bytes; o.bytes_length = blen;
  return qa_sparse_map(e->mem, &o);
}
static qa_guest_core_address ext_alloc(void *ctx, size_t len, bool ha, uint64_t al, bool hp,
                                  qa_guest_permissions perm, const char *label) {
  test_ext *e = ctx;
  qa_guest_allocation_options o;
  memset(&o, 0, sizeof o);
  o.byte_length = len; o.alignment = ha ? al : 0; o.permissions = perm;
  o.has_permissions = hp; o.label = label;
  return qa_sparse_allocate(e->mem, &o);
}
static void ext_unmap(void *ctx, qa_guest_core_address a, size_t len) {
  qa_sparse_unmap(((test_ext *)ctx)->mem, a, len);
}
static void ext_protect(void *ctx, qa_guest_core_address a, size_t len, qa_guest_permissions p) {
  qa_sparse_protect(((test_ext *)ctx)->mem, a, len, p);
}
static const qa_guest_mapping *ext_mappings(void *ctx, size_t *count) {
  test_ext *e = ctx;
  *count = qa_sparse_mappings(e->mem, e->buf, 256);
  if (*count > 256) *count = 256;
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

typedef struct fixture {
  qa_sparse_guest_memory mem;
  test_ext ext_store;
  qa_mapped_memory_ext ext;
  qa_guest_callback_table callbacks;
  qa_guest_processor_state state;
  qa_x64_cpu x64;
  qa_i386_cpu x86;
  qa_guest_cpu cpu;
  _Alignas(16) uint8_t runner_storage[16384];
  qa_guest_call_runner *runner;
  qa_guest_core_call_context context;
  int width;
} fixture;

static bool is_host_call(void *ctx, qa_guest_core_address address) {
  return qa_guest_callback_table_resolve((qa_guest_callback_table *)ctx, address) != NULL;
}
static qa_guest_execution_stop run_x64(void *ctx, int64_t budget, bool has_return,
                                       qa_guest_core_address return_address) {
  return qa_x64_cpu_run((qa_x64_cpu *)ctx, budget, has_return, return_address);
}
static qa_guest_execution_stop run_x86(void *ctx, int64_t budget, bool has_return,
                                       qa_guest_core_address return_address) {
  return qa_i386_cpu_run((qa_i386_cpu *)ctx, budget, has_return, return_address);
}
static void smap(fixture *f, uint64_t base, size_t len, qa_guest_permissions perm,
                 const uint8_t *bytes, size_t blen) {
  qa_guest_map_options o;
  memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = len; o.permissions = perm; o.bytes = bytes;
  o.bytes_length = blen;
  (void)qa_sparse_map(&f->mem, &o);
}
static void fixture_init(fixture *f, int width) {
  memset(f, 0, sizeof *f);
  f->width = width;
  qa_sparse_guest_memory_options opts;
  memset(&opts, 0, sizeof opts);
  opts.module = sparse_module;
  opts.pointer_bytes = width;
  opts.allocation_base = 0x50000000;
  opts.has_allocation_base = true;
  qa_sparse_guest_memory_init(&f->mem, &opts);
  smap(f, 0x10000, 65536, QA_PERM_READ_WRITE, NULL, 0);
  static const uint8_t sentinel_byte[1] = {0xcc};
  smap(f, 0x30000, 4096, QA_PERM_READ_EXECUTE, sentinel_byte, 1);
  make_ext(&f->ext, &f->ext_store, &f->mem);
  qa_guest_callback_table_init(&f->callbacks, &f->mem);
  qa_guest_processor_initial_state init = {width == 4 ? QA_GUEST_I386 : QA_GUEST_X86_64,
                                           0x30000, 0x20000, 2, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&f->state, &init);
  if (width == 8) qa_x64_cpu_init(&f->x64, &f->state, &f->mem, NULL, NULL, &f->callbacks);
  else qa_i386_cpu_init(&f->x86, &f->state, &f->mem, is_host_call, &f->callbacks, NULL, NULL);
  memset(&f->cpu, 0, sizeof f->cpu);
  f->cpu.state = &f->state;
  f->cpu.memory = &f->mem.base;
  f->cpu.memory_ext = &f->ext;
  f->cpu.module = test_module;
  f->cpu.run_ctx = width == 8 ? (void *)&f->x64 : (void *)&f->x86;
  f->cpu.run = width == 8 ? run_x64 : run_x86;
  f->runner = (qa_guest_call_runner *)f->runner_storage;
  qa_guest_call_runner_options runner_options;
  memset(&runner_options, 0, sizeof runner_options);
  runner_options.cpu = &f->cpu;
  runner_options.callbacks = &f->callbacks;
  runner_options.return_address = A(0x30000);
  (void)qa_guest_call_runner_init(f->runner, runner_options);
  memset(&f->context, 0, sizeof f->context);
  f->context.module = test_module;
  f->context.callback_kind = QA_CALLBACK_REF_TYPESCRIPT;
}
static qa_guest_core_address guest_alloc(fixture *f, size_t len) {
  return qa_gmem_allocate(&f->ext, len, false, 0, false, QA_PERM_READ_WRITE, "test");
}
static qa_guest_core_address guest_text(fixture *f, const char *value) {
  size_t len = 0;
  uint8_t *bytes = qa_string_bytes(value, false, &len);
  qa_guest_core_address address = guest_alloc(f, len);
  qa_memory_write(&f->mem.base, address, bytes, len);
  free(bytes);
  return address;
}
static qa_call_result invoke_at(fixture *f, qa_guest_core_address address,
                                const qa_call_value *args, size_t count) {
  const qa_guest_host_callback *callback =
      qa_guest_callback_table_resolve(&f->callbacks, address);
  if (callback == NULL) qa_range_error("Missing test callback");
  qa_guest_call_request request;
  memset(&request, 0, sizeof request);
  request.target = address;
  request.signature = callback->signature;
  request.arguments = args;
  request.argument_count = count;
  request.context = f->context;
  request.instruction_budget = 100000;
  return qa_guest_call_runner_invoke(f->runner, &request);
}
static qa_call_value ptr_arg(bool has, qa_guest_core_address a) { return qa_call_pointer(has, a); }
static qa_call_value size_arg(fixture *f, uint64_t value) {
  return f->width == 4 ? qa_call_uint32((uint32_t)value) : qa_call_uint64(value);
}
static bool result_is_int(qa_call_result r, int64_t value) {
  return r.kind == QA_CALL_INT32 && (int64_t)r.number == value;
}
static bool result_is_ptr(qa_call_result r, bool has, uint64_t offset) {
  return r.kind == QA_CALL_POINTER && r.pointer_has == has &&
         (!has || r.pointer.byte_offset == offset);
}

/* ---------- layouts (iostream.ts/stdio.ts pure functions) ---------- */
static void test_layouts(void) {
  qa_system_v_ios_layout l8 = qa_system_v_ios_layout_for(8, false);
  CHECK(l8.size == 264 && l8.precision == 8 && l8.tie == 216 && l8.buffer == 232 &&
        l8.fill_initialized == 225 && l8.num_get == 256);
  qa_system_v_ios_layout l8w = qa_system_v_ios_layout_for(8, true);
  CHECK(l8w.fill_initialized == 228);
  qa_system_v_ios_layout l4 = qa_system_v_ios_layout_for(4, false);
  CHECK(l4.size == 136 && l4.buffer == 120 && l4.ctype == 124 && l4.num_get == 132);
  qa_system_v_ios_layout l4w = qa_system_v_ios_layout_for(4, true);
  CHECK(l4w.size == 140 && l4w.buffer == 124 && l4w.num_get == 136);
  qa_system_v_file_layout f8 = qa_system_v_file_layout_for(8);
  CHECK(f8.size == 216 && f8.descriptor == 112 && f8.mode == 192 && f8.old_offset == 120);
  qa_system_v_file_layout f4 = qa_system_v_file_layout_for(4);
  CHECK(f4.size == 148 && f4.descriptor == 56 && f4.wide_data == 88);
}

/* ---------- format/float.test (formatFloat cases) ---------- */
static char *format_number(double value, char code, bool has_precision, int precision,
                           bool alternate, qa_format_rounding mode, bool windows) {
  uint64_t bits = qa_float64_bits(value);
  qa_bigint bigint;
  qa_bigint_init(&bigint);
  qa_bigint_set_u64(&bigint, bits);
  qa_binary_value binary = qa_fp_decode_binary(&bigint, 64);
  qa_bigint_free(&bigint);
  char *out = qa_format_float(&binary, code, has_precision, precision, alternate, mode,
                              windows, false, 2);
  qa_binary_value_free(&binary);
  return out;
}
static void check_float(double value, char code, bool has_precision, int precision,
                        const char *expected) {
  char *out = format_number(value, code, has_precision, precision, false, QA_FORMAT_NEAREST,
                            false);
  CHECK(out != NULL && strcmp(out, expected) == 0);
  free(out);
}
static void test_format_float(void) {
  check_float(2.5, 'f', true, 0, "2");
  check_float(3.5, 'f', true, 0, "4");
  check_float(2.25, 'f', true, 1, "2.2");
  char *legacy = format_number(2.25, 'f', true, 1, false, QA_FORMAT_LEGACY_NEAREST, true);
  CHECK(legacy != NULL && strcmp(legacy, "2.3") == 0);
  free(legacy);
  check_float(1e21, 'f', true, 1, "1000000000000000000000.0");
  check_float(0.1, 'f', true, 20, "0.10000000000000000555");
  check_float(999.5, 'g', true, 3, "1e+03");
  check_float(0.00009999, 'g', true, 3, "0.0001");
  check_float(16, 'g', true, 17, "16");
  char *alt = format_number(16, 'g', true, 4, true, QA_FORMAT_NEAREST, false);
  CHECK(alt != NULL && strcmp(alt, "16.00") == 0);
  free(alt);
  check_float(1.5, 'a', false, 0, "0x1.8p+0");
  check_float(1.5, 'a', true, 0, "0x2p+0");
  check_float(4.9406564584124654e-324, 'a', false, 0, "0x0.0000000000001p-1022");
  char *down = format_number(-2.25, 'f', true, 1, false, QA_FORMAT_DOWN, false);
  CHECK(down != NULL && strcmp(down, "2.3") == 0);
  free(down);
  /* x87 extended value 0x3fff8000000000000001 (80-bit). */
  qa_bigint bigint;
  qa_bigint_init(&bigint);
  qa_bigint_set_hex(&bigint, "3fff8000000000000001");
  qa_binary_value extended = qa_fp_decode_binary(&bigint, 80);
  qa_bigint_free(&bigint);
  char *g = qa_format_float(&extended, 'g', true, 21, false, QA_FORMAT_NEAREST, false, true, 2);
  CHECK(g != NULL && strcmp(g, "1.00000000000000000011") == 0);
  free(g);
  char *a = qa_format_float(&extended, 'a', false, 0, false, QA_FORMAT_NEAREST, false, true, 2);
  CHECK(a != NULL && strcmp(a, "0x8.000000000000001p-3") == 0);
  free(a);
  qa_binary_value_free(&extended);
}

/* ---------- formatGuestBuffer direct (format.test.ts last case) ---------- */
static void test_format_buffer(void) {
  fixture f;
  fixture_init(&f, 8);
  qa_mapped_guest_memory *m = &f.mem.base;
  qa_guest_core_address buffer = guest_alloc(&f, 256);
  qa_guest_core_address args_addr = guest_alloc(&f, 64);
  qa_gmem_write_i32(m, args_addr, -8);
  qa_gmem_write_i32(m, qa_memory_offset(m, args_addr, 8), 3);
  qa_gmem_write_i32(m, qa_memory_offset(m, args_addr, 16), -7);
  qa_memory_write_u64(m, qa_memory_offset(m, args_addr, 24), 0x12345678ffffffffULL);
  qa_memory_write_u64(m, qa_memory_offset(m, args_addr, 32), 255);
  qa_gmem_write_pointer(m, qa_memory_offset(m, args_addr, 40), true,
                        guest_text(&f, "\xff""abc"));
  qa_memory_write_u64(m, qa_memory_offset(m, args_addr, 48), 0x1234);
  qa_guest_format_request request;
  memset(&request, 0, sizeof request);
  request.memory = m;
  request.memory_ext = &f.ext;
  request.dialect = QA_FORMAT_WINDOWS;
  request.has_buffer = true; request.buffer = buffer;
  request.capacity = 256;
  request.has_format = true;
  request.format = guest_text(&f, "%*.*d|%ld|%hhd|%.2s|%p");
  request.has_arguments = true; request.arguments = args_addr;
  request.termination = QA_TERMINATION_C99;
  request.exponent_digits = 2;
  qa_guest_format_result result = qa_format_guest_buffer(&request);
  CHECK(result.result == 34 && !result.has_errno);
  char *text = qa_read_string(m, &f.ext, buffer, 1048576);
  CHECK(strcmp(text, "-007    |-1|-1|""\xff""a|0000000000001234") == 0);
  free(text);
  qa_guest_format_request overflow = request;
  overflow.has_buffer = false;
  overflow.capacity = 0;
  overflow.format = guest_text(&f, "%2147483647d!");
  qa_guest_format_result overflow_result = qa_format_guest_buffer(&overflow);
  CHECK(overflow_result.result == -1 && overflow_result.has_errno &&
        overflow_result.errno_value == 132);
  qa_sparse_guest_memory_destroy(&f.mem);
}

/* ---------- scanWindowsBuffer direct (scan.test.ts subset) ---------- */
static int scan_direct(fixture *f, qa_guest_core_address input, int64_t capacity,
                       const char *format, qa_guest_core_address args_addr,
                       qa_guest_scan_catch *catch_out) {
  qa_scan_windows_buffer_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f->mem.base;
  options.memory_ext = &f->ext;
  options.input = input;
  options.capacity = capacity;
  options.format = guest_text(f, format);
  options.has_arguments = true;
  options.arguments = args_addr;
  return qa_scan_windows_buffer(&options, catch_out);
}
static void test_scan(void) {
  fixture f;
  fixture_init(&f, 8);
  qa_mapped_guest_memory *m = &f.mem.base;
  qa_guest_core_address destinations = guest_alloc(&f, 32);
  qa_guest_core_address args_addr = guest_alloc(&f, 24);
  for (int i = 0; i < 3; i++)
    qa_gmem_write_pointer(m, qa_memory_offset(m, args_addr, i * 8), true,
                          qa_memory_offset(m, destinations, i * 8));
  qa_guest_scan_catch catch_;
  CHECK(scan_direct(&f, guest_text(&f, "-1768.0"), INT64_MAX, "%lf", args_addr, &catch_) == 1);
  CHECK(qa_gmem_read_f64(m, destinations) == -1768.0);
  CHECK(scan_direct(&f, guest_text(&f, " -12.5 010"), INT64_MAX, "%f %i", args_addr,
                    &catch_) == 2);
  CHECK(qa_gmem_read_f32(m, destinations) == -12.5);
  CHECK(qa_gmem_read_i32(m, qa_memory_offset(m, destinations, 8)) == 8);
  CHECK(scan_direct(&f, guest_text(&f, "+0x2a"), INT64_MAX, "%i", args_addr, &catch_) == 1);
  CHECK(qa_gmem_read_i32(m, destinations) == 42);
  CHECK(scan_direct(&f, guest_text(&f, "12345"), INT64_MAX, "%3d%d", args_addr, &catch_) == 2);
  CHECK(qa_gmem_read_i32(m, destinations) == 123);
  CHECK(qa_gmem_read_i32(m, qa_memory_offset(m, destinations, 8)) == 45);
  CHECK(scan_direct(&f, guest_text(&f, ""), INT64_MAX, "%lf", args_addr, &catch_) == -1);
  CHECK(scan_direct(&f, guest_text(&f, "x"), INT64_MAX, "%lf", args_addr, &catch_) == 0);
  CHECK(scan_direct(&f, guest_text(&f, "-0"), INT64_MAX, "%lf", args_addr, &catch_) == 1);
  CHECK(qa_memory_read_u64(m, destinations) == 0x8000000000000000ULL);
  CHECK(scan_direct(&f, guest_text(&f, "4.9406564584124654e-324"), INT64_MAX, "%lf",
                    args_addr, &catch_) == 1);
  CHECK(qa_memory_read_u64(m, destinations) == 1);
  CHECK(scan_direct(&f, guest_text(&f, "1"), INT64_MAX, "%s", args_addr, &catch_) == -2);
  qa_sparse_guest_memory_destroy(&f.mem);
}

/* ---------- System V runtime ---------- */
typedef struct sv_output { char text[256]; size_t len; int flushes[16]; size_t flush_count;
                           uint8_t input[8]; size_t input_len; bool input_used; } sv_output;
static bool sv_now(void *ctx, int64_t *out) { (void)ctx; *out = 123456; return true; }
static int sv_out(void *ctx, qa_system_v_stream stream, const uint8_t *bytes, size_t len) {
  sv_output *o = ctx;
  (void)stream;
  memcpy(o->text + o->len, bytes, len);
  o->len += len;
  o->text[o->len] = '\0';
  return (int)len;
}
static int sv_flush(void *ctx, qa_system_v_stream stream) {
  sv_output *o = ctx;
  if (o->flush_count < 16) o->flushes[o->flush_count] = stream;
  o->flush_count++;
  return 0;
}
static size_t sv_in(void *ctx, size_t maximum, uint8_t *out) {
  sv_output *o = ctx;
  if (o->input_used) return 0;
  o->input_used = true;
  size_t n = o->input_len < maximum ? o->input_len : maximum;
  memcpy(out, o->input, n);
  return n;
}
static qa_call_result sv_call(fixture *f, qa_system_v_guest_runtime *rt, const char *library,
                              const char *name, const char *version, const qa_call_value *args,
                              size_t count) {
  qa_guest_core_address address = {0};
  if (!qa_system_v_resolve_address(&rt->host, library, name, version, &address))
    qa_range_error("Missing System V test service %s", name);
  return invoke_at(f, address, args, count);
}
typedef struct sv_fixture {
  fixture f;
  qa_system_v_guest_runtime rt;
  sv_output out;
} sv_fixture;
static void sv_init(sv_fixture *s, int width) {
  fixture_init(&s->f, width);
  memset(&s->out, 0, sizeof s->out);
  s->out.input[0] = 104; s->out.input[1] = 105; s->out.input_len = 2;
  qa_system_v_runtime_options options;
  memset(&options, 0, sizeof options);
  options.memory = &s->f.mem.base;
  options.memory_ext = &s->f.ext;
  options.callbacks = &s->f.callbacks;
  options.module = test_module;
  options.has_capabilities = true;
  options.capabilities.ctx = &s->out;
  options.capabilities.now_seconds = sv_now;
  options.capabilities.standard_input = sv_in;
  options.capabilities.standard_output = sv_out;
  options.capabilities.standard_flush = sv_flush;
  qa_system_v_guest_runtime_init(&s->rt, &options);
  qa_system_v_guest_runtime_attach_runner(&s->rt, s->f.runner);
}
static void test_system_v_heap_guard_qsort(void) {
  sv_fixture s;
  sv_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_call_value malloc_args[] = {size_arg(f, 16)};
  qa_call_result allocated = sv_call(f, &s.rt, "libc.so.6", "malloc", NULL, malloc_args, 1);
  CHECK(allocated.kind == QA_CALL_POINTER && allocated.pointer_has);
  qa_guest_core_address allocation = allocated.pointer;
  const uint8_t values[16] = {7, 0, 0, 0, 2, 0, 0, 0, 9, 0, 0, 0, 1, 0, 0, 0};
  qa_memory_write(m, allocation, values, 16);
  static const uint8_t code[] = {0x8b, 0x07, 0x2b, 0x06, 0xc3};
  smap(f, 0x40000, 4096, QA_PERM_READ_EXECUTE, code, sizeof code);
  qa_call_value qsort_args[] = {ptr_arg(true, allocation), size_arg(f, 4), size_arg(f, 4),
                                ptr_arg(true, A(0x40000))};
  qa_call_result sorted = sv_call(f, &s.rt, "libc.so.6", "qsort", NULL, qsort_args, 4);
  CHECK(sorted.kind == QA_CALL_VOID);
  const int expected[4] = {1, 2, 7, 9};
  for (int i = 0; i < 4; i++)
    CHECK(qa_gmem_read_i32(m, qa_memory_offset(m, allocation, i * 4)) == expected[i]);
  bool qsort_reached = false;
  for (size_t i = 0; i < qa_system_v_guest_runtime_coverage_count(&s.rt); i++) {
    qa_system_v_import_coverage c = qa_system_v_guest_runtime_coverage(&s.rt, i);
    if (strcmp(c.name, "qsort") == 0 && c.version == NULL) qsort_reached = c.reached == 1;
  }
  CHECK(qsort_reached);
  qa_guest_core_address guard = qa_system_v_allocate(&s.rt.host, 8);
  qa_call_value guard_args[] = {ptr_arg(true, guard)};
  CHECK(result_is_int(sv_call(f, &s.rt, "libstdc++.so.6", "__cxa_guard_acquire", NULL,
                              guard_args, 1), 1));
  CHECK_ABORTS((void)sv_call(f, &s.rt, "libstdc++.so.6", "__cxa_guard_acquire", NULL,
                             guard_args, 1));
  (void)sv_call(f, &s.rt, "libstdc++.so.6", "__cxa_guard_release", NULL, guard_args, 1);
  CHECK(qa_memory_read_u8(m, guard) == 1);
  CHECK(result_is_int(sv_call(f, &s.rt, "libstdc++.so.6", "__cxa_guard_acquire", NULL,
                              guard_args, 1), 0));
  qa_call_value chk_args[] = {ptr_arg(true, allocation), ptr_arg(true, allocation),
                              size_arg(f, 16), size_arg(f, 8)};
  CHECK_ABORTS((void)sv_call(f, &s.rt, "libc.so.6", "__memcpy_chk", NULL, chk_args, 4));
  qa_call_value free_args[] = {ptr_arg(true, allocation)};
  (void)sv_call(f, &s.rt, "libc.so.6", "free", NULL, free_args, 1);
  qa_system_v_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
static void test_system_v_format_service(void) {
  sv_fixture s;
  sv_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_guest_core_address service_addr = {0};
  CHECK(qa_system_v_resolve_address(&s.rt.host, "libc.so.6", "__vsnprintf_chk", "GLIBC_2.3.4",
                                    &service_addr));
  qa_guest_core_address buffer = guest_alloc(f, 256);
  qa_guest_core_address registers = guest_alloc(f, 176);
  qa_guest_core_address stack = guest_alloc(f, 64);
  qa_guest_core_address descriptor = guest_alloc(f, 24);
  qa_guest_core_address label = guest_text(f, "guest");
  qa_guest_core_address count_addr = guest_alloc(f, 8);
  qa_memory_write_u32(m, descriptor, 40);
  qa_memory_write_u32(m, qa_memory_offset(m, descriptor, 4), 160);
  qa_gmem_write_pointer(m, qa_memory_offset(m, descriptor, 8), true, stack);
  qa_gmem_write_pointer(m, qa_memory_offset(m, descriptor, 16), true, registers);
  qa_gmem_write_i64(m, qa_memory_offset(m, registers, 40), -42);
  qa_gmem_write_f64(m, qa_memory_offset(m, registers, 160), 2.5);
  qa_gmem_write_pointer(m, stack, true, label);
  qa_memory_write_u64(m, qa_memory_offset(m, stack, 8), UINT64_MAX);
  qa_gmem_write_f64(m, qa_memory_offset(m, stack, 16), 1.25);
  qa_gmem_write_pointer(m, qa_memory_offset(m, stack, 24), true, count_addr);
  qa_guest_core_address format = guest_text(f, "%+06d|%.0f|%.3s|%#llx|%.2f%n");
  qa_call_value call_args[] = {ptr_arg(true, buffer), size_arg(f, 256),
                               qa_call_int32(0), size_arg(f, 256), ptr_arg(true, format),
                               ptr_arg(true, descriptor)};
  const char *expected = "-00042|2|gue|0xffffffffffffffff|1.25";
  qa_call_result result = invoke_at(f, service_addr, call_args, 6);
  CHECK(result_is_int(result, (int64_t)strlen(expected)));
  char *text = qa_read_string(m, &f->ext, buffer, 1048576);
  CHECK(strcmp(text, expected) == 0);
  free(text);
  CHECK(qa_gmem_read_i32(m, count_addr) == (int)strlen(expected));
  CHECK(qa_memory_read_u32(m, descriptor) == 48);
  CHECK(qa_memory_read_u32(m, qa_memory_offset(m, descriptor, 4)) == 176);
  qa_guest_core_address cursor = {0};
  CHECK(qa_gmem_read_pointer(m, qa_memory_offset(m, descriptor, 8), &cursor) &&
        cursor.byte_offset == stack.byte_offset + 32);
  qa_system_v_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
static qa_call_result invoke_virtual(fixture *f, qa_guest_core_address object, int slot_index,
                                     const qa_call_value *extra, size_t extra_count) {
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_guest_core_address table = {0};
  if (!qa_gmem_read_pointer(m, object, &table)) qa_range_error("Missing vtable");
  qa_guest_core_address target = {0};
  if (!qa_gmem_read_pointer(m, qa_memory_offset(m, table, (int64_t)slot_index * m->pointer_bytes),
                            &target))
    qa_range_error("Missing virtual method");
  qa_call_value *all = malloc((extra_count + 1) * sizeof *all);
  all[0] = qa_call_pointer(true, object);
  for (size_t i = 0; i < extra_count; i++) all[i + 1] = extra[i];
  qa_call_result result = invoke_at(f, target, all, extra_count + 1);
  free(all);
  return result;
}
static void test_system_v_iostreams(void) {
  sv_fixture s;
  sv_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_system_v_iostreams *streams = s.rt.iostreams;
  qa_guest_core_address object = qa_system_v_allocate(&s.rt.host, 1);
  qa_call_value object_arg[] = {ptr_arg(true, object)};
  (void)sv_call(f, &s.rt, "libstdc++.so.6", "_ZNSt8ios_base4InitC1Ev", "GLIBCXX_3.4",
                object_arg, 1);
  CHECK(qa_gmem_read_i32(m, streams->refcount) == 2);
  const qa_system_v_standard_stream *cin_s = &streams->streams[0];
  const qa_system_v_standard_stream *cout_s = &streams->streams[1];
  const qa_system_v_standard_stream *cerr_s = &streams->streams[2];
  const qa_system_v_standard_stream *clog_s = &streams->streams[3];
  const qa_system_v_standard_stream *wcerr_s = &streams->streams[6];
  qa_guest_core_address tie = {0};
  CHECK(qa_gmem_read_pointer(m, qa_memory_offset(m, cin_s->ios, 216), &tie) &&
        tie.byte_offset == cout_s->address.byte_offset);
  CHECK(qa_gmem_read_pointer(m, qa_memory_offset(m, cerr_s->ios, 216), &tie) &&
        tie.byte_offset == cout_s->address.byte_offset);
  CHECK(qa_memory_read_u32(m, qa_memory_offset(m, cerr_s->ios, 24)) == 0x3002);
  CHECK(qa_memory_read_u32(m, qa_memory_offset(m, cout_s->ios, 24)) == 0x1002);
  qa_guest_core_address cerr_buffer = {0}, clog_buffer = {0};
  CHECK(qa_gmem_read_pointer(m, qa_memory_offset(m, cerr_s->ios, 232), &cerr_buffer) &&
        qa_gmem_read_pointer(m, qa_memory_offset(m, clog_s->ios, 232), &clog_buffer) &&
        cerr_buffer.byte_offset == clog_buffer.byte_offset);
  CHECK(streams->locale != NULL);
  CHECK(qa_gmem_read_i32(m, streams->locale->implementation) == 16);
  qa_call_value a_arg[] = {qa_call_int32(97)};
  CHECK(result_is_int(invoke_virtual(f, streams->locale->ctype[0], 2, a_arg, 1), 65));
  qa_call_value is_args[] = {qa_call_uint32(0x2000), qa_call_uint32(32)};
  CHECK(result_is_int(invoke_virtual(f, streams->locale->ctype[1], 2, is_args, 2), 1));
  is_args[1] = qa_call_uint32(65);
  CHECK(result_is_int(invoke_virtual(f, streams->locale->ctype[1], 2, is_args, 2), 0));
  qa_guest_core_address bytes = qa_system_v_allocate(&s.rt.host, 2);
  const uint8_t ab[2] = {65, 66};
  qa_memory_write(m, bytes, ab, 2);
  qa_guest_core_address cout_buffer = {0};
  qa_gmem_read_pointer(m, qa_memory_offset(m, cout_s->ios, 232), &cout_buffer);
  qa_call_value xsputn_args[] = {ptr_arg(true, bytes), qa_call_int64(2)};
  (void)invoke_virtual(f, cout_buffer, 12, xsputn_args, 2);
  CHECK(s.out.len == 0);
  qa_call_value fputc_args[] = {qa_call_int32(67), ptr_arg(true, streams->stdio.stdout_)};
  (void)sv_call(f, &s.rt, "libc.so.6", "fputc", NULL, fputc_args, 2);
  qa_call_value flush_args[] = {ptr_arg(true, cout_s->address)};
  (void)sv_call(f, &s.rt, "libstdc++.so.6", "_ZNSo5flushEv", "GLIBCXX_3.4", flush_args, 1);
  CHECK(strcmp(s.out.text, "ABC") == 0);
  qa_guest_core_address cin_buffer = {0};
  qa_gmem_read_pointer(m, qa_memory_offset(m, cin_s->ios, 232), &cin_buffer);
  CHECK(result_is_int(invoke_virtual(f, cin_buffer, 9, NULL, 0), 104));
  CHECK(result_is_int(invoke_virtual(f, cin_buffer, 10, NULL, 0), 104));
  CHECK(result_is_int(invoke_virtual(f, cin_buffer, 10, NULL, 0), 105));
  qa_call_value pback_arg[] = {qa_call_int32(-1)};
  CHECK(result_is_int(invoke_virtual(f, cin_buffer, 11, pback_arg, 1), 105));
  CHECK(result_is_int(invoke_virtual(f, cin_buffer, 10, NULL, 0), 105));
  qa_guest_core_address wcerr_buffer = {0};
  qa_gmem_read_pointer(m, qa_memory_offset(m, wcerr_s->ios, 232), &wcerr_buffer);
  qa_call_value z_arg[] = {qa_call_uint32(90)};
  qa_call_result z = invoke_virtual(f, wcerr_buffer, 13, z_arg, 1);
  CHECK(z.kind == QA_CALL_UINT32 && z.number == 90);
  CHECK(strcmp(s.out.text, "ABCZ") == 0);
  (void)sv_call(f, &s.rt, "libstdc++.so.6", "_ZNSt8ios_base4InitC1Ev", "GLIBCXX_3.4",
                object_arg, 1);
  size_t flushes_before = s.out.flush_count;
  (void)sv_call(f, &s.rt, "libstdc++.so.6", "_ZNSt8ios_base4InitD1Ev", "GLIBCXX_3.4",
                object_arg, 1);
  CHECK(s.out.flush_count == flushes_before);
  (void)sv_call(f, &s.rt, "libstdc++.so.6", "_ZNSt8ios_base4InitD1Ev", "GLIBCXX_3.4",
                object_arg, 1);
  CHECK(s.out.flush_count - flushes_before == 10);
  CHECK(qa_gmem_read_i32(m, streams->refcount) == 1);
  qa_system_v_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
typedef struct destructor_probe { qa_sparse_guest_memory *mem; qa_guest_core_address counter; }
    destructor_probe;
static qa_call_result destructor_impl(void *ctx, const qa_guest_core_call_context *context,
                                      const qa_call_value *args, size_t count) {
  destructor_probe *probe = ctx;
  (void)context;
  qa_guest_core_address value = qa_guest_required_pointer(args, count, 0);
  qa_mapped_guest_memory *m = &probe->mem->base;
  qa_gmem_write_i32(m, probe->counter,
                    qa_gmem_read_i32(m, probe->counter) * 10 + qa_gmem_read_i32(m, value));
  return qa_call_void();
}
static void test_system_v_destructors(void) {
  sv_fixture s;
  sv_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_guest_core_address counter = qa_system_v_allocate(&s.rt.host, 4);
  qa_guest_core_address other_dso = qa_system_v_allocate(&s.rt.host, 1);
  destructor_probe probe = {&f->mem, counter};
  qa_abi_storage params[] = {QA_STORAGE_POINTER};
  const char *versions[] = {NULL};
  qa_system_v_implementation impl = {&probe, destructor_impl};
  qa_system_v_service(&s.rt.host, "fixture.so", "destructor", versions, 1, params, 1, true,
                      QA_STORAGE_INT32, impl);
  qa_guest_core_address target = {0};
  CHECK(qa_system_v_resolve_address(&s.rt.host, "fixture.so", "destructor", NULL, &target));
  for (int digit = 1; digit <= 3; digit++) {
    qa_guest_core_address argument = qa_system_v_allocate(&s.rt.host, 4);
    qa_gmem_write_i32(m, argument, digit);
    qa_call_value atexit_args[] = {ptr_arg(true, target), ptr_arg(true, argument),
                                   ptr_arg(true, digit == 2 ? other_dso : counter)};
    (void)sv_call(f, &s.rt, "libc.so.6", "__cxa_atexit", NULL, atexit_args, 3);
  }
  qa_call_value finalize_args[] = {ptr_arg(true, counter)};
  (void)sv_call(f, &s.rt, "libc.so.6", "__cxa_finalize", NULL, finalize_args, 1);
  CHECK(qa_gmem_read_i32(m, counter) == 31);
  s.rt.host.finalize_destructors(s.rt.host.ctx, &f->context, false, A(0));
  CHECK(qa_gmem_read_i32(m, counter) == 312);
  s.rt.host.finalize_destructors(s.rt.host.ctx, &f->context, false, A(0));
  CHECK(qa_gmem_read_i32(m, counter) == 312);
  qa_system_v_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}

/* ---------- Windows runtime ---------- */
typedef struct fake_file { uint8_t bytes[64]; size_t len; bool closed; } fake_file;
static size_t fake_read(void *ctx, size_t offset, uint8_t *out, size_t length) {
  fake_file *file = ctx;
  if (offset >= file->len) return 0;
  size_t n = file->len - offset < length ? file->len - offset : length;
  memcpy(out, file->bytes + offset, n);
  return n;
}
static int64_t fake_write(void *ctx, size_t offset, const uint8_t *bytes, size_t len) {
  fake_file *file = ctx;
  if (offset + len > sizeof file->bytes) return 0;
  memcpy(file->bytes + offset, bytes, len);
  if (offset + len > file->len) file->len = offset + len;
  return (int64_t)len;
}
static int64_t fake_size(void *ctx) { return (int64_t)((fake_file *)ctx)->len; }
static void fake_truncate(void *ctx, size_t length) { ((fake_file *)ctx)->len = length; }
static void fake_flush(void *ctx) { (void)ctx; }
static void fake_close(void *ctx) { ((fake_file *)ctx)->closed = true; }
typedef struct win_caps { fake_file file; qa_windows_file file_iface; } win_caps;
static qa_windows_file *win_open(void *ctx, const char *path, bool read, bool write,
                                 int creation) {
  win_caps *caps = ctx;
  (void)read; (void)write; (void)creation;
  return strcmp(path, "save.sav") == 0 ? &caps->file_iface : NULL;
}
static bool win_now(void *ctx, double *out) { (void)ctx; *out = 1700000000123.0; return true; }
typedef struct win_fixture {
  fixture f;
  qa_windows_guest_runtime rt;
  win_caps caps;
} win_fixture;
static void win_init(win_fixture *s, int width) {
  fixture_init(&s->f, width);
  memset(&s->caps, 0, sizeof s->caps);
  s->caps.file.bytes[0] = 10; s->caps.file.bytes[1] = 20; s->caps.file.bytes[2] = 30;
  s->caps.file.len = 3;
  s->caps.file_iface.ctx = &s->caps.file;
  s->caps.file_iface.read = fake_read;
  s->caps.file_iface.write = fake_write;
  s->caps.file_iface.size = fake_size;
  s->caps.file_iface.truncate = fake_truncate;
  s->caps.file_iface.flush = fake_flush;
  s->caps.file_iface.close = fake_close;
  qa_windows_runtime_options options;
  memset(&options, 0, sizeof options);
  options.memory = &s->f.mem.base;
  options.memory_ext = &s->f.ext;
  options.callbacks = &s->f.callbacks;
  options.module = test_module;
  options.has_capabilities = true;
  options.capabilities.ctx = &s->caps;
  options.capabilities.open_file = win_open;
  options.capabilities.now_milliseconds = win_now;
  qa_windows_guest_runtime_init(&s->rt, &options);
  qa_windows_guest_runtime_attach_runner(&s->rt, s->f.runner);
}
static qa_call_result win_call(win_fixture *s, const char *library, const char *name,
                               const qa_call_value *args, size_t count) {
  qa_guest_core_address address = {0};
  if (!qa_windows_resolve_address(&s->rt.host, library, name, &address))
    qa_range_error("Missing Windows test service %s", name);
  return invoke_at(&s->f, address, args, count);
}
static uint64_t result_ptr_offset(qa_call_result r) {
  if (r.kind != QA_CALL_POINTER || !r.pointer_has) qa_range_error("Expected pointer result");
  return r.pointer.byte_offset;
}
static void test_windows_services(void) {
  win_fixture s;
  win_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_call_value null_arg[] = {ptr_arg(false, A(0))};
  qa_call_result encoded_null = win_call(&s, "kernel32.dll", "EncodePointer", null_arg, 1);
  CHECK(encoded_null.kind == QA_CALL_POINTER && encoded_null.pointer_has &&
        encoded_null.pointer.byte_offset != 0);
  qa_call_value decode_args[] = {encoded_null};
  CHECK(result_is_ptr(win_call(&s, "kernel32.dll", "DecodePointer", decode_args, 1), false, 0));
  qa_call_value heap_create_args[] = {qa_call_uint32(0), size_arg(f, 0), size_arg(f, 0)};
  qa_guest_core_address heap = A(result_ptr_offset(
      win_call(&s, "kernel32.dll", "HeapCreate", heap_create_args, 3)));
  qa_call_value heap_alloc_args[] = {ptr_arg(true, heap), qa_call_uint32(8), size_arg(f, 16)};
  qa_guest_core_address allocation = A(result_ptr_offset(
      win_call(&s, "kernel32.dll", "HeapAlloc", heap_alloc_args, 3)));
  qa_memory_write_u32(m, allocation, 0x12345678);
  qa_call_value realloc_args[] = {ptr_arg(true, heap), qa_call_uint32(8),
                                  ptr_arg(true, allocation), size_arg(f, 40)};
  qa_guest_core_address enlarged = A(result_ptr_offset(
      win_call(&s, "kernel32.dll", "HeapReAlloc", realloc_args, 4)));
  CHECK(qa_memory_read_u32(m, enlarged) == 0x12345678);
  qa_call_result tls_index = win_call(&s, "kernel32.dll", "TlsAlloc", NULL, 0);
  CHECK(tls_index.kind == QA_CALL_UINT32);
  qa_call_value tls_set_args[] = {tls_index, ptr_arg(true, enlarged)};
  CHECK(result_is_int(win_call(&s, "kernel32.dll", "TlsSetValue", tls_set_args, 2), 1));
  qa_guest_core_address slot = qa_memory_offset(m, s.rt.teb, 0x1480 + (int64_t)tls_index.number * 8);
  qa_guest_core_address stored = {0};
  CHECK(qa_gmem_read_pointer(m, slot, &stored) &&
        stored.byte_offset == enlarged.byte_offset);
  qa_call_value tls_get_args[] = {tls_index};
  CHECK(result_is_ptr(win_call(&s, "kernel32.dll", "TlsGetValue", tls_get_args, 1), true,
                      enlarged.byte_offset));
  CHECK(qa_windows_last_error_get(&s.rt.host) == 0);
  CHECK(result_is_int(win_call(&s, "kernel32.dll", "TlsFree", tls_get_args, 1), 1));
  CHECK(result_is_ptr(win_call(&s, "kernel32.dll", "TlsGetValue", tls_get_args, 1), false, 0));
  CHECK(qa_windows_last_error_get(&s.rt.host) == 87);
  qa_guest_core_address path = guest_text(f, "save.sav");
  qa_call_value create_args[] = {ptr_arg(true, path), qa_call_uint32(0xc0000000),
                                 qa_call_uint32(0), ptr_arg(false, A(0)), qa_call_uint32(3),
                                 qa_call_uint32(0), ptr_arg(false, A(0))};
  qa_guest_core_address handle = A(result_ptr_offset(
      win_call(&s, "kernel32.dll", "CreateFileA", create_args, 7)));
  qa_guest_core_address buffer = guest_alloc(f, 8);
  qa_guest_core_address transferred = guest_alloc(f, 4);
  qa_call_value read_args[] = {ptr_arg(true, handle), ptr_arg(true, buffer),
                               qa_call_uint32(2), ptr_arg(true, transferred),
                               ptr_arg(false, A(0))};
  CHECK(result_is_int(win_call(&s, "kernel32.dll", "ReadFile", read_args, 5), 1));
  uint8_t bytes[2];
  qa_memory_copy(m, buffer, bytes, 2);
  CHECK(bytes[0] == 10 && bytes[1] == 20);
  CHECK(qa_memory_read_u32(m, transferred) == 2);
  qa_call_value seek_args[] = {ptr_arg(true, handle), qa_call_int32(1), ptr_arg(false, A(0)),
                               qa_call_uint32(0)};
  qa_call_result seek = win_call(&s, "kernel32.dll", "SetFilePointer", seek_args, 4);
  CHECK(seek.kind == QA_CALL_UINT32 && seek.number == 1);
  const uint8_t write_bytes[2] = {99, 88};
  qa_memory_write(m, buffer, write_bytes, 2);
  (void)win_call(&s, "kernel32.dll", "WriteFile", read_args, 5);
  CHECK(s.caps.file.bytes[0] == 10 && s.caps.file.bytes[1] == 99 && s.caps.file.bytes[2] == 88);
  qa_call_value close_args[] = {ptr_arg(true, handle)};
  (void)win_call(&s, "kernel32.dll", "CloseHandle", close_args, 1);
  CHECK(s.caps.file.closed);
  qa_call_value destroy_args[] = {ptr_arg(true, heap)};
  CHECK(result_is_int(win_call(&s, "kernel32.dll", "HeapDestroy", destroy_args, 1), 1));
  qa_windows_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
static void test_windows_crt(void) {
  win_fixture s;
  win_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_guest_core_address buffer = guest_alloc(f, 32);
  const uint8_t initial[6] = {1, 2, 3, 4, 5, 6};
  qa_memory_write(m, buffer, initial, 6);
  qa_call_value move_args[] = {ptr_arg(true, qa_memory_offset(m, buffer, 1)),
                               ptr_arg(true, buffer), qa_call_uint64(5)};
  (void)win_call(&s, "ucrtbase.dll", "memmove", move_args, 3);
  uint8_t moved[6];
  qa_memory_copy(m, buffer, moved, 6);
  CHECK(memcmp(moved, "\x01\x01\x02\x03\x04\x05", 6) == 0);
  qa_call_value next_args[] = {qa_call_float32(1), qa_call_float32(2)};
  qa_call_result next = win_call(&s, "ucrtbase.dll", "nextafterf", next_args, 2);
  CHECK(next.kind == QA_CALL_FLOAT32 && next.number == 1.0 + 0x1p-23);
  qa_call_value modf_args[] = {qa_call_float64(-2), ptr_arg(true, buffer)};
  qa_call_result fraction = win_call(&s, "ucrtbase.dll", "modf", modf_args, 2);
  CHECK(fraction.kind == QA_CALL_FLOAT64 && fraction.number == 0 && signbit(fraction.number));
  CHECK(qa_gmem_read_f64(m, buffer) == -2.0);
  qa_call_value dsign_args[] = {qa_call_float64(-0.0)};
  CHECK(result_is_int(win_call(&s, "ucrtbase.dll", "_dsign", dsign_args, 1), -32768));
  qa_guest_core_address ticks_addr = {0};
  CHECK(qa_windows_resolve_address(&s.rt.host, "msvcp140.dll", "_Xtime_get_ticks",
                                   &ticks_addr));
  qa_call_result ticks = invoke_at(f, ticks_addr, NULL, 0);
  CHECK(ticks.kind == QA_CALL_INT64 && ticks.int64_value == 17000000001230000LL);
  qa_guest_core_address text_addr = guest_alloc(f, 32);
  qa_guest_core_address end_addr = guest_alloc(f, 8);
  qa_memory_write(m, text_addr, (const uint8_t *)"-0x10!\0", 7);
  qa_call_value strtoul_args[] = {ptr_arg(true, text_addr), ptr_arg(true, end_addr),
                                  qa_call_int32(0)};
  qa_call_result parsed = win_call(&s, "ucrtbase.dll", "strtoul", strtoul_args, 3);
  CHECK(parsed.kind == QA_CALL_UINT32 && (uint32_t)parsed.number == 0xfffffff0u);
  qa_guest_core_address end_value = {0};
  CHECK(qa_gmem_read_pointer(m, end_addr, &end_value) &&
        end_value.byte_offset == text_addr.byte_offset + 5);
  qa_memory_write(m, text_addr, (const uint8_t *)"4294967296x\0", 12);
  strtoul_args[2] = qa_call_int32(10);
  parsed = win_call(&s, "ucrtbase.dll", "strtoul", strtoul_args, 3);
  CHECK(parsed.kind == QA_CALL_UINT32 && (uint32_t)parsed.number == 0xffffffffu);
  qa_call_result errno_result = win_call(&s, "ucrtbase.dll", "_errno", NULL, 0);
  CHECK(qa_gmem_read_i32(m, errno_result.pointer) == 34);
  qa_windows_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
typedef struct compare_probe { qa_sparse_guest_memory *mem; int comparisons; } compare_probe;
static qa_call_result compare_impl(void *ctx, const qa_guest_core_call_context *context,
                                   const qa_call_value *args, size_t count) {
  compare_probe *probe = ctx;
  (void)context;
  qa_mapped_guest_memory *m = &probe->mem->base;
  probe->comparisons++;
  return qa_call_int32(qa_gmem_read_i32(m, qa_guest_required_pointer(args, count, 0)) -
                       qa_gmem_read_i32(m, qa_guest_required_pointer(args, count, 1)));
}
static void test_windows_qsort_and_libraries(void) {
  win_fixture s;
  win_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  compare_probe probe = {&f->mem, 0};
  qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  qa_windows_import_implementation impl = {&probe, compare_impl};
  qa_windows_service(&s.rt.host, "ucrtbase.dll", "test-sort-compare", params, 2, false,
                     QA_STORAGE_INT32, impl);
  qa_guest_core_address comparator = {0};
  CHECK(qa_windows_resolve_address(&s.rt.host, "ucrtbase.dll", "test-sort-compare",
                                   &comparator));
  qa_guest_core_address buffer = guest_alloc(f, 32);
  const int values[5] = {9, -3, 9, 0, 2};
  for (int i = 0; i < 5; i++) qa_gmem_write_i32(m, qa_memory_offset(m, buffer, i * 4), values[i]);
  qa_call_value qsort_args[] = {ptr_arg(true, buffer), qa_call_uint64(5), qa_call_uint64(4),
                                ptr_arg(true, comparator)};
  (void)win_call(&s, "ucrtbase.dll", "qsort", qsort_args, 4);
  const int expected[5] = {-3, 0, 2, 9, 9};
  for (int i = 0; i < 5; i++)
    CHECK(qa_gmem_read_i32(m, qa_memory_offset(m, buffer, i * 4)) == expected[i]);
  CHECK(probe.comparisons > 0);
  /* Dynamic library references and critical sections (services.test.ts). */
  qa_guest_core_address wide_name = guest_alloc(f, 64);
  const char *kernel32 = "kernel32.dll";
  for (size_t i = 0; i <= strlen(kernel32); i++)
    qa_memory_write_u16(m, qa_memory_offset(m, wide_name, (int64_t)i * 2),
                        (uint8_t)kernel32[i]);
  qa_call_value load_args[] = {ptr_arg(true, wide_name), ptr_arg(false, A(0)),
                               qa_call_uint32(0x800)};
  qa_guest_core_address library = A(result_ptr_offset(
      win_call(&s, "kernel32.dll", "LoadLibraryExW", load_args, 3)));
  qa_call_value handle_args[] = {ptr_arg(true, wide_name)};
  CHECK(result_is_ptr(win_call(&s, "kernel32.dll", "GetModuleHandleW", handle_args, 1),
                      true, library.byte_offset));
  qa_guest_core_address proc_name = guest_text(f, "GetLastError");
  qa_call_value proc_args[] = {ptr_arg(true, library), ptr_arg(true, proc_name)};
  qa_guest_core_address get_last_error = {0};
  CHECK(qa_windows_resolve_address(&s.rt.host, "kernel32.dll", "GetLastError",
                                   &get_last_error));
  CHECK(result_is_ptr(win_call(&s, "kernel32.dll", "GetProcAddress", proc_args, 2), true,
                      get_last_error.byte_offset));
  qa_call_value free_lib_args[] = {ptr_arg(true, library)};
  CHECK(result_is_int(win_call(&s, "kernel32.dll", "FreeLibrary", free_lib_args, 1), 1));
  CHECK(result_is_int(win_call(&s, "kernel32.dll", "FreeLibrary", free_lib_args, 1), 0));
  qa_guest_core_address lock = guest_alloc(f, 40);
  qa_call_value spin_args[] = {ptr_arg(true, lock), qa_call_uint32(4000)};
  CHECK(result_is_int(win_call(&s, "kernel32.dll",
                               "InitializeCriticalSectionAndSpinCount", spin_args, 2), 1));
  CHECK(qa_memory_read_u64(m, qa_memory_offset(m, lock, 32)) == 4000);
  qa_call_value lock_args[] = {ptr_arg(true, lock)};
  (void)win_call(&s, "kernel32.dll", "EnterCriticalSection", lock_args, 1);
  (void)win_call(&s, "kernel32.dll", "EnterCriticalSection", lock_args, 1);
  CHECK(qa_memory_read_u32(m, qa_memory_offset(m, lock, 12)) == 2);
  (void)win_call(&s, "kernel32.dll", "LeaveCriticalSection", lock_args, 1);
  (void)win_call(&s, "kernel32.dll", "LeaveCriticalSection", lock_args, 1);
  CHECK(qa_memory_read_u32(m, qa_memory_offset(m, lock, 8)) == 0xffffffffu);
  qa_windows_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
static void test_windows_ucrt_format(void) {
  win_fixture s;
  win_init(&s, 8);
  fixture *f = &s.f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_guest_core_address service_addr = {0};
  CHECK(qa_windows_resolve_address(&s.rt.host, "api-ms-win-crt-stdio-l1-1-0.dll",
                                   "__stdio_common_vsprintf", &service_addr));
  qa_guest_core_address buffer = guest_alloc(f, 64);
  qa_guest_core_address args_addr = guest_alloc(f, 32);
  qa_gmem_write_i32(m, args_addr, 17);
  qa_gmem_write_f64(m, qa_memory_offset(m, args_addr, 8), 16);
  qa_call_value call_args[] = {qa_call_uint64(0x26), ptr_arg(true, buffer),
                               qa_call_uint64(36), ptr_arg(true, guest_text(f, "%.*g")),
                               ptr_arg(false, A(0)), ptr_arg(true, args_addr)};
  CHECK(result_is_int(invoke_at(f, service_addr, call_args, 6), 2));
  char *text = qa_read_string(m, &f->ext, buffer, 1048576);
  CHECK(strcmp(text, "16") == 0);
  free(text);
  qa_guest_core_address literal = guest_text(f, "abcde");
  uint8_t fill[64];
  memset(fill, 0x7f, sizeof fill);
  qa_memory_write(m, buffer, fill, sizeof fill);
  qa_call_value c99_args[] = {qa_call_uint64(2), ptr_arg(true, buffer), qa_call_uint64(4),
                              ptr_arg(true, literal), ptr_arg(false, A(0)),
                              ptr_arg(false, A(0))};
  CHECK(result_is_int(invoke_at(f, service_addr, c99_args, 6), 5));
  uint8_t copied[6];
  qa_memory_copy(m, buffer, copied, 6);
  const uint8_t expected_copied[6] = {97, 98, 99, 0, 127, 127};
  CHECK(memcmp(copied, expected_copied, 6) == 0);
  qa_windows_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}
static void test_windows_unsupported_import(void) {
  win_fixture s;
  win_init(&s, 8);
  fixture *f = &s.f;
  qa_guest_import import_;
  memset(&import_, 0, sizeof import_);
  import_.library = "unavailable.dll";
  import_.symbol.name = "Missing";
  import_.slot = A(0x10000);
  qa_guest_image image;
  memset(&image, 0, sizeof image);
  qa_guest_import_resolution resolution =
      qa_windows_guest_runtime_resolve(&s.rt, &import_, &image);
  CHECK(resolution.kind == QA_RESOLUTION_HOST);
  bool found = false;
  for (size_t i = 0; i < qa_windows_guest_runtime_coverage_count(&s.rt); i++) {
    qa_windows_import_coverage c = qa_windows_guest_runtime_coverage(&s.rt, i);
    if (strcmp(c.name, "Missing") == 0) found = !c.supported && c.reached == 0;
  }
  CHECK(found);
  CHECK_ABORTS((void)qa_guest_callback_table_invoke(&f->callbacks, resolution.address,
                                                    &f->context, NULL, 0));
  qa_windows_guest_runtime_destroy(&s.rt);
  qa_sparse_guest_memory_destroy(&f->mem);
}

int main(void) {
  test_layouts();
  test_format_float();
  test_format_buffer();
  test_scan();
  test_system_v_heap_guard_qsort();
  test_system_v_format_service();
  test_system_v_iostreams();
  test_system_v_destructors();
  test_windows_services();
  test_windows_crt();
  test_windows_qsort_and_libraries();
  test_windows_ucrt_format();
  test_windows_unsupported_import();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
