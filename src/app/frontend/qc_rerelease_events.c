#include "internal.h"
#include "qc_rerelease_events.h"
#include "qa/qc.h"

typedef struct qc_text_source {
    struct qc_text_source *next;
    qa_actor_owner provider;
    qa_font_world_store *texts;
} qc_text_source;
struct frontend_qc_rerelease { qc_text_source *sources; bool handling; };
bool frontend_qc_rerelease_idle(const qa_frontend *f)
{ return f && (!f->qc_rerelease || !f->qc_rerelease->handling); }

static bool source_get(qa_frontend *f,qa_actor_owner provider,qc_text_source **out,qa_error *error)
{
    if (!provider || !qa_application_provider_instance(f->application,provider)) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug source has no actual installed provider");
        return false;
    }
    if (!f->qc_rerelease) {
        f->qc_rerelease=calloc(1,sizeof(*f->qc_rerelease));
        if (!f->qc_rerelease) {
            frontend_fail(error,QA_ERROR_MEMORY,"Allocating QC debug continuation");
            return false;
        }
    }
    qc_text_source **link=&f->qc_rerelease->sources;
    while (*link && (*link)->provider!=provider) link=&(*link)->next;
    if (!*link) {
        qc_text_source *source=calloc(1,sizeof(*source));
        if (!source) {
            frontend_fail(error,QA_ERROR_MEMORY,"Allocating QC debug source");
            return false;
        }
        source->texts=qa_font_world_store_create(error);
        if (!source->texts) { free(source); return false; }
        source->provider=provider; *link=source;
    }
    *out=*link; return true;
}
static void source_free(qc_text_source *source)
{ qa_font_world_store_destroy(source->texts); free(source); }
void frontend_qc_rerelease_destroy(qa_frontend *f)
{
    if (!f || !f->qc_rerelease) return;
    while (f->qc_rerelease->sources) {
        qc_text_source *source=f->qc_rerelease->sources;
        f->qc_rerelease->sources=source->next; source_free(source);
    }
    free(f->qc_rerelease); f->qc_rerelease=NULL;
}
void frontend_qc_rerelease_retire_world(qa_frontend *f)
{ frontend_qc_rerelease_destroy(f); }
static bool event_body(qa_frontend *f,const qa_builtin_event *event,qa_error *error)
{
    if (event->argument_count!=3 || !event->arguments)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug event lacks its actual typed tuple");
    double values[3];
    for (unsigned i=0;i<3;++i) {
        if (event->arguments[i].kind!=QA_BUILTIN_MESSAGE_NUMBER || !isfinite(event->arguments[i].value.number))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug event has a nonnumeric source argument");
        values[i]=event->arguments[i].value.number;
    }
    if (values[0]<0 || values[0]>255 || trunc(values[0])!=values[0] || values[1]<0 ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->end) || !qa_vec_finite(event->direction) ||
        !isfinite(event->value) || !isfinite(event->volume) || !isfinite(event->attenuation))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug geometry leaves source numeric bounds");
    qc_text_source *source;
    if (!source_get(f,event->provider,&source,error)) return false;
    bool depth=values[2]!=0;
    const char *string=qa_strings_cstr(qa_session_strings(qa_application_session(f->application)),event->text);
    if (!string) return frontend_fail(error,QA_ERROR_ARGUMENT,"QC world text has no actual source string");
    qa_font_world_text text={.text={(const uint8_t *)string,strlen(string)},.origin=event->origin,
        .color={1,1,1,1},.cell_size=event->value,.orientation=QA_FONT_WORLD_BILLBOARD,
        .font=QA_FONT_WORLD_SELECTED,.depth_test=depth};
    return qa_font_world_store_submit(source->texts,&text,(double)event->time_ns/1e9,values[1],error);
}
bool frontend_qc_rerelease_event(qa_frontend *f,const qa_builtin_event *event,bool *handled,qa_error *error)
{
    if (!f || !f->application || !event || !handled)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC event needs its actual frontend and result");
    *handled=false;
    const char *name=qa_strings_cstr(qa_session_strings(qa_application_session(f->application)),event->resource);
    if (event->family!=QA_GAME_Q1 || event->kind!=QA_BUILTIN_EFFECT ||
        event->code!=QA_QC_BUILTIN_EX_DRAW_WORLDTEXT || !name || strcmp(name,"q1:rerelease-debug")) return true;
    *handled=true;
    if (f->source_restoring || (f->qc_rerelease && f->qc_rerelease->handling))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug consumer is restoring or reentered");
    if (!f->qc_rerelease) {
        f->qc_rerelease=calloc(1,sizeof(*f->qc_rerelease));
        if (!f->qc_rerelease) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating QC debug event owner");
    }
    f->qc_rerelease->handling=true;
    bool ok=event_body(f,event,error);
    f->qc_rerelease->handling=false; return ok;
}
bool frontend_qc_rerelease_draw(qa_frontend *f,uint32_t seat,const qa_scene_view *view,qa_error *error)
{
    if (!f || !view || seat>=f->options.seats || view->seat!=seat || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug draw requires its actual seat and view");
    if (!f->qc_rerelease || f->options.dedicated) return true;
    for (qc_text_source *source=f->qc_rerelease->sources;source;source=source->next) {
        qa_clock_state clock;
        if (!qa_session_clock(qa_application_session(f->application),source->provider,&clock))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug source lost its actual clock owner");
        qa_font_world_snapshot texts;
        if (!qa_font_world_store_snapshot(source->texts,(double)clock.frame.time_ns/1e9,f->frame_number,&f->frame.storage,&texts,error) ||
            (texts.count && !qa_font_world_draw(&f->frame,view,&texts,&f->seats[seat].fonts,0,false,error))) return false;
    }
    return true;
}
