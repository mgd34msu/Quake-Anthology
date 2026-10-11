#include "maps/internal.h"
#include "qa/text.h"

static bool fail(qa_error *error, qa_actor_id actor, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", message);
    return false;
}

static q1_actor *capture_world(qa_q1_game *game, qa_error *error) {
    qa_actor_id actor = game && game->maps ? game->maps->world_actor : (qa_actor_id){0};
    q1_actor *world = q1_entity(game, actor);
    if (!game || game->destroy_pending || game->continuation_pending ||
        game->options.program != QA_Q1_CTF || !world || !world->native ||
        !q1_classnamed(game, actor, game->runtime_names[Q1_NAME_WORLDSPAWN])) {
        fail(error, actor, "CTF capture words require the actual native source world");
        return NULL;
    }
    return world;
}

bool qa_q1_source_capture_words_read(qa_q1_game *game, double *seconds, double *team,
    qa_error *error) {
    q1_actor *world = capture_world(game, error);
    if (!world || !seconds || !team) return false;
    *seconds = world->ctf_last_capture;
    *team = world->ctf_last_capture_team;
    return true;
}

bool qa_q1_source_capture_words_write(qa_q1_game *game, double seconds, double team,
    qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *world = capture_world(game, error);
    float time_value = (float)seconds, team_value = (float)team;
    bool okay = world && isfinite(time_value) && isfinite(team_value);
    if (okay) {
        world->ctf_last_capture = time_value;
        world->ctf_last_capture_team = team_value;
    } else if (world) fail(error, world->id, "Invalid CTF capture values");
    qa_q1_game_operation_end(&operation);
    return okay;
}

static q1_actor *flag(qa_q1_game *game, qa_actor_id actor, qa_error *error) {
    q1_actor *entity = q1_entity(game, actor);
    if (!game || game->continuation_pending || game->options.program != QA_Q1_CTF ||
        !entity || !entity->native || entity->kind != Q1_SOURCE_CTF_FLAG ||
        (!q1_classnamed(game, actor, game->runtime_names[Q1_NAME_ITEM_FLAG_TEAM1]) &&
         !q1_classnamed(game, actor, game->runtime_names[Q1_NAME_ITEM_FLAG_TEAM2]))) {
        fail(error, actor, "CTF flag requires its genuine native source actor");
        return NULL;
    }
    return entity;
}

static bool current(qa_q1_game *game, qa_actor_id actor, const q1_actor *expected,
    qa_error *error) {
    if (!game->source_flags.current)
        return fail(error, actor, "CTF flag has no genuine composition service binding");
    if (!game->source_flags.current(game->source_flags.context, game, error)) return false;
    return flag(game, actor, error) == expected ||
        fail(error, actor, "CTF flag changed during a source callback");
}

bool qa_q1_source_flags_configure(qa_q1_game *game,
    const qa_q1_source_flags_services *services, qa_error *error) {
    if (!game || game->destroy_pending || game->continuation_pending || game->observation_depth ||
        game->options.program != QA_Q1_CTF || !services || !services->current ||
        !services->touch || !services->return_flag || !services->drop_flag ||
        !services->update || !services->player_frame)
        return fail(error, (qa_actor_id){0}, "CTF flags require their genuine composition services");
    game->source_flags = *services;
    return true;
}

