#include "remote_unified_private.h"
#include "qa/network_unified_control.h"
#include "remote_unified_presentation.h"
#include "remote_unified_render.h"
#include "remote_unified_events.h"
#include "remote_unified_metadata.h"
#include "remote_unified_q1.h"
#include "remote_unified_q2.h"
#include "remote_unified_q3.h"
#include "remote_unified_components.h"
#include "remote_unified_input.h"
#include "unified_q3_sources.h"
#include "remote_unified_save.h"
#include "unified_q3_runtime_factory.h"
#include "unified_q3_video.h"
#include "video_guests.h"
#include "qa/source_frame_time.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct unified_audio_identity {
    struct unified_audio_identity *next;
    qa_actor_id actor;
    uint64_t audio;
} unified_audio_identity;
typedef struct unified_presentation unified_presentation;
typedef struct unified_q3_client_row {
    struct unified_q3_client_row *next;
    frontend_unified_q3_client *client;
    frontend_unified_q3_client_frame *frame;
    frontend_unified_q3_runtime_factory *factory;
    frontend_unified_q3_source_retirement *retirement;
    unified_presentation *owner;
    uint64_t receiver,audio_owner;
    bool selected, born, factory_built, factory_initialized, initialization_attempted, cg_prepared;
    bool primary_view;
    bool retirement_bound;
} unified_q3_client_row;
struct unified_presentation {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media, *candidate_media;
    frontend_unified_render *render, *candidate_render;
    frontend_remote_unified_prediction *prediction;
    frontend_unified_input *physical;
    frontend_unified_events *events;
    frontend_unified_q1 *q1;
    frontend_unified_q2 *q2;
    frontend_unified_q3 *q3;
    frontend_unified_components *components;
    frontend_unified_component_frame *component_frame;
    frontend_unified_q3_sources *q3_sources;
    frontend_unified_q3_source_frame *q3_source_frame;
    unified_q3_client_row *q3_clients,*retired_q3;
    unified_audio_identity *audio;
    qa_scene_light *draw_lights,*reflected_lights;
    size_t q2_light_offset,q2_light_count,draw_light_count;
    uint64_t light_frame;
    uint64_t audio_owner;
    bool received, frame_prepared, busy;
    frontend_unified_recipient_clock clock;
    bool clock_started;
    const frontend_unified_q3_video *video;
};
static bool input(void *,frontend_remote_unified *,const qa_unified_input *,double,qa_error *);
static bool q3_row_source(const unified_q3_client_row *,bool,frontend_unified_q3_source_view *,qa_error *);
static unified_q3_client_row *q3_roster_at(const unified_presentation *p,size_t index)
{
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) if (!row->born && !index--) return row;
    for (unified_q3_client_row *row=p->retired_q3;row;row=row->next) if (!index--) return row;
    return NULL;
}
static bool q3_retirement_drain(unified_presentation *p,qa_error *error)
{
    while (p->retired_q3) {
        unified_q3_client_row *row=p->retired_q3;
        if (!frontend_unified_q3_runtime_factory_destroy(&row->factory,error) ||
            !frontend_unified_q3_client_destroy(&row->client,error)) return false;
        row->retirement_bound=false;
        if (!frontend_unified_q3_source_retirement_return(&row->retirement,error)) return false;
        p->retired_q3=row->next; free(row);
    }
    return true;
}
static bool q3_retirement_rollback(unified_q3_client_row *row,qa_error *error)
{
    if (row->retirement_bound) {
        if (!frontend_unified_q3_client_retirement_unbind(row->client,row->retirement,error)) return false;
        row->retirement_bound=false;
    }
    return frontend_unified_q3_source_retirement_return(&row->retirement,error);
}

