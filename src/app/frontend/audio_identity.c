#include "internal.h"
#include "round.h"
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
    frontend->audio_ids[frontend->audio_id_count++] = (frontend_audio_identity){.actor = actor, .id = id};
    return id;
}
