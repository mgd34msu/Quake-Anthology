#include "qa/rankings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ranked_player {
    int32_t slot;
    qa_ranking_player state;
} ranked_player;
struct qa_rankings {
    qa_ranking_provider provider;
    qa_ranking_observers observers;
    qa_ranking_state state;
    ranked_player *players;
    size_t count, capacity;
    uint64_t match;
    bool configured, has_match, busy;
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static void reason(char out[256], const char *text) {
    (void)snprintf(out, 256, "%s", text ? text : "Ranking service failed");
}
static void state(qa_rankings *rankings, qa_ranking_service_kind kind, const char *text) {
    qa_ranking_state next = {.kind = kind};
    if (kind == QA_RANKING_ACTIVE)
        next.game_id = rankings->match;
    if (kind == QA_RANKING_UNAVAILABLE)
        reason(next.reason, text);
    rankings->state = next;
    if (rankings->observers.service)
        rankings->observers.service(rankings->observers.context, &rankings->state);
}
static bool enter(qa_rankings *rankings, qa_error *error) {
    if (!rankings || rankings->busy)
        return fail(error, "Ranking owner is absent or already in an operation");
    rankings->busy = true;
    return true;
}
static bool leave(qa_rankings *rankings, bool ok, qa_error *error) {
    if (!ok) {
        if (error && error->code == QA_OK)
            fail(error, "Ranking service failed");
        state(rankings, QA_RANKING_UNAVAILABLE, error ? error->message : NULL);
    }
    rankings->busy = false;
    return ok;
}
static size_t find(const qa_rankings *rankings, int32_t slot) {
    if (rankings)
        for (size_t i = 0; i < rankings->count; ++i)
            if (rankings->players[i].slot == slot)
                return i;
    return SIZE_MAX;
}
qa_ranking_state qa_rankings_state(const qa_rankings *rankings) {
    return rankings ? rankings->state : (qa_ranking_state){.kind = QA_RANKING_DISABLED};
}
qa_ranking_player qa_rankings_player(const qa_rankings *rankings, int32_t slot) {
    size_t index = find(rankings, slot);
    return index == SIZE_MAX ? (qa_ranking_player){.kind = QA_RANKING_NEW_PLAYER}
                             : rankings->players[index].state;
}
static void notify(qa_rankings *rankings, int32_t slot, const qa_ranking_player *player) {
    if (rankings->observers.player)
        rankings->observers.player(rankings->observers.context, slot, player);
}
static bool player(qa_rankings *rankings, int32_t slot, qa_ranking_player value, qa_error *error) {
    size_t index = find(rankings, slot);
    if (index == SIZE_MAX) {
        if (rankings->count == rankings->capacity) {
            size_t count = rankings->capacity ? rankings->capacity * 2 : 16;
            if (count < rankings->capacity || count > SIZE_MAX / sizeof(*rankings->players))
                return fail(error, "Too many ranked players");
            ranked_player *players = realloc(rankings->players, count * sizeof(*players));
            if (!players) {
                qa_error_set(error, QA_ERROR_MEMORY, count, "Retaining ranked players");
                return false;
            }
            rankings->players = players;
            rankings->capacity = count;
        }
        index = rankings->count++;
    }
    rankings->players[index] = (ranked_player){slot, value};
    notify(rankings, slot, &rankings->players[index].state);
    return true;
}
bool qa_rankings_create(const qa_ranking_provider *provider, const qa_ranking_observers *observers,
                        qa_rankings **out, qa_error *error) {
    if (!out || (provider &&
                 (!provider->begin || !provider->login || !provider->join || !provider->report ||
                  !provider->poll || !provider->logout || !provider->finish)))
        return fail(error, "Incomplete ranking provider");
    qa_rankings *rankings = calloc(1, sizeof(*rankings));
    if (!rankings) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating ranking lifecycle");
        return false;
    }
    if (provider) {
        rankings->provider = *provider;
        rankings->configured = true;
    }
    if (observers)
        rankings->observers = *observers;
    *out = rankings;
    return true;
}
bool qa_rankings_begin(qa_rankings *rankings, bool enabled, bool single, const char *key,
                       qa_error *error) {
    if (!enter(rankings, error))
        return false;
    bool ok = true;
    if (rankings->has_match)
        ok = fail(error, "Ranking match is already active");
    else if (!enabled || single)
        state(rankings, QA_RANKING_DISABLED, NULL);
    else if (!rankings->configured)
        state(
            rankings, QA_RANKING_UNAVAILABLE,
            "No ranking service is configured. Local progress and match records remain available.");
    else if (!key)
        ok = fail(error, "Missing ranking game key");
    else {
        state(rankings, QA_RANKING_STARTING, NULL);
        ok = rankings->provider.begin(rankings->provider.context, key, &rankings->match, error);
        if (ok) {
            rankings->has_match = true;
            state(rankings, QA_RANKING_ACTIVE, NULL);
        }
    }
    return leave(rankings, ok, error);
}
bool qa_rankings_account(qa_rankings *rankings, int32_t slot, const qa_ranking_request *request,
                         qa_error *error) {
    if (!enter(rankings, error))
        return false;
    if (slot < 0 || !request ||
        (request->kind != QA_RANKING_LOGIN && request->kind != QA_RANKING_CREATE_ACCOUNT) ||
        !request->username || !request->password ||
        (request->kind == QA_RANKING_CREATE_ACCOUNT && !request->email))
        return leave(rankings, fail(error, "Invalid ranking account request"), error);
    if (!rankings->configured || !rankings->has_match || rankings->state.kind != QA_RANKING_ACTIVE)
        return leave(rankings, fail(error, "Ranking service is not active"), error);
    if (qa_rankings_player(rankings, slot).kind == QA_RANKING_ACTIVE_PLAYER)
        return leave(rankings, fail(error, "Ranking player is already active"), error);
    if (!player(rankings, slot, (qa_ranking_player){.kind = QA_RANKING_PENDING_PLAYER}, error))
        return leave(rankings, false, error);
    qa_ranking_login login = {0};
    bool ok = rankings->provider.login(rankings->provider.context, rankings->match, request, &login,
                                       error);
    if (ok && !login.accepted) {
        qa_ranking_player denied = {.kind = QA_RANKING_DENIED_PLAYER};
        memcpy(denied.reason, login.reason, sizeof(denied.reason));
        denied.reason[255] = 0;
        ok = player(rankings, slot, denied, error);
        return leave(rankings, ok, error);
    }
    if (ok && !isfinite(login.account.rank))
        ok = fail(error, "Ranking provider returned a nonfinite rank");
    if (ok)
        for (size_t i = 0; i < rankings->count; ++i) {
            qa_ranking_player *present = &rankings->players[i].state;
            if (present->kind == QA_RANKING_ACTIVE_PLAYER &&
                present->account.player_id == login.account.player_id) {
                ok = fail(error, "Ranking account is already joined");
                break;
            }
        }
    if (ok)
        ok = rankings->provider.join(rankings->provider.context, rankings->match, login.account,
                                     error);
    qa_ranking_player next = {.kind = ok ? QA_RANKING_ACTIVE_PLAYER : QA_RANKING_DENIED_PLAYER,
                              .account = login.account};
    if (!ok)
        reason(next.reason, error ? error->message : "Ranking login failed");
    /* Pending already owns this slot; publication cannot allocate. */
    (void)player(rankings, slot, next, NULL);
    return leave(rankings, ok, error);
}
static bool id(const qa_rankings *rankings, int32_t slot, uint64_t *out) {
    if (slot == -1) {
        *out = 0;
        return true;
    }
    qa_ranking_player current = qa_rankings_player(rankings, slot);
    if (current.kind != QA_RANKING_ACTIVE_PLAYER)
        return false;
    *out = current.account.player_id;
    return true;
}
static bool report(qa_rankings *rankings, int32_t self, int32_t other, qa_ranking_report *report,
                   qa_error *error) {
    if (!enter(rankings, error))
        return false;
    bool ok = true;
    if (rankings->state.kind == QA_RANKING_ACTIVE && rankings->has_match && rankings->configured &&
        id(rankings, self, &report->self) && id(rankings, other, &report->other)) {
        if (report->stat.kind == QA_RANKING_STRING && !report->stat.value.string)
            ok = fail(error, "Missing ranking report text");
        else
            ok = rankings->provider.report(rankings->provider.context, rankings->match, report,
                                           error);
    }
    return leave(rankings, ok, error);
}
bool qa_rankings_report_integer(qa_rankings *rankings, int32_t self, int32_t other, int32_t key,
                                int32_t value, bool accumulate, qa_error *error) {
    qa_ranking_report event = {
        .stat = {.kind = QA_RANKING_INTEGER, .key = key, .value.integer = {value, accumulate}}};
    return report(rankings, self, other, &event, error);
}
bool qa_rankings_report_string(qa_rankings *rankings, int32_t self, int32_t other, int32_t key,
                               const char *value, qa_error *error) {
    qa_ranking_report event = {
        .stat = {.kind = QA_RANKING_STRING, .key = key, .value.string = value}};
    return report(rankings, self, other, &event, error);
}
bool qa_rankings_poll(qa_rankings *rankings, qa_error *error) {
    if (!enter(rankings, error))
        return false;
    bool ok = rankings->state.kind != QA_RANKING_ACTIVE || !rankings->configured ||
              rankings->provider.poll(rankings->provider.context, error);
    return leave(rankings, ok, error);
}
bool qa_rankings_reset(qa_rankings *rankings, int32_t slot, qa_error *error) {
    if (!enter(rankings, error))
        return false;
    qa_ranking_player current = qa_rankings_player(rankings, slot);
    bool ok = slot >= 0 || fail(error, "Invalid ranking player slot");
    if (ok && rankings->state.kind == QA_RANKING_ACTIVE &&
        (current.kind == QA_RANKING_DENIED_PLAYER || current.kind == QA_RANKING_SPECTATOR))
        ok = player(rankings, slot, (qa_ranking_player){.kind = QA_RANKING_NEW_PLAYER}, error);
    return leave(rankings, ok, error);
}
bool qa_rankings_spectate(qa_rankings *rankings, int32_t slot, qa_error *error) {
    if (!enter(rankings, error))
        return false;
    bool ok = slot >= 0 || fail(error, "Invalid ranking player slot");
    if (ok && rankings->state.kind == QA_RANKING_ACTIVE) {
        qa_ranking_player current = qa_rankings_player(rankings, slot);
        if (current.kind == QA_RANKING_ACTIVE_PLAYER && rankings->has_match && rankings->configured)
            ok = rankings->provider.logout(rankings->provider.context, rankings->match,
                                           current.account, error);
        if (ok)
            ok = player(rankings, slot, (qa_ranking_player){.kind = QA_RANKING_SPECTATOR}, error);
    }
    return leave(rankings, ok, error);
}
bool qa_rankings_disconnect(qa_rankings *rankings, int32_t slot, qa_error *error) {
    if (!enter(rankings, error))
        return false;
    bool ok = slot >= 0 || fail(error, "Invalid ranking player slot");
    if (ok) {
        size_t index = find(rankings, slot);
        if (index != SIZE_MAX) {
            qa_ranking_player current = rankings->players[index].state;
            if (current.kind == QA_RANKING_ACTIVE_PLAYER && rankings->has_match &&
                rankings->configured)
                ok = rankings->provider.logout(rankings->provider.context, rankings->match,
                                               current.account, error);
            memmove(rankings->players + index, rankings->players + index + 1,
                    (rankings->count - index - 1) * sizeof(*rankings->players));
            --rankings->count;
        }
        qa_ranking_player fresh = {.kind = QA_RANKING_NEW_PLAYER};
        notify(rankings, slot, &fresh);
    }
    return leave(rankings, ok, error);
}
bool qa_rankings_unavailable(qa_rankings *rankings, const char *text, qa_error *error) {
    if (!enter(rankings, error))
        return false;
    state(rankings, QA_RANKING_UNAVAILABLE, text);
    return leave(rankings, true, error);
}
static void collect(qa_error *first, const qa_error *next, size_t *count) {
    if (!(*count)++) {
        *first = *next;
        if (first->code == QA_OK)
            fail(first, "Ranking cleanup failed");
    }
}
bool qa_rankings_end(qa_rankings *rankings, qa_error *error) {
    if (!enter(rankings, error))
        return false;
    if (!rankings->has_match)
        return leave(rankings, true, error);
    state(rankings, QA_RANKING_ENDING, NULL);
    qa_error first = {0};
    size_t failures = 0;
    for (size_t i = 0; i < rankings->count; ++i) {
        ranked_player *current = rankings->players + i;
        qa_error local = {0};
        if (current->state.kind == QA_RANKING_ACTIVE_PLAYER && rankings->configured &&
            !rankings->provider.logout(rankings->provider.context, rankings->match,
                                       current->state.account, &local))
            collect(&first, &local, &failures);
        qa_ranking_player fresh = {.kind = QA_RANKING_NEW_PLAYER};
        notify(rankings, current->slot, &fresh);
    }
    qa_error local = {0};
    if (rankings->configured &&
        !rankings->provider.finish(rankings->provider.context, rankings->match, &local))
        collect(&first, &local, &failures);
    rankings->has_match = false;
    rankings->match = 0;
    rankings->count = 0;
    state(rankings, QA_RANKING_DISABLED, NULL);
    if (failures && error) {
        *error = first;
        if (failures > 1)
            qa_error_set(error, first.code, failures, "%zu ranking cleanup failures; first: %.170s",
                         failures, first.message);
    }
    return leave(rankings, !failures, error);
}
bool qa_rankings_close_ready(const qa_rankings *rankings) {
    return !rankings || !rankings->busy;
}
bool qa_rankings_close(qa_rankings *rankings, qa_error *error) {
    if (!rankings)
        return true;
    if (rankings->busy)
        return fail(error, "Cannot close active ranking operation");
    bool ok = qa_rankings_end(rankings, error);
    free(rankings->players);
    free(rankings);
    return ok;
}
