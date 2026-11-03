/*
 * Depth checks for the QC core: machine, program, memory, builtins, executor,
 * save, profile, source-call, compatibility, weapon-behavior-profile,
 * message-effects, message-routing, quakeworld-presentation.
 *
 * Ports the parts of tests/compat/qc/runtime.test.ts,
 * rerelease-builtins.test.ts and mod-qw-messages.test.ts that cover these files.
 * Expected values with no TS test come from running the TS modules under bun:
 * the builtin tables below and the interpreter fuzz hashes, whose generators
 * apply the same inputs to the TS QcMachine. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qc.h"
#include "qa/compat/qc/compatibility.h"
#include "qa/compat/qc/message_effects.h"
#include "qa/compat/qc/message_routing.h"
#include "qa/compat/qc/quakeworld_presentation.h"
#include "qa/compat/qc/source_call.h"
#include "qa/compat/qc/weapon_behavior_profile.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)
#define CHECK_STR(actual, expected)                                       \
  do {                                                                    \
    checks++;                                                             \
    const char *a_ = (actual), *e_ = (expected);                          \
    if (a_ == NULL || strcmp(a_, e_) != 0) {                              \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: got \"%s\" expected \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", e_); \
    }                                                                     \
  } while (0)

/* expect(() => fn(ctx)).toThrow(fragment) */
static void expect_throw_at(int line, void (*fn)(void *), void *ctx, const char *fragment) {
  qa_caught_error caught;
  checks++;
  if (qa_try(fn, ctx, &caught)) { failures++; fprintf(stderr, "FAIL line %d: no throw, expected \"%s\"\n", line, fragment); return; }
  if (strstr(caught.message, fragment) == NULL) { failures++; fprintf(stderr, "FAIL line %d: threw \"%s\", expected \"%s\"\n", line, caught.message, fragment); }
}
#define EXPECT_THROW(fn, ctx, fragment) expect_throw_at(__LINE__, fn, ctx, fragment)

/* The sentinel every callback must receive. */
static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
#define CHECK_CTX(ctx) CHECK((ctx) == SENTINEL)

/* --- synthetic progs.dat, in the loader's file format --- */
typedef struct { uint8_t *data; size_t length, capacity; } Buf;
static void put(Buf *b, const void *p, size_t n) {
  if (b->length + n > b->capacity) { b->capacity = (b->length + n) * 2; b->data = realloc(b->data, b->capacity); }
  memcpy(b->data + b->length, p, n); b->length += n;
}
static void put_i32(Buf *b, int32_t v) { put(b, &v, 4); }
static void put_u16(Buf *b, uint16_t v) { put(b, &v, 2); }
static void patch_i32(Buf *b, size_t at, int32_t v) { memcpy(b->data + at, &v, 4); }
static int32_t intern(Buf *strings, const char *s) {
  int32_t at = (int32_t)strings->length; put(strings, s, strlen(s) + 1); return at;
}
typedef struct { const char *name; uint16_t type, offset; bool save; } Def;
typedef struct { int32_t first, parm_start, locals; const char *name; int32_t nparms; uint8_t parms[8]; } Fn;
typedef struct { uint16_t op, a, b, c; } St;
typedef struct { uint16_t word; uint32_t bits; } Init;
enum { STR_ABC = 1, STR_ABD = 5 };

static uint8_t *build(int32_t crc, const St *st, size_t st_n, const Def *g, size_t g_n, const Def *f, size_t f_n,
                      const Fn *fns, size_t fn_n, const Init *init, size_t init_n, int field_words, int global_words, size_t *out_length) {
  Buf strings = {0}, out = {0};
  put(&strings, "", 1); put(&strings, "abc", 4); put(&strings, "abd", 4);
  put_i32(&out, 6); put_i32(&out, crc);
  size_t slots[6];
  for (int i = 0; i < 6; i++) { slots[i] = out.length; put_i32(&out, 0); put_i32(&out, 0); }
  put_i32(&out, field_words);
  patch_i32(&out, slots[0], (int32_t)out.length); patch_i32(&out, slots[0] + 4, (int32_t)st_n);
  for (size_t i = 0; i < st_n; i++) { put_u16(&out, st[i].op); put_u16(&out, st[i].a); put_u16(&out, st[i].b); put_u16(&out, st[i].c); }
  patch_i32(&out, slots[1], (int32_t)out.length); patch_i32(&out, slots[1] + 4, (int32_t)g_n);
  for (size_t i = 0; i < g_n; i++) { put_u16(&out, (uint16_t)(g[i].type | (g[i].save ? 0x8000 : 0))); put_u16(&out, g[i].offset); put_i32(&out, intern(&strings, g[i].name)); }
  patch_i32(&out, slots[2], (int32_t)out.length); patch_i32(&out, slots[2] + 4, (int32_t)f_n);
  for (size_t i = 0; i < f_n; i++) { put_u16(&out, f[i].type); put_u16(&out, f[i].offset); put_i32(&out, intern(&strings, f[i].name)); }
  patch_i32(&out, slots[3], (int32_t)out.length); patch_i32(&out, slots[3] + 4, (int32_t)fn_n);
  int32_t file = intern(&strings, "test.qc");
  for (size_t i = 0; i < fn_n; i++) {
    put_i32(&out, fns[i].first); put_i32(&out, fns[i].parm_start); put_i32(&out, fns[i].locals); put_i32(&out, 0);
    put_i32(&out, intern(&strings, fns[i].name)); put_i32(&out, file); put_i32(&out, fns[i].nparms); put(&out, fns[i].parms, 8);
  }
  patch_i32(&out, slots[4], (int32_t)out.length); patch_i32(&out, slots[4] + 4, (int32_t)strings.length);
  put(&out, strings.data, strings.length);
  patch_i32(&out, slots[5], (int32_t)out.length); patch_i32(&out, slots[5] + 4, global_words);
  uint32_t *words = calloc((size_t)global_words, 4);
  for (size_t i = 0; i < init_n; i++) words[init[i].word] = init[i].bits;
  put(&out, words, (size_t)global_words * 4);
  free(words); free(strings.data);
  *out_length = out.length;
  return out.data;
}

/* Words of the core program. */
enum { G_RESULT = 31, G_RSTR = 32, G_RENT = 33, G_VEC = 34, G_FNREF = 37, G_FLD = 38, G_PTR = 39, G_BLOB = 40,
       K_15 = 50, K_225 = 51, K_37 = 52, K_12 = 53, K_ZERO = 54, K_HOSTFN = 55, K_CALLS = 56, K_FIELD4 = 57, K_DEEP = 58,
       K_ABC = 66, K_ABD = 67, W_PTR = 64, W_OUT = 65 };
enum { F_ADD = 1, F_HOST, F_CALLS, F_OUTER, F_REGIONED, F_STATE, F_SPIN, F_DEEP, F_BIG, F_BITS, F_WORLD, F_POINTER,
       F_STRINGS, F_ESCAPE, F_RETURNS, F_FALLS, F_NAMED, F_MISSING, F_COUNT };
enum { REGION_ENTRY = 9, REGION_EXIT = 11, ESCAPE_ENTRY = 40, ESCAPE_EXIT = 42, RETURNS_ENTRY = 45, RETURNS_EXIT = 47 };
static uint32_t float_bits(float value) { uint32_t b; memcpy(&b, &value, 4); return b; }
static uint8_t *core_bytes(int32_t crc, size_t *length) {
  static const St st[] = {
    /* 0*/ {QA_QC_OP_DONE, 0, 0, 0},
    /* 1*/ {QA_QC_OP_ADD_F, 80, 81, G_RESULT}, {QA_QC_OP_RETURN, G_RESULT, 0, 0},
    /* 3*/ {QA_QC_OP_STORE_F, K_15, 82, 0}, {QA_QC_OP_CALL0, K_HOSTFN, 0, 0}, {QA_QC_OP_RETURN, 82, 0, 0},
    /* 6*/ {QA_QC_OP_STORE_F, K_225, 83, 0}, {QA_QC_OP_CALL0, K_CALLS, 0, 0}, {QA_QC_OP_RETURN, 83, 0, 0},
    /* 9*/ {QA_QC_OP_STORE_F, K_15, 85, 0}, {QA_QC_OP_ADD_F, 85, K_225, 85}, {QA_QC_OP_STORE_F, 85, G_RESULT, 0}, {QA_QC_OP_RETURN, 85, 0, 0},
    /*13*/ {QA_QC_OP_STATE, K_15, K_HOSTFN, 0}, {QA_QC_OP_DONE, 0, 0, 0},
    /*15*/ {QA_QC_OP_GOTO, 0, 0, 0},
    /*16*/ {QA_QC_OP_CALL0, K_DEEP, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0},
    /*18*/ {QA_QC_OP_DONE, 0, 0, 0},
    /*19*/ {QA_QC_OP_BITAND, K_37, K_12, 60}, {QA_QC_OP_BITOR, K_37, K_12, 61}, {QA_QC_OP_AND, K_15, K_ZERO, 62}, {QA_QC_OP_OR, K_15, K_ZERO, 63}, {QA_QC_OP_DONE, 0, 0, 0},
    /*24*/ {QA_QC_OP_ADDRESS, K_ZERO, K_FIELD4, W_PTR}, {QA_QC_OP_DONE, 0, 0, 0},
    /*26*/ {QA_QC_OP_ADDRESS, 28, K_FIELD4, W_PTR}, {QA_QC_OP_STOREP_F, K_15, W_PTR, 0}, {QA_QC_OP_LOAD_F, 28, K_FIELD4, W_OUT}, {QA_QC_OP_DONE, 0, 0, 0},
    /*30*/ {QA_QC_OP_EQ_S, K_ABC, K_ABD, 70}, {QA_QC_OP_NE_S, K_ABC, K_ABD, 71}, {QA_QC_OP_EQ_S, K_ABC, K_ABC, 72}, {QA_QC_OP_NOT_S, K_ABC, 0, 73}, {QA_QC_OP_NOT_S, K_ZERO, 0, 74}, {QA_QC_OP_DONE, 0, 0, 0},
    /*36*/ {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0},
    /*40*/ {QA_QC_OP_GOTO, 4, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0},
    /*45*/ {QA_QC_OP_RETURN, K_15, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0}, {QA_QC_OP_DONE, 0, 0, 0},
    /*48: last statement, with no return */ {QA_QC_OP_STORE_F, K_15, 75, 0},
  };
  static const Def g[] = {
    {"self", 4, 28, false}, {"other", 4, 29, false}, {"time", 2, 30, false}, {"result", 2, G_RESULT, true},
    {"rstr", 1, G_RSTR, true}, {"rent", 4, G_RENT, true}, {"vec", 3, G_VEC, true}, {"fnref", 6, G_FNREF, false},
    {"fldref", 5, G_FLD, false}, {"ptr", 7, G_PTR, false}, {"blob", 9, G_BLOB, false}, {"trace_plane_dist", 2, 41, false},
  };
  static const Def f[] = {
    {"classname", 1, 0, false}, {"origin", 3, 1, false}, {"health", 2, 4, false}, {"think", 6, 5, false},
    {"nextthink", 2, 6, false}, {"frame", 2, 7, false}, {"owner", 4, 8, false}, {"angles", 3, 9, false},
    {"light_lev", 2, 12, false}, {"a_b", 2, 13, false}, {"message", 1, 14, false}, {"fld", 5, 15, false},
    {"ptr", 7, 16, false}, {"opaque", 9, 17, false}, {"nothing", 0, 18, false},
    {"mins", 3, 19, false}, {"maxs", 3, 22, false}, {"velocity", 3, 25, false}, {"v_angle", 3, 28, false},
    {"solid", 2, 31, false}, {"netname", 1, 32, false},
  };
  static const Fn fns[F_COUNT] = {
    [0] = {0, 0, 0, "", 0, {0}},
    [F_ADD] = {1, 80, 2, "add", 2, {1, 1}},
    [F_HOST] = {-200, 0, 0, "host", 0, {0}},
    [F_CALLS] = {3, 82, 1, "calls_host", 0, {0}},
    [F_OUTER] = {6, 83, 1, "outer", 0, {0}},
    [F_REGIONED] = {9, 84, 2, "regioned", 1, {1}},
    [F_STATE] = {13, 0, 0, "state_fn", 0, {0}},
    [F_SPIN] = {15, 0, 0, "spin", 0, {0}},
    [F_DEEP] = {16, 0, 0, "deep", 0, {0}},
    [F_BIG] = {18, 90, 30, "big", 0, {0}},
    [F_BITS] = {19, 0, 0, "bits", 0, {0}},
    [F_WORLD] = {24, 0, 0, "world", 0, {0}},
    [F_POINTER] = {26, 0, 0, "pointer", 0, {0}},
    [F_STRINGS] = {30, 0, 0, "strings", 0, {0}},
    [F_ESCAPE] = {ESCAPE_ENTRY, 86, 1, "escape", 0, {0}},
    [F_RETURNS] = {RETURNS_ENTRY, 87, 1, "returns", 0, {0}},
    [F_FALLS] = {48, 0, 0, "falls", 0, {0}},
    [F_NAMED] = {0, 0, 0, "ex_named", 0, {0}},
    [F_MISSING] = {-300, 0, 0, "absent", 0, {0}},
  };
  const Init init[] = {
    {K_15, float_bits(1.5f)}, {K_225, float_bits(2.25f)}, {K_37, float_bits(3.7f)}, {K_12, float_bits(1.2f)},
    {K_HOSTFN, F_HOST}, {K_CALLS, F_CALLS}, {K_FIELD4, 4}, {K_DEEP, F_DEEP}, {K_ABC, STR_ABC}, {K_ABD, STR_ABD},
  };
  return build(crc, st, sizeof st / sizeof *st, g, sizeof g / sizeof *g, f, sizeof f / sizeof *f, fns, F_COUNT, init, sizeof init / sizeof *init, 40, 128, length);
}
static void load_core(qa_compat_qc_program *p) {
  size_t length; uint8_t *bytes = core_bytes(5927, &length);
  CHECK(qa_compat_qc_load_program(bytes, length, NULL, "core.dat", p));
  free(bytes);
}

typedef struct {
  qa_compat_qc_program program; qa_compat_qc_entity_memory entities; qa_compat_qc_builtin_registry registry; qa_compat_qc_machine vm;
} Rig;
static bool server_active(void *ctx) { CHECK_CTX(ctx); return true; }
static void rig_options(Rig *r, qa_compat_qc_machine_options *o, int64_t capacity, int64_t count) {
  qa_compat_qc_entity_layout layout = qa_compat_qc_classic_entity_layout(&r->program);
  CHECK(qa_compat_qc_entity_memory_init(&r->entities, layout, capacity, count));
  memset(o, 0, sizeof *o);
  o->program = &r->program; o->entities = &r->entities; o->numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  o->builtins = &r->registry; o->ctx = SENTINEL; o->server_active = server_active;
}
static void rig_destroy(Rig *r) {
  qa_compat_qc_machine_destroy(&r->vm); qa_compat_qc_registry_destroy(&r->registry);
  qa_compat_qc_entity_memory_destroy(&r->entities); qa_compat_qc_program_destroy(&r->program);
}
static void netquake_builtins(Rig *r) {
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services);
  services.kind = qa_compat_qc_host_kind_for_program(&r->program);
  memset(&r->registry, 0, sizeof r->registry);
  CHECK(qa_compat_qc_create_builtins(services, &r->registry));
}
static uint32_t global_bits(const qa_compat_qc_machine *m, int64_t word) { uint32_t b; memcpy(&b, m->globals.bytes + word * 4, 4); return b; }

typedef struct { qa_compat_qc_machine *m; int64_t fn, argc; } Run;
static void run_body(void *ctx) { Run *r = ctx; qa_compat_qc_machine_execute(r->m, r->fn, r->argc); }

/* --- program loader --- */
typedef struct { const uint8_t *bytes; size_t length; const char *source; const qa_quakec_api_identity *expected; qa_compat_qc_program p; } LoadRun;
static void load_body(void *ctx) { LoadRun *l = ctx; qa_compat_qc_load_program(l->bytes, l->length, l->expected, l->source, &l->p); qa_compat_qc_program_destroy(&l->p); }
static void mutated_throw(const uint8_t *base, size_t length, size_t offset, int32_t value, const char *fragment) {
  uint8_t *bytes = malloc(length); memcpy(bytes, base, length); memcpy(bytes + offset, &value, 4);
  LoadRun l = {bytes, length, "bad.dat", NULL, {0}};
  EXPECT_THROW(load_body, &l, fragment);
  free(bytes);
}
typedef struct { qa_compat_qc_program *p; int64_t i; const char *n; } test_program__A;
static void test_program__at_body(void *c) { test_program__A *a = c; qa_compat_qc_program_function_at(a->p, a->i); }
static void test_program__named_body(void *c) { test_program__A *a = c; qa_compat_qc_program_function_named(a->p, a->n); }
static void test_program(void) {
  size_t length; uint8_t *bytes = core_bytes(5927, &length);
  qa_compat_qc_program p; CHECK(qa_compat_qc_load_program(bytes, length, NULL, "core.dat", &p));
  CHECK(p.api.kind == QA_QUAKEC_API_Q1_NETQUAKE);
  CHECK_STR(p.source, "core.dat");
  /* Statement operands arrive in file order. */
  CHECK(p.statements[1].opcode == QA_QC_OP_ADD_F && p.statements[1].a == 80 && p.statements[1].b == 81 && p.statements[1].c == G_RESULT);
  CHECK(p.functions[F_NAMED].named_builtin);
  CHECK(!p.functions[0].named_builtin);
  CHECK(!p.functions[F_HOST].named_builtin);
  const qa_compat_qc_definition *blob = qa_compat_qc_program_global_named(&p, "blob");
  CHECK_STR(blob->type, "opaque"); CHECK(blob->native_type == 9);
  CHECK(qa_compat_qc_program_global_named(&p, "result")->save);
  CHECK(!qa_compat_qc_program_global_named(&p, "time")->save);
  CHECK(qa_compat_qc_signed_branch(0xffff) == -1); CHECK(qa_compat_qc_signed_branch(0x7fff) == 32767); CHECK(qa_compat_qc_signed_branch(0x8000) == -32768);
  qa_compat_qc_program_destroy(&p);

  mutated_throw(bytes, length, 0, 7, "bad.dat: unsupported program version 7");
  mutated_throw(bytes, length, 4, 99, "bad.dat: unknown system layout CRC 99");
  mutated_throw(bytes, length, 8 + 4, -1, "bad.dat: negative section count");
  mutated_throw(bytes, length, 56, 0, "bad.dat: invalid entity field count 0");
  mutated_throw(bytes, length, 56, 65537, "bad.dat: invalid entity field count 65537");
  { int32_t words; memcpy(&words, bytes + 8 + 5 * 8 + 4, 4); CHECK(words == 128);
    mutated_throw(bytes, length, 8 + 5 * 8 + 4, 27, "missing reserved global words"); }
  { int32_t fn_offset; memcpy(&fn_offset, bytes + 8 + 3 * 8, 4);
    /* Function 1 locals past globals; negative locals; parameter count; entry past the statement table. */
    mutated_throw(bytes, length, (size_t)fn_offset + 36 + 8, 1000, "invalid function locals/parameters for add");
    mutated_throw(bytes, length, (size_t)fn_offset + 36 + 8, -1, "invalid function locals/parameters for add");
    mutated_throw(bytes, length, (size_t)fn_offset + 36 + 24, 9, "invalid function locals/parameters for add");
    mutated_throw(bytes, length, (size_t)fn_offset + 36, 1000, "invalid entry point for add");
    mutated_throw(bytes, length, (size_t)fn_offset + 36 + 28, 2, "invalid parameter size for add"); }
  { int32_t st_offset; memcpy(&st_offset, bytes + 8, 4);
    uint8_t *bad = malloc(length); memcpy(bad, bytes, length); uint16_t op = 66; memcpy(bad + st_offset, &op, 2);
    LoadRun l = {bad, length, "bad.dat", NULL, {0}}; EXPECT_THROW(load_body, &l, "bad.dat: unsupported opcode 66"); free(bad); }
  { qa_quakec_api_identity qw = {QA_QUAKEC_API_Q1_QUAKEWORLD, 6, 54730};
    LoadRun l = {bytes, length, "bad.dat", &qw, {0}}; EXPECT_THROW(load_body, &l, "bad.dat: expected q1-quakeworld, found q1-netquake"); }
  free(bytes);

  /* TS program.functionAt / functionNamed. */
  load_core(&p);
  test_program__A at = {&p, 0, "nosuch"};
  EXPECT_THROW(test_program__at_body, &at, "core.dat: invalid function 0");
  at.i = F_COUNT; EXPECT_THROW(test_program__at_body, &at, "invalid function 19");
  EXPECT_THROW(test_program__named_body, &at, "core.dat: missing function nosuch");
  at.n = ""; EXPECT_THROW(test_program__named_body, &at, "missing function ");
  qa_compat_qc_program_destroy(&p);
}

