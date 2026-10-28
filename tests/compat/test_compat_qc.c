/*
 * Port of key cases from the TS compat/qc test files (program/memory/
 * machine/save/message behavior) against a synthetic progs.dat built
 * to the loader format verified in program.ts. TypeScript throw
 * cases run in a child process and must abort. GPL-2.0-or-later.
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

#include "qa/compat/qc.h"
#include "qa/compat/qc/compatibility.h"
#include "qa/compat/qc/message_effects.h"
#include "qa/compat/qc/mod_objectives.h"
#include "qa/compat/qc/source_call.h"
#include "qa/compat/qc/weapon_behavior_profile.h"
#include "qa/core/numeric.h"

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

/* --- synthetic progs.dat builder --- */
typedef struct { uint8_t *data; size_t length, capacity; } Buf;
static void put(Buf *b, const void *p, size_t n) {
  if (b->length + n > b->capacity) { b->capacity = (b->length + n) * 2 + 64; b->data = realloc(b->data, b->capacity); }
  memcpy(b->data + b->length, p, n); b->length += n;
}
static void put_i32(Buf *b, int32_t v) { put(b, &v, 4); }
static void put_u16(Buf *b, uint16_t v) { put(b, &v, 2); }
static void patch_i32(Buf *b, size_t offset, int32_t v) { memcpy(b->data + offset, &v, 4); }

static int32_t string_offset(Buf *strings, const char *s) {
  /* find existing */
  for (size_t i = 0; i < strings->length; i++)
    if (strcmp((const char *)strings->data + i, s) == 0 && (i == 0 || strings->data[i - 1] == 0)) return (int32_t)i;
  int32_t offset = (int32_t)strings->length;
  put(strings, s, strlen(s) + 1);
  return offset;
}
typedef struct { const char *name; uint16_t type; uint16_t offset; bool save; } DefSpec;
typedef struct { int32_t first, parm_start, locals; const char *name; uint8_t parms[8]; int32_t nparms; } FnSpec;

static const DefSpec kGlobals[] = {
  {"self", 4, 28, false}, {"other", 4, 29, false}, {"time", 2, 30, false},
  {"result", 2, 31, true}, {"vec", 3, 32, false}, {"msg", 1, 35, true}, {"fnref", 6, 36, false},
};
static const DefSpec kFields[] = {
  {"classname", 1, 0, false}, {"origin", 3, 1, false}, {"health", 2, 4, false},
  {"angles", 3, 5, false}, {"model", 1, 8, false}, {"think", 6, 9, false},
  {"nextthink", 2, 10, false}, {"frame", 2, 11, false}, {"solid", 2, 12, false},
  {"flags", 2, 13, false}, {"owner", 4, 14, false}, {"takedamage", 2, 15, false},
  {"armorvalue", 2, 16, false}, {"armortype", 2, 17, false}, {"invincible_finished", 2, 18, false},
  {"a_b", 2, 19, false}, {"velocity", 3, 20, false}, {"mins", 3, 23, false}, {"maxs", 3, 26, false},
  {"v_angle", 3, 29, false}, {"netname", 1, 32, false},
};
enum { kFieldWords = 40, kGlobalWords = 64 };

