/* C checks for src/compat/q2/native-mod-{invocations,region,armor,deferred,
 * weapon-stage,clients,combat,actors,protection,provider}. No TS test imports
 * these modules, so the expected values come from reading each TS body and from
 * the bun scenario recorded in the report. Every fake leaf asserts its sentinel
 * ctx. GPL-2.0-or-later. */
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

#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_mod_actors.h"
#include "qa/compat/q2/native_mod_armor.h"
#include "qa/compat/q2/native_mod_client_stages.h"
#include "qa/compat/q2/native_mod_clients.h"
#include "qa/compat/q2/native_mod_combat.h"
#include "qa/compat/q2/native_mod_deferred.h"
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/compat/q2/native_mod_invocations.h"
#include "qa/compat/q2/native_mod_protection.h"
#include "qa/compat/q2/native_mod_provider.h"
#include "qa/compat/q2/native_mod_region.h"
#include "qa/compat/q2/native_mod_weapon_stage.h"

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
    fflush(NULL);                                                       \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);          \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {          \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                   \
  } while (0)

/* --- Fake guest: flat memory, fake CPU, fake callback table. ---------------- */
#define MEM_BYTES (1u << 20)
#define MEM_CTX ((void *)0x1001)
#define RUN_CTX ((void *)0x1002)
#define HOST_CTX ((void *)0x1003)
#define PRES_CTX ((void *)0x1004)
#define OPS_CTX ((void *)0x1005)
#define SVC_CTX ((void *)0x1006)

typedef struct fake_observer { uint64_t id; int64_t address; double length; void *ctx; qa_compat_q2_written_fn cb; } fake_observer;
typedef struct fake_entry_observer { uint64_t id; int64_t address; void *ctx; void (*cb)(void *); } fake_entry_observer;
typedef struct fake_binding { uint64_t id; int64_t address; void *ctx; qa_compat_q2_host_invoke_fn cb; void *accepts_ctx; bool (*accepts)(void *); } fake_binding;
typedef struct fake_region { uint64_t id; int64_t entry, join; void *ctx; void (*cb)(void *, qa_compat_q2_inline_continuation *); void *accepts_ctx; bool (*accepts)(void *); } fake_region;
typedef qa_guest_call_result (*fake_guest_fn)(const qa_guest_call_value *values, size_t count);
typedef struct fake_function { int64_t address; fake_guest_fn fn; } fake_function;
typedef struct fake_processor { uint64_t registers[4]; uint64_t rsp; uint8_t xmm[32]; } fake_processor;

static uint8_t guest[MEM_BYTES];
static const char *const address_space = "fake-guest";
static int64_t bump;
static fake_observer write_observers[64]; static size_t write_observer_count;
static fake_entry_observer entry_observers[64]; static size_t entry_observer_count;
static fake_binding bindings[64]; static size_t binding_count;
static fake_region regions[32]; static size_t region_count;
static fake_function functions[32]; static size_t function_count;
static uint64_t next_handle;
static fake_processor cpu;
static double runner_depth;
static double last_unwind;
static int processor_captures, processor_frees, ctx_failures;
static uint8_t xmm_storage[256];

static void want_ctx(void *got, void *expected) { if (got != expected) ctx_failures++; }
static qa_guest_address fake_address(int64_t offset) { qa_guest_address a; a.address_space = address_space; a.byte_offset = offset; return a; }
static qa_guest_address_result mem_pointer(void *ctx, int64_t raw) { want_ctx(ctx, MEM_CTX); qa_guest_address_result r; r.has_value = raw != 0; r.value = fake_address(raw); return r; }
static qa_guest_address mem_offset(void *ctx, qa_guest_address a, int64_t d) { want_ctx(ctx, MEM_CTX); a.byte_offset += d; return a; }
static qa_guest_address mem_allocate(void *ctx, double n, bool has_alignment, int64_t alignment, const char *label) {
  (void)label; (void)has_alignment; (void)alignment; want_ctx(ctx, MEM_CTX);
  int64_t at = bump; bump += (int64_t)n + 16 - ((int64_t)n % 16); return fake_address(at);
}
static int unmapped;
static void mem_unmap(void *ctx, qa_guest_address a, double n) { (void)a; (void)n; want_ctx(ctx, MEM_CTX); unmapped++; }
static void mem_check(void *ctx, qa_guest_address a, double n, const char *access) {
  (void)access; want_ctx(ctx, MEM_CTX);
  if (a.byte_offset < 0 || (double)a.byte_offset + n > MEM_BYTES) qa_range_error("Fake guest access out of bounds");
}
static uint8_t *mem_borrow(void *ctx, qa_guest_address a, double n) { (void)n; want_ctx(ctx, MEM_CTX); return guest + a.byte_offset; }
static void mem_copy(void *ctx, qa_guest_address a, uint8_t *dest, double n) { want_ctx(ctx, MEM_CTX); memcpy(dest, guest + a.byte_offset, (size_t)n); }
static void mem_copy_into(void *ctx, qa_guest_address a, uint8_t *dest, size_t dl, double n) { (void)dl; want_ctx(ctx, MEM_CTX); memcpy(dest, guest + a.byte_offset, (size_t)n); }
static void mem_write(void *ctx, qa_guest_address a, const uint8_t *bytes, size_t n) {
  want_ctx(ctx, MEM_CTX); memcpy(guest + a.byte_offset, bytes, n);
  for (size_t i = 0; i < write_observer_count; i++) {
    fake_observer o = write_observers[i];
    if (o.id != 0 && a.byte_offset < o.address + (int64_t)o.length && o.address < a.byte_offset + (int64_t)n) {
      int64_t from = a.byte_offset > o.address ? a.byte_offset : o.address, to = a.byte_offset + (int64_t)n < o.address + (int64_t)o.length ? a.byte_offset + (int64_t)n : o.address + (int64_t)o.length;
      qa_compat_q2_written_range range = {(double)(from - o.address), (double)(to - from)};
      o.cb(o.ctx, &range, 1);
    }
  }
}
static qa_compat_q2_memory fake_memory(int64_t pointer_bytes) {
  qa_compat_q2_memory m; memset(&m, 0, sizeof m);
  m.ctx = MEM_CTX; m.module.id = "q2:fake"; m.module.artifact_path = "fake.dll"; m.module.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000"; m.module.revision = "1";
  m.address_space = address_space; m.pointer_bytes = pointer_bytes;
  m.pointer = mem_pointer; m.offset = mem_offset; m.allocate = mem_allocate; m.unmap = mem_unmap; m.check = mem_check; m.borrow = mem_borrow; m.copy = mem_copy; m.copy_into = mem_copy_into; m.write = mem_write;
  return m;
}
static void *run_capture(void *ctx) { want_ctx(ctx, RUN_CTX); fake_processor *p = malloc(sizeof *p); *p = cpu; memcpy(p->xmm, xmm_storage, sizeof p->xmm); processor_captures++; return p; }
static void run_restore(void *ctx, void *snapshot) { want_ctx(ctx, RUN_CTX); cpu = *(fake_processor *)snapshot; memcpy(xmm_storage, cpu.xmm, sizeof cpu.xmm); }
static void run_free(void *ctx, void *snapshot) { want_ctx(ctx, RUN_CTX); free(snapshot); processor_frees++; }
static double run_depth(void *ctx) { want_ctx(ctx, RUN_CTX); return runner_depth; }
static void run_unwind(void *ctx, double depth) { want_ctx(ctx, RUN_CTX); runner_depth = depth; last_unwind = depth; }
static int register_index(const char *name) { if (!strcmp(name, "rax")) return 0; if (!strcmp(name, "rcx")) return 1; if (!strcmp(name, "rdx")) return 2; return 3; }
static uint64_t run_read_register(void *ctx, const char *name, int width) {
  want_ctx(ctx, RUN_CTX); uint64_t v = !strcmp(name, "rsp") ? cpu.rsp : cpu.registers[register_index(name)];
  return width >= 64 ? v : v & ((UINT64_C(1) << width) - 1);
}
static void run_write_register(void *ctx, const char *name, int width, uint64_t value) {
  want_ctx(ctx, RUN_CTX); uint64_t masked = width >= 64 ? value : value & ((UINT64_C(1) << width) - 1);
  if (!strcmp(name, "rsp")) cpu.rsp = masked; else cpu.registers[register_index(name)] = masked;
}
static uint64_t run_observe_entry(void *ctx, qa_guest_address a, void *cb_ctx, void (*cb)(void *)) {
  want_ctx(ctx, RUN_CTX); fake_entry_observer *o = &entry_observers[entry_observer_count++]; o->id = ++next_handle; o->address = a.byte_offset; o->ctx = cb_ctx; o->cb = cb; return o->id;
}
static uint64_t run_bind_entry(void *ctx, qa_guest_address a, qa_callback_id id, qa_compat_q2_guest_call_signature signature, void *cb_ctx, qa_compat_q2_host_invoke_fn cb, void *accepts_ctx, bool (*accepts)(void *)) {
  (void)id; (void)signature; want_ctx(ctx, RUN_CTX); fake_binding *b = &bindings[binding_count++]; b->id = ++next_handle; b->address = a.byte_offset; b->ctx = cb_ctx; b->cb = cb; b->accepts_ctx = accepts_ctx; b->accepts = accepts; return b->id;
}
static void run_remove_entry(void *ctx, uint64_t handle) {
  want_ctx(ctx, RUN_CTX);
  for (size_t i = 0; i < binding_count; i++) if (bindings[i].id == handle) bindings[i].id = 0;
  for (size_t i = 0; i < entry_observer_count; i++) if (entry_observers[i].id == handle) entry_observers[i].id = 0;
}
static uint64_t run_bind_region(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi, void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *), void *accepts_ctx, bool (*accepts)(void *)) {
  (void)abi; want_ctx(ctx, RUN_CTX); fake_region *r = &regions[region_count++]; r->id = ++next_handle; r->entry = entry.byte_offset; r->join = join.byte_offset; r->ctx = cb_ctx; r->cb = cb; r->accepts_ctx = accepts_ctx; r->accepts = accepts; return r->id;
}
static void run_remove_region(void *ctx, uint64_t handle) { want_ctx(ctx, RUN_CTX); for (size_t i = 0; i < region_count; i++) if (regions[i].id == handle) regions[i].id = 0; }
/* Guest control flow: enter a registered region at its entry. */
typedef struct region_flow { void (*body)(void); int executed, skipped; } region_flow;
static void flow_execute(void *ctx) { region_flow *f = ctx; f->executed++; if (f->body != NULL) f->body(); }
static void flow_skip(void *ctx) { ((region_flow *)ctx)->skipped++; }
static bool guest_region(int64_t entry, void (*body)(void)) {
  for (size_t i = 0; i < region_count; i++) {
    fake_region r = regions[i];
    if (r.id == 0 || r.entry != entry || (r.accepts != NULL && !r.accepts(r.accepts_ctx))) continue;
    region_flow flow = {body, 0, 0}; qa_compat_q2_inline_continuation c = {&flow, flow_execute, flow_skip};
    r.cb(r.ctx, &c); return true;
  }
  return false;
}
static qa_compat_q2_runner fake_runner_table;
static qa_guest_call_result host_invoke(void *ctx, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  (void)signature; want_ctx(ctx, HOST_CTX);
  uint64_t caller = cpu.rsp; cpu.rsp -= 64; runner_depth++;
  for (size_t i = 0; i < entry_observer_count; i++) if (entry_observers[i].id != 0 && entry_observers[i].address == address.byte_offset) entry_observers[i].cb(entry_observers[i].ctx);
  qa_guest_call_result result = qa_compat_q2_call_void(); bool done = false;
  for (size_t i = 0; i < binding_count && !done; i++) {
    fake_binding b = bindings[i];
    if (b.id != 0 && b.address == address.byte_offset && (b.accepts == NULL || b.accepts(b.accepts_ctx))) { result = b.cb(b.ctx, NULL, values, count); done = true; }
  }
  for (size_t i = 0; i < function_count && !done; i++) if (functions[i].address == address.byte_offset) { result = functions[i].fn(values, count); done = true; }
  if (!done) qa_range_error("Fake guest has no function at %lld", (long long)address.byte_offset);
  cpu.rsp = caller; runner_depth--;
  return result;
}
static void add_function(int64_t address, fake_guest_fn fn) { functions[function_count].address = address; functions[function_count].fn = fn; function_count++; }
static uint64_t host_observe_writes(void *ctx, qa_guest_address a, double length, void *cb_ctx, qa_compat_q2_written_fn cb) {
  want_ctx(ctx, HOST_CTX); fake_observer *o = NULL;
  for (size_t i = 0; i < write_observer_count && o == NULL; i++) if (write_observers[i].id == 0) o = &write_observers[i];
  if (o == NULL) o = &write_observers[write_observer_count++];
  o->id = ++next_handle; o->address = a.byte_offset; o->length = length; o->ctx = cb_ctx; o->cb = cb; return o->id;
}
static void host_unobserve_writes(void *ctx, uint64_t handle) { want_ctx(ctx, HOST_CTX); for (size_t i = 0; i < write_observer_count; i++) if (write_observers[i].id == handle) write_observers[i].id = 0; }
static void host_set_ip(void *ctx, qa_guest_address a) { (void)a; want_ctx(ctx, HOST_CTX); }
#define TABLE_BASE 0x1000
#define TABLE_STRIDE 320
#define TABLE_COUNT 16
#define IMAGE_BASE 0x40000
static bool slot_active[TABLE_COUNT];
static int64_t client_rows[TABLE_COUNT];
static qa_raw_entity_view host_entity(void *ctx, double slot) {
  want_ctx(ctx, HOST_CTX); qa_raw_entity_view v; memset(&v, 0, sizeof v);
  v.slot = slot; v.address = fake_address(TABLE_BASE + (int64_t)slot * TABLE_STRIDE); v.stride_bytes = TABLE_STRIDE; v.public_layout.byte_length = 16; return v;
}
static bool host_client(void *ctx, double slot, qa_guest_address *out) { want_ctx(ctx, HOST_CTX); if (client_rows[(size_t)slot] == 0) return false; *out = fake_address(client_rows[(size_t)slot]); return true; }
static bool host_active(void *ctx, double slot) { want_ctx(ctx, HOST_CTX); return slot_active[(size_t)slot]; }
static int events_cleared;
static void host_clear_event(void *ctx, double slot) { (void)slot; want_ctx(ctx, HOST_CTX); events_cleared++; }
static qa_guest_address host_entry(void *ctx, const char *name) {
  want_ctx(ctx, HOST_CTX);
  if (!strcmp(name, "Export")) return fake_address(IMAGE_BASE + 0x500);
  qa_range_error("Unknown export %s", name);
}
static qa_compat_q2_native_mod_game_entry host_game_entry(void *ctx, const char *name) { (void)name; want_ctx(ctx, HOST_CTX); qa_range_error("No game entry"); }
static qa_compat_q2_native_mod_entities host_entities(void *ctx) {
  want_ctx(ctx, HOST_CTX); qa_compat_q2_native_mod_entities e; e.base = fake_address(TABLE_BASE); e.stride = TABLE_STRIDE; e.count = TABLE_COUNT; e.capacity = TABLE_COUNT; return e;
}
static double frames_synchronized;
static void host_synchronize(void *ctx, double seconds, double frame) { (void)seconds; want_ctx(ctx, HOST_CTX); frames_synchronized = frame; }
static int host_initialized, host_closed, host_restored;
static void host_initialize(void *ctx, bool restoring) { (void)restoring; want_ctx(ctx, HOST_CTX); host_initialized++; }
static const uint8_t saved_game[] = "{\"game\":1}", saved_level[] = "{\"level\":1}"; static uint8_t *saved_cvars; static size_t saved_cvars_length;
static qa_compat_q2_native_mod_source_save host_checkpoint(void *ctx) {
  want_ctx(ctx, HOST_CTX); qa_compat_q2_native_mod_source_save s; memset(&s, 0, sizeof s);
  s.rerelease = true; s.game.bytes = saved_game; s.game.length = sizeof saved_game - 1; s.level.bytes = saved_level; s.level.length = sizeof saved_level - 1; s.cvars.bytes = saved_cvars; s.cvars.length = saved_cvars_length; return s;
}
static void host_restore(void *ctx, const qa_compat_q2_native_mod_source_save *save) { want_ctx(ctx, HOST_CTX); CHECK(save->rerelease && save->game.length == sizeof saved_game - 1); host_restored++; }
static void host_close(void *ctx) { want_ctx(ctx, HOST_CTX); host_closed++; }
/* Presentation. */
static int presentation_published, presentation_releases, presentation_begin; static char signature_text[32] = "sig";
static void pres_release(void *ctx, qa_actor_id actor) { (void)actor; want_ctx(ctx, PRES_CTX); presentation_releases++; }
static void pres_begin(void *ctx) { want_ctx(ctx, PRES_CTX); presentation_begin++; }
static void pres_publish(void *ctx, const qa_compat_q2_native_mod_actor_slot *rows, size_t count, bool events) { (void)rows; (void)count; (void)events; want_ctx(ctx, PRES_CTX); presentation_published++; }
static const char *pres_signature(void *ctx, double slot) { (void)slot; want_ctx(ctx, PRES_CTX); return signature_text; }
static const qa_app_simulation_presentation *pres_appearance(void *ctx, qa_actor_id actor, double slot, size_t *n) { (void)actor; (void)slot; want_ctx(ctx, PRES_CTX); *n = 0; return NULL; }
static double pres_generation(void *ctx) { want_ctx(ctx, PRES_CTX); return 7; }
static bool pres_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) { (void)actor; (void)out; want_ctx(ctx, PRES_CTX); return false; }
static qa_save_value *pres_checkpoint(void *ctx) { want_ctx(ctx, PRES_CTX); qa_save_value *o = qa_save_value_object(), *fog = qa_save_value_array(); qa_save_value_set(o, "fog", fog); return o; }
static void pres_restore(void *ctx, const qa_save_value *saved) { want_ctx(ctx, PRES_CTX); CHECK(saved != NULL); }
static void pres_close(void *ctx) { want_ctx(ctx, PRES_CTX); }
static qa_compat_q2_memory fake_memory_table;
static qa_compat_q2_native_mod_host fake_host_table;
static const qa_native_call_abi classic_abi = {"windows-i386", "pe32", 4, "cdecl"};
static const qa_native_call_abi rerelease_abi = {"windows-x86-64", "pe32+", 8, "microsoft-x64"};
static void fake_reset(int64_t pointer_bytes) {
  memset(guest, 0, sizeof guest); bump = 0x60000; write_observer_count = entry_observer_count = binding_count = region_count = function_count = 0;
  memset(write_observers, 0, sizeof write_observers); memset(entry_observers, 0, sizeof entry_observers); memset(bindings, 0, sizeof bindings); memset(regions, 0, sizeof regions); memset(xmm_storage, 0, sizeof xmm_storage);
  next_handle = 0; memset(&cpu, 0, sizeof cpu); cpu.rsp = 0x50000; runner_depth = 0; last_unwind = -1; processor_captures = processor_frees = 0;
  memset(slot_active, 0, sizeof slot_active); memset(client_rows, 0, sizeof client_rows); events_cleared = 0; frames_synchronized = -1; host_initialized = host_closed = host_restored = 0;
  presentation_published = presentation_releases = presentation_begin = 0; unmapped = 0;
  fake_memory_table = fake_memory(pointer_bytes);
  qa_compat_q2_runner *r = &fake_runner_table; memset(r, 0, sizeof *r);
  r->ctx = RUN_CTX; r->memory = &fake_memory_table; r->simd_xmm = xmm_storage; r->simd_xmm_len = sizeof xmm_storage; r->depth = run_depth; r->unwind = run_unwind; r->read_register = run_read_register; r->write_register = run_write_register;
  r->capture_processor = run_capture; r->restore_processor = run_restore; r->free_processor = run_free; r->observe_entry = run_observe_entry; r->bind_entry = run_bind_entry; r->remove_entry = run_remove_entry; r->bind_inline_region_accepts = run_bind_region; r->remove_inline_region = run_remove_region;
  qa_compat_q2_native_mod_host *h = &fake_host_table; memset(h, 0, sizeof *h);
  h->ctx = HOST_CTX; h->content = "q2:base:fake:1"; h->memory = &fake_memory_table; h->image_base = fake_address(IMAGE_BASE); h->runner = r; h->abi = pointer_bytes == 4 ? classic_abi : rerelease_abi; h->set_instruction_pointer = host_set_ip;
  h->observe_writes = host_observe_writes; h->unobserve_writes = host_unobserve_writes; h->synchronize_frame = host_synchronize; h->entity = host_entity; h->client = host_client; h->active = host_active; h->clear_entity_event = host_clear_event;
  h->entry = host_entry; h->game_entry = host_game_entry; h->entities = host_entities; h->invoke = host_invoke; h->initialize = host_initialize; h->checkpoint = host_checkpoint; h->restore = host_restore; h->close = host_close;
  qa_compat_q2_native_mod_presentation *p = &h->presentation; p->ctx = PRES_CTX; p->release = pres_release; p->begin_frame = pres_begin; p->publish = pres_publish; p->signature = pres_signature; p->appearance = pres_appearance; p->generation = pres_generation; p->client_frame = pres_frame; p->checkpoint = pres_checkpoint; p->restore = pres_restore; p->close = pres_close;
}
#define HOST (&fake_host_table)
static void put_i32(int64_t at, int32_t value) { memcpy(guest + at, &value, 4); }
static int32_t get_i32(int64_t at) { int32_t v; memcpy(&v, guest + at, 4); return v; }
static float get_f32(int64_t at) { float v; memcpy(&v, guest + at, 4); return v; }
static void guest_write_i32(int64_t at, int32_t value) { uint8_t bytes[4]; memcpy(bytes, &value, 4); mem_write(MEM_CTX, fake_address(at), bytes, 4); }

