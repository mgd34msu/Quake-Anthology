#include "unified_output_capture_private.h"
#include "unified_presentations.h"
#include "unified_prediction.h"
#include "unified_events.h"
#include "unified_frame_private.h"
#include "internal.h"

#include <stdlib.h>

bool application_unified_output_capture_current(const application_unified_output_capture *v)
{
    if (!v) return false;
    if (v->sealed) return !v->components || application_unified_components_current(v->components);
    if (!application_unified_source_current(v->application, &v->source) ||
        !application_unified_player_current(v->application, v->recipient, &v->player) ||
        qa_actors_revision(qa_session_actors(v->source.session)) != v->actors_revision ||
        !application_unified_presentations_current(v->application, &v->visuals) ||
        !application_unified_q3_sources_current(v->q3_sources) ||
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
        application_unified_q3_sources_current(v->q3_sources) &&
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

bool application_unified_output_acquire(qa_application *app, const application_unified_source *source,
    qa_unified_world_frame *world,
    qa_unified_frame_pool *pool, const application_unified_metadata_receipt *committed_metadata,
    const qa_unified_document *committed_source_metadata,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    int64_t acknowledged, uint64_t after, application_unified_component_publisher *publisher,
    const application_unified_output_external *external,
    application_unified_output_capture **out, qa_error *e)
{
    if (!out || *out || !source || !player || !world || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, recipient, player) ||
        (external && (!external->current || (external->control_count && !external->controls))))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified capture requires its actual Source children and recipient");
    if (external && !external->current(external->context, app, source, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified capture external Source child has retired");
    qa_unified_frame *frame = qa_unified_frame_create(pool, e);
    if (!frame) return false;
    application_unified_output_capture *v = application_unified_frame_alloc(frame->lease, 1, sizeof(*v), e);
    if (!v) { qa_unified_frame_destroy(frame); return false; }
    if (frame->lease && !qa_unified_frame_lease_retain(frame->lease, e)) {
        qa_unified_frame_destroy(frame); return false;
    }
    v->lease = frame->lease; v->owned = frame;
    v->application = app; v->source = *source; v->recipient = recipient; v->player = *player;
    v->actors_revision = qa_actors_revision(qa_session_actors(source->session));
    if (external) { v->external = *external; v->has_external = true; }
    bool ok = qa_unified_world_frame_retain(world, e);
    const qa_inventory_entry *entries = NULL;
    size_t entry_count = 0;
    if (ok) {
        v->owned->world = world; v->owned->epoch = epoch; v->owned->acknowledged_input = acknowledged;
        ok = application_unified_output_inventory(app, player->actor, v->owned, &entries, &entry_count, e) &&
            application_unified_prediction_build(app, source, recipient, player, acknowledged, v->owned, entries, entry_count, e) &&
            application_unified_player_values(app, source, recipient, player,
                external ? external->player : NULL, v->owned, entries, entry_count, e) &&
            application_unified_presentations_build(app, source, recipient, player, v->owned, &v->visuals, e) &&
            application_unified_world_text_read(app, source, v->owned->lease, v->owned->visuals, e) &&
            application_unified_q3_sources_build(app, source, recipient, player, v->owned, &v->q3_sources, e) &&
            application_unified_events_read(app, source, recipient, player, epoch, after, &v->events, e);
    }
    if (ok && publisher) ok = application_unified_components_prepare(publisher, source, player,
        epoch, v->owned, v->owned->player, &v->components, e);
    if (ok) ok = application_unified_output_metadata(app, source, v->q3_sources, v->owned, epoch, committed_metadata,
        committed_source_metadata, &v->metadata_receipt, &v->metadata, e);
    if (ok) {
        v->owned->q3 = application_unified_q3_sources_take(v->q3_sources);
        if (v->components) v->owned->components = application_unified_components_take(v->components);
    }
    const qa_unified_document **controls = NULL;
    size_t count = v->events.control_count;
    if (ok && v->metadata) ++count;
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
        controls = application_unified_frame_alloc(v->lease, count, sizeof(*controls), e);
        if (!controls) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining Unified prerequisite order");
    }
    if (ok) {
        for (size_t i = 0; i < v->events.control_count; ++i) controls[i] = v->events.controls[i];
        size_t at = v->events.control_count;
        if (v->metadata) controls[at++] = v->metadata;
        if (component_control) controls[at++] = component_control;
        for (size_t i = 0; i < v->external.control_count; ++i)
            controls[at + i] = v->external.controls[i];
        application_unified_frame_children children = {.context = v, .current = children_current,
            .controls = controls, .control_count = count};
        ok = application_unified_output_build(app, source, recipient, player, &v->owned, &children, &v->output, e);
    }
    if (!v->lease) free(controls);
    if (!ok) { application_unified_output_capture_dispose(v); return false; }
    *out = v;
    return true;
}

const application_unified_output *application_unified_output_capture_value(const application_unified_output_capture *v)
{ return v ? &v->output : NULL; }
uint64_t application_unified_output_capture_events_through(const application_unified_output_capture *v)
{ return v ? v->events.through : 0; }
const application_unified_metadata_receipt *application_unified_output_capture_metadata(const application_unified_output_capture *v)
{ return v ? &v->metadata_receipt : NULL; }
const qa_unified_document *application_unified_output_capture_metadata_document(const application_unified_output_capture *v)
{ return v ? v->metadata : NULL; }
bool application_unified_output_capture_seal(application_unified_output_capture *v, qa_error *e)
{
    if (!application_unified_output_capture_current(v))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified output changed before its immutable queue token was sealed");
    if (v->components && !application_unified_components_seal(v->components, v->output.frame, e)) return false;
    v->sealed = true;
    return true;
}
void application_unified_output_capture_commit(application_unified_output_capture *v)
{ if (v && v->sealed) application_unified_components_commit(v->components); }
void application_unified_output_capture_dispose(application_unified_output_capture *v)
{
    if (!v) return;
    qa_unified_frame_lease *lease = v->lease;
    application_unified_output_dispose(&v->output);
    qa_unified_document_destroy(v->metadata);
    qa_unified_frame_destroy(v->owned);
    application_unified_q3_sources_dispose(v->q3_sources);
    application_unified_events_dispose(&v->events);
    application_unified_components_dispose(v->components);
    if (lease) qa_unified_frame_lease_release(lease);
    else free(v);
}
