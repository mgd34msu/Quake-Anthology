/*
 * Port of src/content/catalog/weapon-behaviors.ts. QcProgram
 * (src/compat/qc/program.ts) and the QC capability profile
 * (src/compat/qc/weapon-behavior-profile.ts) are unported, so the
 * program shape is mirrored here with collision-proof
 * qa_catalog_qc_* names and the capability check is included in
 * the implementation. MountedContent is the open() reader from
 * index.h. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_WEAPON_BEHAVIORS_H
#define QA_CONTENT_CATALOG_WEAPON_BEHAVIORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/weapon_behavior.h"

/* TS QcOpcode values this file uses (enum order in
 * compat/qc/program.ts, Done=0): Address 30, StoreFn 36,
 * StorePFn 42. The constants live in weapon_behaviors.c. */
typedef struct qa_catalog_qc_statement {
  int64_t opcode;
  int64_t a;
  int64_t b;
  int64_t c;
} qa_catalog_qc_statement;
typedef struct qa_catalog_qc_function {
  int64_t index;
  const char *name;
  int64_t first_statement;
  size_t parameter_size_count;
} qa_catalog_qc_function;
typedef struct qa_catalog_qc_definition {
  const char *name;
  const char *type; /* TS QcValueType string */
  int64_t offset;
} qa_catalog_qc_definition;

typedef struct qa_catalog_qc_program {
  const qa_catalog_qc_statement *statements;
  size_t statement_count;
  const qa_catalog_qc_function *functions;
  size_t function_count;
  const qa_catalog_qc_definition *globals;
  size_t global_count;
  const qa_catalog_qc_definition *fields;
  size_t field_count;
  const uint8_t *initial_globals;
  size_t initial_globals_length;
  const char *digest; /* qa_content_digest text */
} qa_catalog_qc_program;

/* TS SourceWeaponBehaviorMetadata. activation_function NULL =
 * undefined. */
typedef struct qa_source_weapon_behavior_metadata {
  const char *id; /* `${string}:${string}` */
  const char *title;
  const char *artifact_digest;
  qa_projectile_role role;
  const char *aspect; /* "trajectory" */
  const char *fire_function;
  const char *activation_function; /* nullable */
} qa_source_weapon_behavior_metadata;

/* TS resolveQcWeaponBehavior. Callback definitions in the result
 * borrow module/metadata strings. */
qa_weapon_behavior_compatibility qa_resolve_qc_weapon_behavior(
    qa_module_identity module, const qa_catalog_qc_program *program,
    qa_source_weapon_behavior_metadata metadata);

/* TS WeaponBehaviorCatalog. Entries are borrowed pointers. */
typedef struct qa_weapon_behavior_catalog {
  const qa_weapon_behavior_definition **entries;
  size_t count;
  size_t capacity;
} qa_weapon_behavior_catalog;
void qa_weapon_behavior_catalog_init(qa_weapon_behavior_catalog *catalog);
void qa_weapon_behavior_catalog_destroy(qa_weapon_behavior_catalog *catalog);
/* TS add: duplicate ids abort with the exact TS Error. */
bool qa_weapon_behavior_catalog_add(qa_weapon_behavior_catalog *catalog,
                                    const qa_weapon_behavior_definition *definition);
/* TS forRole: malloc'd borrowed array. */
const qa_weapon_behavior_definition **qa_weapon_behavior_catalog_for_role(
    const qa_weapon_behavior_catalog *catalog, qa_projectile_role role,
    size_t *out_count);
/* TS require: unknown ids abort with the exact TS Error. */
const qa_weapon_behavior_definition *qa_weapon_behavior_catalog_require(
    const qa_weapon_behavior_catalog *catalog, const char *id);

/* TS QcTrajectoryBindingInspection. */
typedef struct qa_qc_trajectory_binding_inspection {
  int64_t producer_function;
  int64_t think_function;
  int64_t statement;
} qa_qc_trajectory_binding_inspection;
/* TS inspectQcTrajectoryBindings. Malloc'd list (free array). */
qa_qc_trajectory_binding_inspection *qa_inspect_qc_trajectory_bindings(
    const qa_catalog_qc_program *program, size_t *out_count);

/* TS MountedWeaponBehaviorDiscovery. */
typedef struct qa_mounted_weapon_behavior_discovery {
  bool is_declared;
  qa_weapon_behavior_compatibility *declarations;
  size_t declaration_count;
  qa_qc_trajectory_binding_inspection *bindings;
  size_t binding_count;
  const char *reason; /* undeclared */
  char **owned_strings;
  size_t owned_count;
} qa_mounted_weapon_behavior_discovery;
/* TS discoverQcWeaponBehaviors. False only on OOM. */
bool qa_discover_qc_weapon_behaviors(qa_catalog_mounted_reader content,
                                     qa_module_identity module,
                                     const qa_catalog_qc_program *program,
                                     qa_mounted_weapon_behavior_discovery *out);
void qa_mounted_weapon_behavior_discovery_free(
    qa_mounted_weapon_behavior_discovery *discovery);

#endif
