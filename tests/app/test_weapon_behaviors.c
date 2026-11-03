/* C checks for app/bootstrap/simulation quakec-weapon-behavior.ts,
 * qvm-weapon-behavior.ts and rerelease-weapon-behavior.ts, plus the TS
 * tests tests/compat/q2/rerelease/weapon-behavior.test.ts and
 * tests/content/catalog/qvm-weapon-behaviors.test.ts cases noted below.
 * QuakeC expectations come from running the TS source under bun over the
 * same id1 progs.dat (~/qfiles/Quake/Id1/PAK0.PAK). GPL-2.0-or-later. */
#include <math.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/quakec_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
#include "qa/compat/qc/program.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/image.h"
#include "qa/content/mounts.h"
#include "qa/core/cvars/index.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); if (!a_ || !b_ || strcmp(a_, b_) != 0) { failures++; \
  fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", b_ ? b_ : "(null)"); } } while (0)
static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
#define CHECK_CTX(ctx) CHECK((ctx) == SENTINEL)
static void expect_raise_at(int line, void (*fn)(void *), void *ctx, const char *name, const char *fragment) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) { failures++; fprintf(stderr, "%s:%d: expected a raise containing \"%s\"\n", __FILE__, line, fragment); return; }
  if ((name && strcmp(caught.name, name) != 0) || !strstr(caught.message, fragment)) {
    failures++; fprintf(stderr, "%s:%d: raised %s \"%s\", expected %s \"%s\"\n", __FILE__, line, caught.name, caught.message, name ? name : "*", fragment);
  }
}
#define EXPECT_RAISE(fn, ctx, name, fragment) expect_raise_at(__LINE__, fn, ctx, name, fragment)
static bool vec_eq(qa_vec3 v, double x, double y, double z) { return v.x == x && v.y == y && v.z == z; }
static void sha_hex(const uint8_t *bytes, size_t length, char out[65]) {
  uint8_t digest[SHA256_DIGEST_LENGTH]; SHA256(bytes, length, digest);
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
}
static qa_actor_id actor_id(int64_t slot) { qa_actor_id a; memset(&a, 0, sizeof a); a.slot = slot; a.generation = 1; return a; }
static qa_body_state body_at(double x) {
  qa_body_state b; memset(&b, 0, sizeof b);
  b.origin = (qa_vec3){x, 0, 0}; b.angles = (qa_vec3){0, 90, 0};
  b.bounds.min = (qa_vec3){-16, -16, -24}; b.bounds.max = (qa_vec3){16, 16, 32}; return b;
}

