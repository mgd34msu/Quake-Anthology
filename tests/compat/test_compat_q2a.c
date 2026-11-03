/* Port of key TS cases for compat/q2 rerelease + classic modules.
 * GPL-2.0-or-later. */
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

#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/classic/printf.h"
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/client_profile.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/pickup_profile.h"
#include "qa/compat/q2/rerelease/player_state.h"
#include "qa/compat/q2/rerelease/spatial.h"
#include "qa/compat/q2/rerelease/world_profile.h"

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

static const char *RET = "sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd";

/* Minimal flat guest memory for the classic string helpers. */
typedef struct { uint8_t bytes[8192]; size_t next; } fake_memory_t;
static qa_guest_address fake_offset(void *ctx, qa_guest_address a, int64_t d) {
  (void)ctx; a.byte_offset += d; return a;
}
static qa_guest_address fake_allocate(void *ctx, double length, bool has_alignment, int64_t alignment, const char *label) {
  fake_memory_t *m = ctx; (void)has_alignment; (void)alignment; (void)label;
  qa_guest_address a = {NULL, (int64_t)m->next};
  m->next += (size_t)length;
  return a;
}
static void fake_noop(void *ctx, qa_guest_address a, double n) { (void)ctx; (void)a; (void)n; }
static void fake_check(void *ctx, qa_guest_address a, double n, const char *access) { (void)ctx; (void)a; (void)n; (void)access; }
static uint8_t *fake_borrow(void *ctx, qa_guest_address a, double n) {
  fake_memory_t *m = ctx; (void)n; return m->bytes + a.byte_offset;
}
static void fake_copy(void *ctx, qa_guest_address a, uint8_t *dest, double n) {
  fake_memory_t *m = ctx; memcpy(dest, m->bytes + a.byte_offset, (size_t)n);
}
static void fake_write(void *ctx, qa_guest_address a, const uint8_t *bytes, size_t n) {
  fake_memory_t *m = ctx; memcpy(m->bytes + a.byte_offset, bytes, n);
}
static qa_compat_q2_memory fake_memory(fake_memory_t *state) {
  qa_compat_q2_memory m; memset(&m, 0, sizeof m);
  m.ctx = state; m.pointer_bytes = 8;
  m.offset = fake_offset; m.allocate = fake_allocate; m.unmap = fake_noop;
  m.check = fake_check; m.borrow = fake_borrow; m.copy = fake_copy; m.write = fake_write;
  return m;
}

static void test_layouts(void) {
  qa_compat_q2_rerelease_layouts_ensure();
  /* tests/compat/q2/rerelease/native.test.ts layout byte lengths. */
  CHECK(qa_compat_q2_rerelease_get_game_import_layout()->byte_length == 576);
  CHECK(qa_compat_q2_rerelease_get_game_export_layout()->byte_length == 272);
  CHECK(qa_compat_q2_rerelease_get_cgame_import_layout()->byte_length == 296);
  CHECK(qa_compat_q2_rerelease_get_cgame_export_layout()->byte_length == 152);
  CHECK(qa_compat_q2_rerelease_trace_layout.byte_length == 96);
  CHECK(qa_compat_q2_rerelease_cvar_layout.byte_length == 56);
  CHECK(qa_compat_q2_rerelease_pmove_state_layout.byte_length == 52);
  CHECK(qa_compat_q2_rerelease_usercmd_layout.byte_length == 28);
  CHECK(qa_compat_q2_rerelease_pmove_layout.byte_length == 3320);
  CHECK(qa_compat_q2_rerelease_entity_state_layout.byte_length == 120);
  CHECK(qa_compat_q2_rerelease_player_state_layout.byte_length == 296);
  CHECK(qa_compat_q2_rerelease_client_layout.byte_length == 300);
  CHECK(qa_compat_q2_rerelease_field_offset(qa_compat_q2_rerelease_get_game_export_layout(), "edict_size") == 168);
  CHECK(qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_player_state_layout, "stats") == 166);
  CHECK(qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "sv.health") !=
        qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "s.origin"));
  size_t game_imports = 0, game_exports = 0;
  const qa_compat_q2_rerelease_api_entry *imports = qa_compat_q2_rerelease_get_game_imports(&game_imports);
  const qa_compat_q2_rerelease_api_entry *exports = qa_compat_q2_rerelease_get_game_exports(&game_exports);
  CHECK(game_imports > 0 && game_exports > 0);
  CHECK(qa_compat_q2_rerelease_find_entry(exports, game_exports, "Init") != NULL);
  CHECK(qa_compat_q2_rerelease_find_entry(imports, game_imports, "NoSuchImport") == NULL);
}

