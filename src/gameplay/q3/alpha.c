#include "map/internal.h"

float q3_initial_alpha(const qa_q3_game *game, qa_actor_id id) {
    const qa_q3_map_actor_state *map = q3_map_const(game, id);
    return map ? map->alpha : 1;
}

bool qa_q3_alpha_read(const qa_q3_game *game, qa_actor_id id, float *out,
                      qa_error *error) {
    if (!game || !out)
        return q3_fail(error, "Q3 alpha read requires a source and output");
    const q3_actor *actor = q3_actor_const(game, id);
    if (actor) {
        *out = actor->alpha;
        return true;
    }
    const qa_q3_map_actor_state *map = q3_map_const(game, id);
    if (!map)
        return q3_fail(error, "Q3 actor has no alpha owner");
    *out = map->alpha;
    return true;
}

bool qa_q3_alpha(qa_q3_game *game, qa_actor_id id, float value, qa_error *error) {
    if (!game || game->source_restored || !isfinite(value))
        return q3_fail(error, "Q3 alpha write requires a connected source and finite value");
    q3_actor *actor = q3_actor_get(game, id);
    if (actor) {
        actor->alpha = value;
        return true;
    }
    qa_q3_map_actor_state *map = q3_map_get(game, id);
    if (!map)
        return q3_fail(error, "Q3 actor has no alpha owner");
    map->alpha = value;
    return true;
}
