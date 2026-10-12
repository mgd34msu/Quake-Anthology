#include "guest_q3_weapons_services_private.h"
#include "control_frame.h"
#include "qa/source_save.h"

static bool prefix(qa_source_save_io *io, size_t *count)
{
    uint8_t magic[8] = {'Q','A','G','3','W','R',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','W','R',0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)))
        return application_fail(io->error, QA_ERROR_FORMAT, "Invalid original weapon request continuation");
    size_t maximum = qa_actors_capacity(qa_session_actors(io->session));
    return qa_source_save_count(io, count, maximum);
}
static bool request_fields(qa_source_save_io *io, q3_weapon_request *request)
{
    if (!qa_source_save_actor(io, &request->actor) || !qa_source_save_i32(io, &request->weapon)) return false;
    return request->actor.registry && request->weapon >= 0 ? true :
        application_fail(io->error, QA_ERROR_FORMAT, "Saved Source weapon request has no full actor or selection");
}
bool application_q3_weapons_services_checkpoint(application_q3_weapons_services *s,
    qa_buffer *out, qa_error *error)
{
    if (!s || !out || !application_q3_weapons_services_idle(s) ||
        !application_q3_weapons_idle(s->role->weapons) || !application_q3_weapons_services_validate(s, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon requests require completed actual Source continuations");
    qa_source_save_io io = {0};
    size_t count = s->request_count;
    bool ok = qa_source_save_writer(&io, s->role->engine->provider->application->session, error) &&
        prefix(&io, &count);
    for (q3_weapon_actor_state *state = s->first_request; ok && state; state = state->request_next) {
        q3_weapon_request request = {state->actor, state->weapon};
        ok = request_fields(&io, &request);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_weapons_services_restore(application_q3_weapons_services *s,
    qa_bytes bytes, qa_error *error)
{
    if (!s || !application_q3_weapons_services_idle(s) || s->request_count ||
        !s->role->engine->restore_pending || s->role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon requests restore only into an isolated Source constructor");
    q3_weapon_request *requests = NULL; size_t count = 0;
    qa_application *app = s->role->engine->provider->application;
    qa_unified_frame_lease *storage = NULL;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) && prefix(&io, &count);
    if (ok && (count > (io.input.size - io.offset) / 17 || count > SIZE_MAX / sizeof(*requests)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Truncated original weapon request actors");
    if (ok && count) {
        storage = application_control_storage_acquire(app, error);
        if (storage) requests = qa_unified_frame_lease_alloc(storage, count, sizeof(*requests),
            _Alignof(q3_weapon_request), error);
        ok = requests != NULL;
    }
    for (size_t i = 0; ok && i < count; ++i) {
        ok = request_fields(&io, requests + i);
        for (size_t j = 0; ok && j < i; ++j) if (qa_actor_id_equal(requests[i].actor, requests[j].actor))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved Source weapon requests duplicate a full actor");
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        for (size_t i = 0; ok && i < count; ++i)
            ok = q3_weapon_request_store(s, requests[i].actor, requests[i].weapon, error);
        if (!ok) q3_weapon_requests_clear(s);
    }
    qa_unified_frame_lease_release(storage); return ok;
}
bool application_q3_weapons_services_validate(application_q3_weapons_services *s, qa_error *error)
{
    if (!s || !application_q3_weapons_services_idle(s))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source weapon requests need their idle retained owner");
    for (q3_weapon_actor_state *state = s->first_request; state; state = state->request_next) {
        q3_weapon_actor source;
        if (!q3_weapon_services_source(s, state->actor, &source, error)) return false;
    }
    return true;
}