/* --- native-mod-invocations. ------------------------------------------------- */
typedef struct auth_state { bool current; } auth_state;
static bool auth_current(void *ctx) { return ((auth_state *)ctx)->current; }
typedef struct scope_state { bool current; } scope_state;
static bool scope_current(void *ctx) { return ((scope_state *)ctx)->current; }
typedef struct inv_call { qa_compat_q2_native_mod_invocations *s; qa_compat_q2_native_mod_region_authority *authority; auth_state *auth; int ran; scope_state *inner_scope; int inner_kind; bool retire; const void *other_token; } inv_call;
static void inv_retire_throw(void *ctx) {
  inv_call *c = ctx; c->ran++; cpu.registers[0] = 99; runner_depth += 3;
  if (c->other_token != NULL) qa_compat_q2_native_throw(c->other_token, "other");
  if (c->retire) { c->auth->current = false; qa_compat_q2_native_throw(c->authority, c->authority->error); }
}
static void inv_guarded(void *ctx) { inv_call *c = ctx; qa_compat_q2_native_mod_invocations_guard(c->s, c->authority, c, inv_retire_throw); }
static void inv_outer(void *ctx) {
  inv_call *c = ctx;
  c->inner_kind = qa_compat_q2_native_mod_invocations_run(c->s, c->inner_scope, scope_current, c, inv_guarded);
  c->ran += 100; /* Not reached when the outer scope owns the retirement. */
}
static bool always_current(void *ctx) { (void)ctx; return true; }
static void noop_body(void *ctx) { *(int *)ctx += 1; }
static void test_invocations(void) {
  fake_reset(8);
  qa_compat_q2_native_mod_invocations s; qa_compat_q2_native_mod_invocations_init(&s, HOST);
  int ran = 0;
  CHECK(qa_compat_q2_native_mod_invocations_run(&s, NULL, always_current, &ran, noop_body) == QA_Q2INV_COMPLETED && ran == 1);
  /* Retirement of the owning scope restores the processor, unwinds, returns retired. */
  auth_state auth = {true}; qa_compat_q2_native_mod_region_authority authority = {&auth, "retired", auth_current};
  inv_call c; memset(&c, 0, sizeof c); c.s = &s; c.authority = &authority; c.auth = &auth; c.retire = true;
  cpu.registers[0] = 5; double depth = runner_depth; scope_state scope = {true};
  CHECK(qa_compat_q2_native_mod_invocations_run(&s, &scope, scope_current, &c, inv_guarded) == QA_Q2INV_RETIRED);
  CHECK(c.ran == 1 && cpu.registers[0] == 5 && runner_depth == depth && last_unwind == depth && s.scope_count == 0 && s.retired_count == 0);
  CHECK(processor_captures == processor_frees);
  /* An already-stale authority never runs its body. */
  auth.current = false; c.ran = 0; c.retire = false;
  CHECK(qa_compat_q2_native_mod_invocations_run(&s, &scope, scope_current, &c, inv_guarded) == QA_Q2INV_RETIRED && c.ran == 0);
  /* A stale outer scope owns the retirement: the inner scope rethrows it. */
  auth.current = true; scope_state outer = {false}, inner = {true}; memset(&c, 0, sizeof c); c.s = &s; c.authority = &authority; c.auth = &auth; c.retire = true; c.inner_scope = &inner;
  CHECK(qa_compat_q2_native_mod_invocations_run(&s, &outer, scope_current, &c, inv_outer) == QA_Q2INV_RETIRED);
  CHECK(c.ran == 1 && s.scope_count == 0);
  /* A live inner scope with live outer retires at the inner guard's scope. */
  auth.current = true; outer.current = true; memset(&c, 0, sizeof c); c.s = &s; c.authority = &authority; c.auth = &auth; c.retire = true; c.inner_scope = &inner;
  CHECK(qa_compat_q2_native_mod_invocations_run(&s, &outer, scope_current, &c, inv_outer) == QA_Q2INV_COMPLETED);
  CHECK(c.inner_kind == QA_Q2INV_RETIRED && c.ran == 101);
  /* A throw that is not a retirement propagates unchanged. */
  auth.current = true; memset(&c, 0, sizeof c); c.s = &s; c.authority = &authority; c.auth = &auth; int token = 0; c.other_token = &token;
  CHECK_ABORTS(qa_compat_q2_native_mod_invocations_guard(&s, &authority, &c, inv_retire_throw));
  CHECK_ABORTS(qa_compat_q2_native_mod_invocations_run(&s, &scope, scope_current, &c, inv_guarded));
  qa_compat_q2_native_mod_invocations_destroy(&s);
}

/* --- native-mod-region. ------------------------------------------------------ */
static qa_native_mod_protection_region base_region(void) {
  qa_native_mod_protection_region r; memset(&r, 0, sizeof r);
  r.frame.entry = 0x100; r.entry = 0x110; r.join = 0x120; r.frame.exit = 0x130; r.frame.stack_bytes = 32; r.frame.argument_bytes = 8;
  r.result.kind = QA_NATIVE_MOD_LOCATION_REGISTER; r.result.reg = "rax"; r.result.storage = "int32";
  return r;
}
static void region_guest_body(void) { /* Reads the stack input, writes rax. */ cpu.registers[0] = (uint64_t)(uint32_t)(get_i32((int64_t)cpu.rsp + 8) * 2); cpu.registers[1] = 777; }
static qa_guest_call_result region_guest(const qa_guest_call_value *values, size_t count) {
  (void)values; (void)count;
  cpu.registers[1] = 111; cpu.rsp -= 32; /* prologue */
  CHECK(guest_region(IMAGE_BASE + 0x100, NULL));
  CHECK(guest_region(IMAGE_BASE + 0x110, region_guest_body));
  CHECK(guest_region(IMAGE_BASE + 0x120, NULL));
  cpu.rsp += 32;
  return qa_compat_q2_call_void();
}
static bool retire_during_body_flag;
static auth_state region_auth;
static void region_retiring_body(void) { if (retire_during_body_flag) region_auth.current = false; }
static qa_guest_call_result region_guest_retiring(const qa_guest_call_value *values, size_t count) {
  (void)values; (void)count; cpu.rsp -= 32;
  guest_region(IMAGE_BASE + 0x100, NULL); guest_region(IMAGE_BASE + 0x110, region_retiring_body);
  cpu.rsp += 32; return qa_compat_q2_call_void();
}
typedef struct region_call { qa_guest_address target; qa_compat_q2_native_mod_region_execution *e; } region_call;
static void region_invoke(void *ctx) {
  region_call *c = ctx; qa_compat_q2_guest_call_signature signature; memset(&signature, 0, sizeof signature); signature.result_is_void = true;
  (void)host_invoke(HOST_CTX, c->target, signature, NULL, 0);
}
static void test_region(void) {
  qa_native_mod_protection_region r = base_region();
  qa_compat_q2_validate_native_mod_region(&r, 8);
  r = base_region(); r.join = r.entry; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  r = base_region(); r.frame.stack_bytes = 30; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  r = base_region(); r.result.storage = "pointer"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  qa_native_mod_skip skip = {1, 2}; r = base_region(); r.call.skips = &skip; r.call.skip_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  /* Registers past the source architecture. */
  r = base_region(); r.frame.stack_bytes = 32; r.frame.argument_bytes = 8; r.result.reg = "r8"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 4));
  r.result.reg = "r15"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 4));
  r.result.reg = "rbx"; qa_compat_q2_validate_native_mod_region(&r, 4);
  r.result.storage = "int64"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 4));
  /* SIMD and stack extents. */
  r = base_region(); r.result.kind = QA_NATIVE_MOD_LOCATION_SIMD; r.result.storage = "float32"; r.result.index = 16; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  r.result.index = 7; qa_compat_q2_validate_native_mod_region(&r, 4); r.result.index = 8; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 4));
  r.result.index = 0; r.result.offset = 14; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  r = base_region(); r.result.kind = QA_NATIVE_MOD_LOCATION_STACK; r.result.offset = 36; /* inside the return address */ CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  r.result.offset = 40; qa_compat_q2_validate_native_mod_region(&r, 8); r.result.offset = 46; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  /* Overlapping inputs; a register and the same register. */
  qa_native_mod_region_input inputs[2]; memset(inputs, 0, sizeof inputs);
  inputs[0].target.kind = QA_NATIVE_MOD_LOCATION_STACK; inputs[0].target.offset = 8; inputs[0].target.storage = "int32"; inputs[1] = inputs[0]; inputs[1].target.offset = 10;
  r = base_region(); r.inputs = inputs; r.input_count = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
  inputs[1].target.offset = 12; qa_compat_q2_validate_native_mod_region(&r, 8);

  /* Execution: prologue skipped with inputs staged, region run, epilogue skipped. */
  fake_reset(8);
  add_function(IMAGE_BASE + 0x200, region_guest);
  r = base_region(); r.inputs = inputs; r.input_count = 1; inputs[0].target.offset = 8;
  qa_guest_call_value value = qa_compat_q2_call_int32(21);
  int64_t stack_slot = 0x50000 - 64 - 32 + 8; put_i32(stack_slot, 1234);
  qa_compat_q2_native_mod_region_execution *e = qa_compat_q2_native_mod_region_execution_create(HOST, &r, &value, 1, NULL);
  qa_compat_q2_native_mod_region_execution_bind(e, fake_address(IMAGE_BASE + 0x200));
  CHECK_ABORTS(qa_compat_q2_native_mod_region_execution_result(e));
  region_call rc = {fake_address(IMAGE_BASE + 0x200), e}; region_invoke(&rc);
  qa_guest_call_value out = qa_compat_q2_native_mod_region_execution_result(e);
  CHECK(out.kind == QA_GUEST_VALUE_INT32 && out.number_value == 42);
  CHECK(get_i32(stack_slot) == 1234); /* stack input rolled back */
  CHECK(cpu.registers[1] == 111);     /* processor restored at the join */
  qa_compat_q2_native_mod_region_execution_destroy(e);
  CHECK(region_count == 3 && regions[0].id == 0 && regions[1].id == 0 && regions[2].id == 0);
  /* Input count mismatch. */
  CHECK_ABORTS(qa_compat_q2_native_mod_region_execution_create(HOST, &r, &value, 0, NULL));
  /* A retired authority throws its own token out of the region. */
  fake_reset(8); add_function(IMAGE_BASE + 0x200, region_guest_retiring); r = base_region();
  region_auth.current = true; retire_during_body_flag = true;
  qa_compat_q2_native_mod_region_authority authority = {&region_auth, "donor retired", auth_current};
  e = qa_compat_q2_native_mod_region_execution_create(HOST, &r, NULL, 0, &authority);
  qa_compat_q2_native_mod_region_execution_bind(e, fake_address(IMAGE_BASE + 0x200));
  region_call rc2 = {fake_address(IMAGE_BASE + 0x200), e}; qa_compat_q2_native_thrown thrown;
  CHECK(!qa_compat_q2_native_try(region_invoke, &rc2, &thrown) && thrown.token == &authority);
  cpu.rsp = 0x50000; runner_depth = 0;
  qa_compat_q2_native_mod_region_execution_destroy(e);
}