/* ---------------- QuakeC (real id1 progs.dat) ---------------- */
static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)n); if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
  fclose(f); *out_length = (size_t)n; return data;
}
static uint8_t *pak_progs(size_t *out_length) {
  char path[512]; snprintf(path, sizeof path, "%s/qfiles/Quake/Id1/PAK0.PAK", getenv("HOME") ? getenv("HOME") : "");
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
typedef struct test_random { void *sentinel; qa_app_sim_source_random *random; double words[31]; } test_random;
static double tr_next_integer(void *ctx) { test_random *r = ctx; CHECK_CTX(r->sentinel); return qa_app_sim_source_random_next_integer(r->random); }
static double tr_next_unit(void *ctx) { test_random *r = ctx; CHECK_CTX(r->sentinel); return qa_app_sim_source_random_next_unit(r->random); }
static qa_random_state tr_checkpoint(void *ctx) {
  test_random *r = ctx; CHECK_CTX(r->sentinel);
  qa_app_sim_source_random_state s = qa_app_sim_source_random_checkpoint(r->random);
  qa_random_state out; memset(&out, 0, sizeof out); out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
  for (int i = 0; i < 31; i++) r->words[i] = s.words[i];
  out.words.values = r->words; out.words.count = 31; out.front = s.front; out.rear = s.rear; out.draws = (double)s.draws;
  return out;
}
static void tr_init(test_random *r, double seed) { r->sentinel = SENTINEL; r->random = qa_app_sim_source_random_create(seed, "classic"); }
static void tr_restore(test_random *r, qa_random_state state) {
  qa_app_sim_source_random_state s; memset(&s, 0, sizeof s); s.kind = "glibc-random";
  for (size_t i = 0; i < 31 && i < state.words.count; i++) s.words[i] = (int32_t)state.words.values[i];
  s.front = (int)state.front; s.rear = (int)state.rear; s.draws = (int64_t)state.draws;
  qa_app_sim_source_random_restore(r->random, s);
}
static qa_random_source tr_source(test_random *r) { qa_random_source s = {r, tr_next_integer, tr_next_unit, tr_checkpoint}; return s; }

typedef struct qc_host {
  void *sentinel;
  qa_app_sim_qc_weapon_target targets[2]; size_t target_count;
  bool model_null; int prints, aims, models, contents, traces;
} qc_host;
static const qa_app_sim_qc_weapon_target *qc_targets(void *ctx, size_t *count) { qc_host *h = ctx; CHECK_CTX(h->sentinel); *count = h->target_count; return h->targets; }
static qa_vec3 qc_aim(void *ctx, qa_actor_id shooter, double speed) {
  qc_host *h = ctx; CHECK_CTX(h->sentinel); h->aims++; CHECK(shooter.slot == 1);
  return (qa_vec3){speed == 1000 ? 0.6 : 0, 0.8, 0};
}
static bool qc_model(void *ctx, const char *path, qa_app_sim_qc_weapon_model *out) {
  qc_host *h = ctx; CHECK_CTX(h->sentinel); h->models++;
  if (h->model_null) return false;
  out->index = (double)strlen(path); out->bounds.min = (qa_vec3){-1, -2, -3}; out->bounds.max = (qa_vec3){1, 2, 3}; return true;
}
static void qc_print(void *ctx, const qa_actor_id *recipient, const char *text) { qc_host *h = ctx; CHECK_CTX(h->sentinel); (void)recipient; (void)text; h->prints++; }
static qa_trace_result qc_trace(void *ctx, qa_trace_query query) { qc_host *h = ctx; CHECK_CTX(h->sentinel); (void)query; h->traces++; qa_trace_result r; memset(&r, 0, sizeof r); r.kind = "q1"; r.fraction = 1; return r; }
static qa_point_contents_result qc_contents(void *ctx, qa_point_contents_query query) {
  qc_host *h = ctx; CHECK_CTX(h->sentinel); (void)query; h->contents++;
  qa_point_contents_result r; memset(&r, 0, sizeof r); r.kind = "q1"; r.contents = -1; return r;
}
typedef struct qc_fixture {
  qa_compat_qc_program program; qa_module_identity module; qc_host host; test_random random;
  qa_app_sim_qc_weapon_behavior_options options;
} qc_fixture;
static int64_t qc_function(const qa_compat_qc_program *p, const char *name) { return qa_compat_qc_program_function_named(p, name)->index; }
static qa_weapon_behavior_definition qc_definition(qc_fixture *f, const char *id, const char *role, const char *function) {
  qa_weapon_behavior_definition d; memset(&d, 0, sizeof d);
  d.id = id; d.title = "Super nails"; d.module = f->module; d.role = role; d.aspect = "trajectory"; d.has_activate = false;
  d.fire.kind = QA_WEAPON_CALLBACK_QUAKEC; d.fire.module = f->module; d.fire.function_index = (double)qc_function(&f->program, function);
  return d;
}
static bool qc_fixture_init(qc_fixture *f) {
  memset(f, 0, sizeof *f);
  size_t length = 0; uint8_t *bytes = pak_progs(&length);
  if (!bytes) { printf("skipped: no ~/qfiles/Quake/Id1/PAK0.PAK\n"); return false; }
  CHECK(qa_compat_qc_load_program(bytes, length, NULL, "Id1/PAK0.PAK", &f->program)); free(bytes);
  f->module.id = "test:id1"; f->module.artifact_path = "progs.dat"; f->module.digest = f->program.digest; f->module.revision = "1";
  f->host.sentinel = SENTINEL;
  f->host.targets[0] = (qa_app_sim_qc_weapon_target){actor_id(1), body_at(0), 100, "player", "tester", true};
  f->host.targets[1] = (qa_app_sim_qc_weapon_target){actor_id(3), body_at(64), 50, "monster_army", "", true};
  f->host.target_count = 2;
  tr_init(&f->random, 7);
  qa_app_sim_qc_weapon_behavior_options o; memset(&o, 0, sizeof o);
  o.definition = qc_definition(f, "quakec:supernails", "nail", "W_FireSuperSpikes"); o.program = &f->program;
  o.random = tr_source(&f->random); o.scene.ctx = &f->host; o.scene.trace = qc_trace; o.scene.point_contents = qc_contents;
  o.mode = "deathmatch"; o.ctx = &f->host; o.targets = qc_targets; o.aim = qc_aim; o.model = qc_model; o.print = qc_print;
  f->options = o;
  return true;
}
static void qc_fixture_destroy(qc_fixture *f) { qa_app_sim_source_random_destroy(f->random.random); qa_compat_qc_program_destroy(&f->program); }
static qa_weapon_behavior_launch qc_launch(int64_t projectile, double time, const char *role) {
  qa_weapon_behavior_launch l; memset(&l, 0, sizeof l);
  l.projectile.id = actor_id(projectile); l.projectile.owner = "test:primary"; l.shooter = actor_id(1);
  l.weapon = "weapon:test"; l.role = role; l.time_seconds = time; l.body = body_at(0); return l;
}
static void hash_snapshot(const qa_compat_qc_machine_snapshot *m, char g[65], char e[65], char s[65]) {
  sha_hex(m->globals, m->globals_length, g); sha_hex(m->entities, m->entities_length, e); sha_hex(m->strings, m->strings_length, s);
}
static qa_actor_id saved_identity(void *ctx, qa_saved_actor_id saved) { CHECK_CTX(ctx); return actor_id((int64_t)saved.slot); }
typedef struct qc_call { qa_app_sim_quakec_weapon_behavior_source *source; qc_fixture *fixture; qa_weapon_behavior_launch launch;
  const qa_app_sim_quakec_weapon_behavior_checkpoint *checkpoint; qa_app_sim_qc_weapon_behavior_options options; } qc_call;
static void qc_attach_call(void *ctx) { qc_call *c = ctx; (void)qa_app_sim_quakec_weapon_behavior_source_attach(c->source, c->launch); }
static void qc_create_call(void *ctx) { qc_call *c = ctx; qa_app_sim_quakec_weapon_behavior_source_destroy(qa_app_sim_quakec_weapon_behavior_source_create(c->options)); }
static void qc_restore_call(void *ctx) { qc_call *c = ctx; qa_app_sim_quakec_weapon_behavior_source_restore(c->source, c->checkpoint, saved_identity, SENTINEL, tr_source(&c->fixture->random)); }
static void qc_resume_call(void *ctx) { qc_call *c = ctx; (void)qa_app_sim_quakec_weapon_behavior_source_resume(c->source, actor_id(42)); }

/* checkpoint -> TS object form, for the reader (encodeCheckpointValue shape). */
static qa_save_value *module_value(qa_module_identity m) {
  qa_save_value *v = qa_save_value_object();
  qa_save_value_set(v, "id", qa_save_value_string(m.id)); qa_save_value_set(v, "artifactPath", qa_save_value_string(m.artifact_path));
  qa_save_value_set(v, "digest", qa_save_value_string(m.digest)); qa_save_value_set(v, "revision", qa_save_value_string(m.revision)); return v;
}
static qa_save_value *qc_checkpoint_value(const qa_app_sim_quakec_weapon_behavior_checkpoint *c, const char *mode) {
  qa_save_value *v = qa_save_value_object(), *d = qa_save_value_object(), *fire = qa_save_value_object();
  qa_save_value_set(v, "version", qa_save_value_number(c->version));
  qa_save_value_set(d, "id", qa_save_value_string(c->definition.id)); qa_save_value_set(d, "title", qa_save_value_string(c->definition.title));
  qa_save_value_set(d, "module", module_value(c->definition.module)); qa_save_value_set(d, "role", qa_save_value_string(c->definition.role));
  qa_save_value_set(d, "aspect", qa_save_value_string(c->definition.aspect)); qa_save_value_set(d, "activate", qa_save_value_null());
  qa_save_value_set(fire, "kind", qa_save_value_string("quakec")); qa_save_value_set(fire, "module", module_value(c->definition.fire.module));
  qa_save_value_set(fire, "functionIndex", qa_save_value_number(c->definition.fire.function_index)); qa_save_value_set(d, "fire", fire);
  qa_save_value_set(v, "definition", d); qa_save_value_set(v, "mode", qa_save_value_string(mode));
  qa_save_value_set(v, "sourceTime", qa_save_value_number(c->source_time));
  qa_save_value *random = qa_save_value_object(), *words = qa_save_value_array();
  qa_save_value_set(random, "kind", qa_save_value_string("glibc-random"));
  for (size_t i = 0; i < c->random.words.count; i++) qa_save_value_push(words, qa_save_value_number(c->random.words.values[i]));
  qa_save_value_set(random, "words", words); qa_save_value_set(random, "front", qa_save_value_number(c->random.front));
  qa_save_value_set(random, "rear", qa_save_value_number(c->random.rear)); qa_save_value_set(random, "draws", qa_save_value_number(c->random.draws));
  qa_save_value_set(v, "random", random);
  qa_save_value *m = qa_save_value_object(), *profiling = qa_save_value_array();
  qa_save_value_set(m, "globals", qa_save_value_bytes(c->machine.globals, c->machine.globals_length));
  qa_save_value_set(m, "entities", qa_save_value_bytes(c->machine.entities, c->machine.entities_length));
  qa_save_value_set(m, "entityCount", qa_save_value_number((double)c->machine.entity_count));
  qa_save_value_set(m, "strings", qa_save_value_bytes(c->machine.strings, c->machine.strings_length));
  qa_save_value_set(m, "statement", qa_save_value_number((double)c->machine.statement));
  qa_save_value_set(m, "functionIndex", qa_save_value_number((double)c->machine.function_index));
  qa_save_value_set(m, "argumentCount", qa_save_value_number((double)c->machine.argument_count));
  for (size_t i = 0; i < c->machine.profiling_count; i++) qa_save_value_push(profiling, qa_save_value_number(c->machine.profiling[i]));
  qa_save_value_set(m, "profiling", profiling); qa_save_value_set(m, "traceEnabled", qa_save_value_boolean(c->machine.trace_enabled));
  qa_save_value_set(v, "machine", m);
  qa_save_value *free_list = qa_save_value_array(), *retired = qa_save_value_array(), *bindings = qa_save_value_array();
  for (size_t i = 0; i < c->free_count; i++) qa_save_value_push(free_list, qa_save_value_number(c->free_slots[i]));
  for (size_t i = 0; i < c->retired_count; i++) qa_save_value_push(retired, qa_save_value_number(c->retired[i]));
  for (size_t i = 0; i < c->binding_count; i++) {
    qa_save_value *b = qa_save_value_object(), *a = qa_save_value_object();
    qa_save_value_set(b, "slot", qa_save_value_number(c->bindings[i].slot));
    qa_save_value_set(a, "slot", qa_save_value_number(c->bindings[i].actor.slot)); qa_save_value_set(a, "generation", qa_save_value_number(c->bindings[i].actor.generation));
    qa_save_value_set(b, "actor", a); qa_save_value_set(b, "kind", qa_save_value_string(c->bindings[i].kind)); qa_save_value_push(bindings, b);
  }
  qa_save_value_set(v, "free", free_list); qa_save_value_set(v, "retired", retired); qa_save_value_set(v, "bindings", bindings);
  return v;
}
typedef struct qc_read_call { qa_save_value *value; qa_weapon_behavior_definition expected; } qc_read_call;
static void qc_read(void *ctx) {
  qc_read_call *c = ctx; qa_save_reader reader; qa_save_reader_init(&reader, c->value, "checkpoint");
  qa_app_sim_quakec_weapon_behavior_checkpoint out = qa_app_sim_read_quakec_weapon_behavior_checkpoint(&reader, c->expected);
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&out); qa_save_reader_destroy(&reader);
}

