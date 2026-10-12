#include "internal.h"
#include "round.h"
#include "native_q3_client_internal.h"
#include "qa/ruleset.h"
static uint64_t append_actor(qa_frontend *frontend, qa_actor_id actor, bool retired,
    qa_error *error);
bool frontend_audio_identities_prepare(qa_frontend *frontend, qa_error *error)
{
    if (frontend->audio_actors.registry) return true;
    const qa_actor_registry *actors = qa_session_actors(qa_application_session(frontend->application));
    if (!qa_actor_index_create(&frontend->audio_actors, actors, NULL, error)) return false;
    size_t capacity = frontend->audio_actors.capacity;
    if (!qa_pool_prepare(&frontend->audio_ids, &frontend->audio_identity_storage, capacity,
            sizeof(frontend_audio_identity), _Alignof(frontend_audio_identity), error) ||
        !qa_id_index_reserve(&frontend->audio_actors.retained, capacity, error) ||
        !qa_id_index_reserve(&frontend->audio_reverse, capacity, error)) {
        frontend_audio_identities_destroy(frontend); return false;
    }
    return true;
}
void frontend_audio_identities_destroy(qa_frontend *frontend)
{
    qa_actor_index_destroy(&frontend->audio_actors);
    qa_id_index_destroy(&frontend->audio_reverse);
    qa_arena_destroy(&frontend->audio_identity_storage);
    frontend->audio_ids = (qa_pool){0};
    frontend->audio_first = frontend->audio_last = NULL;
}
void frontend_audio_retire_round_aliases(qa_frontend *frontend)
{
    /* The caller has drained events and successfully reset the actual mixers. */
    qa_actor_index_clear(&frontend->audio_actors);
    qa_id_index_clear(&frontend->audio_reverse);
    qa_pool_reset(&frontend->audio_ids);
    frontend->audio_first = frontend->audio_last = NULL;
}
void frontend_audio_retire_dead_aliases(qa_frontend *frontend)
{
    const qa_actor_registry *actors = qa_world_actors(qa_application_world(frontend->application));
    for (frontend_audio_identity *row = frontend->audio_first; row; row = row->next)
        if (!qa_actors_get(actors, row->actor)) {
            row->retired = true;
            qa_actor_index_remove(&frontend->audio_actors, row->actor);
        }
}
uint64_t frontend_audio_actor(qa_frontend *frontend, qa_actor_id actor, qa_error *error)
{
    if (!actor.registry) return QA_AUDIO_NO_ACTOR;
    size_t slot;
    if (qa_actor_index_find(&frontend->audio_actors, actor, &slot))
        return ((const frontend_audio_identity *)qa_pool_at(&frontend->audio_ids, slot))->id;
    if (!qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), actor)) {
        if (qa_actor_index_find_retained(&frontend->audio_actors, actor, &slot))
            return ((const frontend_audio_identity *)qa_pool_at(&frontend->audio_ids, slot))->id;
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
    if (frontend->audio_ids.active == frontend->audio_ids.capacity &&
        !qa_pool_extend(&frontend->audio_ids, &frontend->audio_identity_storage,
            frontend->audio_ids.capacity, error)) return QA_AUDIO_NO_ACTOR;
    size_t previous;
    bool remembered = qa_actor_index_find_retained(&frontend->audio_actors, actor, &previous);
    if (!qa_id_index_reserve(&frontend->audio_reverse, 1, error) ||
        (!remembered && !qa_id_index_reserve(&frontend->audio_actors.retained, 1, error)))
        return QA_AUDIO_NO_ACTOR;
    size_t slot;
    frontend_audio_identity *row = qa_pool_take(&frontend->audio_ids, &slot);
    if (!row) {
        frontend_fail(error, QA_ERROR_MEMORY, "allocating audio actor identities"); return QA_AUDIO_NO_ACTOR;
    }
    if (!remembered && !qa_actor_index_remember(&frontend->audio_actors, actor, slot, error)) {
        qa_pool_release(&frontend->audio_ids, slot); return QA_AUDIO_NO_ACTOR;
    }
    uint64_t id = ++frontend->next_audio_id;
    *row = (frontend_audio_identity){.actor = actor, .id = id, .retired = retired};
    qa_id_index_insert(&frontend->audio_reverse, QA_STRING_NONE, id, slot);
    if (!retired) qa_actor_index_put(&frontend->audio_actors, actor, slot);
    if (frontend->audio_last) frontend->audio_last->next = row;
    else frontend->audio_first = row;
    frontend->audio_last = row;
    return id;
}
bool frontend_audio_actor_for_id(const qa_frontend *frontend, uint64_t id, qa_actor_id *actor)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&frontend->audio_reverse, QA_STRING_NONE, id);
    size_t slot;
    if (!qa_id_index_next(&frontend->audio_reverse, &cursor, &slot)) return false;
    const frontend_audio_identity *row = qa_pool_at(&frontend->audio_ids, slot);
    if (row->retired) return false;
    *actor = row->actor; return true;
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
    qa_application_event_view output;
    const qa_application_protocol_event *retained=message;
    if (message->event_id) {
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = message->event_id, .projection = 0}, &output) || !output.protocol) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 protocol sound lost its retained event ID");
            return QA_AUDIO_NO_ACTOR;
        }
        retained=output.protocol;
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
