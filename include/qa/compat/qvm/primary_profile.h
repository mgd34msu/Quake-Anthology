/*
 * Port of src/compat/qvm/primary-profile.ts. The builtin profile
 * converts the landed content factory outputs (which mirror the same
 * TS profile types) into this slice's mirrored structs; fields with
 * no counterpart in the landed structs stay zero and are noted in
 * the slice report. Declaration is a borrowed resolved reference.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_PROFILE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/primary_inventory_profile.h"
#include "qa/compat/qvm/primary_pickup_profile.h"
#include "qa/compat/qvm/primary_player_profile.h"
#include "qa/content/q3/guest_items.h"
#include "qa/contracts/content.h"
#include "qa/persistence/value.h"

/* TS QvmPrimaryProfile. Sub-profiles are NULL for TS null. */
typedef struct qa_qvm_primary_profile {
  const qa_resolved_resource_reference *declaration;
  qa_qvmy_input_definition *input;
  qa_qvmy_primary_weapon_profile *weapons;
  qa_qvmy_inventory_profile *inventory;
  qa_qvmy_pickup_profile *pickups;
  qa_qvmy_primary_combat_profile *combat;
} qa_qvm_primary_profile;

void qa_qvm_builtin_primary_profile(const qa_qvmy_artifact *artifact,
                                    const qa_q3_guest_weapon *catalog, size_t catalog_count,
                                    qa_qvm_primary_profile *out);
void qa_qvm_read_primary_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                 const qa_qvm_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 const qa_qvmy_item_layout *items,
                                 const qa_resolved_resource_reference *declaration,
                                 qa_qvm_primary_profile *out);
void qa_qvm_primary_profile_free(qa_qvm_primary_profile *profile);

#endif
