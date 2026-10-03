#include "guest_q3_weapons_private.h"
#include "guest_q3_weapons_save.h"
#include "qa/source_save.h"

static bool fields(qa_source_save_io *io, application_q3_weapons_saved *state)
{
    uint8_t magic[8] = {'Q','A','G','3','W','P',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','W','P',0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)))
        return application_fail(io->error, QA_ERROR_FORMAT, "Invalid original weapon callback continuation");
    if (!qa_source_save_bool(io, &state->present) || !qa_source_save_count(io, &state->count, 6)) return false;
    for (size_t i = 0; i < 6; ++i) if (!qa_source_save_u64(io, state->bindings + i)) return false;
    if (state->count != (state->present ? 6u : 0u))
        return application_fail(io->error, QA_ERROR_FORMAT, "Original weapon continuation omits its actual hook inventory");
    for (size_t i = 0; i < 6; ++i) {
        if ((i < state->count) != (state->bindings[i] != 0))
            return application_fail(io->error, QA_ERROR_FORMAT, "Original weapon continuation retains an undeclared identity");
        for (size_t j = 0; j < i; ++j) if (state->bindings[i] && state->bindings[i] == state->bindings[j])
            return application_fail(io->error, QA_ERROR_FORMAT, "Original weapon continuation duplicates a callback identity");
    }
    return true;
}
bool application_q3_weapons_checkpoint(const application_q3_weapons *w, qa_buffer *out, qa_error *error)
{
    qa_qvm_saved_function descriptors[6] = {0};
    application_q3_weapons_saved saved = {.present = w != NULL, .count = application_q3_weapons_descriptor_count(w)};
    if (!out || !application_q3_weapons_descriptors(w, descriptors, saved.count, error)) return false;
    for (size_t i = 0; i < saved.count; ++i) saved.bindings[i] = descriptors[i].binding;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &saved) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_weapons_prepare_restore(const application_q3_weapons *w, qa_bytes bytes,
    application_q3_weapons_saved *out, qa_error *error)
{
    qa_qvm_saved_function descriptors[6] = {0};
    size_t count = application_q3_weapons_descriptor_count(w);
    if (!out || !application_q3_weapons_descriptors(w, descriptors, count, error) ||
        (w && (!w->role->engine->restore_pending || w->role->initialized)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon restoration requires an isolated source constructor");
    application_q3_weapons_saved saved = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &saved) && qa_source_save_finish(&io, NULL);
    if (ok && (saved.present != (w != NULL) || saved.count != count))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved original weapon inventory differs from its admitted artifact");
    qa_source_save_dispose(&io);
    if (ok) *out = saved;
    return ok;
}
