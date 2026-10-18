#include "entities/internal.h"
#include "qa/game_q2_monsters.h"

typedef struct alpha_change {
    qa_q2_game *game;
    float value;
    bool *field_present;
} alpha_change;

static float *alpha_owner(qa_q2_game *game, qa_actor_id id) {
    q2_actor *actor = id.slot < game->capacity ? game->actors[id.slot] : NULL;
    if (!actor || !qa_actor_id_equal(actor->id, id))
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
    return NULL;
}

bool qa_q2_alpha_read(qa_q2_game *game, qa_actor_id id, float *out,
    bool *field_present, qa_error *error) {
    if (!game || !out || !field_present || !q2_actor_live(game, id)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 alpha read requires its live actor and outputs");
        return false;
    }
    const float *alpha = alpha_owner(game, id);
    *field_present = alpha != NULL;
    *out = alpha ? *alpha : 1;
    return true;
}

static bool alpha_write(void *opaque, qa_actor_id id, qa_error *error) {
    alpha_change *change = opaque;
    (void)error;
    float *alpha = alpha_owner(change->game, id);
    *change->field_present = alpha != NULL;
    if (alpha) *alpha = change->value;
    return true;
}

bool qa_q2_alpha(qa_q2_game *game, qa_actor_id id, float value,
    bool *field_present, qa_error *error) {
    if (!game || !field_present || !isfinite(value) || !q2_actor_live(game, id)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 source alpha must be finite");
        return false;
    }
    *field_present = alpha_owner(game, id) != NULL;
    if (!*field_present) return true;
    alpha_change change = {game, value, field_present};
    return qa_q2_run_actor(game, id, alpha_write, &change, error);
}