static bool audio_actor(void *context, qa_actor_id actor, uint64_t *out, qa_error *error)
{
    unified_presentation *p = context;
    qa_saved_actor_id reference;
    if (!p || !out || !qa_actors_save_reference(frontend_remote_unified_registry(p->replica),actor,&reference,error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified audio actor belongs to another replica");
    qa_saved_actor_id wire;
    if (!frontend_remote_unified_wire_actor(p->replica, actor, &wire))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified audio actor has no actual wire identity");
    for (unified_audio_identity *row = p->audio; row; row = row->next)
        if (qa_actor_id_equal(row->actor, actor)) { *out = row->audio; return true; }
    unified_audio_identity *row = calloc(1, sizeof(*row));
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining private unified audio identity");
    if (!frontend_source_identity_allocate(p->frontend, &row->audio, error)) { free(row); return false; }
    row->actor = actor; row->next = p->audio; p->audio = row; *out = row->audio; return true;
}
static bool presentation_validate(void *context,const qa_unified_presentation_event *row,qa_error *error)
{
    unified_presentation *p = context;
    if (row->payload.kind==QA_UNIFIED_PRESENTATION_OWNER)
        return frontend_unified_q1_owner_validate(p->q1,row,error) &&
            frontend_unified_q2_owner_validate(p->q2,row,error) &&
            frontend_unified_q3_owner_validate(p->q3,row,error);
    if (p->components) {
        bool handled=false;
        if (!frontend_unified_components_player_event_validate(p->components,row,&handled,error)) return false;
        if (handled) return true;
    }
    if (row->family==QA_GAME_Q1) return frontend_unified_q1_presentation_validate(p->q1,row,error);
    if (row->family==QA_GAME_Q3) return frontend_unified_q3_presentation_validate(p->q3,row,error);
    return frontend_unified_q2_presentation_validate(p->q2,row,error);
}
static bool simulation_validate(void *context,const qa_unified_simulation_event *row,qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q2_simulation_validate(p->q2,row,error);
}
static bool presentation(void *context,const qa_unified_presentation_event *row,bool *mirrored,qa_error *error)
{
    unified_presentation *p = context;
    if (row->payload.kind==QA_UNIFIED_PRESENTATION_OWNER) {
        *mirrored=false;
        return frontend_unified_q1_owner_retire(p->q1,row,error) &&
            frontend_unified_q2_owner_retire(p->q2,row,error) &&
            frontend_unified_q3_owner_retire(p->q3,row,error);
    }
    if (p->components) {
        bool handled=false;
        if (!frontend_unified_components_player_event(p->components,row,&handled,error)) return false;
        if (handled) { *mirrored=false; return true; }
    }
    if (row->family==QA_GAME_Q1) {
        if (row->payload.kind==QA_UNIFIED_PRESENTATION_BUILTIN &&
            row->payload.value.builtin.kind==QA_BUILTIN_SOUND &&
            !(row->payload.value.builtin.flags & 1u)) {
            bool paired=false; *mirrored=false;
            return frontend_unified_events_sound_mirrored(p->events,row,&paired,error) &&
                frontend_unified_q1_sound_presentation(p->q1,row,paired,error);
        }
        return frontend_unified_q1_presentation(p->q1,row,mirrored,error);
    }
    if (row->family==QA_GAME_Q3) return frontend_unified_q3_presentation(p->q3,row,mirrored,error);
    return frontend_unified_q2_presentation(p->q2,row,mirrored,error);
}
static bool simulation(void *context,const qa_unified_simulation_event *row,qa_error *error)
{
    unified_presentation *p = context;
    return frontend_unified_q2_simulation(p->q2,row,error);
}
static bool children_returned(const unified_presentation *p,const frontend_remote_unified *replica)
{
    return p && (!p->replica || p->replica == replica) && !p->busy &&
        frontend_unified_media_idle(p->media) && frontend_unified_media_idle(p->candidate_media) &&
        frontend_unified_render_idle(p->render) && frontend_unified_render_idle(p->candidate_render) &&
        (!p->prediction || frontend_remote_unified_prediction_idle(p->prediction)) &&
        (!p->physical || frontend_unified_input_idle(p->physical)) &&
        (!p->events || frontend_unified_events_idle(p->events)) &&
        (!p->q1 || frontend_unified_q1_idle(p->q1)) &&
        (!p->q2 || frontend_unified_q2_idle(p->q2)) &&
        (!p->q3 || frontend_unified_q3_idle(p->q3)) &&
        (!p->q3_sources || frontend_unified_q3_sources_idle(p->q3_sources)) &&
        (!p->components || frontend_unified_components_idle(p->components));
}
static bool idle(void *context, const frontend_remote_unified *replica)
{
    unified_presentation *p = context;
    if (p) for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!frontend_unified_q3_client_idle(row->client) ||
            !frontend_unified_q3_runtime_factory_idle(row->factory)) return false;
    if (p) for (const unified_q3_client_row *row=p->retired_q3;row;row=row->next)
        if (!frontend_unified_q3_client_idle(row->client) ||
            !frontend_unified_q3_runtime_factory_idle(row->factory)) return false;
    return p && !p->video && children_returned(p,replica);
}
static bool checkpoint_returned(void *context,const frontend_remote_unified *replica)
{
    const unified_presentation *p=context;
    if (!p || p->replica!=replica || p->busy || p->video || replica->busy ||
        !frontend_unified_media_checkpoint_ready(p->media) || !frontend_unified_media_checkpoint_ready(p->candidate_media) ||
        !frontend_unified_render_idle(p->render) || !frontend_unified_render_idle(p->candidate_render) ||
        (p->candidate_render && !frontend_unified_render_pending_current(p->candidate_render)) ||
        (p->prediction && !frontend_remote_unified_prediction_idle(p->prediction)) ||
        (p->physical && !frontend_unified_input_idle(p->physical)) ||
        !frontend_unified_events_checkpoint_ready(p->events) ||
        !frontend_unified_q1_checkpoint_ready(p->q1) || !frontend_unified_q2_checkpoint_ready(p->q2) ||
        (p->q3 && !frontend_unified_q3_checkpoint_ready(p->q3)) ||
        (p->components && !frontend_unified_components_checkpoint_ready(p->components))) return false;
    bool checkpoint=p->frontend->capture!=NULL || p->frontend->source_restoring;
    if (p->q3_source_frame ? !(checkpoint?frontend_unified_q3_source_frame_checkpoint_ready(p->q3_source_frame):
        frontend_unified_q3_sources_ready(p->q3_source_frame)):
        !frontend_unified_q3_sources_idle(p->q3_sources)) return false;
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        if (row->cg_prepared || !row->client) return false;
        if (row->frame) {
            if (!(checkpoint?frontend_unified_q3_client_checkpoint_stage_current(row->client,row->frame):
                frontend_unified_q3_client_ready(row->frame)) ||
                (row->factory && !(checkpoint?
                    frontend_unified_q3_runtime_factory_rebind_checkpoint_ready(row->factory,row->frame):
                    frontend_unified_q3_runtime_factory_rebind_ready(row->factory,row->frame)))) return false;
        } else if (!frontend_unified_q3_client_idle(row->client) ||
            (checkpoint && !frontend_unified_q3_client_checkpoint_stage_current(row->client,NULL)) ||
            !frontend_unified_q3_runtime_factory_idle(row->factory)) return false;
    }
    for (const unified_q3_client_row *row=p->retired_q3;row;row=row->next)
        if (!frontend_unified_q3_client_idle(row->client) ||
            !frontend_unified_q3_runtime_factory_idle(row->factory)) return false;
    return true;
}
bool frontend_remote_unified_presentation_video_current(const frontend_remote_unified *replica,
    const frontend_unified_q3_video *video,qa_error *error)
{
    unified_presentation *p=replica && replica->options.consumers.input==input?
        replica->options.consumers.context:NULL;
    if (!p || p->replica!=replica || !video || p->video!=video || replica->busy ||
        p->frame_prepared || p->candidate_render || p->component_frame ||
        p->q3_source_frame || replica->prepared_frame || p->retired_q3 ||
        !children_returned(p,replica) || !frontend_video_guests_read(p->frontend) ||
        !frontend_remote_unified_current(replica,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified video lost its returned retained parent cohort");
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (row->born || row->frame || row->cg_prepared || row->retirement ||
            !frontend_unified_q3_client_current(row->client) ||
            !frontend_unified_q3_runtime_factory_idle(row->factory))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified video changed its real Source CLIENT cohort");
    return true;
}
bool frontend_remote_unified_presentation_video_held(const frontend_remote_unified *replica)
{
    const unified_presentation *p=replica && replica->options.consumers.input==input?
        replica->options.consumers.context:NULL;
    return p && p->replica==replica && p->video;
}
bool frontend_remote_unified_presentation_video_associate(frontend_remote_unified *replica,
    const frontend_unified_q3_video *video,qa_error *error)
{
    unified_presentation *p=replica && replica->options.consumers.input==input?
        replica->options.consumers.context:NULL;
    if (!p || p->replica!=replica || !video || p->video || replica->busy || p->frame_prepared ||
        p->candidate_render || p->component_frame || p->q3_source_frame ||
        replica->prepared_frame || !frontend_video_guests_read(p->frontend) ||
        !frontend_remote_unified_current(replica,error) || !q3_retirement_drain(p,error) || !idle(p,replica) ||
        (p->physical && frontend_unified_input_pending(p->physical)))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified video requires its returned frame and drained Source retirement");
    p->video=video; return true;
}
bool frontend_remote_unified_presentation_video_release(frontend_remote_unified *replica,
    const frontend_unified_q3_video *video,qa_error *error)
{
    if (!frontend_remote_unified_presentation_video_current(replica,video,error)) return false;
    unified_presentation *p=replica->options.consumers.context;
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!frontend_unified_q3_client_idle(row->client))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified video release precedes actual CLIENT ticket return");
    p->video=NULL; return true;
}
bool frontend_remote_unified_presentation_video_returned(const qa_frontend *frontend,
    const frontend_video_guests *aggregate,qa_error *error)
{
    if (!frontend_video_guests_parent_is(frontend,aggregate)) return false;
    for (const frontend_remote_unified *replica=frontend->remote_unified;replica;replica=replica->next) {
        unified_presentation *p=replica->options.consumers.input==input?replica->options.consumers.context:NULL;
        if (!p || p->replica!=replica || replica->busy || !children_returned(p,replica) ||
            (p->video?!frontend_unified_q3_video_returned(p->video,aggregate,error):!idle(p,replica)))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified resource refresh lacks its actual closed video cohort");
    }
    return true;
}
static void frame_abort(unified_presentation *p)
{
    unified_q3_client_row **slot=&p->q3_clients;
    while (*slot) {
        unified_q3_client_row *row=*slot;
        if (row->factory && row->frame)
            frontend_unified_q3_runtime_factory_rebind_abort(row->factory,row->frame);
        frontend_unified_q3_client_abort(&row->frame);
        q3_retirement_rollback(row,NULL);
        row->selected=false;
        if (row->born && frontend_unified_q3_runtime_factory_destroy(&row->factory,NULL) &&
            frontend_unified_q3_client_destroy(&row->client,NULL)) {
            *slot=row->next; free(row);
        } else slot=&row->next;
    }
    frontend_unified_events_frame_abort(p->events);
    frontend_unified_q1_frame_abort(p->q1);
    frontend_unified_q2_frame_abort(p->q2);
    frontend_unified_q3_frame_abort(p->q3);
    frontend_unified_components_frame_abort(&p->component_frame);
    frontend_unified_q3_sources_abort(&p->q3_source_frame);
    p->frame_prepared = false;
}
static bool close_children(unified_presentation *p, qa_error *error)
{
    if (p->video) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified retirement retains its video cohort");
    frame_abort(p);
    if (!idle(p,p->replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified CLIENT children have not returned");
    if (!q3_retirement_drain(p,error)) return false;
    while (p->q3_clients) {
        unified_q3_client_row *row=p->q3_clients;
        if (!frontend_unified_q3_runtime_factory_destroy(&row->factory,error) ||
            !frontend_unified_q3_client_destroy(&row->client,error)) return false;
        row->retirement_bound=false;
        if (!frontend_unified_q3_source_retirement_return(&row->retirement,error)) return false;
        p->q3_clients=row->next; free(row);
    }
    if (!frontend_unified_input_destroy(&p->physical,error) ||
        !frontend_unified_q1_destroy(&p->q1,error) ||
        !frontend_unified_q2_destroy(&p->q2,error) ||
        !frontend_unified_q3_destroy(&p->q3,error) ||
        !frontend_unified_components_destroy(&p->components,error) ||
        !frontend_unified_q3_sources_destroy(&p->q3_sources,error) ||
        !frontend_unified_events_destroy(&p->events,error) ||
        !frontend_unified_render_destroy(&p->candidate_render,error) ||
        !frontend_unified_render_destroy(&p->render,error) ||
        !frontend_remote_unified_prediction_destroy(&p->prediction,error)) return false;
    if (!frontend_unified_media_destroy(p->media,error)) return false;
    p->media = NULL; p->received = false;
    free(p->draw_lights); p->draw_lights=NULL;
    free(p->reflected_lights); p->reflected_lights=NULL;
    p->q2_light_offset=0; p->q2_light_count=0; p->draw_light_count=0;
    while (p->audio) { unified_audio_identity *row = p->audio; p->audio = row->next; free(row); }
    p->audio_owner = 0;
    return true;
}
static bool prepare(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe,
    bool *ready, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || !ready || p->busy || p->video || (p->replica && p->replica != replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media preparation changed its physical CLIENT");
    p->replica = replica;
    if (p->candidate_media && !frontend_unified_media_ready(p->candidate_media)) {
        if (!frontend_unified_media_destroy(p->candidate_media,error)) return false;
        p->candidate_media=NULL;
    }
    if (!p->candidate_media && !frontend_unified_media_create(p->frontend,recipe,
        replica->options.domain.physical_seat,&p->candidate_media,error)) return false;
    *ready = frontend_unified_media_ready(p->candidate_media); return *ready;
}
static bool offer_publish(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->video || p->replica != replica || !p->candidate_media || !recipe ||
        !frontend_unified_media_ready(p->candidate_media) || !close_children(p,error)) return false;
    p->media = p->candidate_media; p->candidate_media = NULL; return true;
}
static bool offer_ready(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->video || p->replica != replica || !p->media || recipe != frontend_remote_unified_recipe(replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified child construction precedes actual recipe publication");
    if (!p->audio_owner && !frontend_source_identity_allocate(p->frontend,&p->audio_owner,error)) return false;
    frontend_unified_event_options options = {.audio_owner=p->audio_owner,.context=p,
        .presentation_validate=presentation_validate,.simulation_validate=simulation_validate,
        .presentation=presentation,.simulation=simulation,.audio_actor=audio_actor};
    if (!p->events && !frontend_unified_events_create(p->frontend,replica,p->media,&options,&p->events,error)) return false;
    frontend_unified_q1_options q1 = {.audio_owner=p->audio_owner,.context=p,.audio_actor=audio_actor};
    if (!p->q1 && !frontend_unified_q1_create(p->frontend,replica,p->media,&q1,&p->q1,error)) return false;
    if (!p->q2 && !frontend_unified_q2_create(p->frontend,replica,p->media,p->events,&p->q2,error)) return false;
    if (!p->q3 && !frontend_unified_q3_create(p->frontend,replica,p->media,&p->q3,error)) return false;
    if (!p->components && !frontend_unified_components_create(p->frontend,replica,p->media,&p->components,error)) return false;
    if (!p->q3_sources && !frontend_unified_q3_sources_create(replica,p->media,&p->q3_sources,error)) return false;
    return frontend_unified_q1_events(p->q1,p->events,error) &&
        frontend_unified_components_events_bind(p->components,p->events,error) &&
        frontend_unified_q3_audio(p->q3,p->audio_owner,p,audio_actor,error) &&
        frontend_unified_q3_events(p->q3,p->events,error) &&
        frontend_unified_q3_components(p->q3,p->components,error);
}
static bool control(void *context, frontend_remote_unified *replica, const qa_unified_document *doc, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->video || p->replica != replica || !p->events)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified control has no prepared CLIENT consumers");
    if (qa_unified_document_metadata(doc)) return frontend_remote_unified_metadata_control(replica,doc,error);
    if (qa_unified_document_events(doc)) return frontend_unified_events_control(p->events,doc,error);
    if (qa_unified_document_control_type(doc)==QA_UNIFIED_CONTROL_COMPONENTS)
        return frontend_unified_q2_components_control(p->q2,doc,error) &&
            frontend_unified_components_control(p->components,doc,error);
    return frontend_unified_events_control(p->events,doc,error);
}
static bool q3_clients_prepare(unified_presentation *p,qa_error *error)
{
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        if (row->retirement && !q3_retirement_rollback(row,error)) return false;
        row->selected=false;
    }
    for (size_t i=0;i<frontend_unified_q3_source_frame_count(p->q3_source_frame);++i) {
        frontend_unified_q3_source_view view;
        if (!frontend_unified_q3_source_frame_read(p->q3_source_frame,i,&view,error)) return false;
        if (!view.has_client) continue;
        unified_q3_client_row *row=p->q3_clients;
        while (row && !frontend_unified_q3_client_matches(row->client,&view)) row=row->next;
        if (!row) {
            row=calloc(1,sizeof(*row));
            if (!row) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Retaining a per-Source compiled CLIENT");
            if (!frontend_source_identity_allocate(p->frontend,&row->receiver,error) ||
                !frontend_source_identity_allocate(p->frontend,&row->audio_owner,error)) { free(row); return false; }
            row->born=true; row->owner=p;
            unified_q3_client_row **tail=&p->q3_clients;
            while (*tail) tail=&(*tail)->next;
            *tail=row;
            if (!frontend_unified_q3_client_create(p->replica,p->q3_sources,&view,row->receiver,&row->client,error)) return false;
        } else if (!row->born && !row->frame &&
            !frontend_unified_q3_client_prepare(row->client,&view,&row->frame,error)) return false;
        if (row->factory && row->frame &&
            !frontend_unified_q3_runtime_factory_rebind_ready(row->factory,row->frame) &&
            !frontend_unified_q3_runtime_factory_rebind_prepare(row->factory,row->frame,error)) return false;
        if (row->selected) return frontend_unified_fail(error,QA_ERROR_FORMAT,"Compiled Source repeats its actual CLIENT activation");
        row->selected=true;
    }
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) if (!row->selected) {
        frontend_unified_q3_source_view old;
        if (!q3_row_source(row,false,&old,error) ||
            !frontend_unified_q3_source_retirement_prepare(&old,&row->retirement,error) ||
            !frontend_unified_q3_client_retirement_bind(row->client,row->retirement,error)) return false;
        row->retirement_bound=true;
    }
    return true;
}
static bool q3_clients_ready(const unified_presentation *p)
{
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        if (!row->client) return false;
        if (row->selected) {
            if (row->born ? !frontend_unified_q3_client_current(row->client) :
                !frontend_unified_q3_client_ready(row->frame)) return false;
            if (row->factory && row->frame &&
                !frontend_unified_q3_runtime_factory_rebind_ready(row->factory,row->frame)) return false;
        } else if (!row->retirement || !row->retirement_bound ||
            !frontend_unified_q3_source_retirement_current(row->retirement) ||
            !frontend_unified_q3_client_retirement_current(row->client) ||
            !frontend_unified_q3_client_idle(row->client) ||
            !frontend_unified_q3_runtime_factory_idle(row->factory)) return false;
    }
    return true;
}
static void q3_clients_commit(unified_presentation *p)
{
    unified_q3_client_row **slot=&p->q3_clients;
    while (*slot) {
        unified_q3_client_row *row=*slot;
        if (row->selected) {
            if (row->factory && row->frame)
                frontend_unified_q3_runtime_factory_rebind_commit(row->factory,row->frame);
            frontend_unified_q3_client_commit(&row->frame);
            row->selected=false; row->born=false; slot=&row->next;
        } else {
            *slot=row->next; row->next=NULL;
            unified_q3_client_row **tail=&p->retired_q3;
            while (*tail) tail=&(*tail)->next;
            *tail=row;
        }
    }
}
static bool events_enter(unified_presentation *p,qa_error *error)
{
    if (p->events && !frontend_unified_events_enter(p->events,error)) return false;
    if (p->replica->retired) return true;
    if (p->frame_prepared) return true;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!row->born && !frontend_unified_q3_client_seal(row->client,error)) return false;
    return true;
}
static bool q3_row_source(const unified_q3_client_row *row,bool checkpoint,
    frontend_unified_q3_source_view *out,qa_error *error)
{
    const unified_presentation *p=row?row->owner:NULL;
    if (!p || !row->client || !p->q3_sources || !out)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG row has no retained CLIENT Source");
    if (row->retirement) {
        if (!checkpoint || !frontend_unified_q3_source_retirement_checkpoint_current(row->retirement))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Removed compiled Source grants only retained cold custody");
        return frontend_unified_q3_source_retirement_read(row->retirement,out,error);
    }
    size_t count=checkpoint?(row->born?frontend_unified_q3_source_frame_count(p->q3_source_frame):
        frontend_unified_q3_sources_committed_count(p->q3_sources)):
        frontend_unified_q3_sources_committed_count(p->q3_sources);
    for (size_t i=0;i<count;++i) {
        frontend_unified_q3_source_view source;
        if (!(checkpoint?frontend_unified_q3_sources_checkpoint_stage_read(p->q3_sources,p->q3_source_frame,
            row->born,i,&source,error):
            frontend_unified_q3_sources_committed_read(p->q3_sources,i,&source,error))) return false;
        if (checkpoint?frontend_unified_q3_client_checkpoint_matches(row->client,&source):
            frontend_unified_q3_client_matches(row->client,&source)) { *out=source; return true; }
    }
    return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG row lost its genuine Source activation");
}
static bool q3_factory_current(void *context,const frontend_unified_q3_runtime_factory_options *options,
    bool checkpoint)
{
    unified_q3_client_row *row=context;
    unified_presentation *p=row?row->owner:NULL;
    if (!p || !options || options->frontend!=p->frontend || options->replica!=p->replica ||
        options->media!=p->media || options->client!=row->client || options->receiver!=row->receiver ||
        options->events!=p->events || options->prediction!=p->prediction || options->audio_context!=p ||
        options->audio_owner!=row->audio_owner || options->audio_actor!=audio_actor || options->primary_view!=row->primary_view) return false;
    bool linked=false;
    for (const unified_q3_client_row *candidate=p->q3_clients;candidate;candidate=candidate->next)
        if (candidate==row) { linked=true; break; }
    if (checkpoint) for (const unified_q3_client_row *candidate=p->retired_q3;candidate;candidate=candidate->next)
        if (candidate==row) { linked=true; break; }
    frontend_unified_q3_source_view source;
    return linked && q3_row_source(row,checkpoint,&source,NULL) &&
        source.provider==options->source.provider && source.files==options->source.files &&
        source.assets==options->source.assets && source.product==options->source.product &&
        options->source.instance && options->source.content &&
        !strcmp(source.instance,options->source.instance) && !strcmp(source.content,options->source.content) &&
        (checkpoint?frontend_unified_q3_client_checkpoint_stage_current(row->client,row->frame):
            frontend_unified_q3_client_current(row->client));
}
static bool q3_factory_retirement_current(void *context,
    const frontend_unified_q3_runtime_factory_options *options)
{
    unified_q3_client_row *row=context;
    unified_presentation *p=row?row->owner:NULL;
    if (!p || !options || options->frontend!=p->frontend || options->replica!=p->replica ||
        options->media!=p->media || options->client!=row->client || options->receiver!=row->receiver ||
        options->events!=p->events || options->audio_owner!=row->audio_owner ||
        frontend_unified_media_recipe(p->media)!=p->replica->recipe ||
        !frontend_unified_q3_client_idle(row->client)) return false;
    bool linked=false,bank_found=false;
    for (const unified_q3_client_row *candidate=p->q3_clients;candidate;candidate=candidate->next)
        if (candidate==row) { linked=true; break; }
    for (const unified_q3_client_row *candidate=p->retired_q3;candidate;candidate=candidate->next)
        if (candidate==row) { linked=true; break; }
    if (row->retirement && (!frontend_unified_q3_source_retirement_current(row->retirement) ||
        !frontend_unified_q3_client_retirement_current(row->client))) return false;
    for (size_t i=0;i<frontend_unified_media_bank_count(p->media);++i) {
        frontend_unified_bank_view bank;
        if (!frontend_unified_media_bank_read(p->media,i,&bank)) return false;
        if (bank.files==options->source.files && bank.q3_assets==options->source.assets &&
            bank.content && (!options->source.content || !strcmp(bank.content,options->source.content))) {
            bank_found=true; break;
        }
    }
    return linked && bank_found;
}
static bool q3_input_read(void *context,frontend_remote_unified *replica,
    frontend_unified_input **out,qa_error *error)
{
    unified_q3_client_row *row=context;
    unified_presentation *p=row?row->owner:NULL;
    if (!p || !out || p->replica!=replica || !p->physical)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG input has no published physical CLIENT builder");
    *out=p->physical; return true;
}
static bool q3_send_client(void *context,frontend_unified_q3_client *client,
    const qa_command_context *origin,const char *text,qa_error *error)
{
    unified_q3_client_row *row=context;
    unified_presentation *p=row?row->owner:NULL;
    frontend_unified_q3_source_view source;
    if (!p || row->client!=client || !origin || !text || origin->owner!=row->receiver ||
        !q3_row_source(row,false,&source,error) || !qa_actor_id_equal(origin->actor,source.viewer) ||
        !frontend_remote_unified_current(p->replica,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled client command changed its actual lexical Source");
    if (!p->replica->options.source_command)
        return frontend_unified_fail(error,QA_ERROR_UNSUPPORTED,"Compiled Source command channel has no installed scoped emitter");
    qa_command_tokens arguments={0};
    if (!qa_command_tokenize(text,QA_CONSOLE_Q3,false,&arguments,error)) return false;
    bool okay=p->replica->options.source_command(p->replica->options.context,
        &p->replica->options.domain,source.instance,source.publication,source.map_revision,origin,&arguments,error);
    qa_command_tokens_free(&arguments);
    return okay && frontend_remote_unified_current(p->replica,error);
}
bool frontend_remote_unified_presentation_source_command_current(const frontend_remote_unified *replica,
    const char *instance,uint64_t publication,uint64_t map_revision,const qa_command_context *origin,qa_error *error)
{
    if (!replica || !instance || !origin || replica->options.consumers.input!=input ||
        !frontend_remote_unified_current(replica,error)) return false;
    unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica) return false;
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        frontend_unified_q3_source_view source;
        if (!q3_row_source(row,false,&source,error)) return false;
        if (strcmp(source.instance,instance)) continue;
        const qa_command_context *actual=frontend_unified_q3_client_context(row->client);
        return (actual && source.publication==publication && source.map_revision==map_revision &&
            origin->owner==row->receiver && origin->session==actual->session && origin->client==actual->client &&
            origin->seat==actual->seat && origin->registry==actual->registry && origin->generation==actual->generation &&
            origin->dialect==QA_CONSOLE_Q3 && origin->origin==actual->origin && qa_actor_id_equal(origin->actor,source.viewer)) ||
            frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Source command lost its real compiled CLIENT origin");
    }
    return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Source command has no published compiled CLIENT namespace");
}
static bool q3_primary_view(unified_presentation *p,const frontend_unified_q3_source_view *source,
    bool *primary,qa_error *error)
{
    const qa_recipe_choices *choices=qa_executable_recipe_choices(frontend_remote_unified_recipe(p->replica));
    const qa_recipe_binding *binding=NULL;
    if (!choices || !source || !source->provider || !source->provider->selection.instance || !primary)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled Source view has no real immutable world binding");
    for (size_t i=0;i<choices->binding_count;++i) {
        const qa_recipe_binding *candidate=choices->bindings+i;
        if (candidate->scope.kind!=QA_SCOPE_WORLD || candidate->role!=QA_ROLE_ENTITIES ||
            (candidate->selector && candidate->selector[0])) continue;
        if (binding) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled camera world binding is ambiguous");
        binding=candidate;
    }
    *primary=binding && binding->instance && !strcmp(binding->instance,source->provider->selection.instance);
    return true;
}
static bool q3_view_replacement(void *context,const q3n_frame *frame,const qa_q3_player *player,
    bool *consumed,qa_error *error)
{
    unified_q3_client_row *row=context;
    unified_presentation *p=row?row->owner:NULL;
    bool linked=false;
    if (p) for (const unified_q3_client_row *actual=p->q3_clients;actual;actual=actual->next)
        if (actual==row) { linked=true; break; }
    if (!linked || !p->render || !frame || !frame->compiled || !consumed ||
        frame->compiled->source.basis.receiver!=row->receiver ||
        frontend_unified_q3_runtime_entered(frontend_unified_q3_runtime_factory_runtime(row->factory))!=frame->compiled)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"View equipment callback has no actual entered Source factory");
    frontend_unified_render_equipment before={0},after={0}; bool present=false,retained=false,result=false;
    qa_actor_id viewer=frame->compiled->source.basis.viewer;
    if (!frontend_unified_render_equipment_read(p->render,viewer,&before,&present,error) ||
        !frontend_unified_q3_equipment_replacement(p->q3,frame,player,&before,present,&result,error) ||
        !frontend_unified_render_equipment_read(p->render,viewer,&after,&retained,error) ||
        present!=retained || (present && (before.input!=after.input || before.provider!=after.provider ||
            before.instance!=after.instance || before.slot!=after.slot || before.visible!=after.visible ||
            before.source_frame!=after.source_frame || before.scene_sequence!=after.scene_sequence)))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"View equipment callback changed its published renderer receipt");
    *consumed=result; return true;
}
static bool status_replacement(void *context,bool *out,qa_error *error)
{
    unified_presentation *p=context;
    bool components=false,q2=false;
    if (!p || !p->render || !frontend_unified_components_status_replacement(p->components,&components,error) ||
        !frontend_unified_q2_status_replacement(p->q2,&q2,error)) return false;
    *out=components || q2; return true;
}
static bool q3_camera_override(void *context,const q3n_frame *frame,
    qa_application_camera_view *out,bool *found,qa_error *error)
{
    unified_q3_client_row *row=context;unified_presentation *p=row?row->owner:NULL;
    if(!p || !p->render || !row->primary_view || !frame || !frame->compiled ||
        frame->compiled->source.basis.receiver!=row->receiver ||
        frontend_unified_q3_runtime_entered(frontend_unified_q3_runtime_factory_runtime(row->factory))!=frame->compiled ||
        !q3n_frame_current(frame))return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Declared QC camera lost its real entered CG Source");
    qa_hud_value vitals[2];bool has_vitals=false;
    return frontend_unified_render_client_presentation_read(p->render,frame->viewing_actor,
        out,vitals,found,&has_vitals,error) && q3n_frame_current(frame);
}
static bool q3_status_replacement(void *context,const q3n_compiled_frame *frame,bool *out,qa_error *error)
{
    unified_q3_client_row *row=context;unified_presentation *p=row?row->owner:NULL;
    if(!p || !p->render || !row->primary_view || !frame ||
        frame->source.basis.receiver!=row->receiver ||
        frontend_unified_q3_runtime_entered(frontend_unified_q3_runtime_factory_runtime(row->factory))!=frame ||
        !q3n_compiled_frame_current(frame))return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Declared QC HUD lost its real entered CG Source");
    qa_application_camera_view camera;qa_hud_value vitals[2];bool has_view=false,has_vitals=false,native=false;
    if(!frontend_unified_render_client_presentation_read(p->render,frame->source.basis.viewer,
        &camera,vitals,&has_view,&has_vitals,error) || !status_replacement(p,&native,error))return false;
    *out=has_vitals && !native;return q3n_compiled_frame_current(frame);
}

