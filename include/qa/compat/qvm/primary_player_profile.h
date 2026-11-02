/*
 * Port of src/compat/qvm/primary-player-profile.ts. The input
 * definition, weapon profile and combat profile are the landed
 * qa_qvm_input_definition (TS owner src/compat/qvm/game-input.ts;
 * game_input.h), qa_qvm_primary_weapon_profile (TS owner
 * src/compat/qvm/game-weapons.ts; game_weapons.h) and
 * qa_qvm_primary_combat_profile (TS owner
 * src/compat/qvm/game-combat-binding.ts; game_combat_binding.h).
 * The combat call validators are mirrored from
 * src/compat/qvm/game-combat.ts. Reader-owned lists are freed by
 * the *_free functions.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_PLAYER_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_PLAYER_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/game_combat.h"
#include "qa/compat/qvm/game_combat_binding.h"
#include "qa/compat/qvm/game_input.h"
#include "qa/compat/qvm/game_weapons.h"
#include "qa/compat/qvm/module.h"
#include "qa/content/mods/match.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/contracts/source_items.h"
#include "qa/persistence/value.h"

typedef struct qa_qvm_catalog_entry {
  int64_t weapon;
  qa_item_id item;
} qa_qvm_catalog_entry;

/* Equipment contexts are the landed qa_source_equipment_context
 * (contracts/source_items.h); item NULL is the TS null. */

void qa_qvm_read_primary_input(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                               qa_qvm_input_definition *out);
void qa_qvm_read_primary_weapons(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                 const qa_qvm_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 qa_qvm_primary_weapon_profile *out);
void qa_qvm_read_primary_combat(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                qa_qvm_primary_combat_profile *out);
void qa_qvm_input_definition_free(qa_qvm_input_definition *definition);
void qa_qvm_primary_weapon_profile_free(qa_qvm_primary_weapon_profile *profile);
void qa_qvm_primary_combat_profile_free(qa_qvm_primary_combat_profile *profile);

#endif
