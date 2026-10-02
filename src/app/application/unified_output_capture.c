#include "unified_output_capture.h"
#include "unified_output_json.h"
#include "unified_presentations.h"
#include "unified_prediction.h"
#include "unified_events.h"
#include "internal.h"

#include <stdlib.h>

struct application_unified_output_capture {
    qa_application *application;
    application_unified_source source;
    qa_net_client_id recipient;
    qa_unified_session_player player;
    application_unified_output_external external;
    bool has_external;
    uint64_t actors_revision;
    application_unified_presentations visuals;
    application_unified_events events;
    application_unified_component_capture *components;
    bool sealed;
    qa_unified_document *prediction, *player_values, *presentation, *frame_events;
    application_unified_output output;
};

bool application_unified_output_capture_current(const application_unified_output_capture *v)
{
    if (!v) return false;
    if (v->sealed) return !v->components || application_unified_components_current(v->components);
    if (!application_unified_source_current(v->application, &v->source) ||
        !application_unified_player_current(v->application, v->recipient, &v->player) ||
        qa_actors_revision(qa_session_actors(v->source.session)) != v->actors_revision ||
        !application_unified_presentations_current(v->application, &v->visuals) ||
        !application_unified_events_current(&v->events) ||
        (v->components && !application_unified_components_current(v->components))) return false;
    if (v->has_external && !v->external.current(v->external.context, v->application,
        &v->source, v->recipient, &v->player)) return false;
    if (v->external.player && (!v->external.player->current ||
        !v->external.player->current(v->external.player->context, v->application,
            &v->source, v->recipient, &v->player))) return false;
    return application_unified_source_current(v->application, &v->source) &&
        application_unified_player_current(v->application, v->recipient, &v->player) &&
        qa_actors_revision(qa_session_actors(v->source.session)) == v->actors_revision &&
        application_unified_presentations_current(v->application, &v->visuals) &&
        application_unified_events_current(&v->events);
}

static bool children_current(void *context, qa_application *app,
    const application_unified_source *source, qa_net_client_id recipient,
    const qa_unified_session_player *player)
{
    const application_unified_output_capture *v = context;
    return v && v->application == app && source->owner == v->source.owner &&
        source->frame_revision == v->source.frame_revision && recipient.owner == v->recipient.owner &&
        recipient.slot == v->recipient.slot && recipient.generation == v->recipient.generation &&
        qa_actor_id_equal(player->actor, v->player.actor) && application_unified_output_capture_current(v);
}

static bool presentation(application_unified_output_capture *v, qa_error *e)
{
    application_unified_json j = {0};
    bool ok = application_unified_json_text(&j, "{\"models\":", e) &&
        application_unified_json_document(&j, v->visuals.models, e) &&
        application_unified_json_text(&j, ",\"characters\":", e) &&
        application_unified_json_document(&j, v->visuals.characters, e) &&
        application_unified_json_text(&j, ",\"worldText\":", e) &&
        application_unified_json_document(&j, v->events.world_text, e) &&
        application_unified_json_text(&j, ",\"player\":", e) &&
        application_unified_json_document(&j, v->player_values, e);
    const qa_unified_document *components = v->components ?
        application_unified_components_frame(v->components) : v->external.components;
    if (ok && components)
        ok = application_unified_json_text(&j, ",\"components\":", e) &&
            application_unified_json_document(&j, components, e);
    if (ok && v->external.native_camera)
        ok = application_unified_json_text(&j, ",\"nativeCamera\":", e) &&
            application_unified_json_document(&j, v->external.native_camera, e);
    if (ok) ok = application_unified_json_text(&j, "}", e) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){j.bytes.data, j.bytes.size}, &v->presentation, e);
    application_unified_json_dispose(&j);
    return ok;
}

