#include "qa/rankings.h"
#include "qa/rankings_save.h"
#include "save_io.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef qa_rankings_checkpoint_player ranked_player;
struct qa_rankings {
    qa_ranking_provider provider;
    qa_ranking_observers observers;
    qa_ranking_state state;
    ranked_player *players;
    size_t count, capacity;
    uint64_t match;
    bool configured, has_match, busy, restore_pending, restore_loaded, backend_failed;
};
bool qa_rankings_backend_failed(const qa_rankings *rankings) {
    return rankings && !rankings->busy && rankings->backend_failed;
}
static bool backend(qa_rankings *rankings, bool ok) {
    if (!ok) rankings->backend_failed = true;
    return ok;
}
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
    if (!rankings || rankings->busy || rankings->restore_pending)
        return fail(error, "Ranking owner is absent or already in an operation");
    rankings->busy = true;
    rankings->backend_failed = false;
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
bool qa_rankings_create_restored(const qa_ranking_provider *provider,
                                 const qa_ranking_observers *observers,
                                 qa_rankings **out, qa_error *error) {
    if (!out || *out) return fail(error, "Restored rankings require an empty output");
    if (!qa_rankings_create(provider, observers, out, error)) return false;
    (*out)->restore_pending = true;
    return true;
}
bool qa_rankings_provider_configured(const qa_rankings *rankings) {
    return rankings && rankings->configured;
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
        ok = backend(rankings, rankings->provider.begin(rankings->provider.context, key, &rankings->match, error));
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
    bool ok = backend(rankings, rankings->provider.login(rankings->provider.context, rankings->match, request, &login,
                                       error));
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
        ok = backend(rankings, rankings->provider.join(rankings->provider.context, rankings->match, login.account,
                                     error));
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
            ok = backend(rankings, rankings->provider.report(rankings->provider.context, rankings->match, report,
                                           error));
    }
    return leave(rankings, ok, error);
}
bool qa_rankings_report_integer(qa_rankings *rankings, int32_t self, int32_t other, int32_t key,
                                double value, bool accumulate, qa_error *error) {
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
              backend(rankings, rankings->provider.poll(rankings->provider.context, error));
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
            ok = backend(rankings, rankings->provider.logout(rankings->provider.context, rankings->match,
                                           current.account, error));
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
                ok = backend(rankings, rankings->provider.logout(rankings->provider.context, rankings->match,
                                               current.account, error));
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
            !backend(rankings, rankings->provider.logout(rankings->provider.context, rankings->match,
                                       current->state.account, &local)))
            collect(&first, &local, &failures);
        qa_ranking_player fresh = {.kind = QA_RANKING_NEW_PLAYER};
        notify(rankings, current->slot, &fresh);
    }
    qa_error local = {0};
    if (rankings->configured &&
        !backend(rankings, rankings->provider.finish(rankings->provider.context, rankings->match, &local)))
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
    bool ok = rankings->restore_pending || qa_rankings_end(rankings, error);
    free(rankings->players);
    free(rankings);
    return ok;
}

