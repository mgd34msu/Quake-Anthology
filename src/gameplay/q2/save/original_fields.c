#include "original_internal.h"

/* Classic member offsets are from the original g_local.h compiled for the
 * MSVC x86 ABI. Rerelease member names are the donor FIELD(f) keys, including
 * literal dots; nested ps/pers objects are selected by the owning traversal. */
#define MEMBER(TYPE, MEMBER) offsetof(TYPE, MEMBER)

static const q2_original_field persistent_fields[] = {
    {"userinfo", MEMBER(qa_q2_player_state, userinfo), sizeof(((qa_q2_player_state *)0)->userinfo), Q2_ORIGINAL_TEXT, {188, 188, 188}},
    {"social_id", MEMBER(qa_q2_player_state, social_id), sizeof(((qa_q2_player_state *)0)->social_id), Q2_ORIGINAL_TEXT, {65535, 65535, 65535}},
    {"netname", MEMBER(qa_q2_player_state, info.name), sizeof(((qa_q2_player_state *)0)->info.name), Q2_ORIGINAL_TEXT, {700, 700, 700}},
    {"hand", MEMBER(qa_q2_player_state, hand), 1, Q2_ORIGINAL_I32, {716, 716, 716}},
    {"score", MEMBER(qa_q2_player_state, info.score), 1, Q2_ORIGINAL_I32, {1800, 1808, 1800}},
    {"game_help1changed", MEMBER(qa_q2_player_state, mission_primary), 1, Q2_ORIGINAL_U32, {1804, 1812, 1804}},
    {"game_help2changed", MEMBER(qa_q2_player_state, mission_secondary), 1, Q2_ORIGINAL_U32, {65535, 65535, 65535}},
    {"helpchanged", MEMBER(qa_q2_player_state, mission_changed), 1, Q2_ORIGINAL_U32, {1808, 1816, 1808}},
    {"help_time", MEMBER(qa_q2_player_state, mission_time_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
    {"spectator", MEMBER(qa_q2_player_state, info.spectator), 1, Q2_ORIGINAL_BOOL, {1812, 1820, 1812}},
    {"lives", MEMBER(qa_q2_player_state, info.lives), 1, Q2_ORIGINAL_I32, {65535, 65535, 65535}},
};

static const q2_original_field view_fields[] = {
    {"viewangles", MEMBER(qa_q2_player_view, angles), 1, Q2_ORIGINAL_VECTOR, {28, 28, 28}},
    {"viewoffset", MEMBER(qa_q2_player_view, offset), 1, Q2_ORIGINAL_VECTOR, {40, 40, 40}},
    {"gunangles", MEMBER(qa_q2_player_view, gun_angles), 1, Q2_ORIGINAL_VECTOR, {64, 64, 64}},
    {"gunoffset", MEMBER(qa_q2_player_view, gun_offset), 1, Q2_ORIGINAL_VECTOR, {76, 76, 76}},
    {"fov", MEMBER(qa_q2_player_view, fov), 1, Q2_ORIGINAL_F32, {112, 112, 112}},
};

static const q2_original_field client_fields[] = {
    {"resp.entertime", MEMBER(qa_q2_player_state, entered_ns), 1, Q2_ORIGINAL_FRAME_INDEX, {3444, 3460, 3476}},
    {"resp.score", MEMBER(qa_q2_player_state, info.score), 1, Q2_ORIGINAL_I32, {3448, 3464, 3480}},
    {"resp.spectator", MEMBER(qa_q2_player_state, info.spectator), 1, Q2_ORIGINAL_BOOL, {3464, 3480, 3496}},
    {"killer_yaw", MEMBER(qa_q2_player_state, killer_yaw), 1, Q2_ORIGINAL_F32, {3564, 3580, 3596}},
    {"quake_time", MEMBER(qa_q2_player_state, quake_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
    {"v_dmg_roll", MEMBER(qa_q2_player_state, damage_roll), 1, Q2_ORIGINAL_F32, {3596, 3612, 3628}},
    {"v_dmg_pitch", MEMBER(qa_q2_player_state, damage_pitch), 1, Q2_ORIGINAL_F32, {3600, 3616, 3632}},
    {"v_dmg_time", MEMBER(qa_q2_player_state, damage_ns), 1, Q2_ORIGINAL_TIME, {3604, 3620, 3636}},
    {"fall_time", MEMBER(qa_q2_player_state, fall_ns), 1, Q2_ORIGINAL_TIME, {3608, 3624, 3640}},
    {"fall_value", MEMBER(qa_q2_player_state, fall_value), 1, Q2_ORIGINAL_F32, {3612, 3628, 3644}},
    {"damage_alpha", MEMBER(qa_q2_player_state, damage_alpha), 1, Q2_ORIGINAL_F32, {3616, 3632, 3648}},
    {"bonus_alpha", MEMBER(qa_q2_player_state, bonus_alpha), 1, Q2_ORIGINAL_F32, {3620, 3636, 3652}},
    {"damage_blend", MEMBER(qa_q2_player_state, damage_blend), 1, Q2_ORIGINAL_VECTOR, {3624, 3640, 3656}},
    {"bobtime", MEMBER(qa_q2_player_state, bob_time), 1, Q2_ORIGINAL_F32, {3648, 3664, 3680}},
    {"oldviewangles", MEMBER(qa_q2_player_state, old_view_angles), 1, Q2_ORIGINAL_VECTOR, {3652, 3668, 3684}},
    {"oldvelocity", MEMBER(qa_q2_player_state, old_velocity), 1, Q2_ORIGINAL_VECTOR, {3664, 3680, 3696}},
    {"next_drown_time", MEMBER(qa_q2_player_state, drown_ns), 1, Q2_ORIGINAL_TIME, {3676, 3692, 3708}},
    {"old_waterlevel", MEMBER(qa_q2_player_state, old_water), 1, Q2_ORIGINAL_I32, {3680, 3696, 3712}},
    {"breather_sound", MEMBER(qa_q2_player_state, breather_sound), 1, Q2_ORIGINAL_I32, {3684, 3700, 3716}},
    {"anim_end", MEMBER(qa_q2_player_state, animation_end), 1, Q2_ORIGINAL_I32, {3692, 3708, 3724}},
    {"anim_priority", MEMBER(qa_q2_player_state, animation_priority), 1, Q2_ORIGINAL_I32, {3696, 3712, 3728}},
    {"anim_duck", MEMBER(qa_q2_player_state, animation_duck), 1, Q2_ORIGINAL_BOOL, {3700, 3716, 3732}},
    {"anim_run", MEMBER(qa_q2_player_state, animation_run), 1, Q2_ORIGINAL_BOOL, {3704, 3720, 3736}},
    {"respawn_time", MEMBER(qa_q2_player_state, respawn_ns), 1, Q2_ORIGINAL_TIME, {3792, 3820, 3824}},
    {"nuke_time", MEMBER(qa_q2_player_state, nuke_ns), 1, Q2_ORIGINAL_FRAME_TIME, {65535, 65535, 3844}},
    {"tracker_pain_time", MEMBER(qa_q2_player_state, tracker_ns), 1, Q2_ORIGINAL_FRAME_TIME, {65535, 65535, 3848}},
    {"landmark_free_fall", MEMBER(qa_q2_player_state, landmark_free_fall), 1, Q2_ORIGINAL_BOOL, {65535, 65535, 65535}},
    {"landmark_noise_time", MEMBER(qa_q2_player_state, landmark_noise_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
    {"invisibility_fade_time", MEMBER(qa_q2_player_state, invisibility_fade_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
    {"last_firing_time", MEMBER(qa_q2_player_state, last_firing_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
};

static const q2_original_field weapon_fields[] = {
    {"weaponstate", MEMBER(qa_q2_weapon_state, phase), 1, Q2_ORIGINAL_WEAPON_PHASE, {3568, 3584, 3600}},
    {"machinegun_shots", MEMBER(qa_q2_weapon_state, machinegun_shots), 1, Q2_ORIGINAL_I32, {3688, 3704, 3720}},
    {"kick.angles", MEMBER(qa_q2_weapon_state, kick_angles), 1, Q2_ORIGINAL_VECTOR, {3572, 3588, 3604}},
    {"kick.origin", MEMBER(qa_q2_weapon_state, kick_origin), 1, Q2_ORIGINAL_VECTOR, {3584, 3600, 3616}},
    {"kick.total", MEMBER(qa_q2_weapon_state, kick_seconds), 1, Q2_ORIGINAL_SECONDS_TIME, {65535, 65535, 65535}},
    {"kick.time", MEMBER(qa_q2_weapon_state, kick_until_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
    {"grenade_blew_up", MEMBER(qa_q2_weapon_state, grenade_blew_up), 1, Q2_ORIGINAL_BOOL, {3724, 3740, 3756}},
    {"grenade_time", MEMBER(qa_q2_weapon_state, grenade_ns), 1, Q2_ORIGINAL_TIME, {3728, 3744, 3760}},
    {"grenade_finished_time", MEMBER(qa_q2_weapon_state, grenade_finished_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
    {"empty_click_sound", MEMBER(qa_q2_weapon_state, empty_sound_ns), 1, Q2_ORIGINAL_TIME, {65535, 65535, 65535}},
};

static const q2_original_field powers_fields[] = {
    {"quad_time", MEMBER(qa_q2_powerups, quad_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {3708, 3724, 3740}},
    {"invincible_time", MEMBER(qa_q2_powerups, invulnerability_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {3712, 3728, 3744}},
    {"breather_time", MEMBER(qa_q2_powerups, breather_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {3716, 3732, 3748}},
    {"enviro_time", MEMBER(qa_q2_powerups, enviro_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {3720, 3736, 3752}},
    {"invisible_time", MEMBER(qa_q2_powerups, invisibility_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {65535, 65535, 65535}},
    {"quadfire_time", MEMBER(qa_q2_powerups, quad_fire_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {65535, 3748, 65535}},
    {"double_time", MEMBER(qa_q2_powerups, double_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {65535, 65535, 3836}},
    {"ir_time", MEMBER(qa_q2_powerups, ir_until_ns), 1, Q2_ORIGINAL_FRAME_TIME, {65535, 65535, 3840}},
};

static const q2_original_field entity_fields[] = {
    {"spawnflags", MEMBER(qa_q2_entity_state, spawnflags), 1, Q2_ORIGINAL_U32, {284, 284, 284}},
    {"speed", MEMBER(qa_q2_entity_state, speed), 1, Q2_ORIGINAL_F32, {328, 328, 328}},
    {"accel", MEMBER(qa_q2_entity_state, accel), 1, Q2_ORIGINAL_F32, {332, 332, 332}},
    {"decel", MEMBER(qa_q2_entity_state, decel), 1, Q2_ORIGINAL_F32, {336, 336, 336}},
    {"wait", MEMBER(qa_q2_entity_state, wait), 1, Q2_ORIGINAL_F32, {592, 592, 592}},
    {"delay", MEMBER(qa_q2_entity_state, delay), 1, Q2_ORIGINAL_F32, {596, 596, 596}},
    {"random", MEMBER(qa_q2_entity_state, random), 1, Q2_ORIGINAL_F32, {600, 600, 600}},
    {"movedir", MEMBER(qa_q2_entity_state, direction), 1, Q2_ORIGINAL_VECTOR, {340, 340, 340}},
    {"nextthink", MEMBER(qa_q2_entity_state, due_ns), 1, Q2_ORIGINAL_TIME, {428, 428, 428}},
    {"volume", MEMBER(qa_q2_entity_state, volume), 1, Q2_ORIGINAL_F32, {584, 584, 584}},
    {"attenuation", MEMBER(qa_q2_entity_state, attenuation), 1, Q2_ORIGINAL_F32, {588, 588, 588}},
    {"count", MEMBER(qa_q2_entity_state, count), 1, Q2_ORIGINAL_I32, {532, 532, 532}},
    {"style", MEMBER(qa_q2_entity_state, style), 1, Q2_ORIGINAL_I32, {644, 644, 644}},
};

static const q2_original_field mover_fields[] = {
    {"pos1", MEMBER(q2_mover, start), 1, Q2_ORIGINAL_VECTOR, {352, 352, 352}},
    {"pos2", MEMBER(q2_mover, end), 1, Q2_ORIGINAL_VECTOR, {364, 364, 364}},
    {"moveinfo.distance", MEMBER(q2_mover, distance), 1, Q2_ORIGINAL_F32, {724, 724, 724}},
    {"moveinfo.dir", MEMBER(q2_mover, motion.direction), 1, Q2_ORIGINAL_VECTOR, {736, 736, 736}},
    {"moveinfo.current_speed", MEMBER(q2_mover, motion.current_speed), 1, Q2_ORIGINAL_F32, {748, 748, 748}},
    {"moveinfo.move_speed", MEMBER(q2_mover, motion.move_speed), 1, Q2_ORIGINAL_F32, {752, 752, 752}},
    {"moveinfo.next_speed", MEMBER(q2_mover, motion.next_speed), 1, Q2_ORIGINAL_F32, {756, 756, 756}},
    {"moveinfo.remaining_distance", MEMBER(q2_mover, motion.remaining), 1, Q2_ORIGINAL_F32, {760, 760, 760}},
    {"moveinfo.decel_distance", MEMBER(q2_mover, motion.decel_distance), 1, Q2_ORIGINAL_F32, {764, 764, 764}},
};

static const q2_original_field game_fields[] = {
    {"helpmessage1", MEMBER(q2_original_game_state, help[0]), 512, Q2_ORIGINAL_TEXT, {0, 0, 0}},
    {"helpmessage2", MEMBER(q2_original_game_state, help[1]), 512, Q2_ORIGINAL_TEXT, {512, 512, 512}},
    {"help1changed", MEMBER(q2_original_game_state, help_changes[0]), 1, Q2_ORIGINAL_U32, {1024, 1024, 1024}},
    {"help2changed", MEMBER(q2_original_game_state, help_changes[1]), 1, Q2_ORIGINAL_U32, {65535, 65535, 65535}},
    {"spawnpoint", MEMBER(q2_original_game_state, spawnpoint), 512, Q2_ORIGINAL_TEXT, {1032, 1032, 1032}},
    {"maxclients", MEMBER(q2_original_game_state, clients), 1, Q2_ORIGINAL_U32, {1544, 1544, 1544}},
    {"maxentities", MEMBER(q2_original_game_state, entities), 1, Q2_ORIGINAL_U32, {1548, 1548, 1548}},
    {"cross_level_flags", MEMBER(q2_original_game_state, level_flags), 1, Q2_ORIGINAL_U32, {1552, 1552, 1552}},
    {"cross_unit_flags", MEMBER(q2_original_game_state, unit_flags), 1, Q2_ORIGINAL_U32, {65535, 65535, 65535}},
    {"num_items", MEMBER(q2_original_game_state, items), 1, Q2_ORIGINAL_U32, {1556, 1556, 1556}},
    {"autosaved", MEMBER(q2_original_game_state, autosave), 1, Q2_ORIGINAL_BOOL, {1560, 1560, 1560}},
};

static const q2_original_layout layouts[] = {
    {sizeof(qa_q2_player_state), persistent_fields, sizeof(persistent_fields) / sizeof(persistent_fields[0])},
    {sizeof(qa_q2_player_view), view_fields, sizeof(view_fields) / sizeof(view_fields[0])},
    {sizeof(qa_q2_player_state), client_fields, sizeof(client_fields) / sizeof(client_fields[0])},
    {sizeof(qa_q2_weapon_state), weapon_fields, sizeof(weapon_fields) / sizeof(weapon_fields[0])},
    {sizeof(qa_q2_powerups), powers_fields, sizeof(powers_fields) / sizeof(powers_fields[0])},
    {sizeof(qa_q2_entity_state), entity_fields, sizeof(entity_fields) / sizeof(entity_fields[0])},
    {sizeof(q2_mover), mover_fields, sizeof(mover_fields) / sizeof(mover_fields[0])},
    {sizeof(q2_original_game_state), game_fields, sizeof(game_fields) / sizeof(game_fields[0])},
};

const q2_original_layout *q2_original_layout_for(q2_original_record_kind kind)
{
    return (unsigned)kind < sizeof(layouts) / sizeof(layouts[0]) ? &layouts[kind] : NULL;
}

size_t q2_original_client_size(qa_q2_product product)
{
    static const size_t sizes[] = {3804, 3832, 3856};
    return (unsigned)product < sizeof(sizes) / sizeof(sizes[0]) ? sizes[product] : 0;
}

size_t q2_original_entity_size(qa_q2_product product)
{
    static const size_t sizes[] = {892, 896, 1044};
    return (unsigned)product < sizeof(sizes) / sizeof(sizes[0]) ? sizes[product] : 0;
}

size_t q2_original_level_size(qa_q2_product product)
{
    return product == QA_Q2_ROGUE ? 312 : (unsigned)product <= QA_Q2_XATRIX ? 304 : 0;
}