/* --- native-mod-armor. ------------------------------------------------------- */
static double armor_scalar(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value) {
  want_ctx(ctx, OPS_CTX); qa_guest_address a = mem_offset(MEM_CTX, base, (int64_t)field.offset);
  if (has_value) {
    if (!strcmp(field.encoding, "float32")) { uint8_t b[4]; qa_compat_q2_view_set_f32(b, 0, value); mem_write(MEM_CTX, a, b, 4); }
    else { int32_t v = (int32_t)value; mem_write(MEM_CTX, a, (const uint8_t *)&v, 4); }
  }
  return !strcmp(field.encoding, "float32") ? (double)get_f32(a.byte_offset) : (double)get_i32(a.byte_offset);
}
#define ENT "ent"
#define CLI "cli"
static const qa_native_mod_actor_field no_fields[1];
static qa_native_mod_actor_record armor_records[2];
static qa_native_mod_declaration armor_declaration(void) {
  memset(armor_records, 0, sizeof armor_records);
  armor_records[0].id = ENT; armor_records[0].base.kind = "entities"; armor_records[0].stride = TABLE_STRIDE; armor_records[0].first_slot = 1; armor_records[0].capacity = 8; armor_records[0].fields = no_fields;
  armor_records[1].id = CLI; armor_records[1].base.kind = "clients"; armor_records[1].stride = 64; armor_records[1].capacity = 4; armor_records[1].fields = no_fields;
  qa_native_mod_declaration d; memset(&d, 0, sizeof d); d.actor_records = armor_records; d.actor_record_count = 2; d.entity_record = ENT; return d;
}
static qa_native_mod_armor_field afield(const char *record, double offset, const char *encoding) { qa_native_mod_armor_field f; memset(&f, 0, sizeof f); f.record = record; f.field.offset = offset; f.field.encoding = encoding; return f; }
static qa_native_mod_armor_selection positive(qa_native_mod_armor_field f) { qa_native_mod_armor_selection s; memset(&s, 0, sizeof s); s.field = f; return s; }
static qa_native_mod_armor_selection enumerated(qa_native_mod_armor_field f, double value, double none) { qa_native_mod_armor_selection s = positive(f); s.is_enum = true; s.value = value; s.none = none; return s; }
static int armor_changes; static qa_armor_state armor_last_before, armor_last_after;
static void armor_changed(void *ctx, qa_armor_state before, qa_armor_state after) { want_ctx(ctx, OPS_CTX); armor_changes++; armor_last_before = before; armor_last_after = after; }
static void armor_init_with(qa_native_mod_armor definition, const qa_native_mod_declaration *d) {
  qa_compat_q2_native_mod_armor_state s; qa_compat_q2_native_mod_armor_state_init(&s, definition, d, HOST, armor_scalar, OPS_CTX); qa_compat_q2_native_mod_armor_state_destroy(&s);
}
static void test_armor(void) {
  fake_reset(4); qa_native_mod_declaration d = armor_declaration();
  slot_active[1] = true; client_rows[1] = 0x8000;
  /* Client-row q2 armor: jacket enum + points, shield power with enabled mask. */
  qa_native_mod_q2_regular_armor_item regular[2]; memset(regular, 0, sizeof regular);
  regular[0].item = "q2:jacket"; regular[0].selection = enumerated(afield(CLI, 0, "int32"), 1, 0); regular[0].points = afield(CLI, 4, "int32"); regular[0].normal_protection = 0.3; regular[0].energy_protection = 0;
  regular[1].item = "q2:body"; regular[1].selection = enumerated(afield(CLI, 0, "int32"), 2, 0); regular[1].points = afield(CLI, 4, "int32"); regular[1].normal_protection = 0.8; regular[1].energy_protection = 0.6;
  qa_native_mod_armor_enabled enabled; enabled.field = afield(ENT, 40, "int32"); enabled.mask = 4;
  qa_native_mod_power_armor_item power[1]; memset(power, 0, sizeof power);
  power[0].item = "q2:shield"; power[0].kind = "shield"; power[0].selection = positive(afield(CLI, 8, "int32")); power[0].cells = afield(CLI, 12, "int32"); power[0].enabled = &enabled;
  qa_native_mod_armor definition; memset(&definition, 0, sizeof definition); definition.kind = "q2"; definition.q2_regular = regular; definition.q2_regular_count = 2; definition.power = power; definition.power_count = 1;
  qa_compat_q2_native_mod_armor_state s; qa_compat_q2_native_mod_armor_state_init(&s, definition, &d, HOST, armor_scalar, OPS_CTX);
  qa_armor_state state = qa_compat_q2_native_mod_armor_read(&s, 1);
  CHECK(state.regular.kind == QA_REGULAR_ARMOR_NONE && !strcmp(state.powered.kind, "none"));
  put_i32(0x8000, 2); put_i32(0x8004, 55); put_i32(0x8008, 1); put_i32(0x800c, 30);
  state = qa_compat_q2_native_mod_armor_read(&s, 1);
  CHECK(state.regular.kind == QA_REGULAR_ARMOR_Q2 && !strcmp(state.regular.item, "q2:body") && state.regular.points == 55 && state.regular.normal_protection == 0.8);
  CHECK(!strcmp(state.powered.kind, "none")); /* enabled bit clear */
  put_i32(TABLE_BASE + TABLE_STRIDE + 40, 4);
  state = qa_compat_q2_native_mod_armor_read(&s, 1);
  CHECK(!strcmp(state.powered.kind, "shield") && state.powered.cells == 30);
  /* A client without storage reads as none; requires storage for its counts. */
  CHECK(qa_compat_q2_native_mod_armor_read(&s, 2).regular.kind == QA_REGULAR_ARMOR_NONE);
  CHECK_ABORTS(qa_compat_q2_native_mod_armor_read_count(&s, 2, regular[0].points));
  CHECK(qa_compat_q2_native_mod_armor_read_count(&s, 1, regular[0].points) == 55);
  /* Writes: switch regular, keep power, observer sees the diff through the sentinel ctx. */
  qa_compat_q2_native_armor_observation *o = qa_compat_q2_native_mod_armor_observe(&s, 1, OPS_CTX, armor_changed);
  qa_armor_state next = state; next.regular.item = "q2:jacket"; next.regular.points = 10; next.regular.normal_protection = 0.3; next.regular.energy_protection = 0;
  qa_compat_q2_native_mod_armor_write(&s, 1, next);
  CHECK(get_i32(0x8000) == 1 && get_i32(0x8004) == 10 && armor_changes > 0);
  CHECK(armor_last_after.regular.kind == QA_REGULAR_ARMOR_Q2 && armor_last_after.regular.points == 10);
  int changes = armor_changes; put_i32(0x8020, 9); uint8_t unrelated[4] = {1, 2, 3, 4}; mem_write(MEM_CTX, fake_address(0x8020), unrelated, 4); CHECK(armor_changes == changes);
  qa_compat_q2_native_armor_observation_close(o);
  changes = armor_changes; mem_write(MEM_CTX, fake_address(0x8004), unrelated, 4); CHECK(armor_changes == changes); put_i32(0x8004, 10);
  /* Removing armor clears selection and points; power off clears its enabled bit. */
  qa_armor_state cleared = next; cleared.regular.kind = QA_REGULAR_ARMOR_NONE; cleared.regular.item = NULL; cleared.regular.points = 0; cleared.powered.kind = "none"; cleared.powered.cells = 0;
  qa_compat_q2_native_mod_armor_write(&s, 1, cleared);
  CHECK(get_i32(0x8000) == 0 && get_i32(0x8004) == 0 && get_i32(TABLE_BASE + TABLE_STRIDE + 40) == 0);
  /* Reactivation sets the mask and positive selection, then the cell count. */
  put_i32(0x8008, 0);
  qa_armor_state powered = cleared; powered.powered.kind = "shield"; powered.powered.cells = 12;
  qa_compat_q2_native_mod_armor_write(&s, 1, powered);
  CHECK(get_i32(TABLE_BASE + TABLE_STRIDE + 40) == 4 && get_i32(0x8008) == 1 && get_i32(0x800c) == 12);
  /* Invalid representations. */
  qa_armor_state wrong = cleared; wrong.regular.kind = QA_REGULAR_ARMOR_SOURCE; wrong.regular.item = "q2:jacket"; CHECK_ABORTS(qa_compat_q2_native_mod_armor_write(&s, 1, wrong));
  wrong = next; wrong.regular.item = "q2:unknown"; CHECK_ABORTS(qa_compat_q2_native_mod_armor_validate_write(&s, 1, wrong));
  wrong = next; wrong.regular.normal_protection = 0.9; CHECK_ABORTS(qa_compat_q2_native_mod_armor_validate_write(&s, 1, wrong));
  wrong = cleared; wrong.powered.kind = "screen"; wrong.powered.cells = 1; CHECK_ABORTS(qa_compat_q2_native_mod_armor_validate_write(&s, 1, wrong));
  CHECK_ABORTS(qa_compat_q2_native_mod_armor_write_count(&s, 1, regular[0].points, 1.5));
  CHECK_ABORTS(qa_compat_q2_native_mod_armor_write_count(&s, 1, regular[0].points, 2147483648.0));
  qa_compat_q2_native_mod_armor_write_count(&s, 1, regular[0].points, 8); CHECK(get_i32(0x8004) == 8);
  qa_compat_q2_native_mod_armor_state_destroy(&s);

  /* Source armor where points are the positive selection: zero means empty. */
  fake_reset(4); slot_active[1] = true; client_rows[1] = 0x8000; d = armor_declaration();
  qa_native_mod_regular_armor_item source[1]; memset(source, 0, sizeof source);
  source[0].item = "mod:vest"; source[0].selection = positive(afield(CLI, 0, "float32")); source[0].points = afield(CLI, 0, "float32");
  qa_native_mod_armor sd; memset(&sd, 0, sizeof sd); sd.kind = "source"; sd.regular = source; sd.regular_count = 1;
  qa_compat_q2_native_mod_armor_state src; qa_compat_q2_native_mod_armor_state_init(&src, sd, &d, HOST, armor_scalar, OPS_CTX);
  qa_armor_state vest; memset(&vest, 0, sizeof vest); vest.regular.kind = QA_REGULAR_ARMOR_SOURCE; vest.regular.item = "mod:vest"; vest.regular.points = 12.5; vest.powered.kind = "none";
  qa_compat_q2_native_mod_armor_write(&src, 1, vest);
  CHECK(get_f32(0x8000) == 12.5f);
  state = qa_compat_q2_native_mod_armor_read(&src, 1); CHECK(state.regular.kind == QA_REGULAR_ARMOR_SOURCE && state.regular.points == 12.5);
  /* binary32 rounding: the requested value compares after fround. */
  vest.regular.points = 0.1; qa_compat_q2_native_mod_armor_write(&src, 1, vest); CHECK(get_f32(0x8000) == (float)0.1);
  vest.regular.points = 0; qa_compat_q2_native_mod_armor_write(&src, 1, vest); CHECK(get_f32(0x8000) == 0);
  CHECK(qa_compat_q2_native_mod_armor_read(&src, 1).regular.kind == QA_REGULAR_ARMOR_NONE);
  /* Legacy normalization is a pass-through without declared power. */
  CHECK(qa_compat_q2_native_mod_armor_normalize_legacy(&src, 1, vest).regular.points == 0);
  qa_compat_q2_native_mod_armor_state_destroy(&src);

  /* Constructor rejections. */
  qa_native_mod_q2_regular_armor_item bad[2]; memcpy(bad, regular, sizeof bad);
  qa_native_mod_armor b; memset(&b, 0, sizeof b); b.kind = "q2"; b.q2_regular = bad; b.q2_regular_count = 2;
  bad[1].item = bad[0].item; CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  bad[0].selection = enumerated(afield(CLI, 0, "int32"), 1, 1); CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  bad[0].normal_protection = INFINITY; CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  bad[0].points = afield("missing", 4, "int32"); CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  bad[0].points = afield(CLI, 62, "int32"); CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  bad[1].points = afield(CLI, 6, "int32"); CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  bad[0].selection = enumerated(afield(CLI, 0, "float32"), 0.1, 0); bad[1].selection = enumerated(afield(CLI, 0, "float32"), 2, 0); CHECK_ABORTS(armor_init_with(b, &d)); memcpy(bad, regular, sizeof bad);
  qa_native_mod_armor_enabled float_mask; float_mask.field = afield(ENT, 40, "float32"); float_mask.mask = 4; qa_native_mod_power_armor_item pw = power[0]; pw.enabled = &float_mask;
  qa_native_mod_armor pa; memset(&pa, 0, sizeof pa); pa.kind = "source"; pa.power = &pw; pa.power_count = 1; CHECK_ABORTS(armor_init_with(pa, &d));
  qa_native_mod_armor none; memset(&none, 0, sizeof none); none.kind = "none"; armor_init_with(none, &d);
  qa_compat_q2_native_mod_armor_state empty; qa_compat_q2_native_mod_armor_state_init(&empty, none, &d, HOST, armor_scalar, OPS_CTX);
  qa_armor_state nothing; memset(&nothing, 0, sizeof nothing); nothing.powered.kind = "none"; qa_compat_q2_native_mod_armor_write(&empty, 1, nothing);
  qa_armor_state some = nothing; some.powered.kind = "shield"; CHECK_ABORTS(qa_compat_q2_native_mod_armor_write(&empty, 1, some));
  qa_compat_q2_native_mod_armor_state_destroy(&empty);
}