bool qa_q1_source_flag_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_source_flag_view *out, qa_error *error) {
    const q1_actor *entity = q1_entity_const(game, actor);
    if (!out || !game || game->continuation_pending || game->options.program != QA_Q1_CTF ||
        !entity || !entity->native || entity->kind != Q1_SOURCE_CTF_FLAG)
        return fail(error, actor, "CTF flag observation lost its native source owner");
    qa_bytes name = qa_strings_text(qa_session_strings(game->services.session), entity->classname);
    bool blue = name.size == sizeof("item_flag_team2") - 1 &&
        !memcmp(name.data, "item_flag_team2", name.size);
    if (!blue && (name.size != sizeof("item_flag_team1") - 1 ||
        memcmp(name.data, "item_flag_team1", name.size)))
        return fail(error, actor, "CTF flag observation names a different source class");
    *out = (qa_q1_source_flag_view){.actor = actor, .owner = q1_ref_actor(game, entity->owner),
        .base = entity->state.source_flag.base, .angles = entity->state.source_flag.angles,
        .count = entity->count, .blue = blue, .placed = entity->state.source_flag.placed,
        .trigger = entity->physics.solid == QA_PHYSICS_TRIGGER};
    out->movement_flags = entity->state.source_flag.movement_flags;
    return true;
}

static bool placed(qa_q1_game *game, qa_actor_id actor, q1_actor **out, qa_error *error) {
    q1_actor *entity = flag(game, actor, error);
    if (!entity || !current(game, actor, entity, error)) return false;
    if (!entity->state.source_flag.placed)
        return fail(error, actor, "CTF flag has not completed its actual source floor placement");
    *out = entity;
    return true;
}

bool qa_q1_source_flag_carried(const qa_q1_game *game, qa_actor_id player,
    qa_actor_id *out, bool *found, qa_error *error) {
    if (!game || !out || !found || game->destroy_pending || game->continuation_pending ||
        game->options.program != QA_Q1_CTF)
        return fail(error, player, "CTF carried flag requires its actual native source");
    uint32_t ordinal = 0;
    qa_actor_id selected = {0};
    bool present = false;
    const qa_actor_registry *actors = qa_session_actors(game->services.session);
    for (uint32_t slot = 0; slot < game->capacity; ++slot) {
        const q1_actor *entity = game->actors[slot];
        if (!entity || !entity->active || !entity->native || entity->kind != Q1_SOURCE_CTF_FLAG ||
            entity->count != 1 || !q1_ref_equal(entity->owner, q1_ref_from(game, player))) continue;
        const qa_actor_record *record = qa_actors_get(actors, entity->id);
        if (!record || record->owner != game->options.provider || !record->has_source)
            return fail(error, player, "CTF carried flag lost its actual source identity");
        if (!present || record->source_slot < ordinal) {
            selected = entity->id;
            ordinal = record->source_slot;
            present = true;
        } else if (record->source_slot == ordinal)
            return fail(error, player, "CTF carried flags duplicate a physical source ordinal");
    }
    *out = selected;
    *found = present;
    return true;
}

