#include "internal.h"

static bool carry(q2_save_io *io, qa_q2_player_carry *s) {
    Q2F(health); Q2F(maximum_health); Q2U(armor.regular.kind);
    Q2F(armor.regular.points); Q2N(armor.regular.item);
    switch (s->armor.regular.kind) {
    case QA_ARMOR_NONE: case QA_ARMOR_SOURCE: break;
    case QA_ARMOR_Q1: Q2F(armor.regular.protection.q1_absorption); break;
    case QA_ARMOR_Q2:
        Q2F(armor.regular.protection.q2.normal); Q2F(armor.regular.protection.q2.energy); break;
    case QA_ARMOR_Q3: Q2F(armor.regular.protection.q3_protection); break;
    default: return q2_save_fail(io, "Invalid Q2 carry armor kind");
    }
    Q2U(armor.powered.kind); Q2F(armor.powered.cells);
    Q2N(armor.powered.source_owner); Q2U(armor.powered.source_edition);
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
    if (!q2_save_visual(io, &s->visual) || !carry(io, &s->coop) ||
        !q2_save_inventory(io, &s->spawn_inventory, &s->spawn_count) ||
        !q2_save_text(io, s->userinfo, sizeof(s->userinfo)) ||
        !q2_save_text(io, s->social_id, sizeof(s->social_id)) ||
        !q2_save_text(io, s->dogtag, sizeof(s->dogtag))) return false;
    Q2U(hand); Q2I(gender); Q2I(old_water); Q2I(drown_damage); Q2I(breather_sound);
    Q2I(animation_priority); Q2I(animation_end); Q2I(auto_switch); Q2I(auto_shield); Q2I(flashes);
    Q2U(buttons); Q2U(latched_buttons); Q2U(event);
    Q2T(entered_ns); Q2T(respawn_ns); Q2T(air_ns); Q2T(drown_ns); Q2T(pain_ns);
    Q2T(damage_ns); Q2T(power_armor_ns); Q2T(fall_ns); Q2T(landmark_noise_ns); Q2T(flood_until_ns);
    for (size_t i = 0; i < 10; ++i) Q2T(flood_times[i]);
    Q2T(slime_ns); Q2T(animation_ns); Q2T(last_damage_ns); Q2T(last_firing_ns);
    Q2T(invisibility_fade_ns); Q2T(tracker_ns); Q2T(nuke_ns); Q2T(flash_ns);
    Q2T(respawn_timeout_ns); Q2T(grapple_released_ns); Q2T(quake_ns);
    Q2T(help_draw_ns); Q2T(help_marker_ns); Q2T(mission_time_ns);
    Q2U(mission_primary); Q2U(mission_secondary); Q2U(mission_changed);
    Q2F(fov); Q2F(damage_blood); Q2F(damage_armor); Q2F(damage_power); Q2F(damage_knockback);
    Q2F(damage_alpha); Q2F(bonus_alpha); Q2F(damage_pitch); Q2F(damage_roll); Q2F(fall_value);
    Q2F(bob_time); Q2F(bob_move); Q2F(killer_yaw);
    Q2V(damage_from); Q2V(damage_blend); Q2V(old_velocity); Q2V(old_view_angles);
    Q2V(slow_view_angles); Q2V(help_location);
    void *points = s->help_points;
    if (!q2_save_count(io, &s->help_count, 12, sizeof(*s->help_points), &points)) return false;
    s->help_points = points;
    for (size_t i = 0; i < s->help_count; ++i) Q2V(help_points[i]);
    if (s->help_index > UINT32_MAX) return q2_save_fail(io, "Oversized Q2 help cursor");
    Q2U(help_index); s->help_capacity = s->help_count;
    Q2N(loop_sound); Q2N(help_image);
    if (!q2_save_fog(io, &s->fog) || !q2_save_fog(io, &s->wanted_fog)) return false;
    Q2F(fog_transition); Q2U(flood_count);
    Q2B(use_weapons); Q2B(use_inventory); Q2B(requested_spectator); Q2B(bot); Q2B(gibbed);
    Q2B(weapon_thunk); Q2B(animation_duck); Q2B(animation_run); Q2B(landmark_free_fall);
    Q2B(show_scores); Q2B(show_inventory); Q2B(show_help); Q2B(bob_skip); Q2B(nuke_inside);
    Q2B(auto_shield_enabled); Q2B(awaiting_respawn); Q2B(spawned); Q2B(player_collision);
    Q2B(has_coop); Q2B(has_pending_landmark); Q2B(squad_spawn); Q2B(corpse);
    if (!q2_save_landmark(io, &s->pending_landmark)) return false;
    Q2V(squad_origin); Q2V(squad_angles); return true;
}
bool q2_save_player(q2_save_io *io, qa_q2_player_checkpoint *s) {
    Q2U(version); Q2B(present);
    if (s->present && !state(io, &s->value)) return false;
    Q2R(chase_target); Q2R(noise[0]); Q2R(noise[1]); Q2R(sphere_camera); Q2R(landmark_player);
    return true;
}
bool q2_save_players(q2_save_io *io, qa_q2_players_checkpoint *s) {
    Q2U(version);
    for (size_t i = 0; i < 8; ++i) Q2R(corpses[i]);
    Q2R(landmark_player); Q2R(noise_owner[0]); Q2R(noise_owner[1]);
    Q2U(corpse_index); Q2U(death_animation); Q2U(pain_animation);
    Q2B(intermission); Q2B(exit); Q2B(camera_set); Q2B(has_landmark); Q2B(deadly_killbox);
    Q2U(intermission_flags); Q2T(intermission_ns); Q2T(fade_ns); Q2T(restart_ns); Q2N(next_map);
    if (!q2_save_landmark(io, &s->landmark)) return false;
    Q2V(camera_origin); Q2V(camera_angles);
    for (size_t i = 0; i < 2; ++i) { Q2V(noise[i].origin); Q2T(noise[i].time_ns); Q2B(noise[i].present); }
    Q2N(next_map_rule);Q2B(map_list_shuffle);
    void *maps=s->map_list;
    if(!q2_save_count(io,&s->map_list_count,4,sizeof(*s->map_list),&maps)) return false;
    s->map_list=maps;
    for(size_t i=0;i<s->map_list_count;++i) Q2N(map_list[i]);
    return true;
}