/* --- memory --- */
typedef struct { qa_compat_qc_strings *s; int64_t r; const char *t; int64_t c; } test_memory__S;
static void test_memory__get_body(void *c) { test_memory__S *a = c; qa_compat_qc_strings_get(a->s, a->r); }
static void test_memory__engine_body(void *c) { test_memory__S *a = c; qa_compat_qc_strings_set_engine(a->s, "fresh", a->t, a->c); }
typedef struct { qa_compat_qc_strings *s; uint8_t *b; size_t n; } test_memory__R;
static void test_memory__restore_body(void *c) { test_memory__R *r = c; qa_compat_qc_strings_restore(r->s, r->b, r->n); }
typedef struct { qa_compat_qc_entity_memory *m; int64_t v; } test_memory__E;
static void test_memory__resolve_body(void *c) { test_memory__E *a = c; qa_compat_qc_entity_memory_resolve_pointer(a->m, a->v, 1); }
static void test_memory__slot_body(void *c) { test_memory__E *a = c; qa_compat_qc_entity_memory_slot(a->m, a->v); }
static void test_memory__at_body(void *c) { test_memory__E *a = c; qa_compat_qc_entity_memory_at(a->m, a->v); }
static void test_memory__count_body(void *c) { test_memory__E *a = c; qa_compat_qc_entity_memory_set_count(a->m, a->v); }
static void test_memory(void) {
  /* TS: QW negative engine-string pointers alias one mutable buffer across raw save. */
  uint8_t zero = 0; qa_compat_qc_strings s;
  CHECK(qa_compat_qc_strings_init(&s, &zero, 1, true));
  int64_t first = qa_compat_qc_strings_set_engine(&s, "pr_string_temp", "1", -1);
  CHECK(first == -1);
  CHECK(qa_compat_qc_strings_set_engine(&s, "pr_string_temp", "2", -1) == first);
  CHECK_STR(qa_compat_qc_strings_get(&s, first), "2");
  size_t snap_len; uint8_t *snap = qa_compat_qc_strings_snapshot(&s, &snap_len);
  qa_compat_qc_strings_set_engine(&s, "pr_string_temp", "3", -1);
  CHECK(qa_compat_qc_strings_restore(&s, snap, snap_len));
  CHECK_STR(qa_compat_qc_strings_get(&s, first), "2");
  free(snap);
  /* Reserved engine buffers are zero-filled: snapshots are deterministic. */
  { uint32_t used; size_t at = 8; snap = qa_compat_qc_strings_snapshot(&s, &snap_len); memcpy(&used, snap + at, 4);
    CHECK(used == 129); CHECK(snap[at + 4 + 1] == '2');
    bool clean = true; for (size_t i = 2; i < used; i++) if (snap[at + 4 + i] != 0) clean = false;
    CHECK(clean);
    free(snap); }
  /* The arena grows zero-filled. */
  qa_compat_qc_strings_set_engine(&s, "wide", "x", 100000);
  { size_t at = 12; snap = qa_compat_qc_strings_snapshot(&s, &snap_len);
    bool clean = true; for (size_t i = 129 + 1; i < 129 + 100000; i++) if (snap[at + i] != 0) clean = false;
    CHECK(clean); free(snap); }
  /* get results behave as TS string values across gets and mutations. */
  { const char *a = qa_compat_qc_strings_get(&s, -1), *b = qa_compat_qc_strings_get(&s, -2);
    CHECK_STR(a, "2"); CHECK_STR(b, "x");
    qa_compat_qc_strings_set_engine(&s, "pr_string_temp", "changed", -1);
    for (int i = 0; i < 40; i++) qa_compat_qc_strings_allocate(&s, "padding");
    CHECK_STR(a, "2"); CHECK_STR(b, "x"); }
  test_memory__S e = {&s, -3, NULL, 0};
  EXPECT_THROW(test_memory__get_body, &e, "progs.dat: invalid engine string -3");
  e.r = 100000000; EXPECT_THROW(test_memory__get_body, &e, "progs.dat: invalid string offset 100000000");
  e.t = "x"; e.c = 0; EXPECT_THROW(test_memory__engine_body, &e, "Invalid engine string capacity");
  e.c = 1; EXPECT_THROW(test_memory__engine_body, &e, "progs.dat: engine string exceeds its buffer");
  qa_compat_qc_strings_destroy(&s);

  /* A longer value for an existing name moves to max(length + 1, 2 * capacity); the old bytes stay. */
  { qa_compat_qc_strings g; CHECK(qa_compat_qc_strings_init(&g, &zero, 1, false));
    int64_t at = qa_compat_qc_strings_set_engine(&g, "temp", "ab", 4);
    int64_t moved = qa_compat_qc_strings_set_engine(&g, "temp", "abcdef", -1);
    CHECK(at == 1 && moved == 5); CHECK_STR(qa_compat_qc_strings_get(&g, at), "ab"); CHECK_STR(qa_compat_qc_strings_get(&g, moved), "abcdef");
    int64_t again = qa_compat_qc_strings_set_engine(&g, "temp", "abcdefghijklmnopqrst", -1);
    CHECK(again == 5 + 8); CHECK(qa_compat_qc_strings_set_engine(&g, "temp", "x", -1) == again);
    /* intern: one arena copy per value, forgotten by restore. */
    int64_t i1 = qa_compat_qc_strings_intern(&g, "shared"), i2 = qa_compat_qc_strings_intern(&g, "shared"), i3 = qa_compat_qc_strings_intern(&g, "other");
    CHECK(i1 == i2 && i3 != i1); CHECK_STR(qa_compat_qc_strings_get(&g, i1), "shared");
    for (int i = 0; i < 200; i++) { char t[16]; snprintf(t, sizeof t, "v%d", i); CHECK(qa_compat_qc_strings_intern(&g, t) == qa_compat_qc_strings_intern(&g, t)); }
    CHECK(qa_compat_qc_strings_intern(&g, "shared") == i1);
    size_t n; uint8_t *b = qa_compat_qc_strings_snapshot(&g, &n); CHECK(qa_compat_qc_strings_restore(&g, b, n)); free(b);
    CHECK(qa_compat_qc_strings_intern(&g, "shared") != i1);
    qa_compat_qc_strings_destroy(&g); }
  /* Netquake refuses negative IDs and hands back arena offsets. */
  CHECK(qa_compat_qc_strings_init(&s, &zero, 1, false));
  CHECK(qa_compat_qc_strings_set_engine(&s, "a", "x", 8) == 1);
  e.s = &s; e.r = -1; EXPECT_THROW(test_memory__get_body, &e, "invalid engine string -1");
  /* MAX_PRSTR only limits QuakeWorld. */
  qa_compat_qc_strings_destroy(&s);
  CHECK(qa_compat_qc_strings_init(&s, &zero, 1, true));
  for (int i = 0; i < 1023; i++) { char n[16]; snprintf(n, sizeof n, "n%d", i); qa_compat_qc_strings_set_engine(&s, n, "v", 2); }
  e.s = &s; e.t = "v"; e.c = 2; EXPECT_THROW(test_memory__engine_body, &e, "progs.dat: MAX_PRSTR");
  /* Restore rejects mismatched checkpoints. */
  snap = qa_compat_qc_strings_snapshot(&s, &snap_len);
  uint8_t *copy = malloc(snap_len + 1); memcpy(copy, snap, snap_len); copy[snap_len] = 0;
  test_memory__R r = {&s, copy, snap_len + 1}; EXPECT_THROW(test_memory__restore_body, &r, "trailing string checkpoint bytes");
  r.n = snap_len; copy[0] = 0; EXPECT_THROW(test_memory__restore_body, &r, "incompatible string checkpoint");
  free(copy); free(snap);
  qa_compat_qc_strings_destroy(&s);

  /* TS: entity pointers preserve prefix, stride and private bit patterns. */
  qa_compat_qc_entity_memory m; qa_compat_qc_entity_layout layout = {120, 96, 6};
  CHECK(qa_compat_qc_entity_memory_init(&m, layout, 4, 3));
  CHECK(qa_compat_qc_entity_memory_reference(&m, 2) == 240);
  int64_t pointer = qa_compat_qc_entity_memory_pointer(&m, 240, 4);
  CHECK(pointer == 352);
  qa_compat_qc_resolved_pointer dest = qa_compat_qc_entity_memory_resolve_pointer(&m, pointer, 1);
  qa_compat_qc_words_set_int(dest.fields, dest.word, -1);
  CHECK(qa_compat_qc_words_int(qa_compat_qc_entity_memory_at(&m, 2), 4) == -1);
  test_memory__E em = {&m, 240};
  EXPECT_THROW(test_memory__resolve_body, &em, "pointer 240 does not address entity variables");
  em.v = 351; EXPECT_THROW(test_memory__resolve_body, &em, "invalid entity pointer 351");
  em.v = 7; EXPECT_THROW(test_memory__slot_body, &em, "invalid entity reference 7");
  em.v = 360; EXPECT_THROW(test_memory__slot_body, &em, "invalid entity slot 3");
  em.v = -1; EXPECT_THROW(test_memory__at_body, &em, "invalid entity slot -1");
  em.v = 5; EXPECT_THROW(test_memory__count_body, &em, "Invalid QC entity count");
  qa_compat_qc_entity_memory_destroy(&m);

  /* TS DataView int/float staging. */
  uint8_t bytes[16] = {0}; qa_compat_qc_words w; qa_compat_qc_words_init(&w, bytes, 16);
  qa_compat_qc_words_set_int(&w, 0, 4294967295.0); CHECK(qa_compat_qc_words_int(&w, 0) == -1);
  qa_compat_qc_words_set_int(&w, 0, -1.9); CHECK(qa_compat_qc_words_int(&w, 0) == -1);
  qa_compat_qc_words_set_int(&w, 0, NAN); CHECK(qa_compat_qc_words_int(&w, 0) == 0);
  qa_compat_qc_words_set_int(&w, 0, 4294967298.0); CHECK(qa_compat_qc_words_int(&w, 0) == 2);
  qa_compat_qc_words_set_float(&w, 1, 0.1); CHECK(qa_compat_qc_words_float(&w, 1) == (double)0.1f);
  /* Sequential copy, including overlap. */
  qa_compat_qc_words_set_int(&w, 0, 7); qa_compat_qc_words_set_int(&w, 1, 8); qa_compat_qc_words_set_int(&w, 2, 9);
  qa_compat_qc_words_copy_words(&w, &w, 0, 1, 3);
  CHECK(qa_compat_qc_words_int(&w, 1) == 7 && qa_compat_qc_words_int(&w, 2) == 7 && qa_compat_qc_words_int(&w, 3) == 7);
  /* OP_RETURN reads absent words past the table as zero. */
  qa_compat_qc_words_set_int(&w, 3, 5);
  qa_compat_qc_words_copy_return_words(&w, &w, 3, 0, 3);
  CHECK(qa_compat_qc_words_int(&w, 0) == 5 && qa_compat_qc_words_int(&w, 1) == 0 && qa_compat_qc_words_int(&w, 2) == 0);
}

/* --- machine: synthetic program --- */
typedef struct {
  qa_compat_qc_machine *vm;
  int calls; int64_t saw_argc; const qa_compat_qc_function_execution *execute;
  qa_compat_qc_function_execution held; qa_compat_qc_function_execution inner; bool cancel_outer;
  int64_t words[3];
} BoundaryState;
static BoundaryState boundary_state;

/* The host builtin fn+ctx: every call asserts its own sentinel. */
static void host_cancel(void *ctx, qa_compat_qc_machine *m) {
  CHECK_CTX(ctx); (void)m;
  boundary_state.calls++;
  boundary_state.held.cancel(boundary_state.held.ctx, boundary_state.words);
}
static void host_noop(void *ctx, qa_compat_qc_machine *m) { CHECK_CTX(ctx); (void)m; boundary_state.calls++; }
static void host_fail(void *ctx, qa_compat_qc_machine *m) { CHECK_CTX(ctx); qa_compat_qc_machine_fail(m, "host refused %d", 7); }
static void machine_core(Rig *r, qa_compat_qc_machine_options *o, void (*host)(void *, qa_compat_qc_machine *)) {
  load_core(&r->program);
  netquake_builtins(r);
  qa_compat_qc_registry_set_numbered(&r->registry, 200, (qa_compat_qc_builtin){SENTINEL, host});
  rig_options(r, o, 4, 3);
}

static void boundary_cancels(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  CHECK_CTX(ctx); boundary_state.held = execute; boundary_state.saw_argc = call.function_index;
  execute.run(execute.ctx, NULL);
}
typedef struct { qa_compat_qc_function_execution e; int64_t words[3]; } test_cancellation__H;
static void test_cancellation__run_late(void *c) { test_cancellation__H *x = c; x->e.run(x->e.ctx, NULL); }
static void test_cancellation__skip_late(void *c) { test_cancellation__H *x = c; x->e.skip(x->e.ctx, x->words); }
static void test_cancellation__cancel_late(void *c) { test_cancellation__H *x = c; x->e.cancel(x->e.ctx, x->words); }
static void test_cancellation(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_cancel);
  int64_t selected[] = {F_CALLS};
  qa_compat_qc_function_boundary b = {SENTINEL, selected, 1, boundary_cancels};
  o.function_boundary = &b;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  memset(&boundary_state, 0, sizeof boundary_state);
  boundary_state.words[0] = 11; boundary_state.words[1] = -1; boundary_state.words[2] = 4294967295LL;
  qa_compat_qc_words_set_float(&r.vm.globals, 82, 9.0); /* the frame's saved local */
  qa_compat_qc_machine_execute(&r.vm, F_CALLS, 0);
  CHECK(boundary_state.calls == 1);
  /* The source frame unwound inside the boundary, its locals restored and depths reset. */
  CHECK(r.vm.frame_count == 0); CHECK(r.vm.builtin_depth == 0); CHECK(r.vm.boundary_depth == 0); CHECK(r.vm.local_words == 0);
  CHECK(qa_compat_qc_words_float(&r.vm.globals, 82) == 9.0);
  /* Cancellation words become the return. */
  CHECK(qa_compat_qc_words_int(&r.vm.globals, 1) == 11 && qa_compat_qc_words_int(&r.vm.globals, 2) == -1 && qa_compat_qc_words_int(&r.vm.globals, 3) == -1);
  /* A nonfunction machine state: snapshot is admitted again. */
  qa_compat_qc_machine_snapshot snap; CHECK(qa_compat_qc_machine_get_snapshot(&r.vm, &snap)); qa_compat_qc_machine_snapshot_destroy(&snap);
  /* Retained handle: expired for every continuation form. */
  test_cancellation__H h = {boundary_state.held, {0, 0, 0}};
  EXPECT_THROW(test_cancellation__run_late, &h, "function continuation must execute once inside its boundary");
  EXPECT_THROW(test_cancellation__skip_late, &h, "function continuation must execute once inside its boundary");
  EXPECT_THROW(test_cancellation__cancel_late, &h, "function cancellation requires its live source execution");
  /* A cancellation word outside a QC word fails by name, and unwinds. */
  boundary_state.words[1] = 4294967296LL;
  Run run = {&r.vm, F_CALLS, 0};
  EXPECT_THROW(run_body, &run, "replacement return is not a QC word");
  CHECK(r.vm.frame_count == 0); CHECK(r.vm.builtin_depth == 0); CHECK(r.vm.boundary_depth == 0);
  rig_destroy(&r);
}

/* Outer cancels through an inner boundary: TS rethrows past the inner frame. */
static void nested_inner(qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  (void)call; boundary_state.inner = execute; execute.run(execute.ctx, NULL);
  CHECK(!"the outer cancellation abandons this frame");
}
static void nested_run(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  CHECK_CTX(ctx);
  if (call.function_index == F_CALLS) { nested_inner(call, execute); return; }
  boundary_state.held = execute; execute.run(execute.ctx, NULL);
  boundary_state.saw_argc = 99; /* the outer boundary keeps running after the cancellation lands */
}
typedef struct { qa_compat_qc_function_execution e; } test_nested_cancellation__H;
static void test_nested_cancellation__run_late(void *c) { test_nested_cancellation__H *x = c; x->e.run(x->e.ctx, NULL); }
static void test_nested_cancellation(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_cancel);
  int64_t selected[] = {F_CALLS, F_OUTER};
  qa_compat_qc_function_boundary b = {SENTINEL, selected, 2, nested_run};
  o.function_boundary = &b;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  memset(&boundary_state, 0, sizeof boundary_state);
  boundary_state.words[0] = 5;
  qa_compat_qc_words_set_float(&r.vm.globals, 82, 3.0); qa_compat_qc_words_set_float(&r.vm.globals, 83, 4.0);
  qa_compat_qc_machine_execute(&r.vm, F_OUTER, 0);
  CHECK(boundary_state.saw_argc == 99);
  CHECK(r.vm.frame_count == 0); CHECK(r.vm.builtin_depth == 0); CHECK(r.vm.boundary_depth == 0); CHECK(r.vm.local_words == 0);
  CHECK(qa_compat_qc_words_float(&r.vm.globals, 82) == 3.0); CHECK(qa_compat_qc_words_float(&r.vm.globals, 83) == 4.0);
  CHECK(qa_compat_qc_words_int(&r.vm.globals, 1) == 5);
  /* The inner handle died with its boundary. */
  test_nested_cancellation__H h = {boundary_state.inner};
  EXPECT_THROW(test_nested_cancellation__run_late, &h, "function continuation must execute once inside its boundary");
  rig_destroy(&r);
}

/* A host cancellation that a builtin inside the source swallows ends source execution for good:
 * TS catch(any) in host code does the same; the record stays consistent. */
static void swallow_body(void *c) { (void)c; boundary_state.held.cancel(boundary_state.held.ctx, boundary_state.words); }
static void host_swallows(void *ctx, qa_compat_qc_machine *m) {
  CHECK_CTX(ctx); (void)m; qa_caught_error caught; boundary_state.calls++;
  CHECK(!qa_try(swallow_body, NULL, &caught));
  CHECK(m->builtin_depth == 1);
}
static void test_swallowed_cancellation(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_swallows);
  int64_t selected[] = {F_CALLS};
  qa_compat_qc_function_boundary b = {SENTINEL, selected, 1, boundary_cancels};
  o.function_boundary = &b;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  memset(&boundary_state, 0, sizeof boundary_state);
  qa_compat_qc_words_set_float(&r.vm.globals, 82, 9.0);
  qa_compat_qc_machine_execute(&r.vm, F_CALLS, 0);
  /* Source resumed after the builtin and returned its local through word 1. */
  CHECK(qa_compat_qc_words_float(&r.vm.globals, 1) == 1.5);
  CHECK(r.vm.frame_count == 0); CHECK(r.vm.builtin_depth == 0);
  rig_destroy(&r);
}

typedef struct { qa_compat_qc_machine *m; int64_t i; const char *n; } test_machine_faults__A;
static void test_machine_faults__arg_body(void *c) { test_machine_faults__A *x = c; qa_compat_qc_machine_arg_float(x->m, x->i); }
static void test_machine_faults__global_body(void *c) { test_machine_faults__A *x = c; qa_compat_qc_machine_global_offset(x->m, x->n); }
static void test_machine_faults__field_body(void *c) { test_machine_faults__A *x = c; qa_compat_qc_machine_field_offset(x->m, x->n); }
typedef struct { qa_compat_qc_machine *m; const qa_compat_qc_machine_snapshot *s; } test_machine_faults__SR;
static void test_machine_faults__restore_body(void *c) { test_machine_faults__SR *x = c; qa_compat_qc_machine_restore(x->m, x->s); }
typedef struct { qa_compat_qc_machine *m; qa_compat_qc_machine_options o; } test_machine_faults__I;
static void test_machine_faults__init_body(void *c) { test_machine_faults__I *x = c; qa_compat_qc_machine_init(x->m, x->o); qa_compat_qc_machine_destroy(x->m); }
static void test_machine_faults(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_fail);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  Run run = {vm, F_CALLS, 0};
  qa_compat_qc_words_set_float(&vm->globals, 82, 6.5);
  /* Message carries the failing position, the frame unwinds with locals restored. */
  EXPECT_THROW(run_body, &run, "QuakeC 3:4: host refused 7");
  CHECK(vm->frame_count == 0); CHECK(vm->builtin_depth == 0); CHECK(vm->local_words == 0);
  CHECK(qa_compat_qc_words_float(&vm->globals, 82) == 6.5);
  qa_compat_qc_machine_snapshot snap; CHECK(qa_compat_qc_machine_get_snapshot(vm, &snap)); qa_compat_qc_machine_snapshot_destroy(&snap);

  run.fn = F_MISSING; EXPECT_THROW(run_body, &run, "unbound builtin absent (300)");
  run.fn = F_NAMED; EXPECT_THROW(run_body, &run, "unbound builtin ex_named (named)");
  run.fn = F_SPIN; EXPECT_THROW(run_body, &run, "runaway loop error");
  run.fn = F_DEEP; EXPECT_THROW(run_body, &run, "stack overflow");
  CHECK(vm->frame_count == 0);
  run.fn = F_FALLS; EXPECT_THROW(run_body, &run, "statement outside program");
  run.fn = F_WORLD; EXPECT_THROW(run_body, &run, "assignment to world entity");
  run.fn = F_ADD; run.argc = 9; EXPECT_THROW(run_body, &run, "invalid argument count");
  run.argc = -1; EXPECT_THROW(run_body, &run, "invalid argument count");
  run.argc = 0; run.fn = 0; EXPECT_THROW(run_body, &run, "invalid function 0");

  test_machine_faults__A a = {vm, 8, "nosuch"};
  EXPECT_THROW(test_machine_faults__arg_body, &a, "invalid parameter 8");
  a.i = -1; EXPECT_THROW(test_machine_faults__arg_body, &a, "invalid parameter -1");
  EXPECT_THROW(test_machine_faults__global_body, &a, "missing global nosuch");
  EXPECT_THROW(test_machine_faults__field_body, &a, "missing entity field nosuch");
  int64_t off = -1;
  CHECK(!qa_compat_qc_machine_global_offset_or_null(vm, "nosuch", &off));
  CHECK(qa_compat_qc_machine_global_offset_or_null(vm, "time", &off) && off == 30);

  /* missingBuiltins: numbered and named entries. */
  qa_compat_qc_missing_builtin missing[8]; size_t n = qa_compat_qc_machine_missing_builtins(vm, missing, 8);
  CHECK(n == 2);
  CHECK(missing[0].function_index == F_NAMED && !missing[0].has_number); CHECK_STR(missing[0].name, "ex_named");
  CHECK(missing[1].function_index == F_MISSING && missing[1].has_number && missing[1].number == 300);

  /* Snapshot/restore guards. */
  qa_compat_qc_machine_snapshot bad; CHECK(qa_compat_qc_machine_get_snapshot(vm, &bad));
  bad.function_index = 2;
  test_machine_faults__SR sr = {vm, &bad};
  EXPECT_THROW(test_machine_faults__restore_body, &sr, "incompatible machine checkpoint");
  bad.function_index = 0; bad.globals_length -= 4;
  EXPECT_THROW(test_machine_faults__restore_body, &sr, "incompatible machine checkpoint");
  bad.globals_length += 4;
  bad.entity_count = 5; EXPECT_THROW(test_machine_faults__restore_body, &sr, "incompatible machine checkpoint");
  bad.entity_count = 0; EXPECT_THROW(test_machine_faults__restore_body, &sr, "incompatible machine checkpoint");
  bad.entity_count = 3; bad.entities_length -= 4; EXPECT_THROW(test_machine_faults__restore_body, &sr, "incompatible machine checkpoint");
  bad.entities_length += 4; bad.entity_count = 4; CHECK(qa_compat_qc_machine_restore(vm, &bad)); CHECK(r.entities.count == 4);
  qa_compat_qc_machine_snapshot_destroy(&bad);
  rig_destroy(&r);

  /* Limits and layout. */
  Rig l; machine_core(&l, &o, host_noop);
  qa_compat_qc_machine_options bad_o = o; bad_o.statement_limit = -1;
  test_machine_faults__I init = {&l.vm, bad_o};
  EXPECT_THROW(test_machine_faults__init_body, &init, "Invalid QuakeC execution limits");
  /* Function boundary on a builtin; invalid function index. */
  int64_t builtin_selected[] = {F_HOST};
  qa_compat_qc_function_boundary fb = {SENTINEL, builtin_selected, 1, boundary_cancels};
  init.o = o; init.o.function_boundary = &fb;
  EXPECT_THROW(test_machine_faults__init_body, &init, "function boundary requires an interpreted function");
  builtin_selected[0] = 0; EXPECT_THROW(test_machine_faults__init_body, &init, "invalid function 0");
  /* Inline admission. */
  qa_compat_qc_inline_boundary ib; memset(&ib, 0, sizeof ib);
  qa_compat_qc_inline_region regions[2]; memset(regions, 0, sizeof regions);
  ib.regions = regions; ib.region_count = 1; ib.ctx = SENTINEL;
  init.o = o; init.o.inline_boundary = &ib;
  regions[0] = (qa_compat_qc_inline_region){F_REGIONED, REGION_ENTRY - 1, REGION_EXIT, false, false, 0, NULL};
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid inline source region");
  regions[0].entry = REGION_ENTRY; regions[0].exit = REGION_ENTRY;
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid inline source region");
  regions[0].exit = 13; /* next function's first statement */
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid inline source region");
  regions[0] = (qa_compat_qc_inline_region){F_NAMED, 1, 2, false, false, 0, NULL};
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid inline source region");
  regions[0] = (qa_compat_qc_inline_region){F_REGIONED, REGION_ENTRY, REGION_EXIT, false, false, 0, NULL}; regions[1] = regions[0];
  ib.region_count = 2; EXPECT_THROW(test_machine_faults__init_body, &init, "invalid inline source region");
  ib.region_count = 1;
  regions[0] = (qa_compat_qc_inline_region){F_REGIONED, REGION_ENTRY, REGION_EXIT, false, true, 86, NULL};
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid standalone inline result"); /* outside the frame */
  regions[0].standalone_scope = "global"; regions[0].standalone_saved = 27;
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid standalone inline result"); /* reserved */
  regions[0].standalone_saved = 85;
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid standalone inline result"); /* inside the frame */
  regions[0].standalone_saved = G_RSTR;
  EXPECT_THROW(test_machine_faults__init_body, &init, "invalid standalone inline result"); /* not a float global */
  qa_compat_qc_registry_destroy(&l.registry);
  qa_compat_qc_entity_memory_destroy(&l.entities); qa_compat_qc_program_destroy(&l.program);
}

