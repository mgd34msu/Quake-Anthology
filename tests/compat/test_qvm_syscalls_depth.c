/* C checks for tests/compat/qvm: common-game, client-common, server-game,
 * ui-key, client-mark, client-state, client-audio, client-browser,
 * client-cinematic, client-render, guest-game-data, plus TS-module oracle
 * values captured under bun for math, snap-vector and vector traps.
 * Every leaf checks a sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
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

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/bot_library_syscalls.h"
#include "qa/compat/qvm/client_audio_syscalls.h"
#include "qa/compat/qvm/client_browser_syscalls.h"
#include "qa/compat/qvm/client_cinematic_syscalls.h"
#include "qa/compat/qvm/client_collision_syscalls.h"
#include "qa/compat/qvm/client_mark_syscalls.h"
#include "qa/compat/qvm/client_render_syscalls.h"
#include "qa/compat/qvm/client_state_syscalls.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/legacy_bot_syscalls.h"
#include "qa/compat/qvm/math_syscalls.h"
#include "qa/compat/qvm/memory_syscalls.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/compat/qvm/snap_vector_syscalls.h"
#include "qa/compat/qvm/ui_key_syscalls.h"
#include "qa/compat/qvm/vector_syscalls.h"

/* TS Q3GameMemory view of an interpreted allocation (compat/qvm/game_memory.h). */
#define QA_GAME(m) ((const qa_qvm_game_memory[]){qa_qvm_memory_game_memory(m)})

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)
#define CHECK_ABORTS(...)                                             \
  do {                                                                \
    checks++;                                                         \
    pid_t pid = fork();                                               \
    if (pid == 0) {                                                   \
      (void)prctl(PR_SET_DUMPABLE, 0);                                \
      (void)!freopen("/dev/null", "w", stderr);                       \
      __VA_ARGS__;                                                    \
      _exit(0);                                                       \
    }                                                                 \
    int status = 0;                                                   \
    waitpid(pid, &status, 0);                                         \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {      \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                 \
  } while (0)

#define CHECK_STR_EQ(a, b) CHECK((a) != NULL && strcmp((a), (b)) == 0)
static int sentinel_value;
#define SENTINEL ((void *)&sentinel_value)
#define LEAF() CHECK(ctx == SENTINEL)

static uint8_t guest_bytes[16384];
static qa_qvm_memory guest;
static uint8_t word_bytes[64];

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = 0;
  for (int i = 0; i < 4; i++) bits |= (uint32_t)bytes[offset + (size_t)i] << (8 * i);
  return (int32_t)bits;
}
static void put_f32(uint8_t *bytes, size_t offset, float value) {
  uint32_t bits;
  memcpy(&bits, &value, 4);
  put_i32(bytes, offset, (int32_t)bits);
}
static float get_f32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)get_i32(bytes, offset);
  float value;
  memcpy(&value, &bits, 4);
  return value;
}
static void put_text(size_t offset, const char *text) {
  memcpy(guest_bytes + offset, text, strlen(text) + 1);
}

static void reset_guest(void) {
  memset(guest_bytes, 0, sizeof guest_bytes);
  qa_qvm_memory_init(&guest, guest_bytes, sizeof guest_bytes, NULL, NULL);
}

static qa_qvm_host_call make_call(const char *role, int32_t code, const int32_t *args, size_t count) {
  memset(word_bytes, 0, sizeof word_bytes);
  put_i32(word_bytes, 0, code);
  for (size_t i = 0; i < count; i++) put_i32(word_bytes, (i + 1) * 4, args[i]);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = role;
  call.code = code;
  call.guest = &guest;
  call.base.words = word_bytes;
  call.base.words_length = sizeof word_bytes;
  call.base.guest = &guest;
  call.base.memory = guest_bytes;
  call.base.memory_length = sizeof guest_bytes;
  return call;
}
#define CALL(role, code, ...)                                                       \
  make_call(role, code, (const int32_t[]){__VA_ARGS__},                             \
            sizeof((const int32_t[]){__VA_ARGS__}) / sizeof(int32_t))
#define CALL0(role, code) make_call(role, code, NULL, 0)

/* ---- oracle values: bun run over the TS modules ---- */

static void test_math_snap_vector(void) {
  reset_guest();
  uint8_t w[32];
  struct { int32_t trap; float a, b; int32_t expected; } cases[] = {
      {103, 0.7f, 0, 1059384179},  {104, 0.7f, 0, 1061407923}, {105, 1.5f, -2.25f, 1076063749},
      {106, 2, 0, 1068827891},     {107, -1.5f, 0, -1073741824}, {108, -1.5f, 0, -1082130432},
      {111, -1, 0, 1078530011},    {111, 0.25f, 0, 1068021767},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    memset(w, 0, sizeof w);
    put_i32(w, 0, cases[i].trap);
    put_f32(w, 4, cases[i].a);
    put_f32(w, 8, cases[i].b);
    int32_t out = 0;
    CHECK(qa_qvm_math_syscall("cgame", w, sizeof w, &out) && out == cases[i].expected);
  }
  /* Role ownership: null where another service owns the trap. */
  memset(w, 0, sizeof w);
  put_i32(w, 0, 107);
  int32_t out = 0;
  CHECK(!qa_qvm_math_syscall("game", w, sizeof w, &out));
  put_i32(w, 0, 111);
  CHECK(!qa_qvm_math_syscall("ui", w, sizeof w, &out));
  put_i32(w, 0, 110);
  CHECK(!qa_qvm_math_syscall("cgame", w, sizeof w, &out));

  put_f32(guest_bytes, 256, 0.5f);
  put_f32(guest_bytes, 260, 1.5f);
  put_f32(guest_bytes, 264, -0.5f);
  memset(w, 0, sizeof w);
  put_i32(w, 0, 42);
  put_i32(w, 4, 256);
  CHECK(qa_qvm_snap_vector_syscall("game", w, sizeof w, QA_GAME(&guest), &out) && out == 0);
  CHECK(get_f32(guest_bytes, 256) == 0 && get_f32(guest_bytes, 260) == 2);
  /* Negative zero normalizes: the stored word is +0. */
  CHECK(get_i32(guest_bytes, 264) == 0);
  CHECK(!qa_qvm_snap_vector_syscall("ui", w, sizeof w, QA_GAME(&guest), &out));
  CHECK(!qa_qvm_snap_vector_syscall("cgame", w, sizeof w, QA_GAME(&guest), &out));
  put_f32(guest_bytes, 256, NAN);
  put_f32(guest_bytes, 260, 3e10f);
  CHECK(qa_qvm_snap_vector_syscall("game", w, sizeof w, QA_GAME(&guest), &out));
  CHECK(get_f32(guest_bytes, 256) == -2147483648.0f && get_f32(guest_bytes, 260) == -2147483648.0f);

  put_f32(guest_bytes, 512, 30);
  put_f32(guest_bytes, 516, 45);
  put_f32(guest_bytes, 520, 10);
  memset(w, 0, sizeof w);
  put_i32(w, 0, 108);
  put_i32(w, 4, 512);
  put_i32(w, 8, 600);
  put_i32(w, 12, 612);
  put_i32(w, 16, 624);
  CHECK(qa_qvm_vector_syscall("game", w, sizeof w, QA_GAME(&guest), &out));
  const int32_t angle_words[9] = {1058849904, 1058849904, -1090519040, 1059229034, -1086194576,
                                  -1105592783, 1055990520, 1046924740, 1062884759};
  for (int i = 0; i < 9; i++) CHECK(get_i32(guest_bytes, 600 + (size_t)i * 4) == angle_words[i]);
  put_f32(guest_bytes, 700, 0);
  put_f32(guest_bytes, 704, 0);
  put_f32(guest_bytes, 708, 1);
  memset(w, 0, sizeof w);
  put_i32(w, 0, 109);
  put_i32(w, 4, 720);
  put_i32(w, 8, 700);
  CHECK(qa_qvm_vector_syscall("game", w, sizeof w, QA_GAME(&guest), &out));
  CHECK(get_i32(guest_bytes, 720) == 1065353216 && get_i32(guest_bytes, 724) == 0);
  /* Zero projection denominator rejects. */
  put_f32(guest_bytes, 708, 0);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_vector_syscall("game", w, sizeof w, QA_GAME(&guest), &o));
  CHECK(!qa_qvm_vector_syscall("cgame", w, sizeof w, QA_GAME(&guest), &out));
}

static void test_memory_traps(void) {
  reset_guest();
  uint8_t w[32];
  int32_t out = -1;
  memset(w, 0, sizeof w);
  put_i32(w, 0, 100);
  put_i32(w, 4, 64);
  put_i32(w, 8, 0x1aa);
  put_i32(w, 12, 4);
  CHECK(qa_qvm_memory_syscall("cgame", w, sizeof w, QA_GAME(&guest), &out) && out == 0);
  CHECK(guest_bytes[64] == 0xaa && guest_bytes[67] == 0xaa && guest_bytes[68] == 0);
  /* strncpy returns the signed destination word and pads with zeros. */
  put_text(128, "ab");
  memset(guest_bytes + 256, 0xcc, 8);
  put_i32(w, 0, 102);
  put_i32(w, 4, 256);
  put_i32(w, 8, 128);
  put_i32(w, 12, 5);
  CHECK(qa_qvm_memory_syscall("ui", w, sizeof w, QA_GAME(&guest), &out) && out == 256);
  CHECK(memcmp(guest_bytes + 256, "ab\0\0\0", 5) == 0 && guest_bytes[261] == 0xcc);
  /* Overlapping copies behave as memmove (TS memory-syscalls.ts after the audit). */
  put_i32(w, 0, 101);
  put_i32(w, 4, 128);
  put_i32(w, 8, 130);
  put_i32(w, 12, 8);
  for (int i = 0; i < 10; i++) guest_bytes[128 + i] = (uint8_t)(i + 1);
  CHECK(qa_qvm_memory_syscall("game", w, sizeof w, QA_GAME(&guest), &out) && out == 0);
  CHECK(guest_bytes[128] == 3 && guest_bytes[135] == 10 && guest_bytes[136] == 9 && guest_bytes[137] == 10);
  put_i32(w, 0, 99);
  CHECK(!qa_qvm_memory_syscall("game", w, sizeof w, QA_GAME(&guest), &out));
}

