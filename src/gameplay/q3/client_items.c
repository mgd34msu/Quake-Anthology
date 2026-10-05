#include "client_private.h"

bool qa_q3_client_toss_cube(qa_q3_game *game, qa_actor_id actor, int32_t team,
    int32_t timeout_seconds, qa_actor_id *out, qa_error *error) {
    if (!game || !out || game->options.product != QA_Q3_TEAM_ARENA ||
        game->observation_depth == SIZE_MAX ||
        !q3_client_actor(game, actor, error))
        return q3_fail(error, "Q3 cube tossing needs its actual missionpack client");
    *out = (qa_actor_id){0};
    if (!qa_q3_client_tokens_write(game, actor, 0, error)) return false;
    int32_t source_time = game->now_ms;
    bool free_entity = false;
    for (uint32_t slot = QA_Q3_SOURCE_CLIENTS; slot < game->source_count; ++slot)
        if (!game->source_entities[slot].in_use) {
            free_entity = true;
            break;
        }
    if (!free_entity) return true;
    uint32_t item_index;
    if (!qa_q3_find_item(game->options.product,
            team == 1 ? "item_redcube" : "item_bluecube", &item_index))
        return q3_fail(error, "Q3 cube tossing has no actual source cube item");
    ++game->observation_depth;
    qa_vec3 forward;
    q3_source_angle_vectors(qa_v3(0, (float)(source_time % 360), 0), &forward, NULL, NULL);
    qa_vec3 velocity = qa_v3((forward.x * 150),
                            (forward.y * 150),
                            (forward.z * 150));
    float scatter = (q3_crandom(game) * 50);
    velocity.z = (velocity.z + (200 + scatter));
    qa_vec3 origin = qa_v3(0, 0, 0);
    qa_actor_id neutral = game->team_state.neutral_obelisk;
    if (neutral.registry) {
        uint32_t slot;
        qa_q3_entity source;
        qa_q3_wire_visibility visibility;
        if (!qa_q3_source_actor_slot(game, neutral, &slot, error) ||
            !qa_q3_wire_entity_read(game, slot, &source, &visibility, error)) {
            --game->observation_depth;
            return false;
        }
        origin = qa_v3(source.pos.base[0], source.pos.base[1],
                       (source.pos.base[2] + 44));
    }
    qa_actor_id cube;
    bool okay = qa_q3_spawn_item(game, &(qa_q3_item_spawn){.item_index = item_index,
        .dropped = true, .origin = origin, .velocity = velocity},
        &cube, error);
    if (okay) {
        q3_actor *entry = q3_actor_get(game, cube);
        if (!entry || entry->kind != Q3_ACTOR_ITEM)
            okay = q3_fail(error, "Q3 cube constructor lost its actual source actor");
        else {
            entry->spawnflags = team;
            uint32_t bits = (uint32_t)timeout_seconds * 1000u;
            int32_t duration;
            memcpy(&duration, &bits, sizeof(duration));
            entry->state.item.expire_at = q3_add_time(source_time, duration);
            *out = cube;
        }
    }
    --game->observation_depth;
    return okay;
}
