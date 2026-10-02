#include "qa/local_lobby.h"
#include "qa/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct qa_lobby_session {
    qa_lobbies *service;
    qa_local_account account;
    qa_lobby_transitions transitions;
    qa_lobby_id membership;
    const qa_lobby *last;
    uint64_t launched, completed;
    bool busy;
    char names[];
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool begin(qa_lobby_session *session, qa_error *error) {
    if (!session || session->busy)
        return fail(error, "Local lobby session is absent or already in a transition");
    session->busy = true;
    return true;
}
static bool finish(qa_lobby_session *session, bool ok) {
    session->busy = false;
    return ok;
}
static void remember(qa_lobby_session *session, const qa_lobby *room) {
    qa_lobby_retain(room);
    qa_lobby_release(session->last);
    session->last = room;
}
static void failure(qa_error *first, const qa_error *next) {
    if (first->code == QA_OK) {
        *first = *next;
        return;
    }
    size_t used = strlen(first->message);
    const char prefix[] = "; cleanup failed: ";
    size_t available = sizeof(first->message) - used - 1;
    size_t count = sizeof(prefix) - 1;
    if (count > available) count = available;
    memcpy(first->message + used, prefix, count);
    used += count;
    available -= count;
    count = strlen(next->message);
    if (count > available) count = available;
    memcpy(first->message + used, next->message, count);
    first->message[used + count] = '\0';
}
bool qa_lobby_session_create(qa_lobbies *service, qa_local_account account,
                             const qa_lobby_transitions *transitions, qa_lobby_session **out,
                             qa_error *error) {
    if (!service || !account.id || !*account.id || !account.name || !transitions ||
        !transitions->host || !transitions->join || !transitions->leave ||
        !transitions->completed || !out)
        return fail(error, "Missing local lobby session services");
    size_t id_size = strlen(account.id), name_size = strlen(account.name);
    if (!qa_utf8_valid((qa_bytes){(const uint8_t *)account.id, id_size}) ||
        !qa_utf8_valid((qa_bytes){(const uint8_t *)account.name, name_size}) ||
        id_size > SIZE_MAX - sizeof(qa_lobby_session) - 2 ||
        name_size > SIZE_MAX - sizeof(qa_lobby_session) - 2 - id_size)
        return fail(error, "Invalid local lobby account");
    qa_lobby_session *session = calloc(1, sizeof(*session) + id_size + name_size + 2);
    if (!session) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating local lobby membership");
        return false;
    }
    memcpy(session->names, account.id, id_size + 1);
    memcpy(session->names + id_size + 1, account.name, name_size + 1);
    session->account = (qa_local_account){session->names, session->names + id_size + 1};
    session->service = service;
    session->transitions = *transitions;
    *out = session;
    return true;
}
const qa_lobby *qa_lobby_session_current(const qa_lobby_session *session) {
    return session ? qa_lobbies_find(session->service, session->membership) : NULL;
}
static const qa_lobby *require(qa_lobby_session *session, qa_error *error) {
    const qa_lobby *room = qa_lobby_session_current(session);
    const qa_lobby_view *view = qa_lobby_read(room);
    if (view)
        for (size_t i = 0; i < view->member_count; ++i)
            if (!strcmp(view->members[i].account.id, session->account.id))
                return room;
    fail(error, "No current local lobby membership");
    return NULL;
}
bool qa_lobby_session_host(qa_lobby_session *session, const char *name, uint32_t capacity,
                           const qa_lobby_selection *selection, uint32_t seats, qa_error *error) {
    if (!begin(session, error))
        return false;
    bool ok;
    const qa_lobby *room;
    if (session->membership.serial)
        ok = fail(error, "Leave the current lobby before hosting another");
    else {
        ok = qa_lobbies_host(session->service, session->account, name, capacity, selection, seats,
                             &room, error);
        if (ok) {
            session->membership = qa_lobby_read(room)->id;
            remember(session, room);
            session->launched = session->completed = 0;
        }
    }
    return finish(session, ok);
}
bool qa_lobby_session_join(qa_lobby_session *session, qa_lobby_id id, uint32_t seats,
                           qa_error *error) {
    if (!begin(session, error))
        return false;
    bool ok;
    const qa_lobby *room;
    if (session->membership.serial)
        ok = fail(error, "Leave the current lobby before joining another");
    else {
        ok = qa_lobbies_join(session->service, id, session->account, seats, &room, error);
        if (ok) {
            session->membership = id;
            remember(session, room);
            session->launched = session->completed = 0;
        }
    }
    return finish(session, ok);
}
bool qa_lobby_session_ready(qa_lobby_session *session, bool ready, qa_error *error) {
    if (!begin(session, error))
        return false;
    bool ok = require(session, error) && qa_lobbies_ready(session->service, session->membership,
                                                          session->account.id, ready, error);
    return finish(session, ok);
}
bool qa_lobby_session_start(qa_lobby_session *session, qa_error *error) {
    if (!begin(session, error))
        return false;
    const qa_lobby *room;
    bool ok = require(session, error) && qa_lobbies_start(session->service, session->membership,
                                                          session->account.id, &room, error);
    if (!ok)
        return finish(session, false);
    qa_lobby_retain(room);
    const qa_lobby_view *view = qa_lobby_read(room);
    qa_net_address endpoint = {0};
    qa_lobby_wire wire = {0};
    qa_error primary = {0};
    bool bound =
        session->transitions.host(session->transitions.context, view, &endpoint, &wire, &primary);
    const qa_lobby *published;
    ok =
        bound && qa_lobbies_publish(session->service, view->id, session->account.id,
                                    view->match_generation, &endpoint, &wire, &published, &primary);
    if (ok) {
        remember(session, published);
        session->launched = view->match_generation;
    } else {
        if (primary.code == QA_OK)
            fail(&primary, "Local lobby host transition failed");
        qa_error cleanup = {0};
        if (qa_lobbies_find(session->service, view->id) &&
            !qa_lobbies_complete(session->service, view->id, session->account.id,
                                 view->match_generation, NULL, &cleanup))
            failure(&primary, &cleanup);
        cleanup = (qa_error){0};
        if (bound && !session->transitions.leave(session->transitions.context, view, &cleanup)) {
            if (cleanup.code == QA_OK)
                fail(&cleanup, "Bound host teardown failed");
            failure(&primary, &cleanup);
        }
        if (error)
            *error = primary;
    }
    qa_lobby_release(room);
    return finish(session, ok);
}
bool qa_lobby_session_poll(qa_lobby_session *session, qa_error *error) {
    if (!begin(session, error))
        return false;
    if (!session->membership.serial)
        return finish(session, true);
    const qa_lobby *room = qa_lobby_session_current(session);
    bool ok = true;
    if (!room) {
        const qa_lobby *previous = session->last;
        session->last = NULL;
        session->membership = (qa_lobby_id){0};
        if (previous)
            ok = session->transitions.leave(session->transitions.context, qa_lobby_read(previous),
                                            error);
        qa_lobby_release(previous);
    } else {
        remember(session, room); /* This retained version protects callback arguments. */
        const qa_lobby_view *view = qa_lobby_read(room);
        if (view->phase == QA_LOBBY_PLAYING && view->match_generation > session->launched) {
            ok = session->transitions.join(session->transitions.context, view, error);
            if (ok)
                session->launched = view->match_generation;
        } else if (view->phase == QA_LOBBY_OPEN && session->launched > session->completed) {
            ok = session->transitions.completed(session->transitions.context, view, error);
            if (ok)
                session->completed = session->launched;
        }
    }
    return finish(session, ok);
}
bool qa_lobby_session_complete(qa_lobby_session *session, qa_error *error) {
    if (!begin(session, error))
        return false;
    const qa_lobby *room;
    bool ok = require(session, error) &&
              qa_lobbies_complete(session->service, session->membership, session->account.id,
                                  session->launched, &room, error);
    if (ok && session->launched > session->completed) {
        qa_lobby_retain(room);
        ok = session->transitions.completed(session->transitions.context, qa_lobby_read(room),
                                            error);
        if (ok)
            session->completed = session->launched;
        qa_lobby_release(room);
    }
    return finish(session, ok);
}
static bool leave(qa_lobby_session *session, qa_error *error) {
    const qa_lobby *room = qa_lobby_session_current(session);
    if (!room)
        room = session->last;
    qa_lobby_retain(room);
    remember(session, NULL);
    session->membership = (qa_lobby_id){0};
    if (!room)
        return true;
    const qa_lobby_view *view = qa_lobby_read(room);
    qa_error primary = {0}, cleanup = {0};
    bool ok = !qa_lobbies_find(session->service, view->id) ||
              qa_lobbies_leave(session->service, view->id, session->account.id, &primary);
    if (!session->transitions.leave(session->transitions.context, view, &cleanup)) {
        if (cleanup.code == QA_OK)
            fail(&cleanup, "Local lobby leave transition failed");
        failure(&primary, &cleanup);
        ok = false;
    }
    qa_lobby_release(room);
    if (!ok && error)
        *error = primary;
    return ok;
}
bool qa_lobby_session_leave(qa_lobby_session *session, qa_error *error) {
    if (!begin(session, error))
        return false;
    return finish(session, leave(session, error));
}
bool qa_lobby_session_close(qa_lobby_session *session, qa_error *error) {
    if (!session)
        return true;
    if (!begin(session, error))
        return false;
    bool ok = leave(session, error);
    free(session);
    return ok;
}