static void test_quakec_source(void) {
  qc_fixture f; if (!qc_fixture_init(&f)) return;
  qa_app_sim_quakec_weapon_behavior_source *s = qa_app_sim_quakec_weapon_behavior_source_create(f.options);
  CHECK(s != NULL);
  qa_weapon_behavior_source view = qa_app_sim_quakec_weapon_behavior_source_as_source(s);
  CHECK(view.ctx == s); CHECK_STR(view.definition.id, "quakec:supernails");
  qa_weapon_behavior_instance_result attached = view.attach(view.ctx, qc_launch(2, 1.25, "nail"));
  CHECK(attached.has_value);
  qa_weapon_behavior_instance inst = attached.value;
  /* TS: initial {origin 0 0 16, velocity 600 800 0, angles 0 53 0}. */
  CHECK(vec_eq(inst.initial.origin, 0, 0, 16)); CHECK(vec_eq(inst.initial.velocity, 600, 800, 0)); CHECK(vec_eq(inst.initial.angles, 0, 53, 0));
  CHECK(f.host.aims == 1 && f.host.models == 2); CHECK_STR(inst.definition.id, "quakec:supernails");
  qa_body_state pbody; memset(&pbody, 0, sizeof pbody); pbody.origin = (qa_vec3){10, 20, 30}; pbody.velocity = (qa_vec3){600, 800, 0};
  CHECK(!inst.step(inst.ctx, pbody, 2).has_value);
  qa_app_sim_quakec_weapon_behavior_checkpoint cp = qa_app_sim_quakec_weapon_behavior_source_checkpoint(s);
  /* TS cp: sourceTime 2, free [], retired [], bindings [{1,A,target},{2,B,target},{3,P,projectile}], entityCount 4. */
  CHECK(cp.source_time == 2 && cp.free_count == 0 && cp.retired_count == 0 && cp.binding_count == 3 && cp.machine.entity_count == 4);
  CHECK(cp.bindings[0].slot == 1 && cp.bindings[0].actor.slot == 1 && strcmp(cp.bindings[0].kind, "target") == 0);
  CHECK(cp.bindings[1].slot == 2 && cp.bindings[1].actor.slot == 3 && strcmp(cp.bindings[1].kind, "target") == 0);
  CHECK(cp.bindings[2].slot == 3 && cp.bindings[2].actor.slot == 2 && strcmp(cp.bindings[2].kind, "projectile") == 0);
  CHECK(cp.random.kind == QA_RANDOM_STATE_GLIBC_RANDOM && cp.random.words.count == 31);
  CHECK_STR(cp.mode, "deathmatch");
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&cp);
  /* SUB_Remove at nextthink 7.25 retires the projectile. */
  CHECK(!inst.step(inst.ctx, pbody, 7.5).has_value);
  cp = qa_app_sim_quakec_weapon_behavior_source_checkpoint(s);
  CHECK(cp.retired_count == 1 && cp.retired[0] == 3 && cp.binding_count == 3);
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&cp);
  CHECK(!inst.step(inst.ctx, pbody, 8).has_value); /* retired stays null */
  inst.close(inst.ctx); inst.close(inst.ctx);
  f.host.target_count = 1; /* B leaves the world and returns its slot */
  qa_weapon_behavior_instance_result second = qa_app_sim_quakec_weapon_behavior_source_attach(s, qc_launch(4, 8, "nail"));
  CHECK(second.has_value);
  CHECK(vec_eq(second.value.initial.origin, 0, 0, 16) && vec_eq(second.value.initial.velocity, 600, 800, 0) && vec_eq(second.value.initial.angles, 0, 53, 0));
  qa_app_sim_quakec_weapon_behavior_checkpoint cp3 = qa_app_sim_quakec_weapon_behavior_source_checkpoint(s);
  /* TS cp3: free [2], retired [], bindings [{1,A,target},{3,P2,projectile}], entityCount 4, and these machine hashes. */
  CHECK(cp3.free_count == 1 && cp3.free_slots[0] == 2 && cp3.retired_count == 0 && cp3.binding_count == 2 && cp3.machine.entity_count == 4);
  CHECK(cp3.bindings[0].slot == 1 && cp3.bindings[0].actor.slot == 1 && cp3.bindings[1].slot == 3 && cp3.bindings[1].actor.slot == 4);
  CHECK_STR(cp3.bindings[1].kind, "projectile");
  char g[65], e[65], st[65]; hash_snapshot(&cp3.machine, g, e, st);
  CHECK_STR(g, "92f9ce1b9b341aa52c14ade550ad352df5d62e8d9a5e3383e768ff172c9444d8");
  CHECK_STR(e, "cffc229afec4e993d1bd1309918cb419067cb2708c83b2379b3c2aacf18c4ff4");
  CHECK_STR(st, "fe5cbb419519fb2dc1cee783d08fe65a29f1328df67a51c1860b66dc683875e2");
  qc_call call; memset(&call, 0, sizeof call); call.source = s; call.fixture = &f;
  call.launch = qc_launch(9, 9, "rocket");
  EXPECT_RAISE(qc_attach_call, &call, "Error", "Source behavior projectile role differs");
  EXPECT_RAISE(qc_resume_call, &call, "Error", "Saved projectile has no restored QuakeC behavior");
  /* Restore cp3 into a fresh source (TS: same free/bindings and entities). */
  qa_app_sim_quakec_weapon_behavior_source *fresh = qa_app_sim_quakec_weapon_behavior_source_create(f.options);
  test_random restored; tr_init(&restored, 7); tr_restore(&restored, cp3.random);
  qa_app_sim_quakec_weapon_behavior_source_restore(fresh, &cp3, saved_identity, SENTINEL, tr_source(&restored));
  qa_app_sim_quakec_weapon_behavior_checkpoint cp4 = qa_app_sim_quakec_weapon_behavior_source_checkpoint(fresh);
  CHECK(cp4.free_count == 1 && cp4.free_slots[0] == 2 && cp4.binding_count == 2 && cp4.bindings[1].actor.slot == 4);
  char e4[65], g4[65], s4[65]; hash_snapshot(&cp4.machine, g4, e4, s4); CHECK_STR(e4, e);
  qa_weapon_behavior_instance resumed = qa_app_sim_quakec_weapon_behavior_source_resume(fresh, actor_id(4));
  CHECK(vec_eq(resumed.initial.origin, 0, 0, 16));
  call.source = fresh; call.checkpoint = &cp3; call.fixture = &f;
  EXPECT_RAISE(qc_restore_call, &call, "Error", "Incompatible QuakeC weapon behavior checkpoint");
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&cp4);
  /* Reader round trip of the TS object form. */
  qa_save_value *value = qc_checkpoint_value(&cp3, "deathmatch");
  qa_save_reader reader; qa_save_reader_init(&reader, value, "checkpoint");
  qa_app_sim_quakec_weapon_behavior_checkpoint read = qa_app_sim_read_quakec_weapon_behavior_checkpoint(&reader, f.options.definition);
  CHECK(read.version == 1 && read.source_time == cp3.source_time && read.free_count == 1 && read.binding_count == 2 && read.machine.entity_count == 4);
  CHECK_STR(read.mode, "deathmatch"); CHECK(read.random.words.count == 31 && read.random.words.values[5] == cp3.random.words.values[5]);
  CHECK(read.machine.entities_length == cp3.machine.entities_length && memcmp(read.machine.entities, cp3.machine.entities, read.machine.entities_length) == 0);
  qa_app_sim_quakec_weapon_behavior_source *third = qa_app_sim_quakec_weapon_behavior_source_create(f.options);
  test_random third_random; tr_init(&third_random, 7); tr_restore(&third_random, read.random);
  qa_app_sim_quakec_weapon_behavior_source_restore(third, &read, saved_identity, SENTINEL, tr_source(&third_random));
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&read); qa_save_reader_destroy(&reader); qa_save_value_free(value);
  value = qc_checkpoint_value(&cp3, "bogus");
  qc_read_call rc = {value, f.options.definition};
  EXPECT_RAISE(qc_read, &rc, NULL, "mode");
  qa_save_value_free(value);
  /* A random stream that differs from the saved one is refused. */
  qa_app_sim_quakec_weapon_behavior_source *fourth = qa_app_sim_quakec_weapon_behavior_source_create(f.options);
  qa_app_sim_source_random_next_integer(f.random.random);
  call.source = fourth; call.checkpoint = &cp3;
  EXPECT_RAISE(qc_restore_call, &call, "Error", "Weapon behavior random source was not restored");
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&cp3);
  qa_app_sim_quakec_weapon_behavior_source_destroy(fourth); qa_app_sim_quakec_weapon_behavior_source_destroy(third);
  qa_app_sim_quakec_weapon_behavior_source_destroy(fresh); qa_app_sim_quakec_weapon_behavior_source_destroy(s);
  qa_app_sim_source_random_destroy(restored.random); qa_app_sim_source_random_destroy(third_random.random);
  qc_fixture_destroy(&f);
}
static void test_quakec_errors(void) {
  qc_fixture f; if (!qc_fixture_init(&f)) return;
  qc_call call; memset(&call, 0, sizeof call); call.fixture = &f;
  /* TS: W_FireRocket raises QcRuntimeError "QuakeC 184:3868: No source model: progs/missile.mdl". */
  call.options = f.options; call.options.definition = qc_definition(&f, "quakec:rocket", "rocket", "W_FireRocket"); call.options.mode = "coop";
  qa_app_sim_quakec_weapon_behavior_source *rocket = qa_app_sim_quakec_weapon_behavior_source_create(call.options);
  f.host.model_null = true; call.source = rocket; call.launch = qc_launch(9, 1, "rocket");
  EXPECT_RAISE(qc_attach_call, &call, "QcRuntimeError", "QuakeC 184:3868: No source model: progs/missile.mdl");
  /* The failed launch released its spawned slot and its launching state. */
  qa_app_sim_quakec_weapon_behavior_checkpoint cp = qa_app_sim_quakec_weapon_behavior_source_checkpoint(rocket);
  CHECK(cp.free_count == 1 && cp.free_slots[0] == 3);
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&cp);
  qa_compat_qc_machine *vm = qa_app_sim_quakec_weapon_behavior_source_machine(rocket);
  CHECK(qa_compat_qc_words_float(&vm->globals, qa_compat_qc_machine_global_offset(vm, "coop")) == 1);
  CHECK(qa_compat_qc_words_float(&vm->globals, qa_compat_qc_machine_global_offset(vm, "deathmatch")) == 0);
  qa_app_sim_quakec_weapon_behavior_source_destroy(rocket); f.host.model_null = false;
  call.options = f.options; call.options.definition = qc_definition(&f, "quakec:spikes", "nail", "W_FireSpikes");
  EXPECT_RAISE(qc_create_call, &call, "Error", "Behavior callback identity differs from its source module");
  call.options = f.options; call.options.definition.module.digest = "sha256:0000";
  EXPECT_RAISE(qc_create_call, &call, "Error", "Weapon behavior requires its exact declared QuakeC artifact");
  call.options = f.options; call.options.definition.fire.module.revision = "2";
  EXPECT_RAISE(qc_create_call, &call, "Error", "Behavior callback identity differs from its source module");
  call.options = f.options; call.options.definition.fire.function_index = 1.5;
  EXPECT_RAISE(qc_create_call, &call, "Error", "Behavior callback identity differs from its source module");
  qc_fixture_destroy(&f);
}

