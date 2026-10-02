#include "remote_unified_private.h"
#include "remote_unified_presentation.h"
#include "remote_unified_render.h"
#include "remote_unified_events.h"
#include "remote_unified_q1.h"
#include "remote_unified_q2.h"
#include "remote_unified_q3.h"

#include <stdlib.h>
#include <string.h>

typedef struct unified_audio_identity {
    struct unified_audio_identity *next;
    qa_actor_id actor;
    uint64_t audio;
} unified_audio_identity;
typedef struct unified_presentation {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media, *candidate_media;
    frontend_unified_render *render, *candidate_render;
    frontend_remote_unified_prediction *prediction;
    qa_unified_document *candidate_prediction;
    frontend_unified_events *events;
    frontend_unified_q1 *q1;
    frontend_unified_q2 *q2;
    frontend_unified_q3 *q3;
    unified_audio_identity *audio;
    qa_scene_light *draw_lights;
    uint64_t audio_owner;
    bool received, frame_prepared, busy;
} unified_presentation;

static bool audio_actor(void *context, qa_actor_id actor, uint64_t *out, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || !out || actor.registry != frontend_remote_unified_registry(p->replica))
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
static bool family(const qa_unified_document *doc, qa_json_id row, const char *kind)
{
    const qa_json_document *json = qa_unified_document_json(doc);
    return qa_json_string_equal(json, qa_json_get(json, row, "kind"), kind);
}
static bool q1_family(const qa_unified_document *doc, qa_json_id row)
{ return family(doc,row,"q1") || family(doc,row,"q1-composition") || family(doc,row,"q1-client") || family(doc,row,"q1-sky"); }
static bool q3_family(const qa_unified_document *doc, qa_json_id row)
{ return family(doc,row,"q3-source") || family(doc,row,"q3-character") || family(doc,row,"q3-ballistics"); }
static bool validate(void *context, bool simulation, const qa_unified_document *doc, qa_json_id row, qa_error *error)
{
    unified_presentation *p = context;
    const qa_json_document *json=qa_unified_document_json(doc);
    qa_json_id payload=simulation?qa_json_get(json,row,"payload"):row;
    if (!simulation && q1_family(doc,payload)) return frontend_unified_q1_validate(p->q1,false,doc,row,error);
    if (q3_family(doc,payload)) return frontend_unified_q3_validate(p->q3,simulation,doc,row,error);
    return frontend_unified_q2_validate(p->q2,simulation,doc,row,error);
}
static bool presentation(void *context, const qa_unified_document *doc, qa_json_id row, bool *mirrored, qa_error *error)
{
    unified_presentation *p = context;
    if (q1_family(doc,row)) {
        const qa_json_document *json=qa_unified_document_json(doc);
        qa_json_id event=qa_json_get(json,row,"event");
        if (qa_json_string_equal(json,qa_json_get(json,event,"kind"),"sound")) {
            bool paired=false; *mirrored=false;
            return frontend_unified_events_sound_mirrored(p->events,doc,row,&paired,error) &&
                frontend_unified_q1_sound_presentation(p->q1,doc,row,paired,error);
        }
        return frontend_unified_q1_presentation(p->q1,doc,row,mirrored,error);
    }
    if (q3_family(doc,row)) return frontend_unified_q3_presentation(p->q3,doc,row,mirrored,error);
    return frontend_unified_q2_presentation(p->q2,doc,row,mirrored,error);
}
static bool simulation(void *context, const qa_unified_document *doc, qa_json_id row, qa_error *error)
{
    unified_presentation *p = context;
    const qa_json_document *json = qa_unified_document_json(doc);
    qa_json_id payload = qa_json_get(json,row,"payload");
    if (q3_family(doc,payload)) return frontend_unified_q3_simulation(p->q3,doc,row,error);
    return frontend_unified_q2_simulation(p->q2,doc,row,error);
}
static bool idle(void *context, const frontend_remote_unified *replica)
{
    unified_presentation *p = context;
    return p && (!p->replica || p->replica == replica) && !p->busy &&
        frontend_unified_render_idle(p->render) && frontend_unified_render_idle(p->candidate_render) &&
        (!p->prediction || frontend_remote_unified_prediction_idle(p->prediction)) &&
        (!p->events || frontend_unified_events_idle(p->events)) &&
        (!p->q1 || frontend_unified_q1_idle(p->q1)) &&
        (!p->q2 || frontend_unified_q2_idle(p->q2)) &&
        (!p->q3 || frontend_unified_q3_idle(p->q3));
}
static void frame_abort(unified_presentation *p)
{
    frontend_unified_events_frame_abort(p->events);
    frontend_unified_q1_frame_abort(p->q1);
    frontend_unified_q2_frame_abort(p->q2);
    frontend_unified_q3_frame_abort(p->q3);
    p->frame_prepared = false;
}
static bool close_children(unified_presentation *p, qa_error *error)
{
    frame_abort(p);
    if (!idle(p,p->replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified CLIENT children have not returned");
    if (!frontend_unified_q1_destroy(&p->q1,error) ||
        !frontend_unified_q2_destroy(&p->q2,error) ||
        !frontend_unified_q3_destroy(&p->q3,error) ||
        !frontend_unified_events_destroy(&p->events,error) ||
        !frontend_unified_render_destroy(&p->candidate_render,error) ||
        !frontend_unified_render_destroy(&p->render,error) ||
        !frontend_remote_unified_prediction_destroy(&p->prediction,error)) return false;
    qa_unified_document_destroy(p->candidate_prediction); p->candidate_prediction = NULL;
    if (!frontend_unified_media_destroy(p->media,error)) return false;
    p->media = NULL; p->received = false;
    free(p->draw_lights); p->draw_lights=NULL;
    while (p->audio) { unified_audio_identity *row = p->audio; p->audio = row->next; free(row); }
    p->audio_owner = 0; return true;
}
static bool prepare(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe,
    bool *ready, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || !ready || p->busy || (p->replica && p->replica != replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media preparation changed its physical CLIENT");
    p->replica = replica;
    if (!p->candidate_media && !frontend_unified_media_create(p->frontend,recipe,&p->candidate_media,error)) return false;
    *ready = true; return true;
}
static bool offer_publish(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->candidate_media || !recipe ||
        !frontend_unified_media_current(p->candidate_media) || !close_children(p,error)) return false;
    p->media = p->candidate_media; p->candidate_media = NULL; return true;
}
static bool offer_ready(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->media || recipe != frontend_remote_unified_recipe(replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified child construction precedes actual recipe publication");
    if (!p->audio_owner && !frontend_source_identity_allocate(p->frontend,&p->audio_owner,error)) return false;
    frontend_unified_event_options options = {.audio_owner=p->audio_owner,.context=p,
        .validate=validate,.presentation=presentation,.simulation=simulation,.audio_actor=audio_actor};
    if (!p->events && !frontend_unified_events_create(p->frontend,replica,p->media,&options,&p->events,error)) return false;
    frontend_unified_q1_options q1 = {.audio_owner=p->audio_owner,.context=p,.audio_actor=audio_actor};
    if (!p->q1 && !frontend_unified_q1_create(p->frontend,replica,p->media,&q1,&p->q1,error)) return false;
    if (!p->q2 && !frontend_unified_q2_create(p->frontend,replica,p->media,p->events,&p->q2,error)) return false;
    if (!p->q3 && !frontend_unified_q3_create(p->frontend,replica,p->media,&p->q3,error)) return false;
    return frontend_unified_q3_audio(p->q3,p->audio_owner,p,audio_actor,error) &&
        frontend_unified_q3_events(p->q3,p->events,error);
}
static bool control(void *context, frontend_remote_unified *replica, const qa_unified_document *doc, qa_error *error)
{
    unified_presentation *p = context;
    const qa_json_document *json = qa_unified_document_json(doc);
    qa_json_id value = qa_json_get(json,qa_unified_document_root(doc),"value");
    qa_json_id kind = qa_json_get(json,value,"kind");
    if (!p || p->replica != replica || !p->events)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified control has no prepared CLIENT consumers");
    if (qa_json_string_equal(json,kind,"components") || qa_json_string_equal(json,kind,"component-state") ||
        qa_json_string_equal(json,kind,"component-gamestate") || qa_json_string_equal(json,kind,"component-command"))
        return frontend_unified_fail(error, QA_ERROR_UNSUPPORTED, "Unified component control needs its actual component CLIENT");
    return frontend_unified_events_control(p->events,doc,error);
}
static bool frame(void *context, frontend_remote_unified *replica, const qa_unified_document *doc,
    const qa_unified_document *prediction, bool *ready, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !ready || !p->events || !p->q1 || !p->q2 || !p->q3)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame has incomplete actual CLIENT children");
    if (p->frame_prepared) { *ready = true; return true; }
    const qa_json_document *json = qa_unified_document_json(doc);
    qa_json_id payload = qa_json_get(json,qa_unified_document_root(doc),"components");
    if (payload != QA_JSON_NONE)
        return frontend_unified_fail(error, QA_ERROR_UNSUPPORTED, "Unified received component frame has no installed component CLIENT");
    if (!p->prediction && !frontend_remote_unified_prediction_create(replica,&p->prediction,error)) return false;
    if (!p->candidate_render && !frontend_unified_render_create(p->frontend,replica,p->media,doc,&p->candidate_render,error)) return false;
    if (!p->candidate_prediction && !frontend_unified_clone(prediction,&p->candidate_prediction,error)) return false;
    bool okay = frontend_unified_events_frame_prepare(p->events,doc,error) &&
        frontend_unified_q1_frame_prepare(p->q1,doc,error) && frontend_unified_q2_frame_prepare(p->q2,doc,error) &&
        frontend_unified_q3_frame_prepare(p->q3,doc,error);
    if (!okay) { frame_abort(p); return false; }
    p->frame_prepared = true; *ready = true; return true;
}
static bool publish(void *context, frontend_remote_unified *replica, const qa_unified_document *doc, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->frame_prepared || !p->candidate_render ||
        !p->candidate_prediction || p->busy || !frontend_unified_render_idle(p->render) ||
        !frontend_unified_render_idle(p->candidate_render) ||
        !frontend_remote_unified_prediction_idle(p->prediction) ||
        !frontend_unified_events_frame_ready(p->events,doc,error) ||
        !frontend_unified_q1_frame_ready(p->q1,doc,error) ||
        !frontend_unified_q2_frame_ready(p->q2,doc,error) ||
        !frontend_unified_q3_frame_ready(p->q3,doc,error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame publication lost its prepared children");
    /* This is the last fallible adoption. Metadata was installed by the replica
     * and will be restored there if prediction refuses this frame. */
    if (!frontend_remote_unified_prediction_receive(p->prediction,p->candidate_prediction,error)) return false;
    frontend_unified_q1_frame_commit(p->q1); frontend_unified_q2_frame_commit(p->q2);
    frontend_unified_q3_frame_commit(p->q3); frontend_unified_events_frame_commit(p->events);
    /* Retirement was qualified above; no callback or allocation separates that
     * check from this owned HUD/frame disposal. */
    if (!frontend_unified_render_destroy(&p->render,error)) return false;
    p->render = p->candidate_render; p->candidate_render = NULL;
    qa_unified_document_destroy(p->candidate_prediction); p->candidate_prediction = NULL;
    p->frame_prepared = false; p->received = true; return true;
}
static bool input(void *context, frontend_remote_unified *replica, const qa_unified_input *command,
    double time, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->received)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified input awaits its authoritative prediction frame");
    return frontend_remote_unified_prediction_input(p->prediction,command,time,error);
}
static bool sample(void *context, frontend_remote_unified *replica, uint64_t now, qa_error *error)
{
    unified_presentation *p = context; (void)now;
    return p && p->replica == replica && (!p->events || frontend_unified_events_enter(p->events,error));
}
static bool world(void *context, const qa_scene_view *view, const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    unified_presentation *p = context;
    return frontend_unified_q1_world(p->q1,view,input,frame,error) &&
        frontend_unified_q2_world(p->q2,view,input,frame,error) && frontend_unified_q3_world(p->q3,view,input,frame,error) &&
        frontend_unified_events_draw(p->events,view,frame,error);
}
static bool lights(void *context, const qa_scene_view *view, const qa_scene_world_input *input,
    const qa_scene_light **out, size_t *count, qa_error *error)
{
    unified_presentation *p = context;
    const qa_scene_light *q2 = NULL; size_t n = 0;
    if (!frontend_unified_q2_lights(p->q2,view,input,&q2,&n,error)) return false;
    if (!n) { *out=input->lights; *count=input->light_count; return true; }
    if (n > SIZE_MAX/sizeof(*q2)-input->light_count)
        return frontend_unified_fail(error,QA_ERROR_MEMORY,"Unified light pool exceeds actual draw storage");
    size_t total=n+input->light_count;
    qa_scene_light *joined=malloc(total*sizeof(*joined));
    if (!joined) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Joining received family light pools");
    if (input->light_count) memcpy(joined,input->lights,input->light_count*sizeof(*joined));
    memcpy(joined+input->light_count,q2,n*sizeof(*joined));
    free(p->draw_lights); p->draw_lights=joined; *out=joined; *count=total; return true;
}
static bool world_input(void *context, qa_scene_world_input *input, qa_error *error)
{ return frontend_unified_q1_world_input(((unified_presentation *)context)->q1,input,error); }
static bool hud(void *context, qa_ui *ui, qa_scene_rect viewport, qa_scene_frame *frame, qa_error *error)
{
    unified_presentation *p = context;
    return frontend_unified_q1_hud(p->q1,ui,viewport,frame,error) &&
        frontend_unified_q2_hud(p->q2,ui,viewport,frame,error) && frontend_unified_q3_hud(p->q3,ui,viewport,frame,error);
}
static bool model(void *context, qa_actor_id actor, const char *content, const char *path,
    qa_scene_model_input *input, qa_error *error)
{ return frontend_unified_q1_model(((unified_presentation *)context)->q1,actor,content,path,input,error); }
static bool draw(void *context, frontend_remote_unified *replica, float stereo, qa_audio_listener *listener, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->render || !p->received)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified drawing awaits its actual received frame");
    frontend_unified_prediction_view prediction;
    frontend_unified_render_children children = {.context=p,.world_input=world_input,.lights=lights,.world=world,.hud=hud,.model=model};
    if (!frontend_remote_unified_prediction_read(p->prediction,&prediction,error) ||
        !frontend_unified_events_enter(p->events,error) ||
        !frontend_unified_render_draw(p->render,&prediction,&children,stereo,listener,error)) return false;
    return audio_actor(p,prediction.actor,&listener->actor,error);
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
    return p && idle(p,replica) && (!p->media || frontend_unified_media_visit(p->media,visitor,error)) &&
        (!p->candidate_media || frontend_unified_media_visit(p->candidate_media,visitor,error));
}
static void dispose(void *context) { free(context); }
bool frontend_remote_unified_presentation_create(qa_frontend *frontend,
    const frontend_remote_unified_options *source, frontend_remote_unified **out, qa_error *error)
{
    if (!source || !frontend || !out || *out)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified factory needs its actual CLIENT construction tuple");
    unified_presentation *p = calloc(1,sizeof(*p));
    if (!p) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating readonly unified presentation owner");
    p->frontend = frontend;
    frontend_remote_unified_options options = *source;
    options.consumers = (frontend_remote_unified_consumers){.context=p,.prepare=prepare,.offer_publish=offer_publish,
        .offer_ready=offer_ready,.control=control,.frame=frame,.publish=publish,.input=input,.sample=sample,.draw=draw,
        .idle=idle,.close=close,.content_visit=content_visit,.dispose=dispose};
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