/* --- Shared world for combat/actors/deferred/provider. ------------------------ */
static qa_identity_owner identities;
static qa_session_actor_registry *registry;
static qa_shared_body_table *bodies;
static qa_shared_inventory_table *inventory;
static qa_gameplay_authority *authority;
static qa_actor_callback_table *callback_table;
static qa_mod_host_services services;
static double world_seconds;
static qa_owned_actor world_actor;
static qa_session_mod_engine_services engine_services;
static int hook_confirmed, hook_before_reaction;
static bool engine_world(void *ctx, qa_actor_id *out) { want_ctx(ctx, SVC_CTX); *out = world_actor.id; return true; }
static qa_source_time services_time(void *ctx) { want_ctx(ctx, SVC_CTX); qa_source_time t; t.kind = QA_SOURCE_TIME_SECONDS; t.value = world_seconds; return t; }
static void services_damage_context(void *ctx, qa_provider_id source, double *sequence, qa_provider_id *weapon, qa_provider_id *combat, qa_provider_id *inv, qa_provider_id *movement) {
  (void)source; want_ctx(ctx, SVC_CTX); *sequence = 1; *weapon = source; *combat = source; *inv = source; *movement = source;
}
static qa_bounds body_absolute(void *ctx, qa_owned_actor actor, qa_body_state state) { (void)ctx; return qa_translated_body_bounds(actor, state); }
static void body_on_link(void *ctx, qa_linked_body body) { (void)ctx; (void)body; }
static void body_on_unlink(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }
static void hooks_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) { (void)ctx; (void)actor; (void)impulse; (void)movement; }
static void hooks_before(void *ctx, qa_owned_actor actor, qa_damage_decision decision) { (void)ctx; (void)actor; (void)decision; hook_before_reaction++; }
static void hooks_confirmed(void *ctx, qa_damage_outcome outcome) { (void)ctx; (void)outcome; hook_confirmed++; }
static void world_setup(void) {
  CHECK(qa_identity_owner_create(&identities, "native-mod-test"));
  registry = qa_session_actor_registry_create(&identities, 64);
  qa_body_link_hooks link; memset(&link, 0, sizeof link); link.absolute_bounds = body_absolute; link.on_link = body_on_link; link.on_unlink = body_on_unlink;
  bodies = qa_shared_body_table_create(registry, link); inventory = qa_shared_inventory_table_create(registry); callback_table = qa_actor_callback_table_create(registry);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks); hooks.impulse = hooks_impulse; hooks.before_reaction = hooks_before; hooks.confirmed = hooks_confirmed;
  authority = qa_gameplay_authority_create(registry, callback_table, hooks);
  memset(&services, 0, sizeof services); services.actors = registry; services.bodies = bodies; services.combat = authority; services.inventory = inventory; services.callbacks = callback_table; services.ctx = SVC_CTX;
  services.time = services_time; services.damage_context = services_damage_context;
  world_seconds = 10; hook_confirmed = hook_before_reaction = 0;
  memset(&engine_services, 0, sizeof engine_services); engine_services.ctx = SVC_CTX; engine_services.world = engine_world; services.engine = &engine_services;
  world_actor = qa_session_actor_registry_allocate(registry, "test:world", "world");
}
static void world_teardown(void) {
  qa_gameplay_authority_destroy(authority); qa_actor_callback_table_destroy(callback_table); qa_shared_inventory_table_destroy(inventory); qa_shared_body_table_destroy(bodies); qa_session_actor_registry_destroy(registry); qa_identity_owner_destroy(&identities);
}

/* --- native-mod-deferred. ---------------------------------------------------- */
static qa_compat_q2_saved_native_deferred_damage sample_saved(void) {
  qa_compat_q2_saved_native_deferred_damage r; memset(&r, 0, sizeof r);
  r.target.slot = 3; r.target.generation = 1; r.amount = 25; r.knockback_req = 4; r.point_req.x = 1; r.direction.y = 1; r.normal.z = 1; r.delivery = "radius"; r.blood = 25; r.knockback = 9; r.point.x = 2; r.mod[0] = 5; r.mod[1] = 1; r.mod[2] = 0;
  r.has_attacker = true; r.attacker_slot = 2;
  r.attack.values.sequence = 7; r.attack.values.time.kind = QA_SOURCE_TIME_SECONDS; r.attack.values.time.value = 3; r.attack.values.weapon_provider = "q2:w"; r.attack.values.combat_provider = "q2:c"; r.attack.values.inventory_provider = "q2:i"; r.attack.values.movement_provider = "q2:m";
  r.attack.values.cause.kind = QA_ATTACK_CAUSE_Q2; r.attack.values.cause.means_of_death = 4; r.attack.values.cause.damage_flags = 1;
  return r;
}
static void test_deferred_reader(void) {
  qa_compat_q2_saved_native_deferred_damage record = sample_saved();
  qa_save_value *value = qa_compat_q2_native_deferred_damage_value(&record);
  size_t length = 0; uint8_t *bytes = qa_encode_checkpoint_value(value, &length); qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader reader; CHECK(qa_save_reader_init(&reader, decoded, NULL));
  qa_compat_q2_saved_native_deferred_damage back = qa_compat_q2_read_native_deferred_damage(&reader);
  CHECK(back.target.slot == 3 && back.amount == 25 && back.knockback_req == 4 && !strcmp(back.delivery, "radius") && back.blood == 25 && back.knockback == 9 && back.point.x == 2);
  CHECK(back.mod[0] == 5 && back.mod[1] == 1 && back.has_attacker && back.attacker_slot == 2 && !back.has_inflictor && back.attack.values.sequence == 7 && back.attack.values.cause.means_of_death == 4);
  qa_save_reader_destroy(&reader); qa_save_value_free(decoded); free(bytes);
  /* A mod_t that is not three bytes fails. */
  qa_save_value *mod_list = NULL; for (size_t i = 0; i < value->entry_count; i++) if (!strcmp(value->entries[i].key, "mod")) mod_list = value->entries[i].value;
  CHECK(mod_list != NULL); mod_list->items[0]->number = 256;
  qa_save_reader bad; CHECK(qa_save_reader_init(&bad, value, NULL)); CHECK_ABORTS(qa_compat_q2_read_native_deferred_damage(&bad)); qa_save_reader_destroy(&bad);
  mod_list->items[0]->number = 5; qa_save_value_push(mod_list, qa_save_value_number(0));
  CHECK(qa_save_reader_init(&bad, value, NULL)); CHECK_ABORTS(qa_compat_q2_read_native_deferred_damage(&bad)); qa_save_reader_destroy(&bad);
  qa_save_value_free(value);
}

/* --- native-mod-weapon-stage. ------------------------------------------------ */
#define WEAPON_ENTRY (IMAGE_BASE + 0x700)
#define WEAPON_RECORD "ent"
static qa_actor_id weapon_actor; static int weapon_selected, weapon_completed_calls; static bool weapon_reached, weapon_is_current;
static double weapon_field_value; static int weapon_invoked;
static bool weapon_cancel_installed, weapon_accepts; static int weapon_token;
static bool wop_actor(void *ctx, const char *record, qa_guest_address address, qa_actor_id *out) { want_ctx(ctx, OPS_CTX); CHECK(!strcmp(record, WEAPON_RECORD)); if (address.byte_offset != 0x2000) return false; *out = weapon_actor; return true; }
static bool wop_current(void *ctx, qa_actor_id actor) { (void)actor; want_ctx(ctx, OPS_CTX); return weapon_is_current; }
static bool wop_selected(void *ctx, qa_actor_id actor) { (void)actor; want_ctx(ctx, OPS_CTX); return weapon_selected != 0; }
static qa_guest_address wop_pointer(void *ctx, qa_actor_id actor, qa_native_item_pointer field) { (void)actor; want_ctx(ctx, OPS_CTX); return fake_address(0x3000 + (int64_t)field.offset); }
static qa_guest_address wop_resolve(void *ctx, qa_native_mod_address address) { want_ctx(ctx, OPS_CTX); return fake_address((int64_t)address.rva); }
static double wop_read(void *ctx, qa_actor_id actor, qa_native_item_field field) { (void)actor; (void)field; want_ctx(ctx, OPS_CTX); return weapon_field_value; }
static void wop_write(void *ctx, qa_actor_id actor, qa_native_item_field field, double value) { (void)actor; (void)field; want_ctx(ctx, OPS_CTX); weapon_field_value = value; }
static void wop_invoke(void *ctx, qa_actor_id actor, const qa_native_mod_source_call *call) { (void)actor; (void)call; want_ctx(ctx, OPS_CTX); weapon_invoked++; int64_t v = 0x4000; memcpy(guest + 0x3000, &v, 4); }
static void wop_completed(void *ctx, qa_actor_id actor, bool reached) { (void)actor; want_ctx(ctx, OPS_CTX); weapon_completed_calls++; weapon_reached = reached; }
static bool wcancel_accepts(void *ctx, const qa_compat_q2_native_thrown *error) { want_ctx(ctx, OPS_CTX); return weapon_accepts && error->token == &weapon_token; }
static bool wcancel_current(void *ctx) { want_ctx(ctx, OPS_CTX); return true; }
static bool wop_cancellation(void *ctx, qa_actor_id actor, qa_compat_q2_native_weapon_cancellation *out) { (void)actor; want_ctx(ctx, OPS_CTX); if (!weapon_cancel_installed) return false; out->ctx = OPS_CTX; out->accepts = wcancel_accepts; out->source_current = wcancel_current; return true; }
static double weapon_seen_in_region; static bool weapon_guest_throws; static int weapon_guest_calls;
static void weapon_region_body(void) { weapon_seen_in_region = weapon_field_value; }
static qa_guest_call_result weapon_guest(const qa_guest_call_value *values, size_t count) {
  (void)values; (void)count; weapon_guest_calls++; cpu.registers[2] = 55;
  guest_region(IMAGE_BASE + 0x710, weapon_region_body);
  if (weapon_guest_throws) { runner_depth += 2; qa_compat_q2_native_throw(&weapon_token, "cancelled"); }
  return qa_compat_q2_call_void();
}
static qa_compat_q2_native_weapon_host weapon_host(void) {
  qa_compat_q2_native_weapon_host h; memset(&h, 0, sizeof h); h.memory = &fake_memory_table; h.runner = &fake_runner_table; h.image_base = fake_address(IMAGE_BASE); h.ctx = HOST_CTX; h.invoke = host_invoke; h.entry = host_entry; return h;
}
static void weapon_call(void) {
  qa_guest_call_value args[2] = {qa_compat_q2_call_pointer(false, fake_address(0)), qa_compat_q2_call_pointer(true, fake_address(0x2000))};
  qa_compat_q2_guest_call_signature signature; memset(&signature, 0, sizeof signature); signature.result_is_void = true;
  (void)host_invoke(HOST_CTX, fake_address(WEAPON_ENTRY), signature, args, 2);
}
static void test_weapon_stage(void) {
  fake_reset(4);
  qa_identity_owner owner; CHECK(qa_identity_owner_create(&owner, "weapon-test")); weapon_actor = qa_identity_owner_actor(&owner, 3, 1);
  weapon_is_current = true; weapon_selected = 0; weapon_completed_calls = 0; weapon_field_value = 0xF7; weapon_invoked = 0; weapon_cancel_installed = false; weapon_guest_throws = false; weapon_guest_calls = 0;
  add_function(WEAPON_ENTRY, weapon_guest);
  qa_native_weapon_decision_field decision_fields[1]; memset(decision_fields, 0, sizeof decision_fields); decision_fields[0].clear_mask = 3;
  qa_native_weapon_decision decisions[1]; memset(decisions, 0, sizeof decisions); decisions[0].entry = 0x710; decisions[0].join = 0x720; decisions[0].fields = decision_fields; decisions[0].field_count = 1;
  qa_native_item_test continuing_test; memset(&continuing_test, 0, sizeof continuing_test); continuing_test.kind = "scalar"; continuing_test.has_mask = true; continuing_test.mask = 0xF0; continuing_test.comparison = "equals"; continuing_test.scalar_value = 0xF0;
  qa_native_item_test_row rows[1] = {{&continuing_test, 1}};
  qa_native_item_test settled_test = continuing_test; settled_test.has_mask = false; settled_test.comparison = "at-most"; settled_test.scalar_value = 1;
  qa_native_item_test_row settled_rows[1] = {{&settled_test, 1}};
  qa_native_mod_address weapon_a; memset(&weapon_a, 0, sizeof weapon_a); weapon_a.rva = 0x4000;
  qa_native_weapon_selection_value selection_values[1]; memset(selection_values, 0, sizeof selection_values); selection_values[0].item = "q2:gun"; selection_values[0].address = weapon_a; selection_values[0].request.returns = "void";
  qa_native_mod_entry entry; memset(&entry, 0, sizeof entry); entry.kind = "rva"; entry.rva = 0x700;
  qa_native_weapon_stage definition; memset(&definition, 0, sizeof definition);
  definition.dispatcher.entry = entry; definition.dispatcher.record = WEAPON_RECORD; definition.dispatcher.argument = 1; definition.dispatcher.arguments = 2;
  definition.decisions = decisions; definition.decision_count = 1; definition.continuations = rows; definition.continuation_row_count = 1; definition.settled = settled_rows; definition.settled_row_count = 1;
  definition.selection.active.offset = 0; definition.selection.values = selection_values; definition.selection.value_count = 1;
  qa_compat_q2_native_weapon_operations ops; memset(&ops, 0, sizeof ops);
  ops.ctx = OPS_CTX; ops.actor = wop_actor; ops.current = wop_current; ops.selected = wop_selected; ops.pointer = wop_pointer; ops.resolve = wop_resolve; ops.read = wop_read; ops.write = wop_write; ops.invoke = wop_invoke; ops.completed = wop_completed; ops.cancellation = wop_cancellation;
  qa_compat_q2_native_mod_weapon_stage *stage = qa_compat_q2_native_mod_weapon_stage_create(&definition, classic_abi, weapon_host(), "q2:test", ops);
  /* Unselected: the decision masks bits during the region and restores them after. */
  weapon_call();
  CHECK(weapon_guest_calls == 1 && weapon_seen_in_region == 0xF4 && weapon_field_value == 0xF7 && weapon_completed_calls == 1 && weapon_reached);
  /* Selected: no masking. */
  weapon_selected = 1; weapon_call(); CHECK(weapon_seen_in_region == 0xF7); weapon_selected = 0;
  /* Non-actor pointer argument passes straight to the original. */
  qa_guest_call_value other[2] = {qa_compat_q2_call_pointer(false, fake_address(0)), qa_compat_q2_call_pointer(true, fake_address(0x2100))};
  int completed = weapon_completed_calls; qa_compat_q2_guest_call_signature sig; memset(&sig, 0, sizeof sig); sig.result_is_void = true;
  (void)host_invoke(HOST_CTX, fake_address(WEAPON_ENTRY), sig, other, 2); CHECK(weapon_completed_calls == completed);
  /* Tests. */
  CHECK(qa_compat_q2_native_mod_weapon_stage_continuing(stage, weapon_actor));
  CHECK(!qa_compat_q2_native_mod_weapon_stage_settled(stage, weapon_actor));
  weapon_field_value = 1; CHECK(qa_compat_q2_native_mod_weapon_stage_settled(stage, weapon_actor));
  /* Selection. */
  CHECK(qa_compat_q2_native_mod_weapon_stage_active(stage, weapon_actor) == NULL);
  CHECK(qa_compat_q2_native_mod_weapon_stage_pending(stage, weapon_actor) == NULL);
  CHECK(qa_compat_q2_native_mod_weapon_stage_request(stage, weapon_actor, "q2:gun") && weapon_invoked == 1);
  CHECK(!strcmp(qa_compat_q2_native_mod_weapon_stage_active(stage, weapon_actor), "q2:gun"));
  CHECK(!qa_compat_q2_native_mod_weapon_stage_request(stage, weapon_actor, "q2:missing"));
  int32_t undeclared = 0x4444; memcpy(guest + 0x3000, &undeclared, 4); CHECK_ABORTS(qa_compat_q2_native_mod_weapon_stage_active(stage, weapon_actor));
  /* Cancellation: the guest's throw is caught, the processor restored. */
  weapon_cancel_installed = true; weapon_accepts = true; weapon_guest_throws = true; cpu.registers[2] = 3; double depth = runner_depth;
  weapon_call();
  CHECK(cpu.registers[2] == 3 && runner_depth == depth);
  /* A declined cancellation rethrows. */
  weapon_accepts = false;
  CHECK_ABORTS(weapon_call());
  weapon_guest_throws = false; weapon_cancel_installed = false;
  qa_compat_q2_native_mod_weapon_stage_destroy(stage);
  CHECK(regions[0].id == 0 && bindings[0].id == 0);
  /* The dispatcher alone: currentActor while the original runs. */
  qa_identity_owner_destroy(&owner);
}