/* ---------------- QVM (synthetic qagame) ---------------- */
enum { E_BASE = 0x1000, STRIDE = 544, E1 = E_BASE + STRIDE, E2 = E_BASE + 2 * STRIDE, C_BASE = 0x4000, C_STRIDE = 512 };
enum { F_ALLOC = 16, F_FREE = 22, F_FIRE = 31, F_THINK1 = 49, F_THINK2 = 61, F_ACTIVATE = 67 };
typedef struct code { uint8_t bytes[1024]; size_t at; int32_t count; } code;
static void op(code *c, int opcode) { c->bytes[c->at++] = (uint8_t)opcode; c->count++; }
static void op4(code *c, int opcode, int32_t value) { op(c, opcode); memcpy(c->bytes + c->at, &value, 4); c->at += 4; }
static void op1(code *c, int opcode, uint8_t value) { op(c, opcode); c->bytes[c->at++] = value; }
static void store(code *c, int32_t address, int32_t value) { op4(c, QA_QVM_OP_CONST, address); op4(c, QA_QVM_OP_CONST, value); op(c, QA_QVM_OP_STORE4); }
static int32_t f32(float v) { int32_t bits; memcpy(&bits, &v, 4); return bits; }
static size_t build_qvm(uint8_t *out) {
  code c; memset(&c, 0, sizeof c);
  op4(&c, QA_QVM_OP_ENTER, 32);  /* vmMain: trap_LocateGameData, return 0 */
  const int32_t args[5] = {E_BASE, 8, STRIDE, C_BASE, C_STRIDE};
  for (int i = 0; i < 5; i++) { op4(&c, QA_QVM_OP_CONST, args[i]); op1(&c, QA_QVM_OP_ARG, (uint8_t)(8 + 4 * i)); }
  op4(&c, QA_QVM_OP_CONST, -16); op(&c, QA_QVM_OP_CALL); op(&c, QA_QVM_OP_POP);
  op4(&c, QA_QVM_OP_CONST, 0); op4(&c, QA_QVM_OP_LEAVE, 32);
  CHECK(c.count == F_ALLOC);
  op4(&c, QA_QVM_OP_ENTER, 8); store(&c, E1 + 516, 1); op4(&c, QA_QVM_OP_CONST, E1); op4(&c, QA_QVM_OP_LEAVE, 8);
  CHECK(c.count == F_FREE);
  op4(&c, QA_QVM_OP_ENTER, 8); op4(&c, QA_QVM_OP_LOCAL, 16); op(&c, QA_QVM_OP_LOAD4); op4(&c, QA_QVM_OP_CONST, 516); op(&c, QA_QVM_OP_ADD);
  op4(&c, QA_QVM_OP_CONST, 0); op(&c, QA_QVM_OP_STORE4); op4(&c, QA_QVM_OP_CONST, 0); op4(&c, QA_QVM_OP_LEAVE, 8);
  CHECK(c.count == F_FIRE);
  op4(&c, QA_QVM_OP_ENTER, 8); store(&c, E2 + 516, 1); store(&c, E2 + 4, 3); store(&c, E2 + 520, 2000); store(&c, E2 + 524, F_THINK1);
  store(&c, E2 + 36, f32(100)); op4(&c, QA_QVM_OP_CONST, E2); op4(&c, QA_QVM_OP_LEAVE, 8);
  CHECK(c.count == F_THINK1);
  op4(&c, QA_QVM_OP_ENTER, 8); store(&c, E2 + 40, f32(5)); store(&c, E2 + 520, 3000); store(&c, E2 + 524, F_THINK2);
  op4(&c, QA_QVM_OP_CONST, 0); op4(&c, QA_QVM_OP_LEAVE, 8);
  CHECK(c.count == F_THINK2);
  op4(&c, QA_QVM_OP_ENTER, 8); store(&c, E2 + 516, 0); op4(&c, QA_QVM_OP_CONST, 0); op4(&c, QA_QVM_OP_LEAVE, 8);
  CHECK(c.count == F_ACTIVATE);
  op4(&c, QA_QVM_OP_ENTER, 8); store(&c, 8, 1); op4(&c, QA_QVM_OP_CONST, 0); op4(&c, QA_QVM_OP_LEAVE, 8);
  size_t length = 0;
  const int32_t header[8] = {0x12721444, c.count, 32, (int32_t)c.at, 32 + (int32_t)c.at, 16, 0, 0x20000};
  memcpy(out, header, 32); length = 32; memcpy(out + length, c.bytes, c.at); length += c.at;
  memset(out + length, 0, 16); length += 16;
  return length;
}
typedef struct qvm_host {
  void *sentinel; qa_app_sim_qvm_weapon_target targets[2]; size_t target_count; int asserts, prints;
} qvm_host;
static const qa_app_sim_qvm_weapon_target *qvm_targets(void *ctx, size_t *count) { qvm_host *h = ctx; CHECK_CTX(h->sentinel); *count = h->target_count; return h->targets; }
static void qvm_print(void *ctx, const char *text) { qvm_host *h = ctx; CHECK_CTX(h->sentinel); (void)text; h->prints++; }
static void qvm_assert_current(void *ctx) { qvm_host *h = ctx; CHECK_CTX(h->sentinel); h->asserts++; }
static double qvm_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) { qvm_host *h = ctx; CHECK_CTX(h->sentinel); (void)has_output; (void)out; return 0; }
typedef struct qvm_fixture {
  uint8_t bytes[2048]; size_t length; char *digest; qa_resolved_qvm_artifact artifact; qa_qvm_weapon_profile profile;
  qvm_host host; qa_app_sim_qvm_weapon_behavior_options options;
} qvm_fixture;
static void qvm_fixture_init(qvm_fixture *f) {
  memset(f, 0, sizeof *f);
  f->length = build_qvm(f->bytes); f->digest = qa_digest_bytes(f->bytes, f->length);
  qa_qvm_resolve_artifact_options ro; memset(&ro, 0, sizeof ro);
  ro.module.id = "weapon-behavior:author"; ro.module.artifact_path = "vm/qagame.qvm"; ro.module.digest = f->digest; ro.module.revision = f->digest;
  ro.role = "qagame"; ro.bytes = f->bytes; ro.length = f->length;
  CHECK(qa_qvm_resolve_artifact(ro, &f->artifact));
  qa_qvm_weapon_profile *p = &f->profile;
  p->entity_stride = STRIDE; p->level_time = 4; p->allocate = F_ALLOC; p->free_entry = F_FREE;
  p->field_inuse = 516; p->field_nextthink = 520; p->field_think = 524; p->field_health = 528; p->fire_abi = "entity-pointer-start-direction";
  qa_weapon_behavior_definition *d = &p->definition;
  d->id = "qvm:authored-layout"; d->title = "ABI test only"; d->module = f->artifact.module; d->role = "rocket"; d->aspect = "trajectory";
  d->fire.kind = QA_WEAPON_CALLBACK_QVM; d->fire.module = f->artifact.module; d->fire.instruction_index = F_FIRE;
  d->has_activate = true; d->activate.kind = QA_WEAPON_CALLBACK_QVM; d->activate.module = f->artifact.module; d->activate.instruction_index = F_ACTIVATE;
  f->host.sentinel = SENTINEL;
  f->host.targets[0] = (qa_app_sim_qvm_weapon_target){actor_id(1), body_at(32), 75.9, "actor", NULL, NULL};
  f->host.target_count = 1;
  qa_app_sim_qvm_weapon_behavior_options *o = &f->options;
  o->artifact = &f->artifact; o->profile = &f->profile; o->seed = 5;
  o->entity_text = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"1 2 3\"\n}\n{\n\"classname\" \"func_door\"\n}\n";
  o->mode = "deathmatch"; o->team_mode = false; o->ctx = &f->host; o->real_time = qvm_real_time;
  o->targets = qvm_targets; o->print = qvm_print; o->assert_current = qvm_assert_current;
}
static void qvm_fixture_destroy(qvm_fixture *f) { qa_resolved_qvm_artifact_destroy(&f->artifact); free(f->digest); }
static int32_t entity_int(qa_qvm_game *game, int32_t address) {
  qa_qvm_writable_view v; qa_qvm_memory_data_view(game->module.memory, address, 4, &v); return qa_qvm_writable_view_get_int32(&v, 0, true);
}
typedef struct qvm_call {
  qa_app_sim_qvm_weapon_behavior_source *source; qa_weapon_behavior_launch launch; qa_weapon_behavior_instance instance;
  qa_body_state body; double time; qa_app_sim_qvm_weapon_behavior_options options;
  const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint;
} qvm_call;
static void qvm_attach(void *ctx) { qvm_call *c = ctx; (void)qa_app_sim_qvm_weapon_behavior_source_attach(c->source, c->launch); }
static void qvm_step(void *ctx) { qvm_call *c = ctx; (void)c->instance.step(c->instance.ctx, c->body, c->time); }
static void qvm_resume(void *ctx) { qvm_call *c = ctx; (void)qa_app_sim_qvm_weapon_behavior_source_resume(c->source, c->launch.projectile.id); }
static void qvm_create(void *ctx) { qvm_call *c = ctx; qa_app_sim_qvm_weapon_behavior_source_destroy(qa_app_sim_qvm_weapon_behavior_source_create(c->options)); }
static void qvm_checkpoint(void *ctx) { qvm_call *c = ctx; qa_app_sim_qvm_weapon_behavior_checkpoint cp = qa_app_sim_qvm_weapon_behavior_source_checkpoint(c->source); qa_app_sim_qvm_weapon_behavior_checkpoint_free(&cp); }
static void qvm_create_restored(void *ctx) {
  qvm_call *c = ctx;
  qa_app_sim_qvm_weapon_behavior_source_destroy(qa_app_sim_qvm_weapon_behavior_source_create_restored(c->options, c->checkpoint, saved_identity, SENTINEL));
}
static qa_weapon_behavior_launch qvm_launch(int64_t projectile, double time, qa_vec3 velocity) {
  qa_weapon_behavior_launch l; memset(&l, 0, sizeof l);
  l.projectile.id = actor_id(projectile); l.projectile.owner = "test:primary"; l.shooter = actor_id(1);
  l.weapon = "weapon:test"; l.role = "rocket"; l.time_seconds = time; l.body = body_at(32); l.body.origin = (qa_vec3){8, 9, 10}; l.body.velocity = velocity; return l;
}
static void test_qvm_source(void) {
  qvm_fixture f; qvm_fixture_init(&f);
  qa_app_sim_qvm_weapon_behavior_source *s = qa_app_sim_qvm_weapon_behavior_source_create(f.options);
  CHECK(s != NULL); CHECK(f.host.asserts == 0);
  qa_qvm_game *game = qa_app_sim_qvm_weapon_behavior_source_game(s);
  CHECK(qa_qvm_game_data_get_entity_stride_bytes(&game->data) == STRIDE);
  CHECK_STR(qa_app_sim_qvm_weapon_behavior_source_definition(s).id, "qvm:authored-layout");
  qa_weapon_behavior_source view = qa_app_sim_qvm_weapon_behavior_source_as_source(s);
  qa_weapon_behavior_instance_result attached = view.attach(view.ctx, qvm_launch(2, 1, (qa_vec3){3, 4, 0}));
  CHECK(attached.has_value);
  qa_weapon_behavior_instance inst = attached.value;
  CHECK(vec_eq(inst.initial.velocity, 100, 0, 0) && vec_eq(inst.initial.origin, 0, 0, 0));
  CHECK(entity_int(game, 8) == 1);              /* activation ran once */
  CHECK(entity_int(game, 4) == 1000);           /* levelTime */
  CHECK(entity_int(game, E1 + 528) == 75);      /* trunc(health) */
  qa_qvm_shared_entity *shooter = qa_qvm_game_data_entity_from_pointer(&game->data, E1);
  CHECK(vec_eq(qa_qvm_shared_entity_get_current_origin(shooter), 8, 9, 10));
  qa_vec3 dir = qa_qvm_entity_state_get_pos(&shooter->s).delta;
  CHECK(dir.x == (double)(float)0.6 && dir.y == (double)(float)0.8 && dir.z == 0);
  qa_body_state body; memset(&body, 0, sizeof body); body.origin = (qa_vec3){50, 60, 70}; body.velocity = (qa_vec3){11, 12, 13}; body.angles = (qa_vec3){1, 2, 3};
  CHECK(!inst.step(inst.ctx, body, 1.5).has_value);
  qa_weapon_trajectory_update_result next = inst.step(inst.ctx, body, 2.5);
  CHECK(next.has_value && vec_eq(next.value.origin, 50, 60, 70) && vec_eq(next.value.velocity, 11, 5, 13) && vec_eq(next.value.angles, 1, 2, 3));
  qvm_call call; memset(&call, 0, sizeof call); call.source = s; call.instance = inst; call.body = body;
  call.time = 2; EXPECT_RAISE(qvm_step, &call, "Error", "Invalid QVM weapon source time");
  qa_app_sim_qvm_weapon_behavior_checkpoint cp = qa_app_sim_qvm_weapon_behavior_source_checkpoint(s);
  CHECK(cp.binding_count == 2 && cp.activated_count == 1 && cp.retired_count == 0 && strlen(cp.profile_digest) == 64);
  CHECK(cp.bindings[0].pointer == E1 && strcmp(cp.bindings[0].kind, "target") == 0 && cp.bindings[1].pointer == E2 && strcmp(cp.bindings[1].kind, "projectile") == 0);
  CHECK(cp.module.kind == QA_GUEST_CHECKPOINT_QVM && strcmp(cp.module.host_state.format, "q3:weapon-host") == 0);
  /* The second activation is skipped; a missing direction refuses the launch. */
  call.launch = qvm_launch(5, 2.6, (qa_vec3){0, 0, 0});
  EXPECT_RAISE(qvm_attach, &call, "Error", "QVM source firing requires a projectile direction");
  call.launch = qvm_launch(5, 2.6, (qa_vec3){1, 0, 0}); call.launch.role = "grenade";
  EXPECT_RAISE(qvm_attach, &call, "Error", "QVM weapon launch role or ownership differs");
  call.launch.projectile.id = actor_id(2); call.launch.role = "rocket";
  EXPECT_RAISE(qvm_attach, &call, "Error", "QVM weapon launch role or ownership differs");
  EXPECT_RAISE(qvm_resume, &call, "Error", "QVM projectile already has an instance");
  CHECK(!inst.step(inst.ctx, body, 3.5).has_value); /* think2 clears inuse: retired */
  qa_app_sim_qvm_weapon_behavior_checkpoint cp2 = qa_app_sim_qvm_weapon_behavior_source_checkpoint(s);
  CHECK(cp2.binding_count == 1 && cp2.retired_count == 1 && cp2.retired[0].actor.slot == 2 && vec_eq(cp2.retired[0].trajectory.velocity, 11, 12, 13));
  CHECK(strcmp(cp.profile_digest, cp2.profile_digest) == 0);
  /* TS static restore over the saved module. */
  qa_app_sim_qvm_weapon_behavior_source *restored = qa_app_sim_qvm_weapon_behavior_source_create_restored(f.options, &cp2, saved_identity, SENTINEL);
  qa_weapon_behavior_instance again = qa_app_sim_qvm_weapon_behavior_source_resume(restored, actor_id(2));
  CHECK(vec_eq(again.initial.velocity, 11, 12, 13));
  CHECK(!again.step(again.ctx, body, 4).has_value);
  again.close(again.ctx);
  qa_app_sim_qvm_weapon_behavior_source_destroy(restored);
  /* Instance restore: old instances belong to the previous generation. */
  qa_app_sim_qvm_weapon_behavior_source_restore(s, &cp, saved_identity, SENTINEL);
  call.time = 5; EXPECT_RAISE(qvm_step, &call, "Error", "QVM trajectory instance is closed");
  inst.close(inst.ctx);
  qa_weapon_behavior_instance back = qa_app_sim_qvm_weapon_behavior_source_resume(s, actor_id(2));
  CHECK(vec_eq(back.initial.velocity, 11, 5, 13));
  /* A changed profile digest is refused before any state changes. */
  qa_app_sim_qvm_weapon_behavior_checkpoint bad = cp; bad.profile_digest[0] = bad.profile_digest[0] == '0' ? '1' : '0';
  call.options = f.options; call.checkpoint = &bad;
  EXPECT_RAISE(qvm_create_restored, &call, "Error", "Saved QVM weapon profile differs from the selected artifact declaration");
  /* Targets that leave are released through the source free function. */
  f.host.target_count = 0;
  qvm_checkpoint(&call);
  CHECK(entity_int(game, E1 + 516) == 0);
  back.close(back.ctx);
  qa_app_sim_qvm_weapon_behavior_source_close(s);
  EXPECT_RAISE(qvm_checkpoint, &call, "Error", "QVM weapon source is closed");
  qa_app_sim_qvm_weapon_behavior_checkpoint_free(&cp); qa_app_sim_qvm_weapon_behavior_checkpoint_free(&cp2);
  qa_app_sim_qvm_weapon_behavior_source_destroy(s);
  /* A profile whose stride differs from the located data fails initialization. */
  qvm_fixture g; qvm_fixture_init(&g);
  g.profile.entity_stride = STRIDE + 4; g.profile.field_health = 532;
  qvm_call bad_call; memset(&bad_call, 0, sizeof bad_call); bad_call.options = g.options;
  EXPECT_RAISE(qvm_create, &bad_call, "Error", "QVM weapon private layout differs from located source data");
  qvm_fixture_destroy(&g);
  qvm_fixture_destroy(&f);
}
static void entities_call(void *ctx) { free(qa_app_sim_qvm_weapon_initialization_entities(ctx)); }
static void test_qvm_initialization_entities(void) {
  char *out = qa_app_sim_qvm_weapon_initialization_entities(
      "{\"classname\" \"worldspawn\" \"message\" \"m\"}\n{\"classname\" \"info_player_deathmatch\" \"origin\" \"1 2 3\"}\n{\"classname\" \"weapon_rocketlauncher\"}\n{\"classname\" \"team_CTF_redspawn\"}");
  CHECK_STR(out, "{\n\"classname\" \"worldspawn\"\n\"message\" \"m\"\n}\n{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"1 2 3\"\n}\n{\n\"classname\" \"team_CTF_redspawn\"\n}\n");
  free(out);
  EXPECT_RAISE(entities_call, "{\"classname\" \"info_player_start\"}", "Error", "QVM component requires one authored worldspawn");
  EXPECT_RAISE(entities_call, "{\"classname\" \"worldspawn\"}{\"classname\" \"worldspawn\"}", "Error", "QVM component requires one authored worldspawn");
}

