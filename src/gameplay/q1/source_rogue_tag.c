#include "maps/internal.h"
#include "qa/text.h"

static bool fail(qa_error *error, qa_actor_id actor, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", message);
    return false;
}
static q1_actor *world(qa_q1_game *game, qa_error *error) {
    q1_actor *source = game && game->maps ? q1_entity(game, game->maps->world_actor) : NULL;
    if (!game || game->destroy_pending || game->continuation_pending ||
        game->options.program != QA_Q1_ROGUE || !source || !source->native ||
        !q1_classnamed(game, source->id, game->runtime_names[Q1_NAME_WORLDSPAWN])) {
        fail(error, (qa_actor_id){0}, "Rogue token lost its actual native source world");
        return NULL;
    }
    return source;
}
static q1_actor *token(qa_q1_game *game, qa_actor_id actor, qa_error *error) {
    q1_actor *source = q1_entity(game, actor);
    if (!world(game, error) || !source || !source->native ||
        source->kind != Q1_SOURCE_ROGUE_TAG || !q1_classnamed(game, actor, game->runtime_names[Q1_NAME_DMATCH_TAG_TOKEN])) {
        fail(error, actor, "Rogue token lost its physical source actor");
        return NULL;
    }
    return source;
}
static bool current(qa_q1_game *game, qa_actor_id actor, const q1_actor *expected,
    qa_error *error) {
    return (game->source_rogue_tag.current &&
        game->source_rogue_tag.current(game->source_rogue_tag.context, game, error) &&
        token(game, actor, error) == expected) ||
        fail(error, actor, "Rogue token changed during a source callback");
}
static bool write_value(qa_q1_game *game, q1_actor *source, bool frags,
    double number, qa_error *error) {
    float value = (float)number;
    if (!isfinite(value)) return fail(error, source->id, "Invalid Rogue token value");
    if (!current(game, source->id, source, error)) return false;
    if (frags) source->state.source_tag.frags = value;
    else source->state.source_tag.message_time = value;
    return true;
}
static bool announce(qa_q1_game *game, q1_actor *source, const char *text,
    qa_actor_id player, qa_error *error) {
    return current(game, source->id, source, error) &&
        game->source_rogue_tag.announce(game->source_rogue_tag.context, text, player, error) &&
        current(game, source->id, source, error);
}
static bool drop_floor(qa_q1_game *game, q1_actor *source, bool *placed,
    qa_error *error) {
    qa_body_state body;
    qa_actor_id actor = source->id;
    *placed = false;
    if (!qa_world_body_read(game->services.world, actor, &body, error) ||
        !current(game, actor, source, error)) return false;
    qa_trace_query query = {.start = body.origin, .end = qa_vec_sub(body.origin, qa_v3(0, 0, 256)),
        .pass_actor = actor, .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
        .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_trace_result hit;
    if (!qa_world_trace(game->services.world, &query, &hit, error) ||
        !current(game, actor, source, error)) return false;
    if (hit.all_solid || hit.fraction == 1) return true;
    body.origin = hit.end;
    body.ground = hit.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(game->options.provider, 0) : q1_ref_from(game, hit.actor);
    if (!qa_world_body_write(game->services.world, actor, &body, error) ||
        !q1_link(game, source, error) || !current(game, actor, source, error)) return false;
    *placed = true;
    return true;
}
static bool take(qa_q1_game *game, q1_actor *source, qa_actor_id player,
    double delay, qa_error *error) {
    q1_actor *actual_world = world(game, error);
    if (!actual_world || !current(game, source->id, source, error)) return false;
    actual_world->rogue_tag_owner = q1_ref_from(game, player);
    source->owner = q1_ref_from(game, player);
    if (!write_value(game, source, true, 0, error) ||
        !write_value(game, source, false, game->time + delay, error)) return false;
    source->physics.solid = QA_PHYSICS_NOT_SOLID;
    source->touch_disabled = true;
    return q1_link(game, source, error) && current(game, source->id, source, error) &&
        q1_schedule(game, source, .1, Q1_THINK_SOURCE_ROGUE_TAG, error);
}
static bool respawn(qa_q1_game *game, q1_actor *source, qa_error *error) {
    qa_actor_id actor = source->id, point;
    qa_body_state body, spot;
    if (!current(game, actor, source, error) ||
        !game->source_rogue_tag.spawn_point(game->source_rogue_tag.context, &point, error) ||
        !current(game, actor, source, error) ||
        !qa_world_body_read(game->services.world, point, &spot, error) ||
        !qa_world_body_read(game->services.world, actor, &body, error) ||
        !current(game, actor, source, error)) return false;
    body.origin = spot.origin;
    if (!qa_world_body_write(game->services.world, actor, &body, error) ||
        !current(game, actor, source, error)) return false;
    q1_actor *actual_world = world(game, error);
    if (!actual_world) return false;
    actual_world->rogue_tag_owner = (q1_ref){0};
    source->owner = (q1_ref){0};
    source->physics.solid = QA_PHYSICS_TRIGGER;
    source->touch_disabled = false;
    source->think = Q1_THINK_NONE;
    source->next_think = 0;
    if (!write_value(game, source, true, 0, error) || !q1_link(game, source, error) ||
        !current(game, actor, source, error)) return false;
    bool placed;
    return drop_floor(game, source, &placed, error);
}
bool qa_q1_source_rogue_tag_configure(qa_q1_game *game,
    const qa_q1_source_rogue_tag_services *services, qa_error *error) {
    if (!game || game->destroy_pending || game->continuation_pending || game->observation_depth ||
        game->options.program != QA_Q1_ROGUE || !services || !services->current ||
        !services->player || !services->announce || !services->spawn_point)
        return fail(error, (qa_actor_id){0}, "Rogue token requires its actual composition services");
    game->source_rogue_tag = *services;
    return true;
}
bool q1_source_rogue_tag_spawn(qa_q1_game *game, q1_actor *source, bool *handled,
    qa_error *error) {
    *handled = game->options.program == QA_Q1_ROGUE && q1_classnamed(game, source->id, game->runtime_names[Q1_NAME_DMATCH_TAG_TOKEN]);
    if (!*handled) return true;
    float mode;
    if (!game->host.cvars ||
        !q1_source_value(game, QA_Q1_SOURCE_TEAMPLAY, 0, &mode, error)) return false;
    if (q1_entity(game, source->id) != source)
        return fail(error, source->id, "Rogue token retired during source policy read");
    if (mode != 3) return q1_remove(game, source, error);
    source->kind = Q1_SOURCE_ROGUE_TAG;
    source->skin = 1;
    source->effects |= 8;
    qa_body_state body;
    if (!q1_model(game, source, "progs/sphere.mdl", error) ||
        !qa_world_body_read(game->services.world, source->id, &body, error)) return false;
    if (q1_entity(game, source->id) != source)
        return fail(error, source->id, "Rogue token retired during native construction");
    body.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
    return qa_world_body_write(game->services.world, source->id, &body, error) &&
        q1_schedule(game, source, .2, Q1_THINK_SOURCE_ROGUE_TAG_PLACE, error);
}
bool q1_source_rogue_tag_touch(qa_q1_game *game, q1_actor *source,
    qa_actor_id player, qa_error *error) {
    bool found;
    qa_actor_id actor = source->id;
    if (!current(game, actor, source, error) ||
        !game->source_rogue_tag.player(game->source_rogue_tag.context, player, &found, error) ||
        !current(game, actor, source, error)) return false;
    if (!found) return true;
    return take(game, source, player, 30, error) &&
        q1_sound_resource(game, actor, game->runtime_names[Q1_NAME_RESOURCE_RUNES_END1_WAV], 0, 1, 1, error) &&
        current(game, actor, source, error) && announce(game, source, "$qc_got_token", player, error);
}
bool q1_source_rogue_tag_think(qa_q1_game *game, q1_actor *source,
    q1_think_kind kind, qa_error *error) {
    qa_actor_id actor = source->id;
    if (!current(game, actor, source, error)) return false;
    if (kind == Q1_THINK_SOURCE_ROGUE_TAG_RESPAWN) return respawn(game, source, error);
    if (kind == Q1_THINK_SOURCE_ROGUE_TAG_PLACE) {
        qa_body_state body;
        if (!qa_world_body_read(game->services.world, actor, &body, error) ||
            !current(game, actor, source, error)) return false;
        source->physics.motion = QA_PHYSICS_TOSS;
        source->physics.solid = QA_PHYSICS_TRIGGER;
        body.origin = qa_vec_add(body.origin, qa_v3(0, 0, 6));
        if (!qa_world_body_write(game->services.world, actor, &body, error) ||
            !current(game, actor, source, error)) return false;
        bool placed;
        if (!drop_floor(game, source, &placed, error)) return false;
        return placed || q1_remove(game, source, error);
    }
    if (kind == Q1_THINK_SOURCE_ROGUE_TAG_FALL) {
        bool placed;
        return write_value(game, source, true, 0, error) &&
            drop_floor(game, source, &placed, error) &&
            q1_schedule(game, source, 30, Q1_THINK_SOURCE_ROGUE_TAG_RESPAWN, error);
    }
    qa_actor_id owner = q1_ref_actor(game, source->owner);
    qa_combat_state combat = {0};
    if (owner.registry && qa_combat_storage_serial(game->services.combat, owner) &&
        (!qa_combat_read(game->services.combat, owner, &combat, error) ||
         !current(game, actor, source, error))) return false;
    if (owner.registry && combat.health > 0) {
        double message_time = source->state.source_tag.message_time;
        if (message_time < game->time &&
            (!announce(game, source, "$qc_has_token", owner, error) ||
             !write_value(game, source, false, game->time + 30, error))) return false;
        qa_body_state body, carrier;
        if (!qa_world_body_read(game->services.world, actor, &body, error) ||
            !qa_world_body_read(game->services.world, owner, &carrier, error) ||
            !current(game, actor, source, error)) return false;
        body.origin = qa_vec_add(carrier.origin, qa_v3(0, 0, 48));
        return qa_world_body_write(game->services.world, actor, &body, error) &&
            q1_link(game, source, error) && current(game, actor, source, error) &&
            q1_schedule(game, source, .1, Q1_THINK_SOURCE_ROGUE_TAG, error);
    }
    if (owner.registry && !announce(game, source, "$qc_lost_token", owner, error)) return false;
    if (!write_value(game, source, true, 0, error)) return false;
    source->physics.solid = QA_PHYSICS_TRIGGER;
    source->touch_disabled = false;
    source->owner = (q1_ref){0};
    return q1_link(game, source, error) && current(game, actor, source, error) &&
        q1_schedule(game, source, .1, Q1_THINK_SOURCE_ROGUE_TAG_FALL, error);
}
bool qa_q1_source_rogue_tag_score(qa_q1_game *game, qa_actor_id victim,
    qa_actor_id attacker, int32_t *points, qa_error *error) {
    if (!game || !points) return fail(error, victim, "Rogue token scoring has no source output");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    bool okay = world(game, error) != NULL;
    q1_actor *source = NULL;
    uint32_t ordinal = UINT32_MAX;
    for (uint32_t i = 0; okay && i < game->capacity; ++i) {
        q1_actor *row = game->actors[i];
        if (!row || row->kind != Q1_SOURCE_ROGUE_TAG) continue;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), row->id);
        if (!record || !record->has_source || record->owner != game->options.provider) {
            okay = fail(error, row->id, "Rogue token lost its source creation order");
            break;
        }
        if (!source || record->source_slot < ordinal) { source = row; ordinal = record->source_slot; }
    }
    int32_t result = 1;
    if (!okay || !source) goto finish;
    okay = current(game, source->id, source, error);
    if (!okay) goto finish;
    qa_actor_id owner = q1_ref_actor(game, world(game, error)->rogue_tag_owner);
    if (owner.registry && qa_actor_id_equal(attacker, owner)) {
        okay = write_value(game, source, true, source->state.source_tag.frags + 1, error);
        double frags = source->state.source_tag.frags;
        if (okay && frags == 5 && q1_player_get(game, attacker)) {
            qa_string_id text = 0;
            okay = qa_strings_intern_cstr(qa_session_strings(game->services.session), "$qc_got_quad", &text, error);
            qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
                .provider = game->options.provider, .actor = attacker, .text = text, .time_ns = game->time_ns};
            if (okay) okay = qa_builtin_emit(&game->services, &event, error) &&
                current(game, source->id, source, error) &&
                q1_power_give(game, attacker, QA_Q1_QUAD, 30, error) &&
                current(game, source->id, source, error);
        } else if (okay && frags == 10)
            okay = announce(game, source, "$qc_lost_token", attacker, error) && respawn(game, source, error);
        result = 3;
    } else if (owner.registry && qa_actor_id_equal(victim, owner)) {
        okay = q1_sound_resource(game, victim, game->runtime_names[Q1_NAME_RESOURCE_RUNES_END1_WAV], 0, 1, 1, error) &&
            current(game, source->id, source, error);
        bool found;
        if (okay) okay = game->source_rogue_tag.player(game->source_rogue_tag.context, attacker, &found, error) &&
            current(game, source->id, source, error);
        if (okay && found) okay = take(game, source, attacker, .5, error);
        result = 5;
    }
finish:
    if (okay && !qa_q1_game_operation_live(&operation))
        okay = fail(error, victim, "Rogue token scoring retired its source");
    if (okay) *points = result;
    qa_q1_game_operation_end(&operation);
    return okay;
}