static void test_opcodes(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  o.local_stack_words = 16;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  /* toInt32 before bit ops: 3 & 1, 3 | 1; And/Or are boolean floats. */
  qa_compat_qc_machine_execute(vm, F_BITS, 0);
  CHECK(qa_compat_qc_words_float(&vm->globals, 60) == 1); CHECK(qa_compat_qc_words_float(&vm->globals, 61) == 3);
  CHECK(qa_compat_qc_words_float(&vm->globals, 62) == 0); CHECK(qa_compat_qc_words_float(&vm->globals, 63) == 1);
  /* Strings: byteCompare difference for NeS, not 0/1. */
  qa_compat_qc_machine_execute(vm, F_STRINGS, 0);
  CHECK(qa_compat_qc_words_float(&vm->globals, 70) == 0);
  CHECK(qa_compat_qc_words_float(&vm->globals, 71) == (double)('c' - 'd'));
  CHECK(qa_compat_qc_words_float(&vm->globals, 72) == 1);
  CHECK(qa_compat_qc_words_float(&vm->globals, 73) == 0); CHECK(qa_compat_qc_words_float(&vm->globals, 74) == 1);
  /* Arguments stage into parameters, the callee's locals are restored by return. */
  qa_compat_qc_words_set_float(&vm->globals, 80, 77); qa_compat_qc_words_set_float(&vm->globals, 81, 78);
  qa_compat_qc_words_set_float(&vm->globals, 4, 1.5); qa_compat_qc_words_set_float(&vm->globals, 7, 0.1);
  qa_compat_qc_machine_execute(vm, F_ADD, 2);
  CHECK(qa_compat_qc_words_float(&vm->globals, 1) == (double)(float)(1.5 + (double)0.1f));
  CHECK(qa_compat_qc_words_float(&vm->globals, 80) == 77 && qa_compat_qc_words_float(&vm->globals, 81) == 78);
  CHECK(qa_compat_qc_machine_get_argc(vm) == 2);
  /* Locals budget. */
  Run run = {vm, F_BIG, 0}; EXPECT_THROW(run_body, &run, "locals stack overflow");
  rig_destroy(&r);
}

/* validateEntityAccess, observeEntityStore, observeCall, trace, sentinel contexts. */
typedef struct { int validations, observed, calls, traces; int64_t last_word; char last_kind[8]; uint8_t last_before[12], last_after[12]; size_t last_len; } Observe;
static Observe observe;
static void o_validate(void *ctx, int64_t reference, int64_t word, int64_t words, const char *kind) {
  CHECK_CTX(ctx); (void)reference; (void)words; observe.validations++; observe.last_word = word; snprintf(observe.last_kind, sizeof observe.last_kind, "%s", kind);
}
static void o_store(void *ctx, qa_compat_qc_entity_store_observation s) {
  CHECK_CTX(ctx); observe.observed++; observe.last_len = s.before_length; memcpy(observe.last_before, s.before, s.before_length); memcpy(observe.last_after, s.after, s.after_length);
}
static void o_call(void *ctx, qa_compat_qc_call_site c) { CHECK_CTX(ctx); (void)c; observe.calls++; }
static void o_trace(void *ctx, qa_compat_qc_machine *m) { CHECK_CTX(ctx); (void)m; observe.traces++; }
static void test_observers(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  o.validate_entity_access = o_validate; o.observe_entity_store = o_store; o.observe_call = o_call; o.trace = o_trace;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm; memset(&observe, 0, sizeof observe);
  int64_t ref = qa_compat_qc_entity_memory_reference(&r.entities, 1);
  qa_compat_qc_words_set_int(&vm->globals, 28, (double)ref);
  /* setEntity* stores: validator then observation with the before/after words. */
  qa_compat_qc_machine_set_entity_float(vm, ref, "health", 2.5);
  CHECK(observe.validations == 1 && observe.observed == 1); CHECK_STR(observe.last_kind, "write");
  CHECK(observe.last_len == 4);
  { float before, after; memcpy(&before, observe.last_before, 4); memcpy(&after, observe.last_after, 4); CHECK(before == 0 && after == 2.5f); }
  CHECK(qa_compat_qc_machine_entity_float(vm, ref, "health") == 2.5);
  CHECK_STR(observe.last_kind, "read");
  qa_vec3 v = {1, 2, 3}; qa_compat_qc_machine_set_entity_vector(vm, ref, "origin", v); CHECK(observe.last_len == 12);
  CHECK(qa_compat_qc_machine_entity_vector(vm, ref, "origin").z == 3);
  /* Pointer store validates and observes; the load validates as a read. */
  memset(&observe, 0, sizeof observe);
  qa_compat_qc_machine_execute(vm, F_POINTER, 0);
  CHECK(observe.observed == 1); CHECK(observe.calls == 1); CHECK(observe.validations == 2); CHECK_STR(observe.last_kind, "read");
  CHECK(qa_compat_qc_words_float(&vm->globals, W_OUT) == 1.5);
  /* STATE under a validator: three validated, observed writes. */
  memset(&observe, 0, sizeof observe);
  qa_compat_qc_words_set_float(&vm->globals, 30, 3.0);
  qa_compat_qc_machine_execute(vm, F_STATE, 0);
  CHECK(observe.validations == 3 && observe.observed == 3);
  CHECK(qa_compat_qc_machine_entity_float(vm, ref, "nextthink") == (double)(float)3.1);
  CHECK(qa_compat_qc_machine_entity_float(vm, ref, "frame") == 1.5);
  CHECK(qa_compat_qc_machine_entity_int(vm, ref, "think") == F_HOST);
  /* Trace runs only while traceEnabled; execute clears it. */
  memset(&observe, 0, sizeof observe);
  vm->trace_enabled = true; qa_compat_qc_machine_execute(vm, F_BITS, 0); CHECK(observe.traces == 0);
  rig_destroy(&r);
}
static void test_state_unobserved(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  o.observe_entity_store = o_store;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  memset(&observe, 0, sizeof observe);
  int64_t ref = qa_compat_qc_entity_memory_reference(&r.entities, 2);
  qa_compat_qc_words_set_int(&r.vm.globals, 28, (double)ref);
  qa_compat_qc_machine_execute(&r.vm, F_STATE, 0);
  /* TS observes STATE only inside its validator branch. */
  CHECK(observe.observed == 0);
  CHECK(qa_compat_qc_machine_entity_int(&r.vm, ref, "think") == F_HOST);
  rig_destroy(&r);
}

/* --- inline boundaries --- */
typedef struct { int mode, runs; qa_compat_qc_inline_continuation held; } InlineState;
static InlineState inline_state;
static void inline_again(void *c) { qa_compat_qc_inline_continuation *x = c; x->run(x->ctx); }
static void inline_boundary_run(void *ctx, qa_compat_qc_inline_region region, qa_compat_qc_inline_continuation execute) {
  CHECK_CTX(ctx); (void)region; inline_state.runs++; inline_state.held = execute;
  switch (inline_state.mode) {
    case 0: execute.run(execute.ctx); break;
    case 1: execute.skip_to_join(execute.ctx); break;
    case 2: break; /* omitted */
    case 3: { execute.run(execute.ctx);
      qa_caught_error caught;
      CHECK(!qa_try(inline_again, &execute, &caught)); break; }
    default: break;
  }
}
static void test_inline__run_late(void *c) { qa_compat_qc_inline_continuation *x = c; x->run(x->ctx); }
typedef struct { qa_compat_qc_machine *m; qa_compat_qc_inline_region g; int64_t n; } test_inline__G;
static void test_inline__region_body(void *c) { test_inline__G *x = c; (void)qa_compat_qc_machine_execute_region(x->m, x->g, x->n); }
static void test_inline(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  qa_compat_qc_inline_region regions[3] = {
    {F_REGIONED, REGION_ENTRY, REGION_EXIT, true, true, 85, NULL},
    {F_ESCAPE, ESCAPE_ENTRY, ESCAPE_EXIT, false, false, 0, NULL},
    {F_RETURNS, RETURNS_ENTRY, RETURNS_EXIT, false, false, 0, NULL},
  };
  qa_compat_qc_inline_boundary ib = {SENTINEL, regions, 3, inline_boundary_run};
  o.inline_boundary = &ib;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  memset(&inline_state, 0, sizeof inline_state);
  /* Execute: the region runs, then the join statement. */
  qa_compat_qc_machine_execute(vm, F_REGIONED, 1);
  CHECK(inline_state.runs == 1);
  CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 3.75); CHECK(qa_compat_qc_words_float(&vm->globals, G_RESULT) == 3.75);
  /* The expired continuation fails by name. */
  EXPECT_THROW(test_inline__run_late, &inline_state.held, "inline continuation must execute once inside its boundary");
  /* skipToJoin: statements in the region never ran; the local read back is whatever was in it. */
  inline_state.mode = 1;
  qa_compat_qc_words_set_float(&vm->globals, 85, 8.0); qa_compat_qc_words_set_float(&vm->globals, G_RESULT, 11.0);
  qa_compat_qc_machine_execute(vm, F_REGIONED, 1);
  CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 8.0); /* restored frame word held 8 */
  CHECK(qa_compat_qc_words_float(&vm->globals, G_RESULT) == 8.0);
  /* Omitted. */
  inline_state.mode = 2;
  Run run = {vm, F_REGIONED, 1};
  EXPECT_THROW(run_body, &run, "inline boundary omitted source execution");
  CHECK(vm->frame_count == 0);
  /* A swallowed second continuation still fails the boundary. */
  inline_state.mode = 3;
  EXPECT_THROW(run_body, &run, "inline continuation must execute once inside its boundary");
  CHECK(vm->frame_count == 0);
  /* Skip needs admission. */
  inline_state.mode = 1;
  run.fn = F_ESCAPE; run.argc = 0;
  EXPECT_THROW(run_body, &run, "inline source region is not admitted for replacement");
  CHECK(vm->frame_count == 0);
  /* Escape and early return are rejected. */
  inline_state.mode = 0;
  EXPECT_THROW(run_body, &run, "inline source region escaped its continuation");
  run.fn = F_RETURNS;
  EXPECT_THROW(run_body, &run, "inline source region returned before its continuation");
  CHECK(vm->frame_count == 0);

  /* executeRegion, frame-saved result: locals restored around it, argument count too. */
  qa_compat_qc_words_set_float(&vm->globals, 85, 1.0);
  vm->argument_count = 5;
  CHECK(qa_compat_qc_machine_execute_region(vm, regions[0], 1) == 3.75);
  CHECK(vm->frame_count == 0); CHECK(vm->argument_count == 5);
  CHECK(qa_compat_qc_words_float(&vm->globals, 85) == 1.0);
  test_inline__G g = {vm, regions[1], 0};
  EXPECT_THROW(test_inline__region_body, &g, "standalone inline execution requires an admitted source region");
  g.g = regions[0]; g.n = 0;
  EXPECT_THROW(test_inline__region_body, &g, "invalid standalone inline argument count");
  rig_destroy(&r);

  /* executeRegion, global-saved result: the global returns to its previous word; an escape restores too. */
  Rig r2; machine_core(&r2, &o, host_noop);
  qa_compat_qc_inline_region global_region = {F_REGIONED, REGION_ENTRY, REGION_EXIT + 1, false, true, G_RESULT, "global"};
  qa_compat_qc_inline_boundary ib2 = {SENTINEL, &global_region, 1, inline_boundary_run};
  o.inline_boundary = &ib2;
  CHECK(qa_compat_qc_machine_init(&r2.vm, o));
  qa_compat_qc_words_set_float(&r2.vm.globals, G_RESULT, 42.0);
  CHECK(qa_compat_qc_machine_execute_region(&r2.vm, global_region, 1) == 3.75);
  CHECK(qa_compat_qc_words_float(&r2.vm.globals, G_RESULT) == 42.0);
  rig_destroy(&r2);
}

/* --- save --- */
typedef struct { qa_compat_qc_machine *m; int64_t slot; const qa_compat_qc_text_pair *pairs; size_t n; const char *text; const qa_compat_qc_definition *d; qa_compat_qc_words *w; } SaveRun;
static void parse_body(void *c) { SaveRun *s = c; qa_compat_qc_parse_value(s->m, s->w, s->d, s->text); }
typedef struct { qa_compat_qc_machine *m; } test_save__A;
static void test_save__bad_pairs(void *c) { test_save__A *x = c; qa_compat_qc_entity_parse_result_compat p; qa_compat_qc_text_pair bad[] = {{"message", "keep"}, {"think", "nosuch"}}; qa_compat_qc_apply_entity_pairs(x->m, 3 - 1, bad, 2, &p); }
static void test_save__save_entity(void *c) { test_save__A *x = c; qa_compat_qc_text_pairs p; qa_compat_qc_save_entity_pairs(x->m, 2, false, &p); }
static void test_save(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_at(&r.entities, 1);
  char long_value[400]; memset(long_value, ' ', 300); memcpy(long_value + 290, "90", 3); /* spaces then a number */
  char long_component[300]; memset(long_component, '0', 200); memcpy(long_component + 200, "12", 3);
  char long_vector[640]; snprintf(long_vector, sizeof long_vector, "%s 2 3", long_component);
  qa_compat_qc_text_pair pairs[] = {
    {"angle", "90"}, {"message", "first\\nsecond\\x\\"}, {"private_unknown", "keep"}, {"_ignored", "x"},
    {"light", "7"}, {"health ", "3"}, {"origin", long_vector}, {"owner", "2.9"}, {"think", "add"},
    {"fld", "health"}, {"nothing", "ignored"},
  };
  qa_compat_qc_entity_parse_result_compat parsed;
  CHECK(qa_compat_qc_apply_entity_pairs(vm, 1, pairs, sizeof pairs / sizeof *pairs, &parsed));
  CHECK(!parsed.empty);
  CHECK(parsed.unknown.count == 1); CHECK_STR(parsed.unknown.items[0].key, "private_unknown");
  CHECK(parsed.retained.count == 10); CHECK_STR(parsed.retained.items[0].key, "angles"); CHECK_STR(parsed.retained.items[0].value, "90");
  CHECK_STR(parsed.retained.items[3].key, "light_lev"); CHECK_STR(parsed.retained.items[4].key, "health");
  qa_vec3 angles = qa_compat_qc_words_vector(self, 9); CHECK(angles.x == 0 && angles.y == 90 && angles.z == 0);
  CHECK_STR(qa_compat_qc_strings_get(&vm->strings, qa_compat_qc_words_int(self, 14)), "first\nsecond\\\\");
  CHECK(qa_compat_qc_words_float(self, 12) == 7); CHECK(qa_compat_qc_words_float(self, 4) == 3);
  /* A component beyond any fixed buffer parses whole. */
  qa_vec3 origin = qa_compat_qc_words_vector(self, 1); CHECK(origin.x == 12 && origin.y == 2 && origin.z == 3);
  CHECK(qa_compat_qc_words_int(self, 8) == qa_compat_qc_entity_memory_reference(&r.entities, 2));
  CHECK(qa_compat_qc_words_int(self, 5) == F_ADD);
  qa_compat_qc_text_pairs_free(&parsed.unknown); qa_compat_qc_text_pairs_free(&parsed.retained);
  /* A long "angle" keeps its whole value. */
  qa_compat_qc_text_pair angle_long[] = {{"angle", long_value}};
  CHECK(qa_compat_qc_apply_entity_pairs(vm, 1, angle_long, 1, &parsed));
  CHECK(qa_compat_qc_words_vector(self, 9).y == 0);
  qa_compat_qc_text_pairs_free(&parsed.unknown); qa_compat_qc_text_pairs_free(&parsed.retained);
  CHECK(qa_compat_qc_apply_entity_pairs(vm, 2, NULL, 0, &parsed)); CHECK(parsed.empty);

  /* Vector text with short components: remaining words parse as empty, zero. */
  SaveRun s = {vm, 1, NULL, 0, "5", qa_compat_qc_program_field_named(&r.program, "origin"), self};
  parse_body(&s); origin = qa_compat_qc_words_vector(self, 1); CHECK(origin.x == 5 && origin.y == 0 && origin.z == 0);
  /* Failures, by name. */
  s.d = qa_compat_qc_program_field_named(&r.program, "fld"); s.text = "nosuch";
  EXPECT_THROW(parse_body, &s, "cannot find field nosuch");
  s.d = qa_compat_qc_program_field_named(&r.program, "ptr"); s.text = "1";
  EXPECT_THROW(parse_body, &s, "text parsing is unavailable for QC type 7 (ptr); restore raw state instead");
  s.d = qa_compat_qc_program_field_named(&r.program, "opaque");
  EXPECT_THROW(parse_body, &s, "text parsing is unavailable for QC type 9 (opaque); restore raw state instead");
  s.d = qa_compat_qc_program_field_named(&r.program, "think"); s.text = "nosuch";
  EXPECT_THROW(parse_body, &s, "core.dat: missing function nosuch");
  s.d = qa_compat_qc_program_field_named(&r.program, "owner"); s.text = "9";
  EXPECT_THROW(parse_body, &s, "invalid entity slot 9");
  s.text = "12345678901234567890123"; /* far past int64: named as String(slot) */
  EXPECT_THROW(parse_body, &s, "invalid entity slot 1.2345678901234568e+22");
  /* A failing pair frees the partial result and propagates. */
   test_save__A a = {vm};
  EXPECT_THROW(test_save__bad_pairs, &a, "missing function nosuch");

  /* Saving: slice(1) skips the first field; names ending "_x" skip; zero words skip. */
  qa_compat_qc_text_pair saved_in[] = {{"classname", "skipped"}, {"health", "0.5"}, {"a_b", "3"}, {"velocity", "1 2 3"}};
  CHECK(qa_compat_qc_apply_entity_pairs(vm, 2, saved_in, 4, &parsed));
  qa_compat_qc_text_pairs_free(&parsed.unknown); qa_compat_qc_text_pairs_free(&parsed.retained);
  qa_compat_qc_text_pairs out;
  CHECK(qa_compat_qc_save_entity_pairs(vm, 2, false, &out));
  CHECK(out.count == 2);
  CHECK_STR(out.items[0].key, "health"); CHECK_STR(out.items[0].value, "0.500000");
  CHECK_STR(out.items[1].key, "velocity"); CHECK_STR(out.items[1].value, "1.000000 2.000000 3.000000");
  qa_compat_qc_text_pairs_free(&out);
  CHECK(qa_compat_qc_save_entity_pairs(vm, 2, true, &out)); CHECK(out.count == 0);
  /* Saved field/function/entity/void forms; a pointer word fails by name. */
  qa_compat_qc_words *e3 = qa_compat_qc_entity_memory_at(&r.entities, 2);
  memset(e3->bytes, 0, e3->byte_length);
  qa_compat_qc_words_set_int(e3, 15, 4); qa_compat_qc_words_set_int(e3, 5, F_ADD); qa_compat_qc_words_set_int(e3, 8, (double)qa_compat_qc_entity_memory_reference(&r.entities, 1)); qa_compat_qc_words_set_int(e3, 18, 1);
  CHECK(qa_compat_qc_save_entity_pairs(vm, 2, false, &out));
  CHECK(out.count == 4);
  CHECK_STR(out.items[0].key, "think"); CHECK_STR(out.items[0].value, "add");
  CHECK_STR(out.items[1].key, "owner"); CHECK_STR(out.items[1].value, "1");
  CHECK_STR(out.items[2].key, "fld"); CHECK_STR(out.items[2].value, "health");
  CHECK_STR(out.items[3].key, "nothing"); CHECK_STR(out.items[3].value, "void");
  qa_compat_qc_text_pairs_free(&out);
  qa_compat_qc_words_set_int(e3, 16, 1);
  EXPECT_THROW(test_save__save_entity, &a, "text saving is unavailable for QC type 7 (ptr); save raw state instead");
  qa_compat_qc_words_set_int(e3, 16, 0); qa_compat_qc_words_set_int(e3, 15, 39);
  EXPECT_THROW(test_save__save_entity, &a, "cannot save field offset 39");
  /* Globals: save flag and a string/float/entity type. */
  qa_compat_qc_words_set_float(&vm->globals, G_RESULT, 0.25); qa_compat_qc_words_set_int(&vm->globals, G_RSTR, STR_ABC); qa_compat_qc_words_set_int(&vm->globals, G_RENT, (double)qa_compat_qc_entity_memory_reference(&r.entities, 2));
  CHECK(qa_compat_qc_save_global_pairs(vm, &out));
  CHECK(out.count == 3);
  CHECK_STR(out.items[0].key, "result"); CHECK_STR(out.items[0].value, "0.250000");
  CHECK_STR(out.items[1].key, "rstr"); CHECK_STR(out.items[1].value, "abc");
  CHECK_STR(out.items[2].key, "rent"); CHECK_STR(out.items[2].value, "2");
  qa_compat_qc_text_pairs_free(&out);
  qa_compat_qc_text_pair globals[] = {{"result", "6.5"}, {"nosuch", "z"}, {"rstr", "two\\nlines"}};
  CHECK(qa_compat_qc_apply_global_pairs(vm, globals, 3, &out));
  CHECK(out.count == 1); CHECK_STR(out.items[0].key, "nosuch"); CHECK_STR(out.items[0].value, "z");
  qa_compat_qc_text_pairs_free(&out);
  CHECK(qa_compat_qc_words_float(&vm->globals, G_RESULT) == 6.5);
  CHECK_STR(qa_compat_qc_strings_get(&vm->strings, qa_compat_qc_words_int(&vm->globals, G_RSTR)), "two\nlines");
  rig_destroy(&r);

  /* QuakeWorld keeps trailing-space keys (NetQuake trims). */
  Rig q; size_t length; uint8_t *bytes = core_bytes(54730, &length);
  CHECK(qa_compat_qc_load_program(bytes, length, NULL, "qw.dat", &q.program)); free(bytes);
  CHECK(q.program.api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD);
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services); services.kind = "quakeworld";
  memset(&q.registry, 0, sizeof q.registry); CHECK(qa_compat_qc_create_builtins(services, &q.registry));
  rig_options(&q, &o, 4, 3); CHECK(qa_compat_qc_machine_init(&q.vm, o));
  qa_compat_qc_text_pair spaced[] = {{"health ", "3"}};
  CHECK(qa_compat_qc_apply_entity_pairs(&q.vm, 1, spaced, 1, &parsed));
  CHECK(parsed.unknown.count == 1); CHECK_STR(parsed.unknown.items[0].key, "health ");
  qa_compat_qc_text_pairs_free(&parsed.unknown); qa_compat_qc_text_pairs_free(&parsed.retained);
  rig_destroy(&q);
}