static uint8_t *build_program(int32_t version, int32_t crc, size_t *out_length) {
  Buf strings = {0}; put(&strings, "", 1);
  Buf out = {0};
  put_i32(&out, version); put_i32(&out, crc);
  size_t section_slots[6];
  for (int i = 0; i < 6; i++) { section_slots[i] = out.length; put_i32(&out, 0); put_i32(&out, 0); }
  put_i32(&out, kFieldWords);
  /* statements: 0 Done; 1 AddF 40,41->31; 2 Return 31; 3 Goto 0 (self) */
  patch_i32(&out, section_slots[0], (int32_t)out.length); patch_i32(&out, section_slots[0] + 4, 4);
  static const uint16_t stmts[4][4] = {{0,0,0,0},{6,40,41,31},{43,31,0,0},{61,0,0,0}};
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) put_u16(&out, stmts[i][j]);
  patch_i32(&out, section_slots[1], (int32_t)out.length); patch_i32(&out, section_slots[1] + 4, (int32_t)(sizeof kGlobals / sizeof *kGlobals));
  for (size_t i = 0; i < sizeof kGlobals / sizeof *kGlobals; i++) {
    put_u16(&out, (uint16_t)(kGlobals[i].type | (kGlobals[i].save ? 0x8000 : 0)));
    put_u16(&out, kGlobals[i].offset); put_i32(&out, string_offset(&strings, kGlobals[i].name));
  }
  patch_i32(&out, section_slots[2], (int32_t)out.length); patch_i32(&out, section_slots[2] + 4, (int32_t)(sizeof kFields / sizeof *kFields));
  for (size_t i = 0; i < sizeof kFields / sizeof *kFields; i++) {
    put_u16(&out, kFields[i].type); put_u16(&out, kFields[i].offset); put_i32(&out, string_offset(&strings, kFields[i].name));
  }
  static const FnSpec fns[] = {
    {0, 0, 0, "", {0}, 0},
    {1, 28, 0, "add", {0}, 0},
    {3, 28, 0, "spin", {0}, 0},
    {-9, 0, 0, "normalize", {3}, 1},
    {-42, 0, 0, "missing", {0}, 0},
  };
  patch_i32(&out, section_slots[3], (int32_t)out.length); patch_i32(&out, section_slots[3] + 4, (int32_t)(sizeof fns / sizeof *fns));
  for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
    put_i32(&out, fns[i].first); put_i32(&out, fns[i].parm_start); put_i32(&out, fns[i].locals);
    put_i32(&out, 0); put_i32(&out, string_offset(&strings, fns[i].name)); put_i32(&out, string_offset(&strings, "test.qc"));
    put_i32(&out, fns[i].nparms); put(&out, fns[i].parms, 8);
  }
  patch_i32(&out, section_slots[4], (int32_t)out.length); patch_i32(&out, section_slots[4] + 4, (int32_t)strings.length);
  put(&out, strings.data, strings.length);
  patch_i32(&out, section_slots[5], (int32_t)out.length); patch_i32(&out, section_slots[5] + 4, kGlobalWords);
  for (int i = 0; i < kGlobalWords; i++) put_i32(&out, 0);
  free(strings.data);
  *out_length = out.length;
  return out.data;
}

static bool load_test_program(qa_compat_qc_program *program) {
  size_t length = 0; uint8_t *bytes = build_program(6, 5927, &length);
  bool ok = qa_compat_qc_load_program(bytes, length, NULL, "test.dat", program);
  free(bytes); return ok;
}
static bool server_active_true(void *ctx) { (void)ctx; return true; }
static bool make_machine(qa_compat_qc_program *program, qa_compat_qc_entity_memory *entities,
  qa_compat_qc_builtin_registry *registry, qa_compat_qc_machine *machine, int64_t statement_limit) {
  qa_compat_qc_entity_layout layout = qa_compat_qc_classic_entity_layout(program);
  if (!qa_compat_qc_entity_memory_init(entities, layout, 4, 2)) return false;
  qa_compat_qc_machine_options options;
  memset(&options, 0, sizeof options);
  options.program = program; options.entities = entities;
  options.numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  options.builtins = registry; options.server_active = server_active_true;
  options.statement_limit = statement_limit;
  return qa_compat_qc_machine_init(machine, options);
}

static void test_program(void) {
  qa_compat_qc_program program;
  CHECK(load_test_program(&program));
  CHECK(program.api.kind == QA_QUAKEC_API_Q1_NETQUAKE);
  CHECK(program.entity_field_words == kFieldWords);
  CHECK(program.function_count == 5);
  const qa_compat_qc_function *add = qa_compat_qc_program_function_named(&program, "add");
  CHECK(add->index == 1 && add->first_statement == 1);
  const qa_compat_qc_definition *health = qa_compat_qc_program_field_named(&program, "health");
  CHECK(health && health->offset == 4 && strcmp(health->type, "float") == 0);
  CHECK(qa_compat_qc_signed_branch(0x7fff) == 32767);
  CHECK(qa_compat_qc_signed_branch(0xffff) == -1);
  CHECK(qa_compat_qc_signed_branch(0x8000) == -32768);
  char *text = qa_compat_qc_byte_string(program.strings, program.string_length, 1);
  CHECK(text != NULL); free(text);
  /* classic layout: netquake prefix 96 */
  qa_compat_qc_entity_layout layout = qa_compat_qc_classic_entity_layout(&program);
  CHECK(layout.variables_offset_bytes == 96 && layout.stride_bytes == 96 + kFieldWords * 4);
  /* search order */
  const char *order[2];
  CHECK(qa_compat_qc_program_search_order("quakeworld", order, 2) == 2 && strcmp(order[0], "qwprogs.dat") == 0);
  CHECK(qa_compat_qc_program_search_order("netquake", order, 2) == 1 && strcmp(order[0], "progs.dat") == 0);
  CHECK_ABORTS(qa_compat_qc_program_function_named(&program, "nosuch"));
  CHECK_ABORTS(qa_compat_qc_byte_string(program.strings, program.string_length, (int64_t)program.string_length));
  qa_compat_qc_program_destroy(&program);
  size_t length = 0;
  uint8_t *bad = build_program(5, 5927, &length);
  CHECK_ABORTS(qa_compat_qc_program p2; qa_compat_qc_load_program(bad, length, NULL, "bad.dat", &p2));
  free(bad);
  bad = build_program(6, 1234, &length);
  CHECK_ABORTS(qa_compat_qc_program p2; qa_compat_qc_load_program(bad, length, NULL, "bad.dat", &p2));
  free(bad);
}

