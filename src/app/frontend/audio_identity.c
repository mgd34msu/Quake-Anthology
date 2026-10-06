#include "internal.h"
#include "round.h"
#include "native_q3_client_internal.h"
static uint64_t append_actor(qa_frontend *frontend, qa_actor_id actor, bool retired,
    qa_error *error);
void frontend_audio_retire_round_aliases(qa_frontend *frontend)
{
    for (size_t i = 0; i < frontend->audio_id_count; ++i) frontend->audio_ids[i].retired = true;
}
void frontend_audio_retire_dead_aliases(qa_frontend *frontend)
{
    const qa_actor_registry *actors = qa_world_actors(qa_application_world(frontend->application));
    for (size_t i = 0; i < frontend->audio_id_count; ++i)
        if (!qa_actors_get(actors, frontend->audio_ids[i].actor)) frontend->audio_ids[i].retired = true;
}
uint64_t frontend_audio_actor(qa_frontend *frontend, qa_actor_id actor, qa_error *error)
{
    if (!actor.registry) return QA_AUDIO_NO_ACTOR;
    for (size_t i = 0; i < frontend->audio_id_count; ++i)
        if (!frontend->audio_ids[i].retired && qa_actor_id_equal(frontend->audio_ids[i].actor, actor))
            return frontend->audio_ids[i].id;
    if (!qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), actor)) {
        for (size_t i = 0; i < frontend->audio_id_count; ++i)
            if (frontend->audio_ids[i].retired && qa_actor_id_equal(frontend->audio_ids[i].actor, actor))
                return frontend->audio_ids[i].id;
        return QA_AUDIO_NO_ACTOR;
    }
    return append_actor(frontend, actor, false, error);
}
static uint64_t append_actor(qa_frontend *frontend, qa_actor_id actor, bool retired,
    qa_error *error)
{
    if (frontend->next_audio_id >= UINT64_MAX - 1) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "audio actor identity exhausted"); return QA_AUDIO_NO_ACTOR;
    }
    if (frontend->audio_id_count == frontend->audio_id_capacity) {
        size_t capacity = frontend->audio_id_capacity ? frontend->audio_id_capacity * 2 : 128;
        if (capacity < frontend->audio_id_capacity || capacity > SIZE_MAX / sizeof(*frontend->audio_ids)) {
            frontend_fail(error, QA_ERROR_MEMORY, "audio actor identity table overflow"); return QA_AUDIO_NO_ACTOR;
        }
        frontend_audio_identity *ids = realloc(frontend->audio_ids, capacity * sizeof(*ids));
        if (!ids) { frontend_fail(error, QA_ERROR_MEMORY, "allocating audio actor identities"); return QA_AUDIO_NO_ACTOR; }
        frontend->audio_ids = ids; frontend->audio_id_capacity = capacity;
    }
    uint64_t id = ++frontend->next_audio_id;
    frontend->audio_ids[frontend->audio_id_count++] =
        (frontend_audio_identity){.actor = actor, .id = id, .retired = retired};
    return id;
}
static uint64_t retained_actor(qa_frontend *frontend, qa_actor_id actor, qa_error *error)
{
    uint64_t id = frontend_audio_actor(frontend, actor, error);
    if (id != QA_AUDIO_NO_ACTOR || (error && error->code)) return id;
    if (qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), actor))
        return QA_AUDIO_NO_ACTOR;
    return append_actor(frontend, actor, true, error);
}
uint64_t frontend_audio_native_q3_actor(frontend_native_q3 *row,
    uint32_t source_number, qa_error *error)
{
    if (!row || !frontend_native_q3_current(row) || !row->frontend->audio ||
        row->frontend->capture || row->frontend->resource_inventory || row->frontend->source_restoring ||
        source_number >= QA_Q3_ENTITY_WORLD) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q3 sound requires its actual installed CLIENT receipt");
        return QA_AUDIO_NO_ACTOR;
    }
    qa_actor_id actor;
    bool present;
    if (!qa_native_q3_wire_reader_actor(row->view.reader, source_number, &actor, &present, error))
        return QA_AUDIO_NO_ACTOR;
    if (!actor.registry) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q3 sound has no mapped Source actor receipt");
        return QA_AUDIO_NO_ACTOR;
    }
    /* CG can first position a received entity after GAME has freed it. Its
     * retained full generation owns the sound; its decoded position follows
     * through S_UpdateEntityPosition, without reading a replacement body. */
    return retained_actor(row->frontend, actor, error);
}
static bool retained_event_source(qa_frontend *frontend, qa_actor_owner owner, qa_game_family family,
    qa_error *error)
{
    qa_clock_state clock;
    if (!qa_session_clock(qa_application_session(frontend->application),
        owner, &clock)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound lost its actual Source clock");
        return false;
    }
    qa_console_dialect dialect;
    switch (clock.frame.kind) {
    case QA_CLOCK_NETQUAKE: dialect = QA_CONSOLE_Q1; break;
    case QA_CLOCK_QUAKEWORLD: dialect = QA_CONSOLE_QW; break;
    case QA_CLOCK_Q2_CLASSIC: dialect = QA_CONSOLE_Q2; break;
    case QA_CLOCK_Q2_RERELEASE: dialect = QA_CONSOLE_Q2_RERELEASE; break;
    case QA_CLOCK_Q3: dialect = QA_CONSOLE_Q3; break;
    default:
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound has no gameplay Source clock");
        return false;
    }
    bool matches = family == QA_GAME_Q1 ? dialect == QA_CONSOLE_Q1 || dialect == QA_CONSOLE_QW :
        family == QA_GAME_Q2 ? dialect == QA_CONSOLE_Q2 || dialect == QA_CONSOLE_Q2_RERELEASE :
        family == QA_GAME_Q3 && dialect == QA_CONSOLE_Q3;
    if (!matches)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound changed its Source family");
    qa_command_context source = {.owner = owner, .origin = QA_COMMAND_SERVER, .dialect = dialect};
    qa_command_context captured;
    if (!qa_application_capture_command_context(frontend->application, &source, &captured, error))
        return false;
    return true;
}
uint64_t frontend_audio_retained_event_actor(qa_frontend *frontend,
    const qa_builtin_event *event, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->stepping || !frontend->audio ||
        frontend->capture || frontend->resource_inventory || frontend->source_restoring ||
        !event || (event->kind != QA_BUILTIN_SOUND && event->kind != QA_BUILTIN_MUZZLE) ||
        (event->kind == QA_BUILTIN_SOUND && (event->flags & 1u)) || !event->actor.registry || !event->provider) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound requires its actual event delivery boundary");
        return QA_AUDIO_NO_ACTOR;
    }
    if (!retained_event_source(frontend,event->provider,event->family,error)) return QA_AUDIO_NO_ACTOR;
    return retained_actor(frontend, event->actor, error);
}
uint64_t frontend_audio_q2_protocol_actor(qa_frontend *frontend,
    const qa_application_protocol_event *message, qa_actor_id actor, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->stepping || !frontend->audio ||
        frontend->capture || frontend->resource_inventory || frontend->source_restoring ||
        !message || !actor.registry || !message->provider) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound requires its actual delivery boundary");
        return QA_AUDIO_NO_ACTOR;
    }
    bool retained=false, referenced=false;
    for (size_t i=0;i<qa_application_protocol_event_count(frontend->application);++i) {
        qa_application_protocol_event queued;
        if (!qa_application_protocol_event_at(frontend->application,i,&queued)) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol queue changed during sound delivery");
            return QA_AUDIO_NO_ACTOR;
        }
        if (queued.provider==message->provider && queued.time_ns==message->time_ns &&
            queued.payload.data==message->payload.data && queued.payload.size==message->payload.size &&
            queued.references==message->references && queued.reference_count==message->reference_count) {
            retained=true; break;
        }
    }
    for (size_t i=0;retained && i<message->reference_count;++i)
        if (qa_actor_id_equal(message->references[i].actor,actor)) { referenced=true; break; }
    if (!retained || !referenced || !retained_event_source(frontend,message->provider,QA_GAME_Q2,error)) {
        if (!retained || !referenced)
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound lost its captured actor reference");
        return QA_AUDIO_NO_ACTOR;
    }
    return retained_actor(frontend,actor,error);
}
