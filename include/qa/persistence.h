/*
 * Port of src/persistence/index.ts (re-export barrel) plus the
 * persistence headers the barrel does not name but later modules
 * consume directly, in dependency order. Barrel re-exports not
 * included yet (q2-foundation, q2-monsters, q2-players,
 * q2-weapons, q2-rerelease-state, q2-missionpacks) are omitted
 * until their dependencies land.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_H
#define QA_PERSISTENCE_H

#include "qa/persistence/value.h"
#include "qa/persistence/source_bytes.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/provider_ownership.h"
#include "qa/persistence/save_policy.h"
#include "qa/persistence/native_weapon.h"
#include "qa/persistence/source_items.h"
#include "qa/persistence/world_state.h"
#include "qa/persistence/protection.h"
#include "qa/persistence/q1.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/q1_selection.h"
#include "qa/persistence/saved_game.h"
#include "qa/persistence/q2_base_entities.h"
#include "qa/persistence/q2_classic.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/q2_containers.h"
#include "qa/persistence/q2_hand_grenades.h"
#include "qa/persistence/q2_items.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/q2_rerelease.h"
#include "qa/persistence/q2_typescript.h"
#include "qa/persistence/qvm_grapple.h"
#include "qa/persistence/kex_q2_save.h"
#include "qa/persistence/source_json.h"

#endif
