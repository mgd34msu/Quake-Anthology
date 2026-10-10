#include "internal.h"

static bool carry(q2_save_io *io, qa_q2_player_carry *s) {
    Q2F(health); Q2F(maximum_health); Q2U(armor.regular.kind);
    if (!q2_save_f64(io, &s->armor.regular.points)) return false;
    Q2N(armor.regular.item);
    switch (s->armor.regular.kind) {
    case QA_ARMOR_NONE: case QA_ARMOR_SOURCE: break;
    case QA_ARMOR_Q1: Q2F(armor.regular.protection.q1_absorption); break;
    case QA_ARMOR_Q2:
        if (!q2_save_f64(io, &s->armor.regular.protection.q2.normal) ||
            !q2_save_f64(io, &s->armor.regular.protection.q2.energy)) return false;
        break;
    case QA_ARMOR_Q3: Q2F(armor.regular.protection.q3_protection); break;
    default: return q2_save_fail(io, "Invalid Q2 carry armor kind");
    }
    Q2U(armor.powered.kind);
    if (!q2_save_f64(io, &s->armor.powered.cells)) return false;
    Q2N(armor.powered.source_owner); Q2U(armor.powered.source_edition);
    Q2U(armor.powered.source_kind);
    if (!qa_armor_validate(&s->armor, io->error)) return false;
    if (!q2_save_inventory(io, &s->inventory, &s->count)) return false;
    Q2U(weapon); Q2N(selected_item); Q2I(score); Q2U(flags); Q2U(power_cubes); return true;
}
static bool state(q2_save_io *io, qa_q2_player_state *s) {
    Q2U(info.slot); Q2U(info.seat);
    if (!q2_save_text(io, s->info.name, sizeof(s->info.name)) ||
        !q2_save_text(io, s->info.skin, sizeof(s->info.skin))) return false;
    Q2I(info.score); Q2I(info.ping); Q2I(info.lives); Q2N(info.selected_item); Q2F(info.view_height);
    Q2B(info.connected); Q2B(info.spectator); Q2B(info.dead); Q2B(info.god);
    Q2B(info.notarget); Q2B(info.noclip); Q2B(info.flashlight);
    if (!q2_save_visual(io, &s->rule.visual) || !carry(io, &s->rule.coop) ||
        !q2_save_inventory(io, &s->rule.spawn_inventory, &s->rule.spawn_count) ||
        !q2_save_text(io, s->rule.userinfo, sizeof(s->rule.userinfo)) ||
        !q2_save_text(io, s->rule.social_id, sizeof(s->rule.social_id)) ||
        !q2_save_text(io, s->rule.dogtag, sizeof(s->rule.dogtag))) return false;
    Q2U(rule.hand); Q2I(rule.gender); Q2I(rule.old_water); Q2I(rule.drown_damage); Q2I(rule.breather_sound);
    Q2I(rule.animation_priority); Q2I(rule.animation_end); Q2I(rule.auto_switch); Q2I(rule.auto_shield); Q2I(rule.flashes);
    Q2S(i32, rule.hit_marker_damage);
    Q2U(rule.buttons); Q2U(rule.latched_buttons); Q2U(rule.event);
    Q2T(rule.entered_ns); Q2T(rule.respawn_ns); Q2T(rule.air_ns); Q2T(rule.drown_ns); Q2T(rule.pain_ns);
    Q2T(rule.damage_ns); Q2T(rule.power_armor_ns); Q2T(rule.fall_ns); Q2T(rule.landmark_noise_ns); Q2T(rule.flood_until_ns);
    for (size_t i = 0; i < 10; ++i) Q2T(rule.flood_times[i]);
    Q2T(rule.slime_ns); Q2T(rule.animation_ns); Q2T(rule.last_damage_ns); Q2T(rule.last_firing_ns);
    Q2T(rule.invisibility_fade_ns); Q2T(rule.tracker_ns); Q2T(rule.nuke_ns); Q2T(rule.flash_ns);
    Q2T(rule.respawn_timeout_ns); Q2T(rule.grapple_released_ns); Q2T(rule.quake_ns);
    Q2T(rule.help_draw_ns); Q2T(rule.help_marker_ns); Q2T(rule.mission_time_ns);
    Q2U(rule.mission_primary); Q2U(rule.mission_secondary); Q2U(rule.mission_changed);
    Q2F(rule.fov); Q2F(rule.damage_blood); Q2F(rule.damage_armor); Q2F(rule.damage_power); Q2F(rule.damage_knockback);
    Q2F(rule.damage_alpha); Q2F(rule.bonus_alpha); Q2F(rule.damage_pitch); Q2F(rule.damage_roll); Q2F(rule.fall_value);
    Q2F(rule.bob_time); Q2F(rule.bob_move); Q2F(rule.killer_yaw);
    Q2V(rule.damage_from); Q2V(rule.damage_blend); Q2V(rule.old_velocity); Q2V(rule.old_view_angles);
    Q2V(rule.slow_view_angles); Q2V(rule.help_location);
    void *points = s->rule.help_points;
    if (!q2_save_count(io, &s->rule.help_count, 12, sizeof(*s->rule.help_points), &points)) return false;
    s->rule.help_points = points;
    for (size_t i = 0; i < s->rule.help_count; ++i) Q2V(rule.help_points[i]);
    if (s->rule.help_index > UINT32_MAX) return q2_save_fail(io, "Oversized Q2 help cursor");
    Q2U(rule.help_index); s->rule.help_capacity = s->rule.help_count;
    Q2N(rule.loop_sound); Q2N(rule.help_image);
    Q2N(rule.character_model); Q2I(rule.character_skin); Q2B(rule.character_configured);
    if (!q2_save_fog(io, &s->rule.fog) || !q2_save_fog(io, &s->rule.wanted_fog)) return false;
    Q2F(rule.fog_transition); Q2U(rule.flood_count);
    Q2B(rule.use_weapons); Q2B(rule.use_inventory); Q2B(rule.requested_spectator); Q2B(bot); Q2B(rule.gibbed);
    Q2B(rule.weapon_thunk); Q2B(rule.animation_duck); Q2B(rule.animation_run); Q2B(rule.landmark_free_fall);
    Q2B(rule.show_scores); Q2B(rule.show_inventory); Q2B(rule.show_help); Q2B(rule.bob_skip); Q2B(rule.nuke_inside);
    Q2B(rule.auto_shield_enabled); Q2B(rule.awaiting_respawn); Q2B(rule.spawned); Q2B(rule.player_collision);
    Q2B(rule.has_coop); Q2B(rule.has_pending_landmark); Q2B(rule.squad_spawn); Q2B(rule.corpse);
    Q2B(rule.pending_start_items);
    Q2B(rule.sphere_vehicle);
    if (!q2_save_landmark(io, &s->rule.pending_landmark)) return false;
    Q2V(rule.squad_origin); Q2V(rule.squad_angles); return true;
}
bool q2_save_player(q2_save_io *io, qa_q2_player_checkpoint *s) {
    Q2B(present);
    if (s->present && !state(io, &s->value)) return false;
    Q2R(chase_target); Q2R(noise[0]); Q2R(noise[1]); Q2R(sphere_camera); Q2R(landmark_player);
    return true;
}
bool q2_save_players(q2_save_io *io, qa_q2_players_checkpoint *s) {
    for (size_t i = 0; i < 8; ++i) Q2R(corpses[i]);
    Q2R(landmark_player); Q2R(noise_owner[0]); Q2R(noise_owner[1]);
    Q2U(corpse_index); Q2U(death_animation); Q2U(pain_animation);
    Q2B(intermission); Q2B(exit); Q2B(camera_set); Q2B(has_landmark); Q2B(deadly_killbox);
    Q2U(intermission_flags); Q2T(intermission_ns); Q2T(fade_ns); Q2T(restart_ns); Q2N(next_map);
    if (!q2_save_landmark(io, &s->landmark)) return false;
    Q2V(camera_origin); Q2V(camera_angles);
    for (size_t i = 0; i < 2; ++i) { Q2V(noise[i].origin); Q2T(noise[i].time_ns); Q2B(noise[i].present); }
    if (io->level_only) return true;
    Q2N(next_map_rule);Q2B(map_list_shuffle);
    void *maps=s->map_list;
    if(!q2_save_count(io,&s->map_list_count,4,sizeof(*s->map_list),&maps)) return false;
    s->map_list=maps;
    for(size_t i=0;i<s->map_list_count;++i) Q2N(map_list[i]);
    return true;
}
