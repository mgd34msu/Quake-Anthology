/* Checks for the Q2 native primary/mod bindings: native-mod-items, native-mod-pickups,
 * native-pickups, native-input and native-primary-{weapons,inventory,commands,drop,player}.
 * Only native-input has a direct TS test (tests/compat/q2/rerelease/native.test.ts:301-316,
 * against the retail DLL); test_input_bridge reproduces that scenario. The rest reproduce the
 * TS branches over a scripted guest: real byte memory, write observers, entry/observer/
 * inline-region tables and a stack, with guest functions written in C. Host leaves check a
 * sentinel ctx. GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/compat/q2/native_input.h"
#include "qa/compat/q2/native_mod_items.h"
#include "qa/compat/q2/native_mod_pickups.h"
#include "qa/compat/q2/native_pickups.h"
#include "qa/compat/q2/native_primary_commands.h"
#include "qa/compat/q2/native_primary_drop.h"
#include "qa/compat/q2/native_primary_inventory.h"
#include "qa/compat/q2/native_primary_player.h"
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/player_state.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/inventory.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { checks++; fflush(NULL); pid_t pid = fork(); \
    if (pid == 0) { (void)prctl(PR_SET_DUMPABLE, 0); if (!freopen("/dev/null", "w", stderr)) _exit(0); __VA_ARGS__; _exit(0); } \
    int status = 0; waitpid(pid, &status, 0); \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

/* --- Scripted guest. --- */
typedef struct guest guest_t;
typedef qa_guest_call_result (*guest_fn)(guest_t *g, const qa_guest_call_value *args, size_t count);
typedef struct { bool active; uint64_t handle; int64_t address; void *ctx; void (*cb)(void *); } observer_row;
typedef struct { bool active; uint64_t handle; int64_t address; void *ctx; qa_compat_q2_host_invoke_fn cb; void *accepts_ctx; bool (*accepts)(void *); } entry_row;
typedef struct { bool active; uint64_t handle; int64_t entry, join; void *ctx; void (*cb)(void *, qa_compat_q2_inline_continuation *); void *accepts_ctx; bool (*accepts)(void *); } region_row;
typedef struct { bool active; uint64_t handle; int64_t address; double length; void *ctx; qa_compat_q2_write_observer_fn cb; } write_row;
typedef struct { int64_t address; guest_fn fn; } function_row;
typedef struct { uint64_t rsp, rax, rcx, rdx; } processor_t;
#define GUEST_BYTES 0x100000
#define IMAGE 0x10000
#define ENTITIES 0x40000
#define STRIDE 0x100
#define CLIENTS 0x50000
#define CLIENT_STRIDE 0x1000
#define CLIENT_FIELD 0x30
#define MAX_HEALTH 0x40
#define INUSE 0x48
#define ITEMFIELD 0x50
#define COUNTFIELD 0x58
#define SPAWNFLAGS 0x5c
#define GENERATION 0x60
#define VELOCITY 0x70
#define STRINGS 0x20000
struct guest {
  uint8_t *bytes; int64_t next_alloc, next_string;
  qa_compat_q2_memory memory; qa_compat_q2_runner runner;
  observer_row observers[64]; size_t observer_count;
  entry_row entries[64]; size_t entry_count;
  region_row regions[128]; size_t region_count;
  write_row writes[64]; size_t write_count;
  function_row functions[64]; size_t function_count;
  processor_t cpu; double depth; uint64_t next_handle;
  qa_guest_address image; int unmaps, allocations;
};
static const char space_token = 0;
static const char *DIGEST = "sha256:test";
static qa_guest_address addr(int64_t offset) { return (qa_guest_address){&space_token, offset}; }
static qa_guest_address_result m_pointer(void *ctx, int64_t raw) { (void)ctx; qa_guest_address_result r; r.has_value = raw != 0; r.value = addr(raw); return r; }
static qa_guest_address m_offset(void *ctx, qa_guest_address a, int64_t d) { (void)ctx; a.byte_offset += d; return a; }
static qa_guest_address m_allocate(void *ctx, double n, bool has_alignment, int64_t alignment, const char *label) {
  guest_t *g = ctx; (void)has_alignment; (void)alignment; (void)label;
  int64_t at = g->next_alloc; g->next_alloc += (int64_t)n + 16; g->allocations++;
  return addr(at);
}
static void m_unmap(void *ctx, qa_guest_address a, double n) { guest_t *g = ctx; (void)a; (void)n; g->unmaps++; }
static void m_check(void *ctx, qa_guest_address a, double n, const char *access) { (void)ctx; (void)access; if (a.byte_offset < 0 || a.byte_offset + (int64_t)n > GUEST_BYTES) qa_range_error("guest check"); }
static uint8_t *m_borrow(void *ctx, qa_guest_address a, double n) { guest_t *g = ctx; m_check(ctx, a, n, "read"); return g->bytes + a.byte_offset; }
static void m_copy(void *ctx, qa_guest_address a, uint8_t *dest, double n) { guest_t *g = ctx; m_check(ctx, a, n, "read"); memcpy(dest, g->bytes + a.byte_offset, (size_t)n); }
static void m_copy_into(void *ctx, qa_guest_address a, uint8_t *dest, size_t dest_length, double n) { (void)dest_length; m_copy(ctx, a, dest, n); }
static void m_write(void *ctx, qa_guest_address a, const uint8_t *bytes, size_t n) {
  guest_t *g = ctx; m_check(ctx, a, (double)n, "write"); memcpy(g->bytes + a.byte_offset, bytes, n);
  for (size_t i = 0; i < g->write_count; i++) {
    write_row *w = &g->writes[i];
    if (w->active && a.byte_offset < w->address + (int64_t)w->length && w->address < a.byte_offset + (int64_t)n) w->cb(w->ctx);
  }
}
static uint64_t m_observe_writes(void *ctx, qa_guest_address a, double n, void *cb_ctx, qa_compat_q2_write_observer_fn cb) {
  guest_t *g = ctx;
  size_t slot = 0;
  while (slot < g->write_count && g->writes[slot].active) slot++;
  if (slot == g->write_count) g->write_count++;
  g->writes[slot] = (write_row){true, ++g->next_handle, a.byte_offset, n, cb_ctx, cb};
  return g->writes[slot].handle;
}
static void m_unobserve_writes(void *ctx, uint64_t handle) { guest_t *g = ctx; for (size_t i = 0; i < g->write_count; i++) if (g->writes[i].handle == handle) g->writes[i].active = false; }
static guest_fn function_at(const guest_t *g, int64_t address) { for (size_t i = 0; i < g->function_count; i++) if (g->functions[i].address == address) return g->functions[i].fn; return NULL; }
/* One guest call: stack frame, entry observers, then the accepted entry or the guest body. */
static qa_guest_call_result g_call(guest_t *g, qa_guest_address target, const qa_guest_call_value *args, size_t count, int64_t return_address) {
  g->depth++;
  g->cpu.rsp -= (uint64_t)g->memory.pointer_bytes;
  qa_compat_q2_mem_write_pointer(&g->memory, addr((int64_t)g->cpu.rsp), return_address != 0, addr(return_address));
  for (size_t i = 0; i < g->observer_count; i++) if (g->observers[i].active && g->observers[i].address == target.byte_offset) g->observers[i].cb(g->observers[i].ctx);
  qa_guest_call_result result;
  entry_row *entry = NULL;
  for (size_t i = 0; i < g->entry_count && entry == NULL; i++) if (g->entries[i].active && g->entries[i].address == target.byte_offset && (g->entries[i].accepts == NULL || g->entries[i].accepts(g->entries[i].accepts_ctx))) entry = &g->entries[i];
  if (entry != NULL) result = entry->cb(entry->ctx, NULL, args, count);
  else {
    guest_fn fn = function_at(g, target.byte_offset);
    if (fn == NULL) qa_range_error("guest has no function at %lld", (long long)target.byte_offset);
    result = fn(g, args, count);
  }
  g->cpu.rsp += (uint64_t)g->memory.pointer_bytes;
  g->depth--;
  return result;
}
typedef struct { guest_t *g; void (*body)(guest_t *, void *); void *ctx; bool consumed; } continuation_state;
static void cont_execute(void *v) { continuation_state *c = v; c->consumed = true; c->body(c->g, c->ctx); }
static void cont_skip(void *v) { continuation_state *c = v; c->consumed = true; }
/* A guest inline region: interceptable when bound and accepted, else the body runs. */
static void g_region(guest_t *g, int64_t entry, void (*body)(guest_t *, void *), void *ctx) {
  for (size_t i = 0; i < g->region_count; i++) {
    region_row *r = &g->regions[i];
    if (r->active && r->entry == entry && (r->accepts == NULL || r->accepts(r->accepts_ctx))) {
      continuation_state state = {g, body, ctx, false};
      qa_compat_q2_inline_continuation continuation = {&state, cont_execute, cont_skip};
      r->cb(r->ctx, &continuation);
      if (!state.consumed) qa_range_error("Inline interceptor did not join its source frame");
      return;
    }
  }
  body(g, ctx);
}
static uint64_t r_observe_entry(void *ctx, qa_guest_address a, void *cb_ctx, void (*cb)(void *)) { guest_t *g = ctx; g->observers[g->observer_count] = (observer_row){true, ++g->next_handle, a.byte_offset, cb_ctx, cb}; return g->observers[g->observer_count++].handle; }
static uint64_t r_bind_entry(void *ctx, qa_guest_address a, qa_callback_id id, qa_compat_q2_guest_call_signature signature, void *cb_ctx, qa_compat_q2_host_invoke_fn cb, void *accepts_ctx, bool (*accepts)(void *)) {
  guest_t *g = ctx; (void)id; (void)signature; g->entries[g->entry_count] = (entry_row){true, ++g->next_handle, a.byte_offset, cb_ctx, cb, accepts_ctx, accepts}; return g->entries[g->entry_count++].handle;
}
static void r_remove_entry(void *ctx, uint64_t handle) {
  guest_t *g = ctx;
  for (size_t i = 0; i < g->entry_count; i++) if (g->entries[i].handle == handle) g->entries[i].active = false;
  for (size_t i = 0; i < g->observer_count; i++) if (g->observers[i].handle == handle) g->observers[i].active = false;
}
static uint64_t r_bind_region_accepts(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi, void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *), void *accepts_ctx, bool (*accepts)(void *)) {
  guest_t *g = ctx; (void)abi;
  if (entry.byte_offset == join.byte_offset) qa_range_error("Invalid or occupied inline region");
  for (size_t i = 0; i < g->region_count; i++) if (g->regions[i].active && g->regions[i].entry == entry.byte_offset) qa_range_error("Invalid or occupied inline region");
  g->regions[g->region_count] = (region_row){true, ++g->next_handle, entry.byte_offset, join.byte_offset, cb_ctx, cb, accepts_ctx, accepts};
  return g->regions[g->region_count++].handle;
}
static uint64_t r_bind_region(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi, void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *)) { return r_bind_region_accepts(ctx, entry, join, abi, cb_ctx, cb, NULL, NULL); }
static void r_remove_region(void *ctx, uint64_t handle) { guest_t *g = ctx; for (size_t i = 0; i < g->region_count; i++) if (g->regions[i].handle == handle) g->regions[i].active = false; }
static uint64_t *reg_slot(guest_t *g, const char *name) {
  if (strcmp(name, "rsp") == 0) return &g->cpu.rsp;
  if (strcmp(name, "rax") == 0) return &g->cpu.rax;
  if (strcmp(name, "rcx") == 0) return &g->cpu.rcx;
  if (strcmp(name, "rdx") == 0) return &g->cpu.rdx;
  qa_range_error("guest register %s", name);
}
static uint64_t r_read_register(void *ctx, const char *name, int width) { uint64_t v = *reg_slot(ctx, name); return width == 32 ? (uint32_t)v : v; }
static void r_write_register(void *ctx, const char *name, int width, uint64_t value) { *reg_slot(ctx, name) = width == 32 ? (uint32_t)value : value; }
static void *r_capture(void *ctx) { guest_t *g = ctx; processor_t *p = malloc(sizeof *p); *p = g->cpu; return p; }
static void r_restore(void *ctx, void *snapshot) { guest_t *g = ctx; g->cpu = *(processor_t *)snapshot; }
static void r_free_processor(void *ctx, void *snapshot) { (void)ctx; free(snapshot); }
static double r_depth(void *ctx) { return ((guest_t *)ctx)->depth; }
static void r_unwind(void *ctx, double depth) { ((guest_t *)ctx)->depth = depth; }
static guest_t *guest_create(int64_t pointer_bytes) {
  guest_t *g = calloc(1, sizeof *g);
  g->bytes = calloc(1, GUEST_BYTES);
  g->next_alloc = 0x90000; g->next_string = STRINGS; g->cpu.rsp = 0x8f000; g->image = addr(IMAGE);
  qa_compat_q2_memory *m = &g->memory;
  m->ctx = g; m->module.id = "test:module"; m->module.digest = DIGEST; m->address_space = &space_token; m->pointer_bytes = pointer_bytes;
  m->pointer = m_pointer; m->offset = m_offset; m->allocate = m_allocate; m->unmap = m_unmap; m->check = m_check; m->borrow = m_borrow; m->copy = m_copy; m->copy_into = m_copy_into; m->write = m_write;
  m->observe_writes = m_observe_writes; m->unobserve_writes = m_unobserve_writes;
  qa_compat_q2_runner *r = &g->runner;
  r->ctx = g; r->memory = m; r->observe_entry = r_observe_entry; r->bind_entry = r_bind_entry; r->remove_entry = r_remove_entry;
  r->bind_inline_region = r_bind_region; r->bind_inline_region_accepts = r_bind_region_accepts; r->remove_inline_region = r_remove_region;
  r->read_register = r_read_register; r->write_register = r_write_register; r->capture_processor = r_capture; r->restore_processor = r_restore; r->free_processor = r_free_processor;
  r->depth = r_depth; r->unwind = r_unwind;
  return g;
}
static void guest_destroy(guest_t *g) { free(g->bytes); free(g); }
static void g_fn(guest_t *g, int64_t rva, guest_fn fn) { g->functions[g->function_count++] = (function_row){IMAGE + rva, fn}; }
static qa_guest_address g_string(guest_t *g, const char *text) {
  int64_t at = g->next_string; size_t length = strlen(text) + 1;
  memcpy(g->bytes + at, text, length); g->next_string += (int64_t)length;
  return addr(at);
}
static void put_ptr(guest_t *g, int64_t at, qa_guest_address value) { qa_compat_q2_mem_write_pointer(&g->memory, addr(at), true, value); }
static void put_i32(guest_t *g, int64_t at, int32_t v) { qa_compat_q2_mem_write_i32(&g->memory, addr(at), v); }
static int32_t get_i32(guest_t *g, int64_t at) { return qa_compat_q2_mem_read_i32(&g->memory, addr(at)); }
static int64_t get_ptr(guest_t *g, int64_t at) { qa_guest_address out; return qa_compat_q2_mem_read_pointer(&g->memory, addr(at), &out) ? out.byte_offset : 0; }
static double get_f32(guest_t *g, int64_t at) { return qa_compat_q2_mem_read_f32(&g->memory, addr(at)); }
static qa_guest_call_value ptr_value(int64_t at) { return qa_compat_q2_call_pointer(true, addr(at)); }
/* --- Host with entity records. --- */
typedef struct { guest_t *g; bool alive[8]; qa_actor_id actors[8]; int invokes; } world_t;
static world_t WORLD;
static guest_t *GUEST;
static const char token_a = 0;
#define CHECK_WORLD(ctx) CHECK((ctx) == &WORLD)
static qa_actor_id actor_at(int slot) { qa_actor_id a; memset(&a, 0, sizeof a); a.slot = slot; a.generation = 1; a.token = &token_a; return a; }
static int64_t entity_at(int slot) { return ENTITIES + slot * STRIDE; }
static int64_t client_at(int slot) { return CLIENTS + slot * CLIENT_STRIDE; }
static qa_raw_entity_view view_of(int slot) { qa_raw_entity_view r; memset(&r, 0, sizeof r); r.slot = slot; r.address = addr(entity_at(slot)); return r; }
static qa_guest_call_result h_invoke(void *ctx, qa_guest_address a, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  world_t *w = ctx; (void)signature; CHECK_WORLD(ctx); w->invokes++;
  return g_call(w->g, a, values, count, 0);
}
static qa_guest_address h_entry(void *ctx, const char *name) { CHECK_WORLD(ctx); qa_range_error("no export %s", name); }
static qa_raw_entity_view h_record(void *ctx, qa_guest_address a) { CHECK_WORLD(ctx); return view_of((int)((a.byte_offset - ENTITIES) / STRIDE)); }
static bool h_actor(void *ctx, qa_raw_entity_view record, qa_actor_id *out) {
  world_t *w = ctx; CHECK_WORLD(ctx);
  int slot = (int)record.slot;
  if (slot < 0 || slot >= 8 || !w->alive[slot]) return false;
  *out = w->actors[slot];
  return true;
}
static bool h_record_for(void *ctx, qa_actor_id actor, qa_raw_entity_view *out) {
  world_t *w = ctx; CHECK_WORLD(ctx);
  if (actor.slot < 0 || actor.slot >= 8 || !w->alive[actor.slot] || !qa_same_actor(w->actors[actor.slot], actor)) return false;
  *out = view_of((int)actor.slot);
  return true;
}
static const qa_native_abi ABI64 = {"windows-x86-64", "pe32+", 8, "microsoft-x64"};
static const qa_native_abi ABI32 = {"windows-i386", "pe32", 4, "cdecl"};
static qa_compat_q2_native_primary_weapon_host setup(int64_t pointer_bytes) {
  GUEST = guest_create(pointer_bytes);
  memset(&WORLD, 0, sizeof WORLD);
  WORLD.g = GUEST;
  for (int slot = 0; slot < 8; slot++) {
    WORLD.actors[slot] = actor_at(slot); WORLD.alive[slot] = true;
    put_ptr(GUEST, entity_at(slot) + CLIENT_FIELD, addr(client_at(slot)));
    put_i32(GUEST, entity_at(slot) + INUSE, 1);
  }
  qa_compat_q2_native_primary_weapon_host host = {&GUEST->memory, &GUEST->runner, GUEST->image, &WORLD, h_invoke, h_entry, h_record, h_actor, h_record_for};
  return host;
}
static void teardown(void) { guest_destroy(GUEST); GUEST = NULL; }
static qa_native_item_field FIELD(const char *record, double offset, const char *encoding) { qa_native_item_field f; f.record = record; f.field.offset = offset; f.field.encoding = encoding; return f; }
static qa_guest_call_result g_void(guest_t *g, const qa_guest_call_value *args, size_t count) { (void)g; (void)args; (void)count; return qa_compat_q2_call_void(); }