static void test_profiles(void) {
  const qa_compat_q2_rerelease_primary_world_profile *retail = qa_compat_q2_rerelease_get_retail_world_profile();
  CHECK(retail != NULL);
  CHECK(qa_compat_q2_rerelease_get_primary_world_profile(RET) == retail);
  CHECK(qa_compat_q2_rerelease_get_primary_world_profile("sha256:unknown") == NULL);
  qa_compat_q2_validate_rerelease_primary_world_profile(retail, RET);
  CHECK_ABORTS(qa_compat_q2_validate_rerelease_primary_world_profile(retail, "sha256:unknown"));
  const qa_compat_q2_rerelease_client_profile *source = qa_compat_q2_rerelease_get_source_client_profile();
  const qa_compat_q2_rerelease_client_profile *client = qa_compat_q2_rerelease_get_retail_client_profile();
  CHECK(source->inventory_count == 82 && source->ammo_count == 12 && !source->authority_is_artifact);
  CHECK(client->inventory_count == 84 && client->ammo_count == 12 && client->authority_is_artifact);
  CHECK(client->layout.byte_length == 7344);
  CHECK(qa_compat_q2_rerelease_field_offset(&client->layout, "pers.inventory") == 2688);
  const qa_compat_q2_native_pickup_profile *pickup = qa_compat_q2_rerelease_get_pickup_profile(RET);
  CHECK(pickup && pickup->touch == 0x67be0 && pickup->items_count == 84 && pickup->grant_count == 5);
  CHECK(qa_compat_q2_rerelease_get_pickup_profile("sha256:unknown") == NULL);
}

static void test_player_state(void) {
  uint8_t view[64]; memset(view, 0xcc, sizeof view);
  qa_q2_rerelease_movement_state move; memset(&move, 0, sizeof move);
  move.type = 3; move.origin = (qa_vec3){1.5, -2.25, 30}; move.velocity = (qa_vec3){0, 100, -50.5};
  move.flags = 0x8001; move.time_milliseconds = 65000; move.gravity = -800;
  move.delta_angles = (qa_vec3){90, 45, 0}; move.view_height = -12;
  qa_compat_q2_write_rerelease_movement_state(view, move);
  qa_q2_rerelease_movement_state back = qa_compat_q2_read_rerelease_movement_state(view);
  CHECK(back.type == 3 && back.flags == 0x8001 && back.time_milliseconds == 65000);
  CHECK(back.gravity == -800 && back.view_height == -12);
  CHECK(back.origin.x == 1.5 && back.origin.y == -2.25 && back.velocity.z == -50.5);
  CHECK(view[34] == 0xcc && view[35] == 0xcc); /* gravity padding untouched */
  uint8_t cmd[28]; memset(cmd, 0, sizeof cmd);
  qa_q2_rerelease_user_command command; memset(&command, 0, sizeof command);
  command.milliseconds = 300; command.buttons = 5; command.angles = (qa_vec3){10, 20, 30};
  command.forward_move = 400.5; command.side_move = -200.25; command.server_frame = 4294967295.0;
  qa_compat_q2_write_rerelease_user_command(cmd, command);
  qa_q2_rerelease_user_command read = qa_compat_q2_read_rerelease_user_command(cmd);
  CHECK(read.milliseconds == 44); /* uint8 wrap, TS setUint8 */
  CHECK(read.buttons == 5 && read.forward_move == 400.5 && read.side_move == -200.25);
  CHECK(read.server_frame == 4294967295.0);
}

static void test_spatial(void) {
  qa_body_state state; memset(&state, 0, sizeof state);
  state.origin = (qa_vec3){10, 20, 30};
  state.bounds.min = (qa_vec3){-16, -16, -24};
  state.bounds.max = (qa_vec3){16, 16, 32};
  qa_bounds linked = qa_compat_q2_rerelease_link_bounds(state, 2);
  CHECK(linked.min.x == -7 && linked.min.z == 5 && linked.max.x == 27 && linked.max.z == 63);
  state.angles = (qa_vec3){0, 90, 0};
  linked = qa_compat_q2_rerelease_link_bounds(state, 3);
  CHECK(linked.min.x == 10 - 32 - 1 && linked.max.z == 30 + 32 + 1); /* rotated extent 32 */
  CHECK(qa_compat_q2_rerelease_network_solid(state.bounds, 3, 0) == 31);
  qa_bounds point = {{1, 1, 1}, {1, 1, 1}};
  CHECK(qa_compat_q2_rerelease_network_solid(point, 2, 0) == 0);
  /* packed: maxz+32=64<<24 | -minz=24<<16 | maxy=16<<8 | maxx=16 */
  CHECK(qa_compat_q2_rerelease_network_solid(state.bounds, 2, 0) == ((64.0 * 16777216) + (24 * 65536) + (16 * 256) + 16));
  CHECK(qa_compat_q2_rerelease_network_solid(state.bounds, 2, 2) == 0); /* server flag 2 */
}