static void test_memory(void) {
  uint8_t bytes[16] = {0};
  qa_compat_qc_words words;
  qa_compat_qc_words_init(&words, bytes, sizeof bytes);
  qa_compat_qc_words_set_float(&words, 1, 2.5);
  CHECK(qa_compat_qc_words_float(&words, 1) == 2.5);
  qa_compat_qc_words_set_int(&words, 2, -7);
  CHECK(qa_compat_qc_words_int(&words, 2) == -7);
  CHECK_ABORTS(qa_compat_qc_words w2; qa_compat_qc_words_init(&w2, bytes, 6));
  CHECK_ABORTS(qa_compat_qc_words_float(&words, 4));

  qa_compat_qc_entity_layout layout = {224, 96, 32};
  qa_compat_qc_entity_memory memory;
  CHECK(qa_compat_qc_entity_memory_init(&memory, layout, 4, 2));
  CHECK(qa_compat_qc_entity_memory_reference(&memory, 1) == 224);
  CHECK(qa_compat_qc_entity_memory_slot(&memory, 224) == 1);
  CHECK(qa_compat_qc_entity_memory_pointer(&memory, 224, 3) == 224 + 96 + 12);
  CHECK_ABORTS(qa_compat_qc_entity_memory_slot(&memory, 100));
  CHECK_ABORTS(qa_compat_qc_entity_memory_reference(&memory, 2));
  qa_compat_qc_entity_memory_destroy(&memory);
  CHECK_ABORTS(qa_compat_qc_entity_memory bad; qa_compat_qc_entity_memory_init(&bad, layout, 0, 1));

  static const uint8_t prog_strings[] = "\0hello\0";
  qa_compat_qc_strings strings;
  CHECK(qa_compat_qc_strings_init(&strings, prog_strings, sizeof prog_strings - 1, true));
  int64_t allocated = qa_compat_qc_strings_allocate(&strings, "world");
  CHECK(strcmp(qa_compat_qc_strings_get(&strings, allocated), "world") == 0);
  int64_t engine = qa_compat_qc_strings_set_engine(&strings, "tmp", "abc", -1);
  CHECK(engine == -1); /* QW negative engine id */
  CHECK(strcmp(qa_compat_qc_strings_get(&strings, -1), "abc") == 0);
  CHECK(qa_compat_qc_strings_set_engine(&strings, "tmp", "xyz", -1) == -1);
  CHECK(strcmp(qa_compat_qc_strings_get(&strings, -1), "xyz") == 0);
  size_t snap_length = 0;
  uint8_t *snapshot = qa_compat_qc_strings_snapshot(&strings, &snap_length);
  CHECK(snapshot != NULL);
  qa_compat_qc_strings restored;
  CHECK(qa_compat_qc_strings_init(&restored, prog_strings, sizeof prog_strings - 1, true));
  CHECK(qa_compat_qc_strings_restore(&restored, snapshot, snap_length));
  CHECK(strcmp(qa_compat_qc_strings_get(&restored, -1), "xyz") == 0);
  free(snapshot);
  qa_compat_qc_strings_destroy(&restored);
  qa_compat_qc_strings_destroy(&strings);
}

