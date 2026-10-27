/*
 * Port of src/content/catalog/native-weapon-behaviors.ts.
 * Unported TS owners behind the service table:
 * compat/q2/rerelease/native-weapon-declaration.ts
 * (readNativeWeaponDeclaration/validateNativeWeaponImage),
 * compat/q2/rerelease/weapon-behavior-profile.ts
 * (builtInRereleaseWeaponDeclaration/rereleaseWeaponDefinition),
 * and guest/pe parsePe output (opaque to this file; the guest/pe
 * C port will adapt it to the validate callback's byte view).
 * Declarations ride as owned save-value trees plus their parsed
 * contract form produced by the service.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_NATIVE_WEAPON_BEHAVIORS_H
#define QA_CONTENT_CATALOG_NATIVE_WEAPON_BEHAVIORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/persistence/value.h"

typedef struct qa_mounted_native_weapon_behavior {
  qa_resolved_resource_reference resource;
  qa_weapon_behavior_definition definition;
  qa_native_weapon_behavior_declaration declaration;
} qa_mounted_native_weapon_behavior;

typedef struct qa_native_weapon_behavior_services {
  void *ctx;
  /* readNativeWeaponDeclaration(value[, module]): fills the
   * declaration; module NULL spells the first TS call. */
  bool (*read_declaration)(void *ctx, const qa_save_value *value,
                           const qa_module_identity *module,
                           qa_native_weapon_behavior_declaration *out);
  /* validateNativeWeaponImage(declaration, parsePe(bytes)). */
  bool (*validate_image)(void *ctx,
                         const qa_native_weapon_behavior_declaration *declaration,
                         const uint8_t *bytes, size_t length);
  /* rereleaseWeaponDefinition: false = TS null. */
  bool (*weapon_definition)(
      void *ctx, qa_module_identity module,
      const qa_native_weapon_behavior_declaration *declaration,
      qa_weapon_behavior_definition *out);
  /* builtInRereleaseWeaponDeclaration: false = TS null; *out is
   * an owned save value in TS `unknown` declaration form. */
  bool (*builtin_declaration)(void *ctx, qa_module_identity module,
                              qa_save_value **out);
} qa_native_weapon_behavior_services;

/* TS readNativeWeaponBehaviorDocument: borrowed entries in an
 * owned parsed tree. */
typedef struct qa_native_weapon_behavior_document {
  const qa_save_value *const *profiles;
  size_t profile_count;
  qa_save_value *tree;
} qa_native_weapon_behavior_document;
bool qa_read_native_weapon_behavior_document(
    const uint8_t *bytes, size_t length,
    qa_native_weapon_behavior_document *out);
void qa_native_weapon_behavior_document_free(
    qa_native_weapon_behavior_document *document);

/* TS loadNativeWeaponBehavior. False only on OOM/service absence. */
bool qa_load_native_weapon_behavior(qa_catalog_mounted_reader mounts,
                                    qa_native_weapon_behavior_services services,
                                    qa_provider_id provider,
                                    const qa_save_value *value,
                                    qa_mounted_native_weapon_behavior *out);

typedef struct qa_mounted_native_weapon_behavior_list {
  qa_mounted_native_weapon_behavior *values;
  size_t count;
} qa_mounted_native_weapon_behavior_list;
void qa_mounted_native_weapon_behavior_list_free(
    qa_mounted_native_weapon_behavior_list *list);
/* TS discoverNativeWeaponBehaviors. *out_found false = TS null. */
bool qa_discover_native_weapon_behaviors(
    qa_catalog_mounted_reader mounts,
    qa_native_weapon_behavior_services services, qa_provider_id provider,
    qa_mounted_native_weapon_behavior_list *out, bool *out_found);

#endif