/* ---- bot library helpers: staged goal rounding, stringContains, legacy flags. The
 * dispatch over the real BotLibrary is checked in tests/compat/test_qvm_bot_syscalls.c. ---- */

static void test_bot_library(void) {
  /* add3(sub3()) rounds each step: 16777216+15 rounds to 16777232. */
  qa_qvmx_bot_goal goal;
  memset(&goal, 0, sizeof goal);
  goal.mins.x = -16777216;
  goal.maxs.x = 16777216;
  qa_vec3 origin = {16777232, 0, 0};
  CHECK(qa_qvm_touching_goal(origin, goal));
  origin.x = 16777234;
  CHECK(!qa_qvm_touching_goal(origin, goal));

  CHECK(qa_qvm_string_contains("Hello", "LL", false) == 2);
  CHECK(qa_qvm_string_contains("Hello", "LL", true) == -1);
  CHECK(qa_qvm_string_contains("Hello", "", true) == 0);
  CHECK(qa_qvm_string_contains(NULL, "a", true) == -1);
  CHECK(qa_qvm_legacy_bot_action_flags(0x10 | 0x80 | 2) == (8 | 16 | 2));
}

/* ---- audio: the content Q3ClientSound over a real Q3PresentationSoundBank ---- */

static int audio_prints, audio_starts, audio_clears, audio_locals, audio_loads;
static bool audio_kill_all;
static bool audio_has_origin;
static qa_sound_asset audio_asset = {NULL, "resource:x", "sound/x.wav", {22050, 1, NULL, false, 0, 0, -1}};
static const qa_sound_asset *audio_load_sync(void *ctx, const char *path, bool compressed) {
  LEAF();
  CHECK(strcmp(path, "sound/x.wav") == 0 && compressed);
  audio_loads++;
  return &audio_asset;
}
static void audio_print(void *ctx, const char *text) {
  LEAF();
  CHECK(strcmp(text, "^3") == 0);
  audio_prints++;
}
static void audio_start(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *pcm) {
  LEAF();
  CHECK(channel == 2 || channel == 1);
  CHECK(pcm == &audio_asset.pcm);
  audio_has_origin = origin != NULL;
  if (origin != NULL) CHECK(origin->x == 12.5 && entity >= -1);
  audio_starts++;
}
static void audio_local(void *ctx, qa_pcm_sound *pcm, double channel) {
  LEAF();
  CHECK(pcm == &audio_asset.pcm && channel == 6);
  audio_locals++;
}
static void audio_clear(void *ctx, bool kill_all) {
  LEAF();
  audio_kill_all = kill_all;
  audio_clears++;
}
static qa_vec3 audio_listener_origin;
static qa_axis audio_listener_axis;
static void audio_listener(void *ctx, double client, qa_vec3 origin, qa_axis axis) {
  LEAF();
  CHECK(client == 1);
  audio_listener_origin = origin;
  audio_listener_axis = axis;
}
static char audio_intro[32], audio_loop[32];
static bool audio_track(void *ctx, const char *intro, const char *loop) {
  LEAF();
  snprintf(audio_intro, sizeof audio_intro, "%s", intro);
  snprintf(audio_loop, sizeof audio_loop, "%s", loop);
  return true;
}

static void test_audio(void) {
  reset_guest();
  qa_q3_pres_presentation_sound_bank *bank =
      qa_q3_pres_presentation_sound_bank_create(NULL, NULL, audio_load_sync, SENTINEL);
  CHECK(bank != NULL);
  /* The source presentation registers the shared sound first (handle 1). */
  qa_pcm_sound *shared = NULL;
  CHECK(qa_q3_pres_presentation_sound_bank_sound(bank, "sound/x.wav", true, &shared) && shared == &audio_asset.pcm);
  CHECK(audio_loads == 1);
  qa_qvm_client_audio_services services;
  memset(&services, 0, sizeof services);
  services.role = "cgame";
  services.ctx = SENTINEL;
  services.print = audio_print;
  services.sound.bank = bank;
  services.sound.ctx = SENTINEL;
  services.sound.start_sound = audio_start;
  services.sound.start_local_sound = audio_local;
  services.sound.clear_looping_sounds = audio_clear;
  services.sound.set_listener = audio_listener;
  services.sound.start_background_track = audio_track;
  put_text(128, "sound/x.wav");
  put_text(160, "*shared");
  qa_qvm_host_result out = -1;

  /* Guest handles belong to the same bank the source presentation uses. */
  qa_qvm_host_call call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_REGISTERSOUND, 128, 1);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 1 && audio_loads == 1);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_S_REGISTERSOUND, 128, 0);
  services.role = "ui";
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 1);
  services.role = "cgame";
  /* Empty and '*' names register nothing. */
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_REGISTERSOUND, 160, 0);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 0);
  put_text(192, "");
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_REGISTERSOUND, 192, 0);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 0);
  /* A null name dereferences NULL at strlen (TS Error). */
  qa_qvm_host_call bad = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_REGISTERSOUND, 0, 0);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_audio_syscall(&bad, services, &o));

  /* Invalid handles print the bare color format and start nothing; handle 0 is a null PCM. */
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_STARTSOUND, 0, 1, 2, 99);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 0);
  CHECK(audio_prints == 1 && audio_starts == 0);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_STARTLOCALSOUND, 1, 6);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && audio_locals == 1);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_STARTSOUND, 0, 1, 2, 1);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && audio_starts == 1 && !audio_has_origin);
  put_f32(guest_bytes, 512, 12.5f);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_STARTSOUND, 512, 1, 2, 1);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && audio_starts == 2 && audio_has_origin);
  bad = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_STARTSOUND, 0, -1, 1, 1);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_audio_syscall(&bad, services, &o));
  /* With an origin the entity is not validated. */
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_STARTSOUND, 512, -1, 1, 1);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && audio_starts == 3);
  CHECK(audio_prints == 1);

  /* Legacy clear always kills; modern reads the word. */
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_CLEARLOOPINGSOUNDS, 0);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && !audio_kill_all);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_CLEARLOOPINGSOUNDS, 0);
  call.has_abi_profile = true;
  call.abi_profile = "q3-1.16n-base";
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && audio_kill_all && audio_clears == 2);

  put_f32(guest_bytes, 512, 12.5f);
  put_f32(guest_bytes, 516, -3.25f);
  put_f32(guest_bytes, 520, 8);
  for (int i = 0; i < 9; i++) put_f32(guest_bytes, 600 + (size_t)i * 4, (float)i);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_RESPATIALIZE, 1, 512, 600, 0);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 0);
  CHECK(audio_listener_origin.x == 12.5 && audio_listener_origin.y == -3.25);
  CHECK(audio_listener_axis.vectors[1].y == 4 && audio_listener_axis.vectors[2].z == 8);
  bad = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_RESPATIALIZE, 1, 512, 16380, 0);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_audio_syscall(&bad, services, &o));

  services.role = "ui";
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_S_STARTBACKGROUNDTRACK, 128, 0);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && out == 0);
  CHECK(strcmp(audio_intro, "sound/x.wav") == 0 && audio_loop[0] == '\0');
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_S_STOPBACKGROUNDTRACK, 0);
  CHECK(qa_qvm_client_audio_syscall(&call, services, &out) && audio_intro[0] == '\0');
  /* A cgame-only trap under the UI role is not owned. */
  call = CALL("ui", QA_QVM_CGAME_IMPORT_CG_S_RESPATIALIZE, 1, 512, 600, 0);
  CHECK(!qa_qvm_client_audio_syscall(&call, services, &out));
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_S_CLEARLOOPINGSOUNDS, 0);
  CHECK(!qa_qvm_client_audio_syscall(&call, services, &out));
  qa_q3_pres_presentation_sound_bank_destroy(bank);
}

/* ---- browser ---- */

static int browser_address_calls, browser_info_calls;
static double browser_count_source;
static double browser_count(void *ctx, double source) {
  LEAF();
  browser_count_source = source;
  return 2;
}
static void browser_address(void *ctx, double source, double index, double size, qa_qvm_browser_write write) {
  LEAF();
  (void)source;
  (void)index;
  (void)size;
  browser_address_calls++;
  write.call(write.ctx, "127.0.0.1:27960");
}
static void browser_info(void *ctx, double source, double index, double size, qa_qvm_browser_write write) {
  LEAF();
  (void)source;
  (void)index;
  (void)size;
  browser_info_calls++;
  write.call(write.ctx, "\\hostname\\Live");
}
static qa_qvmx_browser_ping browser_ping(void *ctx, double index, double size, qa_qvm_browser_write write) {
  LEAF();
  (void)size;
  qa_qvmx_browser_ping ping = {NULL, 41};
  write.call(write.ctx, index == 0 ? NULL : "10.0.0.1");
  return ping;
}
static const char *browser_ping_info(void *ctx, double index, double size, qa_qvm_browser_write write) {
  LEAF();
  (void)size;
  if (index != 0) return NULL;
  write.call(write.ctx, "info");
  return "info";
}
static double browser_add(void *ctx, double source, qa_qvm_browser_string_source name,
                          qa_qvm_browser_string_source address) {
  LEAF();
  if (source == 99) return -1; /* capacity guard: neither string is read */
  char *text = name.call(name.ctx);
  CHECK(strlen(text) == 31);
  free(text);
  text = address.call(address.ctx);
  CHECK(strcmp(text, "1.2.3.4") == 0);
  free(text);
  return 1;
}
static bool browser_status_had_size;
static const char *browser_status(void *ctx, const char *address, bool has_size, double size,
                                  qa_qvm_browser_write write) {
  LEAF();
  (void)size;
  browser_status_had_size = has_size;
  if (address == NULL) return NULL;
  if (has_size) write.call(write.ctx, "status");
  return "status";
}

