#include "guest_q3_weapons_services_private.h"
#include "qa/source_save.h"

static bool fields(qa_source_save_io *io, q3_weapon_request **requests, size_t *count)
{
    uint8_t magic[8] = {'Q','A','G','3','W','R',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','W','R',0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)))
        return application_fail(io->error, QA_ERROR_FORMAT, "Invalid original weapon request continuation");
    size_t maximum = qa_actors_capacity(qa_session_actors(io->session));
    if (!qa_source_save_count(io, count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (*count > (io->input.size - io->offset) / 17 || *count > SIZE_MAX / sizeof(**requests))
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated original weapon request actors");
        *requests = *count ? calloc(*count, sizeof(**requests)) : NULL;
        if (*count && !*requests) return application_fail(io->error, QA_ERROR_MEMORY, "Retaining restored Source weapon requests");
    }
    for (size_t i = 0; i < *count; ++i) {
        q3_weapon_request *request = *requests + i;
        if (!qa_source_save_actor(io, &request->actor) || !qa_source_save_i32(io, &request->weapon)) return false;
        if (!request->actor.registry || request->weapon < 0)
            return application_fail(io->error, QA_ERROR_FORMAT, "Saved Source weapon request has no full actor or selection");
        for (size_t j = 0; j < i; ++j) if (qa_actor_id_equal(request->actor, (*requests)[j].actor))
            return application_fail(io->error, QA_ERROR_FORMAT, "Saved Source weapon requests duplicate a full actor");
    }
    return true;
}
bool application_q3_weapons_services_checkpoint(application_q3_weapons_services *s,
    qa_buffer *out, qa_error *error)
{
    if (!s || !out || !application_q3_weapons_services_idle(s) ||
        !application_q3_weapons_idle(s->role->weapons) || !application_q3_weapons_services_validate(s, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon requests require completed actual Source continuations");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, s->role->engine->provider->application->session, error) &&
        fields(&io, &s->requests, &s->request_count) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_weapons_services_restore(application_q3_weapons_services *s,
    qa_bytes bytes, qa_error *error)
{
    if (!s || !application_q3_weapons_services_idle(s) || s->request_count ||
        !s->role->engine->restore_pending || s->role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon requests restore only into an isolated Source constructor");
    q3_weapon_request *requests = NULL; size_t count = 0;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, s->role->engine->provider->application->session, bytes, error) &&
        fields(&io, &requests, &count) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { free(requests); return false; }
    free(s->requests); s->requests = requests; s->request_count = count; return true;
}
bool application_q3_weapons_services_validate(application_q3_weapons_services *s, qa_error *error)
{
    if (!s || !application_q3_weapons_services_idle(s))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source weapon requests need their idle retained owner");
    for (size_t i = 0; i < s->request_count; ++i) {
        q3_weapon_actor source;
        if (!q3_weapon_services_source(s, s->requests[i].actor, &source, error)) return false;
    }
    return true;
}
