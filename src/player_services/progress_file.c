#include "progress_internal.h"
#include "qa/json.h"
#include "qa/json_writer.h"

static bool string_field(const qa_json_document *document, qa_json_id object, const char *key,
                         qa_buffer *out, qa_error *error) {
    return qa_json_string(document, qa_json_get(document, object, key), out, error);
}
static bool decode_event(const qa_json_document *document, qa_json_id object, progress_data *data,
                         qa_error *error) {
    qa_progress_event event = {0};
    qa_buffer participant = {0}, identity = {0}, subject = {0};
    qa_json_id kind = qa_json_get(document, object, "kind");
    qa_json_id source = qa_json_get(document, object, "source");
    if (qa_json_string_equal(document, source, "q1"))
        event.source = QA_GAME_Q1;
    else if (qa_json_string_equal(document, source, "q2"))
        event.source = QA_GAME_Q2;
    else if (qa_json_string_equal(document, source, "q3"))
        event.source = QA_GAME_Q3;
    else
        goto invalid;
    if (qa_json_string_equal(document, kind, "achievement"))
        event.kind = QA_PROGRESS_ACHIEVEMENT;
    else if (qa_json_string_equal(document, kind, "level-completed"))
        event.kind = QA_PROGRESS_LEVEL_COMPLETED;
    else if (qa_json_string_equal(document, kind, "match-completed"))
        event.kind = QA_PROGRESS_MATCH_COMPLETED;
    else
        goto invalid;
    bool ok =
        string_field(document, object, "participant", &participant, error) &&
        string_field(document, object, "event", &identity, error) &&
        string_field(document, object, event.kind == QA_PROGRESS_ACHIEVEMENT ? "award" : "map",
                     &subject, error);
    event.participant = (qa_bytes){participant.data, participant.size};
    event.event = (qa_bytes){identity.data, identity.size};
    qa_bytes value = {subject.data, subject.size};
    if (event.kind == QA_PROGRESS_ACHIEVEMENT)
        event.value.award = value;
    else if (event.kind == QA_PROGRESS_LEVEL_COMPLETED)
        event.value.map = value;
    else {
        event.value.match.map = value;
        if (ok)
            ok = qa_json_number(document, qa_json_get(document, object, "score"),
                                &event.value.match.score, error);
    }
    progress_row row;
    bool duplicate = false;
    if (ok)
        ok = progress_prepare(data, &event, &row, &duplicate, error);
    if (ok && duplicate) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Duplicate player progress event");
        ok = false;
    }
    if (ok)
        progress_append(data, &row);
    qa_buffer_free(&participant);
    qa_buffer_free(&identity);
    qa_buffer_free(&subject);
    return ok;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid player progress event identity");
    return false;
}
bool progress_decode(qa_bytes bytes, progress_data *out, qa_error *error) {
    progress_data data = {0};
    qa_json_document *document = NULL;
    if (!qa_json_parse(bytes, &document, error))
        return false;
    qa_json_id root = qa_json_root(document);
    qa_json_id events = qa_json_get(document, root, "events");
    double version;
    bool ok = qa_json_type(document, root) == QA_JSON_OBJECT &&
              qa_json_type(document, events) == QA_JSON_ARRAY &&
              qa_json_number(document, qa_json_get(document, root, "version"), &version, error) &&
              version == 1;
    if (!ok)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid player progress file");
    if (ok)
        ok = qa_strings_create(&data.strings, error);
    for (size_t i = 0; ok && i < qa_json_size(document, events); ++i)
        ok = decode_event(document, qa_json_at(document, events, i), &data, error);
    qa_json_destroy(document);
    if (!ok) {
        progress_data_free(&data);
        return false;
    }
    *out = data;
    return true;
}
static void encode_event(qa_json_writer *writer, const progress_data *data,
                         const progress_row *row) {
    static const char *const kinds[] = {"achievement", "level-completed", "match-completed"};
    static const char *const sources[] = {"q1", "q2", "q3"};
    qa_json_writer_object(writer);
    qa_json_writer_key(writer, "kind");
    qa_json_writer_string(writer, kinds[row->kind]);
    qa_json_writer_key(writer, "source");
    qa_json_writer_string(writer, sources[row->source]);
    qa_json_writer_key(writer, "participant");
    qa_json_writer_bytes(writer, qa_strings_text(data->strings, row->participant));
    qa_json_writer_key(writer, "event");
    qa_json_writer_bytes(writer, qa_strings_text(data->strings, row->event));
    qa_json_writer_key(writer, row->kind == QA_PROGRESS_ACHIEVEMENT ? "award" : "map");
    qa_json_writer_bytes(writer, qa_strings_text(data->strings, row->subject));
    if (row->kind == QA_PROGRESS_MATCH_COMPLETED) {
        qa_json_writer_key(writer, "score");
        qa_json_writer_number(writer, row->score);
    }
    qa_json_writer_end(writer);
}
bool progress_encode(const progress_data *data, const progress_row *extra, qa_buffer *out,
                     qa_error *error) {
    qa_json_writer writer = {0};
    qa_json_writer_object(&writer);
    qa_json_writer_key(&writer, "version");
    qa_json_writer_number(&writer, 1);
    qa_json_writer_key(&writer, "events");
    qa_json_writer_array(&writer);
    for (size_t i = 0; i < data->count; ++i)
        encode_event(&writer, data, data->rows + i);
    if (extra)
        encode_event(&writer, data, extra);
    qa_json_writer_end(&writer);
    qa_json_writer_end(&writer);
    bool ok = qa_json_writer_finish(&writer, out, error);
    qa_json_writer_destroy(&writer);
    return ok;
}
bool qa_player_progress_reload(qa_player_progress *store, qa_error *error) {
    if (!store) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing player progress store");
        return false;
    }
    qa_fs_entry_kind kind;
    if (!qa_fs_root_status(store->root, store->relative, &kind, NULL, error))
        return false;
    progress_data data = {0};
    if (kind == QA_FS_MISSING) {
        if (!qa_strings_create(&data.strings, error))
            return false;
    } else {
        qa_fs_file *file = NULL;
        qa_fs_identity identity;
        qa_buffer bytes = {0};
        bool ok = qa_fs_root_file_open(store->root, store->relative, &file, &identity, error) &&
                  qa_fs_file_read_snapshot(file, &identity, &bytes, error) &&
                  progress_decode((qa_bytes){bytes.data, bytes.size}, &data, error);
        qa_fs_file_close(file);
        qa_buffer_free(&bytes);
        if (!ok)
            return false;
    }
    progress_data_free(&store->data);
    store->data = data;
    store->reload_required = false;
    return true;
}
bool qa_player_progress_open(qa_fs_root *root, const char *relative, qa_player_progress **out,
                             qa_error *error) {
    if (!root || !relative || !*relative || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid player progress file location");
        return false;
    }
    qa_player_progress *store = calloc(1, sizeof(*store));
    if (!store) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating player progress store");
        return false;
    }
    size_t size = strlen(relative) + 1;
    store->relative = malloc(size);
    if (!store->relative) {
        qa_error_set(error, QA_ERROR_MEMORY, size, "Retaining player progress file name");
        free(store);
        return false;
    }
    memcpy(store->relative, relative, size);
    store->root = root;
    qa_fs_root_retain(root);
    if (!qa_player_progress_reload(store, error)) {
        qa_player_progress_close(store);
        return false;
    }
    *out = store;
    return true;
}
void qa_player_progress_close(qa_player_progress *store) {
    if (!store)
        return;
    progress_data_free(&store->data);
    qa_fs_root_close(store->root);
    free(store->relative);
    free(store);
}