static void test_browser(void) {
  reset_guest();
  qa_qvmx_browser_view view;
  memset(&view, 0, sizeof view);
  view.ctx = SENTINEL;
  view.get_server_count = browser_count;
  view.get_server_address_string = browser_address;
  view.get_server_info = browser_info;
  view.get_ping = browser_ping;
  view.source_ping_info = browser_ping_info;
  view.add_server = browser_add;
  view.server_status = browser_status;
  qa_qvm_host_result out = -9;

  qa_qvm_host_call call = make_call("ui", 46, NULL, 0);
  call.kind = "extension";
  call.has_abi_profile = true;
  call.abi_profile = "q3-1.16n-base";
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 2 && browser_count_source == 0);
  call.code = 48;
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && browser_count_source == 2);
  call = CALL("ui", 49, 0, 160, 64);
  call.kind = "extension";
  call.has_abi_profile = true;
  call.abi_profile = "q3-1.16n-base";
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 0);
  CHECK(strcmp((char *)guest_bytes + 160, "127.0.0.1:27960") == 0);
  /* Extension trap without the legacy profile is not owned. */
  call.has_abi_profile = false;
  CHECK(!qa_qvm_client_browser_syscall(&call, view, &out));
  call = CALL("cgame", 65, 0);
  CHECK(!qa_qvm_client_browser_syscall(&call, view, &out));

  /* Out-of-range records clear one byte and never reach the owner. */
  guest_bytes[4095] = 0xaa;
  browser_address_calls = 0;
  call = CALL("ui", 66, 99, 4096, 4095, -1);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 0);
  CHECK(guest_bytes[4095] == 0 && browser_address_calls == 0);
  call = CALL("ui", 66, 2, 4096, 160, 64);
  memset(guest_bytes + 160, 0xaa, 4);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out));
  CHECK(guest_bytes[160] == 0 && browser_address_calls == 0);
  call = CALL("ui", 66, 2, 4095, 160, 64);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && browser_address_calls == 1);
  /* Info trap with a null destination returns before anything else. */
  browser_info_calls = 0;
  call = CALL("ui", 67, 3, 0, 0, -1);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 0 && browser_info_calls == 0);
  call = CALL("ui", 67, 3, 0, 256, 64);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && browser_info_calls == 1);
  CHECK(strcmp((char *)guest_bytes + 256, "\\hostname\\Live") == 0);
  /* Address string with a null destination faults in the guest view. */
  qa_qvm_host_call bad = CALL("ui", 66, 99, 0, 0, 0);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_browser_syscall(&bad, view, &o));

  memset(guest_bytes + 160, 0xaa, 8);
  call = CALL("ui", 48, 0, 160, 64, 240);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 0);
  CHECK(guest_bytes[160] == 0 && guest_bytes[161] == 0xaa && get_i32(guest_bytes, 240) == 41);
  call = CALL("ui", 48, 1, 160, 64, 240);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out));
  CHECK(strcmp((char *)guest_bytes + 160, "10.0.0.1") == 0);
  /* Null ping info: capacity 0 leaves the byte alone, nonzero clears. */
  guest_bytes[300] = 0xaa;
  call = CALL("ui", 49, 1, 300, 0);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && guest_bytes[300] == 0xaa);
  call = CALL("ui", 49, 1, 300, 8);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && guest_bytes[300] == 0);

  /* AddServer: lazily read pointers, 31 byte name bound. */
  memset(guest_bytes + 512, 'n', 64);
  guest_bytes[576] = 0;
  put_text(600, "1.2.3.4");
  call = CALL("ui", 73, 1, 512, 600);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 1);
  call = CALL("ui", 73, 99, 0, 0);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == -1);
  bad = CALL("ui", 73, 3, 0, 600);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_browser_syscall(&bad, view, &o));

  put_text(700, "host");
  call = CALL("ui", 82, 700, 800, 64);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 1 && browser_status_had_size);
  CHECK(strcmp((char *)guest_bytes + 800, "status") == 0);
  call = CALL("ui", 82, 700, 0, -1);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && !browser_status_had_size);
  call = CALL("ui", 82, 0, 0, 0);
  CHECK(qa_qvm_client_browser_syscall(&call, view, &out) && out == 0);
  call = CALL("ui", 86, 0);
  CHECK(!qa_qvm_client_browser_syscall(&call, view, &out));
}

/* ---- cinematic ---- */

static int cine_dev_prints, cine_draws;
static qa_rect cine_rect;
static double cine_play(void *ctx, const char *path, qa_rect rect, double bits) {
  LEAF();
  CHECK(strcmp(path, "test.roq") == 0 && bits == 4);
  cine_rect = rect;
  return 3;
}
static double cine_run(void *ctx, double handle) { LEAF(); return handle + 10; }
static double cine_stop(void *ctx, double handle) { LEAF(); return handle + 20; }
static qa_draw2d cine_draw2d;
static void cine_draw(void *ctx, double handle, const qa_draw2d *draw) {
  LEAF();
  CHECK(handle == 3 && draw == &cine_draw2d && draw->commands.ctx == SENTINEL);
  cine_draws++;
}
static void cine_extents(void *ctx, double handle, qa_rect rect) {
  LEAF();
  CHECK(handle == 3);
  cine_rect = rect;
}
static void cine_print(void *ctx, const char *text) {
  LEAF();
  CHECK(strcmp(text, "UI_CIN_PlayCinematic\n") == 0);
  cine_dev_prints++;
}

static void test_cinematic(void) {
  reset_guest();
  qa_qvm_client_cinematic_services services;
  memset(&services, 0, sizeof services);
  services.cinematics.ctx = SENTINEL;
  services.cinematics.play_guest = cine_play;
  services.cinematics.run_guest = cine_run;
  services.cinematics.stop_guest = cine_stop;
  services.cinematics.draw_guest = cine_draw;
  services.cinematics.set_extents = cine_extents;
  memset(&cine_draw2d, 0, sizeof cine_draw2d);
  cine_draw2d.commands.ctx = SENTINEL;
  services.draw = &cine_draw2d;
  services.ctx = SENTINEL;
  services.developer_print = cine_print;
  put_text(128, "test.roq");
  qa_qvm_host_result out = -9;

  qa_qvm_host_call call = CALL("cgame", 74, 128, 3, 4, 50, 60, 4);
  CHECK(qa_qvm_client_cinematic_syscall(&call, services, &out) && out == 3 && cine_dev_prints == 0);
  CHECK(cine_rect.x == 3 && cine_rect.y == 4 && cine_rect.width == 50 && cine_rect.height == 60);
  call = CALL("ui", 75, 128, 3, 4, 50, 60, 4);
  CHECK(qa_qvm_client_cinematic_syscall(&call, services, &out) && cine_dev_prints == 1);
  call = CALL("cgame", 75, 1);
  CHECK(qa_qvm_client_cinematic_syscall(&call, services, &out) && out == 21);
  call = CALL("cgame", 76, 1);
  CHECK(qa_qvm_client_cinematic_syscall(&call, services, &out) && out == 11);
  call = CALL("cgame", 77, 3);
  CHECK(qa_qvm_client_cinematic_syscall(&call, services, &out) && out == 0 && cine_draws == 1);
  call = CALL("cgame", 78, 3, 7, 8, 9, 10);
  CHECK(qa_qvm_client_cinematic_syscall(&call, services, &out) && cine_rect.height == 10);
  call = CALL("cgame", 79, 3);
  CHECK(!qa_qvm_client_cinematic_syscall(&call, services, &out));
  call = CALL("ui", 74, 3);
  CHECK(!qa_qvm_client_cinematic_syscall(&call, services, &out));
  call = CALL("qagame", 75, 3);
  CHECK(!qa_qvm_client_cinematic_syscall(&call, services, &out));
}

/* ---- collision: map load ownership (clip-model queries are exercised
 * through the world collision tests) ---- */

static int collision_models_calls;
static char collision_map[64];
static qa_qvm_client_clip_models collision_models(void *ctx) {
  LEAF();
  collision_models_calls++;
  qa_qvm_client_clip_models models;
  memset(&models, 0, sizeof models);
  return models;
}
static void collision_load(void *ctx, const char *name) {
  LEAF();
  snprintf(collision_map, sizeof collision_map, "%s", name);
}