/* ---------------- rerelease (TS weapon-behavior.test.ts) ---------------- */
typedef struct retire_state {
  void *sentinel; qa_actor_id original, replacement;
  qa_app_sim_rerelease_slot_entry slots[4]; size_t slot_count;
  qa_app_sim_rerelease_binding_entry bindings[4]; size_t binding_count;
  qa_app_sim_rerelease_retired_entry retired[4]; size_t retired_count;
  int disposals;
} retire_state;
static qa_app_sim_rerelease_retire_state retire_view(retire_state *r) {
  qa_app_sim_rerelease_retire_state s = {r->slots, &r->slot_count, r->bindings, &r->binding_count, r->retired, &r->retired_count}; return s;
}
static void dispose(void *ctx, double slot, qa_app_sim_rerelease_binding_entry binding) {
  retire_state *r = ctx; CHECK_CTX(r->sentinel); r->disposals++;
  CHECK(slot == 5 && qa_same_actor(binding.actor, r->original));
  for (size_t i = 0; i < r->slot_count; i++) CHECK(!qa_same_actor(r->slots[i].actor, r->original));
  for (size_t i = 0; i < r->binding_count; i++) CHECK(r->bindings[i].slot != 5);
  r->slots[r->slot_count++] = (qa_app_sim_rerelease_slot_entry){r->replacement, 5};
  r->bindings[r->binding_count++] = (qa_app_sim_rerelease_binding_entry){5, r->replacement, "target", 2, {r->replacement, NULL, NULL}};
}
static void on_release(void *ctx, qa_owned_actor actor) { retire_state *r = ctx; CHECK_CTX(r->sentinel); qa_app_sim_retire_rerelease_weapon_actor(actor.id, retire_view(r), dispose, r); }
static void test_retire_rerelease_actor(void) {
  /* TS: native mirrors retire once across actor release, reentrant source-slot reuse and later attachment disposal. */
  qa_identity_owner identity; CHECK(qa_identity_owner_create(&identity, "native-retirement"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 16);
  qa_owned_actor original = qa_session_actor_registry_allocate(actors, "test:primary", "test:rocket");
  qa_owned_actor replacement = qa_session_actor_registry_allocate(actors, "test:primary", "test:target");
  retire_state r; memset(&r, 0, sizeof r); r.sentinel = SENTINEL; r.original = original.id; r.replacement = replacement.id;
  r.slots[r.slot_count++] = (qa_app_sim_rerelease_slot_entry){original.id, 5};
  r.bindings[r.binding_count++] = (qa_app_sim_rerelease_binding_entry){5, original.id, "projectile", 1, original};
  int64_t subscription = qa_session_actor_registry_on_release(actors, on_release, &r);
  qa_session_actor_registry_release(actors, original);
  qa_app_sim_retire_rerelease_weapon_actor(original.id, retire_view(&r), dispose, &r);
  CHECK(r.disposals == 1 && r.binding_count == 1 && qa_same_actor(r.bindings[0].actor, replacement.id));
  qa_session_actor_registry_off_release(actors, subscription); qa_session_actor_registry_release(actors, replacement);
  CHECK(r.disposals == 1);
  qa_session_actor_registry_destroy(actors); qa_identity_owner_destroy(&identity);
}
static void rr_entities_call(void *ctx) {
  static const char *const classes[] = {"worldspawn", "info_player_start"};
  qa_string_view_list list = {classes, 2}; free(qa_compat_q2_rerelease_weapon_initialization_entities(ctx, list));
}
static void test_rerelease_initialization_entities(void) {
  /* TS: native component initialization retains authored world and admission fields without taking world actor ownership. */
  const char *source = "{\"classname\" \"worldspawn\" \"custom_policy\" \"keep\"}\n{\"classname\" \"info_player_start\" \"origin\" \"10 20 30\" \"targetname\" \"entry\"}\n{\"classname\" \"func_door\" \"model\" \"*1\"}\n{\"classname\" \"custom_monster\" \"script\" \"authored\"}";
  static const char *const classes[] = {"worldspawn", "info_player_start"};
  qa_string_view_list list = {classes, 2};
  char *text = qa_compat_q2_rerelease_weapon_initialization_entities(source, list);
  size_t count = 0; qa_q1_entity *entities = qa_q1_parse_entities(text, &count);
  CHECK(count == 2);
  if (count == 2) {
    CHECK_STR(qa_q1_entity_value(&entities[0], "classname"), "worldspawn"); CHECK_STR(qa_q1_entity_value(&entities[1], "classname"), "info_player_start");
    CHECK_STR(qa_q1_entity_value(&entities[0], "custom_policy"), "keep"); CHECK(qa_q1_entity_value(&entities[1], "custom_policy") == NULL);
    CHECK(qa_q1_entity_value(&entities[0], "origin") == NULL); CHECK_STR(qa_q1_entity_value(&entities[1], "origin"), "10 20 30");
    CHECK(qa_q1_entity_value(&entities[0], "targetname") == NULL); CHECK_STR(qa_q1_entity_value(&entities[1], "targetname"), "entry");
  }
  qa_q1_entities_destroy(entities, count); free(text);
  EXPECT_RAISE(rr_entities_call, "{\"classname\" \"info_player_start\"}", NULL, "one authored worldspawn");
}
typedef struct provisioning { void *sentinel; qa_cvar_registry *source; double observed[4]; size_t observed_count; bool throw_inside; } provisioning;
static void provisioning_refresh(void *ctx) { provisioning *p = ctx; CHECK_CTX(p->sentinel); p->observed[p->observed_count++] = qa_cvar_registry_variable_value(p->source, "cheats"); }
static void *provisioning_run(void *ctx) {
  provisioning *p = ctx; CHECK_CTX(p->sentinel);
  if (p->throw_inside) {
    CHECK(qa_cvar_registry_variable_value(p->source, "maxclients") == 8); CHECK(qa_cvar_registry_variable_value(p->source, "cheats") == 1);
    qa_raise("Error", 0, 0, "authored command failure");
  }
  qa_cvar_snapshot set; qa_cvar_registry_set(p->source, "unrelated", "1", false, &set); qa_cvar_read_free(&set); return NULL;
}
static void provisioning_call(void *ctx) {
  provisioning *p = ctx; static const qa_native_weapon_cvar cvars[] = {{"cheats", "1"}};
  qa_compat_q2_with_rerelease_weapon_provisioning(cvars, 1, p->source, p, provisioning_refresh, provisioning_run);
}
static bool same_value(qa_save_value *a, qa_save_value *b) {
  size_t la = 0, lb = 0; uint8_t *ea = qa_encode_checkpoint_value(a, &la), *eb = qa_encode_checkpoint_value(b, &lb);
  bool same = la == lb && memcmp(ea, eb, la) == 0; free(ea); free(eb); return same;
}
static void test_rerelease_provisioning(void) {
  /* TS: native multiplayer provisioning scopes its declared private cheat capability and restores a pending latch on failure. */
  qa_identity_owner identity; CHECK(qa_identity_owner_create(&identity, "native-provisioning"));
  qa_cvar_registry_options o; memset(&o, 0, sizeof o);
  o.dialect = QA_COMMAND_DIALECT_Q2_RERELEASE; o.context.session = identity.session; o.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *source = qa_cvar_registry_create(&o);
  qa_cvar_snapshot r;
  qa_cvar_registry_register(source, "cheats", "0", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_LATCH, &r); qa_cvar_read_free(&r);
  qa_cvar_registry_register(source, "maxclients", "8", 0, &r); qa_cvar_read_free(&r);
  qa_cvar_registry_register(source, "unrelated", "0", 0, &r); qa_cvar_read_free(&r);
  qa_cvar_registry_set_server_active(source, true); qa_cvar_registry_set(source, "cheats", "2", false, &r); qa_cvar_read_free(&r);
  qa_save_value *before = qa_cvar_registry_capture_world_transfer_state(source);
  provisioning p; memset(&p, 0, sizeof p); p.sentinel = SENTINEL; p.source = source; p.throw_inside = true;
  EXPECT_RAISE(provisioning_call, &p, NULL, "authored command failure");
  qa_save_value *after = qa_cvar_registry_capture_world_transfer_state(source);
  CHECK(same_value(before, after)); CHECK(p.observed_count == 2 && p.observed[0] == 1 && p.observed[1] == 0);
  qa_save_value_free(after);
  p.throw_inside = false;
  EXPECT_RAISE(provisioning_call, &p, NULL, "undeclared source cvar");
  after = qa_cvar_registry_capture_world_transfer_state(source);
  CHECK(same_value(before, after));
  qa_save_value_free(after); qa_save_value_free(before);
  qa_cvar_registry_destroy(source); qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_quakec_source();
  test_quakec_errors();
  test_qvm_initialization_entities();
  test_qvm_source();
  test_retire_rerelease_actor();
  test_rerelease_initialization_entities();
  test_rerelease_provisioning();
  if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
  printf("test_weapon_behaviors: ok\n");
  return 0;
}
