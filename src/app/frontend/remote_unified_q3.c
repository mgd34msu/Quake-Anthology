#include "remote_unified_private.h"
#include "remote_unified_q3.h"
#include "remote_unified_events.h"
#include "../../presentation/q3_native/events.h"
#include "../../presentation/q3_native/marks.h"
#include "../../presentation/q3_native/pose.h"
#include "../../presentation/q3_native/attachments.h"
#include "../../presentation/q3_native/selected_media.h"
#include "qa/scene_marks.h"
#include "qa/q3_assets_save.h"
#include "qa/source_save.h"
#include <float.h>
#include <math.h>

typedef struct unified_q3_bank {
    struct unified_q3_bank *next;
    struct frontend_unified_q3 *owner;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_actor_owner provider;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *backend;
    q3n_media *media;
    q3n_events *effects;
    q3n_unified_effect_source source;
    bool loaded;
} unified_q3_bank;
typedef struct unified_q3_character {
    struct unified_q3_character *next;
    qa_actor_id actor;
    unified_q3_bank *bank;
    q3n_player_pose pose;
    qa_player_animation_config animation;
    qa_resource *animation_holder;
    int32_t models[3], skins[3];
    qa_vec3 origin, angles, velocity;
    int32_t movement, legs, torso, time;
    uint32_t flags, powerups;
    float color[4], scale, opacity;
    bool visible, reset;
} unified_q3_character;
struct frontend_unified_q3 {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_events *events;
    unified_q3_bank *banks;
    unified_q3_character *characters;
    qa_unified_document *frame, *candidate;
    const qa_unified_document *candidate_input;
    uint64_t audio_owner;
    void *audio_context;
    bool (*audio_actor)(void *, qa_actor_id, uint64_t *, qa_error *);
    uint32_t epoch;
    int32_t time, previous_time, candidate_time;
    qa_actor_id entered_actor;
    const qa_unified_document *entered_frame;
    const qa_scene_view *view;
    const qa_scene_world_input *world;
    qa_scene_frame *scene;
    char *configstrings[1024];
    bool busy, prepared, has_frame;
};
static qa_json_id field(const qa_json_document *j, qa_json_id row, const char *name)
{ return qa_json_get(j, row, name); }
static bool number(const qa_unified_document *d, qa_json_id row, double *n, qa_error *e)
{ return (qa_unified_document_number(d, row, n, e) && isfinite(*n)) ||
    frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 scalar is not finite"); }
static bool real(const qa_unified_document *d, qa_json_id row, float *out, qa_error *e)
{
    double n; if (!number(d, row, &n, e)) return false;
    if (fabs(n) > FLT_MAX) return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 scalar exceeds float storage");
    *out = (float)n; return true;
}
static bool integer(const qa_unified_document *d, qa_json_id row, int32_t *out, qa_error *e)
{
    double n; if (!number(d, row, &n, e)) return false;
    if (n < INT32_MIN || n > INT32_MAX || trunc(n) != n)
        return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 integer exceeds its Source domain");
    *out = (int32_t)n; return true;
}
static bool vector(const qa_unified_document *d, qa_json_id row, qa_vec3 *v, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d);
    return real(d, field(j,row,"x"), &v->x,e) && real(d, field(j,row,"y"), &v->y,e) &&
        real(d, field(j,row,"z"), &v->z,e);
}
static bool text(const qa_unified_document *d, qa_json_id row, qa_buffer *b, qa_error *e)
{
    if (!qa_json_string(qa_unified_document_json(d), row, b, e)) return false;
    if (!memchr(b->data, 0, b->size)) return true;
    qa_buffer_free(b); return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 text contains NUL");
}
static bool actor(frontend_unified_q3 *o, const qa_unified_document *d, qa_json_id row, qa_actor_id *id, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d); uint64_t slot, generation;
    if (qa_json_type(j,row) == QA_JSON_NULL) { *id = (qa_actor_id){0}; return true; }
    return qa_json_u64(j,field(j,row,"slot"),&slot,e) && slot <= UINT32_MAX &&
        qa_json_u64(j,field(j,row,"generation"),&generation,e) &&
        frontend_remote_unified_actor(o->replica,(uint32_t)slot,generation,id,e);
}
bool frontend_unified_q3_current(const frontend_unified_q3 *o)
{ return o && o->epoch == frontend_remote_unified_epoch(o->replica) &&
    frontend_unified_media_current(o->media) && frontend_remote_unified_current(o->replica,NULL); }
