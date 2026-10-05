#include "remote_unified_private.h"
#include "remote_unified_events.h"
#include "remote_unified_save.h"
#include "../application/unified_output.h"
#include "qa/binary.h"

#include <float.h>
#include <math.h>

typedef struct unified_event_batch {
    struct unified_event_batch *next;
    uint64_t frame;
    qa_unified_document *presentation, *simulation;
    size_t presentation_at, simulation_at;
} unified_event_batch;
typedef struct unified_event_resource {
    struct unified_event_resource *next;
    qa_unified_document *key;
    char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    const qa_resource *resource;
    qa_audio_asset *asset;
    qa_audio_family family;
} unified_event_resource;
typedef struct unified_event_link {
    double sequence;
    bool mirrored;
} unified_event_link;
typedef struct unified_component_owner {
    struct unified_component_owner *next;
    qa_unified_document *identity;
    char *provider, *content;
    uint64_t generation;
    bool retired, cancelled;
} unified_component_owner;
struct frontend_unified_events {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_event_options options;
    unified_event_batch *pending, **tail;
    unified_event_resource *resources;
    unified_component_owner *components;
    unified_event_link *links;
    size_t link_count, link_capacity;
    qa_hud *hud;
    qa_ui *hud_ui;
    qa_application *hud_application;
    uint32_t hud_seat;
    uint32_t epoch;
    uint64_t frame, prepared_frame;
    qa_unified_document *prepared_document;
    double seconds, prepared_seconds, presentation_sequence, simulation_sequence;
    bool has_frame, prepared, busy, failed, owns_audio, families_ready;
};
static qa_json_id field(const qa_json_document *j, qa_json_id row, const char *key)
{ return qa_json_get(j, row, key); }
static bool current(frontend_unified_events *o, qa_error *e)
{
    if (!o || o->failed || !frontend_unified_media_current(o->media))
        return frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified event media is not current");
    bool snapshot=o->frontend->capture || o->frontend->source_restoring;
    if (!(snapshot?frontend_remote_unified_checkpoint_current(o->replica,e):frontend_remote_unified_current(o->replica,e))) return false;
    return o->epoch == frontend_remote_unified_epoch(o->replica) ||
        frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified event owner changed its admitted epoch");
}
static bool execution_current(frontend_unified_events *o,qa_error *e)
{
    if (!o || !o->families_ready || o->frontend->capture || o->frontend->source_restoring || o->frontend->resource_inventory)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event delivery overlaps private graph capture or restore");
    return current(o,e);
}
static bool scalar(const qa_unified_document *d, qa_json_id row, double *v, qa_error *e)
{
    if (!qa_unified_document_number(d, row, v, e)) return false;
    return isfinite(*v) || frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified event scalar is not finite");
}
static bool sequence_value(double value,double minimum)
{ return isfinite(value) && value>=minimum && value<=(double)QA_UNIFIED_SAFE_INTEGER && trunc(value)==value; }
static bool sequence_read(const qa_unified_document *d,qa_json_id row,double *out,qa_error *e)
{
    if (!scalar(d,row,out,e)) return false;
    return sequence_value(*out,0) || frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event sequence is outside its actual source counter");
}
static bool real(const qa_unified_document *d, qa_json_id row, float *v, qa_error *e)
{
    double n;
    if (!scalar(d,row,&n,e)) return false;
    if (fabs(n)>FLT_MAX) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event exceeds float storage");
    *v=(float)n; return true;
}
static bool vector(const qa_unified_document *d, qa_json_id row, qa_vec3 *v, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    return real(d,field(j,row,"x"),&v->x,e) && real(d,field(j,row,"y"),&v->y,e) &&
        real(d,field(j,row,"z"),&v->z,e);
}
static bool text(const qa_unified_document *d, qa_json_id row, qa_buffer *v, qa_error *e)
{
    if (!qa_json_string(qa_unified_document_json(d),row,v,e)) return false;
    if (!memchr(v->data,0,v->size)) return true;
    qa_buffer_free(v);
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event text contains an embedded NUL");
}
static bool actor(frontend_unified_events *o,const qa_unified_document *d,qa_json_id row,
    qa_actor_id *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if (qa_json_type(j,row)==QA_JSON_NULL) { *out=(qa_actor_id){0}; return true; }
    uint64_t slot,generation;
    if (!qa_json_u64(j,field(j,row,"slot"),&slot,e) || slot>UINT32_MAX ||
        !qa_json_u64(j,field(j,row,"generation"),&generation,e))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event actor exceeds its wire identity domain");
    return o->frontend->capture || o->frontend->source_restoring?
        frontend_remote_unified_actor_retained(o->replica,(uint32_t)slot,generation,out,e):
        frontend_remote_unified_actor(o->replica,(uint32_t)slot,generation,out,e);
}
static bool component_identity(const qa_unified_document *d,qa_json_id row,qa_buffer *provider,uint64_t *generation,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    return qa_json_type(j,row)==QA_JSON_OBJECT && text(d,field(j,row,"provider"),provider,e) && provider->size &&
        qa_json_u64(j,field(j,row,"generation"),generation,e) && *generation && *generation<=QA_UNIFIED_SAFE_INTEGER;
}
static unified_component_owner *component_find(const frontend_unified_events *o,const char *provider,uint64_t generation)
{
    for (unified_component_owner *c=o->components;c;c=c->next)
        if (c->generation==generation && !strcmp(c->provider,provider)) return c;
    return NULL;
}
static void component_free(unified_component_owner *c)
{ qa_unified_document_destroy(c->identity); free(c->provider); free(c->content); free(c); }
static bool component_read(frontend_unified_events *o,const qa_unified_document *d,const char *content,
    unified_component_owner **out,qa_error *e)
{
    qa_buffer provider={0}; uint64_t generation=0; qa_vfs *files; const qa_product *product;
    bool okay=d && qa_unified_document_type(d)==QA_UNIFIED_CHECKPOINT && content && *content &&
        component_identity(d,qa_unified_document_root(d),&provider,&generation,e) &&
        qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&product,e);
    unified_component_owner *c=okay?calloc(1,sizeof(*c)):NULL;
    if (okay && !c) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining reliable component presentation identity");
    if (okay) {
        c->content=malloc(strlen(content)+1);
        okay=c->content && qa_unified_document_retain(d,&c->identity,e);
        if (!c->content) frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining component content identity");
    }
    if (okay) { strcpy(c->content,content); c->provider=(char *)provider.data; provider=(qa_buffer){0};
        c->generation=generation; *out=c; }
    else { if (c) component_free(c); if (!e || e->code==QA_OK)
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Component presentation identity is outside its admitted recipe"); }
    qa_buffer_free(&provider); return okay;
}
bool frontend_unified_events_component_current(const frontend_unified_events *o,const qa_unified_document *d,
    const char *content,bool *active,qa_error *e)
{
    if (!o || !d || !content || !active || qa_unified_document_type(d)!=QA_UNIFIED_CHECKPOINT)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component qualification needs its retained owner and content");
    qa_buffer provider={0}; uint64_t generation=0;
    bool okay=component_identity(d,qa_unified_document_root(d),&provider,&generation,e);
    unified_component_owner *c=okay?component_find(o,(const char *)provider.data,generation):NULL;
    if (okay && (!c || strcmp(c->content,content)))
        okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Component presentation token has no matching reliable content admission");
    if (okay) *active=!c->retired && !c->cancelled;
    qa_buffer_free(&provider); return okay;
}
bool frontend_unified_events_component_admit(frontend_unified_events *o,const qa_unified_document *d,const char *content,qa_error *e)
{ bool created; return frontend_unified_events_component_admit_created(o,d,content,&created,e); }
bool frontend_unified_events_component_admit_created(frontend_unified_events *o,const qa_unified_document *d,const char *content,
    bool *created,qa_error *e)
{
    if (!o || !created || o->busy || o->prepared || !current(o,e)) return false;
    *created=false;
    unified_component_owner *candidate=NULL;
    if (!component_read(o,d,content,&candidate,e)) return false;
    unified_component_owner *c=component_find(o,candidate->provider,candidate->generation);
    if (c) {
        bool okay=!strcmp(c->content,candidate->content) ||
            frontend_unified_fail(e,QA_ERROR_FORMAT,"Reliable component token changed its admitted content");
        if (okay && c->cancelled) { c->cancelled=false; *created=true; }
        component_free(candidate); return okay;
    }
    candidate->next=o->components; o->components=candidate; *created=true; return true;
}
bool frontend_unified_events_component_cancel(frontend_unified_events *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || o->prepared || !d || qa_unified_document_type(d)!=QA_UNIFIED_CHECKPOINT)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component cancellation needs returned retained event storage");
    qa_buffer provider={0}; uint64_t generation=0;
    bool okay=component_identity(d,qa_unified_document_root(d),&provider,&generation,e);
    unified_component_owner *c=okay?component_find(o,(const char *)provider.data,generation):NULL;
    if (okay && (!c || c->retired)) okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Component cancellation has no unpublished admission");
    if (okay) c->cancelled=true;
    qa_buffer_free(&provider); return okay;
}
bool frontend_unified_events_component_retire(frontend_unified_events *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || o->prepared || !d || qa_unified_document_type(d)!=QA_UNIFIED_CHECKPOINT)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component retirement needs returned retained event storage");
    qa_buffer provider={0}; uint64_t generation=0;
    bool okay=component_identity(d,qa_unified_document_root(d),&provider,&generation,e);
    unified_component_owner *c=okay?component_find(o,(const char *)provider.data,generation):NULL;
    if (okay && !c) okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Retiring component has no reliable presentation admission");
    if (okay) { c->retired=true; c->cancelled=false; }
    qa_buffer_free(&provider); return okay;
}
static uint64_t clock_ns(double seconds)
{
    long double n=(long double)seconds*1e9L;
    return n<=0?0:n>=(long double)UINT64_MAX?UINT64_MAX:(uint64_t)n;
}
static bool hud_read(void *ctx,const qa_hud_frame *f,qa_hud_data *out,qa_error *e)
{
    frontend_unified_events *o=ctx;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!o->busy || !d || f->seat!=d->physical_seat)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified message HUD lost its physical CLIENT seat");
    *out=(qa_hud_data){.source_vitals=true}; return true;
}
static qa_hud_options hud_options(frontend_unified_events *o)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    o->hud_ui=o->frontend->seats[d->physical_seat].ui;
    o->hud_application=d->application; o->hud_seat=d->physical_seat;
    return (qa_hud_options){.ui=o->hud_ui,
        .application=d->application,.seat=d->physical_seat,.context=o,.read=hud_read};
}
static bool hud_current(frontend_unified_events *o,qa_error *e)
{
    if (!execution_current(o,e)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    return (d && d->physical_seat==o->hud_seat && d->application==o->hud_application &&
        o->frontend->seats[d->physical_seat].ui==o->hud_ui) ||
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Compiled center print changed its actual CLIENT HUD or UI");
}
static frontend_unified_events *allocate(qa_frontend *f,frontend_remote_unified *r,
    frontend_unified_media *m,const frontend_unified_event_options *opts,qa_error *e)
{
    if (!f || !r || !m || !opts || !opts->audio_owner || opts->audio_owner==QA_AUDIO_NO_OWNER ||
        !opts->audio_actor || !opts->validate || !frontend_remote_unified_domain_read(r)) {
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified events need their actual media and audio route"); return NULL;
    }
    frontend_unified_events *o=calloc(1,sizeof(*o));
    if (!o) { frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating private unified event ledger"); return NULL; }
    o->frontend=f; o->replica=r; o->media=m; o->options=*opts;
    o->epoch=frontend_remote_unified_epoch(r); o->tail=&o->pending;
    o->families_ready=true;
    o->presentation_sequence=-1; o->simulation_sequence=-1; return o;
}
bool frontend_unified_events_create(qa_frontend *f,frontend_remote_unified *r,
    frontend_unified_media *m,const frontend_unified_event_options *opts,frontend_unified_events **out,qa_error *e)
{
    if (!out || *out) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event output must be empty");
    frontend_unified_events *o=allocate(f,r,m,opts,e);
    if (!o) return false;
    qa_hud_options h=hud_options(o);
    if (!current(o,e) || !qa_hud_create(&h,&o->hud,e)) { free(o); return false; }
    o->owns_audio=true; *out=o; return true;
}
static void batch_free(unified_event_batch *b)
{ qa_unified_document_destroy(b->presentation); qa_unified_document_destroy(b->simulation); free(b); }
static void resource_free(unified_event_resource *r)
{ qa_audio_asset_release(r->asset); qa_unified_document_destroy(r->key); free(r); }
static bool resource_read(frontend_unified_events *o,const qa_unified_document *key,
    unified_event_resource **out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(key); qa_json_id row=qa_unified_document_root(key);
    qa_buffer content={0},path={0},hash={0}; uint64_t length;
    qa_sha256_digest digest; qa_launch_resource resource; qa_vfs *vfs; const qa_vfs_acquisition *opening;
    bool okay=text(key,field(j,row,"content"),&content,e) && text(key,field(j,row,"path"),&path,e) &&
        text(key,field(j,row,"digest"),&hash,e) && qa_sha256_parse((const char *)hash.data,&digest,e) &&
        qa_json_u64(j,field(j,row,"byteLength"),&length,e);
    if (okay) okay=qa_executable_recipe_find_resource(frontend_remote_unified_recipe(o->replica),
        (const char *)content.data,(const char *)path.data,&digest,length,&resource,&vfs,&opening);
    unified_event_resource *r=okay?calloc(1,sizeof(*r)):NULL;
    const qa_product *product=NULL; qa_vfs *actual;
    if (okay && !r) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining declared unified sound identity");
    if (okay) okay=qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),
        (const char *)content.data,&actual,&product,e) && actual==vfs &&
        application_unified_resource_key(product,(const char *)path.data,resource.resource,&r->key,r->id,e);
    if (okay) {
        r->resource=resource.resource;
        r->family=product->family==QA_GAME_Q1?QA_AUDIO_Q1:product->family==QA_GAME_Q2?QA_AUDIO_Q2:QA_AUDIO_Q3;
        *out=r;
    } else { if (r) resource_free(r); if (!e || e->code==QA_OK)
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified resource is outside its admitted recipe dictionary"); }
    qa_buffer_free(&content); qa_buffer_free(&path); qa_buffer_free(&hash); return okay;
}
static bool declare_resources(frontend_unified_events *o,const qa_unified_document *doc,qa_json_id array,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(doc);
    unified_event_resource *head=NULL,**tail=&head;
    bool okay=true;
    for (size_t i=0;okay && i<qa_json_size(j,array);++i) {
        qa_unified_document *key=NULL; unified_event_resource *r=NULL;
        okay=qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,qa_json_at(j,array,i)),&key,e) &&
            resource_read(o,key,&r,e);
        qa_unified_document_destroy(key);
        if (okay) { *tail=r; tail=&r->next; }
    }
    if (!okay) { while (head) { unified_event_resource *r=head; head=r->next; resource_free(r); } return false; }
    while (head) {
        unified_event_resource *r=head; head=r->next; unified_event_resource *same=o->resources;
        while (same && strcmp(same->id,r->id)) same=same->next;
        if (same) resource_free(r); else { r->next=o->resources; o->resources=r; }
    }
    return true;
}
static bool rows_valid(frontend_unified_events *o,const qa_unified_document *d,bool simulation,bool children,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id root=qa_unified_document_root(d);
    if (qa_json_type(j,root)!=QA_JSON_ARRAY || qa_json_size(j,root)>65536)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event delivery is not a bounded array");
    for (size_t i=0;i<qa_json_size(j,root);++i) {
        qa_json_id row=qa_json_at(j,root,i); double sequence;
        if (!sequence_read(d,field(j,row,"sequence"),&sequence,e)) return false;
        if (!simulation) {
            qa_json_id recipient=field(j,row,"recipient");
            if (recipient!=QA_JSON_NONE && qa_json_type(j,recipient)!=QA_JSON_NULL) {
                qa_actor_id received,viewer; uint32_t number;
                if (!actor(o,d,recipient,&received,e) ||
                    !frontend_remote_unified_player(o->replica,&viewer,&number) || !qa_actor_id_equal(received,viewer))
                    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified presentation changed its admitted full actor recipient");
            }
            qa_json_id token=field(j,row,"owner");
            if (token!=QA_JSON_NONE && qa_json_type(j,token)!=QA_JSON_NULL) {
                qa_buffer provider={0}; uint64_t generation=0;
                bool okay=component_identity(d,token,&provider,&generation,e);
                unified_component_owner *c=okay?component_find(o,(const char *)provider.data,generation):NULL;
                if (okay && c && !qa_json_string_equal(j,field(j,row,"content"),c->content))
                    okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Component event changed its reliable content identity");
                qa_buffer_free(&provider); if (!okay) return false;
            }
        }
        if (simulation) {
            qa_json_id time=field(j,row,"time"),a=field(j,row,"audience"),ak=field(j,a,"kind");
            double value;
            if (!scalar(d,field(j,time,"value"),&value,e) ||
                (!qa_json_string_equal(j,field(j,time,"kind"),"seconds") &&
                 !qa_json_string_equal(j,field(j,time,"kind"),"milliseconds")))
                return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified simulation event has no actual source clock");
            if (qa_json_string_equal(j,ak,"client")) {
                uint64_t slot,generation; qa_json_id client=field(j,a,"client");
                if (!qa_json_u64(j,field(j,client,"slot"),&slot,e) ||
                    !qa_json_u64(j,field(j,client,"generation"),&generation,e) ||
                    slot!=o->replica->wire_client.slot || generation!=o->replica->wire_client.generation)
                    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified simulation event changed its admitted wire client");
            } else if (!qa_json_string_equal(j,ak,"world"))
                return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Unified event audience has no admitted remote CLIENT route");
            qa_json_id p=field(j,row,"payload"),kind=field(j,p,"kind");
            if (qa_json_string_equal(j,kind,"sound")) {
                qa_buffer id={0}; qa_actor_id actual; qa_vec3 origin; float volume,attenuation; double channel;
                bool okay=text(d,field(j,p,"resource"),&id,e) && actor(o,d,field(j,p,"actor"),&actual,e) &&
                    vector(d,field(j,p,"origin"),&origin,e) && real(d,field(j,p,"volume"),&volume,e) &&
                    real(d,field(j,p,"attenuation"),&attenuation,e) && scalar(d,field(j,p,"channel"),&channel,e);
                unified_event_resource *r=o->resources;
                while (okay && r && strcmp(r->id,(const char *)id.data)) r=r->next;
                qa_buffer_free(&id);
                if (!okay) return false;
                if (!r || channel<INT32_MIN || channel>INT32_MAX || trunc(channel)!=channel)
                    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified sound has no declared resource or source channel");
                continue;
            }
            if (qa_json_string_equal(j,kind,"message")) {
                qa_json_id event=field(j,p,"event"),ek=field(j,event,"kind");
                if (qa_json_string_equal(j,ek,"print") || qa_json_string_equal(j,ek,"center-print") ||
                    qa_json_string_equal(j,ek,"command-text")) {
                    qa_buffer t={0}; bool okay=text(d,field(j,event,"text"),&t,e);
                    qa_buffer_free(&t); if (!okay) return false;
                    qa_json_id link=field(j,p,"sourcePresentationSequence");
                    if (link!=QA_JSON_NONE && !sequence_read(d,link,&value,e)) return false;
                    if (!qa_json_string_equal(j,ek,"command-text")) continue;
                }
            }
        }
        if (children && !o->options.validate(o->options.context,simulation,d,row,e)) return false;
    }
    return true;
}
bool frontend_unified_events_control(frontend_unified_events *o,const qa_unified_document *doc,qa_error *e)
{
    if (!o || o->busy || o->prepared || !doc || qa_unified_document_type(doc)!=QA_UNIFIED_CONTROL_DOCUMENT ||
        !execution_current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(doc);
    qa_json_id value=field(j,qa_unified_document_root(doc),"value"),kind=field(j,value,"kind");
    uint64_t epoch;
    if (!qa_json_u64(j,field(j,value,"epoch"),&epoch,e) || epoch!=o->epoch)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event control changed its source epoch");
    if (qa_json_string_equal(j,kind,"resources")) return declare_resources(o,doc,field(j,value,"resources"),e);
    if (!qa_json_string_equal(j,kind,"events")) return true;
    unified_event_batch *b=calloc(1,sizeof(*b)); qa_buffer p={0},s={0};
    if (!b) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining reliable pre-frame unified events");
    bool okay=qa_json_u64(j,field(j,value,"frame"),&b->frame,e) &&
        qa_unified_document_bytes(doc,field(j,value,"payload"),&p,e) &&
        qa_unified_document_bytes(doc,field(j,value,"simulation"),&s,e) &&
        qa_unified_document_decode(QA_UNIFIED_EVENTS_DOCUMENT,(qa_bytes){p.data,p.size},&b->presentation,e) &&
        qa_unified_document_decode(QA_UNIFIED_CHECKPOINT,(qa_bytes){s.data,s.size},&b->simulation,e) &&
        rows_valid(o,b->presentation,false,true,e) && rows_valid(o,b->simulation,true,true,e) && current(o,e);
    qa_buffer_free(&p); qa_buffer_free(&s);
    if (!okay) { batch_free(b); return false; }
    *o->tail=b; o->tail=&b->next; return true;
}
bool frontend_unified_events_frame_prepare(frontend_unified_events *o,const qa_unified_document *doc,qa_error *e)
{
    if (!o || o->busy || o->prepared || !doc || qa_unified_document_type(doc)!=QA_UNIFIED_FRAME_DOCUMENT ||
        !execution_current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(doc); qa_json_id root=qa_unified_document_root(doc);
    qa_json_id frame=field(j,field(j,field(j,root,"output"),"snapshot"),"frame");
    qa_json_id time=field(j,frame,"time"); uint64_t epoch,n; double seconds;
    if (!qa_json_u64(j,field(j,root,"epoch"),&epoch,e) || epoch!=o->epoch ||
        !qa_json_u64(j,field(j,frame,"frame"),&n,e) || !scalar(doc,field(j,time,"value"),&seconds,e)) return false;
    if (qa_json_string_equal(j,field(j,time,"kind"),"milliseconds")) seconds/=1000;
    else if (!qa_json_string_equal(j,field(j,time,"kind"),"seconds"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified frame clock has no source domain");
    if (o->has_frame && n<=o->frame) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event publication must advance its actual frame");
    if(!qa_unified_document_retain(doc,&o->prepared_document,e))return false;
    o->prepared_frame=n; o->prepared_seconds=seconds; o->prepared=true; return true;
}
void frontend_unified_events_frame_commit(frontend_unified_events *o)
{
    if (!o || !o->prepared || o->busy) return;
    o->frame=o->prepared_frame; o->seconds=o->prepared_seconds; o->has_frame=true; o->prepared=false;
    qa_unified_document_destroy(o->prepared_document);o->prepared_document=NULL;
}
bool frontend_unified_events_frame_ready(frontend_unified_events *o,const qa_unified_document *doc,qa_error *e)
{
    if (!o || o->busy || !o->prepared || !doc || o->failed ||
        qa_unified_document_type(doc)!=QA_UNIFIED_FRAME_DOCUMENT ||
        (o->hud && !qa_hud_idle(o->hud)) || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified events have no returned prepared frame");
    const qa_json_document *j=qa_unified_document_json(doc);
    qa_json_id root=qa_unified_document_root(doc);
    qa_json_id f=field(j,field(j,field(j,root,"output"),"snapshot"),"frame"),t=field(j,f,"time");
    uint64_t epoch,n; double seconds;
    if (!qa_json_u64(j,field(j,root,"epoch"),&epoch,e) || !qa_json_u64(j,field(j,f,"frame"),&n,e) ||
        !scalar(doc,field(j,t,"value"),&seconds,e)) return false;
    if (qa_json_string_equal(j,field(j,t,"kind"),"milliseconds")) seconds/=1000;
    else if (!qa_json_string_equal(j,field(j,t,"kind"),"seconds")) return false;
    return (epoch==o->epoch && n==o->prepared_frame && seconds==o->prepared_seconds) ||
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified prepared event frame differs from publication");
}
void frontend_unified_events_frame_abort(frontend_unified_events *o)
{ if (o && !o->busy) {o->prepared=false;qa_unified_document_destroy(o->prepared_document);o->prepared_document=NULL;} }
static bool mirrored(frontend_unified_events *o,double sequence)
{
    for (size_t i=0;i<o->link_count;++i) if (o->links[i].sequence==sequence) return o->links[i].mirrored;
    return false;
}
static bool link_reserve(frontend_unified_events *o,qa_error *e)
{
    if (o->link_count<o->link_capacity) return true;
    size_t n=o->link_capacity?o->link_capacity*2:32;
    if (n<o->link_capacity || n>SIZE_MAX/sizeof(*o->links))
        return frontend_unified_fail(e,QA_ERROR_MEMORY,"Unified presentation link table overflow");
    void *p=realloc(o->links,n*sizeof(*o->links));
    if (!p) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining unified message sequence links");
    o->links=p; o->link_capacity=n; return true;
}
static bool message(frontend_unified_events *o,const qa_unified_document *d,qa_json_id row,
    double seconds,bool center,bool newline,qa_error *e)
{
    qa_buffer t={0}; const qa_json_document *j=qa_unified_document_json(d);
    if (!text(d,field(j,row,"text"),&t,e)) return false;
    if (newline) {
        if (t.size>SIZE_MAX-2) { qa_buffer_free(&t); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Source print extent overflow"); }
        uint8_t *p=realloc(t.data,t.size+2);
        if (!p) { qa_buffer_free(&t); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining source print newline"); }
        t.data=p;
    }
    (void)seconds; (void)center;
    bool okay=current(o,e);
    if (okay) {
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        if (newline) { t.data[t.size]='\n'; t.data[t.size+1]=0; }
        if (okay) qa_console_emit(domain->console,&domain->command_context,(const char *)t.data);
    }
    qa_buffer_free(&t); return okay;
}
static bool sound(frontend_unified_events *o,const qa_unified_document *d,qa_json_id p,double ms,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_buffer id={0};
    bool okay=text(d,field(j,p,"resource"),&id,e);
    unified_event_resource *r=o->resources;
    while (okay && r && strcmp(r->id,(const char *)id.data)) r=r->next;
    qa_buffer_free(&id);
    if (!okay) return false;
    if (!r) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified sound precedes its actual resource declaration");
    qa_actor_id a; qa_vec3 origin; float volume,attenuation; double channel;
    if (!actor(o,d,field(j,p,"actor"),&a,e) || !vector(d,field(j,p,"origin"),&origin,e) ||
        !real(d,field(j,p,"volume"),&volume,e) || !real(d,field(j,p,"attenuation"),&attenuation,e) ||
        !scalar(d,field(j,p,"channel"),&channel,e)) return false;
    if (channel<INT32_MIN || channel>INT32_MAX || trunc(channel)!=channel)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified sound channel exceeds its source word");
    uint64_t audio=QA_AUDIO_NO_ACTOR;
    if (a.registry && !o->options.audio_actor(o->options.context,a,&audio,e)) return false;
    if (!r->asset) {
        const qa_json_document *kj=qa_unified_document_json(r->key); qa_json_id kr=qa_unified_document_root(r->key);
        qa_buffer content={0},path={0}; qa_scene_resources *images; qa_material_library *materials;
        qa_font_library *fonts; qa_audio_bank *bank;
        bool loaded=text(r->key,field(kj,kr,"content"),&content,e) && text(r->key,field(kj,kr,"path"),&path,e) &&
            frontend_unified_media_bank(o->media,(const char *)content.data,&images,&materials,&fonts,&bank,e) &&
            qa_audio_bank_register(bank,(const char *)path.data,r->family,&r->asset,e);
        qa_buffer_free(&content); qa_buffer_free(&path);
        if (!loaded) return false;
        if (!r->asset) return frontend_unified_fail(e,QA_ERROR_NOT_FOUND,"Declared Unified sound is absent from its actual bank");
        const qa_resource *resource=qa_audio_asset_resource(r->asset);
        if (!qa_sha256_equal(qa_resource_digest(resource),qa_resource_digest(r->resource)) ||
            qa_resource_bytes(resource).size!=qa_resource_bytes(r->resource).size)
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified sound bank differs from its declared resource authority");
    }
    if (!o->frontend->audio) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified sound has no actual output engine");
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_audio_play play={.sample=qa_audio_asset_sample(r->asset),.asset=r->asset,
        .resource_id=qa_resource_id(r->resource),.family=r->family,
        .actor=audio,.owner=o->options.audio_owner,.audience=domain->physical_seat,.origin_kind=QA_AUDIO_FIXED,
        .origin=origin,.channel=(int32_t)channel,.volume=volume,.attenuation=attenuation,
        .has_server_time=true,.server_milliseconds=ms};
    int32_t signed_tick;
    return qa_audio_source_milliseconds(ms,&signed_tick,e) && current(o,e) &&
        qa_audio_engine_play(o->frontend->audio,&play,signed_tick,e);
}
static bool apply_presentation(frontend_unified_events *o,const qa_unified_document *d,qa_json_id row,
    bool *mirrors,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id event=field(j,row,"event");
    double seconds;
    *mirrors=false;
    if (!scalar(d,field(j,row,"seconds"),&seconds,e)) return false;
    if (!o->options.presentation)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Unified source presentation consumer is not installed");
    if (!o->options.presentation(o->options.context,d,row,mirrors,e)) return false;
    if (qa_json_string_equal(j,field(j,row,"kind"),"presentation-owner")) {
        qa_buffer provider={0}; uint64_t generation=0;
        if (!component_identity(d,field(j,event,"owner"),&provider,&generation,e)) { qa_buffer_free(&provider); return false; }
        unified_component_owner *c=component_find(o,(const char *)provider.data,generation);
        if (c) { c->retired=qa_json_string_equal(j,field(j,event,"kind"),"retired"); c->cancelled=false; }
        qa_buffer_free(&provider);
    }
    return true;
}
static bool apply_simulation(frontend_unified_events *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id p=field(j,row,"payload"),kind=field(j,p,"kind");
    qa_json_id time=field(j,row,"time"); double ms;
    if (!scalar(d,field(j,time,"value"),&ms,e)) return false;
    if (qa_json_string_equal(j,field(j,time,"kind"),"seconds")) ms*=1000;
    else if (!qa_json_string_equal(j,field(j,time,"kind"),"milliseconds"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified simulation clock has no source domain");
    if (!isfinite(ms)) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event source time overflow");
    if (qa_json_string_equal(j,kind,"sound")) return sound(o,d,p,ms,e);
    if (qa_json_string_equal(j,kind,"message")) {
        qa_json_id link=field(j,p,"sourcePresentationSequence"); double sequence;
        if (link!=QA_JSON_NONE) { if (!scalar(d,link,&sequence,e)) return false;
            if (mirrored(o,sequence)) return true; }
        qa_json_id event=field(j,p,"event"),ek=field(j,event,"kind");
        if (qa_json_string_equal(j,ek,"print") || qa_json_string_equal(j,ek,"center-print"))
            return message(o,d,event,ms/1000,qa_json_string_equal(j,ek,"center-print"),true,e);
    }
    if (!o->options.simulation)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Unified simulation media consumer is not installed");
    return o->options.simulation(o->options.context,d,row,e);
}
bool frontend_unified_events_enter(frontend_unified_events *o,qa_error *e)
{
    if (!o || o->busy || o->prepared || !execution_current(o,e)) return false;
    if (!o->has_frame) return true;
    o->busy=true; bool okay=true;
    while (okay && !o->replica->retired && o->pending && o->pending->frame<=o->frame) {
        unified_event_batch *b=o->pending;
        const qa_json_document *j=qa_unified_document_json(b->presentation);
        qa_json_id root=qa_unified_document_root(b->presentation);
        while (okay && !o->replica->retired && b->presentation_at<qa_json_size(j,root)) {
            qa_json_id row=qa_json_at(j,root,b->presentation_at); double sequence;
            okay=scalar(b->presentation,field(j,row,"sequence"),&sequence,e);
            if (okay && sequence>o->presentation_sequence) {
                bool mirrors;
                okay=link_reserve(o,e) && current(o,e) && apply_presentation(o,b->presentation,row,&mirrors,e);
                if (okay) { o->links[o->link_count++]=(unified_event_link){sequence,mirrors};
                    o->presentation_sequence=sequence; }
            }
            if (okay) ++b->presentation_at;
        }
        bool presentation_done=b->presentation_at==qa_json_size(j,root);
        j=qa_unified_document_json(b->simulation); root=qa_unified_document_root(b->simulation);
        while (okay && !o->replica->retired && b->simulation_at<qa_json_size(j,root)) {
            qa_json_id row=qa_json_at(j,root,b->simulation_at); double sequence;
            okay=scalar(b->simulation,field(j,row,"sequence"),&sequence,e);
            if (okay && sequence>o->simulation_sequence) {
                okay=current(o,e) && apply_simulation(o,b->simulation,row,e);
                if (okay) o->simulation_sequence=sequence;
            }
            if (okay) ++b->simulation_at;
        }
        if (okay && presentation_done && b->simulation_at==qa_json_size(j,root)) {
            o->pending=b->next; batch_free(b); if (!o->pending) o->tail=&o->pending;
        }
    }
    if (!okay) o->failed=true;
    if (okay && o->link_count>4096) {
        size_t n=0;
        for (size_t i=0;i<o->link_count;++i)
            if (o->links[i].sequence>=o->presentation_sequence-2048) o->links[n++]=o->links[i];
        o->link_count=n;
    }
    o->busy=false; return okay;
}
bool frontend_unified_events_center_print(frontend_unified_events *o,const char *text_value,
    double source_milliseconds,double duration_milliseconds,qa_error *e)
{
    if (!o || !text_value || !isfinite(source_milliseconds) || !isfinite(duration_milliseconds))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Compiled center print requires finite Source clocks and text");
    if (o->busy || o->prepared || !o->has_frame || !o->hud || !qa_hud_idle(o->hud))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Compiled center print overlaps a CLIENT callback or unpublished frame");
    if (!hud_current(o,e)) return false;
    o->busy=true;
    bool okay=qa_hud_center_print(o->hud,text_value,clock_ns(source_milliseconds/1000),
        clock_ns(duration_milliseconds/1000),true,0,e);
    o->busy=false;
    return okay && qa_hud_idle(o->hud) && hud_current(o,e);
}
bool frontend_unified_events_draw(frontend_unified_events *o,const qa_scene_view *view,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !view || !frame || o->busy || !o->has_frame || !execution_current(o,e)) return false;
    qa_actor_id player; uint32_t source;
    if (!frontend_remote_unified_player(o->replica,&player,&source)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    o->busy=true;
    bool okay=qa_hud_draw(o->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,
        .time_ns=clock_ns(o->seconds),.viewport=view->viewport,.safe_area=view->viewport,.scale=1,.visible=true},frame,e);
    o->busy=false; return okay;
}
bool frontend_unified_events_checkpoint_ready(const frontend_unified_events *o)
{ return !o || (!o->busy && (!o->hud || qa_hud_idle(o->hud))); }
bool frontend_unified_events_idle(const frontend_unified_events *o)
{ return (!o || !o->prepared) && frontend_unified_events_checkpoint_ready(o); }
bool frontend_unified_events_destroy(frontend_unified_events **slot,qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_events *o=*slot;
    if (!frontend_unified_events_idle(o)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event owner still has a live callback");
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (o->owns_audio && o->frontend->audio && !qa_audio_engine_stop_owner(o->frontend->audio,o->options.audio_owner,d->physical_seat,e)) return false;
    if (o->hud && !qa_hud_destroy(o->hud,e)) return false;
    while (o->pending) { unified_event_batch *b=o->pending; o->pending=b->next; batch_free(b); }
    while (o->resources) { unified_event_resource *r=o->resources; o->resources=r->next; resource_free(r); }
    while (o->components) { unified_component_owner *c=o->components; o->components=c->next; component_free(c); }
    free(o->links); free(o); *slot=NULL; return true;
}
static bool blob(qa_source_save_io *io,qa_buffer *b)
{
    size_t n=b->size;
    if (!qa_source_save_count(io,&n,SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (io->offset>io->input.size || n>io->input.size-io->offset)
            return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Truncated unified event child capsule");
        b->data=n?malloc(n):NULL; b->size=n;
        if (n && !b->data) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring unified event child bytes");
    }
    return qa_source_save_bytes(io,b->data,n);
}
static bool document(qa_source_save_io *io,qa_unified_document_kind kind,qa_unified_document **d)
{
    qa_buffer bytes={0}; bool okay;
    if (io->direction==QA_SOURCE_SAVE_WRITE) {
        okay=qa_unified_document_encode(*d,&bytes,io->error) && blob(io,&bytes);
    } else {
        okay=blob(io,&bytes) && qa_unified_document_decode(kind,(qa_bytes){bytes.data,bytes.size},d,io->error);
    }
    qa_buffer_free(&bytes); return okay;
}
static bool fields(qa_source_save_io *io,frontend_unified_events *o,const frontend_unified_event_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[5]={'Q','U','E','V','6'};
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    uint32_t physical=d->physical_seat,epoch=o->epoch;
    uint64_t audio_owner=o->options.audio_owner;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QUEV6",sizeof(magic)) ||
        !qa_source_save_u32(io,&physical) || physical!=d->physical_seat ||
        !qa_source_save_u32(io,&epoch) || epoch!=o->epoch ||
        !qa_source_save_u64(io,&audio_owner) || audio_owner!=o->options.audio_owner ||
        !qa_source_save_bool(io,&o->has_frame) || !qa_source_save_u64(io,&o->frame) ||
        !qa_source_save_f64(io,&o->seconds) || !isfinite(o->seconds) ||
        (!o->has_frame && (o->frame || o->seconds != 0.0)) ||
        !qa_source_save_f64(io,&o->presentation_sequence) || !sequence_value(o->presentation_sequence,-1) ||
        !qa_source_save_f64(io,&o->simulation_sequence) || !sequence_value(o->simulation_sequence,-1) ||
        !qa_source_save_bool(io,&o->failed)) return false;
    if(!qa_source_save_bool(io,&o->prepared))return false;
    if(o->prepared){
        if(!qa_source_save_u64(io,&o->prepared_frame) || !qa_source_save_f64(io,&o->prepared_seconds) || !isfinite(o->prepared_seconds) ||
            (o->has_frame && o->prepared_frame<=o->frame) || !document(io,QA_UNIFIED_FRAME_DOCUMENT,&o->prepared_document))return false;
        const qa_unified_document *doc=o->prepared_document;const qa_json_document *j=qa_unified_document_json(doc);
        qa_json_id root=qa_unified_document_root(doc),f=field(j,field(j,field(j,root,"output"),"snapshot"),"frame"),t=field(j,f,"time");
        uint64_t actual_epoch,n;double seconds;
        if(!qa_json_u64(j,field(j,root,"epoch"),&actual_epoch,io->error) || actual_epoch!=o->epoch ||
            !qa_json_u64(j,field(j,f,"frame"),&n,io->error) || n!=o->prepared_frame || !scalar(doc,field(j,t,"value"),&seconds,io->error))return false;
        if(qa_json_string_equal(j,field(j,t,"kind"),"milliseconds"))seconds/=1000;
        else if(!qa_json_string_equal(j,field(j,t,"kind"),"seconds"))return false;
        if(seconds!=o->prepared_seconds)return false;
    }
    bool capabilities[]={o->options.validate!=NULL,o->options.presentation!=NULL,
        o->options.simulation!=NULL,o->options.audio_actor!=NULL};
    for (size_t i=0;i<sizeof(capabilities)/sizeof(*capabilities);++i) {
        bool expected=capabilities[i];
        if (!qa_source_save_bool(io,capabilities+i) || capabilities[i]!=expected) return false;
    }
    size_t count=0;
    for (unified_component_owner *c=o->components;c;c=c->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(unified_component_owner))) return false;
    if (reading && count>(io->input.size-io->offset)/18) return false;
    unified_component_owner *c=o->components,**component_tail=&o->components;
    for (size_t i=0;i<count;++i) {
        qa_unified_document *identity=reading?NULL:c->identity;
        qa_buffer content={0};
        if (!reading) { content.data=(uint8_t *)c->content; content.size=strlen(c->content); }
        if (!document(io,QA_UNIFIED_CHECKPOINT,&identity) || !blob(io,&content)) {
            if (reading) { qa_unified_document_destroy(identity); qa_buffer_free(&content); } return false;
        }
        if (reading) {
            bool okay=content.size && !memchr(content.data,0,content.size) && content.size<SIZE_MAX;
            char *name=okay?malloc(content.size+1):NULL;
            if (name) { memcpy(name,content.data,content.size); name[content.size]=0; }
            unified_component_owner *next=NULL;
            okay=name && component_read(o,identity,name,&next,io->error);
            free(name); qa_buffer_free(&content); qa_unified_document_destroy(identity);
            if (!okay) return false;
            if (component_find(o,next->provider,next->generation)) { component_free(next);
                return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Component ledger repeats a reliable owner token"); }
            *component_tail=next; component_tail=&next->next; c=next;
        }
        if (!qa_source_save_bool(io,&c->retired) || !qa_source_save_bool(io,&c->cancelled) || (c->retired && c->cancelled)) return false;
        if (!reading) c=c->next;
    }
    count=0;
    for (unified_event_resource *r=o->resources;r;r=r->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(unified_event_resource))) return false;
    if (reading && count>(io->input.size-io->offset)/9) return false;
    unified_event_resource *r=o->resources,**resource_tail=&o->resources;
    for (size_t i=0;i<count;++i) {
        qa_unified_document *key=reading?NULL:r->key;
        if (!document(io,QA_UNIFIED_CHECKPOINT,&key)) { if (reading) qa_unified_document_destroy(key); return false; }
        if (reading) {
            unified_event_resource *next=NULL;
            bool okay=resource_read(o,key,&next,io->error); qa_unified_document_destroy(key);
            if (!okay) return false;
            for (unified_event_resource *previous=o->resources;previous;previous=previous->next)
                if (!strcmp(previous->id,next->id)) { resource_free(next);
                    return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Unified resource dictionary repeats an identity"); }
            *resource_tail=next; resource_tail=&next->next; r=next;
        }
        bool has_sample=r->asset!=NULL;
        if (!qa_source_save_bool(io,&has_sample)) return false;
        if (has_sample) {
            uint64_t id=0;
            if (!reading) {
                const qa_resource *resource=qa_audio_asset_resource(r->asset);
                if (!resource || qa_audio_asset_family(r->asset)!=r->family ||
                    !qa_sha256_equal(qa_resource_digest(resource),qa_resource_digest(r->resource)) ||
                    qa_resource_bytes(resource).size!=qa_resource_bytes(r->resource).size)
                    return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Unified retained sound no longer matches its declared resource");
            }
            if ((!reading && !refs->asset_encode(refs->context,r->asset,&id,io->error)) ||
                !qa_source_save_u64(io,&id) || !id) return false;
            if (reading) {
                const qa_audio_asset *asset=NULL;
                if (!refs->asset_decode(refs->context,id,&asset,io->error) || !asset ||
                    qa_audio_asset_family(asset)!=r->family) return false;
                const qa_resource *resource=qa_audio_asset_resource(asset);
                if (!qa_sha256_equal(qa_resource_digest(resource),qa_resource_digest(r->resource)) ||
                    qa_resource_bytes(resource).size!=qa_resource_bytes(r->resource).size) return false;
                r->asset=qa_audio_asset_retain((qa_audio_asset *)asset); if (!r->asset) return false;
            }
        }
        if (!reading) r=r->next;
    }
    count=o->link_count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(unified_event_link))) return false;
    if (reading && count>(io->input.size-io->offset)/9) return false;
    if (reading && count) {
        o->links=calloc(count,sizeof(*o->links));
        if (!o->links) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring unified message links");
        o->link_capacity=count; o->link_count=count;
    }
    for (size_t i=0;i<count;++i) {
        unified_event_link *l=o->links+i;
        if (!qa_source_save_f64(io,&l->sequence) || !sequence_value(l->sequence,0) ||
            l->sequence>o->presentation_sequence || (i && l->sequence<=o->links[i-1].sequence) ||
            !qa_source_save_bool(io,&l->mirrored)) return false;
    }
    count=0; for (unified_event_batch *b=o->pending;b;b=b->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(unified_event_batch))) return false;
    if (reading && count>(io->input.size-io->offset)/40) return false;
    unified_event_batch *b=o->pending;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            b=calloc(1,sizeof(*b));
            if (!b) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring pending reliable events");
            *o->tail=b; o->tail=&b->next;
        }
        if (!qa_source_save_u64(io,&b->frame) ||
            !document(io,QA_UNIFIED_EVENTS_DOCUMENT,&b->presentation) ||
            !document(io,QA_UNIFIED_CHECKPOINT,&b->simulation) ||
            !qa_source_save_count(io,&b->presentation_at,65536) ||
            !qa_source_save_count(io,&b->simulation_at,65536)) return false;
        const qa_json_document *pj=qa_unified_document_json(b->presentation),*sj=qa_unified_document_json(b->simulation);
        size_t pn=qa_json_size(pj,qa_unified_document_root(b->presentation));
        size_t sn=qa_json_size(sj,qa_unified_document_root(b->simulation));
        if (b->presentation_at>pn || b->simulation_at>sn ||
            (b->presentation_at<pn && b->simulation_at) ||
            !rows_valid(o,b->presentation,false,!reading,io->error) ||
            !rows_valid(o,b->simulation,true,!reading,io->error)) return false;
        if (!reading) b=b->next;
    }
    qa_buffer hud={0}; bool okay;
    if (!reading) okay=qa_hud_checkpoint(o->hud,NULL,&hud,io->error) && blob(io,&hud);
    else {
        qa_hud_options h=hud_options(o);
        okay=blob(io,&hud) && qa_hud_restore((qa_bytes){hud.data,hud.size},&h,NULL,&o->hud,io->error);
    }
    qa_buffer_free(&hud); return okay;
}
bool frontend_unified_events_assets_read(const frontend_unified_events *o,qa_audio_asset ***out,size_t *count,qa_error *e)
{
    if (!o || !out || *out || !count || !frontend_unified_events_checkpoint_ready(o)) return false;
    size_t n=0; for (unified_event_resource *r=o->resources;r;r=r->next) if (r->asset) ++n;
    if (n>SIZE_MAX/sizeof(**out)) return false;
    qa_audio_asset **rows=n?malloc(n*sizeof(*rows)):NULL;
    if (n && !rows) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Reading Unified retained sound assets");
    size_t i=0; for (unified_event_resource *r=o->resources;r;r=r->next) if (r->asset) rows[i++]=r->asset;
    *out=rows; *count=n; return true;
}
bool frontend_unified_events_checkpoint(frontend_unified_events *o,const frontend_unified_event_refs *refs,qa_buffer *out,qa_error *e)
{
    if (!o || !refs || !refs->asset_encode || !out || out->data || out->size || !frontend_unified_events_checkpoint_ready(o))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event capture needs a returned owner and empty output");
    if (!frontend_remote_unified_checkpoint_current(o->replica,e)) return false;
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && fields(&io,o,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_unified_events_restore(qa_frontend *f,frontend_remote_unified *replica,frontend_unified_media *media,
    const frontend_unified_event_options *opts,const frontend_unified_event_refs *refs,qa_bytes bytes,frontend_unified_events **out,qa_error *e)
{
    if (!refs || !refs->asset_decode || !out || *out) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event candidate needs actual asset graph references");
    frontend_unified_events *o=allocate(f,replica,media,opts,e);
    if (!o) return false;
    o->families_ready=false;
    if (!frontend_remote_unified_checkpoint_current(replica,e)) { free(o); return false; }
    *out=o;
    qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,o,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!okay) {
        qa_error ignored={0};
        frontend_unified_events_frame_abort(o);frontend_unified_events_destroy(out,&ignored);
        if (e && e->code==QA_OK) frontend_unified_fail(e,QA_ERROR_FORMAT,"Invalid unified event ledger");
        return false;
    }
    return true;
}
bool frontend_unified_events_restore_finish(frontend_unified_events *o,qa_error *e)
{
    if (!o || !o->frontend->source_restoring || !frontend_unified_events_checkpoint_ready(o) ||
        !frontend_remote_unified_checkpoint_current(o->replica,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event import needs its returned family owners");
    for (unified_event_batch *b=o->pending;b;b=b->next)
        if (!rows_valid(o,b->presentation,false,true,e) || !rows_valid(o,b->simulation,true,true,e)) return false;
    o->families_ready=true; return true;
}
void frontend_unified_events_adopt(frontend_unified_events *o)
{ if (o && frontend_unified_events_checkpoint_ready(o)) o->owns_audio=true; }
uint64_t frontend_unified_events_audio_owner(const frontend_unified_events *o)
{ return o?o->options.audio_owner:QA_AUDIO_NO_OWNER; }
bool frontend_unified_events_audio_actor(frontend_unified_events *o,qa_actor_id actor_id,uint64_t *id,qa_error *e)
{
    if (!o || !id) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified audio identity needs its actual CLIENT owner");
    if (!actor_id.registry) { *id=QA_AUDIO_NO_ACTOR; return true; }
    qa_saved_actor_id wire;
    if (!frontend_remote_unified_wire_actor(o->replica,actor_id,&wire))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified audio actor is outside its private received ledger");
    return o->options.audio_actor(o->options.context,actor_id,id,e);
}
bool frontend_unified_events_sound_path(frontend_unified_events *o,const char *content,const char *path,
    qa_actor_id actor_id,qa_vec3 origin,double ms,int32_t channel,float volume,float attenuation,double delay,qa_error *e)
{
    if (!o || !content || !path || !isfinite(ms) || !isfinite(delay) || !execution_current(o,e)) return false;
    qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *bank;
    qa_vfs *files; const qa_product *product;
    if (!qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&product,e) ||
        !frontend_unified_media_bank(o->media,content,&images,&materials,&fonts,&bank,e)) return false;
    qa_audio_family family=product->family==QA_GAME_Q1?QA_AUDIO_Q1:product->family==QA_GAME_Q2?QA_AUDIO_Q2:QA_AUDIO_Q3;
    qa_audio_asset *asset=NULL;
    if (!qa_audio_bank_register(bank,path,family,&asset,e)) return false;
    if (!asset) return true; /* Retains the genuine bank's optional-miss policy. */
    uint64_t audio;
    bool okay=frontend_unified_events_audio_actor(o,actor_id,&audio,e) && current(o,e);
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (okay && !o->frontend->audio) okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT sound has no output engine");
    if (okay) {
        int32_t signed_tick;
        qa_audio_play play={.sample=qa_audio_asset_sample(asset),.asset=asset,
            .resource_id=qa_resource_id(qa_audio_asset_resource(asset)),.name=path,.family=family,
            .actor=audio,.owner=o->options.audio_owner,.audience=d->physical_seat,.origin_kind=QA_AUDIO_FIXED,
            .origin=origin,.channel=channel,.volume=volume,.attenuation=attenuation,.delay_seconds=delay,
            .has_server_time=true,.server_milliseconds=ms};
        okay=qa_audio_source_milliseconds(ms,&signed_tick,e) &&
            qa_audio_engine_play(o->frontend->audio,&play,signed_tick,e);
    }
    qa_audio_asset_release(asset); return okay;
}
bool frontend_unified_events_sound_mirrored(frontend_unified_events *o,const qa_unified_document *doc,
    qa_json_id row,bool *out,qa_error *e)
{
    if (!o || !doc || !out) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Sound linkage needs its actual delivery record");
    const qa_json_document *j=qa_unified_document_json(doc);
    qa_json_id p=field(j,row,"event"),kind=field(j,p,"kind");
    if (!qa_json_string_equal(j,kind,"sound") && !qa_json_string_equal(j,kind,"ambient")) { *out=false; return true; }
    unified_event_batch *b=o->pending;
    while (b && b->presentation!=doc) b=b->next;
    if (!b) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Sound linkage is not in the retained reliable delivery");
    qa_buffer path={0},content={0}; double seconds,channel=0; float volume,attenuation; qa_vec3 origin;
    qa_actor_id a={0}; bool ambient=qa_json_string_equal(j,kind,"ambient");
    bool okay=text(doc,field(j,p,"path"),&path,e) && text(doc,field(j,row,"content"),&content,e) &&
        scalar(doc,field(j,row,"seconds"),&seconds,e) && vector(doc,field(j,p,"origin"),&origin,e) &&
        real(doc,field(j,p,"volume"),&volume,e) && real(doc,field(j,p,"attenuation"),&attenuation,e);
    if (okay && !ambient) {
        okay=actor(o,doc,field(j,p,"actor"),&a,e);
        qa_json_id c=field(j,p,"channel");
        static const char *const names[]={"auto","weapon","voice","item","body"};
        bool named=false;
        for (size_t i=0;i<5;++i) if (qa_json_string_equal(j,c,names[i])) { channel=(double)i; named=true; break; }
        if (okay && !named) okay=scalar(doc,c,&channel,e);
    }
    bool found=false;
    const qa_json_document *sj=qa_unified_document_json(b->simulation); qa_json_id root=qa_unified_document_root(b->simulation);
    for (size_t i=0;okay && !found && i<qa_json_size(sj,root);++i) {
        qa_json_id s=qa_json_at(sj,root,i),payload=field(sj,s,"payload");
        if (!qa_json_string_equal(sj,field(sj,payload,"kind"),"sound")) continue;
        qa_buffer id={0}; okay=text(b->simulation,field(sj,payload,"resource"),&id,e);
        unified_event_resource *r=o->resources;
        while (okay && r && strcmp(r->id,(const char *)id.data)) r=r->next;
        qa_buffer_free(&id); if (!okay) break;
        if (!r) { okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Linked sound lost its declared resource"); break; }
        const qa_json_document *kj=qa_unified_document_json(r->key); qa_json_id kr=qa_unified_document_root(r->key);
        if (!qa_json_string_equal(kj,field(kj,kr,"content"),(const char *)content.data)) continue;
        qa_buffer resource_path={0}; okay=text(r->key,field(kj,kr,"path"),&resource_path,e);
        bool same_path=okay && (!strcmp((const char *)resource_path.data,(const char *)path.data) ||
            (!strncmp((const char *)resource_path.data,"sound/",6) && !strcmp((const char *)resource_path.data+6,(const char *)path.data)) ||
            (path.size && path.data[0]=='#' && !strcmp((const char *)resource_path.data,(const char *)path.data+1)));
        qa_buffer_free(&resource_path); if (!okay || !same_path) continue;
        double time,c; float v,at; qa_vec3 position; qa_actor_id sa;
        qa_json_id t=field(sj,s,"time");
        okay=scalar(b->simulation,field(sj,t,"value"),&time,e) && scalar(b->simulation,field(sj,payload,"channel"),&c,e) &&
            real(b->simulation,field(sj,payload,"volume"),&v,e) && real(b->simulation,field(sj,payload,"attenuation"),&at,e) &&
            vector(b->simulation,field(sj,payload,"origin"),&position,e) && actor(o,b->simulation,field(sj,payload,"actor"),&sa,e);
        if (qa_json_string_equal(sj,field(sj,t,"kind"),"milliseconds")) time/=1000;
        if (okay) found=time==seconds && c==channel && v==volume && at==attenuation &&
            position.x==origin.x && position.y==origin.y && position.z==origin.z && qa_actor_id_equal(sa,a);
    }
    qa_buffer_free(&path); qa_buffer_free(&content); if (okay) *out=found; return okay;
}
bool frontend_unified_events_sound_loop_path(frontend_unified_events *o,const char *content,const char *path,
    qa_actor_id actor_id,qa_vec3 origin,qa_vec3 velocity,double ms,int32_t channel,
    float volume,float attenuation,int32_t frame_number,bool persistent,qa_error *e)
{
    if (!o || !content || !path || !isfinite(ms) || !qa_vec_finite(origin) || !qa_vec_finite(velocity) ||
        !isfinite(volume) || !isfinite(attenuation) || !execution_current(o,e)) return false;
    qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *bank;
    qa_vfs *files; const qa_product *product;
    if (!qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&product,e) ||
        !frontend_unified_media_bank(o->media,content,&images,&materials,&fonts,&bank,e)) return false;
    qa_audio_family family=product->family==QA_GAME_Q1?QA_AUDIO_Q1:product->family==QA_GAME_Q2?QA_AUDIO_Q2:QA_AUDIO_Q3;
    qa_audio_asset *asset=NULL;
    if (!qa_audio_bank_register(bank,path,family,&asset,e)) return false;
    if (!asset) return true;
    uint64_t audio; bool okay=frontend_unified_events_audio_actor(o,actor_id,&audio,e) && current(o,e);
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (okay && !o->frontend->audio) okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified loop has no output engine");
    if (okay) {
        qa_audio_loop loop={.sound={.sample=qa_audio_asset_sample(asset),.asset=asset,
            .resource_id=qa_resource_id(qa_audio_asset_resource(asset)),.name=path,.family=family,
            .actor=audio,.owner=o->options.audio_owner,.audience=d->physical_seat,.origin_kind=QA_AUDIO_FIXED,
            .origin=origin,.channel=channel,.volume=volume,.attenuation=attenuation,
            .has_server_time=true,.server_milliseconds=ms},.velocity=velocity,.frame_number=frame_number,.persistent=persistent};
        okay=qa_audio_engine_loop(o->frontend->audio,&loop,e);
    }
    qa_audio_asset_release(asset); return okay;
}
bool frontend_unified_events_sound_stop_loop(frontend_unified_events *o,qa_actor_id actor_id,qa_error *e)
{
    uint64_t audio;
    if (!o || !o->frontend->audio || !current(o,e) || !frontend_unified_events_audio_actor(o,actor_id,&audio,e)) return false;
    return qa_audio_engine_stop_loop(o->frontend->audio,audio,o->options.audio_owner,
        frontend_remote_unified_domain_read(o->replica)->physical_seat,e);
}

bool frontend_unified_events_frame_restore_bind(frontend_unified_events *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || !o->frontend->source_restoring || !frontend_unified_events_checkpoint_ready(o) || !current(o,e))return false;
    if(!o->prepared)return d==o->replica->prepared_frame;
    if(!d || !o->prepared_document || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT)return false;
    return frontend_unified_document_restore_bind(&o->prepared_document,d,true,e) &&
        frontend_unified_events_frame_ready(o,d,e);
}
