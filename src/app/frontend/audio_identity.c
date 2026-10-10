#include "internal.h"
#include "round.h"
#include "native_q3_client_internal.h"
#include "qa/ruleset.h"
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
    if ((unsigned)clock.frame.kind > QA_RULESET_Q3) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound has no gameplay Source clock");
        return false;
    }
    bool matches = qa_ruleset_descriptors[clock.frame.kind].family == family;
    return matches || frontend_fail(error, QA_ERROR_ARGUMENT, "Retained sound changed its Source family");
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
    qa_application_protocol_event queued;
    const qa_application_protocol_event *retained=message;
    if (message->event_id) {
        if (!qa_application_protocol_event_at(frontend->application,message->event_id,0,&queued)) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound lost its retained event ID");
            return QA_AUDIO_NO_ACTOR;
        }
        retained=&queued;
    }
    bool referenced=false;
    for (size_t i=0;i<retained->reference_count;++i)
        if (qa_actor_id_equal(retained->references[i].actor,actor)) { referenced=true; break; }
    if (!referenced || !retained_event_source(frontend,retained->provider,QA_GAME_Q2,error)) {
        if (!referenced)
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound lost its captured actor reference");
        return QA_AUDIO_NO_ACTOR;
    }
    return retained_actor(frontend,actor,error);
}