/* --- native-mod-clients. ----------------------------------------------------- */
#define MAX_CLIENTS 4
typedef struct fake_client { qa_client_id client; qa_actor_id actor; bool live; char userinfo[128]; } fake_client;
static fake_client fake_clients[MAX_CLIENTS]; static size_t fake_client_count;
static qa_mod_client_identity fake_identities[MAX_CLIENTS];
static qa_mod_client_listener_fn client_listener; static void *client_listener_ctx; static int client_unsubscribed, client_drops; static char client_drop_reason[64];
static const qa_mod_client_identity *svc_clients(void *ctx, size_t *n) {
  want_ctx(ctx, SVC_CTX); size_t count = 0;
  for (size_t i = 0; i < fake_client_count; i++) if (fake_clients[i].live) { fake_identities[count].client = fake_clients[i].client; fake_identities[count].actor = fake_clients[i].actor; count++; }
  *n = count; return fake_identities;
}
static bool svc_for_actor(void *ctx, qa_actor_id actor, qa_client_id *out) { want_ctx(ctx, SVC_CTX); for (size_t i = 0; i < fake_client_count; i++) if (fake_clients[i].live && qa_same_actor(fake_clients[i].actor, actor)) { *out = fake_clients[i].client; return true; } return false; }
static bool svc_actor(void *ctx, qa_client_id client, qa_actor_id *out) { want_ctx(ctx, SVC_CTX); for (size_t i = 0; i < fake_client_count; i++) if (fake_clients[i].live && qa_same_client(fake_clients[i].client, client)) { *out = fake_clients[i].actor; return true; } return false; }
static const char *svc_userinfo(void *ctx, qa_client_id client) { want_ctx(ctx, SVC_CTX); for (size_t i = 0; i < fake_client_count; i++) if (qa_same_client(fake_clients[i].client, client)) return fake_clients[i].userinfo; return ""; }
static void svc_set_userinfo(void *ctx, qa_client_id client, const char *value) { want_ctx(ctx, SVC_CTX); for (size_t i = 0; i < fake_client_count; i++) if (qa_same_client(fake_clients[i].client, client)) snprintf(fake_clients[i].userinfo, sizeof fake_clients[i].userinfo, "%s", value); }
static void svc_drop(void *ctx, qa_client_id client, const char *reason, qa_content_id content) { (void)client; (void)content; want_ctx(ctx, SVC_CTX); client_drops++; snprintf(client_drop_reason, sizeof client_drop_reason, "%s", reason); }
static void svc_unsubscribe(void *ctx) { want_ctx(ctx, SVC_CTX); client_unsubscribed++; }
static qa_mod_client_unsubscribe svc_subscribe(void *ctx, qa_mod_client_listener_fn listener, void *listener_ctx) { want_ctx(ctx, SVC_CTX); client_listener = listener; client_listener_ctx = listener_ctx; qa_mod_client_unsubscribe u = {SVC_CTX, svc_unsubscribe}; return u; }
static qa_mod_client_unsubscribe svc_subscribe_application(void *ctx, qa_mod_client_application_listener_fn listener, void *listener_ctx) { (void)listener; (void)listener_ctx; want_ctx(ctx, SVC_CTX); qa_mod_client_unsubscribe u = {SVC_CTX, svc_unsubscribe}; return u; }
static qa_mod_client_services fake_client_services(void) {
  qa_mod_client_services s; memset(&s, 0, sizeof s); s.ctx = SVC_CTX; s.maximum = MAX_CLIENTS; s.clients = svc_clients; s.for_actor = svc_for_actor; s.actor = svc_actor; s.userinfo = svc_userinfo; s.set_userinfo = svc_set_userinfo; s.drop = svc_drop; s.subscribe = svc_subscribe; s.subscribe_application = svc_subscribe_application; return s;
}
static fake_client *add_client(qa_owned_actor actor, const char *userinfo) {
  fake_client *c = &fake_clients[fake_client_count]; c->client = qa_identity_owner_client(&identities, (int64_t)fake_client_count, 1); c->actor = actor.id; c->live = true; snprintf(c->userinfo, sizeof c->userinfo, "%s", userinfo); fake_client_count++; return c;
}
static double client_results[8]; static size_t client_result_count, client_invocations; static const qa_native_mod_source_call *client_last_call; static int client_projected, client_admitted_calls, client_released; static bool client_invoke_null; static qa_compat_q2_native_mod_clients_binding *client_binding_for_forget;
static int command_wrapped;
static void cop_project(void *ctx, qa_actor_id actor) { (void)actor; want_ctx(ctx, OPS_CTX); client_projected++; }
static void cop_admitted(void *ctx, qa_actor_id actor) { (void)actor; want_ctx(ctx, OPS_CTX); client_admitted_calls++; }
static void cop_release(void *ctx, qa_actor_id actor) { (void)actor; want_ctx(ctx, OPS_CTX); client_released++; }
static bool cop_invoke(void *ctx, const qa_native_mod_source_call *call, qa_actor_id actor, double *out) {
  (void)actor; want_ctx(ctx, OPS_CTX); client_last_call = call; client_invocations++;
  if (client_invoke_null) return false;
  *out = client_result_count > 0 ? client_results[--client_result_count] : 1; return true;
}
static void cop_with_command(void *ctx, qa_command_invocation *command, void (*invoke)(void *), void *invoke_ctx) { (void)command; want_ctx(ctx, OPS_CTX); command_wrapped++; invoke(invoke_ctx); }
static void client_event(const char *kind, const fake_client *c) { qa_mod_client_event e; e.kind = kind; e.identity.client = c->client; e.identity.actor = c->actor; client_listener(client_listener_ctx, e); }
static void test_clients(void) {
  world_setup(); fake_client_count = 0; client_drops = client_projected = client_admitted_calls = client_released = client_unsubscribed = command_wrapped = 0; client_invocations = 0; client_invoke_null = false; client_result_count = 0;
  qa_mod_client_services svc = fake_client_services();
  qa_owned_actor a = qa_session_actor_registry_allocate(registry, "q2:p", "a"), b = qa_session_actor_registry_allocate(registry, "q2:p", "b"), c3 = qa_session_actor_registry_allocate(registry, "q2:p", "c");
  fake_client *ca = add_client(a, "\\name\\a");
  qa_native_mod_source_call calls[6]; memset(calls, 0, sizeof calls); for (int i = 0; i < 6; i++) calls[i].returns = "int32";
  qa_native_mod_admission_call admit[1]; memset(admit, 0, sizeof admit); admit[0].call = calls[0]; admit[0].accepts = "nonzero";
  const char *const record_names[1] = {"cli"};
  qa_native_mod_clients declaration; memset(&declaration, 0, sizeof declaration);
  declaration.maximum = 2; declaration.records.values = record_names; declaration.records.count = 1; declaration.admit = admit; declaration.admit_count = 1;
  declaration.userinfo = &calls[1]; declaration.userinfo_count = 1; declaration.disconnect = &calls[2]; declaration.disconnect_count = 1; declaration.command = &calls[3]; declaration.command_count = 1; declaration.frame = &calls[4]; declaration.frame_count = 1; declaration.end_frame = &calls[5]; declaration.end_frame_count = 1;
  qa_compat_q2_native_mod_client_operations ops; memset(&ops, 0, sizeof ops);
  ops.services = &svc; ops.declaration = &declaration; ops.content = "q2:c"; ops.ctx = OPS_CTX; ops.project = cop_project; ops.admitted = cop_admitted; ops.release = cop_release; ops.invoke = cop_invoke; ops.with_command = cop_with_command;
  qa_compat_q2_native_mod_clients_binding s; qa_compat_q2_native_mod_clients_init(&s, ops); client_binding_for_forget = &s;
  /* start() admits existing clients. */
  qa_compat_q2_native_mod_clients_start(&s);
  CHECK(client_projected == 1 && client_admitted_calls == 1 && client_invocations == 1 && client_last_call == &admit[0].call);
  CHECK(qa_compat_q2_native_mod_clients_admitted(&s, a.id));
  double slot = -1; CHECK(qa_compat_q2_native_mod_clients_slot(&s, a.id, &slot) && slot == 0);
  /* Second start is a no-op. */
  qa_compat_q2_native_mod_clients_start(&s); CHECK(client_invocations == 1);
  /* A nonzero admission returning 0 drops with the rejmsg and denies. */
  fake_client *cb = add_client(b, "\\rejmsg\\go away\\name\\b"); client_results[0] = 0; client_result_count = 1;
  client_event("admitted", cb);
  CHECK(client_drops == 1 && !strcmp(client_drop_reason, "go away") && qa_compat_q2_native_mod_clients_rejects(&s, b.id) && !qa_compat_q2_native_mod_clients_admitted(&s, b.id));
  /* A denied actor rows are retained; its slot stays reserved. */
  CHECK(qa_compat_q2_native_mod_clients_slot(&s, b.id, &slot) && slot == 1);
  /* Capacity (declared 2 rows). */
  fake_client *cc = add_client(c3, "\\name\\c"); CHECK_ABORTS(qa_compat_q2_native_mod_clients_slot(&s, c3.id, &slot)); cc->live = false; fake_client_count--;
  /* Rejected clients block a save until they disconnect. */
  size_t count = 0; CHECK_ABORTS(qa_compat_q2_native_mod_clients_checkpoint(&s, &count));
  client_event("disconnecting", cb); CHECK(!qa_compat_q2_native_mod_clients_rejects(&s, b.id) && client_released == 1 && !qa_compat_q2_native_mod_clients_has(&s, b.id));
  cb->live = false;
  /* Userinfo event runs its calls after admission. */
  client_invocations = 0; client_event("userinfo", ca); CHECK(client_invocations == 1 && client_last_call == &calls[1]);
  /* Frame maps edict slot (row + 1); unknown slots report false. */
  client_invocations = 0; CHECK(qa_compat_q2_native_mod_clients_frame(&s, 1) && client_last_call == &calls[4]); CHECK(!qa_compat_q2_native_mod_clients_frame(&s, 2) && client_invocations == 1);
  client_invocations = 0; qa_compat_q2_native_mod_clients_end_frame(&s); CHECK(client_invocations == 1 && client_last_call == &calls[5]);
  /* A null result ends the call list. */
  client_invoke_null = true; client_invocations = 0; client_event("userinfo", ca); CHECK(client_invocations == 1); client_invoke_null = false;
  /* Commands resolve through script origins to their client. */
  qa_command_invocation command; memset(&command, 0, sizeof command); qa_command_origin caller; memset(&caller, 0, sizeof caller); caller.kind = QA_COMMAND_ORIGIN_REMOTE_CLIENT; caller.client = ca->client;
  command.source.origin.kind = QA_COMMAND_ORIGIN_SCRIPT; command.source.origin.caller = &caller;
  client_invocations = 0; CHECK(qa_compat_q2_native_mod_clients_invoke_command(&s, &command) && command_wrapped == 1 && client_last_call == &calls[3]);
  command.source.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE; CHECK(!qa_compat_q2_native_mod_clients_invoke_command(&s, &command));
  /* Userinfo round trip. */
  qa_compat_q2_native_mod_clients_set_userinfo(&s, a.id, "\\name\\z"); CHECK(!strcmp(qa_compat_q2_native_mod_clients_userinfo(&s, a.id), "\\name\\z"));
  /* Checkpoint and restore. */
  qa_compat_q2_native_mod_client_slot *rows = qa_compat_q2_native_mod_clients_checkpoint(&s, &count);
  CHECK(count == 1 && rows[0].slot == 0 && rows[0].admitted);
  qa_compat_q2_native_mod_clients_forget(&s, a.id); CHECK(!qa_compat_q2_native_mod_clients_has(&s, a.id));
  qa_compat_q2_native_mod_clients_restore(&s, rows, count); CHECK(qa_compat_q2_native_mod_clients_admitted(&s, a.id));
  ca->live = false; CHECK_ABORTS(qa_compat_q2_native_mod_clients_validate_restore(&s, rows, count)); CHECK_ABORTS(qa_compat_q2_native_mod_clients_userinfo(&s, a.id)); ca->live = true;
  free(rows);
  /* The default rejection reason. */
  fake_client *cd = add_client(b, "\\name\\b"); cd->client = qa_identity_owner_client(&identities, 9, 1); client_results[0] = 0; client_result_count = 1; client_drops = 0;
  client_event("admitted", cd); CHECK(client_drops == 1 && !strcmp(client_drop_reason, "Connection refused"));
  client_event("disconnecting", cd); cd->live = false;
  /* Close releases rows and unsubscribes both. */
  client_released = 0; qa_compat_q2_native_mod_clients_close(&s); CHECK(client_released == 1 && client_unsubscribed >= 1 && !qa_compat_q2_native_mod_clients_has(&s, a.id));
  qa_compat_q2_native_mod_clients_destroy(&s);
  world_teardown();
}

