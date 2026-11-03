/*
 * Depth checks for src/compat/q3/native/{imports,module,syscall}.ts.
 *
 * The TS suite reaches these classes only through tests/compat/q3/native/
 * fixture.ts, which loads the retail qagamex64.so (absent from ~/qfiles). The
 * same paths run here against authored guest code: a Quake Live dllEntry/
 * export table on x86-64 and a Q3 1.32 dllEntry/vmMain pair on i386, executed
 * by the shared CPU interpreters through the shared call runner. Every host
 * callback asserts its sentinel ctx. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/q3/native/imports.h"
#include "qa/compat/q3/native/module.h"
#include "qa/compat/q3/native/syscall.h"
#include "qa/foundation/support.h"
#include "qa/guest/abi/runner.h"
#include "qa/guest/core/callbacks.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/x64/cpu.h"
#include "qa/guest/x86/cpu.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_STR(actual, expected)                                       \
  do {                                                                    \
    checks++;                                                             \
    const char *a_ = (actual), *e_ = (expected);                          \
    if (a_ == NULL || strcmp(a_, e_) != 0) { failures++; fprintf(stderr, "FAIL %s:%d: got \"%s\" expected \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", e_); } \
  } while (0)
static void expect_throw_at(int line, void (*fn)(void *), void *ctx, const char *fragment) {
  qa_caught_error caught; checks++;
  if (qa_try(fn, ctx, &caught)) { failures++; fprintf(stderr, "FAIL line %d: no throw, expected \"%s\"\n", line, fragment); return; }
  if (strstr(caught.message, fragment) == NULL) { failures++; fprintf(stderr, "FAIL line %d: threw \"%s\", expected \"%s\"\n", line, caught.message, fragment); }
}
#define EXPECT_THROW(fn, ctx, fragment) expect_throw_at(__LINE__, fn, ctx, fragment)

static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
#define CHECK_CTX(ctx) CHECK((ctx) == SENTINEL)

static qa_guest_core_address A(uint64_t v) { qa_guest_core_address a = {v}; return a; }
static qa_guest_module_identity test_module = {"test:q3-native", "authored-native", "digest", "1"};
static qa_guest_module_ref sparse_module = {"test:q3-native", "authored-native", "digest", "1"};

/* --- guest process fixture (as tests/guest/test_guest_runtime.c) --- */
typedef struct { qa_sparse_guest_memory *mem; qa_guest_mapping buf[256]; } test_ext;
static qa_guest_core_address ext_map(void *ctx, uint64_t base, size_t len, qa_guest_permissions perm, const char *label, const uint8_t *bytes, size_t blen) {
  test_ext *e = ctx; qa_guest_map_options o; memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = len; o.permissions = perm; o.label = label; o.bytes = bytes; o.bytes_length = blen;
  return qa_sparse_map(e->mem, &o);
}
static qa_guest_core_address ext_alloc(void *ctx, size_t len, bool ha, uint64_t al, bool hp, qa_guest_permissions perm, const char *label) {
  test_ext *e = ctx; qa_guest_allocation_options o; memset(&o, 0, sizeof o);
  o.byte_length = len; o.alignment = ha ? al : 0; o.permissions = perm; o.has_permissions = hp; o.label = label;
  return qa_sparse_allocate(e->mem, &o);
}
static void ext_unmap(void *ctx, qa_guest_core_address a, size_t len) { qa_sparse_unmap(((test_ext *)ctx)->mem, a, len); }
static void ext_protect(void *ctx, qa_guest_core_address a, size_t len, qa_guest_permissions p) { qa_sparse_protect(((test_ext *)ctx)->mem, a, len, p); }
static const qa_guest_mapping *ext_mappings(void *ctx, size_t *count) {
  test_ext *e = ctx; *count = qa_sparse_mappings(e->mem, e->buf, 256); if (*count > 256) *count = 256; return e->buf;
}
static void ext_move(void *ctx, qa_guest_core_address d, qa_guest_core_address s, size_t n) { qa_sparse_move(((test_ext *)ctx)->mem, d, s, n); }
static void ext_fill(void *ctx, qa_guest_core_address a, size_t n, int v) { qa_sparse_fill(((test_ext *)ctx)->mem, a, n, (uint32_t)v); }
static int64_t ext_find_zero(void *ctx, qa_guest_core_address a, size_t m) { return qa_sparse_find_zero(((test_ext *)ctx)->mem, a, m); }

