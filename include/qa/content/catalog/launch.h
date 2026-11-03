/*
 * Port of src/content/catalog/launch.ts. MountedContent
 * (src/content/mounts/index.ts) and readQvmCompatibility
 * (src/compat/qvm/compatibility.ts) are unported: they are the
 * qa_launch_mounted / qa_launch_services tables below (fn + ctx).
 * Async flows are synchronous. Plain TS Errors abort printing
 * "Error: ..."; RangeErrors use qa_range_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_LAUNCH_H
#define QA_CONTENT_CATALOG_LAUNCH_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/weapons.h"
#include "qa/contracts/content.h"

/* TS LaunchPreset: ExecutableRecipe without schemaVersion/
 * preset/map/execution/mounts/resources, plus selection-shaped
 * map and execution. All fields borrowed except in results. */
typedef struct qa_launch_preset {
  const qa_resolved_weapon_behavior_selection *weapon_behaviors;
  size_t weapon_behavior_count;
  const qa_resolved_gameplay_mod *const *mods;
  size_t mod_count;
  qa_recipe_id id;
  qa_map_selection map;
  qa_campaign_selection campaign;
  qa_provider_reference movement;
  qa_character_selection character;
  qa_provider_reference_list weapons;
  qa_equipment_selection equipment;
  qa_enemy_selection enemies;
  qa_presentation_selection presentation;
  qa_provider_reference engine_behavior;
  qa_provider_reference combat;
  qa_provider_reference inventory;
  qa_provider_reference match;
  qa_provider_reference transition;
  const qa_execution_selection *execution;
  size_t execution_count;
  const qa_provider_timing *timing;
  size_t timing_count;
  qa_frame_ordering ordering;
} qa_launch_preset;

/* TS SelectedLaunch. */
typedef struct qa_selected_launch {
  qa_launch_preset base;
  qa_recipe_id preset;
} qa_selected_launch;

/* Mount predicate for MountedContent.open(path, predicate). */
typedef bool (*qa_launch_mount_predicate)(void *ctx,
                                          const qa_content_mount *mount);

/* Minimal MountedContent surface launch.ts uses. The references open and
 * resolve write are borrowed views owned by the service: valid until its
 * close. Keep one longer only as an owned copy (qa_resolved_resource_copy). */
typedef struct qa_launch_mounted qa_launch_mounted;
struct qa_launch_mounted {
  void *ctx;
  const qa_resolved_mount_plan *plan; /* borrowed */
  bool (*open)(void *ctx, const char *path,
               qa_launch_mount_predicate accept, void *accept_ctx,
               qa_catalog_opened_resource *out);
  bool (*resolve)(void *ctx, const char *path,
                  qa_resolved_resource_reference *out);
  /* borrowOrderedReader: fills a reader view owned by the
   * service; its strings (plan id) are allocated by the caller
   * helpers in launch.c through the same table. */
  bool (*borrow_ordered)(void *ctx, qa_mount_plan_id id,
                         const qa_mount_id *default_order, size_t order_count,
                         qa_launch_mounted *out);
  void (*close)(void *ctx); /* nullable */
};

/* openMountPlan + readQvmCompatibility services. `options` is the
 * unported OpenMountOptions, passed through opaquely. */
typedef struct qa_launch_services {
  void *ctx;
  bool (*open_mount_plan)(void *ctx, const qa_resolved_mount_plan *plan,
                          const void *options, qa_launch_mounted *out);
  /* Returns the QvmAbiProfile string (e.g. "q3-modern"). */
  const char *(*read_qvm_compatibility)(void *ctx,
                                        const qa_launch_mounted *mounted,
                                        const char *artifact_path,
                                        qa_content_digest digest,
                                        const char *role);
} qa_launch_services;

/* TS ResolveLaunchOptions. id NULL = preset.id default handled by
 * select; mounts_options is opaque (OpenMountOptions). */
typedef struct qa_resolve_launch_options {
  qa_launch_choice choice;
  const qa_launch_preset *preset;
  qa_content_catalog *catalog;
  bool has_id;
  qa_recipe_id id;
  const void *mounts_options;
  qa_launch_services services;
} qa_resolve_launch_options;

/* TS presetChoice. */
qa_launch_choice qa_preset_choice(qa_recipe_id preset);

/* TS selectLaunch. id NULL means preset->id. The result borrows
 * choice/preset values. Mismatched presets abort (RangeError). */
qa_selected_launch qa_select_launch(qa_launch_choice choice,
                                    const qa_launch_preset *preset,
                                    qa_recipe_id id);

/* TS validateLaunchRoles. */
void qa_validate_launch_roles(const qa_selected_launch *launch,
                              const qa_content_catalog *catalog);

/* TS resolveLaunchResource. kind is "map" or "artifact". *out is an
 * owned reference (release with qa_resolved_resource_release); it does
 * not depend on `mounted` or the reader borrowed for an artifact, which
 * is closed before return. Plan id strings built on the way are recorded
 * in `strings` (caller frees). False only on OOM. */
typedef struct qa_launch_string_list {
  char **values;
  size_t count;
  size_t capacity;
} qa_launch_string_list;
void qa_launch_string_list_free(qa_launch_string_list *list);
bool qa_resolve_launch_resource(qa_content_catalog *catalog,
                                qa_launch_mounted *mounted,
                                qa_resource_request request,
                                const char *kind,
                                qa_launch_string_list *strings,
                                qa_resolved_resource_reference *out);

/* TS prepareLaunchMountPlan result. */
typedef struct qa_prepare_launch_result {
  qa_selected_launch selected;
  qa_provider_reference *weapons; /* owned canonical weapons */
  char **owned_providers;
  size_t owned_provider_count;
  qa_catalog_mount_plan_result plan;
  qa_launch_string_list strings;
  qa_prefix_mount_order *prefix_orders; /* owned plan view */
  qa_mount_id **prefix_mount_arrays;
  size_t prefix_mount_array_count;
} qa_prepare_launch_result;
bool qa_prepare_launch_mount_plan(const qa_resolve_launch_options *options,
                                  qa_prepare_launch_result *out,
                                  qa_resolved_mount_plan *out_plan);
void qa_prepare_launch_result_free(qa_prepare_launch_result *result);

/* TS resolveLaunch result: the recipe plus all owned backing. The recipe's
 * map.geometry, resources (the same array as `resources`) and the execution
 * artifacts (`artifacts`) are owned references that outlive the mount set
 * resolveLaunch opened and closed; qa_resolve_launch_result_free releases
 * them. */
typedef struct qa_resolve_launch_result {
  qa_executable_recipe recipe;
  qa_prepare_launch_result prepared;
  qa_resolved_mount_plan plan;
  qa_execution_selection *execution; /* owned */
  qa_resolved_resource_reference *artifacts; /* owned */
  qa_resolved_resource_reference *resources; /* owned */
  qa_catalog_timing_list timing;
  qa_provider_id *ordering_providers; /* owned, with its strings */
  size_t ordering_provider_count;
  qa_launch_string_list strings;
  qa_provider_timing *weapon_timing;
  size_t weapon_timing_count;
} qa_resolve_launch_result;
bool qa_resolve_launch(const qa_resolve_launch_options *options,
                       qa_resolve_launch_result *out);
void qa_resolve_launch_result_free(qa_resolve_launch_result *result);

#endif