/* --- native-primary-player --- */
static int spawn_args;
static double spawn_return;
static qa_guest_call_result g_spawn(guest_t *g, const qa_guest_call_value *args, size_t count) {
  spawn_args = (int)count;
  qa_compat_q2_mem_write_f32(&g->memory, args[1].pointer_value.value, 11);
  qa_compat_q2_mem_write_f32(&g->memory, addr(args[1].pointer_value.value.byte_offset + 8), 13);
  qa_compat_q2_mem_write_f32(&g->memory, addr(args[2].pointer_value.value.byte_offset + 4), 90);
  return qa_compat_q2_call_uint32(spawn_return);
}
static int objectives_seen;
static qa_guest_call_result g_objectives(guest_t *g, const qa_guest_call_value *args, size_t count) { (void)g; CHECK(count == 1 && args[0].pointer_value.value.byte_offset == entity_at(1)); objectives_seen++; return qa_compat_q2_call_void(); }
static void init_weapon_profile(qa_compat_q2_native_primary_weapon_profile *w, qa_native_abi abi) {
  memset(w, 0, sizeof *w);
  w->digest = DIGEST; w->abi = abi; w->entity_client = CLIENT_FIELD; w->client_byte_length = 0x800; w->client_view_angles = 0x100;
  w->entity_max_health = FIELD("entity", MAX_HEALTH, "int32");
  w->delay_evaluate_kind = "source-flag"; w->damage_kind = "source-result"; w->damage_result = "int32";
}
static void test_player(void) {
  qa_compat_q2_native_primary_weapon_host host = setup(8);
  qa_compat_q2_native_primary_weapon_profile weapon; init_weapon_profile(&weapon, ABI64);
  qa_compat_q2_native_primary_player_profile profile;
  memset(&profile, 0, sizeof profile);
  profile.digest = DIGEST; profile.has_match = true; profile.match.score = 0x200; profile.spawn = 0x300; profile.objectives_kind = "none"; profile.command_angles = 0x180; profile.velocity = VELOCITY; profile.has_forward = true; profile.forward = 0x1c0;
  qa_compat_q2_native_primary_player player;
  qa_compat_q2_native_primary_player_init(&player, host, &weapon, &profile);
  qa_actor_id one = actor_at(1);
  qa_compat_q2_native_primary_player_set_score(&player, one, 42);
  CHECK(qa_compat_q2_native_primary_player_score(&player, one) == 42);
  CHECK(get_i32(GUEST, client_at(1) + 0x200) == 42);
  CHECK_ABORTS(qa_compat_q2_native_primary_player_set_score(&player, one, 0.5));
  qa_compat_q2_native_primary_player_set_max_health(&player, one, 250);
  CHECK(qa_compat_q2_native_primary_player_max_health(&player, one) == 250);
  CHECK(get_i32(GUEST, entity_at(1) + MAX_HEALTH) == 250);
  CHECK_ABORTS(qa_compat_q2_native_primary_player_set_max_health(&player, one, 0));
  CHECK_ABORTS(qa_compat_q2_native_primary_player_set_max_health(&player, one, 1.5));
  CHECK_ABORTS(qa_compat_q2_native_primary_player_set_max_health(&player, one, 2147483648.0));
  /* Misaligned or oversized score declarations are refused. */
  qa_compat_q2_native_primary_player_profile bad = profile; bad.match.score = 0x202;
  CHECK_ABORTS(qa_compat_q2_native_primary_player_init(&player, host, &weapon, &bad));
  bad.match.score = 0x800;
  CHECK_ABORTS(qa_compat_q2_native_primary_player_init(&player, host, &weapon, &bad));
  qa_compat_q2_native_primary_weapon_profile other = weapon; other.digest = "sha256:other";
  CHECK_ABORTS(qa_compat_q2_native_primary_player_init(&player, host, &other, &profile));
  bad = profile; bad.has_match = false;
  qa_compat_q2_native_primary_player unmatched;
  qa_compat_q2_native_primary_player_init(&unmatched, host, &weapon, &bad);
  CHECK_ABORTS(qa_compat_q2_native_primary_player_score(&unmatched, one));
  /* A retired actor or one with no client is refused. */
  WORLD.alive[1] = false;
  CHECK_ABORTS(qa_compat_q2_native_primary_player_score(&player, one));
  WORLD.alive[1] = true;
  qa_compat_q2_mem_write_pointer(&GUEST->memory, addr(entity_at(2) + CLIENT_FIELD), false, addr(0));
  CHECK_ABORTS(qa_compat_q2_native_primary_player_score(&player, actor_at(2)));
  /* Objectives. */
  qa_compat_q2_native_primary_player_drop_objectives(&player, one);
  CHECK(WORLD.invokes == 0);
  g_fn(GUEST, 0x310, g_objectives);
  bad = profile; bad.objectives_kind = "entry"; bad.objectives_entry = 0x310;
  qa_compat_q2_native_primary_player entry_player;
  qa_compat_q2_native_primary_player_init(&entry_player, host, &weapon, &bad);
  qa_compat_q2_native_primary_player_drop_objectives(&entry_player, one);
  CHECK(objectives_seen == 1);
  /* Spawn selector, rerelease ABI (5 args, uint8 result). */
  g_fn(GUEST, 0x300, g_spawn);
  spawn_return = 1;
  int unmaps = GUEST->unmaps;
  qa_compat_q2_native_spawn_point point = qa_compat_q2_native_primary_player_spawn_point(&player, one);
  CHECK(spawn_args == 5); CHECK(point.origin.x == 11 && point.origin.z == 13); CHECK(point.angles.y == 90);
  CHECK(GUEST->unmaps == unmaps + 1);
  spawn_return = 0;
  CHECK_ABORTS(qa_compat_q2_native_primary_player_spawn_point(&player, one));
  /* Rerelease teleport. */
  uint8_t *state = GUEST->bytes + client_at(1);
  qa_compat_q2_view_set_u16(state, 28, 4);
  qa_compat_q2_view_set_f32(state, 0x180, 10);
  qa_vec3 origin = {1, 2, 3}, velocity = {4, 5, 6}, angles = {30, 40, 50};
  qa_compat_q2_native_primary_player_teleport(&player, one, origin, velocity, angles, 70000);
  CHECK(get_f32(GUEST, entity_at(1) + 4) == 1 && get_f32(GUEST, entity_at(1) + 28 + 8) == 3);
  CHECK(get_f32(GUEST, entity_at(1) + VELOCITY + 4) == 5);
  CHECK(qa_compat_q2_view_f32(state, 4) == 1 && qa_compat_q2_view_f32(state, 16 + 8) == 6);
  CHECK(qa_compat_q2_view_u16(state, 28) == 32); CHECK(qa_compat_q2_view_u16(state, 30) == 65535);
  CHECK(qa_compat_q2_view_f32(state, 36) == 20); CHECK(qa_compat_q2_view_f32(state, 52 + 4) == 40);
  CHECK(qa_compat_q2_view_f32(state, 0x100 + 8) == 50);
  qa_angle_vectors expected = qa_qvm_angle_vectors(angles);
  CHECK(get_f32(GUEST, client_at(1) + 0x1c0) == (double)(float)expected.forward.x);
  teardown();
  /* Classic ABI: 3 spawn args and packed pmove_state fields. */
  host = setup(4);
  init_weapon_profile(&weapon, ABI32);
  profile.has_forward = false;
  qa_compat_q2_native_primary_player_init(&player, host, &weapon, &profile);
  g_fn(GUEST, 0x300, g_spawn);
  spawn_return = 0; /* classic ignores the result */
  point = qa_compat_q2_native_primary_player_spawn_point(&player, one);
  CHECK(spawn_args == 3); CHECK(point.origin.x == 11);
  state = GUEST->bytes + client_at(1);
  state[16] = 4;
  qa_compat_q2_view_set_f32(state, 0x180, 10);
  origin = (qa_vec3){1.5, -2, 3}; velocity = (qa_vec3){-0.25, 0, 100};
  angles = (qa_vec3){55, 0, 0};
  qa_compat_q2_native_primary_player_teleport(&player, one, origin, velocity, angles, 100);
  CHECK(qa_compat_q2_view_i16(state, 4) == 12 && qa_compat_q2_view_i16(state, 6) == -16);
  CHECK(qa_compat_q2_view_i16(state, 10) == -2 && qa_compat_q2_view_i16(state, 14) == 800);
  CHECK(state[16] == 32); CHECK(state[17] == 13);
  CHECK(qa_compat_q2_view_i16(state, 20) == (int16_t)8192); /* 45 * 65536/360 */
  CHECK(qa_compat_q2_view_f32(state, 28) == 55);
  qa_compat_q2_native_primary_player_teleport(&player, one, origin, velocity, angles, 5000);
  CHECK(state[17] == 255);
  teardown();
}
/* --- native-primary-commands --- */
#define ITEM_STRIDE 0x40
#define ITEM_CLASSNAME 0
#define ITEM_ICON 8
#define ITEM_FLAGS 16
#define ITEM_AMMO 20
#define ITEM_LABEL 24
#define ITEM_AMMO_NAME 40
#define TABLE 0x1000
#define ITEM_AT(i) (IMAGE + TABLE + (i) * ITEM_STRIDE)
static void put_item(guest_t *g, int index, const char *classname, const char *icon, uint32_t flags, int32_t ammo, const char *label) {
  put_ptr(g, ITEM_AT(index) + ITEM_CLASSNAME, g_string(g, classname));
  put_ptr(g, ITEM_AT(index) + ITEM_ICON, g_string(g, icon));
  put_i32(g, ITEM_AT(index) + ITEM_FLAGS, (int32_t)flags);
  put_i32(g, ITEM_AT(index) + ITEM_AMMO, ammo);
  put_ptr(g, ITEM_AT(index) + ITEM_LABEL, g_string(g, label));
}
static void standard_items(guest_t *g) {
  /* 0 is the unqualified null row every Q2 item table begins with. */
  put_item(g, 0, "", "", 0, 0, "");
  put_item(g, 1, "weapon_blaster", "w_blaster", 1, 0, "Blaster");
  put_item(g, 2, "weapon_shotgun", "w_shotgun", 1, 3, "Shotgun");
  put_item(g, 3, "ammo_shells", "a_shells", 2, 0, "Shells");
  put_item(g, 4, "item_quad", "p_quad", 0, 0, "Quad Damage");
}
static qa_compat_q2_native_primary_command_profile command_profile(const qa_compat_q2_ammo_grant *grants, size_t count) {
  qa_compat_q2_native_primary_command_profile p;
  memset(&p, 0, sizeof p);
  p.digest = DIGEST; p.abi = ABI64; p.give_entry = 0x100; p.give_weapons = 0x110; p.give_ammo = 0x118; p.give_unknown_entry = 0x120; p.give_unknown_join = 0x128;
  p.ammo_grants = grants; p.ammo_grant_count = count; p.give_argc = 0x800; p.give_argv = 0x808;
  p.drop_entry = 0x200; p.drop_elig_entry = 0x210; p.drop_elig_join = 0x218;
  p.client_pointer = CLIENT_FIELD; p.client_weapon = 0x10; p.has_client_ammo_index = true; p.client_ammo_index = 0x18; p.client_inventory = 0x40;
  p.items_table = TABLE; p.items_stride = ITEM_STRIDE; p.items_count = 5; p.items_classname = ITEM_CLASSNAME; p.items_flags = ITEM_FLAGS; p.items_weapon_flag = 1; p.items_ammunition_flag = 2; p.items_icon = ITEM_ICON;
  p.ammo_kind = "index"; p.ammo_offset = ITEM_AMMO; p.ammo_label = ITEM_LABEL;
  return p;
}
typedef struct { const char *give_categories[4]; int give_count; size_t arg_count; bool give_item_result; qa_item_id ammo_item; const char *ammo_kind; double ammo_amount; int ammo_calls;
  bool has_drop; qa_item_id drop_item; double drop_ammo; } command_log;
