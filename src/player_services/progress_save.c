#include "progress_internal.h"
#include "save_io.h"
#include "qa/player_progress_save.h"
#include "qa/save.h"

static const uint8_t progress_magic[8] = {'Q','A','P','P',1,0,0,0};
static bool root_snapshot(qa_player_progress *store, char **path, qa_fs_identity *identity,
                           qa_error *error) {
    qa_fs_entry_kind kind;
    if (!qa_fs_root_join(store->root, "", path, error) ||
        !qa_fs_root_status(store->root, "", &kind, identity, error)) return false;
    if (kind != QA_FS_DIRECTORY) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Player progress root is not a directory");
        return false;
    }
    return true;
}
static bool identity_fields(qa_source_save_io *io, qa_fs_identity *identity) {
    for (size_t i = 0; i < QA_FS_IDENTITY_WORDS; ++i)
        if (!qa_source_save_u64(io, &identity->words[i])) return false;
    return true;
}
static bool data_fields(qa_source_save_io *io, progress_data *data) {
    qa_buffer strings = {0};
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool ok = reading || qa_save_strings_encode(data->strings, &strings, io->error);
    if (ok) ok = ps_blob(io, &strings);
    if (ok && reading)
        ok = qa_save_strings_decode((qa_bytes){strings.data, strings.size}, &data->strings, io->error);
    qa_buffer_free(&strings);
    if (!ok || !ps_count(io, &data->capacity, 1, sizeof(*data->rows)) ||
        !qa_source_save_count(io, &data->count, data->capacity)) return false;
    if (reading && data->capacity) {
        data->rows = calloc(data->capacity, sizeof(*data->rows));
        if (!data->rows) return ps_fail(io, QA_ERROR_MEMORY, "Allocating private progress rows");
    }
    for (size_t i = 0; i < data->capacity; ++i) {
        bool occupied = i < data->count;
        if (!qa_source_save_bool(io, &occupied) || occupied != (i < data->count))
            return ps_fail(io, QA_ERROR_FORMAT, "Invalid progress physical row partition");
        if (!occupied) continue;
        progress_row row = reading ? (progress_row){0} : data->rows[i];
        uint32_t kind = row.kind, source = row.source;
        if (!qa_source_save_u32(io, &kind) || !qa_source_save_u32(io, &source) ||
            kind > QA_PROGRESS_MATCH_COMPLETED || source > QA_GAME_Q3 ||
            !qa_source_save_u32(io, &row.participant) ||
            !qa_source_save_u32(io, &row.event) || !qa_source_save_u32(io, &row.subject) ||
            !qa_source_save_f64(io, &row.score))
            return ps_fail(io, QA_ERROR_FORMAT, "Invalid private progress row");
        row.kind = (qa_progress_kind)kind; row.source = (qa_game_family)source;
        if (reading) data->rows[i] = row;
    }
    if (!ps_count(io, &data->index_capacity, 8, sizeof(*data->index))) return false;
    if (reading && data->index_capacity) {
        data->index = calloc(data->index_capacity, sizeof(*data->index));
        if (!data->index) return ps_fail(io, QA_ERROR_MEMORY, "Allocating private progress index");
    }
    for (size_t i = 0; i < data->index_capacity; ++i) {
        uint64_t index = data->index[i];
        if (!qa_source_save_u64(io, &index) || index > data->count)
            return ps_fail(io, QA_ERROR_FORMAT, "Invalid private progress index entry");
        if (reading) data->index[i] = (size_t)index;
    }
    return progress_checkpoint_data_ready(data, io->error);
}
bool qa_player_progress_checkpoint(const qa_player_progress *source, qa_buffer *out,
                                    qa_error *error) {
    if (!source || !out || out->data || out->size) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Progress checkpoint needs its owner and empty output");
        return false;
    }
    qa_player_progress copy = *source;
    char *current = NULL;
    qa_fs_identity identity = {0};
    bool ok = progress_checkpoint_data_ready(&copy.data, error) &&
              root_snapshot(&copy, &current, &identity, error);
    if (ok && copy.saved_root &&
        (strcmp(current, copy.admitted_root) ||
         !qa_fs_identity_equal(&identity, &copy.admitted_root_identity))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Admitted player progress root changed");
        ok = false;
    }
    char *root = copy.saved_root ? copy.saved_root : current;
    if (copy.saved_root) identity = copy.saved_root_identity;
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, NULL, error) && ps_magic(&io, progress_magic) &&
                 ps_text(&io, &root) && identity_fields(&io, &identity) &&
                 ps_text(&io, &copy.relative) && qa_source_save_u64(&io, &copy.nonce) &&
                 qa_source_save_bool(&io, &copy.reload_required) && data_fields(&io, &copy.data) &&
                 qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    free(current);
    return ok;
}
bool qa_player_progress_restore(qa_player_progress *store,
                                const qa_player_progress_checkpoint_refs *refs,
                                qa_bytes bytes, qa_error *error) {
    if (!store || !store->restore_pending || store->saved_root || store->data.count ||
        store->data.capacity || store->data.index_capacity ||
        qa_strings_count(store->data.strings)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Progress import requires its stable restored empty owner");
        return false;
    }
    qa_player_progress scratch = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && ps_magic(&io, progress_magic) &&
              ps_text(&io, &scratch.saved_root) && scratch.saved_root && *scratch.saved_root &&
              identity_fields(&io, &scratch.saved_root_identity) && ps_text(&io, &scratch.relative) &&
              scratch.relative && !strcmp(scratch.relative, store->relative) &&
              qa_source_save_u64(&io, &scratch.nonce) &&
              qa_source_save_bool(&io, &scratch.reload_required) && data_fields(&io, &scratch.data) &&
              qa_source_save_finish(&io, NULL);
    if (ok) ok = root_snapshot(store, &scratch.admitted_root, &scratch.admitted_root_identity, error);
    if (ok) {
        if (refs && refs->root_ready)
            ok = refs->root_ready(refs->context, store->root, scratch.saved_root,
                                  &scratch.saved_root_identity, error);
        else ok = scratch.saved_root_identity.words[0] == scratch.admitted_root_identity.words[0] &&
                  scratch.saved_root_identity.words[1] == scratch.admitted_root_identity.words[1];
    }
    if (ok) {
        progress_data_free(&store->data);
        store->data = scratch.data; scratch.data = (progress_data){0};
        store->nonce = scratch.nonce; store->reload_required = scratch.reload_required;
        store->saved_root = scratch.saved_root; scratch.saved_root = NULL;
        store->admitted_root = scratch.admitted_root; scratch.admitted_root = NULL;
        store->saved_root_identity = scratch.saved_root_identity;
        store->admitted_root_identity = scratch.admitted_root_identity;
    } else if (error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Invalid player progress checkpoint admission");
    qa_source_save_dispose(&io);
    progress_data_free(&scratch.data);
    free(scratch.saved_root); free(scratch.admitted_root); free(scratch.relative);
    return ok;
}
void qa_player_progress_publish_restored(qa_player_progress *store) {
    if (!store) return;
    free(store->saved_root); store->saved_root = NULL;
    free(store->admitted_root); store->admitted_root = NULL;
    store->restore_pending = false;
}
