#include "maps/internal.h"
#include "qa/text.h"

static bool fail(qa_error *error, qa_actor_id actor, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", message);
    return false;
}
static q1_actor *flag(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_actor *e = q1_entity(g, actor);
    if (!g || g->destroy_pending || g->continuation_pending ||
        g->options.program != QA_Q1_ROGUE || !e || !e->native ||
        (e->kind != Q1_SOURCE_ROGUE_FLAG && e->kind != Q1_SOURCE_ROGUE_FLAG_BASE)) {
        fail(error, actor, "Rogue flag lost its actual native actor");
        return NULL;
    }
    return e;
}
static bool current(qa_q1_game *g, qa_actor_id actor, const q1_actor *e, qa_error *error) {
    if (!g->source_rogue_flags.current ||
        !g->source_rogue_flags.current(g->source_rogue_flags.context, g, error)) return false;
    return flag(g, actor, error) == e || fail(error, actor, "Rogue flag changed during source callback");
}
static bool word(qa_q1_game *g, qa_string_id id, double *out, qa_error *error) {
    return q1_source_number_read(qa_strings_text(qa_session_strings(g->services.session), id), out, error);
}
static bool write(qa_q1_game *g, q1_actor *e, unsigned field, double value, qa_error *error) {
    char text[32];
    qa_string_id id;
    qa_actor_id actor = e->id;
    if (!qa_format_ecmascript_number((float)(value), text, error) ||
        !qa_strings_intern_cstr(qa_session_strings(g->services.session), text, &id, error) ||
        !current(g, actor, e, error)) return false;
    e->state.rogue_flag.words[field] = id;
    return true;
}
static bool floor_drop(qa_q1_game *g, q1_actor *e, bool *placed, qa_error *error) {
    qa_actor_id actor = e->id;
    qa_body_state body;
    *placed = false;
    if (!qa_world_body_read(g->services.world, actor, &body, error) ||
        !current(g, actor, e, error)) return false;
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 6));
    qa_trace_query query = {.start = start, .end = qa_vec_sub(start, qa_v3(0, 0, 256)),
        .pass_actor = actor, .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_trace_result hit;
    if (!qa_world_trace(g->services.world, &query, &hit, error) ||
        !current(g, actor, e, error)) return false;
    if (hit.fraction == 1 || hit.all_solid) return true;
    body.origin = hit.end;
    body.velocity = qa_v3(0, 0, 0);
    body.ground = hit.actor;
    if (!qa_world_body_write(g->services.world, actor, &body, error) ||
        !q1_link(g, e, error) || !current(g, actor, e, error)) return false;
    *placed = true;
    return true;
}
bool qa_q1_source_rogue_flags_configure(qa_q1_game *g,
    const qa_q1_source_rogue_flags_services *services, qa_error *error) {
    if (!g || g->destroy_pending || g->continuation_pending || g->observation_depth ||
        g->options.program != QA_Q1_ROGUE || !services || !services->current ||
        !services->touch || !services->return_flag || !services->drop_flag ||
        !services->player_frame || !services->carrier)
        return fail(error, (qa_actor_id){0}, "Rogue flags require their actual composition callbacks");
    g->source_rogue_flags = *services;
    return true;
}
bool qa_q1_source_rogue_flag_read(qa_q1_game *g, qa_actor_id actor,
    qa_q1_source_rogue_flag_view *out, qa_error *error) {
    q1_actor *e = flag(g, actor, error);
    qa_q1_source_rogue_flag_view value = {.actor = actor};
    if (!e || !out || !word(g, e->state.rogue_flag.words[0], &value.team, error) ||
        !word(g, e->state.rogue_flag.words[1], &value.count, error)) return false;
    value.owner = e->owner;
    value.base = e->kind == Q1_SOURCE_ROGUE_FLAG_BASE;
    value.placed = e->state.rogue_flag.placed;
    *out = value;
    return true;
}
bool qa_q1_source_rogue_flags_snapshot(qa_q1_game *g, qa_actor_id **out,
    size_t *count, qa_error *error) {
    if (!g || !out || !count || g->destroy_pending || g->continuation_pending ||
        g->options.program != QA_Q1_ROGUE)
        return fail(error, (qa_actor_id){0}, "Rogue flags lost their physical source roster");
    size_t total = 0;
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (g->actors[i] && g->actors[i]->kind == Q1_SOURCE_ROGUE_FLAG) ++total;
    qa_actor_id *rows = total ? malloc(total * sizeof(*rows)) : NULL;
    if (total && !rows) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing Rogue source flags");
        return false;
    }
    size_t used = 0;
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *e = g->actors[i];
        if (!e || e->kind != Q1_SOURCE_ROGUE_FLAG) continue;
        const qa_actor_record *record = qa_actors_get(actors, e->id);
        if (!record || record->owner != g->options.provider || !record->has_source) {
            free(rows);
            return fail(error, e->id, "Rogue flag lost its physical source ordinal");
        }
        size_t at = used;
        while (at && qa_actors_get(actors, rows[at - 1])->source_slot > record->source_slot) {
            rows[at] = rows[at - 1];
            --at;
        }
        rows[at] = e->id;
        ++used;
    }
    *out = rows;
    *count = used;
    return true;
}
bool qa_q1_source_rogue_flag_return(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_actor *e = flag(g, actor, error);
    bool okay = e && e->kind == Q1_SOURCE_ROGUE_FLAG && e->state.rogue_flag.placed &&
        current(g, actor, e, error);
    qa_body_state body;
    if (okay) {
        e->physics.motion = QA_PHYSICS_TOSS;
        e->physics.solid = QA_PHYSICS_TRIGGER;
        okay = q1_sound(g, actor, "items/itembk2.wav", 2, 1, error) &&
            current(g, actor, e, error) && qa_world_body_read(g->services.world, actor, &body, error);
    }
    if (okay) {
        body.origin = e->state.rogue_flag.origin;
        body.angles = e->state.rogue_flag.angles;
        okay = qa_world_body_write(g->services.world, actor, &body, error) &&
            current(g, actor, e, error) && write(g, e, 1, 0, error);
    }
    if (okay) {
        e->owner = (qa_actor_id){0};
        okay = q1_link(g, e, error) && current(g, actor, e, error);
    }
    if (!okay && (!error || error->code == QA_OK)) fail(error, actor, "Rogue return needs a placed source flag");
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_source_rogue_flag_drop(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_actor *e = flag(g, actor, error);
    qa_body_state body, owner;
    bool okay = e && e->kind == Q1_SOURCE_ROGUE_FLAG && current(g, actor, e, error) &&
        qa_world_body_read(g->services.world, e->owner, &owner, error) &&
        qa_world_body_read(g->services.world, actor, &body, error) && current(g, actor, e, error);
    if (okay) {
        body.origin = qa_vec_add(owner.origin, qa_v3(0, 0, -24));
        body.velocity = qa_v3(0, 0, 300);
        body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 74}};
        okay = qa_world_body_write(g->services.world, actor, &body, error) &&
            current(g, actor, e, error) && write(g, e, 1, 2, error);
    }
    if (okay) {
        e->source_movement_flags = UINT32_C(256) | UINT32_C(131072);
        e->physics.solid = QA_PHYSICS_TRIGGER;
        e->physics.motion = QA_PHYSICS_TOSS;
        okay = write(g, e, 2, g->time + 40, error) &&
            q1_link(g, e, error) && current(g, actor, e, error);
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_source_rogue_flag_carry(qa_q1_game *g, qa_actor_id actor,
    qa_actor_id player, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_actor *e = flag(g, actor, error);
    bool okay = e && e->kind == Q1_SOURCE_ROGUE_FLAG && current(g, actor, e, error);
    if (okay) {
        e->owner = player;
        e->physics.motion = QA_PHYSICS_NOCLIP;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        okay = write(g, e, 1, 1, error) && q1_link(g, e, error) && current(g, actor, e, error);
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
static bool policy(qa_q1_game *g, const char *text, float *out, qa_error *error) {
    qa_string_id name;
    return g->services.cvar &&
        qa_strings_intern_cstr(qa_session_strings(g->services.session), text, &name, error) &&
        g->services.cvar(q1_cvar_context(g), name, out, error);
}
bool q1_source_rogue_flag_spawn(qa_q1_game *g, q1_actor *e, bool *handled, qa_error *error) {
    bool red = q1_classnamed(g, e->id, "item_flag_team1"), blue = q1_classnamed(g, e->id, "item_flag_team2");
    *handled = g->options.program == QA_Q1_ROGUE && (red || blue || q1_classnamed(g, e->id, "item_flag"));
    if (!*handled) return true;
    qa_actor_id actor = e->id;
    float mode, deathmatch;
    if (!policy(g, "teamplay", &mode, error) || !policy(g, "deathmatch", &deathmatch, error) ||
        q1_entity(g, actor) != e) return false;
    bool ctf = mode == 4 || mode == 5 || mode == 6;
    if (red || blue ? deathmatch == 0 || !ctf : mode != 5) return q1_remove(g, e, error);
    e->kind = Q1_SOURCE_ROGUE_FLAG;
    e->skin = red ? 0 : blue ? 1 : 2;
    char team_text[32];
    qa_string_id team;
    if (!qa_format_ecmascript_number(red ? 5 : blue ? 14 : 0, team_text, error) ||
        !qa_strings_intern_cstr(qa_session_strings(g->services.session), team_text, &team, error)) return false;
    e->state.rogue_flag.words[0] = team;
    q1_actor *base;
    if (!q1_create(g, red ? "item_flagbase_team1" : blue ? "item_flagbase_team2" : "item_flagbase",
        Q1_SOURCE_ROGUE_FLAG_BASE, (qa_actor_id){0}, &base, error) || q1_entity(g, actor) != e) return false;
    qa_actor_id base_actor = base->id;
    if (!base) return fail(error, base_actor, "Rogue base retired during native creation");
    base->kind = Q1_SOURCE_ROGUE_FLAG_BASE;
    base->skin = e->skin;
    base->state.rogue_flag.words[0] = team;
    base->source_movement_flags = 256;
    base->physics.motion = QA_PHYSICS_TOSS;
    base->physics.solid = mode == 5 || mode == 6 ? QA_PHYSICS_TRIGGER : QA_PHYSICS_NOT_SOLID;
    qa_body_state original, body;
    if (!q1_model(g, base, "progs/ctfbase.mdl", error) ||
        !qa_world_body_read(g->services.world, actor, &original, error) ||
        !qa_world_body_read(g->services.world, base_actor, &body, error) ||
        q1_entity(g, actor) != e || q1_entity(g, base_actor) != base) return false;
    body.origin = original.origin;
    body.angles = original.angles;
    body.bounds = (qa_bounds){{-8, -8, 0}, {8, 8, 8}};
    if (!qa_world_body_write(g->services.world, base_actor, &body, error)) return false;
    /* Construction precedes published player membership. The actual base floor
     * trace needs native actor retention, not a published composition callback. */
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 6));
    qa_trace_query query = {.start = start, .end = qa_vec_sub(start, qa_v3(0, 0, 256)),
        .pass_actor = base_actor, .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_trace_result hit;
    if (!qa_world_trace(g->services.world, &query, &hit, error) ||
        q1_entity(g, actor) != e || q1_entity(g, base_actor) != base) return false;
    if (hit.fraction == 1 || hit.all_solid) {
        if (!q1_remove(g, base, error)) return false;
    } else {
        body.origin = hit.end;
        body.velocity = qa_v3(0, 0, 0);
        body.ground = hit.actor;
        if (!qa_world_body_write(g->services.world, base_actor, &body, error) ||
            !q1_link(g, base, error)) return false;
        base->state.rogue_flag.placed = true;
    }
    if (q1_entity(g, actor) != e) return fail(error, actor, "Rogue flag retired during base placement");
    if ((red || blue) && mode == 5) return q1_remove(g, e, error);
    if (!q1_model(g, e, "progs/ctfmodel.mdl", error) ||
        q1_entity(g, actor) != e) return false;
    original.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 74}};
    return qa_world_body_write(g->services.world, actor, &original, error) &&
        q1_schedule(g, e, .2, Q1_THINK_SOURCE_ROGUE_FLAG_PLACE, error);
}
bool q1_source_rogue_flag_touch(qa_q1_game *g, q1_actor *e,
    qa_actor_id player, qa_error *error) {
    return current(g, e->id, e, error) &&
        g->source_rogue_flags.touch(g->source_rogue_flags.context, e->id, player,
            e->kind == Q1_SOURCE_ROGUE_FLAG_BASE, error);
}
bool q1_source_rogue_flag_think(qa_q1_game *g, q1_actor *e,
    q1_think_kind kind, qa_error *error) {
    qa_actor_id actor = e->id;
    if (!current(g, actor, e, error)) return false;
    if (kind == Q1_THINK_SOURCE_ROGUE_FLAG_PLACE) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, actor, &body, error)) return false;
        e->source_movement_flags = UINT32_C(256) | UINT32_C(131072);
        e->physics.solid = QA_PHYSICS_TRIGGER;
        e->physics.motion = QA_PHYSICS_TOSS;
        e->state.rogue_flag.angles = body.angles;
        e->effects |= 8;
        bool placed;
        if (!write(g, e, 1, 0, error) || !floor_drop(g, e, &placed, error)) return false;
        if (!placed) return q1_remove(g, e, error);
        if (!qa_world_body_read(g->services.world, actor, &body, error) ||
            !current(g, actor, e, error)) return false;
        e->state.rogue_flag.origin = body.origin;
        e->state.rogue_flag.placed = true;
        return q1_schedule(g, e, .1, Q1_THINK_SOURCE_ROGUE_FLAG, error);
    }
    if (!q1_schedule(g, e, .1, Q1_THINK_SOURCE_ROGUE_FLAG, error)) return false;
    double count, deadline;
    if (!word(g, e->state.rogue_flag.words[1], &count, error)) return false;
    if (count == 0) return true;
    if (count == 2) {
        if (!word(g, e->state.rogue_flag.words[2], &deadline, error)) return false;
        return g->time - deadline <= 40 ||
            (g->source_rogue_flags.return_flag(g->source_rogue_flags.context, actor, error) &&
             current(g, actor, e, error));
    }
    if (count != 1) return fail(error, actor, "Rogue flag has an invalid source count");
    bool carrier;
    qa_actor_id owner = e->owner;
    if (!g->source_rogue_flags.carrier(g->source_rogue_flags.context, actor, owner, &carrier, error) ||
        !current(g, actor, e, error)) return false;
    if (!carrier) return g->source_rogue_flags.drop_flag(g->source_rogue_flags.context, actor, error) &&
        current(g, actor, e, error);
    qa_body_state body, held;
    double frame;
    if (!qa_world_body_read(g->services.world, actor, &body, error) ||
        !qa_world_body_read(g->services.world, owner, &held, error) ||
        !g->source_rogue_flags.player_frame(g->source_rogue_flags.context, owner, &frame, error) ||
        !current(g, actor, e, error) || !qa_actor_id_equal(e->owner, owner)) return false;
    static const unsigned offsets[] = {2, 8, 12, 11, 10, 4, 2, 10, 10, 8, 4, 2};
    float distance = 14;
    if (frame >= 29 && frame <= 40 && floor(frame) == frame) distance += (float)offsets[(size_t)frame - 29];
    else if (frame >= 103 && frame <= 118) distance += frame <= 106 ? 6 : 7;
    qa_vec3 forward, right;
    qa_builtin_angle_vectors(held.angles, &forward, &right, NULL);
    forward.z = -forward.z;
    body.origin = qa_vec_add(qa_vec_sub(qa_vec_add(held.origin, qa_v3(0, 0, -16)),
        qa_vec_scale(forward, distance)), qa_vec_scale(right, 22));
    body.angles = qa_vec_add(held.angles, qa_v3(0, 0, -45));
    return qa_world_body_write(g->services.world, actor, &body, error) &&
        q1_link(g, e, error) && current(g, actor, e, error) &&
        q1_schedule(g, e, .01, Q1_THINK_SOURCE_ROGUE_FLAG, error);
}