static bool current(frontend_unified_q3 *o, qa_error *e)
{ return frontend_unified_q3_current(o) || frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q3 CLIENT owner changed"); }
static bool effect_current(const q3n_unified_effect_source *s)
{
    unified_q3_bank *b = s ? s->context : NULL;
    frontend_unified_q3 *o = b ? b->owner : NULL;
    return o && o->busy && o->entered_frame == frontend_remote_unified_frame(o->replica) &&
        frontend_unified_q3_current(o) && s == &b->source && s->assets == b->assets &&
        s->content == b->files && s->provider == b->provider &&
        (!s->actor.registry || qa_actors_get(frontend_remote_unified_registry(o->replica),s->actor));
}
static double bank_clock(void *context)
{ unified_q3_bank *b = context; return b->source.time; }
static bool trace(void *context, const q3n_frame *f, qa_vec3 start, qa_vec3 end,
    qa_bounds bounds, int32_t skip, uint32_t mask, qa_trace_result *out, qa_error *e)
{
    unified_q3_bank *b = context; (void)skip;
    if (f->unified_effects != &b->source || !effect_current(&b->source))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q3 trace lost its entered CLIENT");
    qa_trace_query q = {.start=start,.end=end,.shape={.kind=QA_SHAPE_BOX,.bounds=bounds},
        .policy={.family=QA_COLLISION_Q3,.contents_mask=mask,.curves=true},.pass_actor=b->source.actor};
    qa_collision_geometry *g = (qa_collision_geometry *)frontend_remote_unified_geometry(b->owner->replica);
    return g && qa_collision_trace(g,&q,out,e) && effect_current(&b->source);
}
static bool contents(void *context, const q3n_frame *f, qa_vec3 point, int32_t pass, uint32_t *out, qa_error *e)
{
    unified_q3_bank *b = context; (void)pass;
    if (f->unified_effects != &b->source || !effect_current(&b->source)) return false;
    qa_point_contents result; qa_point_query q = {.point=point,
        .policy={.family=QA_COLLISION_Q3,.curves=true},.pass_actor=b->source.actor};
    qa_collision_geometry *g = (qa_collision_geometry *)frontend_remote_unified_geometry(b->owner->replica);
    if (!g || !qa_collision_point_contents(g,&q,&result,e) || !effect_current(&b->source)) return false;
    *out=(uint32_t)result.contents; return true;
}
static bool fragments(void *context, const q3n_frame *f, const qa_vec3 *points, size_t count,
    qa_vec3 projection, qa_vec3 *output, size_t capacity, q3n_mark_fragment *out,
    size_t fragment_capacity, size_t *returned, qa_error *e)
{
    unified_q3_bank *b = context;
    if (f->unified_effects != &b->source || !effect_current(&b->source)) return false;
    qa_scene_mark_fragment *rows = fragment_capacity ? calloc(fragment_capacity,sizeof(*rows)) : NULL;
    if (fragment_capacity && !rows) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Preparing real Q3 mark fragments");
    qa_scene_mark_result result={0}; qa_scene_world *w=frontend_unified_media_world(b->owner->media);
    bool okay = w && qa_scene_world_mark_fragments(w,points,count,projection,output,capacity,
        rows,fragment_capacity,&result,e) && effect_current(&b->source);
    for (size_t i=0; okay && i<result.fragment_count; ++i) {
        okay=rows[i].first_point<=UINT32_MAX && rows[i].point_count<=UINT32_MAX;
        if (okay) out[i]=(q3n_mark_fragment){(uint32_t)rows[i].first_point,(uint32_t)rows[i].point_count};
    }
    free(rows); if (okay) *returned=result.fragment_count; return okay;
}
static qa_actor_owner provider(frontend_unified_q3 *o, const char *content)
{
    qa_executable_recipe *r=frontend_remote_unified_recipe(o->replica);
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    qa_actor_owner found=0;
    for (size_t i=0; i<qa_executable_recipe_provider_count(r); ++i) {
        const qa_recipe_provider *p=qa_executable_recipe_provider(r,i);
        const qa_product *product=qa_catalog_product(d->catalog,p->selection.product);
        if (product && !strcmp(product->identity,content)) {
            if (found && found!=p->source_owner) return 0;
            found=p->source_owner;
        }
    }
    return found;
}
static bool bank_read(frontend_unified_q3 *o, const char *content, unified_q3_bank **out, qa_error *e)
{
    for (unified_q3_bank *b=o->banks; b; b=b->next) if (!strcmp(b->content,content)) { *out=b; return true; }
    unified_q3_bank *b=calloc(1,sizeof(*b));
    if (!b) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining real Q3 CLIENT resource namespace");
    b->owner=o; b->content=strdup(content); b->provider=provider(o,content);
    bool okay=b->content && b->provider && frontend_unified_media_files(o->media,content,&b->files,&b->product,e) &&
        b->product->family==QA_GAME_Q3 && frontend_unified_media_q3_assets(o->media,content,&b->assets,e);
    qa_q3_product product=okay && b->product->campaign && !strcmp(b->product->campaign,"missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    if (okay) {
        q3n_event_options events={.product=product,.assets=b->assets,.context=b,
            .trace=trace,.point_contents=contents,.mark_fragments=fragments};
        q3n_media_options media={.product=product,.assets=b->assets};
        okay=q3n_media_create(&media,&b->media,e) && q3n_events_create_effects(&events,&b->effects,e);
    }
    if (!okay) { q3n_events_destroy(b->effects); q3n_media_destroy(b->media); free(b->content); free(b); return false; }
    b->source=(q3n_unified_effect_source){.context=b,.provider=b->provider,.content=b->files,
        .assets=b->assets,.product=product,.current=effect_current};
    b->next=o->banks; o->banks=b; *out=b; return true;
}
static bool frame_read(frontend_unified_q3 *o, const qa_unified_document *d, int32_t *time, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id root=qa_unified_document_root(d);
    qa_json_id snap=field(j,field(j,root,"output"),"snapshot"), t=field(j,field(j,snap,"frame"),"time");
    uint64_t epoch; double n;
    if (!qa_json_u64(j,field(j,root,"epoch"),&epoch,e) || epoch!=o->epoch || !number(d,field(j,t,"value"),&n,e)) return false;
    if (qa_json_string_equal(j,field(j,t,"kind"),"seconds")) n*=1000;
    else if (!qa_json_string_equal(j,field(j,t,"kind"),"milliseconds"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q3 clock has no Source time domain");
    double reduced=fmod(trunc(n),4294967296.0); if (reduced<0) reduced+=4294967296.0;
    uint32_t bits=(uint32_t)reduced; memcpy(time,&bits,sizeof(bits)); return true;
}
bool frontend_unified_q3_create(qa_frontend *f, frontend_remote_unified *r,
    frontend_unified_media *m, frontend_unified_q3 **out, qa_error *e)
{
    if (!f || !r || !m || !out || *out || !frontend_remote_unified_domain_read(r))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT requires its actual replica and media owners");
    frontend_unified_q3 *o=calloc(1,sizeof(*o));
    if (!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating persistent Unified Q3 CLIENT");
    o->frontend=f; o->replica=r; o->media=m; o->epoch=frontend_remote_unified_epoch(r);
    if (!current(o,e)) { free(o); return false; } *out=o; return true;
}
bool frontend_unified_q3_audio(frontend_unified_q3 *o, uint64_t owner, void *context,
    bool (*resolve)(void *,qa_actor_id,uint64_t *,qa_error *), qa_error *e)
{
    if (!o || o->busy || o->audio_owner || !owner || owner==QA_AUDIO_NO_OWNER || !resolve)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT audio must bind its real shared bus once");
    o->audio_owner=owner; o->audio_context=context; o->audio_actor=resolve; return true;
}
bool frontend_unified_q3_events(frontend_unified_q3 *o, frontend_unified_events *events, qa_error *e)
{
    if (!o || o->busy || o->events || !events ||
        frontend_unified_events_audio_owner(events)!=o->audio_owner)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT events require the same real audio ledger");
    o->events=events; return true;
}
bool frontend_unified_q3_frame_prepare(frontend_unified_q3 *o, const qa_unified_document *d, qa_error *e)
{
    if (!o || o->busy || o->prepared || !current(o,e)) return false;
    if (!frame_read(o,d,&o->candidate_time,e) || !frontend_unified_clone(d,&o->candidate,e)) return false;
    o->candidate_input=d; o->prepared=true; return true;
}
bool frontend_unified_q3_frame_ready(frontend_unified_q3 *o, const qa_unified_document *d, qa_error *e)
{
    int32_t time;
    return o && !o->busy && o->prepared && o->candidate && o->candidate_input==d && current(o,e) &&
        frame_read(o,d,&time,e) && time==o->candidate_time;
}
void frontend_unified_q3_frame_commit(frontend_unified_q3 *o)
{
    if (!o || !o->prepared || o->busy) return;
    qa_unified_document_destroy(o->frame); o->frame=o->candidate; o->candidate=NULL;
    o->previous_time=o->has_frame?o->time:o->candidate_time; o->time=o->candidate_time;
    o->has_frame=true; o->prepared=false; o->candidate_input=NULL;
}
void frontend_unified_q3_frame_abort(frontend_unified_q3 *o)
{ if (o && !o->busy) { qa_unified_document_destroy(o->candidate); o->candidate=NULL; o->candidate_input=NULL; o->prepared=false; } }
static bool enter(frontend_unified_q3 *o, qa_actor_id id, qa_error *e)
{
    if (!o || o->busy || !o->has_frame || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 effects require their committed real CLIENT FRAME");
    o->entered_frame=frontend_remote_unified_frame(o->replica); o->entered_actor=id; o->busy=true; return true;
}
static bool leave(frontend_unified_q3 *o, bool okay, qa_error *e)
{ if (okay) okay=current(o,e); o->busy=false; o->entered_frame=NULL; o->entered_actor=(qa_actor_id){0}; return okay; }
static q3n_frame effect_frame(frontend_unified_q3 *o, unified_q3_bank *b, int32_t time)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    b->source.time=time; b->source.actor=o->entered_actor;
    return (q3n_frame){.unified_effects=&b->source,.presentation=b->backend,.assets=b->assets,
        .media=b->media,.events=b->effects,.physical_presentation_seat=d->physical_seat,
        .time=time,.frame_milliseconds=(int32_t)((uint32_t)o->time-(uint32_t)o->previous_time)};
}
static const q3n_event_settings effect_settings={.blood=true,.gibs=true,.add_marks=true};
static bool content_text(const qa_unified_document *d, qa_json_id row, qa_buffer *content, qa_error *e)
{ return text(d,field(qa_unified_document_json(d),row,"content"),content,e); }
bool frontend_unified_q3_validate(frontend_unified_q3 *o, bool simulation,
    const qa_unified_document *d, qa_json_id row, qa_error *e)
{
    if (!o || !d || !current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id kind=field(j,row,"kind"), event=field(j,row,"event");
    if (simulation) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 simulation requires an actual normalized family event");
    qa_buffer content={0}; bool okay=content_text(d,row,&content,e); qa_vfs *files; const qa_product *p;
    if (okay) okay=qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),
        (const char *)content.data,&files,&p,e) && p->family==QA_GAME_Q3;
    qa_buffer_free(&content); if (!okay) return false;
    qa_actor_id id; int32_t n; qa_vec3 v;
    if (qa_json_string_equal(j,kind,"q3-character"))
        return actor(o,d,field(j,event,"actor"),&id,e) && integer(d,field(j,event,"event"),&n,e) &&
            integer(d,field(j,event,"parameter"),&n,e) && number(d,field(j,event,"timeMilliseconds"),&(double){0},e);
    if (qa_json_string_equal(j,kind,"q3-ballistics"))
        return actor(o,d,field(j,event,"actor"),&id,e) && integer(d,field(j,event,"weapon"),&n,e) &&
            vector(d,field(j,event,"origin"),&v,e) && vector(d,field(j,event,"end"),&v,e) &&
            vector(d,field(j,event,"normal"),&v,e) && actor(o,d,field(j,event,"target"),&id,e) &&
            integer(d,field(j,event,"surfaceFlags"),&n,e) && number(d,field(j,event,"timeMilliseconds"),&(double){0},e);
    if (!qa_json_string_equal(j,kind,"q3-source"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT received another event family");
    kind=field(j,event,"kind");
    if (qa_json_string_equal(j,kind,"print") || qa_json_string_equal(j,kind,"log") ||
        qa_json_string_equal(j,kind,"console-command")) {
        qa_buffer b={0}; okay=text(d,field(j,event,"text"),&b,e); qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"server-command")) {
        qa_buffer b={0}; okay=integer(d,field(j,event,"client"),&n,e) && text(d,field(j,event,"text"),&b,e);
        qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"configstring")) {
        qa_buffer b={0}; okay=integer(d,field(j,event,"index"),&n,e) && n>=0 && n<1024 && text(d,field(j,event,"value"),&b,e);
        qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"sound")) {
        qa_buffer b={0}; okay=actor(o,d,field(j,event,"actor"),&id,e) && vector(d,field(j,event,"origin"),&v,e) &&
            vector(d,field(j,event,"velocity"),&v,e) && text(d,field(j,event,"path"),&b,e) &&
            integer(d,field(j,event,"channel"),&n,e) && n>=0 && real(d,field(j,event,"volume"),&(float){0},e);
        qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"player-event") || qa_json_string_equal(j,kind,"entity-event"))
        return actor(o,d,field(j,event,"actor"),&id,e) && vector(d,field(j,event,"origin"),&v,e) &&
            integer(d,field(j,event,"time"),&n,e);
    if (qa_json_string_equal(j,kind,"drop-client")) {
        qa_buffer b={0}; okay=integer(d,field(j,event,"client"),&n,e) && text(d,field(j,event,"reason"),&b,e);
        qa_buffer_free(&b); return okay;
    }
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT received an unknown source event variant");
}
static bool character_event(frontend_unified_q3 *o, const qa_unified_document *d, qa_json_id row, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id v=field(j,row,"event");
    qa_actor_id id; int32_t event,parameter,time; double ms;
    if (!actor(o,d,field(j,v,"actor"),&id,e) || !integer(d,field(j,v,"event"),&event,e) ||
        !integer(d,field(j,v,"parameter"),&parameter,e) || !number(d,field(j,v,"timeMilliseconds"),&ms,e)) return false;
    (void)parameter;
    if (ms<INT32_MIN || ms>INT32_MAX) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 event clock exceeds Source storage");
    time=(int32_t)ms; qa_vec3 origin={0}; bool found=false;
    const qa_json_document *f=qa_unified_document_json(o->frame);
    qa_json_id chars=field(f,field(f,qa_unified_document_root(o->frame),"output"),"characters");
    for (size_t i=0; i<qa_json_size(f,chars); ++i) {
        qa_json_id c=qa_json_at(f,chars,i); qa_actor_id actual;
        if (!actor(o,o->frame,field(f,c,"actor"),&actual,e)) return false;
        if (qa_actor_id_equal(actual,id)) { if (!vector(o->frame,field(f,c,"origin"),&origin,e)) return false; found=true; break; }
    }
    if (!found) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 character event has no current full actor body");
    /* Events with separate sound records do not create a second voice. */
    if ((event>=0 && event<=12) || (event>=14 && event<=18) || event==21 || event==22 ||
        (event>=56 && event<=60) || (event>=77 && event<=84)) return true;
    if (event!=13 && event!=42 && event!=43 && event!=66)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 character event requires actual cgame snapshot context");
    qa_buffer content={0}; unified_q3_bank *b=NULL;
    bool okay=content_text(d,row,&content,e) && enter(o,id,e);
    if (okay) okay=bank_read(o,(const char *)content.data,&b,e);
    if (okay) {
        q3n_frame frame=effect_frame(o,b,time); frame.event_settings=&effect_settings;
        okay=q3n_media_load_unified_effects(b->media,&b->source,e);
        if (okay && event==13) {
            q3n_smoke smoke={.origin=origin,.velocity={0,0,1},.radius=32,.color={1,1,1,.33f},
                .duration=1000,.start_time=time,.flags=1,.shader=q3n_media_read(b->media)->graphics[Q3N_G_SMOKE_PUFF]};
            q3n_effect_smoke(&frame,&smoke);
        } else if (okay && event==66) q3n_effect_gib_player(&frame,origin);
        else if (okay) q3n_effect_spawn(&frame,origin);
    }
    qa_buffer_free(&content); return o->busy?leave(o,okay,e):okay;
}
bool frontend_unified_q3_presentation(frontend_unified_q3 *o, const qa_unified_document *d,
    qa_json_id row, bool *mirrored, qa_error *e)
{
    if (!mirrored || !frontend_unified_q3_validate(o,false,d,row,e)) return false;
    *mirrored=false; const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id kind=field(j,row,"kind"), v=field(j,row,"event");
    if (qa_json_string_equal(j,kind,"q3-character")) return character_event(o,d,row,e);
    if (qa_json_string_equal(j,kind,"q3-ballistics"))
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 ballistics require the actual private CLIENT weapon effect owner");
    kind=field(j,v,"kind"); qa_buffer b={0}; bool okay;
    if (qa_json_string_equal(j,kind,"sound")) {
        qa_buffer content={0},path={0}; qa_actor_id id; qa_vec3 origin; int32_t channel; float volume; double seconds;
        okay=o->events && content_text(d,row,&content,e) && text(d,field(j,v,"path"),&path,e) &&
            actor(o,d,field(j,v,"actor"),&id,e) && vector(d,field(j,v,"origin"),&origin,e) &&
            integer(d,field(j,v,"channel"),&channel,e) && real(d,field(j,v,"volume"),&volume,e) &&
            number(d,field(j,row,"seconds"),&seconds,e);
        bool duplicate=false;
        if (okay) okay=frontend_unified_events_sound_mirrored(o->events,d,row,&duplicate,e);
        if (okay && !duplicate) okay=frontend_unified_events_sound_path(o->events,(const char *)content.data,
            (const char *)path.data,id,origin,seconds*1000,channel,volume,1,0,e);
        qa_buffer_free(&content); qa_buffer_free(&path); return okay && current(o,e);
    }
    if (qa_json_string_equal(j,kind,"print") || qa_json_string_equal(j,kind,"log")) {
        okay=text(d,field(j,v,"text"),&b,e);
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        if (okay) qa_console_emit(domain->console,&domain->command_context,(const char *)b.data);
        qa_buffer_free(&b); return okay && current(o,e);
    }
    if (qa_json_string_equal(j,kind,"configstring")) {
        int32_t index; okay=integer(d,field(j,v,"index"),&index,e) && text(d,field(j,v,"value"),&b,e);
        if (okay) { free(o->configstrings[index]); o->configstrings[index]=(char *)b.data; b=(qa_buffer){0}; }
        qa_buffer_free(&b); return okay;
    }
    return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 Source event requires the actual retained cgame command/snapshot owner");
}
bool frontend_unified_q3_simulation(frontend_unified_q3 *o, const qa_unified_document *d, qa_json_id row, qa_error *e)
{ return frontend_unified_q3_validate(o,true,d,row,e); }
bool frontend_unified_q3_idle(const frontend_unified_q3 *o)
{
    if (!o || o->busy || o->prepared) return false;
    for (unified_q3_bank *b=o->banks; b; b=b->next)
        if (!q3n_events_idle(b->effects) || !q3n_media_idle(b->media) ||
            (b->backend && !qa_q3_presentation_idle(b->backend))) return false;
    return true;
}
bool frontend_unified_q3_destroy(frontend_unified_q3 **address, qa_error *e)
{
    if (!address || !*address) return true; frontend_unified_q3 *o=*address;
    if (!frontend_unified_q3_idle(o)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT children have not returned");
    while (o->banks) {
        unified_q3_bank *b=o->banks;
        if (b->backend && !qa_q3_presentation_destroy(b->backend,e)) return false;
        q3n_events_destroy(b->effects); q3n_media_destroy(b->media);
        o->banks=b->next; free(b->content); free(b);
    }
    while (o->characters) { unified_q3_character *c=o->characters; o->characters=c->next;
        qa_resource_release(c->animation_holder); free(c); }
    for (size_t i=0; i<1024; ++i) free(o->configstrings[i]);
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->candidate); free(o); *address=NULL; return true;
}