static void test_collision_ownership(void) {
  reset_guest();
  qa_qvm_client_collision_services services;
  memset(&services, 0, sizeof services);
  services.ctx = SENTINEL;
  services.models = collision_models;
  services.load_map = collision_load;
  put_text(64, "maps/q3dm1.bsp");
  qa_qvm_host_result out = -1;
  qa_qvm_host_call call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_LOADMAP, 64);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == 0);
  CHECK(strcmp(collision_map, "maps/q3dm1.bsp") == 0 && collision_models_calls == 0);
  call = CALL0("cgame", QA_QVM_CGAME_IMPORT_CG_CM_LOADMODEL);
  CHECK(!qa_qvm_client_collision_syscall(&call, services, &out));
  call = CALL0("ui", 19);
  CHECK(!qa_qvm_client_collision_syscall(&call, services, &out) && collision_models_calls == 0);
}

/* ---- mark ---- */

static int mark_mode;
static double mark_project(void *ctx, qa_q3_pres_source_mark_projection query) {
  LEAF();
  CHECK(query.point_count == 3 && query.max_points == 4 && query.max_fragments == 2);
  if (mark_mode == 0) return 0; /* nothing reached: no pointer is dereferenced */
  qa_vec3 projection = query.read_projection(query.ctx);
  CHECK(projection.z == -1);
  qa_vec3 point = query.read_point(query.ctx, 1);
  CHECK(point.x == 9);
  qa_q3_pres_mark_fragment fragment = {2, 3};
  query.write_fragment(query.ctx, 0, fragment);
  qa_vec3 values[2] = {{1, 2, 3}, {4, 5, 6}};
  query.write_points(query.ctx, mark_mode == 2 ? 2000 : 1, values, 2);
  return 1;
}

static void test_mark(void) {
  reset_guest();
  qa_qvm_mark_projector projector = {SENTINEL, mark_project};
  qa_qvm_host_result out = -1;
  /* Unreached null outputs are never touched. */
  mark_mode = 0;
  qa_qvm_host_call call =
      CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS, 3, 0, 0, 4, 0, 2, 0);
  CHECK(qa_qvm_client_mark_syscall(&call, projector, &out) && out == 0);

  put_f32(guest_bytes, 128, -1);
  put_f32(guest_bytes, 136, -1);
  put_f32(guest_bytes, 512 + 12, 9);
  put_i32(guest_bytes, 1808, 0x12345678);
  mark_mode = 1;
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS, 3, 512, 128, 4, 1024, 2, 1800);
  CHECK(qa_qvm_client_mark_syscall(&call, projector, &out) && out == 1);
  CHECK(get_i32(guest_bytes, 1800) == 2 && get_i32(guest_bytes, 1804) == 3);
  CHECK(get_i32(guest_bytes, 1808) == 0x12345678);
  CHECK(get_f32(guest_bytes, 1024 + 12) == 1 && get_f32(guest_bytes, 1024 + 32) == 6);
  /* Fragments store first; a null output-points base then rejects. */
  qa_qvm_host_call bad = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS, 3, 512, 128, 4, 0, 2, 1800);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_mark_syscall(&bad, projector, &o));
  mark_mode = 2;
  bad = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS, 3, 512, 128, 4, 1024, 2, 1800);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_mark_syscall(&bad, projector, &o));
  call = CALL0("ui", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS);
  CHECK(!qa_qvm_client_mark_syscall(&call, projector, &out));

  /* The content BspMarkProjector behind the Pick: an empty BSP reads the
   * projection and every input point, then projects nothing. */
  qa_q3_pres_mark_geometry geometry;
  memset(&geometry, 0, sizeof geometry);
  qa_q3_pres_bsp_mark_projector bsp;
  qa_q3_pres_bsp_mark_projector_init(&bsp, geometry);
  qa_qvm_mark_projector real = qa_qvm_bsp_mark_projector(&bsp);
  CHECK(real.ctx == &bsp);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS, 3, 512, 128, 4, 1024, 2, 1800);
  CHECK(qa_qvm_client_mark_syscall(&call, real, &out) && out == 0);
  bad = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS, 3, 512, 0, 4, 1024, 2, 1800);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_mark_syscall(&bad, real, &o));
}

/* ---- render: the content Q3RendererResources and the text Draw2D ---- */

static const qa_material_picture render_picture_value = {"vm/picture", 9, NULL, false};
static const qa_material_picture render_zero_picture = {"*white", 0, NULL, false};
static const qa_model_tag render_tag_rows[3][1] = {
    {{"tag_head", {0, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}}},
    {{"tag_head", {4, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}}},
    {{"tag_head", {6, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}}}};
static const qa_model_tag *const render_tag_row_ptrs[3] = {render_tag_rows[0], render_tag_rows[1], render_tag_rows[2]};
static const size_t render_tag_counts[3] = {1, 1, 1};
static const qa_q3_mesh_frame render_frames[3] = {
    {"f0", {{-1, -2, -3}, {1, 2, 3}}, {0, 0, 0}, 4}, {"f1", {{0, 0, 0}, {0, 0, 0}}, {0, 0, 0}, 0},
    {"f2", {{0, 0, 0}, {0, 0, 0}}, {0, 0, 0}, 0}};
static const qa_q3_mesh_model render_mesh = {"x", render_frames, 3, render_tag_row_ptrs, render_tag_counts, 3, NULL, 0};
static const qa_q3_pres_scene_skin render_skin = {"skins/x.skin", NULL, 0};
static int render_shader_requests, render_model_requests, render_world_loads, render_clears;
static int render_polys, render_lights, render_entities, render_scenes, render_stretches;
static bool render_light_additive, render_color_null;
static qa_vec4 render_color;
static qa_q3_pres_admitted_ref_entity render_entity;
static qa_q3_pres_refdef render_refdef;
static char render_remap[64];

static bool render_host_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  LEAF();
  CHECK(strcmp(path, "models/x.md3") == 0);
  render_model_requests++;
  memset(out, 0, sizeof *out);
  out->kind = QA_Q3_PRES_SCENE_MODEL_LOADED;
  out->path = "models/x.md3";
  out->model.kind = QA_DECODED_MODEL_Q3_MESH;
  out->model.as.q3_mesh = &render_mesh;
  out->resource.id = "resource:x.md3";
  return true;
}
static bool render_host_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) {
  LEAF();
  *out = strcmp(path, "skins/x.skin") == 0 ? &render_skin : NULL;
  return true;
}
static bool render_host_shader(void *ctx, const char *path, bool mip, const qa_material_picture **out) {
  LEAF();
  (void)mip;
  render_shader_requests++;
  *out = strcmp(path, "vm/picture") == 0 ? &render_picture_value : NULL;
  return true;
}
static bool render_host_world(void *ctx, const char *path, qa_q3_pres_world_scene *out) {
  LEAF();
  CHECK(strcmp(path, "maps/x.bsp") == 0);
  memset(out, 0, sizeof *out);
  render_world_loads++;
  return true;
}
static void render_host_remap(void *ctx, const char *original, const char *replacement, const char *offset) {
  LEAF();
  snprintf(render_remap, sizeof render_remap, "%s>%s@%s", original, replacement, offset);
}
static void render_scene_clear(void *ctx) { LEAF(); render_clears++; }
static void render_scene_entity(void *ctx, const qa_q3_pres_admitted_ref_entity *entity) {
  LEAF();
  render_entity = *entity;
  render_entities++;
}
static void render_scene_poly(void *ctx, const qa_q3_pres_ref_poly *poly) {
  LEAF();
  CHECK(poly->shader != NULL && strcmp(poly->shader->name, "vm/picture") == 0 && poly->vertex_count == 3);
  CHECK(poly->vertices[1].position.y == 7 && poly->vertices[2].color.w == 255);
  render_polys++;
}
static void render_scene_light(void *ctx, qa_q3_pres_dynamic_light light) {
  LEAF();
  CHECK(light.radius == 100 && light.origin.y == 2 && light.color.z == 0.25);
  render_light_additive = light.additive;
  render_lights++;
}
static void render_scene_render(void *ctx, const qa_q3_pres_refdef *refdef) {
  LEAF();
  render_refdef = *refdef;
  render_scenes++;
}
static void render_set_color(void *ctx, const qa_vec4 *color) {
  LEAF();
  render_color_null = color == NULL;
  if (color != NULL) render_color = *color;
}
static void render_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv, const qa_picture_asset *picture) {
  LEAF();
  CHECK(rect.x == 1.5 && rect.height == 4 && uv.s == 0.5 && uv.t2 == 1);
  CHECK(picture->kind == QA_PICTURE_MATERIAL && strcmp(picture->material.name, "vm/picture") == 0);
  render_stretches++;
}
static void put_ref_entity(size_t at, int32_t type, int32_t model, int32_t skin, int32_t shader) {
  memset(guest_bytes + at, 0, QA_QVM_REF_ENTITY_BYTES);
  put_i32(guest_bytes, at, type);
  put_i32(guest_bytes, at + 4, 8);
  put_i32(guest_bytes, at + 8, model);
  put_f32(guest_bytes, at + 68, 11);
  put_i32(guest_bytes, at + 80, 2);
  put_i32(guest_bytes, at + 108, skin);
  put_i32(guest_bytes, at + 112, shader);
  guest_bytes[at + 119] = 200;
  put_f32(guest_bytes, at + 132, 16);
  put_f32(guest_bytes, at + 136, 45);
}

