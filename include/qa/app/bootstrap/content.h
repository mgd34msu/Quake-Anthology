/* Port of src/app/bootstrap/content.ts. The catalog/mount composition
 * (discover, preset selection, mount plans, map decode, mod/grapple/
 * weapon-behavior preparation) is wired by the host through the
 * callbacks below, as in landed startup.c; selection guards, the
 * discovers-mods predicate and opened-mounts bookkeeping are kept
 * here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONTENT_H
#define QA_APP_BOOTSTRAP_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/demo_playback.h"
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/launch.h"
#include "qa/contracts/content.h"
#include "qa/core/q3_product_policy.h"
/* TS ApplicationContentSource. */
typedef struct qa_app_content_source { const char *kind; qa_app_demo_family family; } qa_app_content_source;
/* TS ApplicationWorld = Q1Map | Q2DecodedMap | Q3WorldGeometry (opaque). */
typedef struct qa_app_application_world { const char *kind; const void *value; } qa_app_application_world;
typedef struct qa_app_application_map_sidecar {
  qa_content_id content; const char *path;
  const qa_resolved_resource_reference *resource; /* NULL = TS null */
} qa_app_application_map_sidecar;
/* TS ApplicationSourceSelection. Supersedes the startup_source.h
 * qa_app_startup_source_selection mirror of these fields. */
typedef struct qa_app_application_source_selection {
  qa_provider_reference source, match;
  const char *rules;
} qa_app_application_source_selection;
/* TS ApplicationConfigurationSelection. */
typedef struct qa_app_application_configuration_selection {
  qa_provider_reference source, engine_behavior, match, combat, movement;
  qa_provider_timing timing;
} qa_app_application_configuration_selection;
typedef struct qa_app_loaded_content qa_app_loaded_content;
typedef struct qa_app_configuration_content qa_app_configuration_content;
/* TS ApplicationConfigurationRequest union (kind text). */
typedef struct qa_app_configuration_request {
  const char *kind; /* "launch" | other host kinds */
  const qa_launch_preset *preset;
  qa_launch_choice choice;
} qa_app_configuration_request;
typedef struct qa_app_content_host {
  void *ctx;
  /* Catalog product facts used by the pure selectors. */
  bool (*product)(void *ctx, const char *id, qa_game_family *family, const char **edition, const char **campaign, bool *installed, bool *builtin);
  bool (*require_product)(void *ctx, const char *id); /* false aborts as catalog.require */
  /* Composition steps; return error text or NULL. */
  const char *(*open_loaded)(void *ctx, qa_app_loaded_content *content);
  const char *(*open_configuration)(void *ctx, qa_app_configuration_content *content);
  const char *(*open_remote)(void *ctx, qa_app_configuration_content *content);
  const char *(*for_content)(void *ctx, qa_app_loaded_content *content, qa_content_id content_id, const void **out_mounts);
  const char *(*prepare_mods)(void *ctx, qa_app_loaded_content *content, const char *purpose);
  const char *(*prepare_grapple)(void *ctx, qa_app_loaded_content *content);
  const char *(*prepare_weapon_behaviors)(void *ctx, qa_app_loaded_content *content);
  const char *(*close_loaded)(void *ctx, qa_app_loaded_content *content);
  const char *(*resolve_travel)(void *ctx, qa_app_loaded_content *content, const char *path, qa_executable_recipe *out);
  const char *(*preset)(void *ctx, const qa_app_options *options, bool configuration, qa_launch_preset *out);
} qa_app_content_host;
bool qa_app_application_discovers_mods(const qa_app_options *options, bool has_recipe,
                                       bool product_known, qa_game_family product_family);
qa_app_application_source_selection qa_app_application_source_selection_of(qa_app_content_host host, const char *product, const char *rules);
qa_app_loaded_content *qa_app_load_application_content(qa_app_content_host host, qa_app_options options,
                                                       const qa_executable_recipe *restored_recipe, bool has_recipe);
void qa_app_loaded_content_destroy(qa_app_loaded_content *content);
const qa_executable_recipe *qa_app_loaded_content_recipe(const qa_app_loaded_content *content);
void qa_app_loaded_content_set_recipe(qa_app_loaded_content *content, qa_executable_recipe recipe);
const void *qa_app_loaded_content_catalog(const qa_app_loaded_content *content);
void qa_app_loaded_content_set_catalog(qa_app_loaded_content *content, const void *catalog);
const void *qa_app_loaded_content_mounts(const qa_app_loaded_content *content);
void qa_app_loaded_content_set_mounts(qa_app_loaded_content *content, const void *mounts);
bool qa_app_loaded_content_q3_policy(const qa_app_loaded_content *content, qa_q3_product_policy *out);
void qa_app_loaded_content_set_q3_policy(qa_app_loaded_content *content, qa_q3_product_policy policy);
const void *qa_app_loaded_content_for_content(qa_app_loaded_content *content, qa_content_id content_id);
size_t qa_app_loaded_content_opened_mounts(qa_app_loaded_content *content, const void **out, size_t capacity);
void qa_app_loaded_content_prepare_mods(qa_app_loaded_content *content, const char *purpose);
void qa_app_loaded_content_prepare_qvm_grapple(qa_app_loaded_content *content);
void qa_app_loaded_content_prepare_weapon_behaviors(qa_app_loaded_content *content);
void qa_app_loaded_content_close(qa_app_loaded_content *content);
qa_executable_recipe qa_app_resolve_application_travel(qa_app_loaded_content *content, const char *path);
/* Configuration content (TS ApplicationConfigurationContent). */
qa_app_configuration_content *qa_app_open_application_configuration_content(qa_app_content_host host, qa_app_configuration_request request);
qa_app_configuration_content *qa_app_open_remote_application_content(qa_app_content_host host, qa_app_options options);
void qa_app_configuration_content_destroy(qa_app_configuration_content *content);
qa_app_application_configuration_selection qa_app_configuration_content_selection(const qa_app_configuration_content *content);
void qa_app_configuration_content_set_selection(qa_app_configuration_content *content, qa_app_application_configuration_selection selection);
const void *qa_app_configuration_content_mounts(const qa_app_configuration_content *content);
void qa_app_configuration_content_set_mounts(qa_app_configuration_content *content, const void *mounts);
const void *qa_app_configuration_content_catalog(const qa_app_configuration_content *content);
void qa_app_configuration_content_set_catalog(qa_app_configuration_content *content, const void *catalog);
bool qa_app_configuration_content_q3_product(const qa_app_configuration_content *content, const void **out);
void qa_app_configuration_content_set_q3_product(qa_app_configuration_content *content, const void *product);
void qa_app_configuration_content_close(qa_app_configuration_content *content);
qa_launch_preset qa_app_application_preset(qa_app_content_host host, const qa_app_options *options);
qa_launch_preset qa_app_application_configuration_preset(qa_app_content_host host, const qa_app_options *options);
#endif