typedef struct fixture {
  qa_sparse_guest_memory mem; test_ext ext_store; qa_mapped_memory_ext ext; qa_guest_callback_table callbacks;
  qa_guest_processor_state state; qa_x64_cpu x64; qa_i386_cpu x86; qa_guest_cpu cpu;
  _Alignas(16) uint8_t runner_storage[16384]; qa_guest_call_runner *runner; qa_guest_core_call_context context; int width;
} fixture;
static bool is_host_call(void *ctx, qa_guest_core_address address) { return qa_guest_callback_table_resolve((qa_guest_callback_table *)ctx, address) != NULL; }
static qa_guest_execution_stop run_x64(void *ctx, int64_t budget, bool has_return, qa_guest_core_address ret) { return qa_x64_cpu_run((qa_x64_cpu *)ctx, budget, has_return, ret); }
static qa_guest_execution_stop run_x86(void *ctx, int64_t budget, bool has_return, qa_guest_core_address ret) { return qa_i386_cpu_run((qa_i386_cpu *)ctx, budget, has_return, ret); }
static void smap(fixture *f, uint64_t base, size_t len, qa_guest_permissions perm, const uint8_t *bytes, size_t blen) {
  qa_guest_map_options o; memset(&o, 0, sizeof o); o.base = base; o.byte_length = len; o.permissions = perm; o.bytes = bytes; o.bytes_length = blen;
  (void)qa_sparse_map(&f->mem, &o);
}
static void fixture_init(fixture *f, int width, void *variadic_ctx,
    bool (*variadic)(void *, const qa_guest_host_callback *, const qa_call_value *, size_t, const qa_guest_core_call_context *, qa_abi_value_layout **, size_t *)) {
  memset(f, 0, sizeof *f); f->width = width;
  qa_sparse_guest_memory_options opts; memset(&opts, 0, sizeof opts);
  opts.module = sparse_module; opts.pointer_bytes = width; opts.allocation_base = 0x50000000; opts.has_allocation_base = true;
  qa_sparse_guest_memory_init(&f->mem, &opts);
  smap(f, 0x10000, 65536, QA_PERM_READ_WRITE, NULL, 0);
  static const uint8_t sentinel_byte[1] = {0xcc};
  smap(f, 0x30000, 4096, QA_PERM_READ_EXECUTE, sentinel_byte, 1);
  memset(&f->ext_store, 0, sizeof f->ext_store); f->ext_store.mem = &f->mem;
  memset(&f->ext, 0, sizeof f->ext); f->ext.ctx = &f->ext_store;
  f->ext.map = ext_map; f->ext.allocate = ext_alloc; f->ext.unmap = ext_unmap; f->ext.protect = ext_protect;
  f->ext.mappings = ext_mappings; f->ext.move = ext_move; f->ext.fill = ext_fill; f->ext.find_zero = ext_find_zero;
  qa_guest_callback_table_init(&f->callbacks, &f->mem);
  qa_guest_processor_initial_state init = {width == 4 ? QA_GUEST_I386 : QA_GUEST_X86_64, 0x30000, 0x20000, 2, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&f->state, &init);
  if (width == 8) qa_x64_cpu_init(&f->x64, &f->state, &f->mem, NULL, NULL, &f->callbacks);
  else qa_i386_cpu_init(&f->x86, &f->state, &f->mem, is_host_call, &f->callbacks, NULL, NULL);
  f->cpu.state = &f->state; f->cpu.memory = &f->mem.base; f->cpu.memory_ext = &f->ext; f->cpu.module = test_module;
  f->cpu.run_ctx = width == 8 ? (void *)&f->x64 : (void *)&f->x86; f->cpu.run = width == 8 ? run_x64 : run_x86;
  f->runner = (qa_guest_call_runner *)f->runner_storage;
  qa_guest_call_runner_options ro; memset(&ro, 0, sizeof ro);
  ro.cpu = &f->cpu; ro.callbacks = &f->callbacks; ro.return_address = A(0x30000);
  ro.variadic_ctx = variadic_ctx; ro.variadic_layouts = variadic;
  (void)qa_guest_call_runner_init(f->runner, ro);
  f->context.module = test_module; f->context.callback_kind = QA_CALLBACK_REF_TYPESCRIPT;
}
static void fixture_destroy(fixture *f) {
  qa_guest_call_runner_destroy(f->runner);
  if (f->width == 8) qa_x64_cpu_destroy(&f->x64);
  qa_guest_callback_table_destroy(&f->callbacks);
  qa_sparse_guest_memory_destroy(&f->mem);
}

/* --- engine services seam --- */
typedef struct {
  char printed[256], appended[256], set_name[64], set_value[64], bound_name[64], bound_default[64], dropped[64], sent[64], userinfo_set[64];
  double bound_flags, dropped_client, sent_client, userinfo_client, cs_index; bool set_force;
  double modification_count, numeric, integer; const char *value; bool has_cvar;
  const char *arguments[4]; size_t argument_count; int reads, binds;
} Host;
static Host host;
static void h_append(void *ctx, const char *text) { CHECK_CTX(ctx); snprintf(host.appended, sizeof host.appended, "%s", text); }
static void h_print(void *ctx, const char *text) { CHECK_CTX(ctx); snprintf(host.printed, sizeof host.printed, "%s", text); }
static void h_drop(void *ctx, double client, const char *reason) { CHECK_CTX(ctx); host.dropped_client = client; snprintf(host.dropped, sizeof host.dropped, "%s", reason); }
static void h_send(void *ctx, double client, const char *text) { CHECK_CTX(ctx); host.sent_client = client; snprintf(host.sent, sizeof host.sent, "%s", text); }
static const char *h_get_userinfo(void *ctx, double client) { CHECK_CTX(ctx); host.userinfo_client = client; return "\\name\\player"; }
static void h_set_userinfo(void *ctx, double client, const char *value) { CHECK_CTX(ctx); host.userinfo_client = client; snprintf(host.userinfo_set, sizeof host.userinfo_set, "%s", value); }
static double h_cvar_value(void *ctx, const char *name) { CHECK_CTX(ctx); return strcmp(name, "g_gravity") == 0 ? 0.1 : 0; }
static char *h_cvar_string(void *ctx, const char *name) { CHECK_CTX(ctx); (void)name; return strdup("abcdefgh"); }
static void h_cvar_set(void *ctx, const char *name, const char *value, bool force) { CHECK_CTX(ctx); snprintf(host.set_name, 64, "%s", name); snprintf(host.set_value, 64, "%s", value); host.set_force = force; }
static int64_t h_cvar_bind(void *ctx, const char *name, const char *fallback, double flags) {
  CHECK_CTX(ctx); host.binds++; snprintf(host.bound_name, 64, "%s", name); snprintf(host.bound_default, 64, "%s", fallback); host.bound_flags = flags; return 3;
}
static bool h_cvar_read(void *ctx, int64_t handle, double *modification_count, char **value, double *numeric, double *integer) {
  CHECK_CTX(ctx); host.reads++;
  if (!host.has_cvar || handle != 3) return false;
  *modification_count = host.modification_count; *numeric = host.numeric; *integer = host.integer; *value = strdup(host.value); return true;
}
static void h_cs_set(void *ctx, double index, const char *value) { CHECK_CTX(ctx); host.cs_index = index; snprintf(host.set_value, 64, "%s", value); }
static const char *h_cs_get(void *ctx, double index) { CHECK_CTX(ctx); host.cs_index = index; return "configstring"; }
static const char *const *h_arguments(void *ctx, size_t *count) { CHECK_CTX(ctx); *count = host.argument_count; return host.arguments; }
static qa_compat_q3_native_engine_services services(void) {
  qa_compat_q3_native_engine_services s; memset(&s, 0, sizeof s);
  s.ctx = SENTINEL; s.append_console_command = h_append; s.print = h_print; s.drop_client = h_drop; s.send_server_command = h_send;
  s.get_userinfo = h_get_userinfo; s.set_userinfo = h_set_userinfo; s.cvar_variable_value = h_cvar_value; s.cvar_variable_string = h_cvar_string;
  s.cvar_set = h_cvar_set; s.cvar_bind_vm = h_cvar_bind; s.cvar_read_vm = h_cvar_read; s.configstring_set = h_cs_set; s.configstring_get = h_cs_get;
  s.command_arguments = h_arguments;
  return s;
}

