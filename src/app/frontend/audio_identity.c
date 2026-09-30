#include "internal.h"
uint64_t frontend_audio_actor(qa_frontend *frontend, qa_actor_id actor, qa_error *error)
{
    if (!actor.registry) return QA_AUDIO_NO_ACTOR;
    for (size_t i = 0; i < frontend->audio_id_count; ++i)
        if (qa_actor_id_equal(frontend->audio_ids[i].actor, actor)) return frontend->audio_ids[i].id;
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
    frontend->audio_ids[frontend->audio_id_count++] = (frontend_audio_identity){actor, id};
    return id;
}
