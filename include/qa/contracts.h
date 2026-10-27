/*
 * Port of src/contracts/index.ts (re-export barrel) plus the
 * contract headers the barrel does not name but later modules
 * consume directly, in dependency order. GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_H
#define QA_CONTRACTS_H

#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/world.h"
#include "qa/contracts/render.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/flare.h"
#include "qa/contracts/held_weapon.h"
#include "qa/contracts/model_attachment.h"
#include "qa/contracts/equipment.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/source_match.h"
#include "qa/contracts/session.h"
#include "qa/contracts/ui.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/qvm_grapple.h"
#include "qa/contracts/mod_client_presentation.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/qc_weapon_stage.h"
#include "qa/contracts/qc_pickup_callers.h"
#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/qvm_mod_actor_frame.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/native_mod_region.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/mods.h"

#endif
