/*
 * Port of src/content/catalog/qvm-weapon-behaviors.ts.
 * Unported TS owners behind the service table:
 * compat/qvm/artifacts.ts (resolveQvmArtifact),
 * compat/qvm/weapon-behavior-profile.ts (readQvmWeaponProfile),
 * compat/qvm/module.ts (QvmModuleOptions artifact shape mirrored
 * as qa_catalog_qvm_artifact below).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_QVM_WEAPON_BEHAVIORS_H
#define QA_CONTENT_CATALOG_QVM_WEAPON_BEHAVIORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/persistence/value.h"

/* Mirror of QvmModuleOptions['artifact'] (TS owner:
 * src/compat/qvm/module.ts): kind string ("bytecode" required)
 * plus the resolved artifact identity this file keeps. */
typedef struct qa_catalog_qvm_artifact {
  const char *kind;
  const char *role; /* "qagame" */
  const char *abi_profile;
  qa_module_identity module;
  void *service_value; /* service-owned payload */
} qa_catalog_qvm_artifact;

/* Mirror of QvmWeaponProfile fields this file reads (TS owner:
 * src/compat/qvm/weapon-behavior-profile.ts). */
typedef struct qa_catalog_qvm_weapon_profile {
  qa_weapon_behavior_definition definition;
  void *service_value;
} qa_catalog_qvm_weapon_profile;

typedef struct qa_qvm_weapon_behavior_services {
  void *ctx;
  bool (*resolve_artifact)(void *ctx, qa_catalog_qvm_artifact *artifact,
                           const uint8_t *bytes, size_t length);
  bool (*read_profile)(void *ctx, const qa_save_value *declaration,
                       const qa_catalog_qvm_artifact *artifact,
                       qa_catalog_qvm_weapon_profile *out);
} qa_qvm_weapon_behavior_services;

typedef struct qa_mounted_qvm_weapon_behavior {
  qa_catalog_qvm_artifact artifact;
  qa_resolved_resource_reference resource;
  qa_catalog_qvm_weapon_profile profile;
  const qa_save_value *declaration; /* borrowed */
} qa_mounted_qvm_weapon_behavior;

typedef struct qa_qvm_weapon_behavior_document {
  const qa_save_value *const *profiles;
  size_t profile_count;
  qa_save_value *tree;
} qa_qvm_weapon_behavior_document;
bool qa_read_qvm_weapon_behavior_document(
    const uint8_t *bytes, size_t length, qa_qvm_weapon_behavior_document *out);
void qa_qvm_weapon_behavior_document_free(
    qa_qvm_weapon_behavior_document *document);

bool qa_load_qvm_weapon_behavior(qa_catalog_mounted_reader mounts,
                                 qa_qvm_weapon_behavior_services services,
                                 qa_provider_id provider,
                                 const qa_save_value *declaration,
                                 char **owned_path,
                                 qa_mounted_qvm_weapon_behavior *out);

typedef struct qa_mounted_qvm_weapon_behavior_list {
  qa_mounted_qvm_weapon_behavior *values;
  size_t count;
  char **owned_paths;
  size_t owned_count;
  qa_qvm_weapon_behavior_document document;
} qa_mounted_qvm_weapon_behavior_list;
void qa_mounted_qvm_weapon_behavior_list_free(
    qa_mounted_qvm_weapon_behavior_list *list);
bool qa_discover_qvm_weapon_behaviors(
    qa_catalog_mounted_reader mounts,
    qa_qvm_weapon_behavior_services services, qa_provider_id provider,
    qa_mounted_qvm_weapon_behavior_list *out, bool *out_found);

#endif
