/* Port of src/compat/q2/rerelease/weapon-behavior-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_WEAPON_BEHAVIOR_PROFILE_H
#define QA_COMPAT_Q2_RERELEASE_WEAPON_BEHAVIOR_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/core/cvars/index.h"

typedef struct qa_compat_q2_rerelease_weapon_shooter {
  qa_body_state body;
  qa_vec3 view_angles;
  double view_height;
} qa_compat_q2_rerelease_weapon_shooter;

typedef struct qa_compat_q2_rerelease_weapon_profile {
  void *ctx;
  qa_weapon_behavior_definition definition;
  qa_string_view_list initialization_classes;
  const qa_native_weapon_command *equipment; size_t equipment_count;
  qa_native_weapon_command ammunition;
  const qa_native_weapon_cvar *initial_cvars; size_t initial_cvar_count;
  const qa_native_weapon_cvar *provisioning_cvars; size_t provisioning_cvar_count;
  void (*equip)(void *ctx, qa_raw_entity_view shooter);
  void (*launch)(void *ctx, qa_raw_entity_view shooter);
  bool (*matches)(void *ctx, qa_raw_entity_view projectile, qa_raw_entity_view shooter);
  void (*project_shooter)(void *ctx, qa_raw_entity_view record, qa_compat_q2_rerelease_weapon_shooter shooter);
  void (*project)(void *ctx, qa_raw_entity_view record, qa_body_state body);
  qa_weapon_trajectory_update (*trajectory)(void *ctx, qa_raw_entity_view record);
  double (*generation)(void *ctx, qa_raw_entity_view record);
  void (*time)(void *ctx, int64_t milliseconds);
  int64_t (*next_think)(void *ctx, qa_raw_entity_view record);
  void (*think)(void *ctx, qa_raw_entity_view record);
  qa_raw_entity_view (*allocate)(void *ctx);
  void (*free_record)(void *ctx, qa_raw_entity_view record);
} qa_compat_q2_rerelease_weapon_profile;

/* False is the TS null. On true, out owns malloc'd copies; release
 * with qa_compat_q2_native_weapon_declaration_free. */
bool qa_compat_q2_built_in_rerelease_weapon_declaration(qa_module_identity module, qa_native_weapon_behavior_declaration *out);
/* False is the TS null. declaration NULL is TS undefined. The
 * definition borrows strings from process-retained scratch that the
 * next call here replaces (the TS definition shares the profile). */
bool qa_compat_q2_rerelease_weapon_definition(qa_module_identity module, const qa_native_weapon_behavior_declaration *declaration,
                                              qa_weapon_behavior_definition *out);
/* TS rereleaseWeaponProfile; declaration NULL is TS undefined. NULL only on OOM. */
qa_compat_q2_rerelease_weapon_profile *qa_compat_q2_rerelease_weapon_profile_create(
    qa_compat_q2_rerelease_guest_module *module, qa_guest_address image_base, const qa_native_weapon_behavior_declaration *declaration);
void qa_compat_q2_rerelease_weapon_profile_destroy(qa_compat_q2_rerelease_weapon_profile *profile);
/* Malloc'd; caller frees. */
char *qa_compat_q2_rerelease_weapon_initialization_entities(const char *text, qa_string_view_list initialization_classes);
/* TS withRereleaseWeaponProvisioning<T>: run returns an opaque result. */
void *qa_compat_q2_with_rerelease_weapon_provisioning(const qa_native_weapon_cvar *provisioning_cvars, size_t count,
                                                      qa_cvar_registry *cvars, void *ctx, void (*refresh)(void *ctx),
                                                      void *(*run)(void *ctx));
#endif
