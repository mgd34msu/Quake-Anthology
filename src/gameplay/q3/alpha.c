#include "map/internal.h"

float q3_initial_alpha(const qa_q3_game *game, qa_actor_id id) {
    const qa_q3_map_actor_state *map = q3_map_const(game, id);
    return map ? map->alpha : 1;
}

static float *alpha_field(qa_q3_game *game, qa_actor_id id) {
    q3_actor *actor = q3_actor_get(game, id);
    if (actor) return &actor->alpha;
    qa_q3_map_actor_state *map = q3_map_get(game, id);
    return map ? &map->alpha : NULL;
}

bool qa_q3_alpha_read(qa_q3_game *game, qa_actor_id id, float *out,
                      bool *field_present, qa_error *error) {
    if (!game || !out || !field_present ||
        !qa_actors_get(qa_session_actors(game->options.services.session), id))
        return q3_fail(error, "Q3 alpha read requires its live actor and outputs");
    float *field = alpha_field(game, id);
    *field_present = field != NULL;
    *out = field ? *field : 1;
    return true;
}

bool qa_q3_alpha(qa_q3_game *game, qa_actor_id id, float value,
    bool *field_present, qa_error *error) {
    if (!game || !field_present || !isfinite(value) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), id))
        return q3_fail(error, "Q3 alpha write requires its live actor and finite value");
    float *field = alpha_field(game, id);
    *field_present = field != NULL;
    if (field) *field = value;
    return true;
}
