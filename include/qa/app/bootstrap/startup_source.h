/* Port of src/app/bootstrap/startup-source.ts. ApplicationSourceSelection
 * (content.ts, sibling slice) and the simulation/frame-time registrars
 * (sibling slices) are mirrored minimally / as host fn+ctx.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_SOURCE_H
#define QA_APP_BOOTSTRAP_STARTUP_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/settings/server/selection.h"
typedef struct qa_app_startup_source_host {
  void *ctx;
  void (*register_q3_product_policy)(void *ctx, qa_cvar_registry *cvars);
  void (*register_quakeworld_engine_cvars)(void *ctx, qa_cvar_registry *cvars);
  void (*register_q3_server_cvars)(void *ctx, qa_cvar_registry *cvars, double max_clients, const char *map_name);
  void (*register_frame_time_cvars)(void *ctx, qa_cvar_registry *cvars);
} qa_app_startup_source_host;
qa_cvar_registry *qa_app_create_startup_source(qa_app_startup_source_host host, const qa_app_options *options,
                                               qa_app_application_source_selection selection, qa_command_dialect dialect,
                                               qa_command_context context, double max_clients,
                                               qa_command_print_fn print, void *print_ctx);
typedef struct qa_app_startup_rules { qa_app_options options; double max_clients; } qa_app_startup_rules;
/* definitions: landed serverDefinitionsForRecipe result (borrowed rows). */
qa_app_startup_rules qa_app_resolve_startup_rules(qa_app_options options, qa_cvar_registry *cvars, double default_capacity,
                                                  const qa_server_setting_definition *const *definitions, size_t definition_count, bool apply_explicit);
#endif
