#include "map/internal.h"
#include "qa/game_q3_round.h"

bool qa_q3_round_read(const qa_q3_game *game, qa_q3_round_source *out, qa_error *error) {
    if (!game || !out || game->source_restored || !game->map ||
        !game->map->world_spawned || !game->map->post_spawned ||
        !qa_q3_destroy_ready(game))
        return q3_fail(error, "Q3 round capture requires a completed native map safe point");
    *out = (qa_q3_round_source){.product = game->options.product,
        .game_type = game->options.rules.game_type,
        .start_time_ms = game->map->options.start_time_ms,
        .current_time_ms = game->now_ms,
        .random_seed = game->map->options.random_seed};
    return true;
}

bool qa_q3_round_reset(qa_q3_game *game, int32_t source_time_ms, bool warmup,
                        int32_t restarted, qa_error *error) {
    if (!game || game->source_restored || !game->map ||
        !game->map->world_spawned || !game->map->post_spawned)
        return q3_fail(error, "Q3 round reset requires the actual previously spawned source");
    qa_q3_map_options options = game->map->options;
    options.start_time_ms = source_time_ms;
    options.warmup = warmup;
    options.restarted = restarted;
    if (!qa_q3_maps_reset(game, &options, error))
        return false;
    game->attack_sequence = 0;
    return true;
}
