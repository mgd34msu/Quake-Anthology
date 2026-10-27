/*
 * Port of src/content/composition/q1-q3-supply.ts: base Q1 authored
 * pickups with Q1 progression and selected base Q3 weapons.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_Q3_SUPPLY_H
#define QA_CONTENT_COMPOSITION_Q1_Q3_SUPPLY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/pickups.h"

extern const qa_pickup_supply_profile QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE;

/* Mirrored q3SpawnLoadout (TS owner: src/content/q3/foundation/arsenal.ts,
 * unported) as fn+ctx. product is the TS Product string. */
typedef qa_arsenal_state (*qa_composition_q3_spawn_loadout_fn)(void *ctx, qa_provider_id provider,
                                                               const char *product, bool mission_pack);

/**
 * TS q1Q3SupplyLoadout(provider, product = "baseq3"). product NULL picks
 * the TS default. ammo_out (capacity ammo_capacity) receives the adjusted
 * entry copies *out points at; returns false when it cannot hold them.
 */
bool qa_composition_q1_q3_supply_loadout(qa_provider_id provider, const char *product,
                                         qa_composition_q3_spawn_loadout_fn spawn_loadout, void *spawn_ctx,
                                         qa_inventory_entry *ammo_out, size_t ammo_capacity,
                                         qa_arsenal_state *out);

#endif
