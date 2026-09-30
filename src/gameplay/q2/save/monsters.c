#include "internal.h"

bool q2_save_monster(q2_save_io *io, qa_q2_monster_checkpoint *s) {
    Q2U(version);
    if (!q2_save_text(io, s->definition, sizeof(s->definition)) ||
        !q2_save_text(io, s->move, sizeof(s->move)) ||
        !q2_save_text(io, s->next_move, sizeof(s->next_move))) return false;
    Q2U(spawnflags); Q2U(attack_state); Q2U(spawned_by); Q2U(controller_kind);
    Q2U(start_phase); Q2N(combat_target); Q2N(weapon_sound); Q2T(start_due_ns); Q2B(death_notified);
    Q2I(frame); Q2I(next_frame); Q2I(old_frame); Q2I(skin); Q2I(style); Q2I(count);
    Q2U(render_flags); Q2F(entity_scale); Q2F(animation_scale); Q2F(base_health);
    Q2F(health_scaling); Q2F(max_health); Q2F(max_power_armor); Q2U(initial_power_armor);
    Q2U(medic_tries); Q2F(gib_health); Q2F(normal_height); Q2F(view_height);
    Q2F(ideal_yaw); Q2F(yaw_speed); Q2F(blind_fire_delay); Q2F(fly_min_distance);
    Q2F(fly_max_distance); Q2F(fly_acceleration); Q2F(fly_speed);
    Q2T(next_frame_ns); Q2T(pause_ns); Q2T(idle_ns); Q2T(pain_ns); Q2T(fire_ns);
    Q2T(duck_ns); Q2T(next_duck_ns); Q2T(dodge_ns); Q2T(attack_ns); Q2T(check_attack_ns);
    Q2T(strafe_ns); Q2T(melee_ns); Q2T(search_ns); Q2T(trail_ns); Q2T(hostile_ns);
    Q2T(air_ns); Q2T(environment_ns); Q2T(jump_ns); Q2T(flies_ns); Q2T(fly_position_ns);
    Q2T(recovery_ns); Q2T(death_ns); Q2T(spawn_ns); Q2T(timestamp_ns); Q2T(coop_check_ns);
    Q2T(react_ns); Q2T(widow_powers.quad_until_ns); Q2T(widow_powers.double_until_ns);
    Q2T(widow_powers.invulnerability_until_ns); Q2T(widow_powers.quad_fire_until_ns);
    Q2V(widow_previous_target); Q2U(corpse_phase); Q2T(corpse_due_ns); Q2T(corpse_end_ns);
    Q2R(enemy); Q2R(old_enemy); Q2R(goal); Q2R(move_target); Q2R(last_player_enemy);
    Q2R(commander); Q2R(activator); Q2R(resurrect_target); Q2R(hazard); Q2R(proboscis);
    Q2R(healer); Q2R(bad_medic[0]); Q2R(bad_medic[1]); Q2R(controller_owner);
    Q2R(controller_target); Q2R(sound_target.actor); Q2R(sound_target.owner);
    Q2V(sound_target.origin); Q2T(sound_target.time_ns); Q2B(sound_target.present);
    Q2V(last_sighting); Q2V(saved_goal); Q2V(blind_fire_target); Q2V(fly_ideal_position);
    Q2V(fly_recovery_direction); Q2V(last_damage_point); Q2V(saved_attack_position);
    Q2V(controller_direction);
    if (!q2_save_attack(io, &s->last_attack)) return false;
    Q2R(attack_attacker); Q2R(attack_inflictor); Q2R(attack_projectile);
    Q2F(pending_damage); Q2F(pending_kick); Q2F(controller_damage); Q2T(controller_ns);
    Q2S(i64, monster_slots); Q2S(i64, monster_used); Q2I(water_level); Q2I(water_type);
    Q2B(has_summons); Q2I(summon_strength); Q2U(summon_count);
    if (s->summon_count > 5) return q2_save_fail(io, "Invalid Q2 reinforcement count");
    for (size_t i = 0; i < s->summon_count; ++i) {
        if (!q2_save_text(io, s->summons[i].classname, sizeof(s->summons[i].classname))) return false;
        Q2I(summons[i].strength);
        Q2V(summons[i].bounds.mins); Q2V(summons[i].bounds.maxs);
    }
    Q2N(reinforcement_source); Q2B(reinforcements_configured);
    void *reinforcements = s->reinforcements;
    if (!q2_save_count(io, &s->reinforcement_count, 32,
                       sizeof(*s->reinforcements), &reinforcements)) return false;
    s->reinforcements = reinforcements;
    for (size_t i = 0; i < s->reinforcement_count; ++i) {
        if (!q2_save_text(io, s->reinforcements[i].classname,
                           sizeof(s->reinforcements[i].classname))) return false;
        Q2I(reinforcements[i].strength);
        Q2V(reinforcements[i].bounds.mins); Q2V(reinforcements[i].bounds.maxs);
    }
    Q2T(last_link_count); Q2B(has_saved_goal); Q2B(good_guy); Q2B(target_anger);
    Q2B(ignore_shots); Q2B(do_not_count); Q2B(source_blocked); Q2B(brutal); Q2B(medic);
    Q2B(resurrecting); Q2B(can_take_damage); Q2B(dead); Q2B(corpse); Q2B(gibbed);
    Q2B(stand_ground); Q2B(temporary_stand_ground); Q2B(hold_frame); Q2B(ducked); Q2B(dodging);
    Q2B(charging); Q2B(manual_steering); Q2B(combat_point); Q2B(lefty); Q2B(had_visibility);
    Q2B(close_sight_tripped); Q2B(lost_sight); Q2B(pursue_next); Q2B(pursue_temporary);
    Q2B(pursuit_last_seen); Q2B(cocked); Q2B(force_refire); Q2B(triggered); Q2B(visible);
    Q2B(pending_pain); Q2B(pending_death); Q2B(alternate_fly); Q2B(fly_buzzard); Q2B(fly_above);
    Q2B(fly_pinned); Q2B(fly_thrusters); Q2B(hint_path); Q2B(summoned); Q2B(touch_active);
    Q2B(turret_attached); Q2B(initialized); Q2B(controller_medic); Q2B(controller_fired);
    return true;
}
bool q2_save_monsters(q2_save_io *io, qa_q2_monsters_checkpoint *s) {
    Q2U(version); Q2R(sight_client); Q2R(sight_observer); Q2T(sight_time_ns);
    Q2T(last_frame_ns); Q2B(began_frame);
    void *trails = s->trails;
    if (!q2_save_count(io, &s->trail_count, 21, sizeof(*s->trails), &trails)) return false;
    s->trails = trails;
    for (size_t i = 0; i < s->trail_count; ++i) {
        qa_q2_monster_trail_checkpoint *trail = s->trails + i;
        if (!q2_save_ref(io, &trail->actor) || !q2_save_vec(io, &trail->previous_origin) ||
            !q2_save_bool(io, &trail->has_previous)) return false;
        uint32_t count = (uint32_t)trail->count;
        if ((!io->reading && trail->count > QA_Q2_MONSTER_TRAIL_POINTS) ||
            !q2_save_u32(io, &count) || count > QA_Q2_MONSTER_TRAIL_POINTS)
            return q2_save_fail(io, "Invalid Q2 trail point count");
        trail->count = count;
        for (size_t j = 0; j < trail->count; ++j)
            if (!q2_save_vec(io, &trail->points[j].origin) ||
                !q2_save_u64(io, &trail->points[j].time_ns) ||
                !q2_save_f32(io, &trail->points[j].yaw)) return false;
    }
    void *alerts = s->alerts;
    if (!q2_save_count(io, &s->alert_count, 18, sizeof(*s->alerts), &alerts)) return false;
    s->alerts = alerts;
    for (size_t i = 0; i < s->alert_count; ++i) {
        Q2R(alerts[i].player); Q2R(alerts[i].observer);
        Q2T(alerts[i].time_ns); Q2T(alerts[i].hostile_ns);
    }
    return true;
}