/* --- profile --- */
typedef struct { const uint8_t *bytes; size_t length; const char *accept; } MountState;
static uint8_t *mount_open(void *ctx, const char *path, size_t *out_length, qa_resolved_resource_reference *out_reference, void *storage) {
  CHECK(storage == SENTINEL); MountState *m = ctx; (void)out_reference;
  if (strcmp(path, m->accept) != 0) return NULL;
  uint8_t *copy = malloc(m->length); memcpy(copy, m->bytes, m->length); *out_length = m->length; return copy;
}
typedef struct { qa_compat_qc_mounted_open o; const char *kind; } test_profile__L;
static void test_profile__load_mounted(void *c) { test_profile__L *x = c; qa_compat_qc_program q; qa_compat_qc_load_mounted_program(x->o, x->kind, &q, NULL); }
typedef struct { qa_compat_qc_program *p; qa_compat_qc_entity_layout l; qa_compat_qc_builtin_registry *r; } test_profile__D;
static void test_profile__describe(void *c) { test_profile__D *x = c; qa_compat_qc_host_description h; qa_compat_qc_describe_host(x->p, "quakeworld", x->l, x->r, NULL, 0, &h); }
static void test_profile(void) {
  const char *order[2];
  CHECK(qa_compat_qc_program_search_order("quakeworld", order, 2) == 2); CHECK_STR(order[0], "qwprogs.dat"); CHECK_STR(order[1], "progs.dat");
  CHECK(qa_compat_qc_program_search_order("netquake", order, 2) == 1); CHECK_STR(order[0], "progs.dat");
  CHECK(qa_compat_qc_program_search_order("rerelease", order, 2) == 1);

  size_t length; uint8_t *qw_bytes = core_bytes(54730, &length);
  MountState mount = {qw_bytes, length, "qwprogs.dat"};
  qa_compat_qc_mounted_open open = {&mount, mount_open, SENTINEL};
  qa_compat_qc_program p;
  CHECK(qa_compat_qc_load_mounted_program(open, "quakeworld", &p, NULL));
  CHECK_STR(p.source, "qwprogs.dat");
  /* QW layout prefix. */
  qa_compat_qc_entity_layout layout = qa_compat_qc_classic_entity_layout(&p);
  CHECK(layout.variables_offset_bytes == 104 && layout.stride_bytes == 104 + 40 * 4 && layout.field_words == 40);
  qa_compat_qc_program_destroy(&p);
  /* Falls back to progs.dat; a netquake host refuses QW bytes by name, and frees the bytes it read. */
  mount.accept = "progs.dat";
  CHECK(qa_compat_qc_load_mounted_program(open, "quakeworld", &p, NULL)); CHECK_STR(p.source, "progs.dat"); qa_compat_qc_program_destroy(&p);
   test_profile__L l = {open, "netquake"};
  EXPECT_THROW(test_profile__load_mounted, &l, "progs.dat: expected q1-netquake, found q1-quakeworld");
  mount.accept = "none";
  EXPECT_THROW(test_profile__load_mounted, &l, "progs.dat: missing progs.dat");
  l.kind = "quakeworld"; EXPECT_THROW(test_profile__load_mounted, &l, "progs.dat: missing qwprogs.dat or progs.dat");
  free(qw_bytes);

  /* describeQcHost. */
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  qa_compat_qc_host_description d;
  static const char *const extensions[] = {"DP_TEST"};
  qa_compat_qc_entity_layout nq = qa_compat_qc_classic_entity_layout(&r.program);
  CHECK(nq.variables_offset_bytes == 96);
  CHECK(qa_compat_qc_describe_host(&r.program, "netquake", nq, &r.registry, extensions, 1, &d));
  CHECK_STR(d.globals_layout.id, "quakec:5927-globals"); CHECK_STR(d.entity_layout.id, "quakec:5927-entity-variables");
  CHECK(d.globals_layout.byte_length == 128 * 4); CHECK(d.entity_layout.byte_length == 40 * 4);
  /* Storage by type: vectors count three float words; fields and strings are ints. */
  bool vec_ok = false, time_ok = false, str_ok = false;
  for (size_t i = 0; i < d.globals_layout.field_count; i++) {
    const qa_guest_field_layout *f = &d.globals_layout.fields[i];
    if (strcmp(f->name, "vec") == 0) vec_ok = f->byte_offset == G_VEC * 4 && f->count == 3 && strcmp(f->storage, "float32") == 0;
    if (strcmp(f->name, "time") == 0) time_ok = f->byte_offset == 120 && f->count == 1 && strcmp(f->storage, "float32") == 0;
    if (strcmp(f->name, "rstr") == 0) str_ok = f->count == 1 && strcmp(f->storage, "int32") == 0;
  }
  CHECK(vec_ok && time_ok && str_ok);
  /* Numbered first, with the program's name or builtin_N; then named. */
  size_t count = d.profile.builtin_count; CHECK(count == r.registry.numbered_count + r.registry.named_count);
  bool host200 = false, fallback = false;
  for (size_t i = 0; i < count; i++) {
    if (d.bindings[i].number == 200) { host200 = strcmp(d.bindings[i].name, "host") == 0 && strcmp(d.bindings[i].callback, "quakec:numbered-200") == 0 && strcmp(d.bindings[i].kind, "numbered") == 0; }
    if (d.bindings[i].number == 9) fallback = strcmp(d.bindings[i].name, "builtin_9") == 0;
  }
  CHECK(host200 && fallback);
  qa_compat_qc_host_description_destroy(&d);
  test_profile__D dd = {&r.program, nq, &r.registry};
  EXPECT_THROW(test_profile__describe, &dd, "progs.dat: host kind disagrees with program API");
  qa_compat_qc_registry_destroy(&r.registry); qa_compat_qc_entity_memory_destroy(&r.entities); qa_compat_qc_program_destroy(&r.program);
}

/* --- executor --- */
typedef struct { uint8_t counter; int checkpoints, restores; qa_module_identity module; const char *format; } HostState;
static HostState host_state;
static qa_compat_qc_host_saved_state host_checkpoint(void *ctx) {
  CHECK_CTX(ctx); host_state.checkpoints++;
  qa_compat_qc_host_saved_state saved; memset(&saved, 0, sizeof saved);
  saved.state.module = host_state.module; saved.state.format = host_state.format;
  saved.state.bytes.bytes = &host_state.counter; saved.state.bytes.length = 1;
  return saved;
}
static void host_restore(void *ctx, qa_compat_qc_host_saved_state saved) {
  CHECK_CTX(ctx); host_state.restores++;
  CHECK_STR(saved.state.format, host_state.format);
  CHECK(saved.state.bytes.length == 1);
  host_state.counter = saved.state.bytes.bytes[0];
}
static bool table_actor(void *ctx, int64_t slot, qa_actor_id *out) {
  CHECK_CTX(ctx); if (slot == 0) return false;
  memset(out, 0, sizeof *out); out->slot = slot; out->generation = 3; return true;
}
typedef struct { qa_raw_entity_table *t; qa_guest_address a; } test_executor__P;
/* TS removed QuakeCExecutor (2026-11-01 remediation); checkpoints go through captureQcCheckpoint/restoreQcCheckpoint. */
typedef struct { qa_compat_qc_machine *m; qa_module_identity module; qa_compat_qc_executor_host host; qa_guest_checkpoint cp;
                 uint8_t *bytes; qa_compat_qc_machine_snapshot snap; bool has; } test_executor__X;
static const qa_guest_checkpoint *test_executor__capture(test_executor__X *x) {
  if (x->has) { free(x->bytes); x->bytes = NULL; qa_compat_qc_machine_snapshot_destroy(&x->snap); memset(&x->snap, 0, sizeof x->snap); }
  x->has = qa_compat_qc_capture_checkpoint(x->m, x->module, x->host, &x->cp, &x->snap, &x->bytes);
  CHECK(x->has);
  return &x->cp;
}
static void test_executor__release(test_executor__X *x) {
  if (x->has) { free(x->bytes); qa_compat_qc_machine_snapshot_destroy(&x->snap); }
  x->has = false;
}
typedef struct { test_executor__X *e; } test_executor__C;
typedef struct { qa_compat_qc_machine *m; qa_module_identity i; } test_executor__T;
static void test_executor__pointer_body(void *c) { test_executor__P *x = c; (void)x->t->from_pointer(x->t->ctx, x->a); }
typedef struct { test_executor__X *e; const qa_guest_checkpoint *c; } test_executor__Rs;
static void test_executor__restore_body(void *c) { test_executor__Rs *x = c; (void)qa_compat_qc_restore_checkpoint(x->e->m, x->e->module, x->e->host, x->c); }
static void test_executor__preflight_body(void *c) { test_executor__Rs *x = c; qa_compat_qc_preflight_checkpoint(x->e->m, x->e->module, x->c); }
static void test_executor__checkpoint_body(void *c) { test_executor__C *x = c; (void)test_executor__capture(x->e); }
static void test_executor__table_body(void *c) { test_executor__T *x = c; qa_compat_qc_raw_table_state st; qa_guest_layout big = {"t:b", 1e6, 4, 4, NULL, 0}; (void)qa_compat_qc_create_raw_entity_table(x->m, x->i, big, SENTINEL, table_actor, &st); }
static void test_executor(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  qa_module_identity module; memset(&module, 0, sizeof module);
  module.id = "test:qc"; module.artifact_path = r.program.source; module.digest = r.program.digest; module.revision = "corpus";
  qa_compat_qc_executor_host host = {SENTINEL, host_checkpoint, host_restore};
  host_state = (HostState){4, 0, 0, module, "test:counter"};
  test_executor__X executor; memset(&executor, 0, sizeof executor);
  executor.m = vm; executor.module = module; executor.host = host;
  qa_compat_qc_raw_table_state table_state; qa_guest_layout public_layout = {"t:l", 32, 4, 4, NULL, 0};
  qa_raw_entity_table table = qa_compat_qc_create_raw_entity_table(vm, module, public_layout, SENTINEL, table_actor, &table_state);
  qa_raw_entity_view self_view = table.at_slot(table.ctx, 1), other_view = table.at_slot(table.ctx, 2);
  /* Raw table: two live views keep separate slots; count is live. */
  CHECK(self_view.current_actor(self_view.ctx).value.slot == 1);
  CHECK(other_view.current_actor(other_view.ctx).value.slot == 2);
  CHECK(!table.at_slot(table.ctx, 0).current_actor(table.at_slot(table.ctx, 0).ctx).has_value);
  CHECK(table.count == 3); CHECK(table.live_count(table.ctx) == 3);
  qa_compat_qc_entity_memory_set_count(&r.entities, 4); CHECK(table.live_count(table.ctx) == 4);
  CHECK(self_view.bytes.bytes == r.entities.bytes + self_view.address.byte_offset); CHECK(self_view.bytes.length == (size_t)r.entities.layout.stride_bytes);
  { qa_raw_entity_view back = table.from_pointer(table.ctx, self_view.address); CHECK(back.slot == 1);
     test_executor__P p = {&table, {&table_state, 0}};
    EXPECT_THROW(test_executor__pointer_body, &p, "entity address belongs to another address space");
    p.a = self_view.address; p.a.byte_offset = 1; EXPECT_THROW(test_executor__pointer_body, &p, "invalid entity reference 1"); }
  qa_compat_qc_entity_memory_set_count(&r.entities, 3);

  /* TS: checkpoint + restore round-trips machine words and the host hook. */
  vm->profiling[F_ADD] = 4294967296.0 + 3; /* count >>> 0 in the host state */
  qa_compat_qc_words_set_float(&vm->globals, 1, 1.75);
  const qa_guest_checkpoint *checkpoint = test_executor__capture(&executor);
  CHECK(host_state.checkpoints == 1);
  CHECK(checkpoint->kind == QA_GUEST_CHECKPOINT_QUAKEC); CHECK(strcmp(checkpoint->host_state.format, "quakec:host-v1") == 0);
  CHECK(checkpoint->entity_stride_bytes == (double)r.entities.layout.stride_bytes);
  { uint32_t magic; memcpy(&magic, checkpoint->host_state.bytes.bytes, 4); CHECK(magic == 0x31484351u); }
  host_state.counter = 9;
  qa_compat_qc_words_set_float(&vm->globals, 1, 123);
  vm->profiling[F_ADD] = 0;
  CHECK(qa_compat_qc_restore_checkpoint(vm, module, host, checkpoint));
  CHECK(host_state.restores == 1); CHECK(host_state.counter == 4);
  CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 1.75);
  CHECK(vm->profiling[F_ADD] == 3);
  /* Each damaged-checkpoint path. */
  qa_guest_checkpoint damaged = *checkpoint;
  test_executor__Rs rs = {&executor, &damaged};
  damaged.quakec_api.system_crc = 1; EXPECT_THROW(test_executor__restore_body, &rs, "incompatible QuakeC checkpoint");
  damaged = *checkpoint; damaged.function_index = 1; EXPECT_THROW(test_executor__restore_body, &rs, "checkpoint is not at an idle callback boundary");
  damaged = *checkpoint; damaged.entity_stride_bytes = 4; EXPECT_THROW(test_executor__restore_body, &rs, "checkpoint layout mismatch");
  damaged = *checkpoint; damaged.host_state.format = "other:v2"; EXPECT_THROW(test_executor__restore_body, &rs, "checkpoint layout mismatch");
  uint8_t *copy = malloc(checkpoint->host_state.bytes.length);
  memcpy(copy, checkpoint->host_state.bytes.bytes, checkpoint->host_state.bytes.length);
  damaged = *checkpoint; damaged.host_state.bytes.bytes = copy;
  copy[0] ^= 1; EXPECT_THROW(test_executor__restore_body, &rs, "unknown QuakeC host checkpoint"); copy[0] ^= 1;
  { size_t n = checkpoint->host_state.bytes.length;
    copy[n - 4] ^= 8; EXPECT_THROW(test_executor__restore_body, &rs, "checkpoint entity variable offset mismatch"); copy[n - 4] ^= 8;
    damaged.host_state.bytes.length = n - 1; EXPECT_THROW(test_executor__restore_body, &rs, "QuakeC host checkpoint");
    damaged.host_state.bytes.length = n;
    /* A count past the data raises the reader's error, not an allocation failure. */
    uint32_t huge = 0x7fffffff, saved; memcpy(&saved, copy + 8, 4); memcpy(copy + 8, &huge, 4);
    EXPECT_THROW(test_executor__restore_body, &rs, "QuakeC host checkpoint"); memcpy(copy + 8, &saved, 4); }
  /* Format without a separator. */
  host_state.format = "nocolon";
  checkpoint = test_executor__capture(&executor);
  damaged = *checkpoint;
  EXPECT_THROW(test_executor__restore_body, &rs, "invalid host state format");
  /* preflightQcCheckpoint runs the same decode without restoring: the host hook is not called. */
  { int restores = host_state.restores;
    EXPECT_THROW(test_executor__preflight_body, &rs, "invalid host state format");
    CHECK(host_state.restores == restores); }
  host_state.format = "test:counter";
  free(copy);
  /* A foreign host state and disagreeing profile are refused. */
  host_state.module.id = "other";
   test_executor__C cc = {&executor};
  EXPECT_THROW(test_executor__checkpoint_body, &cc, "host checkpoint belongs to another module");
  host_state.module = module;
  test_executor__release(&executor);
  /* Layout larger than the record. */
  {  test_executor__T t = {vm, module};
    EXPECT_THROW(test_executor__table_body, &t, "public layout exceeds QuakeC entity record"); }
  qa_compat_qc_raw_table_state_destroy(&table_state);
  rig_destroy(&r);
}

