#include "unified_output_capture_save.h"
#include "unified_output_capture_private.h"
#include "unified_components_save.h"
#include "unified_save_internal.h"

#include <stdlib.h>
#include <string.h>

static bool field_equal(const qa_json_document *json, qa_json_id value,
    const qa_unified_document *actual)
{
    if (!actual) return value == QA_JSON_NONE;
    qa_bytes a = qa_json_source(json, value);
    qa_bytes b = qa_json_source(qa_unified_document_json(actual), qa_unified_document_root(actual));
    return value != QA_JSON_NONE && a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}
static bool component_output(const application_unified_output_capture *capture)
{
    if (!capture->components) return true;
    const qa_json_document *json = qa_unified_document_json(capture->output.frame);
    qa_json_id root = qa_unified_document_root(capture->output.frame);
    if (!field_equal(json, qa_json_get(json, root, "components"),
            application_unified_components_frame(capture->components))) return false;
    const qa_unified_document *camera = application_unified_components_camera(capture->components);
    if (camera && !field_equal(json, qa_json_get(json, root, "nativeCamera"), camera)) return false;
    const qa_unified_document *control = application_unified_components_control(capture->components);
    size_t found = 0;
    for (size_t i = 0; i < capture->output.control_count; ++i) {
        const qa_unified_document *item = capture->output.controls[i];
        const qa_json_document *j = qa_unified_document_json(item);
        qa_json_id value = qa_json_get(j, qa_unified_document_root(item), "value");
        if (!qa_json_string_equal(j, qa_json_get(j, value, "kind"), "components")) continue;
        if (!control || !field_equal(j, qa_unified_document_root(item), control)) return false;
        ++found;
    }
    return found == (control ? 1u : 0u);
}

static bool fields(qa_source_save_io *io, application_unified_output_capture *capture,
    const application_unified_source *source, const qa_unified_session_player *player,
    application_unified_component_publisher *publisher)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    bool has_components = capture->components != NULL;
    if (!application_unified_save_magic(io, "QUOC") ||
        !application_unified_save_source(io, capture->application, source, &capture->source, false) ||
        !application_unified_save_client(io, capture->recipient) ||
        !application_unified_save_player(io, player) ||
        !qa_source_save_u64(io, &capture->events.through) ||
        !application_unified_save_output(io, &capture->output) || !capture->output.frame ||
        !qa_source_save_bool(io, &has_components)) return false;
    if (has_components != (publisher != NULL) && !writing) return false;
    if (has_components) {
        qa_buffer saved = {0};
        bool okay = !writing || application_unified_components_capture_checkpoint(capture->components,
            &saved, io->error);
        if (okay) okay = application_unified_save_blob(io, &saved) && saved.size;
        if (okay && !writing) okay = application_unified_components_capture_restore(
            (qa_bytes){saved.data, saved.size}, publisher, source, player, &capture->components, io->error);
        qa_buffer_free(&saved);
        if (!okay) return false;
    }
    return component_output(capture);
}
bool application_unified_output_capture_checkpoint(const application_unified_output_capture *capture,
    qa_buffer *out, qa_error *e)
{
    application_unified_source source;
    qa_unified_session_player player;
    if (!capture || !out || out->data || out->size || !capture->sealed ||
        !application_unified_output_capture_current(capture) ||
        !(application_unified_player_current(capture->application, capture->recipient, &capture->player) ||
            application_unified_player_checkpoint_current(capture->application, capture->recipient, &capture->player)) ||
        !application_unified_save_source_read(capture->application, &source, e) ||
        !application_unified_save_player_read(capture->application, capture->recipient,
            capture->player.seat, &player, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Output checkpoint requires its sealed actual recipient token");
    application_unified_output_capture copy = *capture;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, source.session, e) && fields(&io, &copy, &source, &player, NULL) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay;
}
bool application_unified_output_capture_restore(qa_bytes bytes, qa_application *app,
    const application_unified_source *source, qa_net_client_id recipient,
    const qa_unified_session_player *player, application_unified_component_publisher *publisher,
    application_unified_output_capture **out, qa_error *e)
{
    application_unified_source current;
    qa_unified_session_player actual;
    if (!out || *out || !source || !player ||
        !(application_unified_source_current(app, source) || application_unified_source_checkpoint_current(app, source)) ||
        !(application_unified_player_current(app, recipient, player) ||
            application_unified_player_checkpoint_current(app, recipient, player)) ||
        !application_unified_save_source_read(app, &current, e) ||
        !application_unified_save_player_read(app, recipient, player->seat, &actual, e) ||
        !qa_actor_id_equal(actual.actor, player->actor))
        return application_fail(e, QA_ERROR_ARGUMENT, "Output import requires its genuine imported Source player");
    application_unified_output_capture *capture = calloc(1, sizeof(*capture));
    if (!capture) return application_fail(e, QA_ERROR_MEMORY, "Restoring sealed Unified output token");
    capture->application = app; capture->recipient = recipient; capture->source = *source;
    capture->player = actual;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, current.session, bytes, e) &&
        fields(&io, capture, &current, &actual, publisher) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) { application_unified_output_capture_dispose(capture); return false; }
    capture->sealed = true;
    *out = capture;
    return true;
}