static frontend_unified_q3_runtime_factory_options q3_factory_options(unified_q3_client_row *row,
    const frontend_unified_q3_source_view *source,bool primary)
{
    unified_presentation *p=row->owner;
    return (frontend_unified_q3_runtime_factory_options){.frontend=p->frontend,.replica=p->replica,
        .media=p->media,.client=row->client,.source=*source,.input_read=q3_input_read,
        .prediction=p->prediction,.events=p->events,.receiver=row->receiver,.audio_owner=row->audio_owner,.primary_view=primary,
        .audio_context=p,.audio_actor=audio_actor,.context=row,.current=q3_factory_current,
        .retirement_current=q3_factory_retirement_current,
        .send_client=q3_send_client,.view_replacement=q3_view_replacement,
        .camera_override=q3_camera_override,.status_replacement=q3_status_replacement,.composition={.context=p->q3,
            .body_hidden=frontend_unified_q3_body_hidden,.body_submit=frontend_unified_q3_body_submit,
            .player_weapon=frontend_unified_q3_player_weapon}};
}
static bool q3_factories_ensure(unified_presentation *p,qa_error *error)
{
    if (!p->q3_clients) return true;
    if (!p->received || p->frame_prepared || !events_enter(p,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG construction precedes its published Source baseline");
    if (!p->physical && !frontend_unified_input_create(p->frontend,p->replica,p->prediction,&p->physical,error)) return false;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        frontend_unified_q3_source_view source;
        bool primary=false;
        if (!q3_row_source(row,false,&source,error) || !q3_primary_view(p,&source,&primary,error)) return false;
        if (row->factory && !row->factory_built) {
            if (!frontend_unified_q3_runtime_factory_destroy(&row->factory,error)) return false;
            row->factory_built=false; row->initialization_attempted=false;
        }
        if (row->factory && row->initialization_attempted && !row->factory_initialized) {
            if (!frontend_unified_q3_runtime_factory_constructor_abort(&row->factory,error)) return false;
            row->factory_built=false; row->initialization_attempted=false;
        }
        if (!row->factory) {
            row->primary_view=primary;
            frontend_unified_q3_runtime_factory_options options=q3_factory_options(row,&source,primary);
            if (!frontend_unified_q3_runtime_factory_create(&options,&row->factory,error)) return false;
            row->factory_built=true;
        }
        if (!row->factory_built)
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG construction retains an incomplete checked child");
        if (!row->factory_initialized) {
            row->initialization_attempted=true;
            if (!frontend_unified_q3_runtime_factory_initialize(row->factory,error)) return false;
            row->factory_initialized=true;
        }
    }
    return true;
}
typedef struct unified_q3_prediction_mapping {
    unified_q3_client_row *row;
    int32_t message;
    const qa_q3_snapshot *snapshot;
} unified_q3_prediction_mapping;
static bool q3_prediction_current(void *context,qa_error *error)
{
    unified_q3_prediction_mapping *mapping=context;
    const qa_q3_snapshot *snapshot=NULL;
    return mapping && mapping->row && frontend_unified_q3_runtime_factory_current(mapping->row->factory) &&
        frontend_unified_q3_client_snapshot(mapping->row->client,mapping->message,&snapshot,error) &&
        snapshot==mapping->snapshot && snapshot && snapshot->valid;
}
static bool q3_prediction_number(void *context,qa_actor_id actor,uint32_t *number,bool *found,qa_error *error)
{
    unified_q3_prediction_mapping *mapping=context;
    if (!number || !found || !q3_prediction_current(mapping,error)) return false;
    *found=false;
    for (uint32_t i=0;i<QA_Q3_ENTITY_WORLD;++i) {
        qa_actor_id candidate; bool present;
        if (!frontend_unified_q3_client_snapshot_actor(mapping->row->client,mapping->message,i,
            &candidate,&present,error)) return false;
        if (present && qa_actor_id_equal(candidate,actor)) { *number=i; *found=true; break; }
    }
    return q3_prediction_current(mapping,error);
}
static bool q3_factory_prediction_prepare(unified_q3_client_row *row,qa_error *error)
{
    unified_presentation *p=row->owner;
    frontend_unified_q3_source_view source;
    bool active=false;
    if (!row->factory || row->cg_prepared || !q3_row_source(row,false,&source,error) ||
        !frontend_unified_q3_runtime_factory_prepare(row->factory,0,error)) return false;
    row->cg_prepared=true;
    if (!frontend_unified_q3_runtime_factory_process(row->factory,source.time,&active,error)) return false;
    if (active && row->primary_view) {
        frontend_unified_q3_runtime_prediction_baseline baseline;
        frontend_unified_prediction_view predicted;
        qa_native_q3_client_cvar no_predict,synchronous;
        if (!frontend_unified_q3_runtime_factory_prediction_baseline(row->factory,&baseline,error) ||
            !frontend_remote_unified_prediction_read(p->prediction,&predicted,error) ||
            !frontend_unified_q3_client_cvar_read(row->client,"cg_nopredict",&no_predict,error) ||
            !frontend_unified_q3_client_cvar_read(row->client,"cg_synchronousClients",&synchronous,error)) return false;
        frontend_unified_q3_prediction_receipt receipt={.baseline_revision=predicted.authoritative_frame,
            .command_receipt=predicted.sequence,.outcome=FRONTEND_UNIFIED_Q3_INTERPOLATED};
        qa_q3_player player=baseline.player;
        if (!(player.pmFlags&4096) && !no_predict.integer && !synchronous.integer) {
            unified_q3_prediction_mapping mapping={.row=row,.message=baseline.message};
            frontend_unified_q3_prediction_source mapper={.context=&mapping,
                .current=q3_prediction_current,.number=q3_prediction_number};
            if (!frontend_unified_q3_client_snapshot(row->client,baseline.message,&mapping.snapshot,error) ||
                !frontend_remote_unified_prediction_merged_q3(p->prediction,&baseline.player,baseline.viewer,
                    &mapper,&player,&predicted,error)) return false;
            receipt.outcome=predicted.status==FRONTEND_UNIFIED_PREDICTION_EXHAUSTED?FRONTEND_UNIFIED_Q3_EXHAUSTED:
                predicted.status==FRONTEND_UNIFIED_PREDICTION_ACTIVE?FRONTEND_UNIFIED_Q3_MOVED:FRONTEND_UNIFIED_Q3_UNMOVED;
            if (receipt.outcome==FRONTEND_UNIFIED_Q3_MOVED) {
                frontend_unified_prediction_view boundary; bool matched=false;
                if (!frontend_remote_unified_prediction_read_command_boundary(p->prediction,
                    baseline.previous_command_time,&boundary,&matched,error) ||
                    boundary.authoritative_frame!=predicted.authoritative_frame || boundary.sequence!=predicted.sequence)
                    return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled prediction boundary changed its actual replay receipt");
                receipt.teleport_consumed=matched;
            }
        }
        if (!frontend_unified_q3_runtime_factory_prediction(row->factory,&player,&receipt,
            baseline.correction,baseline.correction_time,baseline.hyperspace,error)) return false;
    }
    return true;
}
static bool q3_factories_prediction_prepare(unified_presentation *p,qa_error *error)
{
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!q3_factory_prediction_prepare(row,error)) return false;
    return true;
}
static bool camera(void *context,qa_scene_view *view,float *fov,bool *owned,qa_error *error)
{
    unified_presentation *p=context;
    if (!view || !fov || !owned)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled camera has no admitted world bindings");
    *owned=false;
    unified_q3_client_row *primary=NULL;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        frontend_unified_q3_source_view source;
        if (!q3_row_source(row,false,&source,error)) return false;
        bool selected=false;
        if (!q3_primary_view(p,&source,&selected,error)) return false;
        if (!selected) continue;
        if (primary) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled camera has multiple clients for its primary Source");
        primary=row;
    }
    if (!primary) return true;
    frontend_unified_q3_runtime_camera receipt; bool active=false;
    if (!primary->cg_prepared ||
        !frontend_unified_q3_runtime_factory_camera_prepare(primary->factory,&receipt,&active,error)) return false;
    if (!active) return true;
    const qa_q3_refdef *r=&receipt.refdef;
    if (r->x<0 || r->y<0 || r->width<=0 || r->height<=0 ||
        !(r->fov_x>0 && r->fov_x<180 && r->fov_y>0 && r->fov_y<180) ||
        !(receipt.near_clip>0 && receipt.far_clip>receipt.near_clip))
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Compiled camera returned an invalid actual refdef");
    int64_t x=(int64_t)receipt.viewport.x+r->x,y=(int64_t)receipt.viewport.y+r->y;
    if (x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX)
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Compiled camera viewport exceeds its physical rectangle");
    qa_scene_view selected=*view;
    selected.viewport=(qa_scene_rect){(int32_t)x,(int32_t)y,(uint32_t)r->width,(uint32_t)r->height};
    selected.origin=r->origin; memcpy(selected.axis,r->axis,sizeof(selected.axis));
    selected.projection=qa_scene_projection(r->fov_x,r->fov_y,receipt.near_clip,receipt.far_clip);
    *view=selected; *fov=r->fov_x; *owned=true; return true;
}
static bool q3_factories_frame_end(unified_presentation *p,bool completed,qa_error *error)
{
    bool okay=true;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (row->cg_prepared) {
            qa_error child_error={0};
            if (frontend_unified_q3_runtime_factory_frame_end(row->factory,completed,&child_error)) row->cg_prepared=false;
            else { if (okay && error) *error=child_error; okay=false; }
        }
    return okay;
}
static bool frame(void *context, frontend_remote_unified *replica, const qa_unified_document *doc,
    frontend_unified_frame_preparation *state, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !state || !p->events || !p->q1 || !p->q2 || !p->q3)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame has incomplete actual CLIENT children");
    if (p->video) { *state=FRONTEND_UNIFIED_FRAME_WAIT; return true; }
    if (p->physical && frontend_unified_input_pending(p->physical)) {
        *state=FRONTEND_UNIFIED_FRAME_WAIT;
        return true;
    }
    if (!q3_retirement_drain(p,error)) return false;
    if (p->frame_prepared) { *state=FRONTEND_UNIFIED_FRAME_READY; return true; }
    bool component_ready=false;
    if (p->component_frame) {
        if (!frontend_unified_components_frame_ready(p->component_frame,doc,error)) return false;
        component_ready=true;
    } else if (!frontend_unified_components_frame_prepare(p->components,doc,&p->component_frame,&component_ready,error)) {
        frontend_unified_components_frame_abort(&p->component_frame); return false;
    }
    if (!component_ready) { *state=FRONTEND_UNIFIED_FRAME_OBSOLETE; return true; }
    if (!p->q3_source_frame && !frontend_unified_q3_sources_prepare(p->q3_sources,doc,&p->q3_source_frame,error)) {
        frame_abort(p); return false;
    }
    if (!q3_clients_prepare(p,error)) { frame_abort(p); return false; }
    if (!p->prediction && !frontend_remote_unified_prediction_create(replica,&p->prediction,error)) return false;
    if (!p->candidate_render && !frontend_unified_render_create(p->frontend,replica,p->media,doc,&p->candidate_render,error)) return false;
    bool okay = frontend_unified_events_frame_prepare(p->events,doc,error) &&
        frontend_unified_q1_frame_prepare(p->q1,doc,error) && frontend_unified_q2_frame_prepare(p->q2,doc,error) &&
        frontend_unified_q3_frame_prepare(p->q3,doc,error);
    if (!okay) { frame_abort(p); return false; }
    p->frame_prepared = true; *state=FRONTEND_UNIFIED_FRAME_READY; return true;
}
static bool publish(void *context, frontend_remote_unified *replica, const qa_unified_document *doc, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->video || p->replica != replica || !p->frame_prepared || !p->candidate_render ||
        p->busy || !frontend_unified_render_idle(p->render) ||
        !frontend_unified_render_idle(p->candidate_render) ||
        !frontend_remote_unified_prediction_idle(p->prediction) ||
        !frontend_unified_events_frame_ready(p->events,doc,error) ||
        !frontend_unified_q1_frame_ready(p->q1,doc,error) ||
        !frontend_unified_q2_frame_ready(p->q2,doc,error) ||
        !frontend_unified_q3_frame_ready(p->q3,doc,error) ||
        !frontend_unified_q3_sources_ready(p->q3_source_frame) ||
        !q3_clients_ready(p) ||
        !frontend_unified_components_frame_ready(p->component_frame,doc,error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame publication lost its prepared children");
    /* This is the last fallible adoption. Metadata was installed by the replica
     * and will be restored there if prediction refuses this frame. */
    if (!frontend_remote_unified_prediction_receive(p->prediction,doc,error)) return false;
    frontend_unified_q1_frame_commit(p->q1); frontend_unified_q2_frame_commit(p->q2);
    frontend_unified_q3_frame_commit(p->q3); frontend_unified_events_frame_commit(p->events);
    frontend_unified_components_frame_commit(&p->component_frame);
    frontend_unified_q3_sources_commit(&p->q3_source_frame);
    q3_clients_commit(p);
    /* Retirement was qualified above; no callback or allocation separates that
     * check from this owned HUD/frame disposal. */
    if (!frontend_unified_render_destroy(&p->render,error)) return false;
    p->render = p->candidate_render; p->candidate_render = NULL;
    p->frame_prepared = false; p->received = true; return true;
}
static bool input(void *context, frontend_remote_unified *replica, const qa_unified_input *command,
    double time, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->video || p->replica != replica || !p->received)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified input awaits its authoritative prediction frame");
    return frontend_remote_unified_prediction_input(p->prediction,command,time,error);
}
static bool sample(void *context, frontend_remote_unified *replica, uint64_t now, qa_error *error)
{
    unified_presentation *p = context; (void)now;
    return p && !p->video && p->replica == replica && q3_retirement_drain(p,error) && events_enter(p,error);
}
static bool clock_read(void *context,const frontend_remote_unified *replica,
    frontend_unified_recipient_clock *out,qa_error *error)
{
    const unified_presentation *p=context;
    if (!p || p->replica!=replica || !out || !p->clock_started ||
        p->clock.begin_generation!=p->frontend->recipient_begin_generation ||
        p->clock.physical_frame!=p->frontend->frame_number ||
        p->clock.wall_time_ns!=p->frontend->wall_time_ns ||
        !frontend_remote_unified_current(replica,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified recipient clock has no committed physical frame");
    *out=p->clock; return true;
}
static bool begin_frame(void *context,frontend_remote_unified *replica,uint64_t now,
    uint64_t elapsed,qa_error *error)
{
    unified_presentation *p=context;
    if (!p || p->video || p->replica!=replica || p->busy || now!=p->frontend->wall_time_ns || elapsed>now)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified recipient clock changed its physical frame receipt");
    if (p->clock_started && p->clock.begin_generation==p->frontend->recipient_begin_generation)
        return (p->clock.physical_frame==p->frontend->frame_number &&
            p->clock.wall_time_ns==now && p->clock.wall_elapsed_ns==elapsed) ||
            frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified recipient begin retry changed its genuine frame tuple");
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(replica);
    double duration=(double)elapsed/1000000.0;
    if (!domain || !qa_source_frame_time_sample(domain->cvars,duration,false,false,&duration,error)) return false;
    double time=(p->clock_started?p->clock.milliseconds:(double)(now-elapsed)/1000000.0)+duration;
    if (!isfinite(time) || !isfinite(duration) || duration<0)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified recipient clock exceeds its real source time range");
    p->clock=(frontend_unified_recipient_clock){.milliseconds=time,.source_elapsed_ms=duration,
        .begin_generation=p->frontend->recipient_begin_generation,
        .physical_frame=p->frontend->frame_number,.wall_time_ns=now,.wall_elapsed_ns=elapsed};
    p->clock_started=true; return true;
}
static bool physical_ready(void *context, frontend_remote_unified *replica,
    uint64_t *sequence, bool *needed, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !sequence || !needed || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified physical input changed its CLIENT owner");
    *sequence=0; *needed=false;
    if (p->video) return true;
    if (!p->received) return true;
    if (!p->physical && !frontend_unified_input_create(p->frontend,replica,p->prediction,&p->physical,error)) return false;
    bool completed=false;
    if (!frontend_unified_input_retry(p->physical,&completed,sequence,error)) return false;
    if (!completed) *sequence=0;
    *needed=true; return true;
}
static bool physical_input(void *context, frontend_remote_unified *replica,
    const qa_seat_input_sample *sample_value, uint64_t sequence, double source_elapsed_ms, qa_error *error)
{
    unified_presentation *p=context;
    if (!p || p->video || p->replica != replica || !p->physical || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified physical sample has no authoritative input owner");
    frontend_unified_recipient_clock clock;
    (void)source_elapsed_ms;
    if (!clock_read(p,replica,&clock,error)) return false;
    return frontend_unified_input_build(p->physical,sample_value,sequence,clock.source_elapsed_ms,error);
}
static bool world(void *context, const qa_scene_view *view, const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    unified_presentation *p = context;
    return frontend_unified_q2_world(p->q2,view,input,frame,error) && frontend_unified_q3_world(p->q3,view,input,frame,error) &&
        frontend_unified_components_world(p->components,view,input,frame,error) &&
        frontend_unified_events_draw(p->events,view,frame,error);
}
static bool reflected_world(void *context,const qa_scene_world_input *input_value,
    qa_scene_frame *frame_value,qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q3_reflected_world(p->q3,input_value,frame_value,error);
}
static bool lights(void *context, const qa_scene_view *view, const qa_scene_world_input *input,
    const qa_scene_light **out, size_t *count, qa_error *error)
{
    unified_presentation *p = context;
    frontend_unified_recipient_clock clock;
    const qa_scene_light *q2=NULL,*q3=NULL,*components=NULL; size_t n=0,m=0,k=0;
    qa_q3_source_scene_bank *bank=NULL;
    if (!clock_read(p,p->replica,&clock,error) ||
        !frontend_unified_components_prepare_draw(p->components,view,p->frontend->frame.sequence,&clock,error) ||
        !frontend_unified_q2_lights(p->q2,view,input,&q2,&n,error) ||
        !frontend_unified_q3_lights(p->q3,view,input,&q3,&m,error) ||
        !frontend_unified_q3_scene_bank_read(p->q3,&p->frontend->frame,&bank,error)) return false;
    size_t native_count=0;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        const qa_scene_light *span; size_t length;
        bool active=false;
        if (!row->cg_prepared ||
            (!row->primary_view && !frontend_unified_q3_runtime_factory_scene_camera(row->factory,view,error)) ||
            !frontend_unified_q3_runtime_factory_scene_prepare(row->factory,bank,&active,error) ||
            !frontend_unified_q3_runtime_factory_scene_lights(row->factory,&span,&length,error)) return false;
        if (length>SIZE_MAX-native_count)
            return frontend_unified_fail(error,QA_ERROR_MEMORY,"Compiled Source light spans exceed draw storage");
        native_count+=length;
    }
    if (!frontend_unified_components_lights_bank(p->components,bank,&components,&k,error)) return false;
    p->q2_light_offset=input->light_count; p->q2_light_count=n;
    p->light_frame=p->frontend->frame.sequence;
    if (!n && !m && !k && !native_count) {
        p->draw_light_count=0; *out=input->lights; *count=input->light_count; return true;
    }
    size_t limit=SIZE_MAX/sizeof(*q2);
    if (input->light_count>limit || n>limit-input->light_count || m>limit-input->light_count-n ||
        k>limit-input->light_count-n-m || native_count>limit-input->light_count-n-m-k)
        return frontend_unified_fail(error,QA_ERROR_MEMORY,"Unified light pool exceeds actual draw storage");
    size_t total=n+m+k+native_count+input->light_count;
    qa_scene_light *joined=malloc(total*sizeof(*joined));
    if (!joined) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Joining received family light pools");
    if (input->light_count) memcpy(joined,input->lights,input->light_count*sizeof(*joined));
    if (n) memcpy(joined+input->light_count,q2,n*sizeof(*joined));
    if (m) memcpy(joined+input->light_count+n,q3,m*sizeof(*joined));
    if (k) memcpy(joined+input->light_count+n+m,components,k*sizeof(*joined));
    size_t offset=input->light_count+n+m+k;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        const qa_scene_light *span; size_t length;
        if (!frontend_unified_q3_runtime_factory_scene_lights(row->factory,&span,&length,error)) {
            free(joined); return false;
        }
        if (length) memcpy(joined+offset,span,length*sizeof(*joined));
        offset+=length;
    }
    free(p->draw_lights); p->draw_lights=joined; p->draw_light_count=total;
    *out=joined; *count=total; return true;
}
static bool reflected_lights(void *context,qa_scene_world_input *input,qa_scene_frame *frame,qa_error *error)
{
    unified_presentation *p=context;
    if (!p || !input || frame!=&p->frontend->frame || p->light_frame!=frame->sequence ||
        (p->draw_light_count && (input->lights!=p->draw_lights || input->light_count!=p->draw_light_count)))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Reflected lights lost their retained normal-view ranges");
    if (!p->q2_light_count) return true;
    const qa_scene_light *q2=NULL; size_t count=0;
    if (!frontend_unified_q2_lights(p->q2,&input->view,input,&q2,&count,error)) return false;
    size_t tail=p->draw_light_count-p->q2_light_offset-p->q2_light_count;
    if (count>SIZE_MAX/sizeof(*q2)-p->q2_light_offset-tail)
        return frontend_unified_fail(error,QA_ERROR_MEMORY,"Reflected Source lights exceed draw storage");
    size_t total=p->q2_light_offset+count+tail;
    qa_scene_light *joined=total?malloc(total*sizeof(*joined)):NULL;
    if (total && !joined) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Retaining reflected Source light metadata");
    if (p->q2_light_offset) memcpy(joined,p->draw_lights,p->q2_light_offset*sizeof(*joined));
    if (count) memcpy(joined+p->q2_light_offset,q2,count*sizeof(*joined));
    if (tail) memcpy(joined+p->q2_light_offset+count,
        p->draw_lights+p->q2_light_offset+p->q2_light_count,tail*sizeof(*joined));
    free(p->reflected_lights); p->reflected_lights=joined;
    input->lights=joined; input->light_count=total; return true;
}
static bool world_models(void *context,const qa_scene_world_input *input_value,qa_scene_frame *frame_value,qa_error *error)
{
    unified_presentation *p=context;
    if (!frontend_unified_q1_world_models(p->q1,input_value,frame_value,error) ||
        !frontend_unified_q2_world_models(p->q2,input_value,frame_value,error)) return false;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!row->cg_prepared ||
            !frontend_unified_q3_runtime_factory_scene_submit(row->factory,input_value,frame_value,error)) return false;
    return true;
}
static bool particles(void *context,const qa_scene_world_input *input_value,qa_scene_frame *frame_value,qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q1_world_particles(p->q1,input_value,frame_value,error) &&
        frontend_unified_q2_world_particles(p->q2,input_value,frame_value,error);
}
static bool dlights(void *context,const qa_scene_world_input *input_value,qa_scene_frame *frame_value,qa_scene_vec4 *overlay,qa_error *error)
{
    return frontend_unified_q1_world_dlights(((unified_presentation *)context)->q1,input_value,frame_value,overlay,error);
}
static bool blend(void *context,const qa_scene_world_input *input_value,qa_scene_vec4 overlay,qa_scene_frame *frame_value,qa_error *error)
{
    return frontend_unified_q1_world_blend(((unified_presentation *)context)->q1,input_value,overlay,frame_value,error);
}
static bool world_input(void *context, qa_scene_world_input *input, qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q1_world_input(p->q1,input,error) &&
        frontend_unified_q2_world_input(p->q2,input,error);
}
static bool view_origin(void *context,qa_actor_id actor,qa_vec3 origin,float fov,qa_error *error)
{
    return frontend_unified_q2_view_origin(((unified_presentation *)context)->q2,actor,origin,fov,error);
}
static bool hud(void *context, qa_ui *ui, qa_scene_rect viewport, qa_scene_frame *frame, qa_error *error)
{
    unified_presentation *p = context;
    qa_q3_presentation *recipient=NULL;
    if (!(frontend_unified_q1_hud(p->q1,ui,viewport,frame,error) &&
        frontend_unified_q2_hud(p->q2,ui,viewport,frame,error) &&
        frontend_unified_components_hud(p->components,ui,viewport,frame,error) &&
        frontend_unified_q3_hud_recipient_read(p->q3,frame,&recipient,error) &&
        frontend_unified_components_pictures(p->components,recipient,frame,error))) return false;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        bool rendered=false;
        if (!row->cg_prepared || !frontend_unified_q3_runtime_factory_hud(row->factory,&rendered,error)) return false;
    }
    return true;
}
static bool model(void *context, qa_actor_id actor, const char *content, const char *path,
    qa_scene_model_input *input, qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q1_model(p->q1,actor,content,path,input,error) &&
        frontend_unified_q2_model(p->q2,actor,content,path,input,error);
}
static bool source_model(void *context,qa_actor_id actor,uint32_t provider,const char *instance,
    bool *owned,qa_error *error)
{
    unified_presentation *p=context;
    if (!p || !instance || !provider || !owned)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Source model has no actual rendering namespace");
    *owned=false;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        frontend_unified_q3_source_view source;
        if (!q3_row_source(row,false,&source,error)) return false;
        if (source.provider->source_owner!=provider || strcmp(source.instance,instance)) continue;
        frontend_unified_q3_runtime_scene_owner receipt; bool present=false;
        if (!row->cg_prepared || !frontend_unified_q3_runtime_factory_scene_actor(row->factory,actor,&receipt,&present,error)) return false;
        if (present && (receipt.provider!=provider || !receipt.instance || strcmp(receipt.instance,instance) ||
            !qa_actor_id_equal(receipt.actor,actor)))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Source model admission changed its full actor or rendering namespace");
        *owned=present; return true;
    }
    return true;
}
static bool equipment_model(void *context,qa_actor_id actor,uint32_t provider,const char *instance,
    bool slot,bool *owned,qa_error *error)
{
    unified_presentation *p=context;
    if (!p || !instance || !provider || !owned)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"View equipment has no actual rendering namespace");
    *owned=false;
    if (slot) return true;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        frontend_unified_q3_source_view source;
        if (!q3_row_source(row,false,&source,error)) return false;
        if (source.provider->source_owner!=provider || strcmp(source.instance,instance)) continue;
        if (!row->cg_prepared) return true;
        frontend_unified_q3_runtime_scene_owner receipt; bool present=false;
        if (!frontend_unified_q3_runtime_factory_scene_view_weapon(row->factory,actor,&receipt,&present,error)) return false;
        if (present && (receipt.provider!=provider || !receipt.instance || strcmp(receipt.instance,instance) ||
            receipt.publication!=source.publication || receipt.map_revision!=source.map_revision ||
            !qa_actor_id_equal(receipt.actor,actor)))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Native viewgun admission changed its actual equipment namespace");
        *owned=present; return true;
    }
    return true;
}
static bool model_after(void *context,qa_actor_id actor,const char *content,const char *path,
    const qa_scene_model_input *input_value,qa_scene_frame *frame_value,qa_error *error)
{
    return frontend_unified_q2_model_after(((unified_presentation *)context)->q2,actor,content,path,
        input_value,frame_value,error);
}
static bool player_blend(void *context,qa_actor_id actor,bool present,const qa_scene_vec4 *value,
    bool damage_present,const qa_scene_vec4 *damage,qa_scene_rect viewport,qa_scene_frame *frame_value,qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q2_player_blend(p->q2,actor,present,value,damage_present,damage,viewport,frame_value,error);
}
static bool draw(void *context, frontend_remote_unified *replica, float stereo, qa_audio_listener *listener, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->video || p->replica != replica || !p->render || !p->received)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified drawing awaits its actual received frame");
    frontend_unified_prediction_view prediction;
    bool source_listener=false;
    frontend_unified_render_children children = {.context=p,.camera=camera,.status_replacement=status_replacement,.source_model=source_model,.equipment_model=equipment_model,.view_origin=view_origin,.world_input=world_input,.lights=lights,.reflected_lights=reflected_lights,
        .world=world,.reflected_world=reflected_world,.world_models=world_models,.particles=particles,.dlights=dlights,.blend=blend,
        .player_blend=player_blend,.hud=hud,.model=model,.model_after=model_after};
    bool okay=frontend_remote_unified_prediction_read(p->prediction,&prediction,error) &&
        events_enter(p,error) && q3_factories_ensure(p,error) && q3_factories_prediction_prepare(p,error) &&
        frontend_unified_render_draw(p->render,&prediction,&children,stereo,listener,error);
    if (okay) for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        if (!row->primary_view) continue;
        qa_audio_listener received; bool present=false;
        if (!frontend_unified_q3_runtime_factory_listener(row->factory,&received,&present,error)) { okay=false; break; }
        if (present) { *listener=received; source_listener=true; }
    }
    qa_error returned={0};
    if (!q3_factories_frame_end(p,okay,&returned)) {
        if (okay && error) *error=returned;
        okay=false;
    }
    if (!okay) return false;
    return source_listener || audio_actor(p,prediction.actor,&listener->actor,error);
}
static bool close(void *context, frontend_remote_unified *replica, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || (p->replica && p->replica != replica) || !close_children(p,error)) return false;
    if (!frontend_unified_media_destroy(p->candidate_media,error)) return false;
    p->candidate_media = NULL; return true;
}
static bool content_visit(void *context, const frontend_remote_unified *replica,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    unified_presentation *p = context;
    return p && checkpoint_returned(p,replica) &&
        (!p->media || frontend_unified_media_visit(p->media,visitor,error)) &&
        (!p->q2 || frontend_unified_q2_visit(p->q2,visitor,error)) &&
        (!p->q3 || frontend_unified_q3_visit(p->q3,visitor,error)) &&
        (!p->components || frontend_unified_components_visit(p->components,visitor,error)) &&
        (!p->candidate_media || frontend_unified_media_visit(p->candidate_media,visitor,error));
}
static void dispose(void *context) { free(context); }
static frontend_remote_unified_consumers consumers(unified_presentation *p)
{
    return (frontend_remote_unified_consumers){.context=p,.prepare=prepare,.offer_publish=offer_publish,
        .offer_ready=offer_ready,.control=control,.frame=frame,.publish=publish,.input=input,
        .physical_ready=physical_ready,.physical_input=physical_input,.begin_frame=begin_frame,.clock_read=clock_read,.sample=sample,.draw=draw,
        .idle=idle,.checkpoint_returned=checkpoint_returned,.close=close,.content_visit=content_visit,.dispose=dispose};
}
bool frontend_remote_unified_presentation_create(qa_frontend *frontend,
    const frontend_remote_unified_options *source, frontend_remote_unified **out, qa_error *error)
{
    if (!source || !frontend || !out || *out)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified factory needs its actual CLIENT construction tuple");
    unified_presentation *p = calloc(1,sizeof(*p));
    if (!p) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating readonly unified presentation owner");
    p->frontend = frontend;
    frontend_remote_unified_options options = *source;
    options.consumers = consumers(p);
    if (!frontend_remote_unified_create(frontend,&options,out,error)) { free(p); return false; }
    p->replica = *out; return true;
}

