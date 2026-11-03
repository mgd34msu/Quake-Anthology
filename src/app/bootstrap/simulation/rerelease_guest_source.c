/* Port of src/app/bootstrap/simulation/rerelease-guest-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/native_entries.h"
#include "qa/compat/q2/rerelease/world_profile.h"
#include "qa/foundation/support.h"

static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
void qa_app_sim_prepare_rerelease_guest(const qa_resolved_execution_module *execution, qa_content_mounted *mounts, qa_app_sim_prepared_rerelease_guest *out) {
  memset(out, 0, sizeof *out);
  if (!same(execution->role, "server-game") || execution->api.kind != QA_GAME_API_Q2_GAME || execution->api.as.q2_game.kind != QA_Q2_GAME_API_RERELEASE
      || !same(execution->profile.kind, "windows-x86-64")) qa_range_error("Rerelease guest requires the native Windows x64 game API 2023");
  qa_compat_q2_native_compatibility_result *primary = qa_app_sim_prepare_native_artifact(execution, mounts, "rerelease", "Native artifact ABI differs from the selected profile",
                                                                                         &out->bytes, &out->length, &out->primary);
  out->edition = "rerelease"; out->execution = *execution; out->has_primary = primary != NULL; out->compatibility = primary;
}
void qa_app_sim_prepared_rerelease_guest_free(qa_app_sim_prepared_rerelease_guest *prepared) {
  free(prepared->bytes); qa_app_sim_native_compatibility_free(prepared->compatibility); memset(prepared, 0, sizeof *prepared);
}

struct qa_app_sim_rerelease_guest_source {
  qa_app_sim_native_guest *guest;
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_guest_address game;
  double budget;
  bool closed;
  void *clock_ctx;
  bool (*now)(void *, double *);
  bool (*counter)(void *, int64_t *);
  char revision[1024];
};
static const qa_native_call_abi rerelease_abi = {"windows-x86-64", "pe32+", 8, "microsoft-x64"};
/* The clock capabilities keep their caller's ctx. */
static bool clock_now(void *ctx, double *out) { qa_app_sim_rerelease_guest_source *s = ctx; return s->now(s->clock_ctx, out); }
static bool clock_counter(void *ctx, int64_t *out) { qa_app_sim_rerelease_guest_source *s = ctx; return s->counter(s->clock_ctx, out); }

