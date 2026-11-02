/*
 * Port of src/compat/qvm/weapon-behavior-profile.ts. The declaration
 * return (TS unknown object) becomes an owned qa_save_value tree the
 * caller frees with qa_save_value_free. The constructed definition
 * id is malloc'd in the profile's owned_id.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_WEAPON_BEHAVIOR_PROFILE_H
#define QA_COMPAT_QVM_WEAPON_BEHAVIOR_PROFILE_H

#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/persistence/value.h"

/* TS QvmWeaponProfile extends QvmWeaponBehaviorLayout. */
typedef struct qa_qvm_weapon_profile {
  int64_t entity_stride;
  int64_t level_time;
  int64_t allocate;
  int64_t free_entry;
  int64_t field_inuse, field_nextthink, field_think, field_health;
  const char *fire_abi; /* "entity-pointer-start-direction" */
  qa_weapon_behavior_definition definition;
  char *owned_id; /* owned storage for definition.id when read */
} qa_qvm_weapon_profile;

qa_save_value *qa_qvm_weapon_profile_declaration(const qa_qvm_weapon_profile *profile,
                                                 qa_qvm_abi_profile abi_profile);
void qa_qvm_read_weapon_profile(const qa_save_value *value, const qa_resolved_qvm_artifact *artifact,
                                qa_qvm_weapon_profile *out);
void qa_qvm_validate_weapon_profile(const qa_qvm_weapon_profile *profile,
                                    const qa_resolved_qvm_artifact *artifact, qa_qvm_weapon_profile *out);
void qa_qvm_weapon_profile_free(qa_qvm_weapon_profile *profile);

#endif
