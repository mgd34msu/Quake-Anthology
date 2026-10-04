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
    Q1_SAVE(io, u64, value->birth_epoch);
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
    Q1_SAVE(io, float, player->current_ammo);
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
        Q1_SAVE(io, u64, player->power_order[i]);
    }
    Q1_SAVE(io, u64, player->power_sequence);
    for (size_t i = 0; i < QA_Q1_POWER_COUNT; ++i) {
        if (player->power_order[i] > player->power_sequence ||
            (!player->power_order[i] && player->power_expires[i] != 0))
            return q1_save_fail(io, "Invalid Q1 timed power insertion order");
        for (size_t j = 0; j < i; ++j)
            if (player->power_order[i] && player->power_order[i] == player->power_order[j])
                return q1_save_fail(io, "Duplicate Q1 timed power insertion order");
    }
    Q1_SAVE(io, double, player->scuba_at);
    Q1_SAVE(io, double, player->shield_until);
    Q1_SAVE(io, double, player->shield_sound_at);
    Q1_SAVE(io, u64, player->wetsuit_scaled_frame);
    Q1_SAVE(io, u16, player->power_warned);
    Q1_SAVE(io, u16, player->power_lost);
    if ((player->power_warned | player->power_lost) & ~((1u << QA_Q1_POWER_COUNT) - 1u))
        return q1_save_fail(io, "Invalid Q1 mission power timer state");
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
    Q1_SAVE(io, bool, player->primary_holstered);
    Q1_SAVE(io, bool, player->arsenal);
    Q1_SAVE(io, u64, player->weapon_definitions.serial);
    if (player->weapon_definitions.serial) {
        if (!player->arsenal)
            return q1_save_fail(io, "Q1 weapon definitions have no admitted arsenal");
        if (io->values.direction == QA_SOURCE_SAVE_READ) {
            player->weapon_definitions.actor = player->id;
            player->inventory_game = io->game;
        } else if (player->inventory_game != io->game ||
            !qa_actor_id_equal(player->weapon_definitions.actor, player->id) ||
            !qa_inventory_lease_current(io->game->services.inventory, player->weapon_definitions))
            return q1_save_fail(io, "Q1 weapon definition lease is no longer current");
    }
    Q1_SAVE(io, bool, player->character);
    Q1_SAVE(io, bool, player->source_client);
    Q1_SAVE(io, u32, player->client_slot);
    if (player->source_client ? player->client_slot >= io->game->options.max_clients
                              : player->client_slot != 0)
        return q1_save_fail(io, "Q1 source client continuation has an invalid admitted slot");
    Q1_SAVE(io, float, player->source_frags);
    Q1_SAVE(io, float, player->source_team);
    Q1_SAVE(io, bool, player->source_observer);
    Q1_SAVE(io, actor, player->source_spectator_goal);
    Q1_SAVE(io, actor, player->source_spectator_track);
    Q1_SAVE(io, u32, player->source_spectator_goal_ordinal);
    Q1_SAVE(io, u32, player->source_spectator_track_slot);
    if(player->source_spectator_track_slot>io->game->options.max_clients)
        return q1_save_fail(io,"Spectator tracker exceeds its actual physical client table");
    if((player->source_spectator_goal.registry || player->source_spectator_track.registry ||
        player->source_spectator_goal_ordinal || player->source_spectator_track_slot) &&
       (!player->source_client || !io->game->options.quakeworld))
        return q1_save_fail(io,"Spectator goal has no actual QW source client");
    Q1_SAVE(io, bool, player->source_no_target);
    Q1_SAVE(io, bool, player->source_god_mode);
    Q1_SAVE(io, i32, player->source_impulse);
    Q1_SAVE(io, bool, player->source_use);
    Q1_SAVE(io, bool, player->source_death_recorded);
    Q1_SAVE(io, bool, player->finale_held_present);
    Q1_SAVE(io, bool, player->finale_held);
    if ((!player->source_client || !io->game->finale_polled) && player->finale_held_present)
        return q1_save_fail(io, "Q1 finale button history has no genuine source client poll");
    if (!player->finale_held_present && player->finale_held)
        return q1_save_fail(io, "Q1 finale button history has no retained actor");
    Q1_SAVE(io, double, player->source_respawn_requested_at);
    uint32_t info_count=(io->values.direction == QA_SOURCE_SAVE_READ)?0:(uint32_t)player->source_info_count;
    if((io->values.direction == QA_SOURCE_SAVE_WRITE && player->source_info_count>UINT32_MAX) || !q1_save_u32(io,&info_count)) return false;
    if(io->values.direction == QA_SOURCE_SAVE_READ) {
        if(io->values.offset>io->values.input.size || info_count>(io->values.input.size-io->values.offset)/8 ||
           (info_count && sizeof(*player->source_info)>SIZE_MAX/info_count))
            return q1_save_fail(io,"Q1 source userinfo map exceeds its saved extent");
        q1_source_client_clear(player);
        if(info_count && !(player->source_info=calloc(info_count,sizeof(*player->source_info))))
            return q1_save_fail(io,"Allocating Q1 source userinfo continuation");
        player->source_info_count=info_count;
    }
    for(uint32_t i=0;i<info_count;++i) {
        Q1_SAVE(io,string,player->source_info[i].key);
        Q1_SAVE(io,string,player->source_info[i].value);
        qa_strings *strings=qa_session_strings(io->game->services.session);
        if(!qa_strings_cstr(strings,player->source_info[i].key) ||
           !qa_strings_cstr(strings,player->source_info[i].value))
            return q1_save_fail(io,"Q1 source userinfo key and value require actual C strings");
        for(uint32_t j=0;j<i;++j)
            if(player->source_info[j].key==player->source_info[i].key)
                return q1_save_fail(io,"Duplicate Q1 source userinfo key");
    }
    if(!player->source_client && (info_count || player->source_frags!=0 || player->source_team!=0 ||
        player->source_observer || player->source_no_target || player->source_god_mode || player->source_impulse ||
        player->source_use || player->source_death_recorded || player->source_respawn_requested_at!=0))
        return q1_save_fail(io,"Unadmitted Q1 player has source client state");
    if (!character(io, &player->character_state))
        return false;
    if (player->character && !player->character_state.birth_epoch)
        return q1_save_fail(io, "Q1 character continuation has no semantic birth");
    if (player->wetsuit_scaled_level > 3)
        return q1_save_fail(io, "Invalid saved Q1 wetsuit water level");
    if (io->values.direction == QA_SOURCE_SAVE_READ)
        player->active = true;
    return true;
}