static command_log CMD;
static void hook_give(void *ctx, qa_actor_id a, const char *category) { CHECK(ctx == &CMD); CHECK(a.slot == 1); if (CMD.give_count < 4) CMD.give_categories[CMD.give_count] = category; CMD.give_count++; }
static bool hook_give_item(void *ctx, qa_actor_id a, const char *const *args, size_t n) {
  (void)a; CHECK(ctx == &CMD); CMD.arg_count = n;
  CHECK(n == 2); CHECK(strcmp(args[0], "quad") == 0 && strcmp(args[1], "9") == 0);
  return CMD.give_item_result;
}
static void hook_give_ammo(void *ctx, qa_actor_id a, qa_item_id item, const char *kind, double amount) { (void)a; CHECK(ctx == &CMD); CMD.ammo_item = item; CMD.ammo_kind = kind; CMD.ammo_amount = amount; CMD.ammo_calls++; }
static bool hook_cmd_drop(void *ctx, qa_actor_id a, qa_item_id *item, double *ammo) { (void)a; CHECK(ctx == &CMD); if (!CMD.has_drop) return false; *item = CMD.drop_item; *ammo = CMD.drop_ammo; return true; }
static bool unknown_body_ran; static bool ammo_add_mode;
static int64_t seen_weapon, seen_ammo_index; static int32_t seen_shells;
static void unknown_body(guest_t *g, void *v) { (void)g; (void)v; unknown_body_ran = true; }
static void ammo_body(guest_t *g, void *v) { (void)v; int32_t current = get_i32(g, client_at(1) + 0x40 + 3 * 4); put_i32(g, client_at(1) + 0x40 + 3 * 4, ammo_add_mode ? current + 5 : 77); }
static qa_guest_call_result g_give(guest_t *g, const qa_guest_call_value *args, size_t count) {
  (void)args; (void)count;
  g_call(g, addr(IMAGE + 0x110), NULL, 0, 0);
  g_call(g, addr(IMAGE + 0x118), NULL, 0, 0);
  g_region(g, IMAGE + 0x120, unknown_body, NULL);
  g->cpu.rdx = ITEM_AT(3);
  g_region(g, IMAGE + 0x130, ammo_body, NULL);
  g_region(g, IMAGE + 0x140, ammo_body, NULL);
  return qa_compat_q2_call_void();
}
static qa_guest_call_result g_argc(guest_t *g, const qa_guest_call_value *args, size_t count) { (void)g; (void)args; (void)count; return qa_compat_q2_call_int32(3); }
static qa_guest_call_result g_argv(guest_t *g, const qa_guest_call_value *args, size_t count) {
  (void)count;
  return ptr_value(g_string(g, args[0].number_value == 1 ? "quad" : "9").byte_offset);
}
static void drop_body(guest_t *g, void *v) { (void)v; seen_weapon = get_ptr(g, client_at(1) + 0x10); seen_ammo_index = get_i32(g, client_at(1) + 0x18); seen_shells = get_i32(g, client_at(1) + 0x40 + 3 * 4); }
static qa_guest_call_result g_drop(guest_t *g, const qa_guest_call_value *args, size_t count) { (void)args; (void)count; g_region(g, IMAGE + 0x210, drop_body, NULL); return qa_compat_q2_call_void(); }
static qa_compat_q2_native_primary_command_hooks command_hooks(void) { qa_compat_q2_native_primary_command_hooks h = {&CMD, hook_give, hook_give_item, hook_give_ammo, hook_cmd_drop}; return h; }
static void with_weapon_run(void *v) { guest_t *g = GUEST; int64_t *out = v; *out = get_ptr(g, client_at(1) + 0x10); }
static void test_commands(void) {
  qa_compat_q2_native_primary_weapon_host host = setup(8);
  standard_items(GUEST);
  static const qa_compat_q2_ammo_grant grants[2] = {{0x130, 0x138, "rdx", "set"}, {0x140, 0x148, "rdx", "add"}};
  qa_compat_q2_native_primary_command_profile profile = command_profile(grants, 1);
  put_ptr(GUEST, IMAGE + 0x800, addr(IMAGE + 0x840)); put_ptr(GUEST, IMAGE + 0x808, addr(IMAGE + 0x848));
  g_fn(GUEST, 0x840, g_argc); g_fn(GUEST, 0x848, g_argv);
  g_fn(GUEST, 0x100, g_give); g_fn(GUEST, 0x110, g_void); g_fn(GUEST, 0x118, g_void); g_fn(GUEST, 0x200, g_drop);
  memset(&CMD, 0, sizeof CMD);
  qa_compat_q2_native_primary_commands *commands = qa_compat_q2_native_primary_commands_create(host, &profile, command_hooks());
  size_t count = 0;
  const qa_compat_q2_native_inventory_slot *slots = qa_compat_q2_native_primary_commands_inventory_slots(commands, &count);
  CHECK(count == 4);
  CHECK(strcmp(slots[0].item, "q2:weapon_blaster") == 0 && slots[0].index == 1 && slots[0].weapon && strcmp(slots[0].icon, "w_blaster") == 0);
  CHECK(slots[2].ammunition && !slots[2].weapon);
  const qa_compat_q2_native_source_weapon *weapons = qa_compat_q2_native_primary_commands_weapons(commands, &count);
  CHECK(count == 2); CHECK(weapons[0].ammo == NULL); CHECK(strcmp(weapons[1].ammo, "q2:ammo_shells") == 0);
  qa_compat_q2_native_source_item source;
  CHECK(qa_compat_q2_native_primary_commands_source_item(commands, "q2:item_quad", &source) && source.index == 4 && source.address.byte_offset == ITEM_AT(4));
  CHECK(!qa_compat_q2_native_primary_commands_source_item(commands, "q2:none", &source));
  CHECK(strcmp(qa_compat_q2_native_primary_commands_item_at(commands, addr(ITEM_AT(2))), "q2:weapon_shotgun") == 0);
  CHECK(qa_compat_q2_native_primary_commands_item_at(commands, addr(ITEM_AT(0))) == NULL);
  /* with_weapon projects the descriptor and restores it. */
  put_ptr(GUEST, client_at(1) + 0x10, addr(ITEM_AT(1)));
  int64_t seen = 0;
  qa_compat_q2_native_primary_commands_with_weapon(commands, actor_at(1), "q2:weapon_shotgun", &seen, with_weapon_run);
  CHECK(seen == ITEM_AT(2)); CHECK(get_ptr(GUEST, client_at(1) + 0x10) == ITEM_AT(1));
  seen = 0;
  qa_compat_q2_native_primary_commands_with_weapon(commands, actor_at(1), NULL, &seen, with_weapon_run);
  CHECK(seen == ITEM_AT(1));
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_with_weapon(commands, actor_at(1), "q2:ammo_shells", &seen, with_weapon_run));
  /* give: categories observed, unknown item skipped when the hook claims it. */
  CMD.give_item_result = true;
  qa_guest_call_value self = ptr_value(entity_at(1));
  put_i32(GUEST, client_at(1) + 0x40 + 3 * 4, 3);
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(CMD.give_count == 2 && strcmp(CMD.give_categories[0], "weapons") == 0 && strcmp(CMD.give_categories[1], "ammo") == 0);
  CHECK(!unknown_body_ran);
  CHECK(CMD.ammo_calls == 1 && strcmp(CMD.ammo_item, "q2:ammo_shells") == 0 && strcmp(CMD.ammo_kind, "set") == 0 && CMD.ammo_amount == 77);
  /* Refused hook leaves the original command in place. */
  CMD.give_item_result = false;
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(unknown_body_ran);
  /* Outside a give the observers do nothing. */
  CMD.give_count = 0;
  g_call(GUEST, addr(IMAGE + 0x110), NULL, 0, 0);
  CHECK(CMD.give_count == 0);
  /* Death drop projects the selected weapon and ammo, then restores them. */
  put_ptr(GUEST, client_at(1) + 0x10, addr(ITEM_AT(1)));
  put_i32(GUEST, client_at(1) + 0x18, 9);
  put_i32(GUEST, client_at(1) + 0x40 + 3 * 4, 4);
  CMD.has_drop = true; CMD.drop_item = "q2:weapon_shotgun"; CMD.drop_ammo = 25;
  g_call(GUEST, addr(IMAGE + 0x200), &self, 1, 0);
  CHECK(seen_weapon == ITEM_AT(2)); CHECK(seen_ammo_index == 3); CHECK(seen_shells == 25);
  CHECK(get_ptr(GUEST, client_at(1) + 0x10) == ITEM_AT(1)); CHECK(get_i32(GUEST, client_at(1) + 0x18) == 9); CHECK(get_i32(GUEST, client_at(1) + 0x40 + 3 * 4) == 4);
  /* Null item clears the pointer. */
  CMD.drop_item = NULL; CMD.drop_ammo = 0;
  g_call(GUEST, addr(IMAGE + 0x200), &self, 1, 0);
  CHECK(seen_weapon == 0); CHECK(seen_ammo_index == 0);
  CMD.drop_item = "q2:item_quad";
  CHECK_ABORTS(g_call(GUEST, addr(IMAGE + 0x200), &self, 1, 0));
  CMD.drop_item = "q2:weapon_shotgun"; CMD.drop_ammo = 1.5;
  CHECK_ABORTS(g_call(GUEST, addr(IMAGE + 0x200), &self, 1, 0));
  /* No projection leaves the original fields. */
  CMD.has_drop = false;
  g_call(GUEST, addr(IMAGE + 0x200), &self, 1, 0);
  CHECK(seen_weapon == ITEM_AT(1));
  qa_compat_q2_native_primary_commands_close(commands);
  qa_compat_q2_native_primary_commands_close(commands);
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_with_weapon(commands, actor_at(1), NULL, &seen, with_weapon_run));
  qa_compat_q2_native_primary_commands_destroy(commands);
  /* "add" ammo grants report the int32 difference. */
  profile = command_profile(&grants[1], 1);
  commands = qa_compat_q2_native_primary_commands_create(host, &profile, command_hooks());
  ammo_add_mode = true; CMD.ammo_calls = 0;
  put_i32(GUEST, client_at(1) + 0x40 + 3 * 4, 3); /* the unbound "set" region's body runs first: 8 */
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  ammo_add_mode = false;
  CHECK(CMD.ammo_calls == 1); CHECK(strcmp(CMD.ammo_kind, "add") == 0); CHECK(CMD.ammo_amount == 5);
  CHECK(get_i32(GUEST, client_at(1) + 0x40 + 3 * 4) == 13);
  qa_compat_q2_native_primary_commands_destroy(commands);
  /* Table validation. */
  qa_compat_q2_native_primary_command_profile bad = profile; bad.digest = "sha256:other";
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_create(host, &bad, command_hooks()));
  put_item(GUEST, 4, "weapon_blaster", "", 0, 0, "");
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_create(host, &profile, command_hooks()));
  put_item(GUEST, 4, "Bad Name", "", 1, 0, "");
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_create(host, &profile, command_hooks()));
  put_item(GUEST, 4, "item_quad", "", 0, 0, "Quad");
  /* Ammo declared by name, matched case-insensitively. */
  put_ptr(GUEST, ITEM_AT(2) + ITEM_AMMO_NAME, g_string(GUEST, "SHELLS"));
  qa_compat_q2_native_primary_command_profile named = command_profile(grants, 0);
  named.ammo_kind = "name"; named.has_client_ammo_index = false; named.ammo_offset = ITEM_AMMO_NAME;
  commands = qa_compat_q2_native_primary_commands_create(host, &named, command_hooks());
  weapons = qa_compat_q2_native_primary_commands_weapons(commands, &count);
  CHECK(count == 2); CHECK(weapons[0].ammo == NULL); CHECK(strcmp(weapons[1].ammo, "q2:ammo_shells") == 0);
  qa_compat_q2_native_primary_commands_destroy(commands);
  put_ptr(GUEST, ITEM_AT(2) + ITEM_AMMO_NAME, g_string(GUEST, "nothing"));
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_create(host, &named, command_hooks()));
  /* An ammo index with no item is refused. */
  put_i32(GUEST, ITEM_AT(2) + ITEM_AMMO, 99);
  qa_compat_q2_native_primary_command_profile indexed = command_profile(grants, 0);
  CHECK_ABORTS(qa_compat_q2_native_primary_commands_create(host, &indexed, command_hooks()));
  teardown();
}
/* --- native-primary-inventory --- */
typedef struct { qa_compat_q2_native_inventory_row rows[6]; size_t count; bool null_rows; qa_item_id used; int uses; char printed[256]; int prints; } inventory_log;
static inventory_log INV;
static bool inv_rows(void *ctx, qa_actor_id a, const qa_compat_q2_native_inventory_row **out, size_t *out_count) { (void)a; CHECK(ctx == &INV); if (INV.null_rows) return false; *out = INV.rows; *out_count = INV.count; return true; }
static void inv_use(void *ctx, qa_actor_id a, qa_item_id item) { (void)a; CHECK(ctx == &INV); INV.used = item; INV.uses++; }
static qa_guest_address inv_descriptor(void *ctx, double index) { CHECK(ctx == &INV); return addr(ITEM_AT((int64_t)index)); }
static qa_item_id inv_item_at(void *ctx, qa_guest_address a) { CHECK(ctx == &INV); return a.byte_offset == ITEM_AT(4) ? "q2:item_quad" : NULL; }
static void inv_print(void *ctx, qa_actor_id a, const char *text) { (void)a; CHECK(ctx == &INV); snprintf(INV.printed, sizeof INV.printed, "%s", text); INV.prints++; }
#define INV_CURSOR 0x18
#define INV_INVENTORY 0x40
#define INV_SELECTION 0x30
static int scan_body_ran, validate_seen; static int32_t validate_count, named_count; static bool use_body_ran;
static const char *lookup_name;
/* Original scanner: accepts the first stocked slot, except the passive slot 3. */
static void next_scan_body(guest_t *g, void *v) {
  (void)v;
  int client = 1;
  scan_body_ran++;
  put_i32(g, client_at(client) + INV_SELECTION, 0x7777);
  for (int index = 1; index < 5; index++) if (get_i32(g, client_at(client) + INV_INVENTORY + index * 4) != 0 && index != 3) { put_i32(g, client_at(client) + INV_CURSOR, index); return; }
  put_i32(g, client_at(client) + INV_CURSOR, -1);
}
static qa_guest_call_result g_inv_next(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; (void)c; g_region(g, IMAGE + 0x110, next_scan_body, NULL); return qa_compat_q2_call_void(); }
static qa_guest_call_result g_inv_prev(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; (void)c; g_region(g, IMAGE + 0x210, next_scan_body, NULL); return qa_compat_q2_call_void(); }
static qa_guest_call_result g_inv_validate(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; (void)c; validate_seen++; validate_count = get_i32(g, client_at(1) + INV_INVENTORY + 2 * 4); return qa_compat_q2_call_void(); }
static void use_body(guest_t *g, void *v) { (void)g; (void)v; use_body_ran = true; }
static qa_guest_call_result g_inv_use(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; (void)c; g_region(g, IMAGE + 0x410, use_body, NULL); return qa_compat_q2_call_void(); }
static void lookup_body(guest_t *g, void *v) { (void)v; g->cpu.rax = strcmp(lookup_name, "quad") == 0 ? ITEM_AT(4) : 0; }
static void named_use_body(guest_t *g, void *v) { (void)v; use_body_ran = true; named_count = get_i32(g, client_at(1) + INV_INVENTORY + 2 * 4); }
static qa_guest_call_result g_inv_named(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)a; (void)c;
  g->cpu.rcx = (uint64_t)g_string(g, lookup_name).byte_offset;
  g_region(g, IMAGE + 0x510, lookup_body, NULL);
  named_count = get_i32(g, client_at(1) + INV_INVENTORY + 2 * 4);
  g_region(g, IMAGE + 0x520, named_use_body, NULL);
  return qa_compat_q2_call_void();
}
static qa_compat_q2_native_inventory_row row_of(qa_item_id item, const char *label, double count, double index, bool selected) {
  qa_compat_q2_native_inventory_row r; memset(&r, 0, sizeof r); r.item = item; r.label = label; r.count = count; r.source_index = index; r.selected = selected; return r;
}
static void test_inventory(void) {
  qa_compat_q2_native_primary_weapon_host host = setup(8);
  static const qa_compat_q2_selection_write writes[1] = {{INV_SELECTION, 4}};
  qa_compat_q2_native_primary_inventory_profile p;
  memset(&p, 0, sizeof p);
  p.digest = DIGEST; p.abi = ABI64; p.client = CLIENT_FIELD; p.inventory = INV_INVENTORY; p.count = 5; p.cursor = INV_CURSOR; p.empty = -1;
  p.selection_writes = writes; p.selection_write_count = 1;
  p.next_entry = 0x100; p.next_scan = 0x110; p.next_join = 0x118; p.next_menu_argument = true;
  p.prev_entry = 0x200; p.prev_scan = 0x210; p.prev_join = 0x218;
  p.validate_entry = 0x300; p.use_entry = 0x400; p.use_call = 0x410; p.use_join = 0x418;
  p.named_entry = 0x500; p.named_lookup_call = 0x510; p.named_lookup_return = 0x518; p.named_call = 0x520; p.named_join = 0x528;
  g_fn(GUEST, 0x100, g_inv_next); g_fn(GUEST, 0x200, g_inv_prev); g_fn(GUEST, 0x300, g_inv_validate); g_fn(GUEST, 0x400, g_inv_use); g_fn(GUEST, 0x500, g_inv_named);
  memset(&INV, 0, sizeof INV);
  qa_compat_q2_native_primary_inventory_hooks hooks = {&INV, inv_rows, inv_use, inv_descriptor, inv_item_at, inv_print};
  qa_compat_q2_native_primary_inventory *inventory = qa_compat_q2_native_primary_inventory_create(host, &p, hooks);
  /* Two canonical selected items share slot 2; a passive one (slot 3) the scanner refuses; slot 4 empty. */
  INV.rows[0] = row_of("q2:a", "Alpha", 5, 2, true);
  INV.rows[1] = row_of("q2:b", "Beta", 7, 2, true);
  INV.rows[2] = row_of("q2:c", "Gamma", 1, 3, false);
  INV.rows[3] = row_of("q2:d", "Delta", 0, 4, false);
  INV.count = 4;
  put_i32(GUEST, client_at(1) + INV_CURSOR, 0);
  put_i32(GUEST, client_at(1) + INV_INVENTORY + 2 * 4, 123); /* source count is preserved by evaluate */
  put_i32(GUEST, client_at(1) + INV_SELECTION, 0x1111);
  qa_guest_call_value next_args[3] = {ptr_value(entity_at(1)), qa_compat_q2_call_int32(-1), qa_compat_q2_call_uint32(0)};
  g_call(GUEST, addr(IMAGE + 0x100), next_args, 3, 0);
  qa_compat_q2_native_inventory_readout readout;
  CHECK(qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout));
  CHECK(readout.selected != NULL && strcmp(readout.selected, "q2:a") == 0);
  CHECK(readout.item_count == 3); /* empty rows are not listed */
  CHECK(strcmp(readout.items[1].label, "Beta") == 0 && readout.items[1].count == 7);
  CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == 2);
  CHECK(get_i32(GUEST, client_at(1) + INV_INVENTORY + 2 * 4) == 123);
  /* Accepted evaluation keeps its selection side effects. */
  CHECK(get_i32(GUEST, client_at(1) + INV_SELECTION) == 0x7777);
  /* The same slot resolves to the next canonical row. */
  g_call(GUEST, addr(IMAGE + 0x100), next_args, 3, 0);
  qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout);
  CHECK(strcmp(readout.selected, "q2:b") == 0);
  /* Forward from b skips the refused c and the empty d, wrapping to a. */
  int before = scan_body_ran;
  g_call(GUEST, addr(IMAGE + 0x100), next_args, 3, 0);
  qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout);
  CHECK(strcmp(readout.selected, "q2:a") == 0);
  CHECK(scan_body_ran == before + 2); /* c refused, then a accepted */
  /* Previous direction. */
  qa_guest_call_value prev_args[2] = {ptr_value(entity_at(1)), qa_compat_q2_call_int32(-1)};
  g_call(GUEST, addr(IMAGE + 0x200), prev_args, 2, 0);
  qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout);
  CHECK(strcmp(readout.selected, "q2:b") == 0);
  /* Every row refused: the empty cursor and restored selection bytes. */
  size_t saved_count = INV.count;
  qa_compat_q2_native_inventory_row saved_row = INV.rows[0];
  INV.rows[0] = row_of("q2:c", "Gamma", 1, 3, false); INV.count = 1;
  put_i32(GUEST, client_at(1) + INV_SELECTION, 0x3333);
  g_call(GUEST, addr(IMAGE + 0x100), next_args, 3, 0);
  CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == -1);
  CHECK(get_i32(GUEST, client_at(1) + INV_SELECTION) == 0x3333);
  INV.rows[0] = saved_row; INV.count = saved_count;
  /* No rows: the original scanner runs. */
  INV.null_rows = true;
  before = scan_body_ran;
  put_i32(GUEST, client_at(1) + INV_INVENTORY + 1 * 4, 1);
  g_call(GUEST, addr(IMAGE + 0x100), next_args, 3, 0);
  CHECK(scan_body_ran == before + 1);
  CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == 1);
  put_i32(GUEST, client_at(1) + INV_INVENTORY + 1 * 4, 0);
  INV.null_rows = false;
  /* restore / validate projection. */
  qa_compat_q2_native_primary_inventory_restore(inventory, actor_at(1), "q2:b");
  CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == 2);
  qa_guest_call_value self = ptr_value(entity_at(1));
  put_i32(GUEST, client_at(1) + INV_INVENTORY + 2 * 4, 0);
  g_call(GUEST, addr(IMAGE + 0x300), &self, 1, 0);
  CHECK(validate_seen == 1); CHECK(validate_count == 7);
  CHECK(get_i32(GUEST, client_at(1) + INV_INVENTORY + 2 * 4) == 0);
  CHECK_ABORTS(qa_compat_q2_native_primary_inventory_restore(inventory, actor_at(1), "q2:missing"));
  /* use: the selected row goes to the hook and skips the original. */
  g_call(GUEST, addr(IMAGE + 0x400), &self, 1, 0);
  CHECK(INV.uses == 1); CHECK(strcmp(INV.used, "q2:b") == 0); CHECK(!use_body_ran);
  /* A non-selected source row keeps the original use. */
  qa_compat_q2_native_primary_inventory_restore(inventory, actor_at(1), NULL);
  CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == -1);
  put_i32(GUEST, client_at(1) + INV_CURSOR, 3);
  g_call(GUEST, addr(IMAGE + 0x400), &self, 1, 0);
  CHECK(use_body_ran); CHECK(INV.uses == 1);
  /* Named use: the lookup finds nothing; the canonical label wins. */
  use_body_ran = false;
  lookup_name = "ALPHA";
  put_i32(GUEST, client_at(1) + INV_INVENTORY + 2 * 4, 1);
  g_call(GUEST, addr(IMAGE + 0x500), &self, 1, 0);
  CHECK(INV.uses == 2 && strcmp(INV.used, "q2:a") == 0); CHECK(!use_body_ran);
  CHECK(named_count == 5);
  CHECK(get_i32(GUEST, client_at(1) + INV_INVENTORY + 2 * 4) == 1);
  CHECK(GUEST->cpu.rax == (uint64_t)ITEM_AT(2));
  /* Ambiguity without an original match prints and the original proceeds. */
  INV.rows[0] = row_of("q2:a", "Same Name", 5, 2, true);
  INV.rows[1] = row_of("q2:b", "Same Name", 7, 2, true);
  lookup_name = "Same Name";
  g_call(GUEST, addr(IMAGE + 0x500), &self, 1, 0);
  CHECK(INV.prints == 1);
  CHECK(strcmp(INV.printed, "Ambiguous item \"same name\"; use q2:a, q2:b\n") == 0);
  CHECK(use_body_ran);
  /* Null rows tracking: read() clears the cursor it set. */
  qa_compat_q2_native_primary_inventory_restore(inventory, actor_at(1), "q2:b");
  INV.null_rows = true;
  CHECK(!qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout));
  CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == -1);
  INV.null_rows = false;
  /* A selected row whose item disappeared clears the cursor. */
  qa_compat_q2_native_primary_inventory_restore(inventory, actor_at(1), "q2:b");
  INV.count = 1;
  qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout);
  CHECK(readout.selected == NULL); CHECK(get_i32(GUEST, client_at(1) + INV_CURSOR) == -1);
  WORLD.alive[1] = false;
  CHECK(!qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout));
  WORLD.alive[1] = true;
  qa_compat_q2_native_primary_inventory_close(inventory);
  CHECK(!qa_compat_q2_native_primary_inventory_read(inventory, actor_at(1), &readout));
  qa_compat_q2_native_primary_inventory_destroy(inventory);
  qa_compat_q2_native_primary_inventory_profile bad = p; bad.digest = "sha256:other";
  CHECK_ABORTS(qa_compat_q2_native_primary_inventory_create(host, &bad, hooks));
  teardown();
}
/* --- native-primary-drop --- */
#define DROP_WEAPON 0x10
#define DROP_CURSOR 0x18
#define DROP_PENDING 0x20
#define DROP_INVENTORY 0x40
typedef struct { qa_compat_q2_native_inventory_row rows[3]; size_t count; qa_item_id selected; qa_item_id dropped_item; double dropped_count; qa_actor_id dropped_pickup; int dropped_calls; bool dropped_result;
  int consumes; bool consume_runs; int actions; bool has_action; char printed[128]; } drop_log;