static void test_render(void) {
  reset_guest();
  qa_q3_pres_resource_host host;
  memset(&host, 0, sizeof host);
  host.scene.ctx = SENTINEL;
  host.scene.clear_scene = render_scene_clear;
  host.scene.add_ref_entity = render_scene_entity;
  host.scene.add_poly = render_scene_poly;
  host.scene.add_light = render_scene_light;
  host.scene.render_scene = render_scene_render;
  host.zero_picture = render_zero_picture;
  host.ctx = SENTINEL;
  host.model = render_host_model;
  host.skin = render_host_skin;
  host.shader = render_host_shader;
  host.world = render_host_world;
  host.remap_shader = render_host_remap;
  qa_q3_pres_q3_renderer_resources *resources = NULL;
  CHECK(qa_q3_pres_q3_renderer_resources_init(&resources, host, NULL, QA_Q3_PRES_HANDLES_CLIENT));
  qa_draw2d draw;
  memset(&draw, 0, sizeof draw);
  draw.commands.ctx = SENTINEL;
  draw.commands.set_color = render_set_color;
  draw.commands.stretch_pixels = render_stretch;
  draw.space = QA_COORD_PIXELS;
  qa_qvm_host_result out = -1;
  put_text(512, "vm/picture");
  put_text(544, "models/x.md3");
  put_text(576, "skins/x.skin");

  /* Resource identities are the shared owner's client handles. */
  qa_qvm_host_call call = CALL("cgame", 57, 512);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 1 && render_shader_requests == 1);
  call = CALL("ui", 20, 512);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 1 && render_shader_requests == 1);
  call = CALL("cgame", 39, 512);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 1);
  /* UI's shader-with-mips slot does not exist: ui 39 is not a resource trap. */
  call = CALL("ui", 39, 512);
  CHECK(!qa_qvm_client_render_syscall(&call, resources, &draw, &out));
  call = CALL("cgame", 37, 544);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 1 && render_model_requests == 1);
  call = CALL("ui", 18, 0);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0 && render_model_requests == 1);
  call = CALL("cgame", 38, 576);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 1);

  call = CALL("cgame", 45, 0);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_color_null);
  put_f32(guest_bytes, 640, 0.25f);
  put_f32(guest_bytes, 652, 1);
  call = CALL("cgame", 45, 640);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out));
  CHECK(!render_color_null && render_color.x == 0.25 && render_color.w == 1);

  /* Draw2D resolves the picture thunk through the shared shader handle. */
  call = CALL0("cgame", 46);
  put_f32(word_bytes, 4, 1.5f);
  put_f32(word_bytes, 16, 4);
  put_f32(word_bytes, 20, 0.5f);
  put_f32(word_bytes, 32, 1);
  put_i32(word_bytes, 36, 1);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0 && render_stretches == 1);

  call = CALL("cgame", 47, 1, 1024, 1040);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0);
  CHECK(get_f32(guest_bytes, 1028) == -2 && get_f32(guest_bytes, 1048) == 3);

  /* The default model has no tag storage: the name pointer is never consumed. */
  call = CALL("cgame", 48, 2048, 0, 1, 2, 0, 99999);
  put_f32(word_bytes, 20, 0.5f);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0);
  CHECK(get_f32(guest_bytes, 2048 + 12) == 1 && get_f32(guest_bytes, 2048) == 0);
  /* UI discards the result; cgame returns it. */
  put_text(600, "tag_head");
  call = CALL("cgame", 48, 2048, 1, 1, 2, 0, 600);
  put_f32(word_bytes, 20, 0.5f);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 1);
  CHECK(get_f32(guest_bytes, 2048) == 5 && get_f32(guest_bytes, 2048 + 12) == 1);
  call = CALL("ui", 29, 2048, 1, 1, 2, 0, 600);
  put_f32(word_bytes, 20, 0.5f);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0);
  put_text(620, "tag_none");
  call = CALL("cgame", 48, 2048, 1, 1, 2, 0, 620);
  put_f32(word_bytes, 20, 0.5f);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0);
  CHECK(get_f32(guest_bytes, 2048) == 0 && get_f32(guest_bytes, 2048 + 12) == 1);

  put_text(700, "a");
  put_text(704, "b");
  put_text(708, "1.5");
  call = CALL("cgame", 79, 700, 704, 708);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && strcmp(render_remap, "a>b@1.5") == 0);
  call = CALL("cgame", 79, 700, 704, 0);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && strcmp(render_remap, "a>b@") == 0);

  /* Polys: skips on zero shader or counts; 87 submits a batch. */
  for (int v = 0; v < 6; v++) {
    put_f32(guest_bytes, 4096 + (size_t)v * 24 + 4, 7);
    guest_bytes[4096 + (size_t)v * 24 + 23] = 255;
  }
  call = CALL("cgame", 42, 0, 3, 4096);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_polys == 0);
  call = CALL("cgame", 42, 1, 0, 4096);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_polys == 0);
  call = CALL("cgame", 42, 1, 3, 4096);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_polys == 1);
  call = CALL("cgame", 87, 1, 3, 4096, 2);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_polys == 3);
  qa_qvm_host_call bad = CALL("cgame", 87, 1, 3, 16380, 2);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_render_syscall(&bad, resources, &draw, &o));
  /* Unregistered shader handles are rejected by the resource owner. */
  bad = CALL("cgame", 42, 5, 3, 4096);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_render_syscall(&bad, resources, &draw, &o));
  /* 87 is cgame only. */
  call = CALL("ui", 87, 1, 3, 4096, 2);
  CHECK(!qa_qvm_client_render_syscall(&call, resources, &draw, &out));

  put_f32(guest_bytes, 3000 + 4, 2);
  for (int code = 0; code < 2; code++) {
    call = CALL("cgame", code == 0 ? 43 : 85, 3000);
    put_f32(word_bytes, 8, 100);
    put_f32(word_bytes, 20, 0.25f);
    CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out));
    CHECK(render_light_additive == (code == 1));
  }
  CHECK(render_lights == 2);
  call = CALL("cgame", 43, 0);
  put_f32(word_bytes, 8, -1);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_lights == 2);

  /* refEntity_t admission: model handles resolve; poly records pass through. */
  put_ref_entity(6000, 0, 1, 1, 1);
  call = CALL("cgame", 41, 6000);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_entities == 1);
  CHECK(!render_entity.is_poly_record && render_entity.entity.kind == QA_Q3_PRES_REF_MODEL);
  CHECK(render_entity.entity.as.model.model.kind == QA_Q3_PRES_SCENE_MODEL_LOADED);
  CHECK_STR_EQ(render_entity.entity.as.model.model.path, "models/x.md3");
  CHECK(render_entity.entity.as.model.custom_skin == &render_skin);
  CHECK(render_entity.entity.as.model.shading.custom_shader != NULL &&
        strcmp(render_entity.entity.as.model.shading.custom_shader->name, "vm/picture") == 0);
  CHECK(render_entity.entity.as.model.shading.render_flags == 8 && render_entity.entity.as.model.frame == 2);
  CHECK(render_entity.entity.as.model.origin.x == 11 && render_entity.entity.as.model.shading.shader_rgba.w == 200);
  put_ref_entity(6000, 1, 77, 77, 77);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_entity.is_poly_record);
  CHECK(render_entity.record.kind == QA_Q3_PRES_REF_POLY && render_entity.record.model.is_handle &&
        render_entity.record.model.handle == 77 && render_entity.record.custom_shader.handle == 77);
  put_ref_entity(6000, 2, 77, 77, 0);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out));
  CHECK(render_entity.entity.kind == QA_Q3_PRES_REF_SPRITE && render_entity.entity.as.sprite.shading.custom_shader == NULL);
  CHECK(render_entity.entity.as.sprite.radius == 16 && render_entity.entity.as.sprite.rotation == 45);
  put_ref_entity(6000, 7, 77, 77, 77);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out));
  CHECK(render_entity.entity.kind == QA_Q3_PRES_REF_PORTAL && render_entity.entity.as.portal.frame == 2);
  put_ref_entity(6000, 8, 0, 0, 0);
  bad = CALL("cgame", 41, 6000);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_client_render_syscall(&bad, resources, &draw, &o));

  /* refdef_t keeps the complete 32-byte text rows. */
  memset(guest_bytes + 7000, 0, QA_QVM_REFDEF_BYTES);
  put_i32(guest_bytes, 7000, 3);
  put_i32(guest_bytes, 7000 + 12, 480);
  put_f32(guest_bytes, 7000 + 16, 90);
  put_i32(guest_bytes, 7000 + 72, 1234);
  guest_bytes[7000 + 80 + 31] = 0xff;
  memcpy(guest_bytes + 7000 + 112, "hi\0x", 4);
  call = CALL("cgame", 44, 7000);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_scenes == 1);
  CHECK(render_refdef.x == 3 && render_refdef.height == 480 && render_refdef.fov_x == 90 && render_refdef.time == 1234);
  CHECK(render_refdef.area_mask_length == 32 && render_refdef.area_mask[31] == 0xff);
  CHECK(render_refdef.text.rows[0].length == 32 && render_refdef.text.rows[0].bytes[3] == 'x');
  call = CALL0("cgame", 40);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_clears == 1);

  put_text(800, "maps/x.bsp");
  call = CALL("cgame", 36, 800);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && render_world_loads == 1);
  call = CALL("cgame", 86, 900, 64);
  CHECK(qa_qvm_client_render_syscall(&call, resources, &draw, &out) && out == 0);

  call = CALL("qagame", 43, 0);
  CHECK(!qa_qvm_client_render_syscall(&call, resources, &draw, &out));
  /* UI has no 36/86/88 world traps. */
  call = CALL("ui", 36, 512);
  CHECK(!qa_qvm_client_render_syscall(&call, resources, &draw, &out));
  qa_q3_pres_q3_renderer_resources_destroy(resources);
}

/* ---- client state ---- */