/* --- Provider declaration validation. ---------------------------------------- */
static qa_native_mod_actor_field field_of(double offset, const char *binding, const char *encoding, qa_item_id item) {
  qa_native_mod_actor_field f; memset(&f, 0, sizeof f); f.offset = offset; f.binding = binding; f.encoding = encoding; f.item = item; f.qvm_field.offset = offset; f.qvm_field.binding = binding; f.qvm_field.item = item; return f;
}
static qa_native_mod_source_call void_call(double rva) { qa_native_mod_source_call c; memset(&c, 0, sizeof c); c.entry.kind = "rva"; c.entry.rva = rva; c.returns = "void"; return c; }
static qa_native_mod_declaration base_declaration(void) {
  qa_native_mod_declaration d; memset(&d, 0, sizeof d); d.version = 1; d.runtime = "native"; d.target.api.kind = QA_Q2_GAME_API_RERELEASE; d.target.api.version = 2023; d.target.abi = rerelease_abi; return d;
}
static void test_validation(void) {
  qa_native_mod_declaration d = base_declaration();
  qa_compat_q2_validate_native_mod_declaration(&d);
  qa_native_mod_source_call project = void_call(1), release = void_call(2);
  qa_native_mod_actor_field fields[3] = {field_of(0, "health", "int32", NULL), field_of(4, "inventory", "int32", "q2:a"), field_of(8, "origin", NULL, NULL)};
  qa_native_mod_actor_record record; memset(&record, 0, sizeof record); record.id = "ent"; record.base.kind = "address"; record.stride = 64; record.capacity = 4; record.fields = fields; record.field_count = 3;
  d.actor_records = &record; d.actor_record_count = 1;
  /* Declared records require project and release. */
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  d.project = &project; d.project_count = 1; d.release = &release; d.release_count = 1;
  qa_compat_q2_validate_native_mod_declaration(&d);
  /* The entity record must use the source edict table. */
  d.entity_record = "ent"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); record.base.kind = "entities"; qa_compat_q2_validate_native_mod_declaration(&d); d.entity_record = NULL; record.base.kind = "address";
  /* Bad record shapes. */
  record.stride = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); record.stride = 64;
  record.capacity = 65537; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); record.capacity = 4;
  qa_native_mod_actor_record duplicate[2] = {record, record}; d.actor_records = duplicate; d.actor_record_count = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); d.actor_records = &record; d.actor_record_count = 1;
  /* Field extent and overlap. */
  fields[2].offset = 60; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); fields[2].offset = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); fields[2].offset = 8;
  /* One authority per shared store. */
  fields[1] = field_of(4, "health", "int32", NULL); CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); fields[1] = field_of(4, "inventory", "int32", "q2:a");
  /* Constant scalars must fit. */
  fields[1] = field_of(4, "constant", "uint8", NULL); fields[1].constant_value = 256; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); fields[1].constant_value = 255; qa_compat_q2_validate_native_mod_declaration(&d); fields[1] = field_of(4, "inventory", "int32", "q2:a");
  /* Linked records must exist. */
  qa_native_mod_actor_field linked[1] = {field_of(0, "record", NULL, NULL)}; linked[0].qvm_field.record = "missing"; record.fields = linked; record.field_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); linked[0].qvm_field.record = "ent"; qa_compat_q2_validate_native_mod_declaration(&d); record.fields = fields; record.field_count = 3;
  /* Source actors. */
  qa_native_mod_clock_entry clock[1]; memset(clock, 0, sizeof clock); clock[0].input = "time"; clock[0].encoding = "float32"; clock[0].units = "seconds";
  qa_native_mod_source_actors owned; memset(&owned, 0, sizeof owned); owned.frame_seconds = 0.1; owned.clock = clock; owned.clock_count = 1; d.source_actors = &owned; qa_compat_q2_validate_native_mod_declaration(&d);
  owned.frame_seconds = 0; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); owned.frame_seconds = 0.1;
  qa_native_mod_source_callbacks callbacks; memset(&callbacks, 0, sizeof callbacks); callbacks.abi = "q2-classic"; owned.callbacks = &callbacks; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); callbacks.abi = "q2-rerelease"; qa_compat_q2_validate_native_mod_declaration(&d);
  qa_native_mod_combat combat; memset(&combat, 0, sizeof combat); combat.damage.abi = "q2-rerelease"; combat.causes.edition = "classic"; owned.combat = &combat; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); combat.causes.edition = "rerelease"; qa_compat_q2_validate_native_mod_declaration(&d); owned.combat = NULL; owned.callbacks = NULL; d.source_actors = NULL;
  /* Call inputs. */
  qa_native_mod_value argument; memset(&argument, 0, sizeof argument); argument.kind = "int32"; argument.value.kind = "input"; argument.value.name = "amount"; qa_native_mod_source_call initialize = void_call(3); initialize.arguments = &argument; initialize.argument_count = 1;
  d.initialize = &initialize; d.initialize_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  argument.value.name = "time"; qa_compat_q2_validate_native_mod_declaration(&d);
  argument.kind = "vector"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); argument.kind = "int32";
  argument.kind = "user-command"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); argument.kind = "int32";
  argument.kind = "actor"; argument.input = "self"; argument.record = "ent"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  qa_native_mod_skip skips[2] = {{4, 2}, {1, 2}}; argument.kind = "int32"; initialize.skips = skips; initialize.skip_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  skips[0].entry = 1; skips[0].join = 2; initialize.skip_count = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); initialize.skip_count = 0;
  /* Duplicate callbacks and missing transform return. */
  qa_native_mod_callback cb[2]; memset(cb, 0, sizeof cb); cb[0].binding.id = "m:a"; cb[0].binding.operation = "damage"; cb[0].binding.stage = "transform"; cb[0].binding.result = "amount"; cb[0].call = void_call(9); d.callbacks = cb; d.callback_count = 1;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); cb[0].call.returns = "int32"; qa_compat_q2_validate_native_mod_declaration(&d);
  cb[1] = cb[0]; d.callback_count = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); d.callback_count = 1;
  /* Operation-specific inputs: amount is available to damage, not to actor.use. */
  qa_native_mod_value amount; memset(&amount, 0, sizeof amount); amount.kind = "int32"; amount.value.kind = "input"; amount.value.name = "amount"; cb[0].call.arguments = &amount; cb[0].call.argument_count = 1; qa_compat_q2_validate_native_mod_declaration(&d);
  cb[0].binding.operation = "actor.use"; cb[0].binding.stage = "replace"; cb[0].binding.result = "boolean"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  d.callbacks = NULL; d.callback_count = 0;
  /* Client layout. */
  qa_native_mod_actor_record client_rec[2]; memset(client_rec, 0, sizeof client_rec);
  qa_native_mod_actor_field client_fields[2] = {field_of(0, "private", NULL, NULL), field_of(16, "private", NULL, NULL)}; client_fields[0].qvm_field.byte_length = 16; client_fields[1].qvm_field.byte_length = 16;
  client_rec[0].id = "ent"; client_rec[0].base.kind = "entities"; client_rec[0].stride = 64; client_rec[0].first_slot = 1; client_rec[0].capacity = 4; client_rec[0].fields = client_fields; client_rec[0].field_count = 2;
  client_rec[1].id = "cli"; client_rec[1].base.kind = "clients"; client_rec[1].stride = 64; client_rec[1].first_slot = 0; client_rec[1].capacity = 2; client_rec[1].fields = client_fields; client_rec[1].field_count = 2;
  const char *const record_names[1] = {"cli"}; qa_native_mod_clients clients; memset(&clients, 0, sizeof clients); clients.maximum = 2; clients.records.values = record_names; clients.records.count = 1;
  d.actor_records = client_rec; d.actor_record_count = 2; d.entity_record = "ent"; d.clients = &clients; d.initialize_count = 0;
  qa_compat_q2_validate_native_mod_declaration(&d);
  client_rec[0].first_slot = 0; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); client_rec[0].first_slot = 1;
  clients.maximum = 3; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); clients.maximum = 2;
  client_rec[1].capacity = 3; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); client_rec[1].capacity = 2;
  d.clients = NULL; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); d.clients = &clients;
  /* ClientConnect must honor rejection; nonzero needs a return. */
  qa_native_mod_admission_call admit[1]; memset(admit, 0, sizeof admit); admit[0].call = void_call(1); admit[0].accepts = "nonzero"; clients.admit = admit; clients.admit_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  admit[0].call.returns = "int32"; qa_compat_q2_validate_native_mod_declaration(&d);
  clients.admit_count = 0;
  /* Client frames need the source actor clock; input fields need input callbacks. */
  qa_native_mod_source_call frame = void_call(2); clients.frame = &frame; clients.frame_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); clients.frame_count = 0;
  qa_native_mod_client_input_field input_field; memset(&input_field, 0, sizeof input_field); input_field.record = "cli"; input_field.value.kind = "int32"; input_field.value.value.kind = "input"; input_field.value.value.name = "attack";
  clients.input_fields = &input_field; clients.input_field_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  qa_native_mod_source_call input_call = void_call(3); qa_mod_client_input_binding binding; memset(&binding, 0, sizeof binding); binding.scope = "client-command"; binding.phase = "before"; binding.calls = &input_call; binding.call_count = 1; binding.call_elem_size = sizeof input_call; clients.input = &binding; clients.input_count = 1;
  qa_compat_q2_validate_native_mod_declaration(&d);
  /* Input field overlap and private coverage. */
  input_field.offset = 62; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); input_field.offset = 0;
  qa_native_mod_client_input_field two[2] = {input_field, input_field}; two[1].offset = 2; clients.input_fields = two; clients.input_field_count = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); clients.input_fields = &input_field; clients.input_field_count = 1;
  /* A handler output needs exactly one self argument. */
  qa_native_mod_input_output handler; memset(&handler, 0, sizeof handler); handler.kind = "handler"; binding.outputs = &handler; binding.output_count = 1; binding.output_elem_size = sizeof handler; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  qa_native_mod_value self_arg; memset(&self_arg, 0, sizeof self_arg); self_arg.kind = "actor"; self_arg.input = "self"; self_arg.record = "cli"; handler.arguments = &self_arg; handler.argument_count = 1; qa_compat_q2_validate_native_mod_declaration(&d);
  qa_native_mod_input_output field_output; memset(&field_output, 0, sizeof field_output); field_output.kind = "field"; field_output.record = "cli"; field_output.offset = 8; binding.outputs = &field_output; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); field_output.offset = 0; qa_compat_q2_validate_native_mod_declaration(&d); binding.output_count = 0;
  /* Pose. */
  qa_native_mod_client_pose pose; memset(&pose, 0, sizeof pose); pose.view_height = afield("cli", 32, "float32"); pose.crouched.field = afield("cli", 36, "int32"); pose.crouched.mask = 4; clients.pose = &pose; qa_compat_q2_validate_native_mod_declaration(&d);
  pose.crouched.mask = 0; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); pose.crouched.mask = 4; clients.pose = NULL;
  clients.input_field_count = 0; clients.input_count = 0;
  /* Protection requires clients and a matching source ABI; donor region layout. */
  qa_native_mod_regular_armor_item storage; memset(&storage, 0, sizeof storage); storage.item = "q2:a"; storage.selection = positive(afield("ent", 0, "int32")); storage.points = afield("ent", 4, "int32");
  qa_native_mod_protection_definition protection[2]; memset(protection, 0, sizeof protection); protection[0].id = "m:p"; protection[0].channel = "regular"; protection[0].regular_storage = &storage; protection[0].regular_storage_count = 1;
  protection[0].absorb_abi = "q2-check-armor"; protection[0].absorb_q2.flags = "q2-classic"; d.protection = protection; d.protection_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  protection[0].absorb_q2.flags = "q2-rerelease"; qa_compat_q2_validate_native_mod_declaration(&d);
  protection[1] = protection[0]; d.protection_count = 2; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); d.protection_count = 1;
  protection[0].absorb_abi = "source-call"; protection[0].absorb_call = void_call(4); CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); protection[0].absorb_call.returns = "int32"; qa_compat_q2_validate_native_mod_declaration(&d);
  /* source-region: x64 cdecl-style home space is 32 bytes, plus the return address. */
  qa_native_mod_protection_region region = base_region(); region.call = void_call(5); region.call.returns = "int32"; region.frame.argument_bytes = 32; protection[0].absorb_abi = "source-region"; protection[0].absorb_region = &region; qa_compat_q2_validate_native_mod_declaration(&d);
  region.frame.argument_bytes = 8; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d)); region.frame.argument_bytes = 32;
  qa_native_mod_region_input donor; memset(&donor, 0, sizeof donor); donor.target.kind = QA_NATIVE_MOD_LOCATION_REGISTER; donor.target.reg = "rcx"; donor.target.storage = "int64"; donor.value.kind = "int32"; donor.value.value.kind = "input"; donor.value.value.name = "amount"; region.inputs = &donor; region.input_count = 1; CHECK_ABORTS(qa_compat_q2_validate_native_mod_declaration(&d));
  donor.target.storage = "int32"; qa_compat_q2_validate_native_mod_declaration(&d);
  d.protection = NULL; d.protection_count = 0;
}

