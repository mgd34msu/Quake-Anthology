#include "internal.h"
#include "qc_rerelease_events.h"
#include "qa/qc.h"

typedef struct qc_debug_line {
    qa_debug_line value;
    uint64_t expires,first_frame;
    bool instant,observed;
} qc_debug_line;
typedef struct qc_debug_source {
    struct qc_debug_source *next;
    qa_actor_owner provider;
    qc_debug_line *lines;
    size_t count,capacity;
    qa_font_world_store *texts;
} qc_debug_source;
struct frontend_qc_rerelease { qc_debug_source *sources; bool handling; };
bool frontend_qc_rerelease_idle(const qa_frontend *f)
{ return f && (!f->qc_rerelease || !f->qc_rerelease->handling); }

static bool source_get(qa_frontend *f,qa_actor_owner provider,qc_debug_source **out,qa_error *error)
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
    qc_debug_source **link=&f->qc_rerelease->sources;
    while (*link && (*link)->provider!=provider) link=&(*link)->next;
    if (!*link) {
        qc_debug_source *source=calloc(1,sizeof(*source));
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
static void source_free(qc_debug_source *source)
{ qa_font_world_store_destroy(source->texts); free(source->lines); free(source); }
void frontend_qc_rerelease_destroy(qa_frontend *f)
{
    if (!f || !f->qc_rerelease) return;
    while (f->qc_rerelease->sources) {
        qc_debug_source *source=f->qc_rerelease->sources;
        f->qc_rerelease->sources=source->next; source_free(source);
    }
    free(f->qc_rerelease); f->qc_rerelease=NULL;
}
void frontend_qc_rerelease_retire_world(qa_frontend *f)
{ frontend_qc_rerelease_destroy(f); }
static void prune(qc_debug_source *source,uint64_t now,uint64_t frame)
{
    size_t kept=0;
    for (size_t i=0;i<source->count;++i) {
        qc_debug_line line=source->lines[i];
        if (line.instant ? line.observed && line.first_frame!=frame : line.expires<=now) continue;
        source->lines[kept++]=line;
    }
    source->count=kept;
}
static bool append(qc_debug_source *source,const qa_debug_line *lines,size_t count,
    uint64_t now,double lifetime,uint64_t frame,qa_error *error)
{
    long double duration=(long double)lifetime*1000000000.L;
    if (duration>=18446744073709551616.L || duration>(long double)(UINT64_MAX-now))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug lifetime exceeds source clock");
    prune(source,now,frame);
    if (count>SIZE_MAX-source->count || source->count+count>SIZE_MAX/sizeof(*source->lines))
        return frontend_fail(error,QA_ERROR_MEMORY,"QC debug line count exceeds storage");
    size_t required=source->count+count;
    if (required>source->capacity) {
        size_t capacity=source->capacity?source->capacity:64;
        while (capacity<required) {
            if (capacity>SIZE_MAX/2/sizeof(*source->lines)) { capacity=required; break; }
            capacity*=2;
        }
        qc_debug_line *grown=realloc(source->lines,capacity*sizeof(*grown));
        if (!grown) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual QC debug lines");
        source->lines=grown; source->capacity=capacity;
    }
    uint64_t expires=lifetime>0?now+(uint64_t)duration:0;
    for (size_t i=0;i<count;++i)
        source->lines[source->count++]=(qc_debug_line){.value=lines[i],.expires=expires,.instant=lifetime==0};
    return true;
}
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
    qc_debug_source *source;
    if (!source_get(f,event->provider,&source,error)) return false;
    bool depth=values[2]!=0;
    if (event->code==QA_QC_BUILTIN_EX_DRAW_WORLDTEXT) {
        const char *string=qa_strings_cstr(qa_session_strings(qa_application_session(f->application)),event->text);
        if (!string) return frontend_fail(error,QA_ERROR_ARGUMENT,"QC world text has no actual source string");
        qa_font_world_text text={.text={(const uint8_t *)string,strlen(string)},.origin=event->origin,
            .color={1,1,1,1},.cell_size=event->value,.orientation=QA_FONT_WORLD_BILLBOARD,
            .font=QA_FONT_WORLD_SELECTED,.depth_test=depth};
        return qa_font_world_store_submit(source->texts,&text,(double)event->time_ns/1e9,values[1],error);
    }
    qa_scene_resources *images; qa_audio_bank *sounds; qa_bytes palette;
    if (!frontend_event_qc_resources(f,event->provider,&images,&sounds,error) ||
        !qa_scene_resources_palette(images,QA_SCENE_Q1,&palette,error)) return false;
    (void)sounds;
    unsigned color=(unsigned)values[0]*3;
    if (palette.size<color+3) return frontend_fail(error,QA_ERROR_FORMAT,"QC debug palette lacks its actual color");
    qa_scene_vec4 rgba={palette.data[color]/255.f,palette.data[color+1]/255.f,palette.data[color+2]/255.f,1};
    qa_debug_shape shape={0};
    switch (event->code) {
    case QA_QC_BUILTIN_EX_DRAW_POINT: shape.kind=QA_DEBUG_POINT; shape.data.point.origin=event->origin; shape.data.point.size=event->value; break;
    case QA_QC_BUILTIN_EX_DRAW_LINE: shape.kind=QA_DEBUG_LINE; shape.data.line.start=event->origin; shape.data.line.end=event->end; break;
    case QA_QC_BUILTIN_EX_DRAW_ARROW: shape.kind=QA_DEBUG_ARROW; shape.data.arrow.start=event->origin; shape.data.arrow.end=event->end; shape.data.arrow.size=event->value; shape.data.arrow.cap_color=rgba; break;
    case QA_QC_BUILTIN_EX_DRAW_RAY: shape.kind=QA_DEBUG_RAY; shape.data.ray.origin=event->origin; shape.data.ray.direction=event->direction; shape.data.ray.length=event->volume; shape.data.ray.size=event->value; break;
    case QA_QC_BUILTIN_EX_DRAW_CIRCLE: shape.kind=QA_DEBUG_CIRCLE; shape.data.round.origin=event->origin; shape.data.round.radius=event->volume; break;
    case QA_QC_BUILTIN_EX_DRAW_BOUNDS: shape.kind=QA_DEBUG_BOUNDS; shape.data.bounds=(qa_bounds){event->origin,event->end}; break;
    case QA_QC_BUILTIN_EX_DRAW_SPHERE: shape.kind=QA_DEBUG_SPHERE; shape.data.round.origin=event->origin; shape.data.round.radius=event->volume; break;
    case QA_QC_BUILTIN_EX_DRAW_CYLINDER: shape.kind=QA_DEBUG_CYLINDER; shape.data.cylinder.origin=event->origin; shape.data.cylinder.radius=event->volume; shape.data.cylinder.half_height=event->attenuation; break;
    default: return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug event has an unknown actual source shape");
    }
    qa_arena scratch; qa_arena_init(&scratch,16384); const qa_debug_line *lines; size_t count;
    bool ok=qa_debug_shape_lines(&shape,rgba,depth,&scratch,&lines,&count,error) &&
        append(source,lines,count,event->time_ns,values[1],f->frame_number,error);
    qa_arena_destroy(&scratch); return ok;
}
bool frontend_qc_rerelease_event(qa_frontend *f,const qa_builtin_event *event,bool *handled,qa_error *error)
{
    if (!f || !f->application || !event || !handled)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC event needs its actual frontend and result");
    *handled=false;
    const char *name=qa_strings_cstr(qa_session_strings(qa_application_session(f->application)),event->resource);
    if (event->family!=QA_GAME_Q1 || event->kind!=QA_BUILTIN_EFFECT || !name || strcmp(name,"q1:rerelease-debug")) return true;
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
    for (qc_debug_source *source=f->qc_rerelease->sources;source;source=source->next) {
        qa_clock_state clock;
        if (!qa_session_clock(qa_application_session(f->application),source->provider,&clock))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"QC debug source lost its actual clock owner");
        prune(source,clock.frame.time_ns,f->frame_number);
        qa_debug_line *lines=source->count?qa_arena_alloc(&f->frame.storage,source->count*sizeof(*lines),_Alignof(qa_debug_line),error):NULL;
        if (source->count && !lines) return false;
        for (size_t i=0;i<source->count;++i) {
            qc_debug_line *line=source->lines+i;
            if (line->instant && !line->observed) { line->observed=true; line->first_frame=f->frame_number; }
            lines[i]=line->value;
        }
        if (source->count) {
            qa_scene_resources *images; qa_audio_bank *sounds;
            if (!frontend_event_qc_resources(f,source->provider,&images,&sounds,error) ||
                !qa_debug_draw(&f->frame,view,qa_scene_white(images),lines,source->count,1,error)) return false;
        }
        qa_font_world_snapshot texts;
        if (!qa_font_world_store_snapshot(source->texts,(double)clock.frame.time_ns/1e9,f->frame_number,&f->frame.storage,&texts,error) ||
            (texts.count && !qa_font_world_draw(&f->frame,view,&texts,&f->seats[seat].fonts,0,false,error))) return false;
    }
    return true;
}
