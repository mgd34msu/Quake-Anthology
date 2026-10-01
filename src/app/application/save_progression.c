#include "internal.h"
#include "save_progression.h"
#include "rankings.h"
#include "qa/source_save.h"

typedef struct progression_record {
    bool rankings_present, progress_present, configured;
    qa_bytes rankings, progress, source;
} progression_record;
static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
}
static bool blob(qa_source_save_io *io, qa_bytes *value) {
    size_t size = value->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ
                                            ? io->input.size : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)value->data, size);
    if (io->offset > io->input.size || size > io->input.size - io->offset)
        return fail(io->error, QA_ERROR_FORMAT, "Truncated application progression record");
    *value = (qa_bytes){size ? io->input.data + io->offset : NULL, size};
    io->offset += size;
    return true;
}
static bool fields(qa_source_save_io *io, progression_record *record) {
    uint8_t magic[4] = {'Q','A','P','R'};
    uint32_t version = 2;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAPR", 4) &&
           qa_source_save_u32(io, &version) && version == 2 &&
           qa_source_save_bool(io, &record->rankings_present) && record->rankings_present &&
           qa_source_save_bool(io, &record->progress_present) &&
           qa_source_save_bool(io, &record->configured) &&
           blob(io, &record->rankings) && record->rankings.size &&
           blob(io, &record->progress) &&
           record->progress_present == (record->progress.size != 0) &&
           blob(io, &record->source) && record->source.size;
}
static bool read_record(qa_bytes bytes, progression_record *record, qa_error *error) {
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, record) &&
              qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK)
        fail(error, QA_ERROR_FORMAT, "Invalid application progression inventory");
    return ok;
}
static bool backend_ready(bool configured, const qa_application_persistence_ops *ops,
                           qa_error *error) {
    if (!configured) return true;
    const qa_rankings_checkpoint_refs *refs = ops ? ops->rankings : NULL;
    return (ops && ops->rankings_handoff && refs && refs->provider_capture &&
            refs->provider_resolve && refs->continuation_ready) ||
           fail(error, QA_ERROR_UNSUPPORTED, "Configured ranking backend lacks genuine continuation and handoff bindings");
}
static bool owner_ready(qa_application *application, const qa_application_persistence_ops *ops,
                         qa_error *error) {
    return (application && (application->operation == APPLICATION_PERSISTING ||
                            application->operation == APPLICATION_IDLE) &&
            application->rankings && qa_rankings_close_ready(application->rankings) &&
            application_rankings_idle(application))
               ? backend_ready(qa_rankings_provider_configured(application->rankings), ops, error)
               : fail(error, QA_ERROR_ARGUMENT, "Application progression needs its actual leased idle owners");
}
bool application_save_progression_prepare(const qa_application_options *options,
    const qa_application_persistence_ops *ops, qa_bytes bytes,
    qa_application_options *construction, qa_error *error) {
    if (!options || !construction) return fail(error, QA_ERROR_ARGUMENT, "Progression admission needs actual restored options");
    progression_record record = {0};
    if (!(read_record(bytes, &record, error) &&
           ((record.configured == (options->ranking_provider != NULL) &&
             (!record.progress_present || options->player_profile_root != NULL)) ||
            fail(error, QA_ERROR_FORMAT, "Saved progression differs from actual installed backend/profile presence")) &&
           backend_ready(record.configured, ops, error) &&
           application_rankings_prepare(options, ops, record.source, error))) return false;
    *construction = *options;
    if (!record.progress_present) construction->player_profile_root = NULL;
    return true;
}
bool application_save_progression_capture(qa_application *application,
    const qa_application_persistence_ops *ops, qa_buffer *out, qa_error *error) {
    if (!out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Application progression needs an empty output");
    if (!owner_ready(application, ops, error)) return false;
    qa_buffer rankings = {0}, progress = {0}, source = {0};
    bool ok = qa_rankings_checkpoint(application->rankings, ops ? ops->rankings : NULL,
                                     &rankings, error);
    if (ok && application->progress)
        ok = qa_player_progress_checkpoint(application->progress, &progress, error);
    if (ok) ok = application_rankings_capture(application, ops, &source, error);
    progression_record record = {.rankings_present = true,
        .progress_present = application->progress != NULL,
        .configured = qa_rankings_provider_configured(application->rankings),
        .rankings = {rankings.data, rankings.size}, .progress = {progress.data, progress.size},
        .source = {source.data, source.size}};
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &record) &&
                 qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_buffer_free(&rankings); qa_buffer_free(&progress); qa_buffer_free(&source);
    if (!ok && error && error->code == QA_OK)
        fail(error, QA_ERROR_FORMAT, "Unqualified application progression record");
    return ok;
}
bool application_save_progression_restore(qa_application *candidate,
    const qa_application_persistence_ops *ops, qa_bytes bytes, qa_error *error) {
    progression_record record = {0};
    if (!read_record(bytes, &record, error) || !owner_ready(candidate, ops, error)) return false;
    if (candidate->operation != APPLICATION_PERSISTING)
        return fail(error, QA_ERROR_ARGUMENT, "Progression import requires the isolated construction lease");
    if (record.configured != qa_rankings_provider_configured(candidate->rankings) ||
        record.progress_present != (candidate->progress != NULL))
        return fail(error, QA_ERROR_FORMAT, "Progression import differs from its actual constructed owners");
    return qa_rankings_restore(candidate->rankings, ops ? ops->rankings : NULL, record.rankings, error) &&
           (!record.progress_present || qa_player_progress_restore(candidate->progress,
               ops ? ops->progress : NULL, record.progress, error)) &&
           application_rankings_restore(candidate, ops, record.source, error);
}
bool application_save_progression_matches(qa_application *candidate,
    const qa_application_persistence_ops *ops, qa_bytes bytes, qa_error *error) {
    progression_record record = {0};
    if (!read_record(bytes, &record, error)) return false;
    qa_buffer actual = {0};
    bool ok = application_rankings_restore_ready(candidate, error) &&
              application_save_progression_capture(candidate, ops, &actual, error);
    if (ok && (actual.size != bytes.size || memcmp(actual.data, bytes.data, bytes.size)))
        ok = fail(error, QA_ERROR_FORMAT, "Candidate progression changed after private import");
    qa_buffer_free(&actual);
    return ok;
}
bool application_save_progression_handoff(qa_application *active, qa_application *candidate,
    const qa_application_persistence_ops *ops, bool *relinquish_active, qa_error *error) {
    if (!relinquish_active || !active || active == candidate || !active->rankings ||
        !qa_rankings_close_ready(active->rankings) || !owner_ready(candidate, ops, error))
        return fail(error, QA_ERROR_ARGUMENT, "Ranking handoff requires distinct idle source and candidate owners");
    return qa_rankings_handoff(active->rankings, candidate->rankings,
        ops ? ops->rankings_handoff : NULL, ops ? ops->context : NULL, relinquish_active, error);
}
void application_save_progression_publish(qa_application *active, qa_application *candidate,
                                          bool relinquish_active) {
    if (relinquish_active) {
        application_rankings_relinquish(active);
        qa_rankings_relinquish_continuation(active->rankings);
    }
    qa_rankings_publish_restored(candidate->rankings);
    qa_player_progress_publish_restored(candidate->progress);
    application_rankings_publish_restored(candidate);
}
