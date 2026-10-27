/* Port of src/world/gameplay/damage-modifier.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_GAMEPLAY_DAMAGE_MODIFIER_H
#define QA_WORLD_GAMEPLAY_DAMAGE_MODIFIER_H

#include <stdbool.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"

/* modifier may be NULL (TS undefined). is_live is fn ptr + ctx.
 * attack.damagePowerupOwner is TS optional (contracts has_ flag). */
qa_damage_request qa_apply_source_damage_modifier(
    qa_damage_request request, const qa_source_damage_modifier *modifier,
    bool (*is_live)(void *ctx, qa_actor_id actor), void *is_live_ctx);

#endif