qa_app_sim_rerelease_guest_source *qa_app_sim_rerelease_guest_source_create(const qa_app_sim_prepared_rerelease_guest *prepared,
                                                                            const qa_app_sim_rerelease_guest_source_options *options) {
  qa_app_sim_rerelease_guest_source *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->clock_ctx = options->clock_ctx; s->now = options->now_milliseconds; s->counter = options->performance_counter;
  qa_module_identity module = qa_app_sim_prepared_module_identity(&prepared->execution, prepared->has_primary, &prepared->primary, s->revision, sizeof s->revision);
  qa_app_sim_native_guest_options guest; memset(&guest, 0, sizeof guest);
  guest.module = module; guest.pointer_bytes = 8; guest.managed = true; guest.bytes = prepared->bytes; guest.length = prepared->length;
  guest.has_base = true; guest.base = 0x280000000ULL; guest.x87_control_word = 0x37f; guest.stack_top = 1048576;
  guest.stack_label = "native game stack"; guest.return_label = "native game return";
  guest.capabilities.ctx = s; guest.capabilities.now_milliseconds = clock_now; guest.capabilities.performance_counter = clock_counter;
  guest.capabilities.has_performance_frequency = true; guest.capabilities.performance_frequency = options->performance_frequency;
  s->guest = qa_app_sim_native_guest_create(&guest); if (s->guest == NULL) qa_range_error("Out of memory");
  qa_compat_q2_memory *memory = qa_app_sim_native_guest_memory(s->guest);
  s->game = qa_app_sim_native_guest_entry(s->guest, "GetGameAPI");
  qa_guest_address cgame = qa_app_sim_native_guest_entry(s->guest, "GetCGameAPI");
  s->budget = options->host.module_options.has_instruction_budget ? options->host.module_options.instruction_budget : 5000000;
  const qa_compat_q2_rerelease_primary_world_profile *world_profile = prepared->has_primary
    ? (const qa_compat_q2_rerelease_primary_world_profile *)prepared->primary.profile.world : qa_compat_q2_rerelease_get_primary_world_profile(module.digest);
  if (options->host.foreign_damage != NULL && world_profile == NULL) qa_raise("Error", 0, 0, "Original native damage requires a declared source world profile");
  qa_compat_q2_rerelease_q2_host_options host = options->host;
  qa_guest_address image = qa_app_sim_native_guest_image_base(s->guest);
  host.module_options.world_profile = world_profile;
  host.module_options.has_image_base = true; host.module_options.image_base = image;
  if (options->pickups != NULL) {
    host.has_pickups = true; host.pickup_admission = options->pickups; host.pickup_image_base = image;
    host.pickup_profile = prepared->has_primary ? prepared->primary.profile.pickups : NULL;
  }
  if (options->host.foreign_damage != NULL && world_profile != NULL) { host.has_native_entries = true; host.native_entries = qa_compat_q2_rerelease_entries(memory, image, world_profile); }
  host.module_options.runner = qa_app_sim_native_guest_runner(s->guest);
  host.module_options.get_game_api = s->game; host.module_options.get_cgame_api = cgame;
  host.services = options->services(options->services_ctx, memory);
  s->host = qa_compat_q2_rerelease_q2_guest_host_create(&host); if (s->host == NULL) qa_range_error("Out of memory");
  qa_app_sim_native_guest_initialize(s->guest, s->game, rerelease_abi, s->budget);
  return s;
}
void qa_app_sim_rerelease_guest_source_destroy(qa_app_sim_rerelease_guest_source *s) {
  if (s == NULL) return;
  qa_compat_q2_rerelease_q2_guest_host_destroy(s->host); qa_app_sim_native_guest_destroy(s->guest); free(s);
}
qa_compat_q2_rerelease_q2_guest_host *qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_source *s) { return s->host; }
qa_compat_q2_memory *qa_app_sim_rerelease_guest_source_memory(qa_app_sim_rerelease_guest_source *s) { return qa_app_sim_native_guest_memory(s->guest); }
qa_app_sim_native_guest *qa_app_sim_rerelease_guest_source_guest(qa_app_sim_rerelease_guest_source *s) { return s->guest; }
qa_windows_guest_runtime *qa_app_sim_rerelease_guest_source_runtime(qa_app_sim_rerelease_guest_source *s) { return qa_app_sim_native_guest_runtime(s->guest); }
qa_guest_address qa_app_sim_rerelease_guest_source_image_base(qa_app_sim_rerelease_guest_source *s) { return qa_app_sim_native_guest_image_base(s->guest); }
qa_guest_address qa_app_sim_rerelease_guest_source_entry(qa_app_sim_rerelease_guest_source *s, const char *name) { return qa_app_sim_native_guest_entry(s->guest, name); }
/* TS closes the source when Init throws; a C throw aborts. */
void qa_app_sim_rerelease_guest_source_init(qa_app_sim_rerelease_guest_source *s) {
  if (s->closed) qa_raise("Error", 0, 0, "Native guest source is closed");
  qa_compat_q2_rerelease_q2_guest_host_init(s->host);
}
void qa_app_sim_rerelease_guest_source_init_loading(qa_app_sim_rerelease_guest_source *s, void (*next_frame)(void *), void *next_ctx) {
  if (s->closed) qa_raise("Error", 0, 0, "Native guest source is closed");
  qa_compat_q2_rerelease_q2_guest_host_init_loading(s->host, next_frame, next_ctx);
}
void qa_app_sim_rerelease_guest_source_close(qa_app_sim_rerelease_guest_source *s) {
  if (s->closed) return;
  if (qa_guest_call_runner_depth(qa_app_sim_native_guest_call_runner(s->guest)) != 0) qa_raise("Error", 0, 0, "Native guest shutdown requires a completed game call");
  s->closed = true;
  qa_guest_call_runner_recover(qa_app_sim_native_guest_call_runner(s->guest));
  qa_compat_q2_rerelease_q2_guest_host_shutdown(s->host);
  qa_app_sim_native_guest_detach(s->guest, s->game, rerelease_abi, s->budget);
  qa_app_sim_native_guest_release_memory(s->guest);
}