static const uint8_t rankings_magic[8] = {'Q','A','R','K',1,0,0,0};
static bool zero_bytes(const void *bytes, size_t count) {
    const uint8_t *p = bytes;
    for (size_t i = 0; i < count; ++i) if (p[i]) return false;
    return true;
}
static bool rankings_ready(const qa_rankings *r, qa_error *error) {
    bool ok = r->count <= r->capacity &&
              (!r->capacity || (r->players && r->capacity >= 16 &&
                                !(r->capacity & (r->capacity - 1)))) &&
              r->capacity <= SIZE_MAX / sizeof(*r->players) &&
              (!r->has_match || r->configured) && (r->has_match || !r->count) &&
              (r->configured || !r->match) &&
              (r->state.kind == QA_RANKING_DISABLED || r->state.kind == QA_RANKING_UNAVAILABLE ||
               r->state.kind == QA_RANKING_ACTIVE) &&
              (r->state.kind != QA_RANKING_ACTIVE || (r->has_match && r->state.game_id == r->match)) &&
              (r->state.kind == QA_RANKING_ACTIVE || r->state.game_id == 0) &&
              (r->state.kind != QA_RANKING_DISABLED || !r->has_match) &&
              memchr(r->state.reason, 0, sizeof(r->state.reason)) &&
              (r->state.kind == QA_RANKING_UNAVAILABLE ||
               zero_bytes(r->state.reason, sizeof(r->state.reason)));
    if (ok) {
        size_t used = strlen(r->state.reason) + 1;
        ok = zero_bytes(r->state.reason + used, sizeof(r->state.reason) - used);
    }
    for (size_t i = 0; ok && i < r->count; ++i) {
        const ranked_player *p = r->players + i;
        ok = p->slot >= 0 &&
             (p->state.kind == QA_RANKING_NEW_PLAYER || p->state.kind == QA_RANKING_SPECTATOR ||
              p->state.kind == QA_RANKING_ACTIVE_PLAYER || p->state.kind == QA_RANKING_DENIED_PLAYER) &&
             memchr(p->state.reason, 0, sizeof(p->state.reason));
        if (p->state.kind != QA_RANKING_DENIED_PLAYER)
            ok = ok && zero_bytes(p->state.reason, sizeof(p->state.reason));
        if (p->state.kind == QA_RANKING_NEW_PLAYER || p->state.kind == QA_RANKING_SPECTATOR)
            ok = ok && p->state.account.player_id == 0 && p->state.account.rank == 0 &&
                 !signbit(p->state.account.rank);
        if (p->state.kind == QA_RANKING_ACTIVE_PLAYER) ok = ok && isfinite(p->state.account.rank);
        for (size_t j = 0; ok && j < i; ++j)
            if (r->players[j].slot == p->slot ||
                (p->state.kind == QA_RANKING_ACTIVE_PLAYER &&
                 r->players[j].state.kind == QA_RANKING_ACTIVE_PLAYER &&
                 r->players[j].state.account.player_id == p->state.account.player_id)) ok = false;
    }
    if (!ok) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid private rankings continuation");
    return ok;
}
static bool rankings_fields(qa_source_save_io *io, qa_rankings *r) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t state_kind = r->state.kind;
    if (!qa_source_save_u32(io, &state_kind) || state_kind > QA_RANKING_ENDING ||
        !qa_source_save_u64(io, &r->state.game_id) ||
        !qa_source_save_bytes(io, r->state.reason, sizeof(r->state.reason)) ||
        !qa_source_save_u64(io, &r->match) || !qa_source_save_bool(io, &r->has_match) ||
        !ps_count(io, &r->capacity, 1, sizeof(*r->players)) ||
        !qa_source_save_count(io, &r->count, r->capacity)) return false;
    r->state.kind = (qa_ranking_service_kind)state_kind;
    if (reading && r->capacity) {
        r->players = calloc(r->capacity, sizeof(*r->players));
        if (!r->players) return ps_fail(io, QA_ERROR_MEMORY, "Allocating ranked player continuation");
    }
    for (size_t i = 0; i < r->capacity; ++i) {
        bool occupied = i < r->count;
        if (!qa_source_save_bool(io, &occupied) || occupied != (i < r->count))
            return ps_fail(io, QA_ERROR_FORMAT, "Invalid rankings physical row partition");
        if (!occupied) continue;
        ranked_player copy = reading ? (ranked_player){0} : r->players[i];
        uint32_t kind = copy.state.kind;
        if (!qa_source_save_i32(io, &copy.slot) || !qa_source_save_u32(io, &kind) ||
            kind > QA_RANKING_DENIED_PLAYER ||
            !qa_source_save_u64(io, &copy.state.account.player_id) ||
            !qa_source_save_f64(io, &copy.state.account.rank) ||
            !qa_source_save_bytes(io, copy.state.reason, sizeof(copy.state.reason))) return false;
        copy.state.kind = (qa_ranking_player_kind)kind;
        if (reading) r->players[i] = copy;
    }
    return rankings_ready(r, io->error);
}
static bool provider_same(const qa_ranking_provider *a, const qa_ranking_provider *b) {
    return a->context == b->context && a->begin == b->begin && a->login == b->login &&
           a->join == b->join && a->report == b->report && a->poll == b->poll &&
           a->logout == b->logout && a->finish == b->finish &&
           ((!a->endpoint && !b->endpoint) ||
            (a->endpoint && b->endpoint && !strcmp(a->endpoint, b->endpoint)));
}
static bool observers_same(const qa_ranking_observers *a, const qa_ranking_observers *b) {
    return a->context == b->context && a->player == b->player && a->service == b->service;
}
static bool continuation_ready(const qa_rankings *r, const qa_rankings_checkpoint_refs *refs,
                                qa_error *error) {
    if (!r->configured) return true;
    qa_rankings_continuation view = {r->state, r->match, r->has_match, r->players,
                                     r->count, r->capacity};
    if (!refs || !refs->continuation_ready)
        return fail(error, "Configured rankings need actual backend continuation qualification");
    return refs->continuation_ready(refs->context, &r->provider, &view, error);
}
bool qa_rankings_checkpoint(const qa_rankings *source, const qa_rankings_checkpoint_refs *refs,
                            qa_buffer *out, qa_error *error) {
    if (!source || source->busy || !out || out->data || out->size)
        return fail(error, "Rankings checkpoint requires an idle owner and empty output");
    qa_rankings *owner = (qa_rankings *)source;
    qa_rankings copy = *source;
    owner->busy = true;
    qa_buffer provider_key = {0}, observer_key = {0};
    bool observers = copy.observers.context || copy.observers.player || copy.observers.service;
    bool context = copy.provider.context != NULL;
    bool player_hook = copy.observers.player != NULL, service_hook = copy.observers.service != NULL;
    bool observer_context = copy.observers.context != NULL;
    char *endpoint = (char *)copy.provider.endpoint;
    bool ok = rankings_ready(&copy, error);
    if (ok && copy.configured)
        ok = refs && refs->provider_capture &&
             refs->provider_capture(refs->context, &copy.provider, &provider_key, error);
    if (ok && observers)
        ok = refs && refs->observers_capture &&
             refs->observers_capture(refs->context, &copy.observers, &observer_key, error);
    if (ok) ok = continuation_ready(&copy, refs, error);
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, NULL, error) && ps_magic(&io, rankings_magic) &&
                 qa_source_save_bool(&io, &copy.configured) && qa_source_save_owned_text(&io, &endpoint) &&
                 qa_source_save_bool(&io, &context) && ps_blob(&io, &provider_key) &&
                 qa_source_save_bool(&io, &observers) && qa_source_save_bool(&io, &observer_context) &&
                 qa_source_save_bool(&io, &player_hook) && qa_source_save_bool(&io, &service_hook) &&
                 ps_blob(&io, &observer_key) && rankings_fields(&io, &copy) &&
                 qa_source_save_finish(&io, out);
    if (!ok && error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Unqualified rankings checkpoint binding");
    qa_source_save_dispose(&io);
    qa_buffer_free(&provider_key); qa_buffer_free(&observer_key);
    owner->busy = false;
    return ok;
}
bool qa_rankings_restore(qa_rankings *owner, const qa_rankings_checkpoint_refs *refs,
                         qa_bytes bytes, qa_error *error) {
    if (!owner || owner->busy || !owner->restore_pending || owner->restore_loaded ||
        owner->players || owner->capacity ||
        owner->count || owner->has_match || owner->match ||
        owner->state.kind != QA_RANKING_DISABLED)
        return fail(error, "Rankings import requires its stable restored empty owner");
    owner->busy = true;
    qa_rankings scratch = {0};
    qa_buffer provider_key = {0}, observer_key = {0};
    char *endpoint = NULL;
    bool context = false, observers = false, observer_context = false;
    bool player_hook = false, service_hook = false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && ps_magic(&io, rankings_magic) &&
              qa_source_save_bool(&io, &scratch.configured) && qa_source_save_owned_text(&io, &endpoint) &&
              qa_source_save_bool(&io, &context) && ps_blob(&io, &provider_key) &&
              qa_source_save_bool(&io, &observers) && qa_source_save_bool(&io, &observer_context) &&
              qa_source_save_bool(&io, &player_hook) && qa_source_save_bool(&io, &service_hook) &&
              ps_blob(&io, &observer_key) && rankings_fields(&io, &scratch) &&
              qa_source_save_finish(&io, NULL);
    if (ok) ok = scratch.configured == owner->configured &&
                 (observers == (observer_context || player_hook || service_hook));
    if (ok && scratch.configured) {
        ok = refs && refs->provider_resolve &&
             refs->provider_resolve(refs->context, (qa_bytes){provider_key.data, provider_key.size},
                                     &scratch.provider, error) &&
             provider_same(&scratch.provider, &owner->provider) &&
             context == (scratch.provider.context != NULL) &&
             ((!endpoint && !scratch.provider.endpoint) ||
              (endpoint && scratch.provider.endpoint && !strcmp(endpoint, scratch.provider.endpoint)));
    } else if (ok) ok = !endpoint && !context && !provider_key.size;
    if (ok && observers) {
        ok = refs && refs->observers_resolve &&
             refs->observers_resolve(refs->context, (qa_bytes){observer_key.data, observer_key.size},
                                     &scratch.observers, error) &&
             observers_same(&scratch.observers, &owner->observers) &&
             observer_context == (scratch.observers.context != NULL) &&
             player_hook == (scratch.observers.player != NULL) &&
             service_hook == (scratch.observers.service != NULL);
    } else if (ok) ok = !observer_key.size &&
        observers_same(&scratch.observers, &owner->observers);
    if (ok) ok = continuation_ready(&scratch, refs, error);
    if (ok) {
        owner->state = scratch.state; owner->match = scratch.match; owner->has_match = scratch.has_match;
        owner->players = scratch.players; scratch.players = NULL;
        owner->count = scratch.count; owner->capacity = scratch.capacity;
        owner->restore_loaded = true;
    } else if (error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Unqualified rankings continuation binding");
    qa_source_save_dispose(&io);
    qa_buffer_free(&provider_key); qa_buffer_free(&observer_key);
    free(endpoint); free(scratch.players);
    owner->busy = false;
    return ok;
}
void qa_rankings_publish_restored(qa_rankings *rankings) {
    if (rankings && !rankings->busy) rankings->restore_pending = false;
}
void qa_rankings_relinquish_continuation(qa_rankings *rankings) {
    if (rankings && !rankings->busy) rankings->restore_pending = true;
}
bool qa_rankings_handoff(qa_rankings *active, qa_rankings *candidate,
                         qa_rankings_handoff_fn callback, void *context,
                         bool *relinquish_active, qa_error *error) {
    if (!active || !candidate || active == candidate || active->busy || candidate->busy ||
        active->restore_pending || !candidate->restore_pending || !candidate->restore_loaded ||
        !relinquish_active || (candidate->configured && !callback))
        return fail(error, "Ranking handoff requires actual idle source and restored candidate owners");
    bool relinquish = false;
    active->busy = candidate->busy = true;
    bool ok = !candidate->configured || callback(context, active, candidate, &relinquish, error);
    active->busy = candidate->busy = false;
    if (ok) *relinquish_active = relinquish;
    else if (error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Ranking backend ownership handoff failed");
    return ok;
}
