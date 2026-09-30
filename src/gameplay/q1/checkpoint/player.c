#include "internal.h"

static bool input(q1_save_io *io, qa_q1_input *value) {
    Q1_SAVE(io, vector, value->view_angles);
    Q1_SAVE(io, bool, value->attack);
    Q1_SAVE(io, bool, value->jump);
    Q1_SAVE(io, bool, value->use);
    Q1_SAVE(io, bool, value->holstered);
    Q1_SAVE(io, u8, value->impulse);
    Q1_SAVE(io, u8, value->water_level);
    Q1_SAVE(io, i32, value->water_type);
    Q1_SAVE(io, float, value->teleport_until);
    return value->water_level <= 3 || q1_save_fail(io, "Invalid saved Q1 input water level");
}
static bool range(q1_save_io *io, qa_q1_frame_range *value) {
    return q1_save_u16(io, &value->first) && q1_save_u16(io, &value->count);
}
static bool character(q1_save_io *io, q1_character *value) {
    qa_q1_character_input *in = &value->input;
    Q1_SAVE(io, bool, in->axe_pose);
    Q1_SAVE(io, bool, in->attack);
    Q1_SAVE(io, bool, in->jump);
    Q1_SAVE(io, bool, in->use);
    Q1_SAVE(io, bool, in->invisible);
    Q1_SAVE(io, bool, in->invulnerable);
    Q1_SAVE(io, u8, in->water_level);
    Q1_SAVE(io, i32, in->water_type);
    if (in->water_level > 3)
        return q1_save_fail(io, "Invalid saved Q1 character water level");
    qa_q1_character_pose *pose = &value->pose;
    Q1_SAVE(io, bool, pose->custom_model);
    Q1_SAVE(io, bool, pose->source_frame);
    Q1_SAVE(io, bool, pose->axe_pose);
    Q1_SAVE(io, string, pose->model);
    Q1_SAVE(io, i32, pose->frame);
    if (!range(io, &pose->stand) || !range(io, &pose->run) || !range(io, &pose->pain) ||
        !range(io, &pose->death))
        return false;
    if (pose->custom_model && (!pose->model || !pose->stand.count || !pose->run.count ||
                               !pose->pain.count || !pose->death.count))
        return q1_save_fail(io, "Q1 saved character model has empty animation ranges");
    Q1_SAVE_ENUM(io, value->life, QA_Q1_RESPAWNABLE);
    Q1_SAVE(io, string, value->model);
    Q1_SAVE(io, i32, value->frame);
    if (!range(io, &value->animation))
        return false;
    Q1_SAVE(io, u16, value->animation_frame);
    Q1_SAVE(io, u16, value->walk_frame);
    Q1_SAVE(io, u8, value->locomotion);
    if (value->locomotion > 2)
        return q1_save_fail(io, "Invalid Q1 saved character locomotion");
    Q1_SAVE(io, bool, value->death_animation);
    Q1_SAVE(io, bool, value->attack_animation);
    Q1_SAVE(io, bool, value->in_water);
    Q1_SAVE(io, bool, value->weapon_hidden);
    Q1_SAVE(io, vector, value->view_offset);
    Q1_SAVE(io, deadline, value->next_animation);
    Q1_SAVE(io, double, value->pain_until);
    Q1_SAVE(io, double, value->air_until);
    Q1_SAVE(io, double, value->hazard_at);
    Q1_SAVE(io, float, value->fall_speed);
    Q1_SAVE(io, float, value->drown_damage);
    return true;
}
bool q1_save_player(q1_save_io *io, q1_player *player) {
    Q1_SAVE(io, owned_actor, player->id);
    if (!input(io, &player->input))
        return false;
    Q1_SAVE_ENUM(io, player->weapon, QA_Q1_WEAPON_COUNT - 1);
    Q1_SAVE(io, i32, player->weapon_frame);
    Q1_SAVE(io, i32, player->animation_base);
    if (player->animation_base > INT32_MAX - 5)
        return q1_save_fail(io, "Invalid Q1 checkpoint weapon animation base");
    Q1_SAVE(io, i32, player->nail_side);
    Q1_SAVE(io, double, player->attack_finished);
    Q1_SAVE(io, double, player->next_weapon_frame);
    Q1_SAVE(io, double, player->animation_at);
    Q1_SAVE(io, double, player->lightning_sound_at);
    Q1_SAVE(io, double, player->hostile_until);
    Q1_SAVE(io, double, player->mega_rot_at);
    Q1_SAVE(io, double, player->air_finished);
    Q1_SAVE(io, double, player->drown_at);
    Q1_SAVE(io, double, player->hazard_at);
    for (size_t i = 0; i < QA_Q1_POWER_COUNT; ++i) {
        Q1_SAVE(io, double, player->power_expires[i]);
        Q1_SAVE(io, double, player->power_flash[i]);
    }
    Q1_SAVE(io, double, player->scuba_at);
    Q1_SAVE(io, double, player->shield_until);
    Q1_SAVE(io, double, player->shield_sound_at);
    Q1_SAVE(io, u64, player->wetsuit_scaled_frame);
    Q1_SAVE(io, u16, player->power_warned);
    Q1_SAVE(io, u8, player->wetsuit_scaled_level);
    Q1_SAVE_ENUM(io, player->auto_switch, QA_Q1_SWITCH_NEVER);
    Q1_SAVE(io, u32, player->mg3_progress.health);
    Q1_SAVE(io, u32, player->mg3_progress.shells);
    Q1_SAVE(io, u32, player->mg3_progress.nails);
    Q1_SAVE(io, u32, player->mg3_progress.rockets);
    Q1_SAVE(io, u32, player->mg3_progress.cells);
    Q1_SAVE(io, u32, player->mg3_progress.bloody);
    Q1_SAVE(io, actor, player->mg3_hammer_target);
    Q1_SAVE(io, double, player->mg3_hammer_until);
    Q1_SAVE(io, double, player->horde_axe_chain_until);
    Q1_SAVE(io, u32, player->horde_axe_chain);
    Q1_SAVE(io, i32, player->mg3_hammer_body);
    Q1_SAVE(io, bool, player->mg3_infinite_ammo);
    Q1_SAVE(io, bool, player->mg3_hammer_glow);
    Q1_SAVE(io, actor, player->killer);
    Q1_SAVE(io, actor, player->hook);
    if (!input(io, &player->grapple_input))
        return false;
    Q1_SAVE(io, bool, player->grapple_release);
    Q1_SAVE(io, bool, player->grapple_pulling);
    Q1_SAVE(io, actor, player->grapple_weapon.animation);
    Q1_SAVE(io, double, player->grapple_weapon.attack_finished);
    Q1_SAVE(io, double, player->grapple_weapon.release_time);
    Q1_SAVE(io, i32, player->grapple_weapon.frame);
    Q1_SAVE(io, bool, player->grapple_weapon.selected);
    Q1_SAVE(io, bool, player->grapple_weapon.available);
    Q1_SAVE(io, float, player->max_health);
    Q1_SAVE(io, float, player->drown_damage);
    Q1_SAVE(io, vector, player->punch);
    Q1_SAVE(io, bool, player->continuous);
    Q1_SAVE(io, bool, player->arsenal);
    Q1_SAVE(io, bool, player->character);
    if (!character(io, &player->character_state))
        return false;
    if (player->wetsuit_scaled_level > 3)
        return q1_save_fail(io, "Invalid saved Q1 wetsuit water level");
    if (io->reading)
        player->active = true;
    return true;
}