bool application_unified_output_acquire(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    int64_t acknowledged, uint64_t after, application_unified_component_publisher *publisher,
    const application_unified_output_external *external,
    application_unified_output_capture **out, qa_error *e)
{
    if (!out || *out || !source || !player || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, recipient, player) ||
        (external && (!external->current || (external->control_count && !external->controls) ||
            (publisher && external->components))))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified capture requires its actual Source children and recipient");
    if (external && !external->current(external->context, app, source, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified capture external Source child has retired");
    application_unified_output_capture *v = calloc(1, sizeof(*v));
    if (!v) return application_fail(e, QA_ERROR_MEMORY, "Retaining completed Unified Source output");
    v->application = app; v->source = *source; v->recipient = recipient; v->player = *player;
    v->actors_revision = qa_actors_revision(qa_session_actors(source->session));
    if (external) { v->external = *external; v->has_external = true; }
    bool ok = application_unified_prediction_build(app, source, recipient, player, acknowledged, &v->prediction, e) &&
        application_unified_player_values(app, source, recipient, player,
            external ? external->player : NULL, &v->player_values, e) &&
        application_unified_presentations_build(app, source, recipient, player, &v->visuals, e) &&
        application_unified_events_read(app, source, recipient, player, epoch, after, &v->events, e);
    if (ok && publisher) ok = application_unified_components_prepare(publisher, source, player,
        epoch, v->player_values, &v->components, e);
    if (ok) ok = presentation(v, e);
    /* The real reliable events CONTROL already owns this simulation stream.
     * FRAME carries no second delivery of those same events. */
    static const uint8_t empty[] = "[]";
    if (ok) ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){empty, sizeof(empty) - 1}, &v->frame_events, e);
    const qa_unified_document **controls = NULL;
    size_t count = v->events.control_count;
    const qa_unified_document *component_control = application_unified_components_control(v->components);
    if (ok && component_control) {
        if (count == SIZE_MAX) ok = application_fail(e, QA_ERROR_MEMORY, "Unified component control extent overflows");
        else ++count;
    }
    if (ok && v->external.control_count > SIZE_MAX - count)
        ok = application_fail(e, QA_ERROR_MEMORY, "Unified Source control extent overflows");
    if (ok) count += v->external.control_count;
    if (ok && count > SIZE_MAX / sizeof(*controls))
        ok = application_fail(e, QA_ERROR_MEMORY, "Unified Source control allocation overflows");
    if (ok && count) {
        controls = calloc(count, sizeof(*controls));
        if (!controls) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining Unified prerequisite order");
    }
    if (ok) {
        for (size_t i = 0; i < v->events.control_count; ++i) controls[i] = v->events.controls[i];
        size_t at = v->events.control_count;
        if (component_control) controls[at++] = component_control;
        for (size_t i = 0; i < v->external.control_count; ++i)
            controls[at + i] = v->external.controls[i];
        application_unified_frame_children children = {.context = v, .current = children_current,
            .prediction = v->prediction, .presentation = v->presentation,
            .simulation_events = v->frame_events, .controls = controls, .control_count = count};
        ok = application_unified_output_build(app, source, recipient, player, epoch, acknowledged, &children, &v->output, e);
    }
    free(controls);
    if (!ok) { application_unified_output_capture_dispose(v); return false; }
    *out = v;
    return true;
}

const application_unified_output *application_unified_output_capture_value(const application_unified_output_capture *v)
{ return v ? &v->output : NULL; }
uint64_t application_unified_output_capture_events_through(const application_unified_output_capture *v)
{ return v ? v->events.through : 0; }
bool application_unified_output_capture_seal(application_unified_output_capture *v, qa_error *e)
{
    if (!application_unified_output_capture_current(v))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified output changed before its immutable queue token was sealed");
    if (v->components && !application_unified_components_seal(v->components, e)) return false;
    v->sealed = true;
    return true;
}
void application_unified_output_capture_commit(application_unified_output_capture *v)
{ if (v && v->sealed) application_unified_components_commit(v->components); }
void application_unified_output_capture_dispose(application_unified_output_capture *v)
{
    if (!v) return;
    application_unified_output_dispose(&v->output);
    qa_unified_document_destroy(v->prediction); qa_unified_document_destroy(v->player_values);
    qa_unified_document_destroy(v->presentation); qa_unified_document_destroy(v->frame_events);
    application_unified_presentations_dispose(&v->visuals);
    application_unified_events_dispose(&v->events);
    application_unified_components_dispose(v->components);
    free(v);
}