static const char *state_config(void *ctx, double index) {
  LEAF();
  return index == 7 ? "value" : NULL;
}
static double state_current_number(void *ctx) { LEAF(); return 1; }
static bool state_read_command(void *ctx, double number, qa_qvmx_wire_user_command *out) {
  LEAF();
  if (number != 1) return false;
  memset(out, 0, sizeof *out);
  out->server_time = 123;
  out->angles[0] = 65537;
  out->rightmove = -127;
  out->weapon = 8;
  return true;
}
static double state_time_number;
static bool state_override_used;
static bool state_override(void *ctx, double number, qa_qvmx_wire_user_command *out) {
  LEAF();
  state_override_used = true;
  return state_read_command(ctx, number, out);
}
static qa_qvmx_snapshot_current state_snapshot_current(void *ctx) {
  LEAF();
  qa_qvmx_snapshot_current current = {1, 200};
  return current;
}
static bool state_snapshot_read(void *ctx, double number, qa_qvmx_snapshot *out) {
  LEAF();
  (void)number;
  (void)out;
  return false;
}
static int state_commands_run;
static bool state_server_command(void *ctx, double number, const char *const **tokens, size_t *count) {
  LEAF();
  (void)tokens;
  (void)count;
  state_commands_run++;
  return number == 7;
}
static double state_weapon, state_sensitivity;
static void state_set_value(void *ctx, double weapon, double sensitivity) {
  LEAF();
  state_weapon = weapon;
  state_sensitivity = sensitivity;
}

static void test_client_state(void) {
  reset_guest();
  qa_qvm_client_state_services services;
  memset(&services, 0, sizeof services);
  services.ctx = SENTINEL;
  services.connection.game_state.ctx = SENTINEL;
  services.connection.game_state.get = state_config;
  services.connection.commands.ctx = SENTINEL;
  services.connection.commands.get_current_number = state_current_number;
  services.connection.commands.read = state_read_command;
  services.snapshots.ctx = SENTINEL;
  services.snapshots.current = state_snapshot_current;
  services.snapshots.read = state_snapshot_read;
  services.get_server_command = state_server_command;
  services.set_user_command_value = state_set_value;
  qa_qvm_host_result out = -1;

  /* UI GETCONFIGSTRING: out of range 0, null clears, present writes. */
  qa_qvm_host_call call = CALL("ui", QA_QVM_UI_IMPORT_UI_GETCONFIGSTRING, 1024, 512, 16);
  guest_bytes[512] = 0xaa;
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0 && guest_bytes[512] == 0xaa);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_GETCONFIGSTRING, 3, 512, 0);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0 && guest_bytes[512] == 0xaa);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_GETCONFIGSTRING, 3, 512, 16);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0 && guest_bytes[512] == 0);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_GETCONFIGSTRING, 7, 512, 16);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 1);
  CHECK(strcmp((char *)guest_bytes + 512, "value") == 0);

  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETCURRENTSNAPSHOTNUMBER, 600, 604);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0);
  CHECK(get_i32(guest_bytes, 600) == 1 && get_i32(guest_bytes, 604) == 200);
  /* A snapshot outside retention yields 0 without touching the record. */
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETSNAPSHOT, 1, 16380);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0);

  call = CALL0("cgame", QA_QVM_CGAME_IMPORT_CG_GETCURRENTCMDNUMBER);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 1);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETUSERCMD, 1, 1024);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 1);
  CHECK(get_i32(guest_bytes, 1024) == 123 && get_i32(guest_bytes, 1028) == 65537);
  CHECK((int8_t)guest_bytes[1024 + 22] == -127 && guest_bytes[1024 + 20] == 8);
  /* Absent: 0 before the destination is viewed. */
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETUSERCMD, -63, 16380);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0);
  services.user_command = state_override;
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETUSERCMD, 1, 1024);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && state_override_used);
  services.user_command = NULL;

  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETSERVERCOMMAND, 7);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 1 && state_commands_run == 1);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_GETSERVERCOMMAND, 8);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0);

  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_SETUSERCMDVALUE, 9);
  put_f32(word_bytes, 8, 0.375f);
  CHECK(qa_qvm_client_state_syscall(&call, services, &out) && out == 0);
  CHECK(state_weapon == 9 && state_sensitivity == 0.375);
  call = CALL("qagame", QA_QVM_CGAME_IMPORT_CG_GETCURRENTCMDNUMBER, 0);
  CHECK(!qa_qvm_client_state_syscall(&call, services, &out));
  (void)state_time_number;
}

/* ---- common + cvar over the real registry ---- */

static double common_milliseconds(void *ctx) { LEAF(); return 4294968530.0; }
static const char *const common_argv[] = {"say", "hello", "world"};
static int common_args_calls;
static const char *const *common_arguments(void *ctx, size_t *count) {
  LEAF();
  common_args_calls++;
  *count = 3;
  return common_argv;
}
static char common_printed[64];
static void common_print(void *ctx, const char *text) {
  LEAF();
  snprintf(common_printed, sizeof common_printed, "%s", text);
}
static char common_log[8][32];
static int common_log_count;
static void common_note(const char *prefix, const char *text) {
  if (common_log_count < 8)
    snprintf(common_log[common_log_count++], 32, "%s:%s", prefix, text == NULL ? "(null)" : text);
}
static void common_execute(void *ctx, const char *text) { LEAF(); common_note("now", text); }
static void common_insert(void *ctx, const char *text) { LEAF(); common_note("insert", text); }
static void common_append(void *ctx, const char *text) { LEAF(); common_note("append", text); }
static void common_register(void *ctx, const char *text) { LEAF(); common_note("register", text); }
static void common_remove(void *ctx, const char *text) { LEAF(); common_note("remove", text); }
static void common_reliable(void *ctx, const char *text) { LEAF(); common_note("reliable", text); }
static double common_real_time(void *ctx, bool has_output, qa_qvm_calendar *calendar) {
  LEAF();
  CHECK(has_output == (calendar != NULL));
  if (has_output) {
    qa_qvm_calendar value = {1, 2, 3, 4, 5, 126, 6, 42, 1};
    *calendar = value;
  }
  return 12345;
}

static void test_common(void) {
  reset_guest();
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  CHECK(cvars != NULL);
  qa_qvm_common_services services;
  memset(&services, 0, sizeof services);
  services.role = "qagame";
  services.cvars = cvars;
  services.ctx = SENTINEL;
  services.print = common_print;
  services.milliseconds = common_milliseconds;
  services.arguments = common_arguments;
  services.real_time = common_real_time;
  services.commands.ctx = SENTINEL;
  services.commands.execute_now = common_execute;
  services.commands.insert = common_insert;
  services.commands.append = common_append;
  services.commands.register_command = common_register;
  services.commands.remove_command = common_remove;
  services.commands.reliable = common_reliable;
  qa_qvm_host_result out = -1;
  put_text(128, "g_speed");
  put_text(160, "320");
  put_text(256, "echo hi");

  qa_qvm_host_call call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_REGISTER, 1024, 128, 160, 0);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 0);
  CHECK(get_i32(guest_bytes, 1024 + 12) == 320);
  CHECK(strcmp((char *)guest_bytes + 1040, "320") == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_VARIABLE_INTEGER_VALUE, 128);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 320);
  /* Null value pointer resets; a null VM record registers only. */
  put_text(192, "400");
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_SET, 128, 192);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_UPDATE, 1024);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && get_i32(guest_bytes, 1024 + 12) == 400);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_SET, 128, 0);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_VARIABLE_INTEGER_VALUE, 128);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 320);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_REGISTER, 0, 128, 160, 0);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 0);
  /* Missing names ignore the capacity. */
  put_text(576, "missing");
  guest_bytes[608] = 0xaa;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_VARIABLE_STRING_BUFFER, 576, 608, 0);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && guest_bytes[608] == 0);
  /* Over-long values reject after the modification count publishes. */
  char *long_value = malloc(300);
  memset(long_value, 'x', 299);
  long_value[299] = 0;
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, "g_speed", long_value, true, &snapshot)) qa_cvar_read_free(&snapshot);
  free(long_value);
  qa_qvm_host_call bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_CVAR_UPDATE, 1024);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_common_syscall(&bad, services, &o));

  /* Milliseconds wrap to int32. */
  call = CALL0("qagame", QA_QVM_GAME_IMPORT_G_MILLISECONDS);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 1234);
  /* Scoped arguments win over the service. */
  const char *const scoped[] = {"one"};
  common_args_calls = 0;
  call = CALL0("qagame", QA_QVM_GAME_IMPORT_G_ARGC);
  call.has_command_arguments = true;
  call.command_arguments = scoped;
  call.command_argument_count = 1;
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 1 && common_args_calls == 0);
  call = CALL0("qagame", QA_QVM_GAME_IMPORT_G_ARGC);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 3 && common_args_calls == 1);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_ARGV, 1, 512, 32);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  CHECK(strcmp((char *)guest_bytes + 512, "hello") == 0);
  for (int index = 9; index >= -1; index -= 10) {
    call = CALL("qagame", QA_QVM_GAME_IMPORT_G_ARGV, index, 512, 32);
    CHECK(qa_qvm_common_syscall(&call, services, &out) && guest_bytes[512] == 0);
  }

  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_REAL_TIME, 0);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 12345);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_REAL_TIME, 700);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 12345);
  const int32_t calendar[9] = {1, 2, 3, 4, 5, 126, 6, 42, 1};
  for (int i = 0; i < 9; i++) CHECK(get_i32(guest_bytes, 700 + (size_t)i * 4) == calendar[i]);

  common_log_count = 0;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_CONSOLE_COMMAND, 0, 0);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && out == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_CONSOLE_COMMAND, 0, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_CONSOLE_COMMAND, 1, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_CONSOLE_COMMAND, 2, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  CHECK(common_log_count == 4 && strcmp(common_log[0], "now:(null)") == 0);
  CHECK(strcmp(common_log[1], "now:echo hi") == 0 && strcmp(common_log[2], "insert:echo hi") == 0);
  CHECK(strcmp(common_log[3], "append:echo hi") == 0);
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_CONSOLE_COMMAND, 3, 256);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_common_syscall(&bad, services, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_ERROR, 256);
  CHECK_ABORTS(qa_qvm_host_result o; (void)qa_qvm_common_syscall(&bad, services, &o));
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_PRINT, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && strcmp(common_printed, "echo hi") == 0);
  /* Role mismatch and foreign traps are unhandled. */
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_ARGC, 0);
  CHECK(!qa_qvm_common_syscall(&call, services, &out));
  call = CALL0("qagame", QA_QVM_GAME_IMPORT_G_LOCATE_GAME_DATA);
  CHECK(!qa_qvm_common_syscall(&call, services, &out));

  /* cgame command ownership and CG_ARGS. */
  services.role = "cgame";
  common_log_count = 0;
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_ARGS, 800, 64);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  CHECK(strcmp((char *)guest_bytes + 800, "hello world") == 0);
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_ADDCOMMAND, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_REMOVECOMMAND, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_SENDCLIENTCOMMAND, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("cgame", QA_QVM_CGAME_IMPORT_CG_SENDCONSOLECOMMAND, 256);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  CHECK(strcmp(common_log[0], "register:echo hi") == 0 && strcmp(common_log[1], "remove:echo hi") == 0);
  CHECK(strcmp(common_log[2], "reliable:echo hi") == 0 && strcmp(common_log[3], "append:echo hi") == 0);
  call = CALL0("cgame", QA_QVM_CGAME_IMPORT_CG_R_CLEARSCENE);
  CHECK(!qa_qvm_common_syscall(&call, services, &out));

  /* UI cvar numbering differs. */
  services.role = "ui";
  put_text(128, "g_speed");
  put_text(160, "250");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_CVAR_SET, 128, 160);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_CVAR_VARIABLEVALUE, 128);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && get_f32((uint8_t *)&out, 0) == 250.0f);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_CVAR_RESET, 128);
  CHECK(qa_qvm_common_syscall(&call, services, &out));
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_CVAR_VARIABLEVALUE, 128);
  CHECK(qa_qvm_common_syscall(&call, services, &out) && get_f32((uint8_t *)&out, 0) == 320.0f);
  qa_cvar_registry_destroy(cvars);
}

