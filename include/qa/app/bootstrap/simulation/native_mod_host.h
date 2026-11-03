/* Port of src/app/bootstrap/simulation/native-mod-host.ts.
 * createNativeModHost reuses the selected DLL's real imports and original
 * save callbacks in an isolated host. The returned table is the
 * NativeModHost every native-mod-* class takes
 * (qa/compat/q2/native_mod_host_api.h); its ctx is the host itself.
 *
 * TS async members (initialize, checkpoint, restore) run synchronously with
 * the next-frame callback (docs/PORTING.md). Ownership: the host borrows
 * options->prepared, declaration, context and services (they must outlive
 * it); it owns the guest source, the services adapter, the presentation and
 * the cvar registry. checkpoint() results stay valid until the next
 * checkpoint or destroy; weapon_model(), presentation signature and
 * appearance results until the next call of the same member.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_HOST_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/native_mod_presentation.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/app/bootstrap/simulation/source_hosts.h"
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/compat/q2/rerelease/navigation.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/core/cvars/index.h"
#include "qa/world/session/mod_commands.h"

/* TS NativeModHostContext. clock carries the Required<Pick<WindowsCapabilities,
 * "nowMilliseconds"|"performanceCounter"|"performanceFrequency">> members
 * with their own ctx. engine returns a host the context keeps alive while
 * the native mod host lives; runtime outlives that host. */
typedef struct qa_app_sim_native_mod_host_context {
  qa_shared_scene_queries *scene;
  const char *map_path;
  double max_clients;
  double frame_milliseconds;
  double skill;
  const char *mode; /* "singleplayer" | "coop" | "deathmatch" */
  double gravity;
  qa_windows_capabilities clock;
  qa_compat_q2_rerelease_navigation_services navigation;
  void *ctx;
  void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
  qa_q2_foundation_host *(*engine)(void *ctx, qa_provider_reference source, const qa_app_sim_actor_host_runtime *runtime);
} qa_app_sim_native_mod_host_context;

/* TS NativeModHostOptions. Exactly one of classic/rerelease is set (TS
 * prepared.edition). bind_commands NULL is TS undefined; localize returns a
 * malloc'd string. */
typedef struct qa_app_sim_native_mod_host_options {
  const qa_app_sim_prepared_classic_guest *classic;
  const qa_app_sim_prepared_rerelease_guest *rerelease;
  const qa_native_mod_declaration *declaration;
  qa_provider_reference source;
  const qa_app_sim_native_mod_host_context *context;
  const qa_mod_host_services *services;
  qa_app_sim_native_mod_projection projection;
  void *ctx;
  qa_mod_command_port *(*bind_commands)(void *ctx, qa_cvar_registry *cvars);
  char *(*localize)(void *ctx, const char *key, const char *const *arguments, size_t count);
  void (*next_frame)(void *ctx);
} qa_app_sim_native_mod_host_options;

/* TS createNativeModHost. Aborts as the TS throws. */
qa_compat_q2_native_mod_host *qa_app_sim_create_native_mod_host(const qa_app_sim_native_mod_host_options *options);
/* Frees the host after close (or without one, for a host never initialized). */
void qa_app_sim_native_mod_host_destroy(qa_compat_q2_native_mod_host *host);
/* TS host.cvars. */
qa_cvar_registry *qa_app_sim_native_mod_host_cvars(const qa_compat_q2_native_mod_host *host);
/* TS host.presentation as the class (the table member forwards to it). */
qa_app_sim_native_mod_presentation *qa_app_sim_native_mod_host_presentation(const qa_compat_q2_native_mod_host *host);
#endif
