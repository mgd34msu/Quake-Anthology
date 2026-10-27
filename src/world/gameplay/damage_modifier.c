/* Port of src/world/gameplay/damage-modifier.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/damage_modifier.h"

#include <string.h>

static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

qa_damage_request qa_apply_source_damage_modifier(
    qa_damage_request request, const qa_source_damage_modifier *modifier,
    bool (*is_live)(void *ctx, qa_actor_id actor), void *is_live_ctx) {
  if (modifier == NULL) return request;
  qa_damage_request result = request;
  /* Live ids keep the caller's borrowed pointers (TS object refs);
   * expired lifetimes become NULL (the original world context). */
  if (result.attack.attacker != NULL && !is_live(is_live_ctx, *result.attack.attacker))
    result.attack.attacker = NULL;
  if (result.attack.inflictor != NULL && !is_live(is_live_ctx, *result.attack.inflictor))
    result.attack.inflictor = NULL;
  bool already_owned = result.attack.has_damage_powerup_owner &&
                       str_eq(result.attack.damage_powerup_owner, modifier->owner);
  result.amount = already_owned
                      ? request.amount
                      : modifier->transform(modifier->ctx, result.attack.attacker, request.amount);
  result.attack.has_damage_powerup_owner = true;
  result.attack.damage_powerup_owner = modifier->owner;
  return result;
}