static void test_machine(void) {
  qa_compat_qc_program program;
  CHECK(load_test_program(&program));
  qa_compat_qc_builtin_services services;
  memset(&services, 0, sizeof services);
  services.kind = "netquake";
  qa_compat_qc_builtin_registry registry;
  memset(&registry, 0, sizeof registry);
  CHECK(qa_compat_qc_create_builtins(services, &registry));
  qa_compat_qc_entity_memory entities;
  qa_compat_qc_machine machine;
  CHECK(make_machine(&program, &entities, &registry, &machine, 0));
  qa_compat_qc_words_set_float(&machine.globals, 40, 1.5);
  qa_compat_qc_words_set_float(&machine.globals, 41, 2.25);
  qa_compat_qc_machine_execute(&machine, 1, 0);
  CHECK(qa_compat_qc_words_float(&machine.globals, 31) == 3.75);
  CHECK(qa_compat_qc_words_float(&machine.globals, 1) == 3.75);
  /* normalize builtin (#9): (3,0,4) -> (0.6,0,0.8) in binary32 */
  qa_compat_qc_words_set_vector(&machine.globals, 4, (qa_vec3){3, 0, 4});
  qa_compat_qc_machine_execute(&machine, 3, 1);
  qa_vec3 normalized = qa_compat_qc_words_vector(&machine.globals, 1);
  CHECK(normalized.x == (double)(float)0.6 && normalized.y == 0 && normalized.z == (double)(float)0.8);
  /* missing builtins: #42 unbound, normalize bound */
  qa_compat_qc_missing_builtin missing[4];
  size_t missing_count = qa_compat_qc_machine_missing_builtins(&machine, missing, 4);
  CHECK(missing_count == 1 && missing[0].has_number && missing[0].number == 42);
  /* snapshot/restore */
  qa_compat_qc_machine_snapshot snapshot;
  CHECK(qa_compat_qc_machine_get_snapshot(&machine, &snapshot));
  qa_compat_qc_words_set_float(&machine.globals, 31, 99);
  CHECK(qa_compat_qc_machine_restore(&machine, &snapshot));
  CHECK(qa_compat_qc_words_float(&machine.globals, 31) == 3.75);
  qa_compat_qc_machine_snapshot_destroy(&snapshot);
  CHECK_ABORTS(qa_compat_qc_machine_execute(&machine, 4, 0));
  CHECK_ABORTS(qa_compat_qc_machine_global_offset(&machine, "nosuch"));
  qa_compat_qc_machine_destroy(&machine);
  qa_compat_qc_entity_memory_destroy(&entities);
  /* runaway loop with a small statement budget */
  qa_compat_qc_entity_memory entities2;
  qa_compat_qc_machine machine2;
  CHECK(make_machine(&program, &entities2, &registry, &machine2, 100));
  CHECK_ABORTS(qa_compat_qc_machine_execute(&machine2, 2, 0));
  qa_compat_qc_machine_destroy(&machine2);
  qa_compat_qc_entity_memory_destroy(&entities2);
  qa_compat_qc_registry_destroy(&registry);
  qa_compat_qc_program_destroy(&program);
}

static void test_save(void) {
  qa_compat_qc_program program;
  CHECK(load_test_program(&program));
  qa_compat_qc_builtin_registry registry;
  memset(&registry, 0, sizeof registry);
  qa_compat_qc_entity_memory entities;
  qa_compat_qc_machine machine;
  CHECK(make_machine(&program, &entities, &registry, &machine, 0));
  qa_compat_qc_text_pair pairs[] = {
    {"angle", "90"}, {"health", "50"}, {"_skip", "1"}, {"unknownkey", "x"}, {"classname", "monster\\ndog"},
  };
  qa_compat_qc_entity_parse_result_compat result;
  CHECK(qa_compat_qc_apply_entity_pairs(&machine, 1, pairs, 5, &result));
  CHECK(!result.empty);
  CHECK(result.retained.count == 4);
  CHECK(result.unknown.count == 1 && strcmp(result.unknown.items[0].key, "unknownkey") == 0);
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(machine.entities, 1);
  int64_t angles = qa_compat_qc_machine_field_offset(&machine, "angles");
  qa_vec3 value = qa_compat_qc_words_vector(words, angles);
  CHECK(value.x == 0 && value.y == 90 && value.z == 0);
  CHECK(qa_compat_qc_words_float(words, qa_compat_qc_machine_field_offset(&machine, "health")) == 50);
  const char *classname = qa_compat_qc_strings_get(&machine.strings,
    qa_compat_qc_words_int(words, qa_compat_qc_machine_field_offset(&machine, "classname")));
  CHECK(strcmp(classname, "monster\ndog") == 0);
  qa_compat_qc_text_pairs_free(&result.unknown);
  qa_compat_qc_text_pairs_free(&result.retained);
  /* save pairs: underscore-second-char field skipped, zero fields skipped */
  qa_compat_qc_words_set_float(words, qa_compat_qc_machine_field_offset(&machine, "a_b"), 3);
  qa_compat_qc_text_pairs saved;
  CHECK(qa_compat_qc_save_entity_pairs(&machine, 1, false, &saved));
  bool saw_health = false, saw_ab = false;
  for (size_t i = 0; i < saved.count; i++) {
    if (strcmp(saved.items[i].key, "health") == 0) saw_health = true;
    if (strcmp(saved.items[i].key, "a_b") == 0) saw_ab = true;
  }
  CHECK(saw_health && !saw_ab);
  qa_compat_qc_text_pairs_free(&saved);
  qa_compat_qc_text_pairs free_saved;
  CHECK(qa_compat_qc_save_entity_pairs(&machine, 1, true, &free_saved));
  CHECK(free_saved.count == 0);
  qa_compat_qc_text_pairs_free(&free_saved);
  qa_compat_qc_machine_destroy(&machine);
  qa_compat_qc_entity_memory_destroy(&entities);
  qa_compat_qc_program_destroy(&program);
}