/* --- source call --- */
static int64_t reference_of(void *ctx, const qa_actor_id *actor) { CHECK_CTX(ctx); return actor ? actor->slot * 1000 : 0; }
typedef struct { qa_compat_qc_machine *m; int calls; bool fail; float seen_arg; float seen_global; const char *seen_text; } SourceState;
static SourceState source_state;
static double source_execute(void *ctx, int64_t argument_count) {
  CHECK_CTX(ctx); CHECK(argument_count == 2); source_state.calls++;
  qa_compat_qc_machine *m = source_state.m;
  source_state.seen_arg = (float)qa_compat_qc_words_float(&m->globals, 4);
  source_state.seen_global = (float)qa_compat_qc_words_float(&m->globals, G_RESULT);
  if (source_state.fail) qa_compat_qc_machine_fail(m, "source faulted");
  qa_compat_qc_machine_execute(m, F_ADD, argument_count);
  return qa_compat_qc_words_float(&m->globals, 1);
}
typedef struct { qa_compat_qc_program *p; qa_mod_source_call c; } test_source_call__Vd;
static void test_source_call__validate_none(void *c) { test_source_call__Vd *x = c; qa_compat_qc_validate_source_call(x->p, x->c, NULL, 0, "probe"); }
static void test_source_call__validate_some(void *c) { test_source_call__Vd *x = c; static const qa_mod_callback_input a[] = {"amount"}; qa_compat_qc_validate_source_call(x->p, x->c, a, 1, "probe"); }
typedef struct { qa_compat_qc_machine *m; qa_mod_source_call c; const qa_compat_qc_mod_input_entry *in; size_t n; qa_compat_qc_actor_reference r; qa_compat_qc_source_execute e; } test_source_call__W;
static void test_source_call__with_body(void *c) { test_source_call__W *x = c; (void)qa_compat_qc_with_source_call(x->m, x->c, x->in, x->n, x->r, x->e); }
typedef struct { qa_compat_qc_machine *m; qa_mod_runtime_value v; qa_compat_qc_actor_reference r; } test_source_call__Wr;
static void test_source_call__write_body(void *c) { test_source_call__Wr *x = c; qa_compat_qc_write_source_value(x->m, &x->m->globals, 60, x->v, x->r); }
static void test_source_call(void) {
  Rig r; qa_compat_qc_machine_options o; machine_core(&r, &o, host_noop);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  source_state = (SourceState){vm, 0, false, 0, 0, NULL};
  /* TS value kinds. */
  qa_mod_callback_value input = {"input", "amount", 0, NULL, {0, 0, 0}};
  CHECK_STR(qa_compat_qc_source_value_type(input), "float");
  input.name = "self"; CHECK_STR(qa_compat_qc_source_value_type(input), "entity");
  input.name = "view-angles"; CHECK_STR(qa_compat_qc_source_value_type(input), "vector");
  input.name = "item"; CHECK_STR(qa_compat_qc_source_value_type(input), "string");
  qa_mod_callback_value literal = {"float", NULL, 2.5, NULL, {0, 0, 0}};
  CHECK_STR(qa_compat_qc_source_value_type(literal), "float");

  /* Validation messages in TS order. */
  qa_mod_callback_value args[2] = {{"input", "amount", 0, NULL, {0, 0, 0}}, {"float", NULL, 0.5, NULL, {0, 0, 0}}};
  qa_mod_named_value gs[1] = {{"result", {"float", NULL, 7, NULL, {0, 0, 0}}}};
  qa_mod_source_call call; memset(&call, 0, sizeof call);
  call.function = "add"; call.arguments = args; call.argument_count = 2; call.globals = gs; call.global_count = 1;
  static const qa_mod_callback_input available[] = {"amount"};
  qa_compat_qc_validate_source_call(&r.program, call, available, 1, "probe");
   test_source_call__Vd vd = {&r.program, call};
  EXPECT_THROW(test_source_call__validate_none, &vd, "Mod probe cannot read amount");
  vd.c.function = "nosuch"; EXPECT_THROW(test_source_call__validate_some, &vd, "missing function nosuch");
  vd.c = call; vd.c.function = "host"; EXPECT_THROW(test_source_call__validate_some, &vd, "Mod callback host has an incompatible source signature");
  vd.c = call; vd.c.argument_count = 1; EXPECT_THROW(test_source_call__validate_some, &vd, "Mod callback add has an incompatible source signature");
  { qa_mod_callback_value vector_arg[2] = {args[0], {"vector", NULL, 0, NULL, {0, 0, 0}}};
    vd.c = call; vd.c.arguments = vector_arg; EXPECT_THROW(test_source_call__validate_some, &vd, "Mod callback add has an incompatible source signature"); }
  { qa_mod_named_value dup[2] = {gs[0], gs[0]}; vd.c = call; vd.c.globals = dup; vd.c.global_count = 2;
    EXPECT_THROW(test_source_call__validate_some, &vd, "Mod callback global result is duplicated or has an incompatible type"); }
  { qa_mod_named_value wrong[1] = {{"rstr", {"float", NULL, 7, NULL, {0, 0, 0}}}}; vd.c = call; vd.c.globals = wrong;
    EXPECT_THROW(test_source_call__validate_some, &vd, "Mod callback global rstr is duplicated or has an incompatible type"); }

  /* Staging is per invocation. */
  qa_compat_qc_mod_input_entry inputs[1]; memset(inputs, 0, sizeof inputs);
  inputs[0].name = "amount"; inputs[0].value.kind = "float"; inputs[0].value.number_value = 1.25;
  qa_compat_qc_actor_reference ref = {SENTINEL, reference_of};
  qa_compat_qc_source_execute exec = {SENTINEL, source_execute};
  qa_compat_qc_words_set_float(&vm->globals, 4, 99); qa_compat_qc_words_set_float(&vm->globals, 7, 98); qa_compat_qc_words_set_float(&vm->globals, G_RESULT, 97);
  CHECK(qa_compat_qc_with_source_call(vm, call, inputs, 1, ref, exec) == 1.75);
  CHECK(source_state.seen_arg == 1.25f); CHECK(source_state.seen_global == 7.0f);
  CHECK(qa_compat_qc_words_float(&vm->globals, 4) == 99); CHECK(qa_compat_qc_words_float(&vm->globals, 7) == 98);
  /* Saved global restored over what the callee stored. */
  CHECK(qa_compat_qc_words_float(&vm->globals, G_RESULT) == 97);
  /* Faults restore too. */
  source_state.fail = true;
  test_source_call__W w = {vm, call, inputs, 1, ref, exec};
  EXPECT_THROW(test_source_call__with_body, &w, "source faulted");
  CHECK(qa_compat_qc_words_float(&vm->globals, 4) == 99); CHECK(qa_compat_qc_words_float(&vm->globals, G_RESULT) == 97);
  source_state.fail = false;
  w.n = 0; EXPECT_THROW(test_source_call__with_body, &w, "Gameplay callback add has no amount input");

  /* Value writes. */
  qa_mod_runtime_value value; memset(&value, 0, sizeof value);
  value.kind = "string"; value.string_value = "text";
  qa_compat_qc_write_source_value(vm, &vm->globals, 60, value, ref);
  CHECK_STR(qa_compat_qc_strings_get(&vm->strings, qa_compat_qc_words_int(&vm->globals, 60)), "text");
  /* Rewriting the same value reuses its buffer. */
  int32_t first = qa_compat_qc_words_int(&vm->globals, 60);
  qa_compat_qc_write_source_value(vm, &vm->globals, 61, value, ref);
  CHECK(qa_compat_qc_words_int(&vm->globals, 61) == first);
  /* A string longer than the 128 default takes its own capacity. */
  char big[300]; memset(big, 'x', 299); big[299] = 0; value.string_value = big;
  qa_compat_qc_write_source_value(vm, &vm->globals, 62, value, ref);
  CHECK_STR(qa_compat_qc_strings_get(&vm->strings, qa_compat_qc_words_int(&vm->globals, 62)), big);
  value.kind = "actor"; qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 3; value.actor_value = &actor;
  qa_compat_qc_write_source_value(vm, &vm->globals, 63, value, ref); CHECK(qa_compat_qc_words_int(&vm->globals, 63) == 3000);
  value.actor_value = NULL; qa_compat_qc_write_source_value(vm, &vm->globals, 63, value, ref); CHECK(qa_compat_qc_words_int(&vm->globals, 63) == 0);
  test_source_call__Wr wr = {vm, value, ref};
  wr.v.kind = "float"; wr.v.number_value = 1e39; EXPECT_THROW(test_source_call__write_body, &wr, "Mod callback number exceeds binary32 range");
  wr.v.kind = "vector"; wr.v.vector_value = (qa_vec3){0, 1e39, 0}; EXPECT_THROW(test_source_call__write_body, &wr, "Mod callback vector exceeds binary32 range");
  rig_destroy(&r);
}

/* --- compatibility, weapon capability --- */
typedef struct { const char *j; size_t n; } test_compatibility__J;
static void test_compatibility__read_body(void *ctx) { test_compatibility__J *x = ctx; qa_compat_qc_quakec_compatibility q; qa_compat_qc_read_quakec_compatibility((const uint8_t *)x->j, x->n, "sha256:x", &q); }
static void test_compatibility(void) {
  qa_compat_qc_quakec_compatibility c;
  CHECK(qa_compat_qc_read_quakec_compatibility(NULL, 0, "sha256:x", &c));
  CHECK_STR(c.message_dialect, "known-retail"); CHECK(!c.has_teams && !c.has_combat && !c.has_weapon_stage && c.pickup_caller_count == 0);
  qa_compat_qc_quakec_compatibility_destroy(&c);
  const char *json = "{\"version\":1,\"artifactDigest\":\"sha256:x\",\"messageDialect\":\"quake-1-re-ts-private\",\"pickupCallers\":[]}";
  CHECK(qa_compat_qc_read_quakec_compatibility((const uint8_t *)json, strlen(json), "sha256:x", &c));
  CHECK_STR(c.message_dialect, "q1-rerelease-private"); /* canonicalized */
  qa_compat_qc_quakec_compatibility_destroy(&c);
  json = "{\"version\":1,\"artifactDigest\":\"sha256:x\",\"messageDialect\":\"q1-rerelease-private\"}";
  CHECK(qa_compat_qc_read_quakec_compatibility((const uint8_t *)json, strlen(json), "sha256:x", &c));
  CHECK_STR(c.message_dialect, "q1-rerelease-private");
  qa_compat_qc_quakec_compatibility_destroy(&c);
  test_compatibility__J j = {"{\"version\":2,\"artifactDigest\":\"sha256:x\"}", 0}; j.n = strlen(j.j); EXPECT_THROW(test_compatibility__read_body, &j, "quakec-compatibility.json.version: expected 1");
  j.j = "{\"version\":1,\"artifactDigest\":\"sha256:other\"}"; j.n = strlen(j.j); EXPECT_THROW(test_compatibility__read_body, &j, "quakec-compatibility.json.artifactDigest: expected sha256:x");
  j.j = "{\"version\":1,\"artifactDigest\":\"sha256:x\",\"messageDialect\":\"bogus\"}"; j.n = strlen(j.j); EXPECT_THROW(test_compatibility__read_body, &j, "quakec-compatibility.json.messageDialect: expected known-retail or q1-rerelease-private or quake-1-re-ts-private");
  j.j = "{\"version\":1,\"artifactDigest\":\"sha256:x\",\"pickupCallers\":{}}"; j.n = strlen(j.j); EXPECT_THROW(test_compatibility__read_body, &j, "quakec-compatibility.json.pickupCallers: expected an array");
  j.j = "{not json"; j.n = strlen(j.j); EXPECT_THROW(test_compatibility__read_body, &j, ""); /* parser wording is the engine's own */
  /* TextDecoder fatal: a bad byte is refused before parsing. */
  j.j = "{\"version\":1,\"artifactDigest\":\"\xff\"}"; j.n = strlen(j.j); EXPECT_THROW(test_compatibility__read_body, &j, "");
}
static qa_compat_qc_definition *def_named(qa_compat_qc_definition *defs, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (strcmp(defs[i].name, name) == 0) return &defs[i];
  return NULL;
}
static void test_weapon_capability(void) {
  /* Synthetic: every required field and global, then one rewrite at a time. */
  static const Def g[] = {{"self", 4, 28, false}, {"other", 4, 29, false}, {"time", 2, 30, false}, {"trace_plane_dist", 2, 31, false}};
  static const Def f[] = {
    {"origin", 3, 1, false}, {"velocity", 3, 4, false}, {"angles", 3, 7, false}, {"mins", 3, 10, false}, {"maxs", 3, 13, false}, {"v_angle", 3, 16, false},
    {"health", 2, 19, false}, {"solid", 2, 20, false}, {"nextthink", 2, 21, false}, {"classname", 1, 22, false}, {"netname", 1, 23, false}, {"think", 6, 24, false},
    {"model", 1, 25, false}, {"modelindex", 2, 26, false}, {"chain", 4, 27, false},
  };
  static const Fn fns[] = {{0, 0, 0, "", 0, {0}}};
  static const St st[] = {{QA_QC_OP_DONE, 0, 0, 0}};
  size_t length; uint8_t *bytes = build(5927, st, 1, g, 4, f, 15, fns, 1, NULL, 0, 40, 64, &length);
  qa_compat_qc_program p; CHECK(qa_compat_qc_load_program(bytes, length, NULL, "w.dat", &p)); free(bytes);
  CHECK(qa_compat_qc_weapon_behavior_capability_error(&p) == NULL);
  struct { qa_compat_qc_definition *defs; size_t n; const char *name; const char *wrong; const char *message; } cases[] = {
    {p.fields, p.field_count, "think", "float", "QuakeC trajectory adapter requires entity field think of type function"},
    {p.fields, p.field_count, "origin", "float", "QuakeC trajectory adapter requires entity field origin of type vector"},
    {p.globals, p.global_count, "other", "float", "QuakeC trajectory adapter requires global other of type entity"},
    {p.fields, p.field_count, "chain", "float", "QuakeC trajectory adapter requires declared entity field chain to have type entity"},
    {p.globals, p.global_count, "time", "string", "QuakeC trajectory adapter requires declared global time to have type float"},
    {p.globals, p.global_count, "trace_plane_dist", "string", "QuakeC trajectory adapter requires declared global trace_plane_dist to have type float"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    qa_compat_qc_definition *d = def_named(cases[i].defs, cases[i].n, cases[i].name);
    const char *type = d->type; d->type = cases[i].wrong;
    char *message = qa_compat_qc_weapon_behavior_capability_error(&p);
    CHECK_STR(message, cases[i].message); free(message);
    d->type = type;
  }
  /* A required definition that is absent. */
  qa_compat_qc_definition *self = def_named(p.globals, p.global_count, "self"); self->name[0] = 'x';
  char *message = qa_compat_qc_weapon_behavior_capability_error(&p);
  CHECK_STR(message, "QuakeC trajectory adapter requires global self of type entity"); free(message);
  self->name[0] = 's';
  qa_compat_qc_program_destroy(&p);
}

/* --- message effects, routing, QW presentation --- */
typedef struct { qa_temporary_entity e; const qa_actor_id *a; bool qw; } test_message_effects__T;
static void test_message_effects__event_body(void *c) { test_message_effects__T *x = c; (void)qa_compat_qc_quake_temporary_event(x->e, x->a, x->qw); }
static void test_message_effects(void) {
  qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 4;
  qa_temporary_entity e; memset(&e, 0, sizeof e);
  e.kind = QA_TE_EXPLOSION_COLORS; e.origin = (qa_vec3){1, 2, 3}; e.color_start = 5; e.color_length = 6;
  qa_q1_event r = qa_compat_qc_quake_temporary_event(e, NULL, false);
  CHECK_STR(r.kind, "colored-explosion"); CHECK(r.origin.z == 3 && r.color_start == 5 && r.color_length == 6);
  memset(&e, 0, sizeof e); e.kind = QA_TE_BEAM; e.entity = 7; e.start = (qa_vec3){1, 0, 0}; e.end = (qa_vec3){0, 1, 0};
  static const struct { double type; const char *style; } beams[] = {{5, "lightning1"}, {6, "lightning2"}, {9, "lightning3"}, {13, "grapple"}};
  for (size_t i = 0; i < 4; i++) { e.type = beams[i].type; r = qa_compat_qc_quake_temporary_event(e, &actor, false);
    CHECK_STR(r.kind, "beam"); CHECK_STR(r.beam_style, beams[i].style); CHECK(r.has_actor && r.actor.slot == 4); CHECK(r.start.x == 1 && r.end.y == 1); }
  test_message_effects__T t = {e, NULL, false}; t.e.type = 5; EXPECT_THROW(test_message_effects__event_body, &t, "QC beam entity 7 had no owned actor when written");
  t.a = &actor; t.e.type = 3; EXPECT_THROW(test_message_effects__event_body, &t, "Unsupported QC beam 3");
  t.e.type = 13; t.qw = true; EXPECT_THROW(test_message_effects__event_body, &t, "Unsupported QC beam 13"); /* QW opcode 13 is blood */
  memset(&e, 0, sizeof e); e.kind = QA_TE_POINT; e.origin = (qa_vec3){4, 5, 6}; e.count = 3;
  /* QW blood/gunshot opcodes become particles. */
  static const struct { double type, color, count; } particles[] = {{2, 0, 60}, {12, 73, 60}, {13, 225, 50}};
  for (size_t i = 0; i < 3; i++) { e.type = particles[i].type; r = qa_compat_qc_quake_temporary_event(e, NULL, true);
    CHECK_STR(r.kind, "particles"); CHECK(r.color == particles[i].color && r.count == particles[i].count); CHECK(r.origin.y == 5); }
  static const struct { double type; const char *name; } points[] = {{0, "spike"}, {1, "superspike"}, {2, "gunshot"}, {3, "explosion"}, {4, "tar-explosion"}, {7, "wizard-spike"}, {8, "knight-spike"}, {10, "lava-splash"}, {11, "teleport"}};
  for (size_t i = 0; i < 9; i++) { e.type = points[i].type; r = qa_compat_qc_quake_temporary_event(e, NULL, false);
    CHECK_STR(r.kind, "effect"); CHECK_STR(r.effect, points[i].name); CHECK(!r.has_actor); CHECK(r.amount == 3); CHECK(r.origin.x == 4); }
  t.e = e; t.a = NULL; t.qw = false; t.e.type = 12; EXPECT_THROW(test_message_effects__event_body, &t, "Unsupported QC point effect 12");
}
typedef struct { int queries; bool visible; const char *kind; qa_vec3 last_point; double from, to; } VisState;
static VisState vis;
static double v_leaf(void *ctx, qa_vec3 point) { CHECK_CTX(ctx); vis.last_point = point; return point.x; }
static double v_cluster(void *ctx, double leaf) { CHECK_CTX(ctx); return leaf + 100; }
static bool v_visible(void *ctx, double from, double to, const char *kind) { CHECK_CTX(ctx); vis.queries++; vis.from = from; vis.to = to; vis.kind = kind; return vis.visible; }
static qa_q1_client_visibility_scene v_scene(void *ctx) { CHECK_CTX(ctx); qa_q1_client_visibility_scene s; memset(&s, 0, sizeof s); s.ctx = SENTINEL; s.point_leaf = v_leaf; s.leaf_cluster = v_cluster; s.cluster_visible = v_visible; return s; }
static qa_vec3 origin_value;
static qa_vec3 v_origin(void *ctx) { CHECK_CTX(ctx); return origin_value; }
static void test_message_routing(void) {
  qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 7; actor.generation = 2;
  qa_actor_id other = actor; other.slot = 8;
  qa_compat_qc_message_origin origin = {SENTINEL, v_origin}; qa_compat_qc_message_visibility visibility = {SENTINEL, v_scene};
  qa_compat_qc_message_destination d; memset(&d, 0, sizeof d);
  d.kind = QA_QC_DEST_CLIENT; d.actor = actor;
  CHECK(qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  CHECK(!qa_compat_qc_receives_quakeworld_message(other, d, origin, visibility));
  d.kind = QA_QC_DEST_SIGNON; CHECK(!qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  d.kind = QA_QC_DEST_BROADCAST; CHECK(qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  /* TS: QW multicast keeps original near-PHS and visibility distinction. */
  memset(&vis, 0, sizeof vis);
  d.kind = QA_QC_DEST_MULTICAST; d.origin = (qa_vec3){0, 0, 0};
  d.visibility = "all"; origin_value = (qa_vec3){1e9, 0, 0}; CHECK(qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  CHECK(vis.queries == 0);
  d.visibility = "phs"; origin_value = (qa_vec3){1024, 0, 0}; CHECK(qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  CHECK(vis.queries == 0);
  d.visibility = "pvs"; origin_value = (qa_vec3){1, 0, 0}; CHECK(!qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  d.visibility = "phs"; origin_value = (qa_vec3){1025, 0, 0}; CHECK(!qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
  CHECK(vis.queries == 2); CHECK_STR(vis.kind, "phs");
  /* The cluster query runs destination first, then client. */
  CHECK(vis.from == 100 && vis.to == 1125);
  vis.visible = true; CHECK(qa_compat_qc_receives_quakeworld_message(actor, d, origin, visibility));
}
typedef struct {
  int messages, emits, presents, receives; qa_network_event last_event; qa_q1_event last_emit; bool emit_has_recipient; qa_actor_id emit_recipient;
  qa_netquake_message last_present; bool present_has_target, present_has_actor; qa_actor_id present_actor;
  qa_netquake_message last_receive; size_t receive_count; bool receive_has_target, receive_has_actor; qa_actor_id receive_actor;
  bool muzzle_found; int order[8]; int order_count;
} PresentState;
static PresentState present;
static void p_message(void *ctx, qa_network_event event, const qa_actor_id *target) { CHECK_CTX(ctx); (void)target; present.messages++; present.last_event = event; }
static void p_emit(void *ctx, qa_q1_event event, const qa_actor_id *recipient) { CHECK_CTX(ctx); present.emits++; present.last_emit = event; present.emit_has_recipient = recipient != NULL; if (recipient) present.emit_recipient = *recipient; }
static void p_present(void *ctx, qa_netquake_message message, const qa_actor_id *target, qa_compat_qc_local_messages state, const qa_actor_id *fixed_actor) {
  CHECK_CTX(ctx); CHECK(state.ctx == SENTINEL); present.presents++; present.last_present = message; present.present_has_target = target != NULL;
  present.present_has_actor = fixed_actor != NULL; if (fixed_actor) present.present_actor = *fixed_actor;
  present.order[present.order_count++] = 2;
}
static bool p_muzzle(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles) { CHECK_CTX(ctx); (void)actor; *out_origin = (qa_vec3){1, 2, 3}; *out_angles = (qa_vec3){4, 5, 6}; return present.muzzle_found; }
static void p_receive(void *ctx, const qa_netquake_message *messages, size_t count, const qa_actor_id *target, const qa_actor_id *actor) {
  CHECK_CTX(ctx); present.receives++; present.last_receive = messages[0]; present.receive_count = count; present.receive_has_target = target != NULL; present.receive_has_actor = actor != NULL; if (actor) present.receive_actor = *actor;
  present.order[present.order_count++] = 1;
}
typedef struct { qa_compat_qc_routed_message e; qa_compat_qc_local_messages s; qa_compat_qc_local_message_host h; } test_qw_presentation__Pr;
static void test_qw_presentation__present_body(void *c) { test_qw_presentation__Pr *x = c; qa_compat_qc_present_quakeworld_message(x->e, NULL, x->s, x->h); }
static void test_qw_presentation(void) {
  qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 3;
  qa_actor_id others[2]; memset(others, 0, sizeof others); others[0].slot = 10; others[1].slot = 11;
  qa_compat_qc_local_messages state = {SENTINEL, p_receive};
  qa_compat_qc_local_message_host host; memset(&host, 0, sizeof host);
  host.ctx = SENTINEL; host.recipients = others; host.recipient_count = 2; host.map = "maps/e1m1.bsp"; host.seconds = 10;
  host.message = p_message; host.emit = p_emit; host.present_local = p_present; host.muzzle = p_muzzle;
  qa_compat_qc_routed_message entry; memset(&entry, 0, sizeof entry);
#define RESET() memset(&present, 0, sizeof present)
  /* print: the level and text survive; no state update. */
  RESET(); entry.message.kind = "print"; entry.message.level = 3; entry.message.text = "hello";
  qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
  CHECK(present.messages == 1 && present.receives == 0); CHECK(present.last_event.kind == QA_NETWORK_EVENT_PRINT); CHECK(present.last_event.level == 3); CHECK_STR(present.last_event.text, "hello");
  /* kick: no effect at all. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "kick";
  qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
  CHECK(present.messages == 0 && present.emits == 0 && present.receives == 0 && present.presents == 0);
  /* temporary-entity with QW opcode semantics. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "temporary-entity"; entry.message.effect.kind = QA_TE_POINT; entry.message.effect.type = 12; entry.message.effect.count = 2;
  qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
  CHECK(present.emits == 1 && !present.emit_has_recipient); CHECK_STR(present.last_emit.kind, "particles"); CHECK(present.last_emit.count == 40 && present.last_emit.color == 73);
  /* intermission to target; to every recipient. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "intermission"; entry.message.origin = (qa_vec3){1, 2, 3}; entry.message.angles = (qa_vec3){4, 5, 6};
  qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
  CHECK(present.emits == 1 && present.emit_recipient.slot == 3); CHECK_STR(present.last_emit.kind, "intermission");
  CHECK(present.last_emit.origin.x == 1 && present.last_emit.angles.z == 6); CHECK_STR(present.last_emit.map, "maps/e1m1.bsp"); CHECK(present.last_emit.exit_after == 15 && present.last_emit.track == 0);
  CHECK(present.receives == 1); CHECK_STR(present.last_receive.kind, "intermission"); CHECK(present.receive_has_target);
  RESET(); qa_compat_qc_present_quakeworld_message(entry, NULL, state, host);
  CHECK(present.emits == 2 && present.emit_recipient.slot == 11); CHECK(!present.receive_has_target);
  /* cd-track: loops itself; state first, then presentation. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "cd-track"; entry.message.track = 4;
  qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
  CHECK(present.last_present.track == 4 && present.last_present.loop_track == 4); CHECK(present.order[0] == 1 && present.order[1] == 2);
  /* sound and stop-sound use the written actor; every field arrives. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "sound"; entry.has_actor = true; entry.actor = actor;
  entry.message.entity = 21; entry.message.channel = 2; entry.message.index = 9; entry.message.origin = (qa_vec3){7, 8, 9}; entry.message.volume = 200; entry.message.attenuation = 1.5;
  qa_compat_qc_present_quakeworld_message(entry, NULL, state, host);
  CHECK(present.receives == 0 && present.presents == 1); CHECK_STR(present.last_present.kind, "sound");
  CHECK(present.present_has_actor && present.present_actor.slot == 3);
  CHECK(present.last_present.entity == 21 && present.last_present.channel == 2 && present.last_present.index == 9 && present.last_present.volume == 200 && present.last_present.attenuation == 1.5 && present.last_present.origin.y == 8);
  test_qw_presentation__Pr pr = {entry, state, host}; pr.e.has_actor = false;
  EXPECT_THROW(test_qw_presentation__present_body, &pr, "QW sound had no owned actor when written");
  pr.e.message.kind = "stop-sound"; EXPECT_THROW(test_qw_presentation__present_body, &pr, "QW stop-sound had no owned actor when written");
  pr.e.message.kind = "muzzle-flash"; EXPECT_THROW(test_qw_presentation__present_body, &pr, "QW muzzle flash had no owned actor when written");
  /* Muzzle flash: skipped when the host has no muzzle. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "muzzle-flash"; entry.has_actor = true; entry.actor = actor;
  qa_compat_qc_present_quakeworld_message(entry, NULL, state, host); CHECK(present.emits == 0);
  RESET(); present.muzzle_found = true; qa_compat_qc_present_quakeworld_message(entry, NULL, state, host);
  CHECK(present.emits == 1 && !present.emit_has_recipient); CHECK_STR(present.last_emit.effect, "muzzleflash");
  CHECK(present.last_emit.origin.z == 3 && present.last_emit.amount == 1 && present.last_emit.has_muzzle && present.last_emit.muzzle_angles.x == 4 && present.last_emit.has_actor);
  /* set-view: state only, carrying the owning actor. */
  RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = "set-view"; entry.message.entity = 6; entry.has_actor = true; entry.actor = actor;
  qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
  CHECK(present.receives == 1 && present.presents == 0); CHECK(present.last_receive.entity == 6); CHECK(present.receive_has_actor && present.receive_actor.slot == 3);
  /* Pass-through services keep every payload field (state and source included). */
  static const char *const passthrough[] = {"nop", "stat", "stufftext", "center-print", "finale", "set-angle", "light-style", "static", "static-sound", "damage", "pause", "killed-monster", "found-secret", "sell-screen"};
  for (size_t i = 0; i < 14; i++) {
    RESET(); memset(&entry, 0, sizeof entry); entry.message.kind = passthrough[i];
    entry.message.index = 5; entry.message.value = 6; entry.message.text = "t"; entry.message.str_value = "abc"; entry.message.angles = (qa_vec3){1, 2, 3}; entry.message.origin = (qa_vec3){4, 5, 6};
    entry.message.source = (qa_vec3){7, 8, 9}; entry.message.armor = 10; entry.message.blood = 11; entry.message.paused = true; entry.message.volume = 12; entry.message.attenuation = 13;
    entry.message.state.alpha = 0.5;
    qa_compat_qc_present_quakeworld_message(entry, &actor, state, host);
    CHECK(present.receives == 1 && present.presents == 1); CHECK(present.order[0] == 1 && present.order[1] == 2); CHECK(!present.present_has_actor);
    qa_netquake_message *m = &present.last_receive; CHECK_STR(m->kind, passthrough[i]);
    CHECK(m->index == 5 && m->value == 6 && m->paused && m->armor == 10 && m->blood == 11 && m->volume == 12 && m->attenuation == 13);
    CHECK_STR(m->text, "t"); CHECK_STR(m->str_value, "abc");
    CHECK(m->angles.y == 2 && m->origin.z == 6 && m->source.x == 7); CHECK(m->state.alpha == 0.5);
    CHECK(present.last_present.source.z == 9 && present.last_present.state.alpha == 0.5);
  }
  pr.e.has_actor = false; pr.e.message.kind = "server-data";
  EXPECT_THROW(test_qw_presentation__present_body, &pr, "QW component service server-data requires a destination owner binding");
#undef RESET
}

/* --- retail programs --- */
static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)n); if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
  fclose(f); *out_length = (size_t)n; return data;
}
/* archive.findEntries("progs.dat").at(-1) */
static uint8_t *pak_progs(const char *relative, size_t *out_length) {
  char path[512]; snprintf(path, sizeof path, "%s/qfiles/Quake/%s", getenv("HOME") ? getenv("HOME") : "", relative);
  size_t length; uint8_t *pak = read_file(path, &length); if (!pak) return NULL;
  int32_t dir, size; memcpy(&dir, pak + 4, 4); memcpy(&size, pak + 8, 4);
  uint8_t *out = NULL;
  for (int32_t i = 0; i < size / 64; i++) {
    const uint8_t *e = pak + dir + i * 64;
    if (strncmp((const char *)e, "progs.dat", 56) == 0) {
      int32_t at, n; memcpy(&at, e + 56, 4); memcpy(&n, e + 60, 4);
      free(out); out = malloc((size_t)n); memcpy(out, pak + at, (size_t)n); *out_length = (size_t)n;
    }
  }
  free(pak); return out;
}
static bool retail(qa_compat_qc_program *p, const char *relative) {
  size_t length; uint8_t *bytes = pak_progs(relative, &length);
  if (!bytes) { printf("skipped: no %s under ~/qfiles/Quake\n", relative); return false; }
  CHECK(qa_compat_qc_load_program(bytes, length, NULL, relative, p));
  free(bytes); return true;
}
static int64_t fn_index(const qa_compat_qc_program *p, const char *name) { return qa_compat_qc_program_function_named(p, name)->index; }
static void retail_rig(Rig *r, const char *relative, qa_compat_qc_machine_options *o, const qa_compat_qc_host_binding *host, size_t host_count, int64_t capacity, int64_t count, bool rerelease_layout) {
  CHECK(retail(&r->program, relative));
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services);
  services.kind = rerelease_layout ? "rerelease" : "netquake"; services.host = host; services.host_count = host_count;
  memset(&r->registry, 0, sizeof r->registry); CHECK(qa_compat_qc_create_builtins(services, &r->registry));
  rig_options(r, o, capacity, count);
}
#define HAVE_RETAIL(path) (access_ok(path))
static bool access_ok(const char *relative) {
  size_t n; uint8_t *b = pak_progs(relative, &n); bool ok = b != NULL; free(b);
  if (!ok) printf("skipped retail checks: no %s under ~/qfiles/Quake\n", relative);
  return ok;
}

/* TS: selected function boundaries enforce one synchronous execution and share the interpreter budget. */
typedef struct { int mode; qa_compat_qc_function_execution retained; } RetailBoundary;
static RetailBoundary retail_boundary;
static void again_body(void *c) { qa_compat_qc_function_execution *e = c; e->run(e->ctx, NULL); }
static void retail_run(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  CHECK_CTX(ctx); (void)call; qa_caught_error caught;
  retail_boundary.retained = execute;
  switch (retail_boundary.mode) {
    case 0: break; /* omitted */
    case 1: execute.run(execute.ctx, NULL); CHECK(!qa_try(again_body, &execute, &caught)); break; /* repeated, swallowed */
    case 2: execute.run(execute.ctx, NULL); break; /* once */
    case 3: CHECK(!qa_try(again_body, &execute, &caught)); break; /* failing, swallowed */
    default: execute.run(execute.ctx, NULL); break;
  }
}
static void boundary_machine(Rig *r, int64_t selected, qa_compat_qc_function_boundary *b, int64_t *selected_store, int mode, int64_t limit) {
  qa_compat_qc_machine_options o;
  retail_rig(r, "Id1/PAK0.PAK", &o, NULL, 0, 8, 3, false);
  *selected_store = selected >= 0 ? selected : fn_index(&r->program, "SUB_Null");
  *b = (qa_compat_qc_function_boundary){SENTINEL, selected_store, 1, retail_run};
  o.function_boundary = b; o.statement_limit = limit;
  retail_boundary.mode = mode;
  CHECK(qa_compat_qc_machine_init(&r->vm, o));
}
static void snapshot_ok(qa_compat_qc_machine *vm) { qa_compat_qc_machine_snapshot s; CHECK(qa_compat_qc_machine_get_snapshot(vm, &s)); qa_compat_qc_machine_snapshot_destroy(&s); }
static void test_retail_boundaries(void) {
  Rig r; qa_compat_qc_function_boundary b; int64_t selected;
  boundary_machine(&r, -1, &b, &selected, 0, 100000);
  int64_t idle = fn_index(&r.program, "SUB_Null");
  Run run = {&r.vm, idle, 0};
  EXPECT_THROW(run_body, &run, "omitted source execution"); snapshot_ok(&r.vm);
  rig_destroy(&r);

  boundary_machine(&r, -1, &b, &selected, 1, 100000); run.m = &r.vm;
  EXPECT_THROW(run_body, &run, "once inside its boundary");
  CHECK(r.vm.profiling[idle] == 1); snapshot_ok(&r.vm);
  rig_destroy(&r);

  boundary_machine(&r, -1, &b, &selected, 2, 100000); run.m = &r.vm;
  qa_compat_qc_machine_execute(&r.vm, idle, 0);
  EXPECT_THROW(again_body, &retail_boundary.retained, "once inside its boundary");
  CHECK(r.vm.profiling[idle] == 1);
  rig_destroy(&r);

  /* failing: execute() throws "unbound builtin setorigin", swallowed; the boundary rethrows it. */
  qa_compat_qc_machine_options o;
  retail_rig(&r, "Id1/PAK0.PAK", &o, NULL, 0, 8, 3, false);
  int64_t done = fn_index(&r.program, "SUB_CalcMoveDone"); selected = done;
  b = (qa_compat_qc_function_boundary){SENTINEL, &selected, 1, retail_run}; o.function_boundary = &b; retail_boundary.mode = 3;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_words_set_int(&r.vm.globals, qa_compat_qc_machine_global_offset(&r.vm, "self"), (double)qa_compat_qc_entity_memory_reference(&r.entities, 1));
  run.m = &r.vm; run.fn = done;
  EXPECT_THROW(run_body, &run, "unbound builtin setorigin");
  CHECK(r.vm.frame_count == 0); CHECK(r.vm.builtin_depth == 0); CHECK(r.vm.boundary_depth == 0); snapshot_ok(&r.vm);
  rig_destroy(&r);

  /* The budget is shared with the boundary: identical to a control with no boundary. */
  Rig limited, control; qa_compat_qc_machine_options co;
  retail_rig(&limited, "Id1/PAK0.PAK", &o, NULL, 0, 8, 3, false);
  selected = fn_index(&limited.program, "T_Damage");
  b = (qa_compat_qc_function_boundary){SENTINEL, &selected, 1, retail_run}; o.function_boundary = &b; o.statement_limit = 8; retail_boundary.mode = 4;
  CHECK(qa_compat_qc_machine_init(&limited.vm, o));
  retail_rig(&control, "Id1/PAK0.PAK", &co, NULL, 0, 8, 3, false);
  co.statement_limit = 8; CHECK(qa_compat_qc_machine_init(&control.vm, co));
  Rig *both[2] = {&limited, &control};
  for (int i = 0; i < 2; i++) {
    qa_compat_qc_machine *vm = &both[i]->vm;
    qa_compat_qc_words_set_int(&vm->globals, qa_compat_qc_machine_global_offset(vm, "self"), (double)qa_compat_qc_entity_memory_reference(&both[i]->entities, 1));
    qa_compat_qc_words_set_int(&vm->globals, qa_compat_qc_machine_global_offset(vm, "other"), (double)qa_compat_qc_entity_memory_reference(&both[i]->entities, 2));
    Run dr = {vm, fn_index(&both[i]->program, "door_blocked"), 0};
    EXPECT_THROW(run_body, &dr, "runaway loop");
    CHECK(vm->frame_count == 0); snapshot_ok(vm);
  }
  CHECK(memcmp(limited.vm.profiling, control.vm.profiling, limited.program.function_count * sizeof(double)) == 0);
  CHECK(memcmp(limited.entities.bytes, control.entities.bytes, limited.entities.byte_length) == 0);
  rig_destroy(&limited); rig_destroy(&control);
}

/* TS: post-boundary source reentry preserves the callee return consumed by its real caller. */
typedef struct { qa_compat_qc_machine *vm; int64_t movedir; qa_compat_qc_entity_memory *entities; } Reentry;
static Reentry reentry;
static void reentry_run(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  CHECK_CTX(ctx); (void)call;
  execute.run(execute.ctx, NULL);
  CHECK(qa_compat_qc_words_float(&reentry.vm->globals, 1) == 180);
  qa_vec3 angles = {0, -1, 0};
  qa_compat_qc_words_set_vector(qa_compat_qc_entity_memory_at(reentry.entities, 1), qa_compat_qc_machine_field_offset(reentry.vm, "angles"), angles);
  qa_compat_qc_machine_execute(reentry.vm, reentry.movedir, 0);
}
static void test_retail_reentry(void) {
  Rig r; qa_compat_qc_machine_options o; retail_rig(&r, "Id1/PAK0.PAK", &o, NULL, 0, 8, 2, false);
  int64_t anglemod = fn_index(&r.program, "anglemod");
  qa_compat_qc_function_boundary b = {SENTINEL, &anglemod, 1, reentry_run}; o.function_boundary = &b;
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  reentry = (Reentry){vm, fn_index(&r.program, "SetMovedir"), &r.entities};
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_at(&r.entities, 1);
  qa_compat_qc_words_set_int(&vm->globals, qa_compat_qc_machine_global_offset(vm, "self"), (double)qa_compat_qc_entity_memory_reference(&r.entities, 1));
  qa_vec3 angles = {0, 180, 0}; qa_compat_qc_words_set_vector(self, qa_compat_qc_machine_field_offset(vm, "angles"), angles);
  qa_compat_qc_words_set_float(self, qa_compat_qc_machine_field_offset(vm, "ideal_yaw"), 0);
  qa_compat_qc_machine_execute(vm, fn_index(&r.program, "FacingIdeal"), 0);
  CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 0);
  CHECK(qa_compat_qc_machine_get_argc(vm) == 1);
  qa_vec3 movedir = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "movedir")); CHECK(movedir.x == 0 && movedir.y == 0 && movedir.z == 1);
  qa_vec3 now = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "angles")); CHECK(now.x == 0 && now.y == 0 && now.z == 0);
  CHECK(vm->profiling[anglemod] > 0); CHECK(vm->profiling[reentry.movedir] > 0);
  CHECK(vm->frame_count == 0); snapshot_ok(vm);
  rig_destroy(&r);
}

