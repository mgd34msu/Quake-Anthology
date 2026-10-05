#include "internal.h"

static bool spawn(q2_save_io *io, qa_q2_item_spawn *s) {
    Q2U(spawnflags); Q2I(count); Q2N(target); Q2N(killtarget); Q2N(message); Q2N(team);
    Q2F(delay); return true;
}
static bool companion(q2_save_io *io, qa_q2_companion_checkpoint *s) {
    Q2U(kind);
    if (!q2_save_actor_pointer(io, &s->owner) || !q2_save_actor_pointer(io, &s->enemy) ||
        !q2_save_actor_pointer(io, &s->child) || !q2_save_actor_pointer(io, &s->credit)) return false;
    Q2T(expires_ns); Q2T(attack_ns); Q2T(next_ns); Q2T(turn_ns); Q2V(goal);
    Q2I(frame); Q2N(loop_sound); Q2B(active); Q2B(decoy); Q2B(camera); return true;
}
bool q2_save_item(q2_save_io *io, qa_q2_item_checkpoint *s) {
    Q2B(present); Q2B(powers_present); Q2N(definition);
    if (!spawn(io, &s->spawn)) return false;
    if (!q2_save_actor_pointer(io, &s->owner)) return false;
    Q2R(team_master); Q2R(team_next); Q2R(sphere);
    Q2T(due_ns); Q2T(expires_ns); Q2U(think); Q2B(targets_used); Q2B(retained);
    Q2B(visible); Q2B(touchable); Q2B(temporary);
    void *slots = s->picked_slots;
    if (!q2_save_count(io, &s->picked_count, 4, sizeof(*s->picked_slots), &slots)) return false;
    s->picked_slots = slots;
    for (size_t i = 0; i < s->picked_count; ++i) Q2U(picked_slots[i]);
    if (!q2_save_visual(io, &s->visual) || !companion(io, &s->companion)) return false;
    Q2T(powers.quad_until_ns); Q2T(powers.invulnerability_until_ns);
    Q2T(powers.breather_until_ns); Q2T(powers.enviro_until_ns);
    Q2T(powers.quad_fire_until_ns); Q2T(powers.double_until_ns);
    Q2T(powers.ir_until_ns); Q2T(powers.invisibility_until_ns);
    Q2F(maximum_health); Q2U(power_cubes); Q2B(definitions_bound); Q2B(power_inventory_bound);
    return true;
}
