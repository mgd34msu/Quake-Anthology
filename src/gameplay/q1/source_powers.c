#include "internal.h"
#include "qa/game_q1_source_powers.h"

typedef struct power_call {
    qa_q1_game_operation operation;
    qa_actor_id actor;
    q1_player *player;
} power_call;

static bool current(power_call *call, qa_error *error) {
    if (qa_q1_game_operation_live(&call->operation) && call->player &&
        q1_player_get(call->operation.game, call->actor) == call->player) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, call->actor.slot,
        "Q1 timed power lost its actual player owner");
    return false;
}
static bool begin(qa_q1_game *game, qa_actor_id actor, bool allocate,
    power_call *call, qa_error *error) {
    if (!qa_q1_game_operation_begin(game, &call->operation, error)) return false;
    call->actor = actor;
    call->player = allocate ? q1_player_allocate(game, actor, error) : q1_player_get(game, actor);
    return current(call, error);
}
static bool publish(power_call *call, qa_q1_power power, double expires, qa_error *error) {
    qa_q1_game *game = call->operation.game;
    if (game->host.powerup) {
        if (!game->host.powerup(game->host.context, call->actor, power, expires, error) ||
            !current(call, error)) return false;
    } else if (power == QA_Q1_INVULNERABILITY) {
        qa_combat_state traits;
        if (!qa_combat_read_traits(game->services.combat, call->actor, &traits, error) ||
            !current(call, error)) return false;
        traits.invulnerable = expires > game->time ||
            (call->player->source_client && call->player->source_god_mode);
        if (!qa_combat_set_traits(game->services.combat, call->actor, &traits, error) ||
            !current(call, error)) return false;
    }
    return true;
}
void q1_powers_forget(q1_player *player) {
    memset(player->power_expires, 0, sizeof(player->power_expires));
    memset(player->power_order, 0, sizeof(player->power_order));
}
static bool assign(qa_q1_game *game, qa_actor_id actor, qa_q1_power power,
    double expires, bool present, bool duration, qa_error *error) {
    if (power < QA_Q1_QUAD || power >= QA_Q1_POWER_COUNT || !isfinite(expires) ||
        (!present && expires != 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Invalid Q1 timed power");
        return false;
    }
    power_call call = {0};
    bool ok = begin(game, actor, true, &call, error);
    if (ok) {
        call.player->power_warned &= (uint16_t)~(1u << power);
        call.player->power_lost &= (uint16_t)~(1u << power);
    }
    if (ok && present && power == QA_Q1_ANTIGRAV) {
        if (!game->host.set_gravity) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                "Q1 anti-gravity requires selected gravity owner");
            ok = false;
        } else ok = game->host.set_gravity(game->host.context, actor, 0.25f, error) &&
            current(&call, error);
    }
    if (ok && duration) expires = (double)(float)(game->time + expires);
    if (ok && present && !call.player->power_order[power]) {
        if (call.player->power_sequence == UINT64_MAX) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 timed power insertion order exhausted");
            ok = false;
        } else call.player->power_order[power] = ++call.player->power_sequence;
    }
    if (ok) {
        if (!present) call.player->power_order[power] = 0;
        call.player->power_expires[power] = expires;
        ok = publish(&call, power, expires, error);
        if (ok && present) ok = qa_builtin_emit(&game->services,
            &(qa_builtin_event){.kind = QA_BUILTIN_Q1_POWERUP,
                .family = QA_GAME_Q1, .provider = game->options.provider,
                .actor = actor, .time_ns = game->time_ns,
                .q1_powerup = {.power = (uint32_t)power, .expires = expires}}, error) &&
            current(&call, error);
    }
    qa_q1_game_operation_end(&call.operation);
    return ok;
}
bool q1_power_assign(qa_q1_game *game, qa_actor_id actor, qa_q1_power power,
    double expires, bool present, qa_error *error) {
    return assign(game, actor, power, expires, present, false, error);
}
bool q1_power_give(qa_q1_game *game, qa_actor_id actor, qa_q1_power power,
    double duration, qa_error *error) {
    return assign(game, actor, power, duration, true, true, error);
}
static bool next(q1_player *player, uint64_t cursor, qa_q1_power *out) {
    uint64_t order = 0;
    for (unsigned i = 0; i < QA_Q1_POWER_COUNT; ++i)
        if (player->power_order[i] > cursor && (!order || player->power_order[i] < order)) {
            order = player->power_order[i];
            *out = (qa_q1_power)i;
        }
    return order != 0;
}
bool qa_q1_player_powers_clear(qa_q1_game *game, qa_actor_id actor, qa_error *error) {
    power_call call = {0};
    bool ok = begin(game, actor, false, &call, error);
    uint64_t cursor = 0;
    qa_q1_power power;
    while (ok && next(call.player, cursor, &power)) {
        cursor = call.player->power_order[power];
        ok = publish(&call, power, 0, error);
    }
    if (ok) q1_powers_forget(call.player);
    qa_q1_game_operation_end(&call.operation);
    return ok;
}
bool q1_powers_expire(qa_q1_game *game, qa_actor_id actor, double seconds, qa_error *error) {
    power_call call = {0};
    bool ok = begin(game, actor, false, &call, error);
    uint64_t cursor = 0;
    qa_q1_power power;
    while (ok && next(call.player, cursor, &power)) {
        cursor = call.player->power_order[power];
        if (call.player->power_expires[power] > seconds) continue;
        call.player->power_order[power] = 0;
        call.player->power_expires[power] = 0;
        ok = publish(&call, power, 0, error);
    }
    qa_q1_game_operation_end(&call.operation);
    return ok;
}