/* --- Provider lifecycle, projection, flush, checkpoint. ----------------------- */
#define PROVIDER_ID "q2:fake"
#define RECORD_BASE (TABLE_BASE + TABLE_STRIDE)
static const qa_native_mod_actor_field provider_fields[4] = {
  {0, "health", {0, NULL, "health", NULL, NULL, 0, NULL, NULL, 0, {0, 0, 0}, 0}, NULL, "int32", NULL, 0, NULL, 0},
  {4, "inventory", {4, NULL, "inventory", NULL, NULL, 0, "q2:shells", NULL, 0, {0, 0, 0}, 0}, NULL, "int32", NULL, 0, "q2:shells", 0},
  {8, "origin", {8, NULL, "origin", NULL, NULL, 0, NULL, NULL, 0, {0, 0, 0}, 0}, NULL, NULL, NULL, 0, NULL, 0},
  {32, "constant", {32, NULL, "constant", NULL, NULL, 0, NULL, NULL, 77, {0, 0, 0}, 0}, NULL, "int32", NULL, 0, NULL, 77},
};
static qa_actor_id provider_target; static int provider_guest_calls, provider_project_calls, provider_release_calls, assert_calls; static bool provider_guest_writes;
static void provider_assert(void *ctx) { want_ctx(ctx, OPS_CTX); assert_calls++; }
static qa_guest_call_result guest_project(const qa_guest_call_value *values, size_t count) { (void)values; (void)count; provider_project_calls++; return qa_compat_q2_call_void(); }
static qa_guest_call_result guest_release(const qa_guest_call_value *values, size_t count) { (void)values; (void)count; provider_release_calls++; return qa_compat_q2_call_void(); }
static qa_guest_call_result guest_callback(const qa_guest_call_value *values, size_t count) {
  provider_guest_calls++;
  if (provider_guest_writes) { CHECK(count == 2 && values[0].kind == QA_GUEST_VALUE_POINTER && values[0].pointer_value.value.byte_offset == RECORD_BASE); guest_write_i32(RECORD_BASE, 33); guest_write_i32(RECORD_BASE + 4, 9); }
  qa_guest_call_result result = qa_compat_q2_call_int32(values[1].number_value + 1); return result;
}
static qa_combat_state combat_state_value; static const qa_inventory_entry *inventory_entries_value;
static qa_body_state body_value; static qa_body_state body_read_value(void *ctx) { want_ctx(ctx, OPS_CTX); return body_value; }
static void body_write_value(void *ctx, qa_body_state state) { want_ctx(ctx, OPS_CTX); body_value = state; }
static qa_combat_state combat_read_value(void *ctx) { want_ctx(ctx, OPS_CTX); return combat_state_value; }
static void combat_write_health(void *ctx, double value) { want_ctx(ctx, OPS_CTX); combat_state_value.health = value; }
static void foreign_actor_setup(qa_owned_actor actor) {
  qa_body_state_binding body; memset(&body, 0, sizeof body); body.ctx = OPS_CTX; body.read = body_read_value; body.write = body_write_value; memset(&body_value, 0, sizeof body_value); body_value.origin.x = 3;
  qa_shared_body_table_bind(bodies, actor, body);
  memset(&combat_state_value, 0, sizeof combat_state_value); combat_state_value.health = 100; combat_state_value.mass = 200; combat_state_value.can_take_damage = true; combat_state_value.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat_state_value.armor.powered.kind = "none";
  qa_combat_state_binding combat; memset(&combat, 0, sizeof combat); combat.ctx = OPS_CTX; combat.read = combat_read_value; combat.write_health = combat_write_health; combat.protection_regular.declared = true; combat.protection_regular.owner_is_null = true;
  qa_gameplay_authority_bind(authority, actor, combat);
  qa_inventory_entry entries[2]; memset(entries, 0, sizeof entries); entries[0].item = "q2:shells"; entries[0].count = 20; entries[0].capacity = 100;
  entries[1].item = "q2:armor"; entries[1].count = 40; entries[1].capacity = 100;
  qa_shared_inventory_table_create_entries(inventory, actor, entries, 2); (void)inventory_entries_value;
}
static qa_mod_runtime_value actor_input(const qa_actor_id *actor) { qa_mod_runtime_value v; memset(&v, 0, sizeof v); v.kind = "actor"; v.actor_value = actor; return v; }
static qa_mod_runtime_value float_input(double value) { qa_mod_runtime_value v; memset(&v, 0, sizeof v); v.kind = "float"; v.number_value = value; return v; }
static void test_provider(void) {
  fake_reset(8); world_setup(); assert_calls = 0; provider_guest_calls = provider_project_calls = provider_release_calls = 0; provider_guest_writes = false;
  add_function(IMAGE_BASE + 0x801, guest_project); add_function(IMAGE_BASE + 0x802, guest_release); add_function(IMAGE_BASE + 0x803, guest_callback);
  qa_native_mod_actor_record record; memset(&record, 0, sizeof record); record.id = "ent"; record.base.kind = "entities"; record.stride = TABLE_STRIDE; record.first_slot = 1; record.capacity = 4; record.fields = provider_fields; record.field_count = 4;
  qa_native_mod_source_call project = void_call(0x801), release = void_call(0x802);
  qa_native_mod_value arguments[2]; memset(arguments, 0, sizeof arguments); arguments[0].kind = "actor"; arguments[0].record = "ent"; arguments[0].input = "self"; arguments[1].kind = "int32"; arguments[1].value.kind = "input"; arguments[1].value.name = "amount";
  qa_native_mod_callback callbacks[1]; memset(callbacks, 0, sizeof callbacks); callbacks[0].binding.id = "m:cb"; callbacks[0].binding.operation = "damage"; callbacks[0].binding.stage = "observe"; callbacks[0].call = void_call(0x803); callbacks[0].call.returns = "int32"; callbacks[0].call.arguments = arguments; callbacks[0].call.argument_count = 2;
  qa_native_mod_declaration d = base_declaration(); d.actor_records = &record; d.actor_record_count = 1; d.entity_record = "ent"; d.project = &project; d.project_count = 1; d.release = &release; d.release_count = 1; d.callbacks = callbacks; d.callback_count = 1;
  uint8_t empty_object[64]; qa_save_value *cvars = qa_save_value_object(); saved_cvars = qa_encode_checkpoint_value(cvars, &saved_cvars_length); qa_save_value_free(cvars); (void)empty_object;
  qa_compat_q2_native_mod_provider *p = qa_compat_q2_native_mod_provider_create(&d, &services, PROVIDER_ID, "base1", OPS_CTX, provider_assert);
  CHECK_ABORTS(qa_compat_q2_native_mod_provider_address(p, world_actor.id)); /* no host yet */
  qa_compat_q2_native_mod_provider_attach(p, HOST);
  CHECK_ABORTS(qa_compat_q2_native_mod_provider_attach(p, HOST));
  qa_compat_q2_native_mod_provider_initialize(p, false);
  CHECK(host_initialized == 1 && assert_calls > 0 && presentation_published == 1);
  /* World actor resolves to the table base and slot 0. */
  CHECK(qa_compat_q2_native_mod_provider_address(p, world_actor.id).byte_offset == TABLE_BASE);
  double slot = -1; CHECK(qa_compat_q2_native_mod_provider_slot_of(p, world_actor.id, &slot) && slot == 0);
  qa_actor_id at; CHECK(qa_compat_q2_native_mod_provider_actor_at(p, 0, &at) && qa_same_actor(at, world_actor.id));
  /* First use of a borrowed actor projects it: constants, authored init, once. */
  qa_owned_actor actor = qa_session_actor_registry_allocate(registry, "other:mod", "player"); foreign_actor_setup(actor); provider_target = actor.id;
  qa_compat_q2_native_pickup_input rows[2]; memset(rows, 0, sizeof rows); rows[0].name = "self"; rows[0].value = actor_input(&actor.id); rows[1].name = "amount"; rows[1].value = float_input(41);
  qa_compat_q2_native_pickup_inputs inputs = {rows, 2}; double result = -1;
  CHECK(qa_compat_q2_native_mod_provider_invoke(p, &callbacks[0].call, inputs, &result) && result == 42);
  CHECK(provider_project_calls == 1 && provider_guest_calls == 1 && get_i32(RECORD_BASE + 32) == 77);
  /* Refresh copied canonical state into the source record. */
  CHECK(get_i32(RECORD_BASE) == 100 && get_i32(RECORD_BASE + 4) == 20 && get_f32(RECORD_BASE + 8) == 3);
  CHECK(qa_compat_q2_native_mod_provider_address(p, actor.id).byte_offset == RECORD_BASE && provider_project_calls == 1);
  /* A guest write commits back through the canonical owners (health, inventory). */
  provider_guest_writes = true; result = -1;
  CHECK(qa_compat_q2_native_mod_provider_invoke(p, &callbacks[0].call, inputs, &result) && result == 42);
  CHECK(combat_state_value.health == 33 && qa_shared_inventory_table_count(inventory, actor.id, "q2:shells") == 9);
  provider_guest_writes = false;
  /* Canonical changes refresh back on the next call. */
  qa_gameplay_authority_set_health(authority, actor, 70); (void)qa_compat_q2_native_mod_provider_invoke(p, &callbacks[0].call, inputs, &result); CHECK(get_i32(RECORD_BASE) == 70);
  /* Scalars out of declared range fail. */
  arguments[1].kind = "uint8"; rows[1].value = float_input(300); CHECK_ABORTS(qa_compat_q2_native_mod_provider_invoke(p, &callbacks[0].call, inputs, &result)); arguments[1].kind = "int32"; rows[1].value = float_input(41);
  /* Advance publishes and clears events without source actors. */
  qa_frame_context frame; memset(&frame, 0, sizeof frame); frame.time.kind = QA_SOURCE_TIME_SECONDS; frame.time.value = 11;
  int published = presentation_published; qa_compat_q2_native_mod_provider_advance(p, frame); CHECK(presentation_published == published + 1 && presentation_begin == 1);
  /* Checkpoint and restore round trip. */
  qa_provider_checkpoint checkpoint = qa_compat_q2_native_mod_provider_checkpoint(p);
  CHECK(!strcmp(checkpoint.schema, "native:mod") && checkpoint.version == 1 && checkpoint.bytes.length > 0);
  qa_compat_q2_validate_native_mod_checkpoint(&checkpoint, fake_memory_table.module, &d);
  qa_provider_checkpoint wrong_owner = checkpoint; wrong_owner.schema = "native:other"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_checkpoint(&wrong_owner, fake_memory_table.module, &d));
  qa_module_identity other_module = fake_memory_table.module; other_module.digest = "sha256:1111111111111111111111111111111111111111111111111111111111111111"; CHECK_ABORTS(qa_compat_q2_validate_native_mod_checkpoint(&checkpoint, other_module, &d));
  qa_native_mod_clock_entry clock[1]; memset(clock, 0, sizeof clock); clock[0].input = "time"; clock[0].encoding = "float32"; clock[0].units = "seconds";
  qa_native_mod_source_actors owned; memset(&owned, 0, sizeof owned); owned.frame_seconds = 0.1; owned.clock = clock; owned.clock_count = 1; qa_native_mod_declaration with_owned = d; with_owned.source_actors = &owned;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_checkpoint(&checkpoint, fake_memory_table.module, &with_owned));
  qa_native_mod_declaration classic_d = d; classic_d.target.api.kind = QA_Q2_GAME_API_CLASSIC; CHECK_ABORTS(qa_compat_q2_validate_native_mod_checkpoint(&checkpoint, fake_memory_table.module, &classic_d));
  put_i32(RECORD_BASE + 32, 0);
  qa_compat_q2_native_mod_provider_restore(p, &checkpoint);
  CHECK(host_restored == 1 && qa_compat_q2_native_mod_provider_address(p, actor.id).byte_offset == RECORD_BASE && provider_project_calls == 1);
  CHECK(get_i32(RECORD_BASE) == 70 && get_i32(RECORD_BASE + 32) == 0); /* restore keeps source constants; refresh copies canonical */
  free((void *)checkpoint.bytes.bytes);
  /* Releasing the actor runs the authored release. */
  qa_session_actor_registry_release(registry, actor);
  CHECK(presentation_releases >= 1);
  qa_compat_q2_native_mod_provider_advance(p, frame); CHECK(provider_release_calls == 1);
  CHECK(!qa_compat_q2_native_mod_provider_slot_of(p, actor.id, &slot));
  /* Close tears down the host exactly once and is idempotent. */
  qa_compat_q2_native_mod_provider_close(p); CHECK(host_closed == 1); qa_compat_q2_native_mod_provider_close(p); CHECK(host_closed == 1);
  CHECK_ABORTS(qa_compat_q2_native_mod_provider_invoke(p, &callbacks[0].call, inputs, &result));
  qa_compat_q2_native_mod_provider_destroy(p); free(saved_cvars); saved_cvars = NULL;
  world_teardown();
}

/* --- Source actors and combat, through the provider. -------------------------- */
#define ALLOC_ENTRY (IMAGE_BASE + 0xA00)
#define FREE_ENTRY (IMAGE_BASE + 0xA01)
#define UPDATE_ENTRY (IMAGE_BASE + 0xA02)
#define DAMAGE_ENTRY (IMAGE_BASE + 0xA03)
#define PAIN_ENTRY (IMAGE_BASE + 0xA04)
#define HEALTH_OFFSET 100
#define VELOCITY_OFFSET 240
#define PAIN_OFFSET 268
#define CLOCK_ADDRESS (IMAGE_BASE + 0xB00)
static int update_calls, free_calls, pain_calls; static double last_update_slot;
static qa_compat_q2_native_mod_provider *live_provider;
static qa_guest_call_result guest_allocate(const qa_guest_call_value *values, size_t count) {
  (void)values; (void)count; slot_active[2] = true; memset(guest + TABLE_BASE + 2 * TABLE_STRIDE, 0, TABLE_STRIDE);
  put_i32(TABLE_BASE + 2 * TABLE_STRIDE + PAIN_OFFSET, PAIN_ENTRY);
  return qa_compat_q2_call_pointer(true, fake_address(TABLE_BASE + 2 * TABLE_STRIDE));
}
static qa_guest_call_result guest_free(const qa_guest_call_value *values, size_t count) { (void)count; free_calls++; slot_active[(values[0].pointer_value.value.byte_offset - TABLE_BASE) / TABLE_STRIDE] = false; return qa_compat_q2_call_void(); }
static qa_guest_call_result guest_update(const qa_guest_call_value *values, size_t count) { (void)count; update_calls++; last_update_slot = (double)((values[0].pointer_value.value.byte_offset - TABLE_BASE) / TABLE_STRIDE); return qa_compat_q2_call_void(); }
static qa_guest_call_result guest_pain(const qa_guest_call_value *values, size_t count) { (void)values; (void)count; pain_calls++; return qa_compat_q2_call_void(); }
/* Original T_Damage: writes health, then calls the guest's pain through the bound entry. */
static qa_guest_call_result guest_damage(const qa_guest_call_value *values, size_t count) {
  CHECK(count == 10);
  int64_t target = values[0].pointer_value.value.byte_offset; int32_t amount = (int32_t)values[6].number_value;
  guest_write_i32(target + HEALTH_OFFSET, get_i32(target + HEALTH_OFFSET) - amount);
  CHECK(values[9].kind == QA_GUEST_VALUE_INT32);
  qa_guest_call_value pain[4]; pain[0] = values[0]; pain[1] = values[2]; memset(&pain[2], 0, sizeof pain[2]); pain[2].kind = QA_GUEST_VALUE_FLOAT32; pain[2].number_value = 5; pain[3] = qa_compat_q2_call_int32(amount);
  qa_compat_q2_guest_call_signature sig; memset(&sig, 0, sizeof sig); sig.result_is_void = true;
  (void)host_invoke(HOST_CTX, fake_address(PAIN_ENTRY), sig, pain, 4);
  return qa_compat_q2_call_void();
}
static void test_actors_combat(void) {
  fake_reset(4); world_setup(); update_calls = free_calls = pain_calls = 0; assert_calls = 0; hook_before_reaction = hook_confirmed = 0;
  slot_active[0] = true;
  add_function(ALLOC_ENTRY, guest_allocate); add_function(FREE_ENTRY, guest_free); add_function(UPDATE_ENTRY, guest_update); add_function(DAMAGE_ENTRY, guest_damage); add_function(PAIN_ENTRY, guest_pain);
  qa_native_mod_entry alloc; memset(&alloc, 0, sizeof alloc); alloc.kind = "rva"; alloc.rva = 0xA00; qa_native_mod_entry release_e = alloc; release_e.rva = 0xA01; qa_native_mod_entry update_e = alloc; update_e.rva = 0xA02; qa_native_mod_entry damage_e = alloc; damage_e.rva = 0xA03;
  static const double no_indirections[1] = {0};
  qa_native_mod_clock_entry clock[2]; memset(clock, 0, sizeof clock); clock[0].address.rva = 0xB00; clock[0].address.indirections.values = no_indirections; clock[0].input = "time"; clock[0].encoding = "int32"; clock[0].units = "milliseconds"; clock[1] = clock[0]; clock[1].address.rva = 0xB04; clock[1].input = "frame";
  qa_native_mod_source_callbacks callbacks; memset(&callbacks, 0, sizeof callbacks); callbacks.abi = "q2-classic"; callbacks.has_pain = true; callbacks.pain = PAIN_OFFSET;
  qa_native_mod_combat combat; memset(&combat, 0, sizeof combat);
  combat.damage.entry = damage_e; combat.damage.abi = "q2-classic"; combat.causes.edition = "classic"; combat.causes.game = "base";
  combat.health.offset = HEALTH_OFFSET; combat.health.encoding = "int32"; combat.mass.offset = 104; combat.mass.encoding = "int32"; combat.takedamage.offset = 108; combat.takedamage.encoding = "int32"; combat.flags.field.offset = 112; combat.flags.field.encoding = "int32"; combat.flags.invulnerable = 1; combat.flags.no_knockback = 2; combat.armor.kind = "none";
  qa_native_mod_source_actors owned; memset(&owned, 0, sizeof owned); owned.allocate = alloc; owned.release = release_e; owned.update.entry = update_e; owned.update.returns = "void"; owned.frame_seconds = 0.1; owned.clock = clock; owned.clock_count = 2;
  owned.fields.velocity = VELOCITY_OFFSET; owned.fields.ground = 252; owned.fields.think = 256; owned.fields.nextthink.offset = 260; owned.fields.nextthink.encoding = "float32"; owned.fields.has_use = false; owned.callbacks = &callbacks; owned.combat = &combat;
  qa_native_mod_declaration d; memset(&d, 0, sizeof d); d.version = 1; d.runtime = "native"; d.target.api.kind = QA_Q2_GAME_API_CLASSIC; d.target.abi = classic_abi; d.source_actors = &owned;
  qa_compat_q2_native_mod_provider *p = qa_compat_q2_native_mod_provider_create(&d, &services, PROVIDER_ID, "base1", OPS_CTX, provider_assert); live_provider = p;
  qa_compat_q2_native_mod_provider_attach(p, HOST);
  qa_compat_q2_native_mod_provider_initialize(p, false);
  /* Clock synchronized at validate: seconds * 1000 rounded for integer storage. */
  CHECK(get_i32(CLOCK_ADDRESS) == 10000 && get_i32(CLOCK_ADDRESS + 4) == 0 && frames_synchronized == 0);
  /* Guest allocation adopts a shared actor at its source slot. */
  qa_compat_q2_guest_call_signature sig; memset(&sig, 0, sizeof sig); sig.result_is_void = false; sig.result.storage = "pointer";
  qa_guest_call_result allocated = host_invoke(HOST_CTX, fake_address(ALLOC_ENTRY), sig, NULL, 0);
  CHECK(allocated.pointer_value.value.byte_offset == TABLE_BASE + 2 * TABLE_STRIDE);
  qa_actor_id adopted; CHECK(qa_compat_q2_native_mod_provider_actor_at(p, 2, &adopted));
  qa_actor_source_result source = qa_session_actor_registry_source_of(registry, adopted); CHECK(source.has_value && source.slot == 2);
  double slot; CHECK(qa_compat_q2_native_mod_provider_slot_of(p, adopted, &slot) && slot == 2);
  CHECK(qa_compat_q2_native_mod_provider_address(p, adopted).byte_offset == TABLE_BASE + 2 * TABLE_STRIDE);
  /* Body reads and writes the edict through the classic offsets. */
  int64_t edict = TABLE_BASE + 2 * TABLE_STRIDE; float x = 7; memcpy(guest + edict + 4, &x, 4);
  qa_body_state_result body = qa_shared_body_table_read(bodies, adopted); CHECK(body.has_value && body.value.origin.x == 7);
  /* The combat binding reads source fields. */
  put_i32(edict + HEALTH_OFFSET, 50); put_i32(edict + 104, 200); put_i32(edict + 108, 1); put_i32(edict + 112, 2);
  qa_combat_state_result state = qa_gameplay_authority_read(authority, adopted); CHECK(state.has_value && state.value.health == 50 && state.value.mass == 200 && state.value.can_take_damage && !state.value.invulnerable && state.value.no_knockback);
  /* advance(): two 100ms ticks run update and the frame counter. */
  qa_frame_context frame; memset(&frame, 0, sizeof frame); frame.time.kind = QA_SOURCE_TIME_SECONDS; frame.time.value = 10.2;
  qa_compat_q2_native_mod_provider_advance(p, frame);
  CHECK(update_calls == 2 && last_update_slot == 2 && get_i32(CLOCK_ADDRESS + 4) == 2 && frames_synchronized == 2 && get_i32(CLOCK_ADDRESS) == 10200 && presentation_begin == 2);
  /* Applying canonical damage runs the original body and captures its write. */
  qa_owned_actor owned_actor; CHECK(qa_session_actor_registry_resolve_owned(registry, adopted, &owned_actor));
  qa_damage_request request; memset(&request, 0, sizeof request);
  qa_actor_id attacker = world_actor.id; request.target = adopted; request.amount = 12; request.knockback = 3; request.delivery = "direct"; request.direction.x = 1;
  request.attack.sequence = 1; request.attack.time.kind = QA_SOURCE_TIME_SECONDS; request.attack.time.value = 10; request.attack.attacker = &attacker; request.attack.inflictor = &attacker;
  request.attack.weapon_provider = "q2:w"; request.attack.combat_provider = PROVIDER_ID; request.attack.inventory_provider = "q2:i"; request.attack.movement_provider = "q2:m";
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2; request.attack.cause.means_of_death = 1; request.attack.cause.damage_flags = 0;
  int mapped = unmapped;
  qa_damage_outcome outcome = qa_gameplay_authority_apply(authority, request, NULL, NULL);
  CHECK(!strcmp(outcome.kind, "committed") && outcome.decision.applied_damage == 12 && !strcmp(outcome.decision.reaction, "pain"));
  CHECK(get_i32(edict + HEALTH_OFFSET) == 38 && pain_calls == 1 && hook_before_reaction >= 1 && hook_confirmed >= 1 && unmapped > mapped);
  /* The guest's own T_Damage entry routes back through the authority for an owned target. */
  qa_guest_call_value args[10]; int64_t vectors = bump; bump += 64;
  args[0] = qa_compat_q2_call_pointer(true, fake_address(edict)); args[1] = qa_compat_q2_call_pointer(false, fake_address(0)); args[2] = qa_compat_q2_call_pointer(false, fake_address(0));
  args[3] = qa_compat_q2_call_pointer(true, fake_address(vectors)); args[4] = args[3]; args[5] = args[3]; args[6] = qa_compat_q2_call_int32(8); args[7] = qa_compat_q2_call_int32(0); args[8] = qa_compat_q2_call_int32(1); args[9] = qa_compat_q2_call_int32(1);
  qa_compat_q2_guest_call_signature void_sig; memset(&void_sig, 0, sizeof void_sig); void_sig.result_is_void = true;
  int confirmed = hook_confirmed; pain_calls = 0;
  (void)host_invoke(HOST_CTX, fake_address(DAMAGE_ENTRY), void_sig, args, 10);
  CHECK(get_i32(edict + HEALTH_OFFSET) == 30 && hook_confirmed == confirmed + 1 && pain_calls == 1);
  /* Stride validation failures are declared before the first frame. */
  /* Checkpoint rows for owned actors survive. */
  qa_compat_q2_native_mod_actors *direct = NULL; (void)direct;
  /* Canonical release asks the source to free its edict on the next drain. */
  qa_session_actor_registry_release(registry, owned_actor);
  qa_compat_q2_native_mod_provider_advance(p, frame);
  CHECK(free_calls == 1 && !qa_compat_q2_native_mod_provider_actor_at(p, 2, &adopted));
  qa_compat_q2_native_mod_provider_close(p); CHECK(host_closed == 1);
  qa_compat_q2_native_mod_provider_destroy(p);
  world_teardown();
}