/* --- authored x86-64 Quake Live module --- */
enum { CODE = 0x400000, DATA = 0x500000, TABLE = DATA, SAVED = DATA + 0x100, CELLS = DATA + 0x108, TEXT = DATA + 0x200, VMCVAR = DATA + 0x300, NAME = DATA + 0x500, DEFAULT = DATA + 0x510, CLIENT = DATA + 0x600 };
typedef struct { uint8_t bytes[1024]; size_t length; } Code;
static void emit(Code *c, const uint8_t *b, size_t n) { memcpy(c->bytes + c->length, b, n); c->length += n; }
static void emit_u8(Code *c, uint8_t b) { emit(c, &b, 1); }
static void emit_imm64(Code *c, uint64_t v) { emit(c, (const uint8_t *)&v, 8); }
static void emit_imm32(Code *c, uint32_t v) { emit(c, (const uint8_t *)&v, 4); }
static void pad_to(Code *c, size_t at) { while (c->length < at) emit_u8(c, 0x90); }
static void mov_rax(Code *c, uint64_t v) { emit_u8(c, 0x48); emit_u8(c, 0xb8); emit_imm64(c, v); }
static void load_imports(Code *c) { mov_rax(c, SAVED); static const uint8_t b[] = {0x48, 0x8b, 0x00}; emit(c, b, 3); }
static void call_slot(Code *c, uint32_t slot) { emit_u8(c, 0xff); emit_u8(c, 0x90); emit_imm32(c, slot * 8); }
enum { F_ENTRY = 0x00, F_SHUTDOWN = 0x40, F_FRAME = 0x60, F_CVARS = 0x80, F_INIT = 0x100, F_COMMAND = 0x120, F_CONNECT = 0x160, F_ENTRY_V9 = 0x180, F_ENTRY_NULL = 0x1c0 };
static void build_ql(Code *c) {
  memset(c, 0, sizeof *c);
  /* dllEntry(output, imports, version) */
  static const uint8_t version[] = {0xc7, 0x02, 0x0a, 0x00, 0x00, 0x00}; emit(c, version, 6);
  mov_rax(c, TABLE); static const uint8_t store_rdi[] = {0x48, 0x89, 0x07}; emit(c, store_rdi, 3);
  mov_rax(c, SAVED); static const uint8_t store_rsi[] = {0x48, 0x89, 0x30}; emit(c, store_rsi, 3);
  emit_u8(c, 0xc3);
  static const uint8_t store_edi[] = {0x89, 0x38};
  pad_to(c, F_SHUTDOWN); mov_rax(c, CELLS); emit(c, store_edi, 2); emit_u8(c, 0xc3);
  pad_to(c, F_FRAME); mov_rax(c, CELLS + 4); emit(c, store_edi, 2); emit_u8(c, 0xc3);
  /* registerCvars: Printf(text); Cvar_Register(vmCvar, name, default, 4) */
  pad_to(c, F_CVARS);
  static const uint8_t sub8[] = {0x48, 0x83, 0xec, 0x08}, add8[] = {0x48, 0x83, 0xc4, 0x08};
  emit(c, sub8, 4); load_imports(c); emit_u8(c, 0x48); emit_u8(c, 0xbf); emit_imm64(c, TEXT); call_slot(c, 2);
  load_imports(c); emit_u8(c, 0x48); emit_u8(c, 0xbf); emit_imm64(c, VMCVAR); emit_u8(c, 0x48); emit_u8(c, 0xbe); emit_imm64(c, NAME);
  emit_u8(c, 0x48); emit_u8(c, 0xba); emit_imm64(c, DEFAULT); static const uint8_t ecx4[] = {0xb9, 0x04, 0x00, 0x00, 0x00}; emit(c, ecx4, 5);
  call_slot(c, 17); emit(c, add8, 4); emit_u8(c, 0xc3);
  /* init(levelTime, seed, restart) */
  pad_to(c, F_INIT); mov_rax(c, CELLS + 8);
  static const uint8_t three[] = {0x89, 0x38, 0x89, 0x70, 0x04, 0x89, 0x50, 0x08}; emit(c, three, 8); emit_u8(c, 0xc3);
  /* consoleCommand: return Argc() */
  pad_to(c, F_COMMAND); emit(c, sub8, 4); load_imports(c); call_slot(c, 20); emit(c, add8, 4); emit_u8(c, 0xc3);
  /* clientConnect(client, ...): client ? CLIENT : NULL */
  pad_to(c, F_CONNECT); static const uint8_t test_jz[] = {0x85, 0xff, 0x74, 0x0b}; emit(c, test_jz, 4);
  mov_rax(c, CLIENT); emit_u8(c, 0xc3); static const uint8_t xor_ret[] = {0x31, 0xc0, 0xc3}; emit(c, xor_ret, 3);
  /* dllEntry writing API 9 / a null table */
  pad_to(c, F_ENTRY_V9); static const uint8_t v9[] = {0xc7, 0x02, 0x09, 0x00, 0x00, 0x00, 0xc3}; emit(c, v9, 7);
  pad_to(c, F_ENTRY_NULL); static const uint8_t vnull[] = {0xc7, 0x02, 0x0a, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x07, 0x00, 0x00, 0x00, 0x00, 0xc3}; emit(c, vnull, sizeof vnull);
}
static void map_ql(fixture *f) {
  Code code; build_ql(&code);
  smap(f, CODE, 4096, QA_PERM_READ_EXECUTE, code.bytes, code.length);
  uint8_t data[4096]; memset(data, 0, sizeof data);
  uint64_t table[11] = {0}; table[0] = CODE + F_SHUTDOWN; table[1] = CODE + F_FRAME; table[2] = CODE + F_CVARS; table[3] = CODE + F_INIT; table[4] = CODE + F_COMMAND; table[8] = CODE + F_CONNECT;
  memcpy(data, table, sizeof table);
  memcpy(data + (TEXT - DATA), "hello from guest", 17); memcpy(data + (NAME - DATA), "g_test", 7); memcpy(data + (DEFAULT - DATA), "5", 2);
  smap(f, DATA, 4096, QA_PERM_READ_WRITE, data, sizeof data);
}
static qa_guest_export exports_storage[4];
static qa_guest_image image_with(const qa_guest_native_abi abi, const char *const *names, const uint64_t *targets, size_t count) {
  qa_guest_image image; memset(&image, 0, sizeof image);
  image.module = test_module; image.abi = abi; image.base = A(CODE);
  memset(exports_storage, 0, sizeof exports_storage);
  for (size_t i = 0; i < count; i++) { exports_storage[i].symbol.name = names[i]; exports_storage[i].address = A(targets[i]); }
  image.exports = exports_storage; image.export_count = count;
  return image;
}
static const qa_guest_native_abi kX64 = {"linux-x86-64", "elf64", 8, "system-v-x86-64"};
static const qa_guest_native_abi kI386 = {"linux-i386", "elf32", 4, "system-v-i386"};