/* TS: loads classic, all rerelease programs and the independent QuakeWorld layout. */
static void test_retail_load(void) {
  qa_compat_qc_program p;
  CHECK(retail(&p, "Id1/PAK0.PAK")); CHECK(p.api.system_crc == 5927);
  CHECK(strncmp(p.digest, "sha256:", 7) == 0 || strlen(p.digest) >= 64);
  qa_compat_qc_program_destroy(&p);
  static const char *const names[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3", "ctf"};
  for (size_t i = 0; i < 7; i++) {
    char path[64]; snprintf(path, sizeof path, "rerelease/%s/pak0.pak", names[i]);
    if (!retail(&p, path)) continue;
    CHECK(p.function_count > 500);
    size_t named = 0; for (size_t j = 0; j < p.function_count; j++) if (p.functions[j].named_builtin) named++;
    CHECK(named == (strcmp(names[i], "ctf") == 0 ? 21u : 18u));
    CHECK_STR(qa_compat_qc_program_field_named(&p, "think")->type, "function");
    if (strcmp(names[i], "mg3") == 0) CHECK(qa_compat_qc_program_global_named(&p, "poses[0]")->native_type == 10);
    qa_compat_qc_program_destroy(&p);
  }
  char path[512]; snprintf(path, sizeof path, "%s/qfiles/Quake/qw/qwprogs.dat", getenv("HOME") ? getenv("HOME") : "");
  size_t length; uint8_t *bytes = read_file(path, &length);
  if (bytes) {
    CHECK(qa_compat_qc_load_program(bytes, length, NULL, NULL, &p)); free(bytes);
    CHECK(p.api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD);
    CHECK(qa_compat_qc_program_function_named_map(&p, "SpectatorThink") != NULL);
    qa_compat_qc_program_destroy(&p);
  }
}

/* TS: executes source SUB_CalcMove and preserves source pointer/think state. */
static void test_retail_calcmove(void) {
  Rig r; qa_compat_qc_machine_options o; retail_rig(&r, "rerelease/id1/pak0.pak", &o, NULL, 0, 16, 3, true);
  /* machineFor uses a 96-byte prefix regardless of API. */
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  qa_compat_qc_words_set_int(&vm->globals, qa_compat_qc_machine_global_offset(vm, "self"), (double)qa_compat_qc_entity_memory_reference(&r.entities, 1));
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_at(&r.entities, 1);
  qa_compat_qc_words_set_vector(self, qa_compat_qc_machine_field_offset(vm, "origin"), (qa_vec3){10, 20, 30});
  qa_compat_qc_words_set_float(self, qa_compat_qc_machine_field_offset(vm, "ltime"), 5);
  qa_compat_qc_words_set_vector(&vm->globals, 4, (qa_vec3){110, 20, 30});
  qa_compat_qc_words_set_float(&vm->globals, 7, 50);
  qa_compat_qc_words_set_int(&vm->globals, 10, (double)fn_index(&r.program, "SUB_Null"));
  qa_compat_qc_machine_execute(vm, fn_index(&r.program, "SUB_CalcMove"), 3);
  qa_vec3 v = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "velocity")); CHECK(v.x == 50 && v.y == 0 && v.z == 0);
  CHECK(qa_compat_qc_words_float(self, qa_compat_qc_machine_field_offset(vm, "nextthink")) == 7);
  CHECK(qa_compat_qc_words_int(self, qa_compat_qc_machine_field_offset(vm, "think")) == fn_index(&r.program, "SUB_CalcMoveDone"));
  qa_vec3 d = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "finaldest")); CHECK(d.x == 110 && d.y == 20 && d.z == 30);
  CHECK(vm->frame_count == 0);
  rig_destroy(&r);
}

/* TS: a builtin re-enters QC immediately, then its caller continues. */
static double observed_z; static int observed_count;
static void setorigin_reenters(void *ctx, qa_compat_qc_machine *vm) {
  CHECK_CTX(ctx);
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_from_reference(vm->entities, qa_compat_qc_machine_arg_int(vm, 0));
  qa_compat_qc_words_set_vector(self, qa_compat_qc_machine_field_offset(vm, "origin"), qa_compat_qc_machine_arg_vector(vm, 1));
  qa_compat_qc_words_set_vector(self, qa_compat_qc_machine_field_offset(vm, "angles"), (qa_vec3){0, -1, 0});
  qa_compat_qc_machine_execute(vm, fn_index(vm->program, "SetMovedir"), 0);
  observed_z = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "movedir")).z; observed_count++;
}
static void test_retail_reentrant_builtin(void) {
  qa_compat_qc_host_binding host[1] = {{"setorigin", {SENTINEL, setorigin_reenters}}};
  Rig r; qa_compat_qc_machine_options o; retail_rig(&r, "rerelease/id1/pak0.pak", &o, host, 1, 16, 3, true);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm; observed_count = 0;
  qa_compat_qc_words_set_int(&vm->globals, qa_compat_qc_machine_global_offset(vm, "self"), (double)qa_compat_qc_entity_memory_reference(&r.entities, 1));
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_at(&r.entities, 1);
  qa_compat_qc_words_set_vector(self, qa_compat_qc_machine_field_offset(vm, "finaldest"), (qa_vec3){12, 13, 14});
  qa_compat_qc_words_set_int(self, qa_compat_qc_machine_field_offset(vm, "think1"), (double)fn_index(&r.program, "SUB_Null"));
  qa_compat_qc_machine_execute(vm, fn_index(&r.program, "SUB_CalcMoveDone"), 0);
  CHECK(observed_count == 1 && observed_z == 1);
  qa_vec3 origin = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "origin")); CHECK(origin.x == 12 && origin.y == 13 && origin.z == 14);
  CHECK(qa_compat_qc_words_float(self, qa_compat_qc_machine_field_offset(vm, "nextthink")) == -1);
  CHECK(vm->frame_count == 0);
  rig_destroy(&r);
}

