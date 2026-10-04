#include "audio_identity_save.h"
#include "qa/source_save.h"
#include "qa/binary.h"

bool frontend_audio_id_read(const qa_frontend *f, uint64_t id, qa_actor_id *actor, bool *retired)
{
    if (!f || !id || id == QA_AUDIO_NO_ACTOR || id > f->audio_id_count) return false;
    const frontend_audio_identity *entry = &f->audio_ids[id - 1];
    if (entry->id != id) return false;
    if (actor) *actor = entry->actor;
    if (retired) *retired = entry->retired;
    return true;
}
static bool fields(qa_source_save_io *io, qa_frontend *state)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','F','A','I'}; size_t count = state->audio_id_count, capacity = state->audio_id_capacity;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic,"QFAI",4) ||
        !qa_source_save_u64(io, &state->next_audio_id) || state->next_audio_id >= UINT64_MAX ||
        !qa_source_save_count(io, &count, reading ? io->input.size / 22 : SIZE_MAX) ||
        !qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(frontend_audio_identity)) || count > capacity ||
        (count == 0 && (capacity || state->next_audio_id))) return false;
    size_t grown = count ? 128 : 0;
    while (grown < count) {
        if (grown > SIZE_MAX / 2) return false;
        grown *= 2;
    }
    if (capacity != grown || state->next_audio_id != count) return false;
    if (reading && capacity) {
        state->audio_ids = calloc(capacity, sizeof(*state->audio_ids));
        if (!state->audio_ids) return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring the genuine audio actor table");
    }
    if (count && !state->audio_ids) return false;
    state->audio_id_count = count; state->audio_id_capacity = capacity;
    uint64_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
        frontend_audio_identity value = reading ? (frontend_audio_identity){0} : state->audio_ids[i];
        if (!qa_source_save_actor(io, &value.actor) || !value.actor.registry ||
            !qa_source_save_u64(io, &value.id) || value.id != (uint64_t)i + 1 || value.id <= previous || value.id > state->next_audio_id ||
            !qa_source_save_bool(io, &value.retired)) return false;
        for (size_t j = 0; j < i; ++j) if (!value.retired && !state->audio_ids[j].retired &&
            qa_actor_id_equal(value.actor, state->audio_ids[j].actor)) return false;
        if (reading) state->audio_ids[i] = value;
        previous = value.id;
    }
    return previous == state->next_audio_id;
}
bool frontend_audio_id_checkpoint(qa_frontend *f, qa_buffer *out, qa_error *error)
{
    if (!f || !f->application || f->stepping || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Audio actor capture requires an idle actual frontend");
    qa_source_save_io io = {0}; qa_frontend state = *f;
    bool ok = qa_source_save_writer(&io,qa_application_session(f->application),error) &&
        fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Invalid retained audio actor identities");
    return ok;
}
bool frontend_audio_id_restore(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || f->stepping || f->audio_ids || f->audio_id_count || f->audio_id_capacity)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Audio actor import requires the actual empty candidate table");
    qa_source_save_io io = {0}; qa_frontend state = {0};
    bool ok = qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) &&
        fields(&io,&state) && state.next_audio_id == f->next_audio_id && qa_source_save_finish(&io,NULL);
    if (ok) {
        f->audio_ids = state.audio_ids; f->audio_id_count = state.audio_id_count;
        f->audio_id_capacity = state.audio_id_capacity;
    } else {
        free(state.audio_ids);
        if (!error || error->code == QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Audio actor table differs from candidate topology/provenance");
    }
    qa_source_save_dispose(&io); return ok;
}
bool frontend_audio_id_encode(void *context, uint64_t id, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !frontend_audio_id_read(context,id,NULL,NULL))
        return frontend_fail(error,QA_ERROR_FORMAT,"Audio holder references no genuine frontend actor identity");
    uint8_t *data = malloc(8);
    if (!data) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining an audio actor identity edge");
    qa_store_u64le(data,id); *out = (qa_buffer){data,8}; return true;
}
bool frontend_audio_id_decode(void *context, qa_bytes bytes, uint64_t *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size != 8)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved audio actor edge has no exact descriptor");
    uint64_t id = qa_load_u64le(bytes.data);
    if (!frontend_audio_id_read(context,id,NULL,NULL))
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved audio holder references no restored actor identity");
    *out = id; return true;
}
