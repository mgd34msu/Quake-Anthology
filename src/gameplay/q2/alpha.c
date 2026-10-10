#include "entities/internal.h"
#include "qa/game_q2_monsters.h"

typedef struct alpha_change {
    qa_q2_game *game;
    float value;
} alpha_change;

static float *alpha_owner(qa_q2_game *game, qa_actor_id id, qa_error *error) {
    q2_actor *actor = q2_actor_get(game, id, false, error);
    if (!actor)
        return NULL;
    if (actor->projectile.kind != Q2_PROJECTILE_NONE)
        return actor->projectile.kind == Q2_RERELEASE_SPAWN_GROWTH
                   ? &actor->projectile.alpha : &actor->alpha;
    qa_q2_monster_view monster;
    if (qa_q2_monster_read(game, id, &monster))
        return &actor->alpha;
    if (actor->client)
        return &actor->client->rule.visual.alpha;
    if (actor->item)
        return &actor->item->visual.alpha;
    if (actor->entity)
        return &actor->entity->visual.alpha;
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Q2 actor has no source alpha owner");
    return NULL;
}

bool qa_q2_alpha_read(qa_q2_game *game, qa_actor_id id, float *out, qa_error *error) {
    if (!game || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 alpha read requires a source and output");
        return false;
    }
    const float *alpha = alpha_owner(game, id, error);
    if (!alpha)
        return false;
    *out = *alpha;
    return true;
}

static bool alpha_write(void *opaque, qa_actor_id id, qa_error *error) {
    alpha_change *change = opaque;
    float *alpha = alpha_owner(change->game, id, error);
    if (!alpha)
        return false;
    *alpha = change->value;
    return true;
}

bool qa_q2_alpha(qa_q2_game *game, qa_actor_id id, float value, qa_error *error) {
    if (!isfinite(value)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 source alpha must be finite");
        return false;
    }
    alpha_change change = {game, value};
    return qa_q2_run_actor(game, id, alpha_write, &change, error);
}