bool frontend_remote_unified_presentation_time(const frontend_remote_unified *replica, double *out, qa_error *error)
{
    if (!replica || replica->options.consumers.input != input)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified command clock has no actual prediction factory");
    const unified_presentation *p = replica->options.consumers.context;
    if (!p->received) return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified command clock awaits its first authoritative frame");
    return frontend_remote_unified_prediction_time(p->prediction,out,error);
}
bool frontend_remote_unified_presentation_media(const frontend_remote_unified *replica,
    frontend_unified_media **installed,frontend_unified_media **pending,qa_error *error)
{
    if (!replica || !installed || !pending || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media inventory has no actual presentation factory");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || replica->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media inventory overlaps its actual factory callback");
    if (p->video) {
        const frontend_video_guests *aggregate=frontend_video_guests_read(p->frontend);
        if (!frontend_video_guests_resources_associated(p->frontend,aggregate) ||
            !frontend_unified_q3_video_returned(p->video,aggregate,error))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media video observation lacks its exact returned resource cohort");
    }
    *installed=p->media; *pending=p->candidate_media; return true;
}
frontend_unified_q3_sources *frontend_remote_unified_presentation_q3_sources(const frontend_remote_unified *replica)
{
    if (!replica || replica->options.consumers.input!=input) return NULL;
    const unified_presentation *p=replica->options.consumers.context;
    return p && p->replica==replica ? p->q3_sources : NULL;
}
size_t frontend_remote_unified_presentation_q3_client_count(const frontend_remote_unified *replica)
{
    if (!replica || replica->options.consumers.input!=input) return 0;
    const unified_presentation *p=replica->options.consumers.context;
    size_t count=0;
    if (p && p->replica==replica) for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!row->born) ++count;
    if (p && p->replica==replica) for (const unified_q3_client_row *row=p->retired_q3;row;row=row->next) ++count;
    return count;
}
frontend_unified_q3_client *frontend_remote_unified_presentation_q3_client(
    const frontend_remote_unified *replica,size_t index)
{
    if (!replica || replica->options.consumers.input!=input) return NULL;
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica) return NULL;
    const unified_q3_client_row *row=q3_roster_at(p,index);
    return row?row->client:NULL;
}
frontend_unified_q3_runtime_factory *frontend_remote_unified_presentation_q3_factory(
    const frontend_remote_unified *replica,size_t index)
{
    if (!replica || replica->options.consumers.input!=input) return NULL;
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica) return NULL;
    const unified_q3_client_row *row=q3_roster_at(p,index);
    return row?row->factory:NULL;
}
bool frontend_remote_unified_presentation_q3_row_read(const frontend_remote_unified *replica,size_t index,
    frontend_unified_presentation_q3_row *out,qa_error *error)
{
    if (!replica || !out || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG roster has no actual Unified factory");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || replica->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG roster overlaps an actual factory callback");
    bool checkpoint=p->frontend->capture!=NULL || p->frontend->source_restoring;
    const unified_q3_client_row *row=q3_roster_at(p,index);
    if (row) {
        frontend_unified_q3_source_view source;
        if (!(row->retirement?frontend_unified_q3_source_retirement_read(row->retirement,&source,error):
            q3_row_source(row,checkpoint,&source,error))) return false;
        for (size_t i=0;i<frontend_unified_media_bank_count(p->media);++i) {
            frontend_unified_bank_view bank;
            if (!frontend_unified_media_bank_read(p->media,i,&bank)) return false;
            if (bank.files==source.files && bank.q3_assets==source.assets) {
                *out=(frontend_unified_presentation_q3_row){.client=row->client,.factory=row->factory,
                    .media=p->media,.source=source,.bank=i,.receiver=row->receiver,.audio_owner=row->audio_owner,
                    .retired=row->retirement!=NULL};
                return true;
            }
        }
    }
    return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Compiled CG roster lost its true bank or CLIENT ordinal");
}
bool frontend_remote_unified_presentation_children_read(const frontend_remote_unified *replica,
    frontend_unified_presentation_children *out,qa_error *error)
{
    if (!replica || !out || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified child inventory has no actual presentation factory");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || replica->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified child inventory overlaps its actual factory callback");
    *out=(frontend_unified_presentation_children){.media=p->media,.pending_media=p->candidate_media,
        .render=p->render,.pending_render=p->candidate_render,.prediction=p->prediction,.input=p->physical,
        .events=p->events,.q1=p->q1,.q2=p->q2,.q3=p->q3,.q3_sources=p->q3_sources,
        .components=p->components,.audio_owner=p->audio_owner};
    return true;
}
bool frontend_remote_unified_presentation_trace(const frontend_remote_unified *replica,
    const qa_trace_query *query,qa_trace_result *out,qa_error *error)
{
    if (!replica || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified trace has no actual CLIENT collision owner");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified trace awaits a returned authoritative prediction scene");
    return frontend_remote_unified_prediction_trace(p->prediction,query,out,error);
}
bool frontend_remote_unified_presentation_body(const frontend_remote_unified *replica,
    qa_actor_id actor,qa_body_state *out,qa_error *error)
{
    if (!replica || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified body has no actual CLIENT collision owner");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified body awaits a returned authoritative prediction scene");
    return frontend_remote_unified_prediction_body_read(p->prediction,actor,out,error);
}

bool frontend_remote_unified_presentation_point_contents(const frontend_remote_unified *replica,
    const qa_point_query *query,qa_point_contents *out,qa_error *error)
{
    if (!replica || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified contents has no actual CLIENT collision owner");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified contents awaits a returned authoritative prediction scene");
    return frontend_remote_unified_prediction_point_contents(p->prediction,query,out,error);
}