bool qa_q1_source_flag_return(qa_q1_game *game, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *entity;
    bool okay = placed(game, actor, &entity, error);
    qa_body_state body;
    if (okay) {
        entity->physics.motion = QA_PHYSICS_TOSS;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->count = 0;
        entity->owner = (q1_ref){0};
        okay = qa_world_body_read(game->services.world, actor, &body, error) &&
            current(game, actor, entity, error);
    }
    if (okay) {
        body.origin = entity->state.source_flag.base;
        body.angles = entity->state.source_flag.angles;
        okay = qa_world_body_write(game->services.world, actor, &body, error) &&
            q1_link(game, entity, error) && current(game, actor, entity, error) &&
            q1_sound_resource(game, actor, game->runtime_names[Q1_NAME_RESOURCE_ITEMS_ITEMBK2_WAV], 0, 1, 1, error) &&
            current(game, actor, entity, error);
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool qa_q1_source_flag_drop(qa_q1_game *game, qa_actor_id actor, qa_actor_id player,
    qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *entity;
    uint32_t slot;
    qa_body_state body, carrier;
    bool okay = placed(game, actor, &entity, error) &&
        qa_q1_native_client_slot(game, player, &slot, error) &&
        entity->count == 1 && q1_ref_equal(entity->owner, q1_ref_from(game, player));
    if (!okay && (!error || error->code == QA_OK))
        fail(error, actor, "CTF drop has no actual source flag carrier");
    if (okay) {
        entity->count = 2;
        entity->physics.motion = QA_PHYSICS_TOSS;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->state.source_flag.movement_flags = UINT32_C(256) | UINT32_C(131072);
        entity->state.source_flag.return_time = (float)(game->time + 15);
        okay = qa_world_body_read(game->services.world, player, &carrier, error) &&
            qa_world_body_read(game->services.world, actor, &body, error) &&
            current(game, actor, entity, error) &&
            qa_q1_native_client_slot(game, player, &slot, error);
        if (okay && (entity->count != 2 || !q1_ref_equal(entity->owner, q1_ref_from(game, player))))
            okay = fail(error, actor, "CTF drop changed during source body preparation");
    }
    if (okay) {
        body.origin = qa_vec_sub(carrier.origin, qa_v3(0, 0, 24));
        body.velocity = qa_v3(0, 0, 300);
        body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 74}};
        okay = qa_world_body_write(game->services.world, actor, &body, error) &&
            q1_link(game, entity, error) && current(game, actor, entity, error);
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool qa_q1_source_flag_carry(qa_q1_game *game, qa_actor_id actor, qa_actor_id player,
    qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *entity;
    uint32_t slot;
    bool okay = placed(game, actor, &entity, error) &&
        qa_q1_native_client_slot(game, player, &slot, error);
    if (okay && entity->physics.solid != QA_PHYSICS_TRIGGER)
        okay = fail(error, actor, "CTF carry requires its genuine source trigger");
    if (okay) {
        entity->count = 1;
        entity->physics.motion = QA_PHYSICS_STATIONARY;
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        entity->owner = q1_ref_from(game, player);
        okay = q1_link(game, entity, error) && current(game, actor, entity, error) &&
            qa_q1_native_client_slot(game, player, &slot, error);
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool q1_source_flag_spawn(qa_q1_game *game, q1_actor *entity, bool *handled, qa_error *error) {
    *handled = game->options.program == QA_Q1_CTF &&
        (q1_classnamed(game, entity->id, game->runtime_names[Q1_NAME_ITEM_FLAG_TEAM1]) ||
         q1_classnamed(game, entity->id, game->runtime_names[Q1_NAME_ITEM_FLAG_TEAM2]));
    if (!*handled) return true;
    qa_actor_id actor = entity->id;
    entity->kind = Q1_SOURCE_CTF_FLAG;
    bool blue = q1_classnamed(game, actor, game->runtime_names[Q1_NAME_ITEM_FLAG_TEAM2]);
    entity->skin = blue ? 1 : 0;
    entity->effects = blue ? 16 : 32;
    qa_body_state body;
    if (!q1_model(game, entity, "progs/flag.mdl", error) ||
        !qa_world_body_read(game->services.world, actor, &body, error)) return false;
    if (flag(game, actor, error) != entity) return false;
    body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 74}};
    return qa_world_body_write(game->services.world, actor, &body, error) &&
        q1_schedule(game, entity, .2, Q1_THINK_SOURCE_CTF_FLAG_PLACE, error);
}

bool q1_source_flag_think(qa_q1_game *game, q1_actor *entity, q1_think_kind kind,
    qa_error *error) {
    qa_actor_id actor = entity->id;
    if (!current(game, actor, entity, error)) return false;
    if (kind == Q1_THINK_SOURCE_CTF_FLAG_PLACE) {
        qa_body_state body;
        if (!qa_world_body_read(game->services.world, actor, &body, error) ||
            !current(game, actor, entity, error)) return false;
        qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 6));
        qa_trace_query query = {.start = start, .end = qa_vec_sub(start, qa_v3(0, 0, 256)),
            .pass_actor = actor, .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
            .policy = qa_collision_default_policy(QA_GAME_Q1)};
        qa_trace_result floor;
        if (!qa_world_trace(game->services.world, &query, &floor, error) ||
            !current(game, actor, entity, error)) return false;
        if (floor.all_solid || floor.fraction == 1) return q1_remove(game, entity, error);
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->physics.motion = QA_PHYSICS_TOSS;
        entity->state.source_flag.movement_flags = UINT32_C(256) | UINT32_C(131072);
        entity->count = 0;
        entity->effects |= 8;
        entity->state.source_flag.angles = body.angles;
        entity->state.source_flag.base = floor.end;
        entity->state.source_flag.placed = true;
        body.origin = floor.end;
        body.velocity = qa_v3(0, 0, 0);
        body.ground = floor.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(game->options.provider, 0) : q1_ref_from(game, floor.actor);
        return qa_world_body_write(game->services.world, actor, &body, error) &&
            q1_link(game, entity, error) && current(game, actor, entity, error) &&
            q1_schedule(game, entity, .1, Q1_THINK_SOURCE_CTF_FLAG, error);
    }
    if (!q1_schedule(game, entity, .1, Q1_THINK_SOURCE_CTF_FLAG, error) ||
        !current(game, actor, entity, error)) return false;
    if (entity->count == 0) return true;
    if (entity->count == 2) {
        double due = entity->state.source_flag.return_time;
        if (game->time - due > 15 &&
            (!game->source_flags.return_flag(game->source_flags.context, actor, error) ||
             !current(game, actor, entity, error))) return false;
        return game->source_flags.update(game->source_flags.context, error) &&
            current(game, actor, entity, error);
    }
    if (entity->count != 1) return fail(error, actor, "CTF flag retained an invalid source count");
    qa_actor_id owner = q1_ref_actor(game, entity->owner);
    if (!owner.registry || !q1_alive(game, owner))
        return game->source_flags.return_flag(game->source_flags.context, actor, error) &&
            current(game, actor, entity, error);
    qa_combat_state combat;
    if (!qa_combat_read(game->services.combat, owner, &combat, error) ||
        !current(game, actor, entity, error)) return false;
    if (combat.health <= 0)
        return game->source_flags.drop_flag(game->source_flags.context, owner, error) &&
            current(game, actor, entity, error);
    qa_body_state carrier, body;
    double frame;
    if (!qa_world_body_read(game->services.world, owner, &carrier, error) ||
        !game->source_flags.player_frame(game->source_flags.context, owner, &frame, error) ||
        !current(game, actor, entity, error) || !q1_ref_equal(entity->owner, q1_ref_from(game, owner)) ||
        !qa_world_body_read(game->services.world, actor, &body, error)) return false;
    static const unsigned offsets[] = {2, 8, 12, 11, 10, 4, 2, 10, 10, 8, 4, 2};
    float distance = 14;
    if (frame >= 29 && frame <= 40 && floor(frame) == frame) distance += (float)offsets[(size_t)frame - 29];
    else if (frame >= 103 && frame <= 118) distance += frame <= 106 ? 6 : 7;
    qa_vec3 forward, right;
    qa_builtin_angle_vectors(carrier.angles, &forward, &right, NULL);
    forward.z = -forward.z;
    body.origin = qa_vec_add(qa_vec_sub(qa_vec_add(carrier.origin, qa_v3(0, 0, -16)),
        qa_vec_scale(forward, distance)), qa_vec_scale(right, 22));
    body.angles = qa_vec_add(carrier.angles, qa_v3(0, 0, -45));
    return qa_world_body_write(game->services.world, actor, &body, error) &&
        q1_link(game, entity, error) && current(game, actor, entity, error) &&
        q1_schedule(game, entity, .01, Q1_THINK_SOURCE_CTF_FLAG, error);
}

bool q1_source_flag_touch(qa_q1_game *game, q1_actor *entity, qa_actor_id player,
    qa_error *error) {
    qa_actor_id actor = entity->id;
    return current(game, actor, entity, error) &&
        game->source_flags.touch(game->source_flags.context, actor, player, error);
}