typedef struct { qa_compat_q3_quakelive_game_module m; qa_compat_q3_native_module_options o; qa_guest_core_address imports; } QlRun;
static void ql_init(void *c) { QlRun *r = c; qa_compat_q3_quakelive_game_module_init(&r->m, r->o, r->imports); }
static void ql_init_bad(void *c) { QlRun *r = c; qa_compat_q3_quakelive_game_module_initialize(&r->m, 1.5, 0, false); }
static void ql_missing_slot(void *c) { QlRun *r = c; qa_call_result res = qa_compat_q3_quakelive_game_module_console_command(&r->m) ? qa_call_void() : qa_call_void(); (void)res; }

static qa_call_result invoke_slot(fixture *f, qa_compat_q3_quakelive_game_imports *imports, int slot, const qa_call_value *args, size_t n) {
  uint64_t target = 0; qa_sparse_copy(&f->mem, A(imports->address.byte_offset + (uint64_t)slot * 8), (uint8_t *)&target, 8);
  const qa_guest_host_callback *callback = qa_guest_callback_table_resolve(&f->callbacks, A(target));
  CHECK(callback != NULL);
  return callback->invoke(callback->ctx, &f->context, args, n);
}
typedef struct { fixture *f; qa_compat_q3_quakelive_game_imports *imports; int slot; qa_call_value args[4]; size_t n; } SlotRun;
static void slot_body(void *c) { SlotRun *s = c; qa_call_result r = invoke_slot(s->f, s->imports, s->slot, s->args, s->n); qa_call_value_free(&r); }
typedef struct { qa_compat_q3_quakelive_game_imports *imports; int64_t slot, capacity; qa_guest_core_address address; } BindRun;
static void bind_body(void *c) { BindRun *b = c; qa_compat_q3_quakelive_game_imports_bind(b->imports, b->slot, "x", NULL, 0, false, QA_STORAGE_INT32, NULL, NULL, true); }
static void write_body(void *c) { BindRun *b = c; qa_compat_q3_quakelive_game_imports_write_string(b->imports, b->address, "text", b->capacity); }