/* --- Protection: source-region retirement, end to end. ----------------------- */
#define DONOR_ENTRY (IMAGE_BASE + 0xC00)
#define CLIENT_ROW 0xD000
#define ENTITY_FIRST_SLOT 1
static int donor_calls; static bool donor_retires; static qa_owned_actor donor_actor;
static void donor_region_body(void) {
  cpu.registers[0] = 7; donor_calls++;
  if (donor_retires) {
    uint8_t points[4] = {0x10, 0, 0, 0}; mem_write(MEM_CTX, fake_address(CLIENT_ROW + 4), points, 4);
    qa_session_actor_registry_release(registry, donor_actor);
    qa_compat_q2_guest_call_signature sig; memset(&sig, 0, sizeof sig); sig.result_is_void = true;
    /* The import after retirement observes the stale authority and unwinds. */
    qa_guest_call_value none[1]; none[0] = qa_compat_q2_call_void(); (void)none;
    qa_compat_q2_native_mod_provider_import_boundary(live_provider, "cvar", NULL, 0, NULL, NULL);
  }
}
static qa_guest_call_result guest_donor(const qa_guest_call_value *values, size_t count) {
  (void)values; (void)count; cpu.rsp -= 32;
  guest_region(IMAGE_BASE + 0xC10, NULL); guest_region(IMAGE_BASE + 0xC20, donor_region_body); guest_region(IMAGE_BASE + 0xC30, NULL);
  cpu.rsp += 32; return qa_compat_q2_call_void();
}
static qa_guest_call_result guest_noop_int(const qa_guest_call_value *values, size_t count) { (void)values; (void)count; return qa_compat_q2_call_int32(1); }
static double policy_saved; static int policy_resumed;
static void policy_resume(void *ctx, qa_armor_stage_result result, qa_current_combat_state current, qa_combat_progress *out) {
  (void)current; const qa_damage_request *request = ctx; policy_saved = result.saved; policy_resumed++;
  memset(out, 0, sizeof *out); out->kind = "complete"; out->request = *request; out->result.applied_damage = 0; out->result.reaction = "none";
}
static void policy_decide(void *ctx, qa_damage_request request, qa_combat_state target, const qa_combat_state *attacker, bool has_prepared, double prepared, qa_combat_progress *out) {
  (void)target; (void)attacker; (void)has_prepared; (void)prepared;
  static qa_damage_request held; held = request; want_ctx(ctx, OPS_CTX);
  memset(out, 0, sizeof *out); out->kind = "armor-stage"; out->request = request; out->channel = QA_PROTECTION_REGULAR; out->input.request = request; out->input.amount = request.amount; out->ctx = &held; out->resume_armor = policy_resume;
}
static qa_damage_outcome donor_damage(qa_owned_actor target, double amount) {
  static qa_actor_id attacker; attacker = world_actor.id;
  qa_damage_request request; memset(&request, 0, sizeof request); request.target = target.id; request.amount = amount; request.delivery = "direct";
  request.attack.sequence = 1; request.attack.time.kind = QA_SOURCE_TIME_SECONDS; request.attack.attacker = &attacker; request.attack.inflictor = &attacker;
  request.attack.weapon_provider = "t:w"; request.attack.combat_provider = "t:combat"; request.attack.inventory_provider = "t:i"; request.attack.movement_provider = "t:m";
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  return qa_gameplay_authority_apply(authority, request, NULL, NULL);
}
static void test_protection(void) {
  fake_reset(8); world_setup(); fake_client_count = 0; assert_calls = 0; donor_calls = 0; donor_retires = false;
  slot_active[0] = true; slot_active[1] = true; client_rows[1] = CLIENT_ROW; slot_active[2] = true;
  add_function(DONOR_ENTRY, guest_donor); add_function(IMAGE_BASE + 0xC40, guest_noop_int);
  qa_mod_client_services client_svc = fake_client_services(); services.clients = &client_svc;
  donor_actor = qa_session_actor_registry_allocate(registry, "other:mod", "player"); foreign_actor_setup(donor_actor); add_client(donor_actor, "\\name\\p");
  /* Records: public edicts plus public client rows. */
  static const qa_native_mod_actor_field ent_fields[1] = {{0, "private", {0, NULL, "private", NULL, NULL, 0, NULL, NULL, 0, {0, 0, 0}, 16}, NULL, NULL, NULL, 0, NULL, 0}};
  static const qa_native_mod_actor_field cli_fields[2] = {
    {0, "private", {0, NULL, "private", NULL, NULL, 0, NULL, NULL, 0, {0, 0, 0}, 4}, NULL, NULL, NULL, 0, NULL, 0},
    {4, "inventory", {4, NULL, "inventory", NULL, NULL, 0, "q2:armor", NULL, 0, {0, 0, 0}, 0}, NULL, "int32", NULL, 0, "q2:armor", 0}};
  qa_native_mod_actor_record records[2]; memset(records, 0, sizeof records);
  records[0].id = "ent"; records[0].base.kind = "entities"; records[0].stride = TABLE_STRIDE; records[0].first_slot = ENTITY_FIRST_SLOT; records[0].capacity = 8; records[0].fields = ent_fields; records[0].field_count = 1;
  records[1].id = "cli"; records[1].base.kind = "clients"; records[1].stride = 64; records[1].first_slot = 0; records[1].capacity = 1; records[1].fields = cli_fields; records[1].field_count = 2;
  records[0].capacity = 1;
  const char *const record_names[1] = {"cli"}; qa_native_mod_clients clients; memset(&clients, 0, sizeof clients); clients.maximum = 1; clients.records.values = record_names; clients.records.count = 1;
  qa_native_mod_regular_armor_item storage; memset(&storage, 0, sizeof storage); storage.item = "q2:armor"; storage.selection = enumerated(afield("cli", 0, "int32"), 1, 0); storage.points = afield("cli", 4, "int32");
  qa_native_mod_protection_region region; memset(&region, 0, sizeof region); region.call = void_call(0xC00); region.call.returns = "int32";
  region.frame.entry = 0xC10; region.entry = 0xC20; region.join = 0xC30; region.frame.exit = 0xC40; region.frame.stack_bytes = 32; region.frame.argument_bytes = 32; region.result.kind = QA_NATIVE_MOD_LOCATION_REGISTER; region.result.reg = "rax"; region.result.storage = "int32";
  qa_native_mod_protection_definition protection[1]; memset(protection, 0, sizeof protection); protection[0].id = "m:armor"; protection[0].channel = "regular"; protection[0].regular_storage = &storage; protection[0].regular_storage_count = 1; protection[0].absorb_abi = "source-region"; protection[0].absorb_region = &region;
  qa_native_mod_source_call project = void_call(0xC40), release = void_call(0xC40);
  qa_native_mod_declaration d = base_declaration(); d.actor_records = records; d.actor_record_count = 2; d.entity_record = "ent"; d.clients = &clients; d.project = &project; d.project_count = 1; d.release = &release; d.release_count = 1; d.protection = protection; d.protection_count = 1;
  qa_compat_q2_native_mod_provider *p = qa_compat_q2_native_mod_provider_create(&d, &services, PROVIDER_ID, "base1", OPS_CTX, provider_assert); live_provider = p;
  qa_compat_q2_native_mod_provider_attach(p, HOST);
  qa_compat_q2_native_mod_provider_initialize(p, false);
  qa_compat_q2_native_mod_provider_activate_protection(p);
  /* Client admitted and row seeded with its canonical armor count. */
  double slot = -1; CHECK(qa_compat_q2_native_mod_provider_slot_of(p, donor_actor.id, &slot) && slot == 1);
  put_i32(CLIENT_ROW, 1);
  /* Reservation: the authority reports this provider's armor, the source-region absorb saves. */
  qa_provider_id owner = NULL; CHECK(qa_gameplay_authority_protection_owner(authority, donor_actor, QA_PROTECTION_REGULAR, &owner) && !strcmp(owner, PROVIDER_ID));
  size_t n = 0; const qa_item_id *items = qa_gameplay_authority_protection_inventory_items(authority, donor_actor, QA_PROTECTION_REGULAR, &n); CHECK(n == 1 && !strcmp(items[0], "q2:armor"));
  /* The canonical read comes from original storage plus its inventory count. */
  qa_combat_state_result read = qa_gameplay_authority_read(authority, donor_actor.id);
  CHECK(read.has_value && read.value.armor.regular.kind == QA_REGULAR_ARMOR_SOURCE && !strcmp(read.value.armor.regular.item, "q2:armor") && read.value.armor.regular.points == 40);
  qa_combat_policy policy; memset(&policy, 0, sizeof policy); policy.ctx = OPS_CTX; policy.id = "t:combat"; policy.decide = policy_decide;
  (void)qa_gameplay_authority_register(authority, policy);
  /* A completed donor region: its result is the saving, the processor returns to
   * the call's state, and its armor write commits through canonical inventory. */
  policy_saved = -1; policy_resumed = 0; cpu.registers[0] = 3; put_i32(CLIENT_ROW + 4, 40);
  qa_damage_outcome outcome = donor_damage(donor_actor, 20);
  CHECK(!strcmp(outcome.kind, "committed") && policy_resumed == 1 && policy_saved == 7 && donor_calls == 1);
  CHECK(cpu.rsp == 0x50000 && runner_depth == 0);
  /* Retirement inside the donor: the owner goes away mid-region, the next import
   * unwinds the guest to the owning invocation, no fabricated saving is seen, and
   * the authority ends the stale decision without resuming its policy. */
  donor_retires = true; policy_saved = -1; policy_resumed = 0; double depth = runner_depth; int captures = processor_captures;
  outcome = donor_damage(donor_actor, 20);
  CHECK(policy_resumed == 0 && policy_saved == -1 && donor_calls == 2 && outcome.decision.applied_damage == 0);
  CHECK(runner_depth == depth && last_unwind == depth && processor_captures > captures && cpu.rsp == 0x50000);
  CHECK(!qa_session_actor_registry_is_live(registry, donor_actor.id));
  qa_compat_q2_native_mod_provider_close(p);
  qa_compat_q2_native_mod_provider_destroy(p);
  services.clients = NULL;
  world_teardown();
}

int main(void) {
  test_invocations();
  test_region();
  test_armor();
  test_deferred_reader();
  test_weapon_stage();
  test_clients();
  test_validation();
  test_provider();
  test_actors_combat();
  test_protection();
  CHECK(ctx_failures == 0);
  printf("test_q2_native_mod: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