static void test_classic_layout(void) {
  CHECK(QA_COMPAT_Q2_CLASSIC_EDICT_BYTES == 260 && QA_COMPAT_Q2_CLASSIC_PLAYER_STATE_BYTES == 184);
  CHECK(QA_COMPAT_Q2_CLASSIC_FRAME_SECONDS == 0.1);
  CHECK(qa_compat_q2_classic_find_export("Init") != NULL);
  CHECK(qa_compat_q2_classic_find_export("NoSuchExport") == NULL);
  CHECK(qa_compat_q2_classic_import_count > 0 && qa_compat_q2_classic_export_count > 0);
}

static qa_guest_call_value int_value(double v) {
  qa_guest_call_value out; memset(&out, 0, sizeof out);
  out.kind = QA_GUEST_VALUE_INT32; out.number_value = v; return out;
}
static qa_guest_call_value double_value(double v) {
  qa_guest_call_value out; memset(&out, 0, sizeof out);
  out.kind = QA_GUEST_VALUE_FLOAT64; out.number_value = v; return out;
}
static void test_classic_printf(void) {
  fake_memory_t state; memset(&state, 0, sizeof state);
  qa_compat_q2_memory memory = fake_memory(&state);
  /* tests/compat/q2/classic/host.test.ts printf cases. */
  CHECK(qa_compat_q2_classic_printf_layouts("%s %*.*f %i %%", NULL, 0) == 5);
  qa_guest_call_value args1[] = {int_value(-3), double_value(1.5)};
  char *text = qa_compat_q2_classic_printf(&memory, "%04i %+.2f %%", args1, 2);
  CHECK(text && strcmp(text, "-003 +1.50 %") == 0);
  free(text);
  qa_guest_call_value args2[] = {int_value(42), int_value(0), double_value(-0.0)};
  text = qa_compat_q2_classic_printf(&memory, "%#08x %.0i %f", args2, 3);
  CHECK(text && strcmp(text, "0x00002a  -0.000000") == 0);
  free(text);
  /* After the drift %I64d is a 64-bit conversion (TS printf.ts size prefixes), not an error. */
  { qa_guest_value_layout layout[1]; CHECK(qa_compat_q2_classic_printf_layouts("%I64d", layout, 1) == 1 && strcmp(layout[0].storage, "int64") == 0); }
  CHECK_ABORTS(free(qa_compat_q2_classic_printf(&memory, "%i", NULL, 0)));
  qa_guest_call_value wrong[] = {double_value(1)};
  CHECK_ABORTS(free(qa_compat_q2_classic_printf(&memory, "%i", wrong, 1)));
}

static void test_classic_strings(void) {
  fake_memory_t state; memset(&state, 0, sizeof state);
  qa_compat_q2_memory memory = fake_memory(&state);
  qa_guest_address address = qa_compat_q2_allocate_classic_string(&memory, "quake");
  char *text = qa_compat_q2_read_classic_string(&memory, true, address, 64);
  CHECK(text && strcmp(text, "quake") == 0);
  free(text);
  CHECK_ABORTS(qa_compat_q2_write_classic_string(&memory, address, "toolong", 4));
  memset(state.bytes, 'x', sizeof state.bytes);
  qa_guest_address raw = {NULL, 100};
  CHECK_ABORTS(free(qa_compat_q2_read_classic_string(&memory, true, raw, 8)));
}

static bool project_none(void *ctx, qa_raw_entity_view view, qa_compat_q2_rerelease_guest_module *module, qa_owned_actor *out) {
  (void)ctx; (void)view; (void)module; (void)out; return false;
}
static void world_text_none(void *ctx, qa_compat_q2_rerelease_world_text_event event) { (void)ctx; (void)event; }
static void test_host_validation(void) {
  qa_compat_q2_rerelease_q2_host_options options; memset(&options, 0, sizeof options);
  options.has_pickups = true;
  options.semantics.project = project_none;
  CHECK_ABORTS((void)qa_compat_q2_rerelease_q2_guest_host_create(&options));
  memset(&options, 0, sizeof options);
  options.debug_drawing_headless = true;
  options.world_text = world_text_none;
  CHECK_ABORTS((void)qa_compat_q2_rerelease_q2_guest_host_create(&options));
}

int main(void) {
  test_layouts();
  test_profiles();
  test_player_state();
  test_spatial();
  test_classic_layout();
  test_classic_printf();
  test_classic_strings();
  test_host_validation();
  printf("test_compat_q2a: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