static void test_quake_live(void) {
  fixture f; fixture_init(&f, 8, NULL, NULL); map_ql(&f);
  memset(&host, 0, sizeof host);
  qa_compat_q3_quakelive_game_imports imports;
  CHECK(qa_compat_q3_quakelive_game_imports_init(&imports, &f.mem, &f.callbacks, kX64, services()));
  /* Coverage: every slot, in order; unbound slots report their names. */
  qa_compat_q3_native_service_coverage coverage[206];
  CHECK(qa_compat_q3_quakelive_game_imports_coverage(&imports, coverage, 206) == 206);
  CHECK(coverage[0].slot == 0); CHECK_STR(coverage[0].name, "SendConsoleCommand"); CHECK(coverage[0].implemented);
  CHECK_STR(coverage[1].name, "unbound-1"); CHECK(!coverage[1].implemented);
  CHECK_STR(coverage[205].name, "unbound-205");

  static const char *const names[1] = {"dllEntry"}; const uint64_t targets[1] = {CODE + F_ENTRY};
  QlRun run; memset(&run, 0, sizeof run);
  run.o.image = image_with(kX64, names, targets, 1); run.o.runner = f.runner; run.o.context = f.context; run.o.instruction_budget = 100000; run.imports = imports.address;
  qa_compat_q3_quakelive_game_module_init(&run.m, run.o, imports.address);
  CHECK(run.m.api_version == 10); CHECK(run.m.exports.byte_offset == TABLE);
  { uint64_t saved = 0; qa_sparse_copy(&f.mem, A(SAVED), (uint8_t *)&saved, 8); CHECK(saved == imports.address.byte_offset); }
  qa_compat_q3_quakelive_game_module *game = &run.m;
  qa_compat_q3_quakelive_game_module_run_frame(game, 1234); CHECK(qa_sparse_read_i32(&f.mem, A(CELLS + 4)) == 1234);
  qa_compat_q3_quakelive_game_module_shutdown(game, true); CHECK(qa_sparse_read_i32(&f.mem, A(CELLS)) == 1);
  qa_compat_q3_quakelive_game_module_initialize(game, 1000, 7, false);
  CHECK(qa_sparse_read_i32(&f.mem, A(CELLS + 8)) == 1000 && qa_sparse_read_i32(&f.mem, A(CELLS + 12)) == 7 && qa_sparse_read_i32(&f.mem, A(CELLS + 16)) == 0);
  /* Wrapped like BigInt.asUintN(32): 2^32 + 5 arrives as 5. */
  qa_compat_q3_quakelive_game_module_run_frame(game, 4294967301.0); CHECK(qa_sparse_read_i32(&f.mem, A(CELLS + 4)) == 5);
  EXPECT_THROW(ql_init_bad, &run, "Integer ABI argument is not an exact integer");

  /* registerCvars: Printf then Cvar_Register, which updates the vmCvar_t from the registry. */
  host.has_cvar = true; host.modification_count = 2; host.numeric = 5.5; host.integer = 5; host.value = "5.5";
  qa_compat_q3_quakelive_game_module_register_cvars(game);
  CHECK_STR(host.printed, "hello from guest");
  CHECK_STR(host.bound_name, "g_test"); CHECK_STR(host.bound_default, "5"); CHECK(host.bound_flags == 4);
  CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR)) == 3); CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 4)) == 2);
  CHECK(qa_sparse_read_f32(&f.mem, A(VMCVAR + 8)) == 5.5); CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 12)) == 5);
  { char text[8] = {0}; qa_sparse_copy(&f.mem, A(VMCVAR + 16), (uint8_t *)text, 4); CHECK_STR(text, "5.5"); }
  CHECK(qa_compat_q3_quakelive_game_imports_coverage(&imports, coverage, 206) == 206);
  CHECK(coverage[2].reached == 1 && coverage[17].reached == 1 && coverage[0].reached == 0);

  /* consoleCommand returns Argc() != 0. */
  host.argument_count = 0; CHECK(!qa_compat_q3_quakelive_game_module_console_command(game));
  host.arguments[0] = "say"; host.arguments[1] = "hi"; host.argument_count = 2; CHECK(qa_compat_q3_quakelive_game_module_console_command(game));
  /* clientConnect: pointer or null. */
  qa_guest_core_address denied = A(0);
  CHECK(qa_compat_q3_quakelive_game_module_client_connect(game, 1, true, false, &denied)); CHECK(denied.byte_offset == CLIENT);
  CHECK(!qa_compat_q3_quakelive_game_module_client_connect(game, 0, false, true, &denied));
  /* A null export slot fails by name. */
  { uint64_t zero = 0; qa_sparse_write(&f.mem, A(TABLE + 4 * 8), (const uint8_t *)&zero, 8);
    EXPECT_THROW(ql_missing_slot, &run, "Missing Quake Live export slot 4"); }

  /* Services invoked directly, with TS argument order and ABI types. */
  SlotRun s; memset(&s, 0, sizeof s); s.f = &f; s.imports = &imports;
  uint8_t data[64];
  qa_call_value text_arg = qa_call_pointer(true, A(TEXT));
  qa_call_result r;
  r = invoke_slot(&f, &imports, 0, &text_arg, 1); CHECK_STR(host.appended, "hello from guest"); qa_call_value_free(&r);
  { qa_call_value name = qa_call_pointer(true, A(NAME)); r = invoke_slot(&f, &imports, 11, &name, 1); CHECK(r.kind == QA_CALL_FLOAT32); CHECK(r.number == 0); qa_call_value_free(&r); }
  /* Cvar_VariableStringBuffer: capacity keeps the terminator; the rest of the buffer is zeroed. */
  memset(data, 0x55, sizeof data); qa_sparse_write(&f.mem, A(DATA + 0x700), data, sizeof data);
  { qa_call_value args[3] = {qa_call_pointer(true, A(NAME)), qa_call_pointer(true, A(DATA + 0x700)), qa_call_int32(4)};
    r = invoke_slot(&f, &imports, 13, args, 3); qa_call_value_free(&r);
    qa_sparse_copy(&f.mem, A(DATA + 0x700), data, 6); CHECK(memcmp(data, "abc\0\x55\x55", 6) == 0); }
  { qa_call_value args[2] = {qa_call_pointer(true, A(NAME)), qa_call_pointer(true, A(DEFAULT))};
    r = invoke_slot(&f, &imports, 15, args, 2); qa_call_value_free(&r); CHECK_STR(host.set_name, "g_test"); CHECK_STR(host.set_value, "5"); CHECK(host.set_force); }
  /* Argv: out of range is "". */
  { qa_call_value args[3] = {qa_call_int32(1), qa_call_pointer(true, A(DATA + 0x700)), qa_call_int32(16)};
    r = invoke_slot(&f, &imports, 18, args, 3); qa_call_value_free(&r); qa_sparse_copy(&f.mem, A(DATA + 0x700), data, 3); CHECK(memcmp(data, "hi\0", 3) == 0);
    args[0] = qa_call_int32(9); r = invoke_slot(&f, &imports, 18, args, 3); qa_call_value_free(&r); qa_sparse_copy(&f.mem, A(DATA + 0x700), data, 1); CHECK(data[0] == 0);
    args[0] = qa_call_int32(-1); r = invoke_slot(&f, &imports, 18, args, 3); qa_call_value_free(&r); qa_sparse_copy(&f.mem, A(DATA + 0x700), data, 1); CHECK(data[0] == 0); }
  r = invoke_slot(&f, &imports, 20, NULL, 0); CHECK(r.kind == QA_CALL_INT32 && r.number == 2); qa_call_value_free(&r);
  { qa_call_value args[2] = {qa_call_int32(3), text_arg};
    r = invoke_slot(&f, &imports, 23, args, 2); qa_call_value_free(&r); CHECK(host.dropped_client == 3); CHECK_STR(host.dropped, "hello from guest");
    r = invoke_slot(&f, &imports, 24, args, 2); qa_call_value_free(&r); CHECK(host.sent_client == 3); CHECK_STR(host.sent, "hello from guest");
    r = invoke_slot(&f, &imports, 25, args, 2); qa_call_value_free(&r); CHECK(host.cs_index == 3);
    r = invoke_slot(&f, &imports, 29, args, 2); qa_call_value_free(&r); CHECK(host.userinfo_client == 3); CHECK_STR(host.userinfo_set, "hello from guest"); }
  { qa_call_value args[3] = {qa_call_int32(7), qa_call_pointer(true, A(DATA + 0x700)), qa_call_int32(64)};
    r = invoke_slot(&f, &imports, 26, args, 3); qa_call_value_free(&r); qa_sparse_copy(&f.mem, A(DATA + 0x700), data, 13); CHECK(memcmp(data, "configstring\0", 13) == 0); CHECK(host.cs_index == 7);
    r = invoke_slot(&f, &imports, 28, args, 3); qa_call_value_free(&r); qa_sparse_copy(&f.mem, A(DATA + 0x700), data, 13); CHECK(memcmp(data, "\\name\\player\0", 13) == 0); }
  /* Cvar_Update: unchanged modification count writes nothing; a changed one rewrites the record. */
  { qa_call_value arg = qa_call_pointer(true, A(VMCVAR)); host.value = "6"; host.numeric = 6; host.integer = 6;
    r = invoke_slot(&f, &imports, 12, &arg, 1); qa_call_value_free(&r); CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 12)) == 5);
    host.modification_count = 3; r = invoke_slot(&f, &imports, 12, &arg, 1); qa_call_value_free(&r);
    CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 4)) == 3); CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 12)) == 6);
    /* The integer word wraps like DataView.setInt32. */
    host.modification_count = 4; host.integer = 4294967295.0; r = invoke_slot(&f, &imports, 12, &arg, 1); qa_call_value_free(&r);
    CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 12)) == -1);
    /* An absent registry entry leaves the record. */
    host.has_cvar = false; host.modification_count = 9; r = invoke_slot(&f, &imports, 12, &arg, 1); qa_call_value_free(&r); CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 4)) == 4);
    /* Over-long values fail by name after the count is written. */
    char big[300]; memset(big, 'v', 299); big[299] = 0; host.has_cvar = true; host.value = big; host.modification_count = 10;
    s.slot = 12; s.args[0] = arg; s.n = 1; EXPECT_THROW(slot_body, &s, "Cvar_Update: source value exceeds MAX_CVAR_VALUE_STRING");
    CHECK(qa_sparse_read_i32(&f.mem, A(VMCVAR + 4)) == 10); host.has_cvar = false; }
  /* Cvar_Register with a null vmCvar binds without writing. */
  { qa_call_value args[4] = {qa_call_pointer(false, A(0)), qa_call_pointer(true, A(NAME)), qa_call_pointer(true, A(DEFAULT)), qa_call_int32(0)};
    int binds = host.binds; r = invoke_slot(&f, &imports, 17, args, 4); qa_call_value_free(&r); CHECK(host.binds == binds + 1); }
  /* Failures by name. */
  s.slot = 50; s.n = 0; EXPECT_THROW(slot_body, &s, "Quake Live game import 50 (unbound-50) is unavailable in the supplied engine host");
  s.slot = 0; s.args[0] = qa_call_pointer(false, A(0)); s.n = 1; EXPECT_THROW(slot_body, &s, "Nonnull guest pointer required");
  s.n = 0; EXPECT_THROW(slot_body, &s, "Missing guest argument 0");
  s.slot = 18; s.args[0] = qa_call_int32(0); s.args[1] = qa_call_pointer(true, A(DATA + 0x700)); s.args[2] = qa_call_int32(0); s.n = 3;
  EXPECT_THROW(slot_body, &s, "Native string output requires positive source capacity");
  BindRun b = {&imports, 206, 0, A(0)}; EXPECT_THROW(bind_body, &b, "Quake Live import slot outside table");
  b.slot = -1; EXPECT_THROW(bind_body, &b, "Quake Live import slot outside table");
  b.address = A(0x900000); b.capacity = 8; EXPECT_THROW(write_body, &b, "");
  /* Direct invocations count as reached too. */
  qa_compat_q3_quakelive_game_imports_coverage(&imports, coverage, 206); CHECK(coverage[0].reached == 3); /* TS counts before invoking, throws included */

  /* Module admission. */
  QlRun bad = run;
  { static const char *const n2[2] = {"dllEntry", "vmMain"}; const uint64_t t2[2] = {CODE, CODE};
    bad.o.image = image_with(kX64, n2, t2, 2); EXPECT_THROW(ql_init, &bad, "Module does not use the Quake Live table interface"); }
  { static const char *const n2[2] = {"GetGameAPI", "dllEntry"}; const uint64_t t2[2] = {CODE, CODE};
    bad.o.image = image_with(kX64, n2, t2, 2); EXPECT_THROW(ql_init, &bad, "Module does not use the Quake Live table interface"); }
  { static const char *const n1[1] = {"other"}; const uint64_t t1[1] = {CODE};
    bad.o.image = image_with(kX64, n1, t1, 1); EXPECT_THROW(ql_init, &bad, "Native module has no dllEntry export"); }
  { const uint64_t t1[1] = {CODE + F_ENTRY_V9}; bad.o.image = image_with(kX64, names, t1, 1);
    EXPECT_THROW(ql_init, &bad, "Quake Live game API 9 is unsupported; expected 10"); }
  { const uint64_t t1[1] = {CODE + F_ENTRY_NULL}; bad.o.image = image_with(kX64, names, t1, 1);
    EXPECT_THROW(ql_init, &bad, "Quake Live dllEntry returned a null export table"); }
  /* A forwarded export is TS null. */
  { qa_guest_image image = image_with(kX64, names, targets, 1); exports_storage[0].is_forward = true; qa_guest_core_address out;
    CHECK(!qa_compat_q3_native_export(&image, "dllEntry", &out)); }

  qa_compat_q3_quakelive_game_imports_destroy(&imports);
  fixture_destroy(&f);
}