/* TS: raw snapshots retain private fields and strings; text fields keep source parsing. */
static void test_retail_snapshots(void) {
  Rig r; qa_compat_qc_machine_options o; retail_rig(&r, "rerelease/mg3/pak0.pak", &o, NULL, 0, 16, 3, true);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  qa_compat_qc_text_pair pairs[] = {{"angle", "90"}, {"message", "first\\nsecond"}, {"private_unknown", "keep"}};
  qa_compat_qc_entity_parse_result_compat parsed;
  CHECK(qa_compat_qc_apply_entity_pairs(vm, 1, pairs, 3, &parsed));
  CHECK(parsed.unknown.count == 1); CHECK_STR(parsed.unknown.items[0].key, "private_unknown"); CHECK_STR(parsed.unknown.items[0].value, "keep");
  qa_compat_qc_text_pairs_free(&parsed.unknown); qa_compat_qc_text_pairs_free(&parsed.retained);
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_at(&r.entities, 1);
  qa_vec3 a = qa_compat_qc_words_vector(self, qa_compat_qc_machine_field_offset(vm, "angles")); CHECK(a.x == 0 && a.y == 90 && a.z == 0);
  int32_t message = qa_compat_qc_words_int(self, qa_compat_qc_machine_field_offset(vm, "message"));
  CHECK_STR(qa_compat_qc_strings_get(&vm->strings, message), "first\nsecond");
  qa_compat_qc_words_set_int(self, r.program.entity_field_words - 1, -0x1234567);
  qa_compat_qc_machine_snapshot snapshot; CHECK(qa_compat_qc_machine_get_snapshot(vm, &snapshot));
  memset(self->bytes, 0, self->byte_length);
  qa_compat_qc_strings_allocate(&vm->strings, "later");
  CHECK(qa_compat_qc_machine_restore(vm, &snapshot));
  qa_compat_qc_machine_snapshot_destroy(&snapshot);
  CHECK(qa_compat_qc_words_int(self, r.program.entity_field_words - 1) == -0x1234567);
  CHECK_STR(qa_compat_qc_strings_get(&vm->strings, message), "first\nsecond");
  qa_compat_qc_text_pairs saved; bool found = false;
  CHECK(qa_compat_qc_save_entity_pairs(vm, 1, false, &saved));
  for (size_t i = 0; i < saved.count; i++) if (strcmp(saved.items[i].key, "angles") == 0) { found = true; CHECK_STR(saved.items[i].value, "0.000000 90.000000 0.000000"); }
  CHECK(found);
  qa_compat_qc_text_pairs_free(&saved);
  rig_destroy(&r);
}

/* TS: required host services fail by builtin name, without fallback success. */
static void test_retail_required(void) {
  Rig r; qa_compat_qc_machine_options o; retail_rig(&r, "rerelease/id1/pak0.pak", &o, NULL, 0, 16, 3, true);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  size_t n = qa_compat_qc_machine_missing_builtins(vm, NULL, 0);
  qa_compat_qc_missing_builtin *missing = calloc(n, sizeof *missing); qa_compat_qc_machine_missing_builtins(vm, missing, n);
  bool setmodel = false; for (size_t i = 0; i < n; i++) if (strcmp(missing[i].name, "setmodel") == 0) setmodel = true;
  CHECK(setmodel); free(missing);
  Run run = {vm, fn_index(&r.program, "setmodel"), 2}; EXPECT_THROW(run_body, &run, "unbound builtin setmodel");
  run.fn = fn_index(&r.program, "ex_bot_movetopoint"); run.argc = 3; EXPECT_THROW(run_body, &run, "unbound builtin ex_bot_movetopoint");
  rig_destroy(&r);
}

/* TS rerelease-builtins: the rerelease program selects its host and every ex_* binds by name. */
static void ex_print(void *ctx, qa_compat_qc_machine *vm) { char *s = qa_compat_qc_machine_var_string(vm, 0); snprintf(ctx, 64, "%s", s); free(s); }
static void ex_zero(void *ctx, qa_compat_qc_machine *vm) { CHECK_CTX(ctx); qa_compat_qc_machine_return_float(vm, 0); }
static void test_rerelease_builtins(void) {
  Rig r; qa_compat_qc_machine_options o;
  CHECK(retail(&r.program, "rerelease/id1/pak0.pak"));
  CHECK_STR(qa_compat_qc_host_kind_for_program(&r.program), "rerelease");
  size_t count = qa_compat_qc_host_requirements("rerelease", NULL, 0);
  qa_compat_qc_builtin_requirement *reqs = calloc(count, sizeof *reqs); qa_compat_qc_host_requirements("rerelease", reqs, count);
  qa_compat_qc_host_binding *host = calloc(count, sizeof *host);
  char printed[64] = {0};
  for (size_t i = 0; i < count; i++) { host[i].name = reqs[i].name; host[i].fn = (qa_compat_qc_builtin){SENTINEL, ex_zero}; if (strcmp(reqs[i].name, "ex_bprint") == 0) host[i].fn = (qa_compat_qc_builtin){printed, ex_print}; }
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services);
  services.kind = "rerelease"; services.host = host; services.host_count = count;
  memset(&r.registry, 0, sizeof r.registry); CHECK(qa_compat_qc_create_builtins(services, &r.registry));
  rig_options(&r, &o, 16, 1); CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  size_t n = qa_compat_qc_machine_missing_builtins(vm, NULL, 0);
  qa_compat_qc_missing_builtin *missing = calloc(n ? n : 1, sizeof *missing); qa_compat_qc_machine_missing_builtins(vm, missing, n);
  size_t missing_named = 0; for (size_t i = 0; i < n; i++) if (!missing[i].has_number) missing_named++;
  CHECK(missing_named == 0); free(missing);
  /* The marker string exists in the program strings. */
  int64_t marker = -1; for (size_t at = 1; at + 10 <= r.program.string_length; at++) if (memcmp(r.program.strings + at, "ex_bprint", 10) == 0) { marker = (int64_t)at; break; }
  CHECK(marker > 0);
  qa_compat_qc_words_set_int(&vm->globals, 4, (double)marker);
  qa_compat_qc_machine_execute(vm, fn_index(&r.program, "ex_bprint"), 1);
  CHECK_STR(printed, "ex_bprint");
  qa_compat_qc_machine_execute(vm, fn_index(&r.program, "ex_CheckPlayerEXFlags"), 1);
  CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 0);
  rig_destroy(&r);
  free(host); free(reqs);
  /* Requirement tables. */
  qa_compat_qc_builtin_requirement list[96];
  size_t nq = qa_compat_qc_host_requirements("netquake", list, 96);
  bool has48 = false, has79 = false; for (size_t i = 0; i < nq; i++) { if (list[i].number == 48) has48 = true; if (list[i].number == 79) has79 = true; }
  CHECK(has48 && !has79);
  size_t qw = qa_compat_qc_host_requirements("quakeworld", list, 96);
  has48 = false; has79 = false; bool has82 = false; for (size_t i = 0; i < qw; i++) { if (list[i].number == 48) has48 = true; if (list[i].number == 79) has79 = true; if (list[i].number == 82) has82 = true; }
  CHECK(!has48 && has79 && has82); CHECK(qw == nq + 2);
  CHECK(qa_compat_qc_host_requirements("rerelease", NULL, 0) == nq + 22);
}

/* --- builtin values: expected from the TS QcMachine under bun --- */
static const struct { uint32_t word; const char *ftos; uint32_t rint, floor, ceil, fabs; } kScalars[] = {
  {0x0u, "0", 0x80000000u, 0x00000000u, 0x00000000u, 0x00000000u},
  {0x80000000u, "0", 0x80000000u, 0x80000000u, 0x80000000u, 0x00000000u},
  {0x3f800000u, "1", 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u},
  {0xbf800000u, "-1", 0xbf800000u, 0xbf800000u, 0xbf800000u, 0x3f800000u},
  {0x3e800000u, "  0.3", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3e800000u},
  {0x3f400000u, "  0.8", 0x3f800000u, 0x00000000u, 0x3f800000u, 0x3f400000u},
  {0xbe800000u, " -0.3", 0x80000000u, 0xbf800000u, 0x80000000u, 0x3e800000u},
  {0x3fa00000u, "  1.3", 0x3f800000u, 0x3f800000u, 0x40000000u, 0x3fa00000u},
  {0x40200000u, "  2.5", 0x40400000u, 0x40000000u, 0x40400000u, 0x40200000u},
  {0x3d4ccccdu, "  0.1", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3d4ccccdu},
  {0x3e19999au, "  0.2", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3e19999au},
  {0xbd23d70au, " -0.0", 0x80000000u, 0xbf800000u, 0x80000000u, 0x3d23d70au},
  {0x3d23d70au, "  0.0", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3d23d70au},
  {0x3f866666u, "  1.0", 0x3f800000u, 0x3f800000u, 0x40000000u, 0x3f866666u},
  {0x42c7e666u, " 99.9", 0x42c80000u, 0x42c60000u, 0x42c80000u, 0x42c7e666u},
  {0x42c80000u, "100", 0x42c80000u, 0x42c80000u, 0x42c80000u, 0x42c80000u},
  {0x49742400u, "1000000", 0x49742400u, 0x49742400u, 0x49742400u, 0x49742400u},
  {0x4b64e1c0u, "15000000", 0x4b64e1c0u, 0x4b64e1c0u, 0x4b64e1c0u, 0x4b64e1c0u},
  {0x4ceb79a3u, "123456792", 0x4ceb79a3u, 0x4ceb79a3u, 0x4ceb79a3u, 0x4ceb79a3u},
  {0x60ad78ecu, "100000002004087730000", 0x60ad78ecu, 0x60ad78ecu, 0x60ad78ecu, 0x60ad78ecu},
  {0x6258d727u, "1.0000000200408773e+21", 0x6258d727u, 0x6258d727u, 0x6258d727u, 0x6258d727u},
  {0x62d8d727u, "2.0000000400817547e+21", 0x62d8d727u, 0x62d8d727u, 0x62d8d727u, 0x62d8d727u},
  {0xf2177617u, "-2.999999894026671e+30", 0xf2177617u, 0xf2177617u, 0xf2177617u, 0x72177617u},
  {0x7f7fc99eu, "3.3999999521443642e+38", 0x7f7fc99eu, 0x7f7fc99eu, 0x7f7fc99eu, 0x7f7fc99eu},
  {0x3eaaaaabu, "  0.3", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3eaaaaabu},
  {0x3727c5acu, "  0.0", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3727c5acu},
  {0x3dcccccdu, "  0.1", 0x00000000u, 0x00000000u, 0x3f800000u, 0x3dcccccdu},
  {0x420551ecu, " 33.3", 0x42040000u, 0x42040000u, 0x42080000u, 0x420551ecu},
  {0x4b800000u, "16777216", 0x4b800000u, 0x4b800000u, 0x4b800000u, 0x4b800000u},
  {0x7fc00000u, "  NaN", 0x7fc00000u, 0x7fc00000u, 0x7fc00000u, 0x7fc00000u},
  {0x7f800000u, "Infinity", 0x7f800000u, 0x7f800000u, 0x7f800000u, 0x7f800000u},
  {0xff800000u, "-Infinity", 0xff800000u, 0xff800000u, 0xff800000u, 0x7f800000u},
  {0x80000000u, "0", 0x80000000u, 0x80000000u, 0x80000000u, 0x00000000u},
  {0x1u, "  0.0", 0x00000000u, 0x00000000u, 0x3f800000u, 0x00000001u},
};
static const struct { double in[3]; const char *vtos; uint32_t vlen, yaw, angles[3], normal[3], vectors[9]; } kVectors[] = {
  {{0.0, 0.0, 0.0}, "'  0.0   0.0   0.0'", 0x00000000u, 0x00000000u, {0x43870000u,0x00000000u,0x00000000u}, {0x00000000u,0x00000000u,0x00000000u}, {0x3f800000u,0x00000000u,0x80000000u,0x00000000u,0xbf800000u,0x80000000u,0x00000000u,0x00000000u,0x3f800000u}},
  {{1.0, 0.0, 0.0}, "'  1.0   0.0   0.0'", 0x3f800000u, 0x00000000u, {0x00000000u,0x00000000u,0x00000000u}, {0x3f800000u,0x00000000u,0x00000000u}, {0x3f7ff605u,0x00000000u,0xbc8ef859u,0x00000000u,0xbf800000u,0x80000000u,0x3c8ef859u,0x00000000u,0x3f7ff605u}},
  {{0.0, 1.0, 0.0}, "'  0.0   1.0   0.0'", 0x3f800000u, 0x42b40000u, {0x00000000u,0x42b40000u,0x00000000u}, {0x00000000u,0x3f800000u,0x00000000u}, {0x3f7ff605u,0x3c8ef859u,0x80000000u,0x3c8ef859u,0xbf7ff605u,0x80000000u,0x00000000u,0x00000000u,0x3f800000u}},
  {{0.0, 0.0, 1.0}, "'  0.0   0.0   1.0'", 0x3f800000u, 0x00000000u, {0x42b40000u,0x00000000u,0x00000000u}, {0x00000000u,0x00000000u,0x3f800000u}, {0x3f800000u,0x00000000u,0x80000000u,0x00000000u,0xbf7ff605u,0xbc8ef859u,0x00000000u,0xbc8ef859u,0x3f7ff605u}},
  {{0.0, 0.0, -1.0}, "'  0.0   0.0  -1.0'", 0x3f800000u, 0x00000000u, {0x43870000u,0x00000000u,0x00000000u}, {0x00000000u,0x00000000u,0xbf800000u}, {0x3f800000u,0x00000000u,0x80000000u,0x00000000u,0xbf7ff605u,0x3c8ef859u,0x00000000u,0x3c8ef859u,0x3f7ff605u}},
  {{3.0, 4.0, 0.0}, "'  3.0   4.0   0.0'", 0x40a00000u, 0x42540000u, {0x00000000u,0x42540000u,0x00000000u}, {0x3f19999au,0x3f4ccccdu,0x00000000u}, {0x3f7f06c3u,0x3d8eaa5cu,0xbd565e3au,0x3d8edc7bu,0xbf7f605cu,0x80000000u,0x3d55d88cu,0x3b6f41c8u,0x3f7fa62fu}},
  {{3.0, 4.0, 12.0}, "'  3.0   4.0  12.0'", 0x41500000u, 0x42540000u, {0x42860000u,0x42540000u,0x00000000u}, {0x3e6c4ec5u,0x3e9d89d9u,0x3f6c4ec5u}, {0x3f7f06c3u,0x3d8eaa5cu,0xbd565e3au,0x3d6b048au,0xbf79fd7au,0xbe549c1bu,0x3d8649f8u,0xbe50b9ecu,0x3f7a1007u}},
  {{-1.0, -1.0, 0.0}, "' -1.0  -1.0   0.0'", 0x3fb504f3u, 0x43610000u, {0x00000000u,0x43610000u,0x00000000u}, {0xbf3504f3u,0xbf3504f3u,0x00000000u}, {0x3f7fec0au,0xbc8ef2c6u,0x3c8ef859u,0xbc8ef859u,0xbf7ff605u,0x80000000u,0xbc8ef2c6u,0x399fb0e7u,0x3f7ff605u}},
  {{-1.0, 1.0, 5.0}, "' -1.0   1.0   5.0'", 0x40a646e1u, 0x43070000u, {0x42940000u,0x43070000u,0x00000000u}, {0xbe4511a3u,0x3e4511a3u,0x3f76560cu}, {0x3f7fec0au,0x3c8ef2c6u,0x3c8ef859u,0x3c9ae285u,0xbf7efaefu,0xbdb277c1u,0xbc81f197u,0xbdb316d6u,0x3f7efcadu}},
  {{0.25, 0.75, -0.25}, "'  0.3   0.8  -0.3'", 0x3f544395u, 0x428e0000u, {0x43ab8000u,0x428e0000u,0x00000000u}, {0x3e9a5fb2u,0x3f678f8bu,0xbe9a5fb2u}, {0x3f7ff9c3u,0x3c567538u,0xbb8efa17u,0x3c56c511u,0xbf7ff9bfu,0x3b8ef9beu,0x3b8d1780u,0x3b90d60fu,0x3f7ffec0u}},
  {{100.5, -200.25, 33.75}, "'100.5 -200.3  33.8'", 0x436294fau, 0x43948000u, {0x41000000u,0x43948000u,0x00000000u}, {0x3ee318beu,0xbf623fd1u,0x3e188711u}, {0x3e2f133bu,0xbd812d5fu,0xbf7bb698u,0x3f4cdfbcu,0x3f174c1eu,0x3dcf5959u,0xbf132108u,0x3f4ddfecu,0xbe1b28ecu}},
  {{1e+20, 1e+20, 1.0}, "'100000002004087734272.0 100000002004087734272.0   1.0'", 0x60f553b3u, 0x42340000u, {0x00000000u,0x42340000u,0x00000000u}, {0x3f3504f3u,0x3f3504f3u,0x1e059196u}, {0x3da18cd4u,0xbe8a0307u,0xbf75b21cu,0x3f76dcd6u,0x3e878c18u,0x3ba09e2eu,0xbe816a33u,0x3f6d0670u,0xbe8fc72cu}},
  {{1.0, -1.0, 0.001}, "'  1.0  -1.0   0.0'", 0x3fb504f6u, 0x439d8000u, {0x00000000u,0x439d8000u,0x00000000u}, {0x3f3504f0u,0xbf3504f0u,0x3a395d20u}, {0x3f7fec0au,0xbc8ef2c6u,0xbc8ef859u,0xbc8ef8fdu,0xbf7ff605u,0xb79262f4u,0x3c8ef223u,0xb9a8d717u,0x3f7ff605u}},
  {{-0.0, 0.0, -5.0}, "'  0.0   0.0  -5.0'", 0x40a00000u, 0x00000000u, {0x43870000u,0x00000000u,0x00000000u}, {0x80000000u,0x00000000u,0xbf800000u}, {0x3f800000u,0x00000000u,0x00000000u,0x00000000u,0xbf7f069eu,0x3db27eb6u,0x80000000u,0x3db27eb6u,0x3f7f069eu}},
  {{45.0, 90.0, 30.0}, "' 45.0  90.0  30.0'", 0x42d20000u, 0x427c0000u, {0x41800000u,0x427c0000u,0x00000000u}, {0x3edb6db7u,0x3f5b6db7u,0x3e924925u}, {0x2447ad06u,0x3f3504f3u,0xbf3504f3u,0x3f5db3d7u,0xbeb504f3u,0xbeb504f3u,0x3f000000u,0x3f1cc470u,0x3f1cc470u}},
  {{-30.0, 720.0, 15.0}, "'-30.0 720.0  15.0'", 0x443431f9u, 0x42b80000u, {0x3f800000u,0x42b80000u,0x00000000u}, {0xbd2a7b56u,0x3f7fb901u,0x3caa7b56u}, {0x3f5db3d7u,0xa5f48d50u,0x3f000000u,0x3e0483eeu,0xbf7746eau,0xbe6585f8u,0xbef746eau,0xbe8483eeu,0x3f5625eeu}},
  {{359.5, 0.5, 180.0}, "'359.5   0.5 180.0'", 0x43c905d0u, 0x00000000u, {0x41d00000u,0x00000000u,0x00000000u}, {0x3f64e8d3u,0x3aa301a8u,0x3ee53a54u}, {0x3f7ffb02u,0x3c0ef859u,0x3c0ef9beu,0xbc0ef9beu,0x3f7ffd81u,0xa50d2fd2u,0x3c0ef859u,0x389fb405u,0xbf7ffd81u}},
};
static void test_builtin_values(void) {
  Rig r; qa_compat_qc_machine_options o; retail_rig(&r, "Id1/PAK0.PAK", &o, NULL, 0, 4, 2, false);
  CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  int64_t ftos = fn_index(&r.program, "ftos"), rint_f = fn_index(&r.program, "rint"), floor_f = fn_index(&r.program, "floor"), ceil_f = fn_index(&r.program, "ceil"), fabs_f = fn_index(&r.program, "fabs");
  for (size_t i = 0; i < sizeof kScalars / sizeof *kScalars; i++) {
    uint32_t word = kScalars[i].word;
    memcpy(vm->globals.bytes + 16, &word, 4); qa_compat_qc_machine_execute(vm, ftos, 1);
    CHECK_STR(qa_compat_qc_strings_get(&vm->strings, qa_compat_qc_words_int(&vm->globals, 1)), kScalars[i].ftos);
    const struct { int64_t fn; uint32_t expected; } ops[4] = {{rint_f, kScalars[i].rint}, {floor_f, kScalars[i].floor}, {ceil_f, kScalars[i].ceil}, {fabs_f, kScalars[i].fabs}};
    for (int k = 0; k < 4; k++) {
      memcpy(vm->globals.bytes + 16, &word, 4); qa_compat_qc_machine_execute(vm, ops[k].fn, 1);
      if (global_bits(vm, 1) != ops[k].expected) { checks++; failures++; fprintf(stderr, "FAIL scalar %08x op %d: %08x != %08x\n", word, k, global_bits(vm, 1), ops[k].expected); } else checks++;
    }
  }
  int64_t vtos = fn_index(&r.program, "vtos"), vlen = fn_index(&r.program, "vlen"), yaw = fn_index(&r.program, "vectoyaw"), angles = fn_index(&r.program, "vectoangles"), normalize = fn_index(&r.program, "normalize"), makevectors = fn_index(&r.program, "makevectors");
  int64_t fw = qa_compat_qc_machine_global_offset(vm, "v_forward"), rt = qa_compat_qc_machine_global_offset(vm, "v_right"), up = qa_compat_qc_machine_global_offset(vm, "v_up");
  for (size_t i = 0; i < sizeof kVectors / sizeof *kVectors; i++) {
    qa_vec3 in = {kVectors[i].in[0], kVectors[i].in[1], kVectors[i].in[2]};
#define SET() qa_compat_qc_words_set_vector(&vm->globals, 4, in)
#define BITS(word, expected) do { checks++; if (global_bits(vm, word) != (expected)) { failures++; fprintf(stderr, "FAIL vector %zu word %d: %08x != %08x\n", i, (int)(word), global_bits(vm, word), (expected)); } } while (0)
    SET(); qa_compat_qc_machine_execute(vm, vtos, 1); CHECK_STR(qa_compat_qc_strings_get(&vm->strings, qa_compat_qc_words_int(&vm->globals, 1)), kVectors[i].vtos);
    SET(); qa_compat_qc_machine_execute(vm, vlen, 1); BITS(1, kVectors[i].vlen);
    SET(); qa_compat_qc_machine_execute(vm, yaw, 1); BITS(1, kVectors[i].yaw);
    SET(); qa_compat_qc_machine_execute(vm, angles, 1); for (int c = 0; c < 3; c++) BITS(1 + c, kVectors[i].angles[c]);
    SET(); qa_compat_qc_machine_execute(vm, normalize, 1); for (int c = 0; c < 3; c++) BITS(1 + c, kVectors[i].normal[c]);
    SET(); qa_compat_qc_machine_execute(vm, makevectors, 1);
    for (int c = 0; c < 3; c++) { BITS(fw + c, kVectors[i].vectors[c]); BITS(rt + c, kVectors[i].vectors[3 + c]); BITS(up + c, kVectors[i].vectors[6 + c]); }
#undef SET
#undef BITS
  }
  rig_destroy(&r);

  /* Numbers 7, 18, 47, 81, 99 are conditional. */
  Rig s; CHECK(retail(&s.program, "Id1/PAK0.PAK"));
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services); services.kind = "netquake";
  memset(&s.registry, 0, sizeof s.registry); CHECK(qa_compat_qc_create_builtins(services, &s.registry));
  CHECK(!qa_compat_qc_registry_get_numbered(&s.registry, 7).fn); CHECK(!qa_compat_qc_registry_get_numbered(&s.registry, 18).fn); CHECK(!qa_compat_qc_registry_get_numbered(&s.registry, 47).fn); CHECK(!qa_compat_qc_registry_get_numbered(&s.registry, 81).fn);
  CHECK(qa_compat_qc_registry_get_numbered(&s.registry, 99).fn != NULL);
  qa_compat_qc_registry_destroy(&s.registry); qa_compat_qc_program_destroy(&s.program);
}