/* ---- server game ---- */

static char server_calls[16][64];
static int server_call_count;
static void server_note(const char *format, double a, const char *text) {
  if (server_call_count < 16)
    snprintf(server_calls[server_call_count++], 64, format, a, text == NULL ? "" : text);
}
static const char *server_get_userinfo(void *ctx, double slot) {
  LEAF();
  (void)slot;
  return "\\name\\player";
}
static void server_set_userinfo(void *ctx, double slot, const char *value) {
  LEAF();
  server_note("userinfo %.0f %s", slot, value);
}
static qa_qvmx_wire_user_command server_user_command(void *ctx, double slot) {
  LEAF();
  (void)slot;
  qa_qvmx_wire_user_command command;
  memset(&command, 0, sizeof command);
  command.server_time = 123;
  command.angles[0] = 32768;
  command.buttons = 0;
  command.weapon = 255;
  command.forwardmove = -127;
  command.upmove = -2;
  return command;
}
static void server_drop(void *ctx, double slot, const char *reason) {
  LEAF();
  server_note("drop %.0f %s", slot, reason);
}
static void server_command(void *ctx, double slot, const char *text) {
  LEAF();
  server_note("command %.0f %s", slot, text);
}
static const char *server_config_get(void *ctx, int32_t index) {
  LEAF();
  static char text[32];
  snprintf(text, sizeof text, "config %d", (int)index);
  return text;
}
static void server_config_set(void *ctx, int32_t index, const char *value) {
  LEAF();
  server_note("config %.0f %s", index, value);
}
static qa_qvm_server_trace_query server_trace_seen;
static qa_qvm_trace_record server_trace(void *ctx, const qa_qvm_server_trace_query *input) {
  LEAF();
  server_trace_seen = *input;
  qa_qvm_trace_record record;
  memset(&record, 0, sizeof record);
  record.result.fraction = 0.5;
  record.entity_num = 1;
  return record;
}
static void server_link(void *ctx, int32_t slot) {
  LEAF();
  server_note("link %.0f %s", slot, "");
}
static bool server_contact(void *ctx, qa_bounds bounds, int32_t slot, bool capsule) {
  LEAF();
  CHECK(bounds.min.x == 1 && bounds.max.x == 4 && slot == 1 && capsule);
  return true;
}
static bool server_connected(void *ctx, int32_t first, int32_t second) {
  LEAF();
  return first == second;
}
static void server_brush(void *ctx, int32_t slot, const char *name) {
  LEAF();
  server_note("brush %.0f %s", slot, name);
}

static qa_qvm_game_data server_data;

static void test_server_game(void) {
  reset_guest();
  qa_qvm_game_data_init(&server_data, QA_GAME(&guest), NULL);
  int entity_stride = qa_qvm_shared_entity_bytes("q3-modern");
  int client_stride = qa_qvm_player_state_bytes("q3-modern");
  qa_qvm_game_data_locate(&server_data, 4096, 2, entity_stride, 8192, client_stride);
  qa_qvm_server_spatial_operations spatial;
  memset(&spatial, 0, sizeof spatial);
  spatial.ctx = SENTINEL;
  spatial.trace = server_trace;
  spatial.link = server_link;
  spatial.entity_contact = server_contact;
  spatial.areas_connected = server_connected;
  spatial.set_brush_model = server_brush;
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  qa_qvm_server_game_services services;
  memset(&services, 0, sizeof services);
  services.data = &server_data;
  services.cvars = cvars;
  services.configstrings_ctx = SENTINEL;
  services.configstring_get = server_config_get;
  services.configstring_set = server_config_set;
  services.max_clients = 2;
  services.spatial = &spatial;
  services.ctx = SENTINEL;
  services.get_userinfo = server_get_userinfo;
  services.set_userinfo = server_set_userinfo;
  services.get_user_command = server_user_command;
  services.drop_client = server_drop;
  services.send_server_command = server_command;
  int32_t out = -1;

  /* Invalid clients return before the guest string is read. */
  qa_qvm_host_call call = CALL("qagame", QA_QVM_GAME_IMPORT_G_DROP_CLIENT, -1, 0);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_SERVER_COMMAND, 2, 0);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  CHECK(server_call_count == 0);
  put_text(128, "bye");
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SEND_SERVER_COMMAND, -1, 128);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp(server_calls[0], "command -1 bye") == 0);
  qa_qvm_host_call bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_USERINFO, -1, 512, 0);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_USERINFO, -1, 512, 32);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_CONFIGSTRING, 1024, 512, 32);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_CONFIGSTRING, 1, 512, 0);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_SERVERINFO, 0, 0);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_USERCMD, 2, 512);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));

  server_call_count = 0;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SET_CONFIGSTRING, 2, 0);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  CHECK(strcmp(server_calls[0], "config 2 ") == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SET_USERINFO, 1, 0);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp(server_calls[1], "userinfo 1 ") == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_USERINFO, 1, 512, 32);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp((char *)guest_bytes + 512, "\\name\\player") == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_CONFIGSTRING, 1, 512, 32);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp((char *)guest_bytes + 512, "config 1") == 0);

  guest_bytes[536] = 0xcc;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_USERCMD, 1, 512);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  CHECK(get_i32(guest_bytes, 512) == 123 && get_i32(guest_bytes, 516) == 32768);
  CHECK(guest_bytes[532] == 255 && (int8_t)guest_bytes[533] == -127 && (int8_t)guest_bytes[535] == -2);
  CHECK(guest_bytes[536] == 0xcc);

  put_f32(guest_bytes, 128, 1);
  put_f32(guest_bytes, 132, 2);
  put_f32(guest_bytes, 136, 3);
  put_f32(guest_bytes, 140, 4);
  put_f32(guest_bytes, 144, 5);
  put_f32(guest_bytes, 148, 6);
  guest_bytes[312] = 0xcc;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_TRACECAPSULE, 256, 128, 0, 0, 140, 7, 99);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 0);
  CHECK(strcmp(server_trace_seen.shape_kind, "capsule") == 0);
  CHECK(server_trace_seen.start.y == 2 && server_trace_seen.end.z == 6);
  CHECK(server_trace_seen.shape_bounds.min.x == 0 && server_trace_seen.shape_bounds.max.z == 0);
  CHECK(server_trace_seen.pass_entity_num == 7 && server_trace_seen.mask == 99);
  CHECK(get_f32(guest_bytes, 256 + 8) == 0.5f && get_i32(guest_bytes, 256 + 52) == 1);
  CHECK(guest_bytes[312] == 0xcc);

  /* Table pointers become slots; misaligned pointers reject. */
  server_call_count = 0;
  int32_t second = 4096 + entity_stride;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_LINKENTITY, second);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp(server_calls[0], "link 1 ") == 0);
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_LINKENTITY, second + 1);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_ENTITY_CONTACTCAPSULE, 128, 140, second);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 1);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_AREAS_CONNECTED, 3, 3);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out) && out == 1);

  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_SET_BRUSH_MODEL, second, 0);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  put_text(600, "models/x.md3");
  bad = CALL("qagame", QA_QVM_GAME_IMPORT_G_SET_BRUSH_MODEL, second, 600);
  CHECK_ABORTS(int32_t o; (void)qa_qvm_server_game_syscall(&bad, &services, &o));
  put_text(600, "*3");
  server_call_count = 0;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SET_BRUSH_MODEL, second, 600);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp(server_calls[0], "brush 1 *3") == 0);

  call = CALL0("qagame", QA_QVM_GAME_IMPORT_BOTLIB_SETUP);
  CHECK(!qa_qvm_server_game_syscall(&call, &services, &out));
  call = CALL0("cgame", QA_QVM_GAME_IMPORT_G_LINKENTITY);
  CHECK(!qa_qvm_server_game_syscall(&call, &services, &out));

  /* Legacy configstring indices remap on both directions. */
  services.data->abi_profile = "q3-1.16n-base";
  server_call_count = 0;
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_SET_CONFIGSTRING, 12, 0);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp(server_calls[0], "config 20 ") == 0);
  call = CALL("qagame", QA_QVM_GAME_IMPORT_G_GET_CONFIGSTRING, 12, 512, 32);
  CHECK(qa_qvm_server_game_syscall(&call, &services, &out));
  CHECK(strcmp((char *)guest_bytes + 512, "config 20") == 0);
  qa_qvm_game_data_destroy(&server_data);
  qa_cvar_registry_destroy(cvars);
}