/* --- authored i386 Q3 1.32 module with a variadic syscall --- */
enum { SYSCALL_CELL = DATA + 0x100 };
/* TS NativeQ3Syscalls: one handler receives the sixteen little-endian words id's VM_DllSyscall copies. */
static int32_t word_at(const uint8_t *words, size_t index) {
  return (int32_t)((uint32_t)words[index * 4] | (uint32_t)words[index * 4 + 1] << 8 | (uint32_t)words[index * 4 + 2] << 16 | (uint32_t)words[index * 4 + 3] << 24);
}
static double syscall_handler(void *ctx, const uint8_t *words, size_t length) {
  CHECK_CTX(ctx);
  CHECK(length == 64);
  const int32_t code = word_at(words, 0);
  if (code == 7) return (double)word_at(words, 1) * 10 + word_at(words, 2);
  if (code == 8) return 4294967296.0 + 9;
  if (code == 9) return 0.5;
  qa_range_error("Unbound Q3 native syscall %d", (int)code);
}
static void build_q3(Code *c) {
  memset(c, 0, sizeof *c);
  /* dllEntry(syscall): mov eax,[esp+4]; mov [SYSCALL_CELL],eax; ret */
  static const uint8_t entry[] = {0x8b, 0x44, 0x24, 0x04, 0xa3}; emit(c, entry, 5); emit_imm32(c, SYSCALL_CELL); emit_u8(c, 0xc3);
  /* vmMain @0x40: command==1 ? syscall(arg2 code, arg0, arg1) : command + arg0 + arg11 */
  pad_to(c, 0x40);
  static const uint8_t head[] = {0x8b, 0x44, 0x24, 0x04, 0x83, 0xf8, 0x01, 0x74, 0x09, 0x03, 0x44, 0x24, 0x08, 0x03, 0x44, 0x24, 0x34, 0xc3};
  emit(c, head, sizeof head);
  static const uint8_t call_sys[] = {0xff, 0x74, 0x24, 0x0c, 0xff, 0x74, 0x24, 0x0c, 0xff, 0x74, 0x24, 0x18, 0xff, 0x15};
  emit(c, call_sys, sizeof call_sys); emit_imm32(c, SYSCALL_CELL);
  static const uint8_t tail[] = {0x83, 0xc4, 0x0c, 0xc3}; emit(c, tail, 4);
}
typedef struct { qa_compat_q3_vm_module m; qa_compat_q3_native_module_options o; qa_guest_core_address syscall; double command; double args[13]; size_t n; } VmRun;
static void vm_init(void *c) { VmRun *r = c; qa_compat_q3_vm_module_init(&r->m, r->o, r->syscall); }
static void vm_call(void *c) { VmRun *r = c; (void)qa_compat_q3_vm_module_call(&r->m, r->command, r->args, r->n); }
typedef struct { qa_guest_callback_table *t; qa_guest_native_abi abi; } SysInit;
static void sys_init(void *c) { SysInit *s = c; qa_compat_q3_native_syscalls x; qa_compat_q3_native_syscalls_init(&x, s->t, s->abi, syscall_handler, SENTINEL); qa_compat_q3_native_syscalls_destroy(&x); }
static void test_quake3_vm(void) {
  qa_compat_q3_native_syscalls syscalls;
  fixture f; fixture_init(&f, 4, &syscalls, qa_compat_q3_native_syscalls_variadic_resolver);
  Code code; build_q3(&code);
  smap(&f, CODE, 4096, QA_PERM_READ_EXECUTE, code.bytes, code.length);
  smap(&f, DATA, 4096, QA_PERM_READ_WRITE, NULL, 0);
  CHECK(qa_compat_q3_native_syscalls_init(&syscalls, &f.callbacks, kI386, syscall_handler, SENTINEL));
  CHECK_STR(syscalls.callback_id, "q3-native-syscall:test:q3-native");
  CHECK(syscalls.callback.signature.variadic);
  static const char *const names[2] = {"dllEntry", "vmMain"}; const uint64_t targets[2] = {CODE, CODE + 0x40};
  VmRun run; memset(&run, 0, sizeof run);
  run.o.image = image_with(kI386, names, targets, 2); run.o.runner = f.runner; run.o.context = f.context; run.o.instruction_budget = 100000; run.syscall = syscalls.address;
  qa_compat_q3_vm_module_init(&run.m, run.o, syscalls.address);
  CHECK(run.m.vm_main.byte_offset == CODE + 0x40);
  { uint32_t stored = 0; qa_sparse_copy(&f.mem, A(SYSCALL_CELL), (uint8_t *)&stored, 4); CHECK(stored == syscalls.address.byte_offset); }
  /* Twelve padded arguments: command + arg0 + arg11. */
  double args[12] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100};
  CHECK(qa_compat_q3_vm_module_call(&run.m, 5, args, 12) == 107);
  CHECK(qa_compat_q3_vm_module_call(&run.m, 5, args, 1) == 7);
  CHECK(qa_compat_q3_vm_module_call(&run.m, -5, NULL, 0) == -5);
  /* The guest's variadic syscall reaches the handler as sixteen words. */
  double sys_args[3] = {4, 2, 7};
  CHECK(qa_compat_q3_vm_module_call(&run.m, 1, sys_args, 3) == 42);
  sys_args[2] = 8; CHECK(qa_compat_q3_vm_module_call(&run.m, 1, sys_args, 3) == 9); /* 2^32 + 9 wraps */
  /* Result encoding: the handler's number completes as `value | 0`. */
  sys_args[2] = 9; CHECK(qa_compat_q3_vm_module_call(&run.m, 1, sys_args, 3) == 0);
  /* A raise inside the guest's call, last: the shared runner is not reused after a host-callback throw. */
  sys_args[2] = 11; run.command = 1; memcpy(run.args, sys_args, sizeof sys_args); run.n = 3;
  EXPECT_THROW(vm_call, &run, "Unbound Q3 native syscall 11");
  run.n = 13; EXPECT_THROW(vm_call, &run, "Q3 vmMain accepts at most twelve arguments");
  run.n = 1; run.command = 2.5; EXPECT_THROW(vm_call, &run, "Integer ABI argument is not an exact integer");
  /* The resolver ignores foreign callbacks (TS null ?? []). */
  { qa_guest_host_callback foreign = syscalls.callback; foreign.id = "elsewhere"; qa_abi_value_layout *out = (qa_abi_value_layout *)1; size_t count = 7;
    qa_call_value fixed = qa_call_int32(7);
    CHECK(qa_compat_q3_native_syscalls_variadic_resolver(&syscalls, &foreign, &fixed, 1, &f.context, &out, &count)); CHECK(out == NULL && count == 0);
    CHECK(qa_compat_q3_native_syscalls_variadic_resolver(&syscalls, &syscalls.callback, &fixed, 1, &f.context, &out, &count));
    CHECK(count == 15 && out[0].storage == QA_STORAGE_INT32 && out[14].storage == QA_STORAGE_INT32); free(out); }
  /* Admission. */
  VmRun bad = run;
  { static const char *const n1[1] = {"dllEntry"}; const uint64_t t1[1] = {CODE};
    bad.o.image = image_with(kI386, n1, t1, 1); EXPECT_THROW(vm_init, &bad, "Q3 1.32 native module requires dllEntry and vmMain exports"); }
  bad.o.image = image_with(kX64, names, targets, 2); EXPECT_THROW(vm_init, &bad, "The Q3 1.32 native profile requires i386; later intptr_t profiles need their own declared ABI");
  SysInit si = {&f.callbacks, kX64}; EXPECT_THROW(sys_init, &si, "Q3 1.32 syscalls require the i386 source ABI");
  qa_compat_q3_native_syscalls_destroy(&syscalls);
  fixture_destroy(&f);
}

