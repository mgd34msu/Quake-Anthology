#include "player/internal.h"
#include "qa/game_q2_bots.h"

bool qa_q2_bot_arsenal_configuration_read(const qa_q2_game *game,qa_q2_bot_arsenal_configuration *out,qa_error *error) {
    if(!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot arsenal requires its actual source configuration");return false;}
    *out=(qa_q2_bot_arsenal_configuration){game->options.edition,game->options.deathmatch};return true;
}
bool qa_q2_bot_arsenal_rules_read(const qa_q2_game *game,qa_q2_weapon_rules *out,qa_error *error) {
    if (!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 arsenal rules require their source owner");return false;}
    *out=game->arsenal_rules;return true;
}
bool qa_q2_bot_arsenal_definition_count(const qa_q2_game *game,uint32_t *out,qa_error *error) {
    if (!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 arsenal count requires its source owner");return false;}
    *out=game->definition_count;return true;
}
bool qa_q2_bot_arsenal_definition_read(const qa_q2_game *game,uint32_t ordinal,
                                      const qa_q2_weapon_definition **out,qa_error *error) {
    if (!game || !out || ordinal>=game->definition_count) {qa_error_set(error,QA_ERROR_ARGUMENT,ordinal,"Q2 arsenal ordinal is outside its source registry");return false;}
    *out=game->definitions+game->definition_order[ordinal];return true;
}

qa_vec3 qa_q2_bot_weapon_launch_velocity(const qa_q2_bot_weapon_fact *fact, qa_vec3 angles) {
    qa_vec3 direction;
    angles.z = 0;
    angles.y += fact->launch_yaw_offset;
    if (fact->pitch_clamped) angles.x = q2_launch_pitch(angles.x);
    qa_builtin_angle_vectors(angles, &direction, NULL, NULL);
    return fact->ballistic && fact->deployable ?
        q2_mine_velocity(direction, fact->speed, fact->extra_z_velocity, 0) :
        q2_launch_velocity(direction, fact->speed, fact->extra_z_velocity, 0);
}

bool qa_q2_bot_weapon_read(qa_q2_game *game, qa_actor_id id, qa_q2_weapon weapon,
                           qa_q2_bot_weapon_fact *out, bool *found, qa_error *error) {
    if (!game || !out || !found || weapon <= QA_Q2_WEAPON_NONE || weapon >= QA_Q2_WEAPON_COUNT) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 weapon observation requires an actual arsenal and weapon");
        return false;
    }
    *found = false;
    *out = (qa_q2_bot_weapon_fact){0};
    const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(game, weapon);
    if (!definition) return true;
    q2_actor *actor = q2_actor_get(game, id, false, error);
    if (!actor) return false;
    if (!actor->weapon_bound) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Q2 weapon observation actor has no actual arsenal");
        return false;
    }
    qa_q2_weapon_state state = actor->weapon;
    bool firing = state.weapon == weapon && state.phase == QA_Q2_FIRING;
    if (!firing) {
        state.weapon = weapon;
        state.phase = QA_Q2_FIRING;
        for (state.frame = 0; state.frame < 64; ++state.frame)
            if (q2_frame_bit(definition->fires, state.frame)) break;
    }
    q2_weapon_call call = {.game = game, .actor = actor, .state = &state, .input = actor->input,
        .definition = definition, .now_ns = game->now_ns, .frame_ns = game->frame_ns,
        .rerelease = game->options.edition == QA_Q2_RERELEASE};
    bool throwing = weapon == QA_Q2_GRENADES || weapon == QA_Q2_TRAP || weapon == QA_Q2_TESLA;
    q2_shot_spec spec;
    if (throwing) {
        qa_combat_state combat;
        if (!qa_combat_read(game->services.combat, id, &combat, error)) return false;
        float fuse = firing && state.grenade_ns ?
            (float)(((double)state.grenade_ns - (double)game->now_ns) / 1e9) :
            (float)((double)q2_throw_cook_ns() / 1e9);
        q2_throw_spec(&call, fuse, combat.health > 0, &spec);
    } else if (!q2_weapon_shot_spec(&call, &spec)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Registered Q2 weapon has no authentic firing observation");
        return false;
    }
    int owned_count;
    if (!q2_count(game, id, game->items[weapon], &owned_count, error)) return false;
    *out = (qa_q2_bot_weapon_fact){.item = game->items[weapon], .ammo = game->ammo[weapon],
        .damage = spec.damage + (spec.damage_random > 0 ? (spec.damage_random - 1) * 0.5 : 0),
        .splash_damage = spec.splash, .effect_damage = spec.effect_damage,
        .speed = spec.speed, .range = spec.range, .radius = spec.radius, .effect_radius = spec.effect_radius,
        .fuse = spec.fuse, .shots = spec.shots, .ammo_per_shot = definition->quantity,
        .spread_x = spec.spread_x, .spread_y = spec.spread_y,
        .spread_degrees_x = spec.spread_degrees_x, .offset = spec.offset, .launch_yaw_offset = spec.yaw_offset,
        .owned = owned_count > 0, .melee = spec.melee, .ballistic = spec.ballistic,
        .homing = spec.homing, .deployable = spec.deployable, .grapple = spec.grapple,
        .conditional = spec.conditional,
        .range_from_bounds = spec.melee && call.rerelease,
        .timed_detonation = weapon == QA_Q2_GRENADES || weapon == QA_Q2_GRENADELAUNCHER,
        .requires_release = throwing,
        .pitch_clamped = call.rerelease && (throwing || weapon == QA_Q2_GRENADELAUNCHER || weapon == QA_Q2_PROXLAUNCHER)};
    if (weapon == QA_Q2_CHAINGUN) out->ammo_per_shot = spec.shots;
    if (weapon == QA_Q2_PHALANX && state.frame != 8) out->ammo_per_shot = 0;
    if (weapon == QA_Q2_TRAP) {
        out->effect_damage = q2_trap_capture_damage();
        out->damage = 0;
        if (call.rerelease || spec.fuse > 0) out->splash_damage = 0;
    } else if (weapon == QA_Q2_TESLA) {
        out->effect_damage = spec.damage;
        out->effect_radius = spec.radius;
        out->damage = 0;
    } else if (weapon == QA_Q2_PROXLAUNCHER) out->damage = 0;
    if (weapon == QA_Q2_BFG) {
        out->effect_damage = spec.damage;
        out->effect_radius = spec.radius;
        out->damage = out->splash_damage = q2_bfg_impact_damage();
        out->radius = q2_bfg_impact_radius();
    }
    out->required_ammo = definition->quantity;
    bool infinite = q2_infinite_ammo(&call);
    bool reserved = weapon == QA_Q2_GRENADES && actor->weapon.weapon == weapon &&
        actor->weapon.hand_reservation != QA_Q2_HAND_UNRESERVED;
    if (weapon == QA_Q2_GRENADES && (reserved || infinite)) {
        out->required_ammo = 0;
        out->ammo_per_shot = 0;
    } else {
        bool honors_infinite = weapon == QA_Q2_ETF_RIFLE ? call.rerelease :
            throwing ? !call.rerelease && weapon != QA_Q2_TRAP : true;
        if (infinite && honors_infinite) out->ammo_per_shot = 0;
        if (weapon == QA_Q2_LMCTF_PLASMA)
            out->ammo_per_shot = (game->options.deathmatch_flags & 8192u) != 0 ? 9 : 10;
    }
    out->ammo_reserved = reserved;
    out->available = out->owned || reserved;
    if (out->ammo) {
        int ammo_count;
        if (!q2_count(game, id, out->ammo, &ammo_count, error)) return false;
        out->available &= ammo_count >= out->required_ammo;
        if (weapon == QA_Q2_CHAINGUN && ammo_count > 0 && (unsigned)ammo_count < out->shots) {
            out->shots = (unsigned)ammo_count;
            if (!infinite) out->ammo_per_shot = ammo_count;
        }
    }
    uint64_t interval = call.rerelease ? q2_animation_native(&call) : game->frame_ns;
    float seconds = (float)((double)interval / 1e9);
    out->has_cycle = !throwing && !out->grapple && weapon != QA_Q2_CHAINFIST && !game->hooks.firing_interval &&
        !game->hooks.selected_firing_interval;
    if (!call.rerelease && call.input.source_rules == QA_Q2_WEAPON_RULES_CTF && call.input.haste)
        seconds *= 0.5f;
    out->cycle = (float)(definition->fire_last - definition->activate_last + (call.rerelease ? 0 : 1)) * seconds;
    if (definition->repeating || weapon == QA_Q2_ETF_RIFLE || weapon == QA_Q2_HEATBEAM)
        out->cycle = seconds;
    int launch_frame = weapon == QA_Q2_BFG ? 17 : state.frame;
    if (!firing) out->launch_delay = (float)(launch_frame - definition->activate_last - 1) * seconds;
    else if (weapon == QA_Q2_BFG && state.frame < launch_frame)
        out->launch_delay = (float)(launch_frame - state.frame) * seconds;
    if (weapon == QA_Q2_CHAINGUN) {
        int first = state.frame;
        q2_weapon_call sustained = call;
        sustained.input.attack = true;
        qa_q2_weapon_state spin = state;
        sustained.state = &spin;
        for (first = 0; first < 64; ++first)
            if (q2_frame_bit(definition->fires, first)) break;
        spin.handoff = QA_Q2_PRIMARY_ACTIVE;
        q2_shot_spec full;
        for (spin.frame = first; spin.frame < 64; ++spin.frame) {
            if (!q2_weapon_shot_spec(&sustained, &full)) return false;
            if (full.shots == 3) break;
        }
        out->spin_up = (float)(spin.frame - first) * seconds;
    }
    state.phase = QA_Q2_ACTIVATING;
    if (call.rerelease) interval = q2_animation_native(&call);
    out->activate = call.rerelease && call.input.instant_switch ? 0 :
        (float)definition->activate_last * (float)((double)interval / 1e9);
    if (out->ballistic) {
        if (!game->services.physics) {
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Q2 ballistic observation lost its actual physics owner");
            return false;
        }
        qa_physics_properties properties = qa_physics_properties_default(QA_COLLISION_Q2);
        out->gravity_acceleration = game->services.physics->gravity * properties.gravity_scale;
        out->extra_z_velocity = out->deployable ?
            q2_mine_lift(spec.kind, call.rerelease, call.input.gravity, 0) :
            q2_grenade_lift(call.rerelease, call.input.gravity, 0);
    }
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, id, &body, error)) return false;
    qa_vec3 angles = call.input.angles, start, direction;
    if (call.rerelease && (throwing || weapon == QA_Q2_GRENADELAUNCHER || weapon == QA_Q2_PROXLAUNCHER))
        angles.x = q2_launch_pitch(angles.x);
    if (!q2_weapon_muzzle(&call, &spec, angles, &start, &direction, error)) return false;
    if (q2_actor_get(game, id, false, error) != actor) {
        if (error && error->code == QA_OK)
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Q2 firing observation lost its actual Source actor");
        return false;
    }
    out->muzzle_count = 1;
    out->muzzle_offsets[0] = qa_vec_sub(start, body.origin);
    out->launch_angles = qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                              atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
    out->launch_velocity = out->ballistic && out->deployable ?
        q2_mine_velocity(direction, spec.speed, out->extra_z_velocity, 0) :
        q2_launch_velocity(direction, spec.speed, out->extra_z_velocity, 0);
    *found = true;
    return true;
}