static drop_log DROP;
static bool drop_rows(void *ctx, qa_actor_id a, const qa_compat_q2_native_inventory_row **out, size_t *n) { (void)a; CHECK(ctx == &DROP); *out = DROP.rows; *n = DROP.count; return true; }
static qa_item_id drop_selected(void *ctx, qa_actor_id a) { (void)a; CHECK(ctx == &DROP); return DROP.selected; }
static qa_compat_q2_native_drop_projection drop_projection(void *ctx, qa_actor_id a, qa_item_id item) { (void)a; (void)item; CHECK(ctx == &DROP); qa_compat_q2_native_drop_projection p = {"q2:ammo_shells", "q2:weapon_blaster", NULL}; return p; }
static bool drop_dropped(void *ctx, qa_actor_id a, qa_actor_id pickup, qa_item_id item, double count) { (void)a; CHECK(ctx == &DROP); DROP.dropped_item = item; DROP.dropped_count = count; DROP.dropped_pickup = pickup; DROP.dropped_calls++; return DROP.dropped_result; }
static void drop_consume(void *ctx, qa_actor_id a, void *execute_ctx, void (*execute)(void *)) { (void)a; CHECK(ctx == &DROP); DROP.consumes++; if (DROP.consume_runs) execute(execute_ctx); }
static void drop_action_run(void *ctx) { CHECK(ctx == &DROP); DROP.actions++; }
static qa_compat_q2_native_drop_action drop_action(void *ctx, qa_actor_id a, qa_item_id item) { (void)a; (void)item; CHECK(ctx == &DROP); qa_compat_q2_native_drop_action act = {&DROP, DROP.has_action ? drop_action_run : NULL}; return act; }
static void drop_print(void *ctx, qa_actor_id a, const char *text) { (void)a; CHECK(ctx == &DROP); snprintf(DROP.printed, sizeof DROP.printed, "%s", text); }
static int32_t seen_counter; static int64_t seen_drop_weapon, seen_drop_pending; static int32_t seen_drop_cursor; static int freed_slot; static bool callback_ran, consumer_ran;
static const char *drop_lookup;
static void callback_body(guest_t *g, void *v) { (void)g; (void)v; callback_ran = true; }
static void debit_body(guest_t *g, void *v) { (void)v; put_i32(g, client_at(1) + DROP_INVENTORY + 3 * 4, get_i32(g, client_at(1) + DROP_INVENTORY + 3 * 4) - 2); }
static void consumer_body(guest_t *g, void *v) { (void)g; (void)v; consumer_ran = true; }
static qa_guest_call_result g_drop_find(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)c;
  char *text = (char *)(g->bytes + a[0].pointer_value.value.byte_offset);
  if (strcmp(text, "shells") == 0) return ptr_value(ITEM_AT(3));
  qa_guest_address none = {0};
  return qa_compat_q2_call_pointer(false, none);
}
static qa_guest_call_result g_drop_alloc(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)g; (void)a; (void)c; return ptr_value(entity_at(5)); }
static qa_guest_call_result g_drop_free(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)g; (void)c; freed_slot = (int)((a[0].pointer_value.value.byte_offset - ENTITIES) / STRIDE); return qa_compat_q2_call_void(); }
static void after_project_common(guest_t *g) {
  seen_counter = get_i32(g, client_at(1) + DROP_INVENTORY + 3 * 4);
  seen_drop_weapon = get_ptr(g, client_at(1) + DROP_WEAPON); seen_drop_pending = get_ptr(g, client_at(1) + DROP_PENDING);
  seen_drop_cursor = get_i32(g, client_at(1) + DROP_CURSOR);
}
static qa_guest_call_result g_drop_body_common(guest_t *g) {
  after_project_common(g);
  qa_guest_call_value allocation[2] = {ptr_value(entity_at(1)), ptr_value(ITEM_AT(3))};
  g_call(g, addr(IMAGE + 0x400), allocation, 2, 0);
  g_region(g, IMAGE + 0x510, callback_body, NULL);
  g_region(g, IMAGE + 0x610, debit_body, NULL);
  g_region(g, IMAGE + 0x710, consumer_body, NULL);
  return qa_compat_q2_call_void();
}
static qa_guest_call_result g_drop_named(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)a; (void)c;
  qa_guest_call_value name = ptr_value(g_string(g, drop_lookup).byte_offset);
  qa_guest_call_result found = g_call(g, addr(IMAGE + 0x300), &name, 1, IMAGE + 0x308);
  if (!found.pointer_value.has_value) return qa_compat_q2_call_void();
  return g_drop_body_common(g);
}
static qa_guest_call_result g_drop_inventory(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)a; (void)c;
  g_call(g, addr(IMAGE + 0x210), NULL, 0, 0);
  return g_drop_body_common(g);
}
static void test_drop(void) {
  qa_compat_q2_native_primary_weapon_host host = setup(8);
  standard_items(GUEST);
  static const qa_compat_q2_ammo_grant no_grants[1] = {{0, 0, "rdx", "set"}};
  qa_compat_q2_native_primary_command_profile command = command_profile(no_grants, 0);
  command.give_entry = 0x900; command.give_weapons = 0x910; command.give_ammo = 0x918; command.give_unknown_entry = 0x920; command.give_unknown_join = 0x928; command.drop_entry = 0x930; command.drop_elig_entry = 0x938; command.drop_elig_join = 0x940;
  memset(&CMD, 0, sizeof CMD);
  qa_compat_q2_native_primary_commands *commands = qa_compat_q2_native_primary_commands_create(host, &command, command_hooks());
  static const qa_compat_q2_native_region_span_fwd callbacks[1] = {{0x510, 0x518}}, debits[1] = {{0x610, 0x618}};
  qa_compat_q2_native_primary_drop_profile p;
  memset(&p, 0, sizeof p);
  p.digest = DIGEST; p.abi = ABI64; p.client_pointer = CLIENT_FIELD; p.client_inventory = DROP_INVENTORY; p.client_cursor = DROP_CURSOR; p.client_weapon = DROP_WEAPON; p.client_pending = DROP_PENDING;
  p.named = 0x100; p.inv_entry = 0x200; p.inv_admitted = 0x210; p.find = 0x300; p.lookup_return = 0x308; p.allocate = 0x400; p.free_entry = 0x800;
  p.has_consumer = true; p.consumer_entry = 0x710; p.consumer_join = 0x718;
  p.callbacks = callbacks; p.callback_count = 1; p.debits = debits; p.debit_count = 1;
  g_fn(GUEST, 0x100, g_drop_named); g_fn(GUEST, 0x200, g_drop_inventory); g_fn(GUEST, 0x210, g_void); g_fn(GUEST, 0x300, g_drop_find); g_fn(GUEST, 0x400, g_drop_alloc); g_fn(GUEST, 0x800, g_drop_free);
  memset(&DROP, 0, sizeof DROP);
  qa_compat_q2_native_primary_drop_hooks hooks = {&DROP, drop_rows, drop_selected, drop_projection, drop_dropped, drop_consume, drop_action, drop_print};
  qa_compat_q2_native_primary_drop *drop = qa_compat_q2_native_primary_drop_create(host, &p, commands, hooks);
  DROP.rows[0] = row_of("q2:ammo_shells", "Shells", 9, 3, true);
  DROP.rows[1] = row_of("q2:other", "Other", 3, 2, false);
  DROP.count = 2; DROP.dropped_result = true; DROP.consume_runs = true;
  int64_t weapon_before = ITEM_AT(2);
  put_ptr(GUEST, client_at(1) + DROP_WEAPON, addr(weapon_before));
  put_ptr(GUEST, client_at(1) + DROP_PENDING, addr(ITEM_AT(2)));
  put_i32(GUEST, client_at(1) + DROP_INVENTORY + 3 * 4, 1);
  qa_guest_call_value self = ptr_value(entity_at(1));
  /* Named drop: projection counter, source weapon pointer; debit reports count 2. */
  drop_lookup = "shells";
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(seen_counter == 9); CHECK(seen_drop_weapon == ITEM_AT(1)); CHECK(seen_drop_pending == 0);
  CHECK(callback_ran);
  CHECK(DROP.dropped_calls == 1 && DROP.dropped_count == 2 && strcmp(DROP.dropped_item, "q2:ammo_shells") == 0 && DROP.dropped_pickup.slot == 5);
  CHECK(DROP.consumes == 1); CHECK(consumer_ran);
  /* Restore happened at the debit. */
  CHECK(get_i32(GUEST, client_at(1) + DROP_INVENTORY + 3 * 4) == 1);
  CHECK(get_ptr(GUEST, client_at(1) + DROP_WEAPON) == weapon_before);
  CHECK(get_ptr(GUEST, client_at(1) + DROP_PENDING) == ITEM_AT(2));
  /* Consume hook withholding execute makes the original consumer skip. */
  consumer_ran = false; DROP.consume_runs = false;
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(DROP.consumes == 2); CHECK(!consumer_ran);
  /* Refused debit frees the pickup and skips the consumer. */
  DROP.dropped_result = false; DROP.consume_runs = true; consumer_ran = false;
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(freed_slot == 5); CHECK(DROP.consumes == 2); CHECK(!consumer_ran);
  /* An item action replaces the callback, restores, and leaves no debit. */
  DROP.dropped_result = true; DROP.has_action = true; callback_ran = false; int dropped_calls = DROP.dropped_calls;
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(DROP.actions == 1); CHECK(!callback_ran); CHECK(DROP.dropped_calls == dropped_calls);
  DROP.has_action = false;
  put_i32(GUEST, client_at(1) + DROP_INVENTORY + 3 * 4, 1);
  /* Inventory drop: admitted observer projects with the cursor. */
  DROP.selected = "q2:ammo_shells";
  put_i32(GUEST, client_at(1) + DROP_CURSOR, 77);
  g_call(GUEST, addr(IMAGE + 0x200), &self, 1, 0);
  CHECK(seen_counter == 9); CHECK(seen_drop_cursor == 3);
  CHECK(get_i32(GUEST, client_at(1) + DROP_CURSOR) == 77);
  /* Ambiguous names print only when the original found nothing. */
  DROP.rows[0] = row_of("q2:x", "Same", 1, 1, true); DROP.rows[1] = row_of("q2:y", "Same", 1, 2, true);
  drop_lookup = "same";
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(strcmp(DROP.printed, "Ambiguous item \"same\"; use q2:x, q2:y\n") == 0);
  /* A debit with no live allocation is fatal. */
  DROP.rows[0] = row_of("q2:ammo_shells", "Shells", 9, 3, true);
  drop_lookup = "shells";
  WORLD.alive[5] = false;
  CHECK_ABORTS(g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0));
  WORLD.alive[5] = true;
  qa_compat_q2_native_primary_drop_destroy(drop);
  qa_compat_q2_native_primary_commands_destroy(commands);
  qa_compat_q2_native_primary_drop_profile bad = p; bad.digest = "sha256:other";
  CHECK_ABORTS(qa_compat_q2_native_primary_drop_create(host, &bad, NULL, hooks));
  teardown();
}
/* --- native-primary-weapons --- */
typedef struct { bool selected; int completed; bool reached; int spawned; } weapon_log;
static weapon_log WEAPON;
static bool w_selected(void *ctx, qa_actor_id a) { (void)a; CHECK(ctx == &WEAPON); return WEAPON.selected; }
static void w_completed(void *ctx, qa_actor_id a, bool reached) { (void)a; CHECK(ctx == &WEAPON); WEAPON.completed++; WEAPON.reached = reached; }
static void w_spawned(void *ctx, qa_actor_id a) { (void)a; CHECK(ctx == &WEAPON); WEAPON.spawned++; }
static int32_t latched_seen; static int animation_calls, skip_body_runs, delay_invokes; static size_t attack_args; static int64_t delay_value; static double delay_flag_at_call;
static void delay_flag_body(guest_t *g, void *v) { (void)v; put_i32(g, IMAGE + 0xa00, 2); put_i32(g, client_at(1) + 0x130, 1); }
static void decision_body(guest_t *g, void *v) { (void)v; latched_seen = get_i32(g, client_at(1) + 0x120); }
static qa_guest_call_result g_dispatcher(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; (void)c; g_region(g, IMAGE + 0x110, decision_body, NULL); g_region(g, IMAGE + 0x210, delay_flag_body, NULL); g_region(g, IMAGE + 0x310, delay_flag_body, NULL); return qa_compat_q2_call_void(); }
static qa_guest_call_result g_w_spawn(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; (void)c; put_i32(g, entity_at(1) + 0xb0, 1); return qa_compat_q2_call_void(); }
static void skip_body(guest_t *g, void *v) { (void)g; (void)v; skip_body_runs++; }
static qa_guest_call_result g_attack(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)a; attack_args = c; animation_calls++; g_region(g, IMAGE + 0x510, skip_body, NULL); return qa_compat_q2_call_void(); }
static qa_guest_call_result g_damage(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)g; (void)a; (void)c; return qa_compat_q2_call_int32(4); }
static qa_guest_call_result g_anim_delay(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)c; delay_invokes++;
  delay_flag_at_call = get_i32(g, client_at(1) + 0x130);
  qa_compat_q2_mem_write_i64(&g->memory, a[0].pointer_value.value, delay_value);
  put_i32(g, client_at(1) + 0x134, 99); /* a declared source write the service restores */
  return ptr_value(a[0].pointer_value.value.byte_offset);
}
static void test_weapons(void) {
  qa_compat_q2_native_primary_weapon_host host = setup(8);
  static const qa_native_weapon_decision_field decision_fields[1] = {{{{0x120, "int32"}, "client"}, 3}};
  static const qa_native_weapon_decision decisions[1] = {{0x110, 0x118, decision_fields, 1}};
  static qa_native_item_test active_tests[2], committed_tests[1], continuing_tests[1], accepted_tests[1];
  memset(active_tests, 0, sizeof active_tests); memset(committed_tests, 0, sizeof committed_tests); memset(continuing_tests, 0, sizeof continuing_tests); memset(accepted_tests, 0, sizeof accepted_tests);
  active_tests[0].kind = "scalar"; active_tests[0].field = FIELD("entity", 0xa0, "int32"); active_tests[0].comparison = "at-most"; active_tests[0].scalar_value = 10;
  static const qa_native_mod_address gun = {0x700, {NULL, 0}};
  active_tests[1].kind = "pointer"; active_tests[1].pointer_field = (qa_native_item_pointer){"client", 0x140}; active_tests[1].pointer_value = &gun;
  committed_tests[0].kind = "scalar"; committed_tests[0].field = FIELD("client", 0x144, "int32"); committed_tests[0].has_mask = true; committed_tests[0].mask = 0xff; committed_tests[0].comparison = "equals"; committed_tests[0].scalar_value = 7;
  continuing_tests[0].kind = "scalar"; continuing_tests[0].field = FIELD("entity", 0xa4, "uint8"); continuing_tests[0].comparison = "equals"; continuing_tests[0].scalar_value = 200;
  accepted_tests[0].kind = "scalar"; accepted_tests[0].field = FIELD("entity", 0xb0, "int32"); accepted_tests[0].comparison = "equals"; accepted_tests[0].scalar_value = 1;
  static const qa_native_item_test_row committed[1] = {{committed_tests, 1}}, continuations[1] = {{continuing_tests, 1}};
  static const double factors[3] = {1, 2, 4};
  static const qa_compat_q2_native_region_span_fwd skips[1] = {{0x510, 0x518}};
  qa_compat_q2_native_primary_weapon_profile p; init_weapon_profile(&p, ABI64);
  p.dispatcher.entry.kind = "rva"; p.dispatcher.entry.rva = 0x100; p.dispatcher.record = "entity"; p.dispatcher.argument = 0; p.dispatcher.arguments = 1;
  p.decisions = decisions; p.decision_count = 1;
  p.spawn_entry = 0x400; p.spawn_accepted = accepted_tests; p.spawn_accepted_count = 1;
  p.active = active_tests; p.active_count = 2; p.committed_input = committed; p.committed_input_count = 1; p.continuations = continuations; p.continuation_count = 1;
  p.time_address = 0xa10; p.time_encoding = "float32"; p.time_milliseconds = 1000;
  p.entity_water_level = FIELD("entity", 0xc0, "int16"); p.entity_view_height = FIELD("entity", 0xc4, "float32"); p.entity_max_health = FIELD("entity", MAX_HEALTH, "int32");
  p.client_buttons = FIELD("client", 0x124, "uint8"); p.client_latched_buttons = FIELD("client", 0x120, "int32");
  p.attack_animation_entry = 0x500; p.attack_skip = skips; p.attack_skip_count = 1;
  p.anim_frame = FIELD("client", 0x150, "int32"); p.anim_end = FIELD("client", 0x154, "int32"); p.anim_priority = FIELD("client", 0x158, "int32"); p.anim_duck = FIELD("client", 0x15c, "uint8"); p.anim_run = FIELD("client", 0x15d, "uint8");
  p.delay_flag = FIELD("client", 0x130, "int32"); p.delay_entry = 0x210; p.delay_join = 0x218; p.delay_evaluate_kind = "source-flag"; p.delay_factors = factors; p.delay_factor_count = 3;
  p.damage_kind = "source-flag"; p.damage_address = 0xa00; p.damage_encoding = "int32"; p.damage_factors = factors; p.damage_factor_count = 3; p.damage_region_entry = 0x310; p.damage_region_join = 0x318;
  g_fn(GUEST, 0x100, g_dispatcher); g_fn(GUEST, 0x400, g_w_spawn); g_fn(GUEST, 0x500, g_attack);
  memset(&WEAPON, 0, sizeof WEAPON);
  qa_compat_q2_native_primary_weapon_hooks hooks = {&WEAPON, w_selected, w_completed, w_spawned};
  qa_compat_q2_native_primary_weapons *weapons = qa_compat_q2_native_primary_weapons_create(host, &p, hooks);
  qa_actor_id one = actor_at(1);
  /* Field encodings. */
  uint8_t *entity = GUEST->bytes + entity_at(1);
  entity[0xa4] = 200;
  CHECK(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("entity", 0xa4, "uint8")) == 200);
  CHECK(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("entity", 0xa4, "int8")) == -56);
  qa_compat_q2_view_set_i16(entity, 0xc0, -3);
  CHECK(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("entity", 0xc0, "int16")) == -3);
  CHECK(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("entity", 0xc0, "uint16")) == 65533);
  CHECK(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("image", 0xa00, "int32")) == 0);
  qa_compat_q2_mem_write_i64(&GUEST->memory, addr(entity_at(1) + 0xd0), 1234567);
  CHECK(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("entity", 0xd0, "int64")) == 1234567);
  qa_compat_q2_mem_write_i64(&GUEST->memory, addr(entity_at(1) + 0xd0), (int64_t)1 << 60);
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("entity", 0xd0, "int64")));
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_read(weapons, one, FIELD("nope", 0, "int32")));
  /* input/animation. */
  uint8_t *client = GUEST->bytes + client_at(1);
  client[0x124] = 5; qa_compat_q2_view_set_i32(client, 0x120, 9);
  qa_compat_q2_view_set_f32(client, 0x100, 12); qa_compat_q2_view_set_f32(entity, 0xc4, 22);
  qa_compat_q2_view_set_i32(entity, MAX_HEALTH, 150);
  qa_compat_q2_native_primary_weapon_input input = qa_compat_q2_native_primary_weapons_input(weapons, one);
  CHECK(input.buttons == 5 && input.latched_buttons == 9 && input.view_angles.x == 12 && input.view_height == 22 && input.water_level == -3 && input.max_health == 150);
  qa_compat_q2_view_set_i32(client, 0x150, 40); qa_compat_q2_view_set_i32(client, 0x154, 45); qa_compat_q2_view_set_i32(client, 0x158, 2); client[0x15c] = 1; client[0x15d] = 0;
  qa_animation_state animation = qa_compat_q2_native_primary_weapons_animation(weapons, one);
  CHECK(strcmp(animation.kind, "q2") == 0 && animation.frame == 40 && animation.end_frame == 45 && animation.priority == 2 && animation.duck && !animation.run);
  /* available: scalar at-most and pointer tests. */
  qa_compat_q2_view_set_i32(entity, 0xa0, 10);
  CHECK(!qa_compat_q2_native_primary_weapons_available(weapons, one)); /* pointer differs */
  put_ptr(GUEST, client_at(1) + 0x140, addr(IMAGE + 0x700));
  CHECK(qa_compat_q2_native_primary_weapons_available(weapons, one));
  qa_compat_q2_view_set_i32(entity, 0xa0, 11);
  CHECK(!qa_compat_q2_native_primary_weapons_available(weapons, one));
  qa_compat_q2_view_set_i32(entity, 0xa0, 10);
  CHECK(qa_compat_q2_native_primary_weapons_continuing(weapons, one));
  WORLD.alive[1] = false;
  CHECK(!qa_compat_q2_native_primary_weapons_available(weapons, one));
  CHECK(!qa_compat_q2_native_primary_weapons_continuing(weapons, one));
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_input(weapons, one));
  WORLD.alive[1] = true;
  /* time */
  qa_compat_q2_mem_write_f32(&GUEST->memory, addr(IMAGE + 0xa10), 2.5);
  CHECK(qa_compat_q2_native_primary_weapons_time_milliseconds(weapons) == 2500);
  /* Damage and delay flags: fatal before the dispatcher ran for this actor. */
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_damage_factor(weapons, one));
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_weapon_delay(weapons, one, 100));
  /* Dispatcher: unselected masks clearMask bits during the decision. */
  qa_compat_q2_view_set_i32(client, 0x120, 7);
  qa_guest_call_value self = ptr_value(entity_at(1));
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(latched_seen == 4); CHECK(qa_compat_q2_view_i32(client, 0x120) == 7);
  CHECK(WEAPON.completed == 1 && WEAPON.reached);
  /* Delay region saw flag 1; damage region saw flag 2. */
  CHECK(qa_compat_q2_native_primary_weapons_weapon_delay(weapons, one, 100) == 200);
  CHECK(qa_compat_q2_native_primary_weapons_damage_factor(weapons, one) == 4);
  /* A selected actor reads the original bits. */
  WEAPON.selected = true;
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(latched_seen == 7);
  /* Committed input bypasses the mask even when unselected. */
  WEAPON.selected = false;
  qa_compat_q2_view_set_i32(client, 0x144, 0x107);
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  CHECK(latched_seen == 7);
  qa_compat_q2_view_set_i32(client, 0x144, 0);
  /* Spawn accepted tests read after the original. */
  qa_compat_q2_view_set_i32(entity, 0xb0, 0);
  g_call(GUEST, addr(IMAGE + 0x400), &self, 1, 0);
  CHECK(WEAPON.spawned == 1);
  /* Attack animation: skip regions bound only for the call; unavailable actors do nothing. */
  qa_compat_q2_native_primary_weapons_attack_animation(weapons, one);
  CHECK(animation_calls == 1); CHECK(attack_args == 8); CHECK(skip_body_runs == 0);
  g_call(GUEST, addr(IMAGE + 0x500), NULL, 0, 0); /* region removed: the body runs */
  CHECK(skip_body_runs == 1);
  qa_compat_q2_view_set_i32(entity, 0xa0, 11);
  qa_compat_q2_native_primary_weapons_attack_animation(weapons, one);
  CHECK(animation_calls == 2);
  qa_compat_q2_view_set_i32(entity, 0xa0, 10);
  qa_compat_q2_native_primary_weapons_release(weapons, one);
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_damage_factor(weapons, one));
  qa_compat_q2_native_primary_weapons_close(weapons);
  qa_compat_q2_native_primary_weapons_close(weapons);
  qa_compat_q2_native_primary_weapons_destroy(weapons);
  /* source-animation delay and source-result damage. */
  static const qa_compat_q2_weapon_projection projection[1] = {{{{0x138, "int32"}, "client"}, 30}};
  static const qa_native_item_field delay_writes[1] = {{{0x134, "int32"}, "client"}};
  p.delay_evaluate_kind = "source-animation"; p.delay_anim_entry = 0x600; p.delay_baseline_ms = 100;
  p.delay_projection = projection; p.delay_projection_count = 1; p.delay_writes = delay_writes; p.delay_write_count = 1;
  p.damage_kind = "source-result"; p.damage_result = "int32"; p.damage_entry = 0x650;
  g_fn(GUEST, 0x600, g_anim_delay); g_fn(GUEST, 0x650, g_damage);
  int allocations = GUEST->allocations, unmaps = GUEST->unmaps;
  weapons = qa_compat_q2_native_primary_weapons_create(host, &p, hooks);
  CHECK(GUEST->allocations == allocations + 1);
  CHECK(qa_compat_q2_native_primary_weapons_damage_factor(weapons, one) == 4);
  put_i32(GUEST, client_at(1) + 0x134, 5); put_i32(GUEST, client_at(1) + 0x138, 6);
  g_call(GUEST, addr(IMAGE + 0x100), &self, 1, 0);
  put_i32(GUEST, client_at(1) + 0x130, 0);
  delay_value = 150;
  CHECK(qa_compat_q2_native_primary_weapons_weapon_delay(weapons, one, 80) == 120);
  CHECK(delay_invokes == 1); CHECK(delay_flag_at_call == 1);
  CHECK(get_i32(GUEST, client_at(1) + 0x130) == 0); CHECK(get_i32(GUEST, client_at(1) + 0x134) == 5); CHECK(get_i32(GUEST, client_at(1) + 0x138) == 6);
  qa_compat_q2_native_primary_weapons_destroy(weapons);
  CHECK(GUEST->unmaps == unmaps + 1);
  qa_compat_q2_native_primary_weapon_profile bad = p; bad.digest = "sha256:other";
  CHECK_ABORTS(qa_compat_q2_native_primary_weapons_create(host, &bad, hooks));
  teardown();
}
/* --- native-pickups --- */
typedef struct {
  qa_source_pickup_selection_kind kind; qa_original_pickup_outcome outcome; bool selection_current;
  qa_original_pickup_offer offer; char item[64]; int runs, grants;
  qa_compat_q2_native_primary_pickups *pickups; qa_source_pickup_quantity quantity; int quantities; double quantity_count, quantity_capacity; qa_pickup_supply_offer supply_offer; char ammo_item[64], offer_weapon[64], offer_item[64];
  int protection_calls, consumer_calls; bool protection_committed; bool retire_during_grant;
} pickup_log;
static pickup_log PK;
static bool sel_current(void *ctx) { CHECK(ctx == &PK); return PK.selection_current; }
static qa_original_pickup_outcome sel_grant(void *ctx) {
  CHECK(ctx == &PK); PK.grants++;
  if (PK.pickups != NULL) {
    qa_original_pickup_offer offer = PK.offer; offer.item = PK.item;
    qa_compat_q2_native_pickup_supply_evaluation evaluation = qa_compat_q2_native_primary_pickups_evaluate_supply(PK.pickups, &offer);
    PK.supply_offer = evaluation.offer;
    if (evaluation.offer.kind == QA_PICKUP_SUPPLY_AMMO_WEAPON) snprintf(PK.offer_weapon, sizeof PK.offer_weapon, "%s", evaluation.offer.weapon);
    if (evaluation.offer.kind == QA_PICKUP_SUPPLY_WEAPON) snprintf(PK.offer_item, sizeof PK.offer_item, "%s", evaluation.offer.item);
    if (evaluation.offer.kind == QA_PICKUP_SUPPLY_WEAPON && evaluation.offer.ammo_count == 1) snprintf(PK.ammo_item, sizeof PK.ammo_item, "%s", evaluation.offer.ammo[0].item);
    if (evaluation.quantity != NULL) { PK.quantity = evaluation.quantity(evaluation.ctx, PK.quantity_count, PK.quantity_capacity); PK.quantities++; }
  }
  if (PK.retire_during_grant) WORLD.alive[2] = false;
  return PK.outcome;
}
static void *adm_run_source(void *ctx, qa_original_pickup_offer offer, void *(*execute)(void *, qa_source_pickup_selection, qa_source_pickup_lifetime), void *execute_ctx) {
  CHECK(ctx == &PK);
  PK.offer = offer; snprintf(PK.item, sizeof PK.item, "%s", offer.item); PK.runs++;
  qa_source_pickup_selection selection; memset(&selection, 0, sizeof selection);
  selection.kind = PK.kind; selection.ctx = &PK; selection.current = sel_current; selection.grant = sel_grant;
  qa_source_pickup_lifetime lifetime; memset(&lifetime, 0, sizeof lifetime);
  return execute(execute_ctx, selection, lifetime);
}
static const qa_original_pickup_admission ADMISSION = {&PK, adm_run_source, NULL};
static const qa_original_pickup_admission *p_admission(void *ctx) { CHECK_WORLD(ctx); return &ADMISSION; }
static bool p_current(void *ctx, qa_raw_entity_view record, qa_actor_id *out) { return h_actor(ctx, record, out); }
typedef struct { void *operation_ctx; bool (*committed)(void *); } protection_scope;
static void consume_publish(void *consume_ctx, void *execute_ctx, void (*execute)(void *)) { protection_scope *scope = consume_ctx; PK.protection_committed = scope->committed == NULL || scope->committed(scope->operation_ctx); execute(execute_ctx); PK.consumer_calls++; }
static void *p_with_protection(void *ctx, qa_raw_entity_view recipient, qa_protection_channel channel, void *operation_ctx, void (*current)(void *), void *(*operation)(void *, void *, qa_compat_q2_native_pickup_consume_fn), bool (*committed)(void *)) {
  CHECK_WORLD(ctx); CHECK(channel == QA_PROTECTION_POWERED); CHECK(recipient.slot == 2);
  PK.protection_calls++;
  current(operation_ctx);
  protection_scope scope = {operation_ctx, committed};
  return operation(operation_ctx, &scope, consume_publish);
}
#define GRANT_ENTRY 0x200
#define SUPPLY_AMMO_GRANT 0x300
#define SUPPLY_WEAPON_GRANT 0x400
#define AMMO_ENTRY 0x600
#define CONSUMER 0x650
#define ITEM_GRANT 32
#define ITEM_TAG 36
static int grant_body_runs, recipient_body_runs, targets_observed_runs; static int32_t grant_return_value; static int consumer_invokes; static bool touch_after_grant;
static bool retire_mid_touch;
static void recipient_body(guest_t *g, void *v) { (void)g; (void)v; recipient_body_runs++; }
static qa_guest_call_result g_grant(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)a; (void)c; grant_body_runs++;
  g_region(g, IMAGE + 0x210, recipient_body, NULL);
  return qa_compat_q2_call_int32(grant_return_value);
}
static qa_guest_call_result g_touch(guest_t *g, const qa_guest_call_value *a, size_t c) {
  /* Original Touch_Item: grant through its own return address, then targets. */
  (void)c;
  qa_guest_address descriptor, grant;
  qa_compat_q2_mem_read_pointer(&g->memory, addr(a[0].pointer_value.value.byte_offset + ITEMFIELD), &descriptor);
  qa_compat_q2_mem_read_pointer(&g->memory, addr(descriptor.byte_offset + ITEM_GRANT), &grant);
  qa_guest_call_result result = g_call(g, grant, a, 2, IMAGE + 0x108);
  if (retire_mid_touch) put_i32(g, a[1].pointer_value.value.byte_offset + INUSE, 0);
  g_call(g, addr(IMAGE + 0x110), a, 2, 0);
  targets_observed_runs++;
  touch_after_grant = result.kind == QA_GUEST_VALUE_INT32 && result.number_value != 0;
  return qa_compat_q2_call_void();
}
static qa_guest_call_result g_consumer(guest_t *g, const qa_guest_call_value *a, size_t c) { (void)g; CHECK(c == 1 && a[0].pointer_value.value.byte_offset == entity_at(2)); consumer_invokes++; return qa_compat_q2_call_void(); }
static int ammo_runs;
static qa_guest_call_result g_ammo(guest_t *g, const qa_guest_call_value *a, size_t c) {
  /* Original Add_Ammo: bump the counter, then pass the qualified stop. */
  (void)c; ammo_runs++;
  int64_t descriptor = a[1].pointer_value.value.byte_offset;
  int64_t index = (descriptor - ITEM_AT(0)) / ITEM_STRIDE;
  int64_t counter = client_at(2) + 0x40 + index * 4;
  put_i32(g, counter, get_i32(g, counter) + (int32_t)a[2].number_value);
  g->cpu.rax = 0xdead;
  g_call(g, addr(IMAGE + 0x660), NULL, 0, 0);
  return qa_compat_q2_call_int32(1);
}
static void ammo_region_body(guest_t *g, void *v) { (void)g; (void)v; recipient_body_runs++; }
static qa_guest_call_result g_supply_ammo_grant(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)a; (void)c; grant_body_runs++;
  g->cpu.rdx = 12;
  g_region(g, IMAGE + 0x310, ammo_region_body, NULL);
  return qa_compat_q2_call_int32(grant_return_value);
}
static int weapon_ammo_owned_seen;
static qa_guest_call_result g_supply_weapon_grant(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)c; grant_body_runs++;
  weapon_ammo_owned_seen = get_i32(g, client_at(2) + 0x40 + 2 * 4);
  g_region(g, IMAGE + 0x410, recipient_body, NULL);
  qa_guest_call_value ammo[3] = {a[1], ptr_value(ITEM_AT(3)), qa_compat_q2_call_int32(8)};
  qa_guest_call_result accepted = g_call(g, addr(IMAGE + AMMO_ENTRY), ammo, 3, IMAGE + 0x420);
  CHECK(accepted.kind == QA_GUEST_VALUE_INT32);
  g_call(g, addr(IMAGE + 0x430), NULL, 0, 0);
  g_region(g, IMAGE + 0x440, recipient_body, NULL);
  return qa_compat_q2_call_int32(grant_return_value);
}
static bool supply_owns_result;
static bool supply_owns(void *ctx, qa_actor_id a, qa_item_id item, qa_actor_id pickup) { CHECK(ctx == &PK); CHECK(a.slot == 2 && pickup.slot == 1); CHECK(strcmp(item, "q2:weapon_shotgun") == 0); return supply_owns_result; }
static void pickup_items(guest_t *g) {
  standard_items(g);
  put_ptr(g, ITEM_AT(4) + ITEM_GRANT, addr(IMAGE + GRANT_ENTRY));
  put_ptr(g, ITEM_AT(3) + ITEM_GRANT, addr(IMAGE + SUPPLY_AMMO_GRANT));
  put_ptr(g, ITEM_AT(2) + ITEM_GRANT, addr(IMAGE + SUPPLY_WEAPON_GRANT));
  put_i32(g, ITEM_AT(3) + ITEM_TAG + 8, 0); /* ammo tag */
}
static void set_touch(guest_t *g, int item) { put_ptr(g, entity_at(1) + ITEMFIELD, addr(ITEM_AT(item))); put_i32(g, entity_at(1) + COUNTFIELD, 0); put_i32(g, entity_at(1) + SPAWNFLAGS, 0); }
static qa_compat_q2_native_pickup_host pickup_host(bool protection) {
  qa_compat_q2_native_pickup_host host;
  memset(&host, 0, sizeof host);
  host.memory = &GUEST->memory; host.runner = &GUEST->runner; host.ctx = &WORLD; host.invoke = h_invoke; host.record = h_record; host.current = p_current; host.admission = p_admission;
  if (protection) host.with_protection = p_with_protection;
  return host;
}
static qa_guest_call_result touch(void) {
  qa_guest_call_value args[2] = {ptr_value(entity_at(1)), ptr_value(entity_at(2))};
  return g_call(GUEST, addr(IMAGE + 0x100), args, 2, 0);
}
static void test_pickups(void) {
  setup(8);
  pickup_items(GUEST);
  g_fn(GUEST, 0x100, g_touch); g_fn(GUEST, 0x110, g_void); g_fn(GUEST, GRANT_ENTRY, g_grant); g_fn(GUEST, SUPPLY_AMMO_GRANT, g_supply_ammo_grant); g_fn(GUEST, SUPPLY_WEAPON_GRANT, g_supply_weapon_grant);
  g_fn(GUEST, AMMO_ENTRY, g_ammo); g_fn(GUEST, CONSUMER, g_consumer); g_fn(GUEST, 0x660, g_void); g_fn(GUEST, 0x430, g_void);
  static const qa_guest_value_layout two[2] = {{.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "pointer"}};
  static const qa_guest_value_layout one_ptr[1] = {{.is_aggregate = false, .storage = "pointer"}};
  static const qa_guest_value_layout three[3] = {{.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "int32"}};
  static const qa_guest_value_layout int_result = {.is_aggregate = false, .storage = "int32"};
  qa_compat_q2_guest_call_signature touch_signature = {ABI64, two, 2, true, {0}, false};
  qa_compat_q2_guest_call_signature grant_signature = {ABI64, two, 2, false, int_result, false};
  qa_compat_q2_guest_call_signature ammo_signature = {ABI64, three, 3, false, int_result, false};
  qa_compat_q2_pickup_consumer consumers[1];
  consumers[0].entry = CONSUMER; consumers[0].signature = (qa_compat_q2_guest_call_signature){ABI64, one_ptr, 1, true, {0}, false}; consumers[0].protection = QA_PROTECTION_POWERED;
  qa_compat_q2_native_pickup_grant grants[3];
  memset(grants, 0, sizeof grants);
  grants[0].entry = GRANT_ENTRY; grants[0].recipient_entry = 0x210; grants[0].recipient_join = 0x218; grants[0].resource = "regular"; grants[0].consumers = consumers; grants[0].consumer_count = 1;
  grants[1].entry = SUPPLY_AMMO_GRANT; grants[1].recipient_join = 0x318; grants[1].resource = "inventory"; grants[1].has_supply = true; grants[1].supply_kind = "ammo"; grants[1].supply_entry = 0x310; grants[1].supply_amount = "rdx";
  grants[2].entry = SUPPLY_WEAPON_GRANT; grants[2].recipient_entry = 0x410; grants[2].recipient_join = 0x418; grants[2].resource = "inventory"; grants[2].has_supply = true; grants[2].supply_kind = "weapon"; grants[2].supply_ammo_return = 0x420; grants[2].supply_settle = 0x430; grants[2].supply_auto_entry = 0x440; grants[2].supply_auto_join = 0x448;
  static const double capacities[1] = {0x1a0};
  qa_compat_q2_native_pickup_profile profile;
  memset(&profile, 0, sizeof profile);
  profile.digest = DIGEST; profile.touch = 0x100; profile.grant_return = 0x108; profile.targets_return = 0x110; profile.touch_signature = touch_signature; profile.grant_signature = grant_signature;
  profile.grants = grants; profile.grant_count = 3; profile.items_table = TABLE; profile.items_stride = ITEM_STRIDE; profile.items_count = 5; profile.items_classname = ITEM_CLASSNAME; profile.items_pickup = ITEM_GRANT;
  profile.entity_item = ITEMFIELD; profile.entity_count = COUNTFIELD; profile.entity_spawnflags = SPAWNFLAGS; profile.entity_inuse = INUSE; profile.entity_inuse_bytes = 4; profile.has_entity_generation = true; profile.entity_generation = GENERATION;
  profile.time_address = 0xb00; profile.time_storage = "float32-seconds";
  profile.supply_client = CLIENT_FIELD; profile.supply_inventory = 0x40; profile.supply_flags = ITEM_FLAGS; profile.supply_weapon_flag = 1;
  profile.ammo_entry = AMMO_ENTRY; profile.ammo_signature = ammo_signature; profile.has_ammo_stop = true; profile.ammo_stop = 0x660; profile.ammo_capacities = capacities; profile.ammo_capacity_count = 1; profile.ammo_capacity_bytes = 2; profile.ammo_tag = ITEM_TAG + 8;
  qa_compat_q2_mem_write_f32(&GUEST->memory, addr(IMAGE + 0xb00), 3.5f);
  /* Construction contract. */
  qa_guest_address image = GUEST->image;
  qa_compat_q2_native_primary_pickups pickups;
  CHECK_ABORTS(qa_compat_q2_native_primary_pickups_init(&pickups, pickup_host(false), image, &profile));
  qa_compat_q2_native_pickup_profile bad = profile; bad.digest = "sha256:other";
  CHECK_ABORTS(qa_compat_q2_native_primary_pickups_init(&pickups, pickup_host(true), image, &bad));
  qa_compat_q2_native_primary_pickups_init(&pickups, pickup_host(true), image, &profile);
  memset(&PK, 0, sizeof PK);
  PK.selection_current = true; PK.outcome = QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED; PK.kind = QA_SOURCE_PICKUP_REPLACEMENT;
  grant_return_value = 30;
  /* Replacement: offer fields, resources before the map continuation, recipient region skipped. */
  set_touch(GUEST, 4);
  put_i32(GUEST, entity_at(1) + COUNTFIELD, 25);
  put_i32(GUEST, entity_at(1) + SPAWNFLAGS, 0x10000);
  double depth = GUEST->depth; uint64_t rsp = GUEST->cpu.rsp;
  touch();
  CHECK(PK.runs == 1); CHECK(strcmp(PK.item, "q2:item_quad") == 0);
  CHECK(PK.offer.recipient.slot == 2 && PK.offer.pickup.slot == 1);
  CHECK(PK.offer.count.is_override && PK.offer.count.amount == 25); CHECK(PK.offer.dropped);
  CHECK(PK.offer.time.kind == QA_SOURCE_TIME_SECONDS && PK.offer.time.value == 3.5);
  CHECK(PK.grants == 1); CHECK(grant_body_runs == 1); CHECK(recipient_body_runs == 0); CHECK(touch_after_grant);
  CHECK(PK.protection_calls == 1); CHECK(PK.consumer_calls == 1); CHECK(consumer_invokes == 1); CHECK(PK.protection_committed);
  CHECK(GUEST->depth == depth && GUEST->cpu.rsp == rsp);
  qa_compat_q2_native_primary_pickups_assert_idle(&pickups);
  /* Refused grant returns 0 without the map continuation or consumers. */
  PK.outcome = QA_ORIGINAL_PICKUP_OUTCOME_REFUSED;
  touch();
  CHECK(grant_body_runs == 1); CHECK(!touch_after_grant); CHECK(consumer_invokes == 1);
  /* Blocked returns 0 at the grant. */
  PK.kind = QA_SOURCE_PICKUP_BLOCKED; int grants_before = PK.grants;
  touch();
  CHECK(PK.grants == grants_before); CHECK(!touch_after_grant);
  /* Original executes everything. */
  PK.kind = QA_SOURCE_PICKUP_ORIGINAL; grant_body_runs = 0; recipient_body_runs = 0;
  touch();
  CHECK(grant_body_runs == 1); CHECK(recipient_body_runs == 1); CHECK(touch_after_grant);
  /* Stale selection returns void without running the caller. */
  PK.kind = QA_SOURCE_PICKUP_STALE; int targets = targets_observed_runs;
  touch();
  CHECK(targets_observed_runs == targets);
  /* A descriptor outside the table runs the original with a null frame. */
  PK.kind = QA_SOURCE_PICKUP_REPLACEMENT; PK.outcome = QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED;
  int runs = PK.runs; targets = targets_observed_runs;
  put_ptr(GUEST, entity_at(1) + ITEMFIELD, addr(IMAGE + GRANT_ENTRY + 4));
  put_ptr(GUEST, IMAGE + GRANT_ENTRY + 4 + ITEM_GRANT, addr(IMAGE + 0x110));
  touch();
  CHECK(PK.runs == runs); CHECK(targets_observed_runs == targets + 1);
  set_touch(GUEST, 4);
  /* A dead recipient is stale before admission. */
  put_i32(GUEST, entity_at(2) + INUSE, 0);
  runs = PK.runs;
  touch();
  CHECK(PK.runs == runs);
  put_i32(GUEST, entity_at(2) + INUSE, 1);
  /* Cancellation: the recipient retires mid-call; the original caller resumes intact. */
  retire_mid_touch = true;
  depth = GUEST->depth; rsp = GUEST->cpu.rsp;
  targets = targets_observed_runs;
  touch();
  retire_mid_touch = false;
  CHECK(targets_observed_runs == targets); /* cancelled at the targets observer */
  CHECK(GUEST->depth == depth); CHECK(GUEST->cpu.rsp == rsp);
  qa_compat_q2_native_primary_pickups_assert_idle(&pickups);
  put_i32(GUEST, entity_at(2) + INUSE, 1);
  /* A generation write invalidates the same way. */
  depth = GUEST->depth;
  targets = targets_observed_runs;
  /* The selection retiring the recipient cancels too. */
  PK.retire_during_grant = true;
  rsp = GUEST->cpu.rsp;
  touch();
  PK.retire_during_grant = false; WORLD.alive[2] = true;
  CHECK(targets_observed_runs == targets);
  CHECK(GUEST->depth == depth); CHECK(GUEST->cpu.rsp == rsp);
  /* Ammo supply: the region amount, offer shape and quantity with its stop and restore. */
  set_touch(GUEST, 3);
  put_i32(GUEST, client_at(2) + 0x40 + 3 * 4, 40);
  qa_compat_q2_view_set_i16(GUEST->bytes + client_at(2), 0x1a0, 100);
  PK.pickups = &pickups; PK.quantity_count = 10; PK.quantity_capacity = 200; PK.quantities = 0;
  grant_return_value = 1; ammo_runs = 0; recipient_body_runs = 0;
  uint64_t rax_before = GUEST->cpu.rax;
  touch();
  CHECK(PK.supply_offer.kind == QA_PICKUP_SUPPLY_AMMO); CHECK(PK.supply_offer.grant.amount == 12);
  CHECK(PK.quantities == 1); CHECK(ammo_runs == 1);
  CHECK(PK.quantity.amount == 12); CHECK(PK.quantity.accepted);
  CHECK(get_i32(GUEST, client_at(2) + 0x40 + 3 * 4) == 40);
  CHECK(qa_compat_q2_view_i16(GUEST->bytes + client_at(2), 0x1a0) == 100);
  CHECK(touch_after_grant); CHECK(recipient_body_runs == 0);
  CHECK(GUEST->cpu.rax == rax_before); /* the quantity's processor is restored */
  /* A weapon-flagged ammo item offers ammoWeapon. */
  put_i32(GUEST, ITEM_AT(3) + ITEM_FLAGS, 3);
  touch();
  CHECK(PK.supply_offer.kind == QA_PICKUP_SUPPLY_AMMO_WEAPON); CHECK(strcmp(PK.offer_weapon, "q2:ammo_shells") == 0);
  put_i32(GUEST, ITEM_AT(3) + ITEM_FLAGS, 2);
  /* Quantity outside the ABI is fatal. */
  PK.quantity_capacity = 40000;
  CHECK_ABORTS(touch());
  PK.quantity_capacity = 200;
  /* Declined supply restores the processor and returns 0. */
  PK.outcome = QA_ORIGINAL_PICKUP_OUTCOME_REFUSED;
  rsp = GUEST->cpu.rsp; depth = GUEST->depth;
  touch();
  CHECK(!touch_after_grant); CHECK(GUEST->cpu.rsp == rsp); CHECK(GUEST->depth == depth);
  /* Weapon supply: owner projection, ammo captured, settled at the observer. */
  PK.outcome = QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED; PK.quantities = 0;
  set_touch(GUEST, 2);
  put_i32(GUEST, client_at(2) + 0x40 + 2 * 4, 9);
  qa_compat_q2_native_pickup_supply owner = {&PK, supply_owns};
  qa_compat_q2_native_primary_pickups_bind_supply(&pickups, owner);
  CHECK_ABORTS(qa_compat_q2_native_primary_pickups_bind_supply(&pickups, owner));
  supply_owns_result = true;
  recipient_body_runs = 0;
  put_i32(GUEST, client_at(2) + 0x40 + 3 * 4, 40);
  touch();
  CHECK(weapon_ammo_owned_seen == 1);
  CHECK(get_i32(GUEST, client_at(2) + 0x40 + 2 * 4) == 9);
  CHECK(recipient_body_runs == 0);
  CHECK(PK.supply_offer.kind == QA_PICKUP_SUPPLY_WEAPON); CHECK(strcmp(PK.offer_item, "q2:weapon_shotgun") == 0);
  CHECK(strcmp(PK.ammo_item, "q2:ammo_shells") == 0);
  CHECK(PK.quantities == 1);
  supply_owns_result = false;
  touch();
  CHECK(weapon_ammo_owned_seen == 0);
  qa_compat_q2_native_primary_pickups_unbind_supply(&pickups, owner);
  CHECK(!pickups.has_supply);
  /* Offer identity check outside any frame. */
  qa_original_pickup_offer offer = PK.offer; offer.item = PK.item;
  CHECK_ABORTS(qa_compat_q2_native_primary_pickups_evaluate_supply(&pickups, &offer));
  qa_compat_q2_native_primary_pickups_close(&pickups);
  CHECK(pickups.closed);
  qa_compat_q2_native_primary_pickups_close(&pickups);
  CHECK_ABORTS(qa_compat_q2_native_primary_pickups_bind_supply(&pickups, owner));
  teardown();
}
/* --- native-input --- */
typedef struct {
  guest_t *g; qa_mod_client_applications *applications; const qa_numeric_operations *numeric;
  bool classic; bool has_identity; qa_mod_client_identity identity; bool live; const qa_mod_client_movement_outputs *outputs; bool has_bounds; qa_bounds bounds;
  void *movement_ctx; void (*boundary)(void *, qa_guest_address, void *, void (*)(void *, const qa_movement_body_shape *)); void *active_ctx; bool (*active)(void *);
  void *release_ctx; qa_compat_q2_native_input_release_listener release_listener; int retired;
  int movements, body_shapes; bool has_body; qa_movement_body_shape body; qa_vec3 aim; qa_user_command movement_command;
  qa_compat_q2_native_input_motion_state read_state; bool read_grounded; qa_compat_q2_native_input_motion_view view; bool write_outputs; qa_compat_q2_native_input_motion_outputs outputs_value; qa_compat_q2_native_input_motion_view after_view;
  bool remove_during_run; int original_commands; qa_user_command last_original_seen;
} input_world;
static input_world IN;
#define IN_CHECK(ctx) CHECK((ctx) == &IN)
static bool in_bounds(void *ctx, qa_actor_id a, qa_bounds *out) { (void)a; IN_CHECK(ctx); if (!IN.has_bounds) return false; *out = IN.bounds; return true; }
static const qa_mod_client_movement_outputs *in_outputs(void *ctx, qa_actor_id a) { (void)a; IN_CHECK(ctx); return IN.outputs; }
static bool in_identity(void *ctx, double slot, qa_mod_client_identity *out) { IN_CHECK(ctx); if (slot != 1 || !IN.has_identity) return false; *out = IN.identity; return true; }
static bool in_live(void *ctx, qa_mod_client_identity identity) { (void)identity; IN_CHECK(ctx); return IN.live; }
static const qa_mod_client_command_receipt *in_accepted(void *ctx, qa_actor_id a) { (void)a; IN_CHECK(ctx); return NULL; }
static qa_frame_context in_frame(void *ctx) { IN_CHECK(ctx); qa_frame_context f; memset(&f, 0, sizeof f); f.frame = 3; f.time.kind = QA_SOURCE_TIME_MILLISECONDS; f.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS; f.elapsed.value = 25; f.phase = QA_FRAME_PHASE_FRAME_ENTRY; return f; }
static uint64_t in_on_release(void *ctx, qa_compat_q2_native_input_release_listener listener, void *listener_ctx) { IN_CHECK(ctx); IN.release_listener = listener; IN.release_ctx = listener_ctx; return 77; }
static void in_remove_release(void *ctx, uint64_t handle) { IN_CHECK(ctx); CHECK(handle == 77); IN.release_listener = NULL; }
static void in_retired(void *ctx, qa_mod_client_identity identity) { (void)identity; IN_CHECK(ctx); }
static qa_guest_call_result in_movement(void *ctx, qa_mod_client_identity identity, const qa_compat_q2_native_input_motion *projection, qa_user_command command, qa_vec3 aim, void *run_ctx, qa_guest_call_result (*run)(void *)) {
  (void)identity; IN_CHECK(ctx); IN.movements++; IN.aim = aim; IN.movement_command = command;
  IN.read_state = projection->read(projection->ctx);
  IN.read_grounded = projection->grounded(projection->ctx);
  IN.view = projection->view(projection->ctx);
  if (IN.write_outputs) {
    projection->write_outputs(projection->ctx, &IN.outputs_value);
    qa_compat_q2_native_input_motion_state moved = {{10, 20, 30}, {-8, 0, 4}};
    projection->write(projection->ctx, moved);
    return qa_compat_q2_call_void();
  }
  qa_guest_call_result result = run(run_ctx);
  IN.after_view = projection->view(projection->ctx);
  return result;
}
static void in_retire(void *ctx, double slot, qa_mod_client_identity identity) { (void)slot; (void)identity; IN_CHECK(ctx); IN.retired++; }
/* Fake source host. */
static void src_entry(void *host, const char *name, qa_guest_address *out_address, qa_compat_q2_guest_call_signature *out_signature) { IN_CHECK(host); CHECK(strcmp(name, "ClientThink") == 0); *out_address = addr(IMAGE + 0x100); memset(out_signature, 0, sizeof *out_signature); }
static qa_guest_call_result src_invoke(void *host, qa_guest_address a, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) { (void)signature; IN_CHECK(host); return g_call(IN.g, a, values, count, 0); }
static qa_raw_entity_view src_from_pointer(void *host, qa_guest_address a) { IN_CHECK(host); return view_of((int)((a.byte_offset - ENTITIES) / STRIDE)); }
static qa_raw_entity_view src_at_slot(void *host, double slot) { IN_CHECK(host); return view_of((int)slot); }
static uint8_t *src_client_prefix(void *host, double slot) { IN_CHECK(host); return IN.g->bytes + client_at((int)slot); }
static uint64_t src_bind(void *host, void *ctx, void (*boundary)(void *, qa_guest_address, void *, void (*)(void *, const qa_movement_body_shape *)), void *active_ctx, bool (*active)(void *)) { IN_CHECK(host); IN.movement_ctx = ctx; IN.boundary = boundary; IN.active_ctx = active_ctx; IN.active = active; return 91; }
static void src_unbind(void *host, uint64_t handle) { IN_CHECK(host); CHECK(handle == 91); IN.boundary = NULL; }
#define PMOVE_ADDR 0x70000
#define COMMAND_ADDR 0x60000
static void pmove_run(void *v, const qa_movement_body_shape *body) {
  (void)v;
  IN.body_shapes++;
  IN.has_body = body != NULL;
  if (body != NULL) { IN.body = *body; body->current_actor(body->ctx); }
  /* The original Pmove writes its results. */
  uint8_t *view = IN.g->bytes + PMOVE_ADDR;
  if (IN.classic) { qa_compat_q2_view_set_f32(view, 192, 22); view[16] = (uint8_t)(view[16] | 1); }
  if (IN.remove_during_run) { IN.live = false; IN.release_listener(IN.release_ctx, IN.identity.actor); }
}
static int think_after_pmove;
static qa_guest_call_result g_client_think(guest_t *g, const qa_guest_call_value *a, size_t c) {
  (void)g; (void)a; (void)c; IN.original_commands++;
  uint8_t *cmd = IN.g->bytes + COMMAND_ADDR;
  if (IN.classic) { IN.last_original_seen.kind = QA_USER_COMMAND_Q2; IN.last_original_seen.as.q2 = qa_compat_q2_read_classic_user_command(cmd); }
  else { IN.last_original_seen.kind = QA_USER_COMMAND_Q2_RERELEASE; IN.last_original_seen.as.q2_rerelease = qa_compat_q2_read_rerelease_user_command(cmd); }
  if (IN.boundary != NULL) IN.boundary(IN.movement_ctx, addr(PMOVE_ADDR), NULL, pmove_run);
  think_after_pmove++;
  return qa_compat_q2_call_void();
}
static bool app_live(void *ctx, qa_mod_client_identity identity) { (void)identity; IN_CHECK(ctx); return IN.live; }
static bool app_scope_slice[64]; static int app_before; static bool app_has_parent[64]; static double app_impulse_seen[64]; static double app_elapsed;
static void app_listener(void *ctx, qa_mod_client_application_event *event) {
  IN_CHECK(ctx);
  if (!event->is_before || app_before >= 64) return;
  app_scope_slice[app_before] = strcmp(event->application->scope, "movement-slice") == 0;
  app_has_parent[app_before] = event->application->has_parent_invocation;
  app_impulse_seen[app_before] = event->application->controls_impulse;
  app_elapsed = event->application->frame.elapsed.value;
  app_before++;
}
static qa_compat_q2_native_input_binding *input_setup(bool classic, bool applications_active) {
  setup(classic ? 4 : 8);
  memset(&IN, 0, sizeof IN);
  app_before = 0;
  IN.g = GUEST; IN.classic = classic; IN.has_identity = true; IN.live = true;
  IN.identity.actor = actor_at(1);
  IN.applications = qa_mod_client_applications_create(app_live, &IN);
  if (applications_active) qa_mod_client_applications_subscribe(IN.applications, app_listener, &IN);
  static const qa_numeric_profile profile = {"q2:test-f32", {QA_ARITHMETIC_BINARY32, NULL, 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false}, QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION};
  IN.numeric = qa_create_numeric_operations(&profile);
  g_fn(GUEST, 0x100, g_client_think);
  qa_compat_q2_native_input_source source;
  memset(&source, 0, sizeof source);
  source.classic = classic; source.memory = &GUEST->memory; source.runner = &GUEST->runner; source.host = &IN; source.retire_ctx = &IN; source.retire = in_retire;
  source.entry = src_entry; source.invoke = src_invoke; source.from_pointer = src_from_pointer; source.at_slot = src_at_slot; source.client_prefix = classic ? src_client_prefix : NULL;
  source.bind_input_movement = src_bind; source.unbind_input_movement = src_unbind;
  qa_compat_q2_native_input_services services;
  memset(&services, 0, sizeof services);
  services.applications = IN.applications; services.numeric = IN.numeric; services.ctx = &IN;
  services.body_bounds = in_bounds; services.client_outputs = in_outputs; services.identity = in_identity; services.live = in_live; services.accepted = in_accepted;
  services.frame = in_frame; services.on_release = in_on_release; services.remove_release = in_remove_release; services.retired = in_retired; services.movement = in_movement;
  return qa_compat_q2_native_input_binding_create(source, services);
}
static void input_teardown(qa_compat_q2_native_input_binding *bridge) {
  qa_compat_q2_native_input_binding_destroy(bridge);
  CHECK(IN.boundary == NULL); CHECK(IN.release_listener == NULL);
  qa_mod_client_applications_close(IN.applications);
  qa_mod_client_applications_destroy(IN.applications);
  teardown();
}
static qa_guest_call_result client_think(int slot) {
  qa_guest_call_value args[2] = {ptr_value(entity_at(slot)), ptr_value(COMMAND_ADDR)};
  return g_call(GUEST, addr(IMAGE + 0x100), args, 2, 0);
}
static void test_input_bridge(void) {
  /* native.test.ts:301-316: applications inactive, the bridge still carries body
   * bounds and the requested shape through ClientThink -> Pmove. */
  qa_compat_q2_native_input_binding *bridge = input_setup(false, false);
  qa_compat_q2_rerelease_layouts_ensure();
  CHECK(!qa_mod_client_applications_active(IN.applications));
  int64_t player_field = PMOVE_ADDR + (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "player");
  put_ptr(GUEST, player_field, addr(entity_at(1)));
  IN.has_bounds = true;
  IN.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  qa_bounds requested = {{-7, -9, -20}, {8, 10, 12}};
  qa_mod_client_movement_outputs outputs; memset(&outputs, 0, sizeof outputs); outputs.body_bounds = &requested;
  IN.outputs = &outputs;
  client_think(1);
  CHECK(IN.original_commands == 1); CHECK(IN.movements == 1);
  CHECK(IN.has_body); CHECK(IN.body.has_requested);
  CHECK(IN.body.requested.min.x == -7 && IN.body.requested.max.z == 12);
  CHECK(IN.body.current.min.z == -24);
  /* requestedBody = undefined keeps the current bounds only. */
  IN.outputs = NULL;
  client_think(1);
  CHECK(IN.has_body); CHECK(!IN.body.has_requested);
  /* Requested bounds with no current actor bounds are fatal. */
  IN.outputs = &outputs; IN.has_bounds = false;
  CHECK_ABORTS(client_think(1));
  IN.has_bounds = true;
  /* A foreign pmove player is not this command's: Pmove runs with no body. */
  put_ptr(GUEST, player_field, addr(entity_at(3)));
  int movements = IN.movements;
  client_think(1);
  CHECK(IN.movements == movements); CHECK(!IN.has_body);
  put_ptr(GUEST, player_field, addr(entity_at(1)));
  /* Activity predicate tracks command scope. */
  CHECK(!IN.active(IN.active_ctx));
  /* A slot with no identity is the original call. */
  int originals = IN.original_commands; movements = IN.movements;
  client_think(2);
  CHECK(IN.original_commands == originals + 1); CHECK(IN.movements == movements);
  input_teardown(bridge);
}
static void test_input_classic(void) {
  qa_compat_q2_native_input_binding *bridge = input_setup(true, true);
  CHECK(qa_mod_client_applications_active(IN.applications));
  uint8_t *command = GUEST->bytes + COMMAND_ADDR, *state = GUEST->bytes + client_at(1), *pmove = GUEST->bytes + PMOVE_ADDR;
  /* usercmd: 25 ms, buttons 3, angles, moves, impulse 9, light 7. */
  command[0] = 25; command[1] = 3; qa_compat_q2_view_set_i16(command, 2, 1000); qa_compat_q2_view_set_i16(command, 8, 400); qa_compat_q2_view_set_i16(command, 12, -200); command[14] = 9; command[15] = 7;
  qa_compat_q2_view_set_i16(state, 22, 2000);
  /* pmove_t shares the state prefix and holds cmd at 28. */
  memcpy(pmove + 28, command, 16);
  qa_compat_q2_view_set_i16(pmove, 4, 80); qa_compat_q2_view_set_i16(pmove, 10, -16); pmove[16] = 4;
  qa_compat_q2_view_set_f32(state, 40, 1); qa_compat_q2_view_set_f32(state, 48, 22);
  qa_compat_q2_view_set_f32(pmove, 192, 26);
  client_think(1);
  CHECK(IN.movements == 1);
  CHECK(app_before == 2);
  CHECK(!app_scope_slice[0]); CHECK(app_scope_slice[1]);
  CHECK(!app_has_parent[0]); CHECK(app_has_parent[1]);
  CHECK(app_impulse_seen[0] == 9); CHECK(app_impulse_seen[1] == 9);
  CHECK(app_elapsed == 25);
  CHECK(IN.last_original_seen.as.q2.forward_move == 400 && IN.last_original_seen.as.q2.up_move == -200);
  CHECK(IN.read_state.origin.x == 10); CHECK(IN.read_state.velocity.x == -2);
  CHECK(IN.read_grounded);
  CHECK(IN.view.view_offset.x == 1 && IN.view.view_offset.z == 22); CHECK(!IN.view.crouched);
  CHECK(IN.after_view.view_offset.z == 22); CHECK(IN.after_view.crouched);
  CHECK(IN.movement_command.as.q2.impulse == 9);
  /* Stance output on the slice writes the command; mode output projects pm_type. */
  qa_compat_q2_view_set_i32(pmove, 0, 0);
  qa_compat_q2_view_set_i16(pmove + 28, 12, 0);
  qa_mod_client_movement_outputs outputs; memset(&outputs, 0, sizeof outputs);
  outputs.has_stance = true; outputs.stance = true; outputs.has_mode = true; outputs.mode = QA_MOD_CLIENT_MOVEMENT_FREEZE;
  IN.outputs = &outputs;
  client_think(1);
  CHECK(qa_compat_q2_view_i16(pmove + 28, 12) < 0); /* crouch request */
  CHECK(qa_compat_q2_view_i32(pmove, 0) == 0); /* projected type restored */
  IN.outputs = NULL;
  /* write / write_outputs without running the guest's movement. */
  IN.write_outputs = true;
  IN.outputs_value = (qa_compat_q2_native_input_motion_outputs){.grounded = false, .has_ground_slot = true, .ground_slot = 3, .ducked = true, .water_level = 2, .water_type = 32, .view_height = 9, .view_angles = {1, 2, 3}, .bounds = {{-1, -2, -3}, {4, 5, 6}}};
  static const qa_compat_q2_native_input_touch touches[2] = {{4, {{0, 0, 1}, 0}}, {5, {{0, 0, 1}, 0}}};
  IN.outputs_value.touches = touches; IN.outputs_value.touch_count = 2;
  pmove[16] = 0x84;
  client_think(1);
  CHECK(pmove[16] == 0x81); /* guest bits kept; grounded clear; ducked set */
  CHECK(qa_compat_q2_view_i32(pmove, 224) == 32 && qa_compat_q2_view_i32(pmove, 228) == 2);
  CHECK(qa_compat_q2_view_f32(pmove, 192) == 9);
  CHECK(qa_compat_q2_view_f32(pmove, 184) == 2 && qa_compat_q2_view_f32(pmove, 196) == -1 && qa_compat_q2_view_f32(pmove, 216) == 6);
  CHECK(get_ptr(GUEST, PMOVE_ADDR + 220) == entity_at(3));
  CHECK(qa_compat_q2_view_i32(pmove, 48) == 2); CHECK(get_ptr(GUEST, PMOVE_ADDR + 52 + 4) == entity_at(5));
  CHECK(qa_compat_q2_view_i16(pmove, 4) == 80 && qa_compat_q2_view_i16(pmove, 8) == 240);
  CHECK(qa_compat_q2_view_i16(pmove, 10) == -64);
  IN.write_outputs = false;
  /* Removing the actor mid-move: retire, and the original caller resumes at a controlled exit. */
  double depth = GUEST->depth; uint64_t rsp = GUEST->cpu.rsp;
  int after = think_after_pmove;
  IN.remove_during_run = true;
  qa_guest_call_result result = client_think(1);
  CHECK(IN.retired == 1); CHECK(result.kind == QA_GUEST_VALUE_VOID);
  CHECK(think_after_pmove == after); /* the abandoned guest body did not continue */
  CHECK(GUEST->depth == depth); CHECK(GUEST->cpu.rsp == rsp);
  IN.remove_during_run = false; IN.live = true;
  /* The binding is still usable and its scope stack empty. */
  int movements = IN.movements;
  client_think(1);
  CHECK(IN.movements == movements + 1);
  input_teardown(bridge);
}
static void test_input_rerelease_motion(void) {
  qa_compat_q2_native_input_binding *bridge = input_setup(false, true);
  qa_compat_q2_rerelease_layouts_ensure();
  const qa_guest_layout *layout = &qa_compat_q2_rerelease_pmove_layout;
  uint8_t *command = GUEST->bytes + COMMAND_ADDR, *pmove = GUEST->bytes + PMOVE_ADDR, *state = GUEST->bytes + client_at(1);
  put_ptr(GUEST, entity_at(1) + (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "client"), addr(client_at(1)));
  put_ptr(GUEST, PMOVE_ADDR + (int64_t)qa_compat_q2_rerelease_field_offset(layout, "player"), addr(entity_at(1)));
  qa_q2_rerelease_user_command user; memset(&user, 0, sizeof user);
  user.milliseconds = 16; user.angles.y = 45; user.forward_move = 200;
  qa_compat_q2_write_rerelease_user_command(command, user);
  size_t cmd_offset = (size_t)qa_compat_q2_rerelease_field_offset(layout, "cmd.msec");
  qa_compat_q2_write_rerelease_user_command(pmove + cmd_offset, user);
  qa_compat_q2_view_set_f32(state, 40, 15);
  qa_compat_q2_view_set_f32(pmove, 40, 15);
  qa_compat_q2_view_set_f32(pmove, 4, 7); qa_compat_q2_view_set_f32(pmove, 16, -9);
  size_t flags = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_state_layout, "pm_flags");
  size_t viewheight = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_state_layout, "viewheight");
  qa_compat_q2_view_set_u16(pmove, flags, 5);
  pmove[viewheight] = 22;
  qa_compat_q2_view_set_f32(pmove, (size_t)qa_compat_q2_rerelease_field_offset(layout, "viewoffset") + 8, 4);
  IN.has_bounds = true;
  IN.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  client_think(1);
  CHECK(IN.movements == 1);
  CHECK(IN.read_state.origin.x == 7 && IN.read_state.velocity.x == -9); CHECK(IN.read_grounded);
  CHECK(IN.view.view_offset.z == 26); CHECK(IN.view.crouched);
  CHECK(IN.aim.y == 60); /* command angle + delta_angles */
  CHECK(app_scope_slice[1]);
  IN.write_outputs = true;
  IN.outputs_value = (qa_compat_q2_native_input_motion_outputs){.grounded = true, .has_ground_slot = false, .ducked = false, .water_level = 1, .water_type = 8, .view_height = -3, .view_angles = {5, 6, 7}, .bounds = {{-1, -2, -3}, {4, 5, 6}}};
  static const qa_compat_q2_native_input_touch touches[1] = {{6, {{0, 0, 1}, 0}}};
  IN.outputs_value.touches = touches; IN.outputs_value.touch_count = 1;
  qa_compat_q2_view_set_u16(pmove, flags, 0x81);
  client_think(1);
  CHECK(qa_compat_q2_view_u16(pmove, flags) == 0x84);
  CHECK(qa_compat_q2_view_u32(pmove, (size_t)qa_compat_q2_rerelease_field_offset(layout, "watertype")) == 8);
  CHECK(pmove[(size_t)qa_compat_q2_rerelease_field_offset(layout, "waterlevel")] == 1);
  CHECK((int8_t)pmove[viewheight] == -3);
  CHECK(qa_compat_q2_view_f32(pmove, (size_t)qa_compat_q2_rerelease_field_offset(layout, "viewangles") + 8) == 7);
  CHECK(qa_compat_q2_view_f32(pmove, (size_t)qa_compat_q2_rerelease_field_offset(layout, "maxs")) == 4);
  CHECK(qa_compat_q2_view_f32(pmove, (size_t)qa_compat_q2_rerelease_field_offset(layout, "viewoffset") + 8) == 0);
  CHECK(get_ptr(GUEST, PMOVE_ADDR + (int64_t)qa_compat_q2_rerelease_field_offset(layout, "groundentity")) == 0);
  CHECK(qa_compat_q2_view_u32(pmove, (size_t)qa_compat_q2_rerelease_field_offset(layout, "touch.num")) == 1);
  CHECK(get_ptr(GUEST, PMOVE_ADDR + (int64_t)qa_compat_q2_rerelease_field_offset(layout, "touch.touches[0].ent")) == entity_at(6));
  CHECK(get_f32(GUEST, PMOVE_ADDR + (int64_t)qa_compat_q2_rerelease_field_offset(layout, "touch.touches[0].plane.normal") + 8) == 1);
  CHECK(qa_compat_q2_view_f32(pmove, 4) == 10 && qa_compat_q2_view_f32(pmove, 16 + 8) == 4);
  input_teardown(bridge);
}
static void test_read_classic_user_command(void) {
  const uint8_t bytes[16] = {25, 3, 0xe8, 0x03, 0x18, 0xfc, 0, 0, 0x90, 0x01, 0, 0, 0x38, 0xff, 9, 7};
  qa_q2_user_command c = qa_compat_q2_read_classic_user_command(bytes);
  CHECK(c.milliseconds == 25 && c.buttons == 3); CHECK(c.angle_shorts.values[0] == 1000 && c.angle_shorts.values[1] == -1000);
  CHECK(c.forward_move == 400 && c.up_move == -200 && c.impulse == 9 && c.light_level == 7);
}
/* --- native-mod-pickups --- */
static const qa_native_mod_source_call CALL_INT = {.returns = "int32"}, CALL_VOID = {.returns = "void"};
static qa_native_mod_clients EMPTY_CLIENTS;
static qa_native_mod_declaration declaration_with(const qa_native_mod_pickup *pickups, size_t count) {
  qa_native_mod_declaration d; memset(&d, 0, sizeof d);
  d.clients = &EMPTY_CLIENTS;
  d.pickups = pickups; d.pickup_count = count;
  return d;
}
typedef struct { int currents, contexts, observes, invokes; bool eligible; double values[2]; bool null_gate; size_t input_count; double time_input, count_input, has_count_input, dropped_input; const char *item_input; qa_actor_id self_input, other_input; bool current_result; qa_session_actor_registry *registry; qa_owned_actor pickup; bool release_pickup_in_invoke; } mod_pickup_log;
static mod_pickup_log MP;
static void mp_current(void *ctx) { CHECK(ctx == &MP); MP.currents++; }
static bool mp_eligible(void *ctx, qa_actor_id a) { (void)a; CHECK(ctx == &MP); return MP.eligible; }
static void mp_context(void *ctx, const qa_native_mod_pickup *d, const qa_original_pickup_offer *offer, qa_compat_q2_native_pickup_inputs inputs, void *execute_ctx, void (*execute)(void *)) { (void)d; (void)offer; (void)inputs; CHECK(ctx == &MP); MP.contexts++; execute(execute_ctx); }
static void mp_observe(void *ctx, qa_actor_id a, const qa_original_pickup_execution *observer, void *execute_ctx, void (*execute)(void *)) { (void)a; (void)observer; CHECK(ctx == &MP); MP.observes++; execute(execute_ctx); }
static double input_float(qa_compat_q2_native_pickup_inputs inputs, const char *name) { for (size_t i = 0; i < inputs.count; i++) if (strcmp(inputs.rows[i].name, name) == 0) return inputs.rows[i].value.number_value; return NAN; }
static bool mp_invoke(void *ctx, const qa_native_mod_source_call *call, qa_compat_q2_native_pickup_inputs inputs, double *out) {
  (void)call; CHECK(ctx == &MP);
  MP.input_count = inputs.count; MP.time_input = input_float(inputs, "time"); MP.count_input = input_float(inputs, "pickup-count"); MP.has_count_input = input_float(inputs, "pickup-has-count"); MP.dropped_input = input_float(inputs, "pickup-dropped");
  for (size_t i = 0; i < inputs.count; i++) {
    if (strcmp(inputs.rows[i].name, "item") == 0) MP.item_input = inputs.rows[i].value.string_value;
    if (strcmp(inputs.rows[i].name, "self") == 0) MP.self_input = *inputs.rows[i].value.actor_value;
    if (strcmp(inputs.rows[i].name, "other") == 0) MP.other_input = *inputs.rows[i].value.actor_value;
  }
  int index = MP.invokes++;
  if (MP.null_gate && index == 0) return false;
  *out = MP.values[index];
  if (MP.release_pickup_in_invoke && index == 0) qa_session_actor_registry_release(MP.registry, MP.pickup);
  return true;
}
static bool exec_current(void *ctx) { CHECK(ctx == &MP); return MP.current_result; }
static const qa_inventory_entry PRIMARY_ENTRIES[1] = {{"q2:shard", 0, 10, false, {NULL, NULL}}};
static const qa_inventory_entry *primary_read(void *ctx, size_t *n) { (void)ctx; *n = 1; return PRIMARY_ENTRIES; }
static void primary_write(void *ctx, qa_inventory_entry e) { (void)ctx; (void)e; }
static void test_mod_pickups(void) {
  static const qa_pickup_write inventory_write[1] = {{QA_PICKUP_RESOURCE_INVENTORY, QA_PROTECTION_REGULAR, "q2:shard", "count"}};
  static const qa_pickup_write capacity_write[1] = {{QA_PICKUP_RESOURCE_INVENTORY, QA_PROTECTION_REGULAR, "q2:shard", "capacity"}};
  static const qa_pickup_write protection_write[1] = {{QA_PICKUP_RESOURCE_PROTECTION, QA_PROTECTION_POWERED, NULL, NULL}};
  static const qa_item_id offered_a[1] = {"q2:shard"}, offered_b[1] = {"q2:armor"};
  qa_native_mod_pickup pickups[2]; memset(pickups, 0, sizeof pickups);
  pickups[0].rule = (qa_mod_pickup_rule){"gate", {inventory_write, 1}, offered_a, 1, {true, &CALL_INT, &CALL_INT, sizeof CALL_INT, "nonzero"}};
  pickups[1].rule = (qa_mod_pickup_rule){"grant", {protection_write, 1}, offered_b, 1, {false, NULL, &CALL_INT, sizeof CALL_INT, NULL}};
  /* Validation: storage is required for each declared write. */
  static const qa_native_mod_actor_field fields[2] = {{.offset = 0, .binding = "inventory", .encoding = "int32", .item = "q2:shard"}, {.offset = 4, .binding = "inventory-capacity", .encoding = "int32", .item = "q2:shard"}};
  static const qa_native_mod_actor_record records[1] = {{.id = "client", .stride = 64, .fields = fields, .field_count = 2}};
  static const qa_native_mod_protection_definition protection[1] = {{.id = "armor", .channel = "powered"}};
  qa_native_mod_declaration d = declaration_with(pickups, 2);
  d.actor_records = records; d.actor_record_count = 1; d.protection = protection; d.protection_count = 1;
  qa_compat_q2_validate_native_mod_pickups(&d);
  qa_native_mod_declaration no_clients = d; no_clients.clients = NULL;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&no_clients));
  qa_native_mod_declaration none = d; none.protection_count = 0;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  none = d; none.actor_record_count = 0;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  qa_native_mod_pickup variant[2]; memcpy(variant, pickups, sizeof variant);
  variant[1].rule.id = "gate";
  none = d; none.pickups = variant;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  memcpy(variant, pickups, sizeof variant);
  variant[1].rule.offered = offered_a;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  memcpy(variant, pickups, sizeof variant);
  variant[0].rule.writes.values = capacity_write;
  qa_compat_q2_validate_native_mod_pickups(&none); /* inventory-capacity declares it */
  static const qa_native_mod_actor_record no_capacity[1] = {{.id = "client", .stride = 64, .fields = fields, .field_count = 1}};
  none.actor_records = no_capacity;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  none = d; none.pickups = variant; memcpy(variant, pickups, sizeof variant);
  variant[1].rule.operation.grant = &CALL_VOID;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  memcpy(variant, pickups, sizeof variant);
  variant[0].rule.operation.gate = &CALL_VOID;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  memcpy(variant, pickups, sizeof variant);
  variant[0].rule.operation.grant = &CALL_VOID;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_pickups(&none));
  variant[0].rule.operation.grant_accepts = "always";
  qa_compat_q2_validate_native_mod_pickups(&none);
  /* Runtime over the real actor registry and inventory table. */
  qa_identity_owner identities; CHECK(qa_identity_owner_create(&identities, "mod-pickups"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 64);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_owned_actor recipient = qa_session_actor_registry_allocate(actors, "q2:test", "player"), pickup = qa_session_actor_registry_allocate(actors, "q2:test", "item");
  qa_inventory_state_binding primary = {NULL, primary_read, NULL, primary_write, NULL};
  qa_shared_inventory_table_bind(inventory, recipient, primary);
  qa_mod_host_services services; memset(&services, 0, sizeof services);
  services.actors = actors; services.inventory = inventory;
  memset(&MP, 0, sizeof MP);
  MP.eligible = true; MP.current_result = true; MP.registry = actors; MP.pickup = pickup; MP.values[0] = 1; MP.values[1] = 5;
  qa_compat_q2_native_mod_pickup_operations operations = {&MP, mp_current, mp_eligible, mp_context, mp_observe, mp_invoke};
  qa_compat_q2_native_mod_pickups *s = qa_compat_q2_native_mod_pickups_create(pickups, 2, &services, "q2:mod", operations);
  size_t count = 0;
  const qa_original_pickup_rule *rules = qa_compat_q2_native_mod_pickups_protection(s, recipient.id, QA_PROTECTION_POWERED, &count);
  CHECK(count == 1 && strcmp(rules[0].id, "grant") == 0);
  qa_compat_q2_native_mod_pickups_protection(s, recipient.id, QA_PROTECTION_REGULAR, &count);
  CHECK(count == 0);
  rules = qa_compat_q2_native_mod_pickups_protection(s, recipient.id, QA_PROTECTION_POWERED, &count);
  qa_original_pickup_rule grant_rule = rules[0];
  qa_original_pickup_offer offer; memset(&offer, 0, sizeof offer);
  offer.recipient = recipient.id; offer.pickup = pickup.id; offer.item = "q2:armor"; offer.time.kind = QA_SOURCE_TIME_MILLISECONDS; offer.time.value = 1500; offer.count.is_override = true; offer.count.amount = 3; offer.dropped = true;
  qa_original_pickup_execution execution; memset(&execution, 0, sizeof execution);
  execution.current_ctx = &MP; execution.current = exec_current;
  /* Inactive rules are refused as not current. */
  CHECK_ABORTS(grant_rule.take(grant_rule.ctx, offer, execution));
  qa_compat_q2_native_mod_pickups_activate(s);
  CHECK(MP.currents >= 1);
  qa_compat_q2_native_mod_pickups_bind_actor(s, recipient.id);
  qa_compat_q2_native_mod_pickups_bind_actor(s, recipient.id);
  /* The bound inventory delegate resolves a shard offer through the table. */
  qa_original_pickup_offer shard = offer; shard.item = "q2:shard";
  qa_original_pickup_resolution resolution = qa_shared_inventory_table_resolve_pickup(inventory, recipient, shard);
  CHECK(resolution.match_count == 1);
  qa_original_pickup_rule gate = resolution.matches[0].operation;
  /* Boolean grant. */
  MP.invokes = 0; MP.values[0] = 5;
  CHECK(grant_rule.take(grant_rule.ctx, offer, execution) == QA_ORIGINAL_PICKUP_ACCEPTED);
  CHECK(MP.contexts == 1 && MP.observes == 1 && MP.invokes == 1);
  CHECK(MP.input_count == 7); CHECK(MP.time_input == 1.5); CHECK(MP.count_input == 3); CHECK(MP.has_count_input == 1); CHECK(MP.dropped_input == 1);
  CHECK(strcmp(MP.item_input, "q2:armor") == 0); CHECK(qa_same_actor(MP.self_input, recipient.id)); CHECK(qa_same_actor(MP.other_input, pickup.id));
  MP.invokes = 0; MP.values[0] = 0;
  CHECK(grant_rule.take(grant_rule.ctx, offer, execution) == QA_ORIGINAL_PICKUP_REFUSED);
  /* Seconds input and default count. */
  offer.time.kind = QA_SOURCE_TIME_SECONDS; offer.time.value = 2; offer.count.is_override = false; offer.dropped = false;
  MP.invokes = 0; MP.values[0] = 1;
  CHECK(grant_rule.take(grant_rule.ctx, offer, execution) == QA_ORIGINAL_PICKUP_ACCEPTED);
  CHECK(MP.time_input == 2 && MP.count_input == 0 && MP.has_count_input == 0 && MP.dropped_input == 0);
  /* Wrong recipient / item / ineligible / stale observer. */
  qa_original_pickup_offer wrong = offer; wrong.item = "q2:other";
  CHECK_ABORTS(grant_rule.take(grant_rule.ctx, wrong, execution));
  wrong = offer; wrong.recipient = pickup.id;
  CHECK_ABORTS(grant_rule.take(grant_rule.ctx, wrong, execution));
  MP.eligible = false;
  CHECK_ABORTS(grant_rule.take(grant_rule.ctx, offer, execution));
  MP.eligible = true;
  /* Gate then grant. */
  MP.invokes = 0; MP.values[0] = 1; MP.values[1] = 9;
  CHECK(gate.take(gate.ctx, shard, execution) == QA_ORIGINAL_PICKUP_ACCEPTED);
  CHECK(MP.invokes == 2);
  MP.invokes = 0; MP.values[0] = 0;
  CHECK(gate.take(gate.ctx, shard, execution) == QA_ORIGINAL_PICKUP_REFUSED);
  CHECK(MP.invokes == 1);
  MP.invokes = 0; MP.null_gate = true;
  CHECK(gate.take(gate.ctx, shard, execution) == QA_ORIGINAL_PICKUP_REFUSED);
  MP.null_gate = false;
  MP.invokes = 0; MP.values[0] = 1; MP.values[1] = 0;
  CHECK(gate.take(gate.ctx, shard, execution) == QA_ORIGINAL_PICKUP_REFUSED);
  /* A retired pickup after the gate refuses. */
  MP.invokes = 0; MP.values[1] = 9; MP.release_pickup_in_invoke = true;
  CHECK(gate.take(gate.ctx, shard, execution) == QA_ORIGINAL_PICKUP_REFUSED);
  MP.release_pickup_in_invoke = false;
  qa_compat_q2_native_mod_pickups_assert_idle(s);
  qa_compat_q2_native_mod_pickups_release(s, recipient.id);
  resolution = qa_shared_inventory_table_resolve_pickup(inventory, recipient, shard);
  CHECK(resolution.match_count == 0);
  /* Ineligible actors bind nothing. */
  MP.eligible = false;
  qa_compat_q2_native_mod_pickups_bind_actor(s, recipient.id);
  resolution = qa_shared_inventory_table_resolve_pickup(inventory, recipient, shard);
  CHECK(resolution.match_count == 0);
  qa_compat_q2_native_mod_pickups_destroy(s);
  qa_shared_inventory_table_destroy(inventory);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}