static void test_signature(void) {
  static const qa_abi_storage params[2] = {QA_STORAGE_POINTER, QA_STORAGE_INT32};
  qa_abi_value_layout layouts[2], result;
  qa_guest_call_signature s = qa_compat_q3_native_signature(kX64, params, 2, true, QA_STORAGE_FLOAT32, true, layouts, &result);
  CHECK(s.parameter_count == 2 && s.parameters == layouts && !s.parameters[0].is_aggregate && s.parameters[0].storage == QA_STORAGE_POINTER && s.parameters[1].storage == QA_STORAGE_INT32);
  CHECK(!s.result_is_void && s.result.storage == QA_STORAGE_FLOAT32 && s.variadic && s.abi.pointer_bytes == 8);
  s = qa_compat_q3_native_signature(kI386, NULL, 0, false, QA_STORAGE_INT32, false, layouts, &result);
  CHECK(s.result_is_void && !s.variadic && s.parameter_count == 0);
  /* Integer staging: wrap after the exactness check. */
  CHECK(qa_compat_q3_native_int32(-1).number == -1);
  CHECK(qa_compat_q3_native_int32(2147483648.0).number == -2147483648.0);
}

int main(void) {
  test_signature();
  test_quake_live();
  test_quake3_vm();
  printf("test_q3_native_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