bool qa_q2_bot_entity_read(qa_q2_game *game,qa_actor_id actor,qa_q2_bot_entity *out,qa_error *error) {
    if(!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot entity requires its source owner and output");return false;}
    qa_entity_visual visual;qa_builtin_actor_traits traits;
    *out=(qa_q2_bot_entity){0};
    if(!qa_q2_presentation_read(game,actor,&visual)) return true;
    if(!qa_q2_actor_traits(game,actor,&traits)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 source bot entity lost its native traits");return false;}
    const char *classname=qa_strings_cstr(qa_session_strings(game->services.session),traits.classname);
    if(!classname) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 bot entity lost its source classname");return false;}
    *out=(qa_q2_bot_entity){.present=true,.model=visual.models[0],.classname=traits.classname,.frame=visual.frame,
        .max_health=traits.max_health,.hidden=!visual.visible,.worldspawn=!strcmp(classname,"worldspawn")};return true;
}
bool qa_q2_bot_clock_read(const qa_q2_game *game,uint64_t *time,bool *intermission,uint64_t *started,qa_error *error) {
    if(!game || !time || !intermission || !started) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot clock requires its actual source owner");return false;}
    *time=game->now_ns;*intermission=game->player_runtime && game->player_runtime->intermission;
    *started=*intermission?game->player_runtime->intermission_ns:0;return true;
}
bool qa_q2_bot_max_clients(const qa_q2_game *game,uint32_t *out,qa_error *error) {
    if(!game || !game->player_runtime || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot maxclients requires its configured source players");return false;}
    *out=game->player_runtime->rules.max_clients;return true;
}
bool qa_q2_bot_activate(qa_q2_game *game,qa_actor_id actor,qa_error *error) {
    if(!game) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot activation requires its source owner");return false;}
    q2_actor *source=q2_actor_get(game,actor,false,NULL);
    if(source && source->client) source->client->player->bot=true;
    return true;
}
qa_actor_id qa_q2_bot_world_actor(const qa_q2_game *game) {
    return game && game->services.physics?game->services.physics->world_actor:(qa_actor_id){0};
}