/* find, nextent, random, atof, checkextension with services through their own ctx. */
static int prepare_calls; static double random_value;
static bool is_free_slot(void *ctx, int64_t slot) { CHECK_CTX(ctx); return slot == 2; }
static void prepare_entities(void *ctx) { CHECK_CTX(ctx); prepare_calls++; }
static double random_next(void *ctx) { CHECK_CTX(ctx); return random_value; }
typedef struct { qa_compat_qc_builtin b; qa_compat_qc_machine *m; } test_service_builtins__F;
static void test_service_builtins__find_body(void *c) { test_service_builtins__F *x = c; x->b.fn(x->b.ctx, x->m); }
static void test_service_builtins(void) {
  Rig r; qa_compat_qc_machine_options o; load_core(&r.program);
  qa_random_source random; memset(&random, 0, sizeof random); random.ctx = SENTINEL; random.next_integer = random_next;
  static const char *const extensions[] = {"DP_HAS"};
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services);
  services.kind = "quakeworld"; services.ctx = SENTINEL; services.random = &random; services.is_free_entity = is_free_slot; services.prepare_entities = prepare_entities;
  services.extensions = extensions; services.extension_count = 1; services.has_extensions = true;
  memset(&r.registry, 0, sizeof r.registry); CHECK(qa_compat_qc_create_builtins(services, &r.registry));
  rig_options(&r, &o, 8, 5); CHECK(qa_compat_qc_machine_init(&r.vm, o));
  qa_compat_qc_machine *vm = &r.vm;
  /* The bound closures stay valid when copied into another registry: their state travels in ctx. */
  qa_compat_qc_builtin rnd = qa_compat_qc_registry_get_numbered(&r.registry, 7);
  random_value = 0x7fff; rnd.fn(rnd.ctx, vm); CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 1);
  random_value = 4294967295.0; rnd.fn(rnd.ctx, vm); CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 1); /* int32 wrap, & 0x7fff */
  random_value = 0x8000; rnd.fn(rnd.ctx, vm); CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 0);
  /* find: skips free slots and zero strings; prepare runs first. */
  qa_compat_qc_builtin find = qa_compat_qc_registry_get_numbered(&r.registry, 18);
  int64_t field = qa_compat_qc_program_field_named(&r.program, "classname")->offset;
  qa_compat_qc_words_set_int(qa_compat_qc_entity_memory_at(&r.entities, 2), field, STR_ABC); /* free */
  qa_compat_qc_words_set_int(qa_compat_qc_entity_memory_at(&r.entities, 3), field, STR_ABC);
  qa_compat_qc_words_set_int(&vm->globals, 4, 0); qa_compat_qc_words_set_int(&vm->globals, 7, (double)field); qa_compat_qc_words_set_int(&vm->globals, 10, STR_ABC);
  prepare_calls = 0; find.fn(find.ctx, vm); CHECK(prepare_calls == 1);
  CHECK(qa_compat_qc_words_int(&vm->globals, 1) == qa_compat_qc_entity_memory_reference(&r.entities, 3));
  qa_compat_qc_words_set_int(&vm->globals, 4, (double)qa_compat_qc_entity_memory_reference(&r.entities, 3));
  find.fn(find.ctx, vm); CHECK(qa_compat_qc_words_int(&vm->globals, 1) == 0);
  /* Non-string field: fails by name; the machine's strings stay usable. */
  qa_compat_qc_words_set_int(&vm->globals, 4, 0); qa_compat_qc_words_set_int(&vm->globals, 7, 4);
   test_service_builtins__F f = {find, vm};
  EXPECT_THROW(test_service_builtins__find_body, &f, "find requires a source string field");
  /* nextent. */
  qa_compat_qc_builtin nextent = qa_compat_qc_registry_get_numbered(&r.registry, 47);
  qa_compat_qc_words_set_int(&vm->globals, 4, (double)qa_compat_qc_entity_memory_reference(&r.entities, 1));
  nextent.fn(nextent.ctx, vm); CHECK(qa_compat_qc_words_int(&vm->globals, 1) == qa_compat_qc_entity_memory_reference(&r.entities, 3));
  qa_compat_qc_words_set_int(&vm->globals, 4, (double)qa_compat_qc_entity_memory_reference(&r.entities, 4));
  nextent.fn(nextent.ctx, vm); CHECK(qa_compat_qc_words_int(&vm->globals, 1) == 0);
  /* stof (81) only on QuakeWorld; checkextension (99). */
  qa_compat_qc_builtin stof = qa_compat_qc_registry_get_numbered(&r.registry, 81);
  qa_compat_qc_words_set_int(&vm->globals, 4, (double)qa_compat_qc_strings_allocate(&vm->strings, "2.5"));
  stof.fn(stof.ctx, vm); CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 2.5);
  qa_compat_qc_builtin ext = qa_compat_qc_registry_get_numbered(&r.registry, 99);
  qa_compat_qc_words_set_int(&vm->globals, 4, (double)qa_compat_qc_strings_allocate(&vm->strings, "DP_HAS"));
  ext.fn(ext.ctx, vm); CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 1);
  qa_compat_qc_words_set_int(&vm->globals, 4, STR_ABC); ext.fn(ext.ctx, vm); CHECK(qa_compat_qc_words_float(&vm->globals, 1) == 0);
  /* error (10) joins every argument into the fault. */
  qa_compat_qc_builtin err = qa_compat_qc_registry_get_numbered(&r.registry, 10);
  vm->argument_count = 2; qa_compat_qc_words_set_int(&vm->globals, 4, STR_ABC); qa_compat_qc_words_set_int(&vm->globals, 7, STR_ABD);
  f.b = err; EXPECT_THROW(test_service_builtins__find_body, &f, ": abcabd");
  /* traceon/traceoff. */
  qa_compat_qc_builtin on = qa_compat_qc_registry_get_numbered(&r.registry, 29), off = qa_compat_qc_registry_get_numbered(&r.registry, 30);
  on.fn(on.ctx, vm); CHECK(vm->trace_enabled); off.fn(off.ctx, vm); CHECK(!vm->trace_enabled);
  rig_destroy(&r);
}

/* --- interpreter differential: expected hashes from the TS machine under bun --- */
static uint32_t fnv;
static void mix_byte(uint32_t b) { fnv = (fnv ^ (b & 255)) * 0x01000193u; }
static void mix_bytes(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) mix_byte(p[i]); }
static void mix_int(uint32_t v) { for (int i = 0; i < 4; i++) mix_byte(v >> (i * 8)); }
/* The oracle engine's NaN sign depends on its JIT tier (boxed NaN is purified, unboxed is not),
 * so the hash folds the x86 default NaN into the canonical one; C always stores the canonical. */
static void mix_words(const uint8_t *p, size_t n) {
  for (size_t i = 0; i + 4 <= n; i += 4) { uint32_t w; memcpy(&w, p + i, 4); if (w == 0xffc00000u) w = 0x7fc00000u; mix_int(w); }
}
static void mix_text(const char *t) { for (; *t; t++) mix_byte((uint8_t)*t); mix_byte(0); }
static uint32_t fuzz_seed;
static uint32_t fuzz_next(void) { fuzz_seed ^= fuzz_seed << 13; fuzz_seed ^= fuzz_seed >> 17; fuzz_seed ^= fuzz_seed << 5; return fuzz_seed; }
static const double kFloats[20] = {0, 1, -1, 0.5, 2, 3.25, 100, -64, 1024, 0.1, 90, 180, 360, 8, 16, 255, 0.25, 0.75, 1e-3, 123456};
#define FUZZ_FLOAT(r) kFloats[(r) % 20]
typedef struct { const char *name; int64_t code; qa_compat_qc_entity_memory *entities; } FuzzHost;
static void fuzz_builtin(void *ctx, qa_compat_qc_machine *vm) {
  FuzzHost *h = ctx; uint32_t r = fuzz_next();
  if (strcmp(h->name, "spawn") == 0) { qa_compat_qc_machine_return_int(vm, (double)qa_compat_qc_entity_memory_reference(h->entities, 1 + (r % 5))); return; }
  if (strcmp(h->name, "checkclient") == 0 || strcmp(h->name, "findradius") == 0) { qa_compat_qc_machine_return_int(vm, (double)qa_compat_qc_entity_memory_reference(h->entities, r % 6)); return; }
  qa_compat_qc_machine_return_float(vm, kFloats[((uint64_t)h->code + r) % 20]);
}
static double fuzz_random(void *ctx) { CHECK_CTX(ctx); return (double)(fuzz_next() & 0x7fffffffu); }
static bool fuzz_free(void *ctx, int64_t slot) { CHECK_CTX(ctx); return slot == 4; }
typedef struct { qa_compat_qc_machine *m; int64_t slot; qa_compat_qc_text_pairs out; } fuzz_run__SaveCtx;
static void fuzz_run__save_entity(void *c) { fuzz_run__SaveCtx *x = c; qa_compat_qc_save_entity_pairs(x->m, x->slot, false, &x->out); }
static void fuzz_run__save_global(void *c) { fuzz_run__SaveCtx *x = c; qa_compat_qc_save_global_pairs(x->m, &x->out); }
static uint32_t fuzz_run(const uint8_t *bytes, size_t length, const char *kind, int64_t limit) {
  fnv = 0x811c9dc5u; fuzz_seed = 0x12345678u;
  qa_compat_qc_program program; CHECK(qa_compat_qc_load_program(bytes, length, NULL, NULL, &program));
  qa_compat_qc_entity_memory entities; CHECK(qa_compat_qc_entity_memory_init(&entities, qa_compat_qc_classic_entity_layout(&program), 8, 6));
  /* new Map: a later requirement with the same name replaces the closure, at every number. */
  qa_compat_qc_builtin_requirement reqs[96]; size_t req_n = qa_compat_qc_host_requirements(kind, reqs, 96);
  FuzzHost hosts[96]; qa_compat_qc_host_binding bindings[96]; size_t host_n = 0;
  for (size_t i = 0; i < req_n; i++) {
    int64_t code = (reqs[i].has_number ? reqs[i].number : 500) + (int64_t)strlen(reqs[i].name);
    size_t at = host_n; for (size_t j = 0; j < host_n; j++) if (strcmp(hosts[j].name, reqs[i].name) == 0) at = j;
    hosts[at] = (FuzzHost){reqs[i].name, code, &entities};
    if (at == host_n) host_n++;
  }
  for (size_t i = 0; i < host_n; i++) bindings[i] = (qa_compat_qc_host_binding){hosts[i].name, {&hosts[i], fuzz_builtin}};
  qa_random_source random; memset(&random, 0, sizeof random); random.ctx = SENTINEL; random.next_integer = fuzz_random;
  static const char *const extensions[] = {"DP_TEST"};
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services);
  services.kind = kind; services.ctx = SENTINEL; services.random = &random; services.is_free_entity = fuzz_free; services.host = bindings; services.host_count = host_n;
  services.extensions = extensions; services.extension_count = 1; services.has_extensions = true;
  qa_compat_qc_builtin_registry registry; memset(&registry, 0, sizeof registry); CHECK(qa_compat_qc_create_builtins(services, &registry));
  qa_compat_qc_machine_options o; memset(&o, 0, sizeof o);
  o.program = &program; o.entities = &entities; o.numeric = qa_create_numeric_operations(&qa_q1_donor_profile); o.builtins = &registry; o.ctx = SENTINEL; o.server_active = server_active;
  o.statement_limit = limit; o.stack_limit = 24;
  qa_compat_qc_machine vm; CHECK(qa_compat_qc_machine_init(&vm, o));
  /* Name-derived strings: TS f.name.slice(0, 16) */
  int64_t *offsets = malloc(program.function_count * sizeof *offsets); size_t offset_n = 0;
  for (size_t i = 0; i < program.function_count; i++) if (program.functions[i].name[0]) {
    char name[16], text[17]; snprintf(name, sizeof name, "s:%zu", offset_n % 7); snprintf(text, sizeof text, "%s", program.functions[i].name);
    offsets[offset_n++] = qa_compat_qc_strings_set_engine(&vm.strings, name, text, -1);
  }
  for (int64_t slot = 0; slot < 6; slot++) {
    qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(&entities, slot);
    for (size_t i = 0; i < program.field_count; i++) {
      const qa_compat_qc_definition *def = &program.fields[i]; uint32_t r = fuzz_next();
      if (strcmp(def->type, "float") == 0) qa_compat_qc_words_set_float(w, def->offset, FUZZ_FLOAT(r));
      else if (strcmp(def->type, "vector") == 0) for (int c = 0; c < 3; c++) { uint32_t v = fuzz_next(); qa_compat_qc_words_set_float(w, def->offset + c, FUZZ_FLOAT(v)); }
      else if (strcmp(def->type, "string") == 0) qa_compat_qc_words_set_int(w, def->offset, (double)offsets[r % offset_n]);
      else if (strcmp(def->type, "entity") == 0) qa_compat_qc_words_set_int(w, def->offset, (double)qa_compat_qc_entity_memory_reference(&entities, r % 6));
      else if (strcmp(def->type, "function") == 0) qa_compat_qc_words_set_int(w, def->offset, (double)(1 + r % (program.function_count - 1)));
      else if (strcmp(def->type, "field") == 0) qa_compat_qc_words_set_int(w, def->offset, (double)program.fields[r % program.field_count].offset);
    }
  }
  int64_t self_o = qa_compat_qc_machine_global_offset(&vm, "self"), other_o = qa_compat_qc_machine_global_offset(&vm, "other");
  for (size_t index = 1; index < program.function_count; index++) {
    const qa_compat_qc_function *fn = &program.functions[index];
    { uint32_t r = fuzz_next(); qa_compat_qc_words_set_int(&vm.globals, self_o, (double)qa_compat_qc_entity_memory_reference(&entities, 1 + r % 5)); }
    { uint32_t r = fuzz_next(); qa_compat_qc_words_set_int(&vm.globals, other_o, (double)qa_compat_qc_entity_memory_reference(&entities, r % 6)); }
    for (int p = 0; p < 8; p++) {
      uint32_t r = fuzz_next(); int64_t size = (size_t)p < fn->parameter_size_count ? fn->parameter_sizes[p] : 1; int64_t word = 4 + p * 3;
      if (size == 3) for (int c = 0; c < 3; c++) { uint32_t v = fuzz_next(); qa_compat_qc_words_set_float(&vm.globals, word + c, FUZZ_FLOAT(v)); }
      else switch (r % 4) {
        case 0: { uint32_t v = fuzz_next(); qa_compat_qc_words_set_float(&vm.globals, word, FUZZ_FLOAT(v)); break; }
        case 1: { uint32_t v = fuzz_next(); qa_compat_qc_words_set_int(&vm.globals, word, (double)qa_compat_qc_entity_memory_reference(&entities, v % 6)); break; }
        case 2: { uint32_t v = fuzz_next(); qa_compat_qc_words_set_int(&vm.globals, word, (double)offsets[v % offset_n]); break; }
        default: qa_compat_qc_words_set_int(&vm.globals, word, 0);
      }
    }
    Run run = {&vm, (int64_t)index, (int64_t)fn->parameter_size_count};
    qa_caught_error caught; caught.message[0] = 0;
    bool ok = qa_try(run_body, &run, &caught);
    mix_text(ok ? "" : caught.message);
    mix_int((uint32_t)vm.frame_count); mix_int((uint32_t)vm.statement); mix_int((uint32_t)vm.function_index); mix_int((uint32_t)vm.argument_count);
    mix_words(vm.globals.bytes, 512);
    if (getenv("QC_FUZZ_DUMP")) printf("%zu %s \"%s\" %x\n", index, fn->name, ok ? "" : caught.message, fnv);
    if (getenv("QC_FUZZ_WORDS") && (size_t)atoi(getenv("QC_FUZZ_WORDS")) == index) {
      printf("statement %lld\n", (long long)vm.statement);
      for (int w = 0; w < 128; w++) printf("w%d %08x\n", w, global_bits(&vm, w));
    }
    if (index % 16 == 0) {
      mix_words(vm.globals.bytes, vm.globals.byte_length); mix_words(entities.bytes, entities.byte_length);
      fuzz_run__SaveCtx sc = {&vm, (int64_t)(1 + index % 5), {NULL, 0}};
      if (qa_try(fuzz_run__save_entity, &sc, &caught)) { for (size_t i = 0; i < sc.out.count; i++) { mix_text(sc.out.items[i].key); mix_text(sc.out.items[i].value); } qa_compat_qc_text_pairs_free(&sc.out); } else mix_text(caught.message);
      sc.out = (qa_compat_qc_text_pairs){NULL, 0};
      if (qa_try(fuzz_run__save_global, &sc, &caught)) { for (size_t i = 0; i < sc.out.count; i++) { mix_text(sc.out.items[i].key); mix_text(sc.out.items[i].value); } qa_compat_qc_text_pairs_free(&sc.out); } else mix_text(caught.message);
      if (getenv("QC_FUZZ_DUMP")) printf(" snap %x\n", fnv);
      if (getenv("QC_FUZZ_WORDS") && (size_t)atoi(getenv("QC_FUZZ_WORDS")) == index) {
        for (size_t i = 0; i < vm.globals.byte_length / 4; i++) printf("g%zu %08x\n", i, global_bits(&vm, (int64_t)i));
        for (size_t i = 0; i < entities.byte_length / 4; i++) { uint32_t b; memcpy(&b, entities.bytes + i * 4, 4); printf("e%zu %08x\n", i, b); }
        sc.out = (qa_compat_qc_text_pairs){NULL, 0};
        if (qa_try(fuzz_run__save_entity, &sc, &caught)) { for (size_t i = 0; i < sc.out.count; i++) printf("p %s=%s\n", sc.out.items[i].key, sc.out.items[i].value); qa_compat_qc_text_pairs_free(&sc.out); } else printf("p! %s\n", caught.message);
        sc.out = (qa_compat_qc_text_pairs){NULL, 0};
        if (qa_try(fuzz_run__save_global, &sc, &caught)) { for (size_t i = 0; i < sc.out.count; i++) printf("q %s=%s\n", sc.out.items[i].key, sc.out.items[i].value); qa_compat_qc_text_pairs_free(&sc.out); } else printf("q! %s\n", caught.message);
      }
    }
    if (index % 64 == 0) {
      qa_compat_qc_machine_snapshot s; CHECK(qa_compat_qc_machine_get_snapshot(&vm, &s)); mix_bytes(s.strings, s.strings_length);
      CHECK(qa_compat_qc_machine_restore(&vm, &s)); qa_compat_qc_machine_snapshot_destroy(&s);
    }
  }
  for (size_t i = 0; i < program.function_count; i++) mix_int((uint32_t)vm.profiling[i]);
  qa_compat_qc_machine_snapshot s; CHECK(qa_compat_qc_machine_get_snapshot(&vm, &s)); mix_bytes(s.strings, s.strings_length); qa_compat_qc_machine_snapshot_destroy(&s);
  free(offsets);
  qa_compat_qc_machine_destroy(&vm); qa_compat_qc_registry_destroy(&registry); qa_compat_qc_entity_memory_destroy(&entities); qa_compat_qc_program_destroy(&program);
  return fnv;
}
static void test_interpreter_differential(void) {
  size_t length; uint8_t *bytes = pak_progs("Id1/PAK0.PAK", &length);
  if (bytes) { uint32_t h = fuzz_run(bytes, length, "netquake", 20000); checks++; if (h != 0x4118d5f4u) { failures++; fprintf(stderr, "FAIL id1 interpreter hash %x != 4118d5f4\n", h); } free(bytes); }
  char path[512]; snprintf(path, sizeof path, "%s/qfiles/Quake/qw/qwprogs.dat", getenv("HOME") ? getenv("HOME") : "");
  bytes = read_file(path, &length);
  if (bytes) { uint32_t h = fuzz_run(bytes, length, "quakeworld", 20000); checks++; if (h != 0xe33651e6u) { failures++; fprintf(stderr, "FAIL qw interpreter hash %x != e33651e6\n", h); } free(bytes); }
  bytes = pak_progs("rerelease/id1/pak0.pak", &length);
  if (bytes) { uint32_t h = fuzz_run(bytes, length, "rerelease", 20000); checks++; if (h != 0xd3461235u) { failures++; fprintf(stderr, "FAIL rerelease interpreter hash %x != d3461235\n", h); } free(bytes); }
}

int main(void) {
  test_program();
  test_memory();
  test_cancellation();
  test_nested_cancellation();
  test_swallowed_cancellation();
  test_machine_faults();
  test_opcodes();
  test_observers();
  test_state_unobserved();
  test_inline();
  test_save();
  test_profile();
  test_executor();
  test_source_call();
  test_service_builtins();
  test_compatibility();
  test_weapon_capability();
  test_message_effects();
  test_message_routing();
  test_qw_presentation();
  if (access_ok("Id1/PAK0.PAK")) {
    test_retail_boundaries();
    test_retail_reentry();
    test_retail_load();
    test_builtin_values();
    if (access_ok("rerelease/id1/pak0.pak")) {
      test_retail_calcmove();
      test_retail_reentrant_builtin();
      test_retail_required();
      test_rerelease_builtins();
    }
    if (access_ok("rerelease/mg3/pak0.pak")) test_retail_snapshots();
    test_interpreter_differential();
  }
  printf("test_qc_core_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
