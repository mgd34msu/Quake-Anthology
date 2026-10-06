#include "wire_internal.h"
#include "maps/internal.h"
#include "qa/game_q1_bots.h"

bool qa_q1_check_client_eye_read(const qa_q1_game *g, uint32_t slot, qa_vec3 *out,
    qa_error *error) {
    if (!g || !out || g->destroy_pending || g->continuation_pending || !g->wire ||
        slot >= g->options.max_clients || !g->wire->board) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q1 check-client eye lost its physical Source row");
        return false;
    }
    *out = g->wire->board[slot].eye;
    return true;
}
bool qa_q1_check_client_eye_store(qa_q1_game *g, qa_actor_id actor, qa_vec3 eye,
    qa_error *error) {
    uint32_t slot;
    if (!qa_vec_finite(eye) || !qa_q1_native_client_slot(g, actor, &slot, error) ||
        !g->wire || !g->wire->board) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 check-client eye lost its actual Source client");
        return false;
    }
    g->wire->board[slot].eye = eye;
    return true;
}
bool qa_q1_check_client_eye_clear(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    return qa_q1_check_client_eye_store(g, actor, qa_v3(0, 0, 0), error);
}
static bool observer_eye(qa_q1_game *g, qa_actor_id actor, qa_q1_check_client_eye read,
    void *context, qa_vec3 *out, qa_error *error) {
    uint32_t slot;
    if (qa_q1_native_client_slot(g, actor, &slot, NULL)) return read(context, actor, out, error);
    const q1_actor *entity = q1_entity_const(g, actor);
    qa_body_state body;
    if (!entity || !entity->native || !qa_world_body_read(g->services.world, actor, &body, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 check-client observer lost its Source entity");
        return false;
    }
    qa_vec3 offset = qa_v3(0, 0, 0);
    if (entity->map && entity->map->has_view_offset) offset = entity->map->view_offset;
    else if (entity->kind == Q1_MONSTER) {
        qa_builtin_actor_traits traits;
        if (!qa_q1_game_actor_traits(g, actor, &traits)) return false;
        offset.z = traits.view_height;
    }
    *out = qa_vec_add(body.origin, offset);
    return true;
}
typedef struct q1_check_client_context {
    qa_q1_game *game;
    qa_actor_id observer;
    const qa_q1_check_client_source *source;
} q1_check_client_context;
static bool check_client_row(void *opaque, uint32_t slot, bool selection,
    qa_builtin_check_client_row *out, qa_error *error) {
    q1_check_client_context *context = opaque;
    qa_actor_id actor;
    return context->source->player(context->source->context, slot - 1, selection,
                                   &actor, out, error);
}
static bool check_client_eye(void *opaque, uint32_t slot, qa_vec3 *out, qa_error *error) {
    q1_check_client_context *context = opaque;
    qa_actor_id actor;
    qa_builtin_check_client_row row;
    if (!context->source->player(context->source->context, slot - 1, false,
                                 &actor, &row, error)) return false;
    if (!row.present) return qa_q1_check_client_eye_read(context->game, slot - 1, out, error);
    if (!context->source->eye(context->source->context, actor, out, error)) return false;
    context->game->wire->board[slot - 1].eye = *out;
    return true;
}
static bool check_client_observer_eye(void *opaque, qa_vec3 *out, qa_error *error) {
    q1_check_client_context *context = opaque;
    return observer_eye(context->game, context->observer, context->source->eye, context->source->context, out, error);
}
bool qa_q1_game_check_client(qa_q1_game *g, qa_actor_id observer,
    const qa_q1_check_client_source *source, qa_actor_id *out, qa_error *error) {
    if (!source || !source->player || !source->eye || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, observer.slot, "Q1 check-client requires its actual eye reader and result");
        return false;
    }
    *out = (qa_actor_id){0};
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    bool okay = g->wire && g->wire->board;
    if (!okay) qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 check-client requires its physical Source clients");
    q1_check_client_context actual = {g, observer, source};
    qa_builtin_check_client_query query = {.session = g->services.session,
        .provider = g->options.provider, .world = g->services.world,
        .capacity = g->options.max_clients, .slot = &g->check_client_slot,
        .time = &g->check_client_time, .cluster = &g->check_client_cluster,
        .context = &actual, .client = check_client_row, .client_eye = check_client_eye,
        .observer_eye = check_client_observer_eye};
    uint32_t slot;
    if (okay) okay = qa_builtin_check_client(&query, &slot, error);
    if (okay && slot) {
        qa_builtin_check_client_row row;
        okay = source->player(source->context, slot - 1, false, out, &row, error);
        if (okay && (!row.present || !out->registry)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q1 check-client lost its returned physical Source player");
            okay = false;
        }
    }
    if (okay && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, observer.slot, "Q1 check-client lost its retained Source operation");
        okay = false;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}

static bool snapshot_acquire(qa_q1_game *g, bool players, qa_builtin_snapshot_frame **out,
                             qa_error *error) {
    qa_builtin_snapshot_frame *snapshot = qa_builtin_snapshot_acquire(&g->snapshots, 0, error);
    if (!snapshot)
        return false;
    if (!(players ? qa_builtin_players(&g->services, &snapshot->snapshot, error)
                  : qa_builtin_observations(&g->services, &snapshot->snapshot, error))) {
        qa_builtin_snapshot_release(snapshot);
        return false;
    }
    *out = snapshot;
    return true;
}

bool q1_snapshot_actors(qa_q1_game *g, qa_builtin_snapshot_frame **out, qa_error *error) {
    return snapshot_acquire(g, false, out, error);
}
bool q1_snapshot_players(qa_q1_game *g, qa_builtin_snapshot_frame **out, qa_error *error) {
    return snapshot_acquire(g, true, out, error);
}
bool q1_snapshot_targets(qa_q1_game *g, qa_targets *targets, qa_string_id name,
                         qa_builtin_snapshot_frame **out, qa_error *error) {
    qa_builtin_snapshot_frame *snapshot = qa_builtin_snapshot_acquire(&g->snapshots,
        qa_actors_capacity(qa_session_actors(g->services.session)), error);
    if (!snapshot) return false;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    size_t count = 0;
    while (qa_targets_next(targets, name, &cursor, &actor))
        snapshot->snapshot.ids[count++] = actor;
    snapshot->snapshot.count = count;
    *out = snapshot;
    return true;
}

bool q1_radius_snapshot(qa_q1_game *g, qa_vec3 origin, float radius, qa_builtin_snapshot_frame **out,
                        qa_error *error) {
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        qa_actor_id actor = snapshot->snapshot.ids[i];
        const q1_actor *native = q1_entity_const(g, actor);
        qa_body_state body;
        if ((native && native->physics.solid == QA_PHYSICS_NOT_SOLID) ||
            !qa_world_body_read(g->services.world, actor, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(origin, center)) <= radius)
            snapshot->snapshot.ids[count++] = actor;
    }
    snapshot->snapshot.count = count;
    *out = snapshot;
    return true;
}