/* ---- ui key ---- */

static int key_writes, key_asserts, key_invokes;
static void key_assert(void *ctx) { LEAF(); key_asserts++; }
static const char *key_directory(void *ctx) { LEAF(); return "missionpack"; }
static void key_write(void *ctx, int64_t unique, const char *directory, const uint8_t *source, size_t length) {
  LEAF();
  CHECK(unique == 1 && strcmp(directory, "missionpack") == 0 && length == 16);
  CHECK(source == guest_bytes + 96);
  key_writes++;
}
static int32_t key_invoke_async(void *ctx, const int32_t *args, size_t count, int64_t index,
                                qa_qvm_validate_fn validate, void *validate_ctx) {
  CHECK(ctx == SENTINEL && args[0] == QA_QVM_UI_EXPORT_UI_HASUNIQUECDKEY && count == 10 && index == 0);
  validate(validate_ctx);
  key_invokes++;
  return 1;
}

static void test_ui_key(void) {
  reset_guest();
  qa_qvm_ui_key_services services;
  memset(&services, 0, sizeof services);
  services.ctx = SENTINEL;
  services.write_ui = key_write;
  services.game_directory = key_directory;
  services.assert_current = key_assert;
  int32_t out = -1;
  qa_qvm_host_call call = CALL("ui", QA_QVM_UI_IMPORT_UI_SET_CDKEY, 96);
  call.base.call_ctx = SENTINEL;
  call.base.invoke_async = key_invoke_async;
  CHECK(qa_qvm_ui_key_syscall(&call, &services, &out) && out == 0);
  /* entry, nested validate, resume, after the write: as the TS. */
  CHECK(key_writes == 1 && key_invokes == 1 && key_asserts == 4);

  put_text(160, "2222");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_VERIFY_CDKEY, 160, 0);
  CHECK(qa_qvm_ui_key_syscall(&call, &services, &out) && out == 0);
  put_text(160, "22222222222a2222");
  bool expected = qa_validate_q3_cd_key("22222222222a2222", NULL);
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_VERIFY_CDKEY, 160, 0);
  CHECK(qa_qvm_ui_key_syscall(&call, &services, &out) && out == (expected ? 1 : 0));
  put_text(200, "21");
  expected = qa_validate_q3_cd_key("22222222222a2222", "21");
  call = CALL("ui", QA_QVM_UI_IMPORT_UI_VERIFY_CDKEY, 160, 200);
  CHECK(qa_qvm_ui_key_syscall(&call, &services, &out) && out == (expected ? 1 : 0));
  call = CALL("cgame", QA_QVM_UI_IMPORT_UI_SET_CDKEY, 96);
  CHECK(!qa_qvm_ui_key_syscall(&call, &services, &out));
  call = CALL0("ui", QA_QVM_UI_IMPORT_UI_PRINT);
  CHECK(!qa_qvm_ui_key_syscall(&call, &services, &out));
}

/* ---- game data ---- */

static void test_game_data(void) {
  reset_guest();
  qa_qvm_game_data data;
  qa_qvm_game_data_init(&data, QA_GAME(&guest), NULL);
  int entity = qa_qvm_shared_entity_bytes("q3-modern");
  int player = qa_qvm_player_state_bytes("q3-modern");
  /* Null, undersized, unaligned and over-capacity tables leave state alone. */
  qa_qvm_game_data_state before = qa_qvm_game_data_checkpoint(&data);
  CHECK(before.entities_word == 0 && before.clients_word == 0);
  CHECK_ABORTS(qa_qvm_game_data_locate(&data, 0, 1, entity, 8192, player));
  CHECK_ABORTS(qa_qvm_game_data_locate(&data, 4096, 1, entity - 4, 8192, player));
  CHECK_ABORTS(qa_qvm_game_data_locate(&data, 4098, 1, entity, 8192, player));
  CHECK_ABORTS(qa_qvm_game_data_locate(&data, 4096, 1025, entity, 8192, player));
  CHECK_ABORTS(qa_qvm_game_data_locate(&data, 4096, 1, entity + 2, 8192, player));
  CHECK_ABORTS(qa_qvm_game_data_player_ping(&data, 0));

  qa_qvm_game_data_locate(&data, 4096, 3, entity, 8192, player);
  CHECK(qa_qvm_game_data_get_num_entities(&data) == 3);
  double pointer = 4096 + entity;
  CHECK(qa_qvm_game_data_number_from_pointer(&data, pointer) == 1);
  CHECK(qa_qvm_game_data_entity_from_pointer(&data, pointer) == qa_qvm_game_data_entity(&data, 1));
  CHECK_ABORTS(qa_qvm_game_data_number_from_pointer(&data, 4096 + 4));
  CHECK_ABORTS(qa_qvm_game_data_number_from_pointer(&data, 0));
  for (int i = 0; i < 4; i++) {
    double slots[] = {-1, 0.5, 3, 1023};
    CHECK_ABORTS(qa_qvm_game_data_entity(&data, slots[i]));
  }
  /* Ping borrows only its field. */
  memset(guest_bytes + 8192, 0xa5, 4);
  memset(guest_bytes + 8192 + 448, 0xa5, 12);
  qa_qvm_game_data_set_player_ping(&data, 0, 87);
  CHECK(qa_qvm_game_data_player_ping(&data, 0) == 87);
  CHECK((uint32_t)get_i32(guest_bytes, 8192 + 448) == 0xa5a5a5a5u);
  CHECK((uint32_t)get_i32(guest_bytes, 8192 + 456) == 0xa5a5a5a5u);
  for (int i = 0; i < 4; i++) {
    double slots[] = {-1, 0.5, 64, 65};
    CHECK_ABORTS(qa_qvm_game_data_player_ping(&data, slots[i]));
  }
  qa_qvm_game_data_set_client_count(&data, 1);
  CHECK_ABORTS(qa_qvm_game_data_player_ping(&data, 1));
  CHECK_ABORTS(qa_qvm_game_data_set_client_count(&data, 65));
  CHECK_ABORTS(qa_qvm_game_data_set_client_count(&data, 0));

  /* Base offset zero round-trips through the allocation-length word. */
  qa_qvm_game_data_state state = qa_qvm_game_data_checkpoint(&data);
  CHECK(state.entities_word == 4096 && state.num_entities == 3);
  qa_qvm_game_data_clear(&data);
  CHECK(qa_qvm_game_data_get_num_entities(&data) == 0);
  qa_qvm_game_data_restore(&data, state);
  CHECK(qa_qvm_game_data_get_num_entities(&data) == 3);
  qa_qvm_game_data_state empty = {0, 0, 0, 0, 0};
  qa_qvm_game_data_restore(&data, empty);
  CHECK(qa_qvm_game_data_get_entity_stride_bytes(&data) == 0);

  /* Relocated strides keep adding borrows past the inline table; entity
   * identity stays stable (TS Map is unbounded). */
  qa_qvm_game_data_locate(&data, 4096, 1, entity, 8192, player);
  qa_qvm_shared_entity *first = qa_qvm_game_data_entity(&data, 0);
  qa_qvm_shared_entity *early[1300];
  size_t early_count = 0;
  for (int64_t offset = 4; offset < (int64_t)1300 * 4; offset += 4) {
    if (4096 + offset + entity > 16384) break;
    qa_qvm_game_data_locate(&data, 4096 + (double)offset, 1, entity, 8192, player);
    early[early_count++] = qa_qvm_game_data_entity(&data, 0);
  }
  CHECK(early_count > 1024);
  qa_qvm_game_data_locate(&data, 4096 + 4 * (double)early_count, 1, entity, 8192, player);
  CHECK(qa_qvm_game_data_entity(&data, 0) == early[early_count - 1]);
  qa_qvm_game_data_locate(&data, 4096, 1, entity, 8192, player);
  CHECK(qa_qvm_game_data_entity(&data, 0) == first);
  qa_qvm_game_data_destroy(&data);
}

int main(void) {
  test_math_snap_vector();
  test_memory_traps();
  test_bot_library();
  test_audio();
  test_browser();
  test_cinematic();
  test_collision_ownership();
  test_mark();
  test_render();
  test_client_state();
  test_common();
  test_server_game();
  test_ui_key();
  test_game_data();
  printf("test_qvm_syscalls_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
