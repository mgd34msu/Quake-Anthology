#include "internal.h"

bool q2_save_attack(q2_save_io *io, qa_attack *s) {
    Q2T(sequence); Q2T(time_ns); Q2N(weapon);
    Q2N(weapon_provider); Q2N(combat_provider); Q2N(inventory_provider);
    Q2N(movement_provider); Q2B(powerup_applied); Q2N(powerup_owner); Q2U(cause.kind);
    switch (s->cause.kind) {
    case QA_CAUSE_Q1:
        Q2N(cause.source.q1.death_type); Q2U(cause.source.q1.armor); break;
    case QA_CAUSE_Q2:
        Q2I(cause.source.q2.means_of_death); Q2U(cause.source.q2.flags);
        Q2U(cause.source.q2.native); Q2I(cause.source.q2.native_value);
        Q2U(cause.source.q2.classic_product); Q2B(cause.source.q2.friendly_fire);
        Q2B(cause.source.q2.no_point_loss); break;
    case QA_CAUSE_Q3:
        Q2I(cause.source.q3.means_of_death); Q2U(cause.source.q3.flags); break;
    case QA_CAUSE_ENVIRONMENT: Q2U(cause.source.hazard); break;
    default: return q2_save_fail(io, "Invalid Q2 continuation damage cause");
    }
    return true;
}
bool q2_save_visual(q2_save_io *io, qa_q2_visual *s) {
    for (size_t i = 0; i < 4; ++i) Q2N(models[i]);
    Q2I(frame); Q2I(old_frame); Q2I(skin); Q2T(effects); Q2U(render_flags);
    Q2F(scale); Q2F(alpha); Q2B(visible); return true;
}
bool q2_save_fog(q2_save_io *io, qa_q2_fog *s) {
    Q2F(density); Q2F(sky_factor); Q2V(color); Q2V(start_color); Q2V(end_color);
    Q2F(start_distance); Q2F(end_distance); Q2F(falloff); Q2F(height_density); return true;
}
bool q2_save_landmark(q2_save_io *io, qa_q2_landmark *s) {
    Q2N(name); Q2V(relative_origin); Q2V(relative_velocity); Q2V(relative_view_angles); return true;
}
bool q2_save_inventory(q2_save_io *io, qa_inventory_entry **entries, size_t *count) {
    void *storage = *entries;
    if (!q2_save_count(io, count, 24, sizeof(**entries), &storage)) return false;
    *entries = storage;
    for (size_t i = 0; i < *count; ++i) {
        qa_inventory_entry *s = *entries + i;
        Q2N(item); Q2S(f64, count); Q2S(f64, capacity); Q2U(policy);
    }
    return true;
}
static bool weapon(q2_save_io *io, qa_q2_weapon_state *s) {
    Q2U(weapon); Q2U(last_weapon); Q2U(pending); Q2U(phase); Q2U(handoff);
    Q2I(frame); Q2I(machinegun_shots); Q2I(view_skin);
    Q2T(think_ns); Q2T(fire_finished_ns); Q2T(empty_sound_ns); Q2T(last_firing_ns);
    Q2T(grenade_ns); Q2T(grenade_finished_ns); Q2T(kick_ns); Q2T(kick_until_ns);
    Q2B(fire_buffered); Q2B(latched_attack); Q2B(source_firing); Q2B(grenade_blew_up);
    Q2U(hand_reservation); Q2V(kick_origin); Q2V(kick_angles);
    Q2F(kick_seconds); Q2F(gun_rate); Q2N(loop_sound); Q2N(view_model); return true;
}
static bool input(q2_save_io *io, qa_q2_weapon_input *s) {
    Q2V(angles); Q2U(hand); Q2U(source_rules); Q2F(view_height); Q2F(gravity);
    Q2B(attack); Q2B(latched_attack); Q2B(holster); Q2B(latched_holster);
    Q2B(ducked); Q2B(spectator); Q2B(notarget); Q2B(animate_player); Q2B(haste);
    Q2B(no_stack_double); Q2B(instant_switch); Q2B(quick_switch); Q2B(infinite_ammo);
    Q2B(players_collide); Q2B(weapon_thunk); Q2B(rune_damage);
    Q2T(quad_until_ns); Q2T(double_until_ns); Q2T(quad_fire_until_ns); return true;
}
static bool physics(q2_save_io *io, qa_physics_properties *s) {
    Q2U(family); Q2U(motion); Q2U(solid); Q2B(q2_rerelease);
    Q2U(flags); Q2U(clip_mask); Q2V(angular_velocity); Q2V(gravity_direction);
    Q2F(gravity_scale); Q2F(delta_yaw); Q2F(ideal_yaw); Q2F(yaw_speed);
    Q2I(water_level); Q2I(water_type);
    Q2S(f64, q1_pusher.local_seconds); Q2S(f64, q1_pusher.next_think_seconds);
    return true;
}
static bool grapple(q2_save_io *io, qa_q2_grapple_state *s) {
    Q2U(phase); Q2T(release_ns); Q2I(hook_state); Q2I(hook_length);
    Q2B(hook_held); Q2B(saved_no_knockback); Q2B(has_saved_no_knockback); Q2B(equipment_bound);
    return weapon(io, &s->equipment);
}
static bool hand(q2_save_io *io, qa_q2_hand_grenade_state *s) {
    Q2B(options.enabled); Q2B(options.infinite_ammo); Q2I(options.initial_ammo); Q2I(options.capacity);
    Q2U(action.kind);
    switch (s->action.kind) {
    case QA_Q2_HAND_IDLE: case QA_Q2_HAND_DISARMED: break;
    case QA_Q2_HAND_PREPARING:
        Q2I(action.state.preparing.frame); Q2T(action.state.preparing.next_ns);
        Q2B(action.state.preparing.release_queued); break;
    case QA_Q2_HAND_COOKING: Q2T(action.state.cooking.expires_ns); break;
    case QA_Q2_HAND_RELEASING:
        Q2T(action.state.releasing.expires_ns); Q2T(action.state.releasing.throw_ns); break;
    case QA_Q2_HAND_RECOVERING:
        Q2T(action.state.recovering.ready_ns); Q2B(action.state.recovering.require_release); break;
    default: return q2_save_fail(io, "Invalid Q2 hand action");
    }
    return true;
}
static bool projectile(q2_save_io *io, qa_q2_projectile_checkpoint *s) {
    Q2U(kind);
    if (!q2_save_attack(io, &s->attack)) return false;
    Q2R(attacker); Q2R(inflictor); Q2R(projectile);
    if (!q2_save_actor_pointer(io, &s->owner) || !q2_save_actor_pointer(io, &s->enemy) ||
        !q2_save_actor_pointer(io, &s->child)) return false;
    Q2V(movedir); Q2F(damage); Q2F(kick); Q2F(radius_damage); Q2F(radius);
    Q2F(gravity); Q2F(speed); Q2F(delay); Q2F(captured_mass); Q2F(turn_fraction);
    Q2T(born_ns); Q2T(expire_ns); Q2T(next_ns); Q2T(effect_ns); Q2T(effects);
    Q2U(render_flags); Q2U(gib_flags); Q2N(classname); Q2N(model); Q2N(loop_sound);
    Q2I(direct_mod); Q2I(splash_mod); Q2I(frame); Q2I(phase); Q2I(wait); Q2I(skin);
    Q2F(scale); Q2F(alpha); Q2B(hand); Q2B(held); Q2B(armed); Q2B(visible); Q2B(gekk);
    Q2B(dodgeable); return true;
}
bool q2_save_actor(q2_save_io *io, qa_q2_actor_checkpoint *s) {
    Q2T(source_order); Q2T(extra_effects); Q2F(alpha); Q2B(lmctf_plasma_bounce);
    Q2T(combat_surprise_ns);
    Q2T(character_birth_epoch); Q2N(combat_life_owner); Q2T(combat_life_birth_epoch);
    Q2B(character_immortal); Q2B(character_no_damage_effects);
    Q2T(combat_death_ns); Q2B(combat_life_present); Q2B(combat_no_knockback);
    Q2B(combat_alive_knockback_only);
    Q2B(weapon_bound); Q2B(physics_bound);
    if (!weapon(io, &s->weapon) || !input(io, &s->input)) return false;
    Q2B(weapon_turn.attack); Q2B(weapon_turn.latched_attack); Q2B(weapon_turn.weapon_thunk);
    Q2U(weapon_turn.firing_weapon); Q2S(f64, weapon_turn.firing_credit);
    if (s->weapon_turn.firing_weapon >= QA_Q2_WEAPON_COUNT || s->weapon_turn.firing_credit < 0 ||
        s->weapon_turn.firing_credit >= 1 ||
        (s->weapon_turn.firing_weapon == QA_Q2_WEAPON_NONE && s->weapon_turn.firing_credit != 0))
        return q2_save_fail(io, "Q2 firing credit leaves its actual weapon turn");
    Q2I(silencer);
    if (!physics(io, &s->physics)) return false;
    Q2R(physics_enemy); Q2R(physics_goal);
    if (!projectile(io, &s->projectile)) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (!grapple(io, &s->grapples[i])) return false;
        Q2R(grapple_hooks[i]);
    }
    Q2B(hand_grenade_bound); return hand(io, &s->hand_grenade);
}
bool q2_save_runtime(q2_save_io *io, qa_q2_runtime_checkpoint *s) {
    if (io->level_only) {
        Q2T(actor_sequence); Q2T(now_ns); Q2T(frame_ns);
        Q2B(lmctf_plasma_quad); Q2U8(widow_damage_multiplier); Q2U8(widow_shot_phase);
        return true;
    }
    Q2U(edition); Q2U(product);
    Q2U(arsenal_rules); Q2B(native_hook); Q2U(hook_edition); Q2U(definition_count);
    Q2U(equipment_hook_rules); Q2U(equipment_hook_edition);
    if (s->definition_count >= QA_Q2_WEAPON_COUNT)
        return q2_save_fail(io,"Invalid Q2 source arsenal count");
    for (uint32_t i = 0; i < s->definition_count; ++i) Q2U(definition_order[i]);
    for (size_t i = 0; i < 31; ++i) Q2U(random.words[i]);
    Q2U8(random.front); Q2U8(random.rear); Q2T(random.draws);
    for (size_t i = 0; i < 624; ++i) Q2U(rerelease_words[i]);
    Q2U(rerelease_index); Q2T(rerelease_draws); Q2T(sequence); Q2T(actor_sequence);
    Q2T(now_ns); Q2T(frame_ns); Q2F(grapple_options.fly_speed);
    Q2F(grapple_options.pull_speed); Q2F(grapple_options.damage); Q2B(grapple_options.players_collide);
    Q2B(lmctf_plasma_quad); Q2U8(widow_damage_multiplier); Q2U8(widow_shot_phase);
    return true;
}