/* --- native-mod-items --- */
#define ITEM_RECORD 0x80000
typedef struct { guest_t *g; bool live; int64_t record; int invokes; const qa_original_pickup_execution *pickup; qa_source_weapon_presentation_model model; } item_ops;
static item_ops IO;
static qa_guest_address io_pointer(void *ctx, qa_actor_id a, const char *record) { (void)a; (void)record; CHECK(ctx == &IO); return addr(IO.record); }
static qa_guest_address io_resolve(void *ctx, qa_native_mod_address address) { CHECK(ctx == &IO); return addr(IMAGE + (int64_t)address.rva); }
static bool io_live(void *ctx, qa_actor_id a) { (void)a; CHECK(ctx == &IO); return IO.live; }
static double io_read(void *ctx, qa_guest_address a, qa_native_mod_scalar encoding) {
  CHECK(ctx == &IO);
  if (strcmp(encoding, "int32") == 0) return get_i32(IO.g, a.byte_offset);
  if (strcmp(encoding, "uint8") == 0) return IO.g->bytes[a.byte_offset];
  if (strcmp(encoding, "int16") == 0) return qa_compat_q2_view_i16(IO.g->bytes, (size_t)a.byte_offset);
  if (strcmp(encoding, "float32") == 0) return get_f32(IO.g, a.byte_offset);
  qa_range_error("encoding %s", encoding);
}
static void io_write(void *ctx, qa_guest_address a, qa_native_mod_scalar encoding, double value) {
  CHECK(ctx == &IO);
  if (strcmp(encoding, "int32") == 0) qa_compat_q2_mem_write_i32(&IO.g->memory, a, (int32_t)value);
  else if (strcmp(encoding, "uint8") == 0) qa_compat_q2_mem_write_u8(&IO.g->memory, a, (uint32_t)value);
  else if (strcmp(encoding, "int16") == 0) qa_compat_q2_mem_write_i16(&IO.g->memory, a, (int32_t)value);
  else if (strcmp(encoding, "float32") == 0) qa_compat_q2_mem_write_f32(&IO.g->memory, a, value);
  else qa_range_error("encoding %s", encoding);
}
static const qa_native_mod_source_call CALL_ACTION = {.returns = "void"};
static int request_calls; static int64_t request_writes_pending;
static void io_invoke(void *ctx, qa_actor_id a, const qa_native_mod_source_call *call) {
  (void)a; CHECK(ctx == &IO); IO.invokes++;
  if (strcmp(call->returns, "request") == 0) { request_calls++; qa_compat_q2_mem_write_pointer(&IO.g->memory, addr(IO.record + 0x20), true, addr(request_writes_pending)); }
}
static const qa_source_weapon_presentation_model *io_model(void *ctx, qa_actor_id a) { (void)a; CHECK(ctx == &IO); return &IO.model; }
static const qa_original_pickup_execution *io_pickup(void *ctx) { CHECK(ctx == &IO); return IO.pickup; }
/* Weapon service stub. */
typedef struct { bool selected; int requests; qa_item_id last_request; qa_source_weapon_binding binding; bool bound; int unbound; } weapon_service_log;
static weapon_service_log WS;
static void ws_unbind(void *ctx) { CHECK(ctx == &WS); WS.unbound++; }
static qa_source_weapon_unbind ws_bind(void *ctx, qa_owned_actor a, qa_source_weapon_binding binding) { (void)a; CHECK(ctx == &WS); WS.binding = binding; WS.bound = true; qa_source_weapon_unbind u = {&WS, ws_unbind}; return u; }
static bool ws_selected(void *ctx, qa_actor_id a, qa_provider_id p) { (void)a; (void)p; CHECK(ctx == &WS); return WS.selected; }
static bool ws_request(void *ctx, qa_actor_id a, qa_weapon_reference w) { (void)a; CHECK(ctx == &WS); WS.requests++; WS.last_request = w.item; return true; }
static qa_guest_call_result item_entry(void *ctx, qa_guest_address a, qa_compat_q2_guest_call_signature s, const qa_guest_call_value *v, size_t n) { CHECK(ctx == &IO); (void)s; return g_call(IO.g, a, v, n, 0); }
static qa_guest_address item_export(void *ctx, const char *name) { CHECK(ctx == &IO); qa_range_error("no %s", name); }
static void retired_dispatch(void *v) { qa_guest_call_value *arg = v; g_call(IO.g, addr(IMAGE + 0x700), arg, 1, 0); }
static void test_mod_items(void) {
  /* Declaration: counter (field capacity), bits, weapon pointer stage. */
  static const qa_native_mod_actor_field fields[1] = {{.offset = 0x100, .binding = "private", .qvm_field = {.byte_length = 16}}};
  static const qa_native_mod_actor_record records[1] = {{.id = "client", .stride = 0x200, .fields = fields, .field_count = 1}};
  static const char *const client_records[1] = {"client"};
  static qa_native_mod_clients clients; clients.records.values = client_records; clients.records.count = 1;
  static const qa_native_item_mask bits[2] = {{"q2:keya", 1}, {"q2:keyb", 2}};
  qa_native_item_storage storage[4]; memset(storage, 0, sizeof storage);
  storage[0].kind = "counter"; storage[0].field = FIELD("client", 0x00, "int32"); storage[0].item = "q2:shells"; storage[0].capacity.kind = "field"; storage[0].capacity.field = FIELD("client", 0x08, "int32");
  storage[1].kind = "bits"; storage[1].field = FIELD("client", 0x10, "uint8"); storage[1].private_mask = 0x80; storage[1].items = bits; storage[1].item_count = 2;
  storage[2].kind = "counter"; storage[2].field = FIELD("client", 0x14, "int32"); storage[2].item = "q2:gun"; storage[2].capacity.kind = "constant"; storage[2].capacity.value = 1;
  storage[3].kind = "counter"; storage[3].field = FIELD("client", 0x18, "float32"); storage[3].item = "q2:cells"; storage[3].capacity.kind = "source"; storage[3].capacity.address = (qa_native_mod_address){0x900, {NULL, 0}}; storage[3].capacity.encoding = "int32";
  qa_native_mod_item_definition definitions[5]; memset(definitions, 0, sizeof definitions);
  const char *names[5] = {"q2:shells", "q2:keya", "q2:keyb", "q2:gun", "q2:cells"};
  for (size_t i = 0; i < 5; i++) { definitions[i].item = names[i]; definitions[i].label = names[i]; definitions[i].admission = "add"; }
  definitions[3].is_weapon = true; definitions[3].weapon.ammo = "q2:shells";
  definitions[0].has_actions = true; definitions[0].action_use = &CALL_ACTION;
  static qa_native_item_test settled_tests[1], continuation_tests[1];
  memset(settled_tests, 0, sizeof settled_tests); memset(continuation_tests, 0, sizeof continuation_tests);
  settled_tests[0].kind = "scalar"; settled_tests[0].field = FIELD("client", 0x30, "int32"); settled_tests[0].comparison = "equals"; settled_tests[0].scalar_value = 0;
  continuation_tests[0].kind = "scalar"; continuation_tests[0].field = FIELD("client", 0x34, "int32"); continuation_tests[0].comparison = "equals"; continuation_tests[0].scalar_value = 1;
  static const qa_native_item_test_row settled_rows[1] = {{settled_tests, 1}}, continuation_rows[1] = {{continuation_tests, 1}};
  static const qa_native_weapon_decision_field decision_fields[1] = {{{{0x38, "int32"}, "client"}, 1}};
  static const qa_native_weapon_decision decisions[1] = {{0x710, 0x718, decision_fields, 1}};
  static const qa_native_item_pointer pending = {"client", 0x20};
  static qa_native_weapon_selection_value selection_values[1];
  selection_values[0].item = "q2:gun"; selection_values[0].address = (qa_native_mod_address){0xa00, {NULL, 0}}; selection_values[0].request.returns = "request";
  qa_native_weapon_stage stage; memset(&stage, 0, sizeof stage);
  stage.dispatcher.entry.kind = "rva"; stage.dispatcher.entry.rva = 0x700; stage.dispatcher.record = "client"; stage.dispatcher.argument = 0; stage.dispatcher.arguments = 1;
  stage.decisions = decisions; stage.decision_count = 1; stage.continuations = continuation_rows; stage.continuation_row_count = 1; stage.settled = settled_rows; stage.settled_row_count = 1;
  stage.selection.active = (qa_native_item_pointer){"client", 0x28}; stage.selection.pending = &pending; stage.selection.values = selection_values; stage.selection.value_count = 1;
  qa_native_mod_items definition; memset(&definition, 0, sizeof definition);
  definition.definitions = definitions; definition.definition_count = 5; definition.storage = storage; definition.storage_count = 4; definition.weapons = &stage;
  qa_native_mod_declaration d; memset(&d, 0, sizeof d);
  d.items = &definition; d.clients = &clients; d.actor_records = records; d.actor_record_count = 1; d.target.abi = ABI64;
  qa_compat_q2_validate_native_mod_items(&d);
  /* Validation refusals. */
  qa_native_mod_declaration v = d; v.clients = NULL;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  qa_native_mod_items changed_items = definition; v = d; v.items = &changed_items;
  changed_items.weapons = NULL;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v)); /* a weapon without a dispatcher */
  qa_native_item_storage changed_storage[4]; memcpy(changed_storage, storage, sizeof changed_storage);
  changed_items = definition; changed_items.storage = changed_storage;
  changed_storage[0].field.field.offset = 0x1ff;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v)); /* exceeds the record */
  memcpy(changed_storage, storage, sizeof changed_storage);
  changed_storage[2].field.field.offset = 0x02;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v)); /* overlaps shells */
  memcpy(changed_storage, storage, sizeof changed_storage);
  changed_storage[2].field.field.offset = 0x104;
  qa_compat_q2_validate_native_mod_items(&v); /* private projections may share bytes */
  static const qa_native_mod_actor_field health_field[1] = {{.offset = 0x104, .binding = "health", .encoding = "int32"}};
  static const qa_native_mod_actor_record health_records[1] = {{.id = "client", .stride = 0x200, .fields = health_field, .field_count = 1}};
  qa_native_mod_declaration health = v; health.actor_records = health_records;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&health)); /* a canonical projection may not */
  memcpy(changed_storage, storage, sizeof changed_storage);
  static const qa_native_item_mask overlap_bits[2] = {{"q2:keya", 1}, {"q2:keyb", 3}};
  changed_storage[1].items = overlap_bits;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  memcpy(changed_storage, storage, sizeof changed_storage);
  static const qa_native_item_mask wide_bits[2] = {{"q2:keya", 1}, {"q2:keyb", 256}};
  changed_storage[1].items = wide_bits;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  memcpy(changed_storage, storage, sizeof changed_storage);
  changed_storage[2].capacity.value = -1;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  memcpy(changed_storage, storage, sizeof changed_storage);
  changed_storage[3].item = "q2:shells";
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v)); /* lacks distinct storage */
  changed_items = definition; changed_items.storage_count = 3;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v)); /* definition without storage */
  qa_native_mod_item_definition changed_defs[5]; memcpy(changed_defs, definitions, sizeof changed_defs);
  changed_items = definition; changed_items.definitions = changed_defs;
  changed_defs[1].item = "q2:shells";
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v)); /* duplicate */
  memcpy(changed_defs, definitions, sizeof changed_defs);
  changed_defs[3].weapon.ammo = "q2:absent";
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  qa_native_weapon_stage changed_stage = stage;
  changed_items = definition; changed_items.weapons = &changed_stage;
  changed_stage.decision_count = 0;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  changed_stage = stage; changed_stage.dispatcher.argument = 1;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  changed_stage = stage; changed_stage.selection.value_count = 0;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_items(&v));
  /* Runtime. */
  setup(8);
  memset(&IO, 0, sizeof IO); memset(&WS, 0, sizeof WS);
  IO.g = GUEST; IO.live = true; IO.record = ITEM_RECORD;
  put_i32(GUEST, ITEM_RECORD + 0, 5); put_i32(GUEST, ITEM_RECORD + 0x08, 50);
  GUEST->bytes[ITEM_RECORD + 0x10] = 1;
  put_i32(GUEST, ITEM_RECORD + 0x14, 1);
  qa_compat_q2_mem_write_f32(&GUEST->memory, addr(ITEM_RECORD + 0x18), 2.5);
  put_i32(GUEST, IMAGE + 0x900, 200);
  g_fn(GUEST, 0x700, g_void);
  qa_identity_owner identities; CHECK(qa_identity_owner_create(&identities, "mod-items"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 64);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_owned_actor owner = qa_session_actor_registry_allocate(actors, "q2:test", "player");
  qa_inventory_state_binding primary = {NULL, primary_read, NULL, primary_write, NULL};
  qa_shared_inventory_table_bind(inventory, owner, primary);
  qa_source_weapon_services weapons = {&WS, ws_bind, ws_selected, NULL, ws_request};
  qa_mod_host_services services; memset(&services, 0, sizeof services);
  services.actors = actors; services.inventory = inventory; services.weapons = &weapons;
  qa_compat_q2_native_mod_item_operations operations = {&IO, io_pointer, io_resolve, io_live, io_read, io_write, io_invoke, io_model, io_pickup};
  qa_compat_q2_native_mod_item_host host = {&GUEST->memory, &GUEST->runner, GUEST->image, &IO, item_entry, item_export};
  qa_compat_q2_native_mod_items *items = qa_compat_q2_native_mod_items_create(&definition, ABI64, host, &services, "q2:mod", "q2:test:content:one", operations);
  qa_actor_id actor = owner.id;
  qa_compat_q2_native_mod_items_admit(items, actor);
  qa_compat_q2_native_mod_items_admit(items, actor); /* idempotent */
  CHECK(WS.bound);
  /* Inventory reads borrow original words. */
  CHECK(qa_shared_inventory_table_count(inventory, actor, "q2:shells") == 5);
  CHECK(qa_shared_inventory_table_count(inventory, actor, "q2:keya") == 1);
  CHECK(qa_shared_inventory_table_count(inventory, actor, "q2:keyb") == 0);
  CHECK(qa_shared_inventory_table_count(inventory, actor, "q2:cells") == 2.5);
  CHECK(qa_shared_inventory_table_mutable_capacity(inventory, actor, "q2:shells"));
  CHECK(!qa_shared_inventory_table_mutable_capacity(inventory, actor, "q2:gun"));
  /* Writes reach the source. */
  qa_inventory_entry shells = {"q2:shells", 9, 60, false, {NULL, NULL}};
  qa_shared_inventory_table_configure(inventory, owner, shells, NULL, NULL);
  CHECK(get_i32(GUEST, ITEM_RECORD + 0) == 9); CHECK(get_i32(GUEST, ITEM_RECORD + 8) == 60);
  qa_inventory_entry key = {"q2:keyb", 1, 1, false, {NULL, NULL}};
  qa_shared_inventory_table_configure(inventory, owner, key, NULL, NULL);
  CHECK(GUEST->bytes[ITEM_RECORD + 0x10] == 3);
  key.count = 0;
  qa_shared_inventory_table_configure(inventory, owner, key, NULL, NULL);
  CHECK(GUEST->bytes[ITEM_RECORD + 0x10] == 1);
  /* Undeclared bits are fatal to read. */
  uint8_t saved_bits = GUEST->bytes[ITEM_RECORD + 0x10];
  GUEST->bytes[ITEM_RECORD + 0x10] = 0x40;
  CHECK_ABORTS(qa_shared_inventory_table_count(inventory, actor, "q2:keya"));
  GUEST->bytes[ITEM_RECORD + 0x10] = saved_bits;
  /* Weapon handoff. */
  qa_source_weapon_handoff handoff = WS.binding.handoff;
  CHECK(WS.binding.current(WS.binding.ctx));
  CHECK(handoff.is_source_input);
  CHECK(handoff.accepts(handoff.ctx, "q2:gun"));
  CHECK(!handoff.accepts(handoff.ctx, "q2:shells"));
  /* Requesting makes the source pend it. */
  request_writes_pending = IMAGE + 0xa00;
  qa_source_weapon_request request = handoff.resume_request(handoff.ctx, "q2:gun");
  CHECK(request.id == 1); CHECK(request_calls == 1);
  CHECK(strcmp(request.status(request.ctx), "pending") == 0);
  CHECK(WS.requests == 0); /* our own request does not re-enter the service */
  qa_source_weapon_presentation presentation = WS.binding.read(WS.binding.ctx);
  CHECK(strcmp(presentation.pending, "q2:gun") == 0); CHECK(presentation.active == NULL);
  CHECK(presentation.item_count == 5); CHECK(presentation.items[3].is_weapon); CHECK(presentation.model == &IO.model);
  CHECK(strcmp(presentation.source.provider, "q2:mod") == 0);
  /* A source-driven selection publishes a service request. */
  qa_compat_q2_mem_write_pointer(&GUEST->memory, addr(ITEM_RECORD + 0x20), false, addr(0));
  CHECK(WS.requests == 0);
  qa_compat_q2_mem_write_pointer(&GUEST->memory, addr(ITEM_RECORD + 0x28), true, addr(IMAGE + 0xa00));
  CHECK(WS.requests == 1); CHECK(strcmp(WS.last_request, "q2:gun") == 0);
  /* The dispatcher's completion settles the pending request as accepted. */
  CHECK(strcmp(request.status(request.ctx), "pending") == 0);
  qa_guest_call_value dispatch_arg = ptr_value(ITEM_RECORD);
  g_call(GUEST, addr(IMAGE + 0x700), &dispatch_arg, 1, 0);
  CHECK(strcmp(request.status(request.ctx), "accepted") == 0);
  /* An already-active request is accepted immediately. */
  qa_source_weapon_request active_request = handoff.resume_request(handoff.ctx, "q2:gun");
  CHECK(strcmp(active_request.status(active_request.ctx), "accepted") == 0);
  /* The older capability is superseded. */
  CHECK(strcmp(request.status(request.ctx), "refused") == 0);
  /* null resume is accepted only when settled and active. */
  qa_source_weapon_request holster = handoff.resume_request(handoff.ctx, NULL);
  CHECK(strcmp(holster.status(holster.ctx), "accepted") == 0);
  put_i32(GUEST, ITEM_RECORD + 0x30, 1);
  CHECK(!handoff.is_holstered(handoff.ctx));
  holster = handoff.resume_request(handoff.ctx, NULL);
  CHECK(strcmp(holster.status(holster.ctx), "refused") == 0);
  put_i32(GUEST, ITEM_RECORD + 0x30, 0);
  CHECK(handoff.is_holstered(handoff.ctx));
  /* Attacks: selected or continuing. */
  CHECK(!qa_compat_q2_native_mod_items_accepts_attack(items, actor));
  WS.selected = true;
  CHECK(qa_compat_q2_native_mod_items_accepts_attack(items, actor));
  WS.selected = false;
  put_i32(GUEST, ITEM_RECORD + 0x34, 1);
  CHECK(qa_compat_q2_native_mod_items_accepts_attack(items, actor));
  /* Checkpoint / restore. */
  qa_source_weapon_request latest = handoff.resume_request(handoff.ctx, "q2:gun");
  qa_compat_q2_native_item_checkpoint checkpoint = qa_compat_q2_native_mod_items_checkpoint(items);
  CHECK(checkpoint.next_request == latest.id); CHECK(checkpoint.request_count == 1);
  CHECK(checkpoint.requests[0].id == latest.id); CHECK(strcmp(checkpoint.requests[0].item, "q2:gun") == 0);
  qa_source_weapon_request restored = handoff.restore_request(handoff.ctx, latest.id, "q2:gun");
  CHECK(restored.id == latest.id);
  CHECK_ABORTS(handoff.restore_request(handoff.ctx, latest.id + 1, "q2:gun"));
  qa_compat_q2_native_mod_items_restore(items, &checkpoint);
  qa_compat_q2_native_item_checkpoint_free(&checkpoint);
  /* Cancellation identity. */
  qa_compat_q2_native_item_cancellation cancellation = qa_compat_q2_native_mod_items_cancellation(items, actor);
  CHECK(qa_compat_q2_native_item_cancellation_source_current(&cancellation));
  qa_compat_q2_native_thrown other = {&IO, "other"};
  CHECK(!qa_compat_q2_native_item_cancellation_accepts(&cancellation, &other));
  /* SourceItemRetired: a client record that moved retires the lease at the dispatcher. */
  int64_t moved = ITEM_RECORD + 0x400;
  memcpy(GUEST->bytes + moved, GUEST->bytes + ITEM_RECORD, 0x200);
  IO.record = moved;
  qa_guest_call_value moved_arg = ptr_value(moved);
  qa_compat_q2_native_thrown thrown;
  CHECK(!qa_compat_q2_native_try(retired_dispatch, &moved_arg, &thrown));
  CHECK(qa_compat_q2_native_item_cancellation_accepts(&cancellation, &thrown));
  CHECK(strcmp(thrown.message, QA_SOURCE_ITEM_RETIRED_MESSAGE) == 0);
  CHECK_ABORTS(retired_dispatch(&moved_arg));
  IO.record = ITEM_RECORD;
  GUEST->cpu.rsp = 0x8f000; GUEST->depth = 0;
  /* Retirement: the owner stops being live; the handoff refuses. */
  IO.live = false;
  CHECK(!WS.binding.current(WS.binding.ctx));
  CHECK(!qa_compat_q2_native_item_cancellation_source_current(&cancellation));
  CHECK_ABORTS(WS.binding.read(WS.binding.ctx));
  CHECK_ABORTS(handoff.resume_request(handoff.ctx, "q2:gun"));
  IO.live = true;
  qa_compat_q2_native_mod_items_release(items, actor);
  CHECK(WS.unbound == 1);
  CHECK(!WS.binding.current(WS.binding.ctx));
  qa_compat_q2_native_item_cancellation released = qa_compat_q2_native_mod_items_cancellation(items, actor);
  CHECK(!qa_compat_q2_native_item_cancellation_source_current(&released));
  qa_compat_q2_native_mod_items_destroy(items);
  qa_shared_inventory_table_destroy(inventory);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
  teardown();
}
int main(void) {
  test_read_classic_user_command();
  test_player();
  test_commands();
  test_inventory();
  test_drop();
  test_weapons();
  test_pickups();
  test_input_bridge();
  test_input_classic();
  test_input_rerelease_motion();
  test_mod_pickups();
  test_mod_items();
  if (failures) { fprintf(stderr, "%d/%d checks failed\n", failures, checks); return 1; }
  printf("all %d checks passed\n", checks);
  return 0;
}