static void test_pure_helpers(void) {
  /* host requirements: 48 numbered; QW drops 48, adds 79/80/82; rerelease adds 401 + 21 named */
  CHECK(qa_compat_qc_host_requirements("netquake", NULL, 0) == 48);
  CHECK(qa_compat_qc_host_requirements("quakeworld", NULL, 0) == 50);
  CHECK(qa_compat_qc_host_requirements("rerelease", NULL, 0) == 70);
  /* temporary events */
  qa_temporary_entity effect;
  memset(&effect, 0, sizeof effect);
  effect.kind = QA_TE_POINT; effect.type = 3; effect.count = 7; effect.origin = (qa_vec3){1, 2, 3};
  qa_q1_event event = qa_compat_qc_quake_temporary_event(effect, NULL, false);
  CHECK(strcmp(event.kind, "effect") == 0 && strcmp(event.effect, "explosion") == 0 && event.amount == 7);
  effect.type = 12;
  event = qa_compat_qc_quake_temporary_event(effect, NULL, true);
  CHECK(strcmp(event.kind, "particles") == 0 && event.color == 73 && event.count == 140);
  effect.kind = QA_TE_BEAM; effect.type = 13; effect.entity = 5;
  qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 2;
  event = qa_compat_qc_quake_temporary_event(effect, &actor, false);
  CHECK(strcmp(event.kind, "beam") == 0 && strcmp(event.beam_style, "grapple") == 0);
  CHECK_ABORTS(qa_compat_qc_quake_temporary_event(effect, NULL, false));
  /* weapon behavior capability: all required fields present except none — test program lacks none of the 12? it has all */
  qa_compat_qc_program program;
  CHECK(load_test_program(&program));
  char *error = qa_compat_qc_weapon_behavior_capability_error(&program);
  CHECK(error == NULL);
  free(error);
  /* compatibility: null bytes default, dialect canonicalization */
  qa_compat_qc_quakec_compatibility compat;
  CHECK(qa_compat_qc_read_quakec_compatibility(NULL, 0, "sha256:x", &compat));
  CHECK(strcmp(compat.message_dialect, "known-retail") == 0 && compat.pickup_caller_count == 0);
  qa_compat_qc_quakec_compatibility_destroy(&compat);
  static const char json[] = "{\"version\":1,\"artifactDigest\":\"sha256:x\",\"messageDialect\":\"quake-1-re-ts-private\"}";
  CHECK(qa_compat_qc_read_quakec_compatibility((const uint8_t *)json, strlen(json), "sha256:x", &compat));
  CHECK(strcmp(compat.message_dialect, "q1-rerelease-private") == 0);
  qa_compat_qc_quakec_compatibility_destroy(&compat);
  CHECK_ABORTS(qa_compat_qc_quakec_compatibility bad; qa_compat_qc_read_quakec_compatibility((const uint8_t *)json, strlen(json), "sha256:other", &bad));
  qa_compat_qc_program_destroy(&program);
}

int main(void) {
  test_program();
  test_memory();
  test_machine();
  test_save();
  test_pure_helpers();
  printf("test_compat_qc: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}


