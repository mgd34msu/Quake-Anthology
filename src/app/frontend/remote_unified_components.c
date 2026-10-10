#include "remote_unified_components_private.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "component_scene.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

size_t q3remote_component_physical_count(const frontend_unified_components *owner)
{
    size_t count=owner->count;
    for(remote_component *row=owner->retired;row;row=row->retired_next) ++count;
    return count;
}
remote_component *q3remote_component_physical_at(const frontend_unified_components *owner,size_t ordinal)
{
    if(ordinal<owner->count) return owner->rows[ordinal];
    ordinal-=owner->count; remote_component *row=owner->retired;
    while(row&&ordinal--) row=row->retired_next;
    return row;
}

bool frontend_unified_components_current(const frontend_unified_components *o)
{
    return o&&!o->closing&&!o->failed&&frontend_unified_media_current(o->media)&&
        o->recipe==frontend_remote_unified_recipe(o->replica)&&frontend_remote_unified_current(o->replica,NULL);
}
bool frontend_unified_components_retained_current(const frontend_unified_components *o)
{
    return o&&!o->closing&&!o->failed&&!o->busy&&
        o->recipe==frontend_remote_unified_recipe(o->replica)&&
        o->recipe==frontend_unified_media_recipe(o->media)&&
        (frontend_unified_media_current(o->media)||frontend_unified_media_importing(o->media))&&
        frontend_remote_unified_checkpoint_current(o->replica,NULL);
}
bool frontend_unified_components_events_bind(frontend_unified_components *o,frontend_unified_events *events,qa_error *e)
{
    if(!o||!events||o->busy||o->closing||(o->events&&o->events!=events))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component events require their actual retained owner association");
    for(size_t i=0;i<o->count;++i) {
        remote_component *row=o->rows[i]; if(!row) continue;
        const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),row->state.mod->product);
        bool active=false;
        if(!product||!frontend_unified_events_component_current(events,&row->state.presentation_owner,product->identity,&active,e)||!active)
            return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Component event token has no genuine active reliable admission");
    }
    o->events=events; return true;
}
bool frontend_unified_components_recipient_content(const frontend_unified_components *o,const char **out,
    const qa_recipe_provider **provider,bool *found,qa_error *e)
{
    if(!out||!provider||!found||!frontend_unified_components_current(o))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component recipient content lost its actual recipe");
    *out=NULL; *provider=NULL; *found=false;
    for(size_t i=0;i<o->count;++i) {
        const remote_component *r=o->rows[i]; if(!r||!r->frame) continue;
        const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),r->state.mod->product);
        if(!product||!r->state.provider_row) return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component recipient has no admitted catalog content tuple");
        *out=product->identity; *provider=r->state.provider_row; *found=true; return true;
    }
    return true;
}
bool frontend_unified_components_recipient_current(const frontend_unified_components *o,const char *content,
    const qa_recipe_provider *provider)
{
    if(!content||!provider||(!frontend_unified_components_current(o)&&!frontend_unified_components_retained_current(o))) return false;
    for(size_t i=0;i<q3remote_component_physical_count(o);++i) {
        const remote_component *r=q3remote_component_physical_at(o,i);
        if(!r||r->state.provider_row!=provider||!r->state.mod) continue;
        const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),r->state.mod->product);
        if(product&&!strcmp(product->identity,content)) return true;
    }
    return false;
}
bool frontend_unified_components_assets_encode(const frontend_unified_components *o,const qa_q3_presentation_assets *assets,
    uint64_t *identity,qa_error *e)
{
    if(!assets||!identity||(!frontend_unified_components_current(o)&&!frontend_unified_components_retained_current(o)))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component registry encode lost its retained private roster");
    for(size_t i=0;i<q3remote_component_physical_count(o);++i) {
        const remote_component *row=q3remote_component_physical_at(o,i);
        if(row&&row->assets==assets&&row->frontend_identity&&row->frontend.owner&&(row->scene||row->retired)) {
            *identity=row->frontend_identity; return true;
        }
    }
    return q3remote_component_fail(e,QA_ERROR_NOT_FOUND,"Source bank registry has no actual component frontend identity");
}
bool frontend_unified_components_assets_decode(const frontend_unified_components *o,uint64_t identity,
    qa_q3_presentation_assets **assets,qa_error *e)
{
    if(!identity||!assets||(!frontend_unified_components_current(o)&&!frontend_unified_components_retained_current(o)))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component registry decode lost its retained private roster");
    for(size_t i=0;i<q3remote_component_physical_count(o);++i) {
        const remote_component *row=q3remote_component_physical_at(o,i);
        if(row&&row->frontend_identity==identity&&row->assets&&row->frontend.owner&&(row->scene||row->retired)) {
            *assets=row->assets; return true;
        }
    }
    return q3remote_component_fail(e,QA_ERROR_NOT_FOUND,"Saved Source bank registry has no actual imported component frontend identity");
}
bool frontend_unified_components_create(qa_frontend *f,frontend_remote_unified *replica,frontend_unified_media *media,
    frontend_unified_components **out,qa_error *e)
{
    if(!f||!replica||!media||!out||*out||!frontend_unified_media_current(media)||!frontend_remote_unified_current(replica,e))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component collection requires its admitted replica and media");
    frontend_unified_components *o=calloc(1,sizeof(*o));
    if(!o) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining real remote component collection");
    o->frontend=f; o->replica=replica; o->media=media; o->recipe=frontend_remote_unified_recipe(replica); *out=o; return true;
}
bool frontend_unified_components_checkpoint_ready(const frontend_unified_components *o)
{
    if(!o) return true;
    if(o->busy) return false;
    for(size_t i=0;i<q3remote_component_physical_count(o);++i) {
        remote_component *r=q3remote_component_physical_at(o,i);
        if(r&&(r->acquired||(r->scene&&!application_q3_scene_idle(r->scene))||
            (r->frontend.owner&&!r->frontend.idle(r->frontend.owner)))) return false;
    }
    return true;
}
bool frontend_unified_components_idle(const frontend_unified_components *o)
{ return (!o||!o->prepared)&&frontend_unified_components_checkpoint_ready(o); }
static remote_component *find(frontend_unified_components *o,const char *provider)
{
    for(size_t i=0;i<o->count;++i) if(o->rows[i]&&!strcmp(o->rows[i]->state.provider,provider)) return o->rows[i];
    return NULL;
}
bool frontend_unified_components_control(frontend_unified_components *o,const qa_unified_document *d,qa_error *e)
{
    if(!frontend_unified_components_current(o)||!frontend_unified_components_idle(o)||!d||!o->events)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component update requires its returned actual collection");
    const qa_unified_control *control=qa_unified_document_control(d);
    const qa_unified_components_control *update=control&&control->kind==QA_UNIFIED_CONTROL_COMPONENTS?&control->value.components:NULL;
    if(!update||o->revision==UINT64_MAX||update->revision!=o->revision+1)
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component reliable revision is not consecutive");
    uint64_t revision=update->revision; size_t count=update->source_count;
    remote_component **next=count?calloc(count,sizeof(*next)):NULL;
    remote_component_state *states=count?calloc(count,sizeof(*states)):NULL;
    bool *created=count?calloc(count,sizeof(*created)):NULL;
    bool *admitted=count?calloc(count,sizeof(*admitted)):NULL;
    if(count&&(!next||!states||!created||!admitted)) { free(next); free(states); free(created); free(admitted); return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining genuine reliable component candidate"); }
    bool ok=true;
    for(size_t i=0;ok&&i<count;++i) {
        const qa_unified_component_q3 *row=update->sources+i;
        remote_component *previous=find(o,row->owner.provider);
        if(previous&&(previous->state.owner_generation!=row->owner.generation||previous->state.generation!=row->generation)) previous=NULL;
        ok=q3remote_component_state_read(o,row,previous,states+i,e);
        for(size_t k=0;ok&&k<i;++k) if(!strcmp(states[k].provider,states[i].provider)) ok=q3remote_component_fail(e,QA_ERROR_FORMAT,"Reliable components duplicate a genuine provider");
        if(!ok) break;
        if(previous) next[i]=previous;
        else {
            next[i]=calloc(1,sizeof(*next[i]));
            if(!next[i]) { ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining actual received component activation"); break; }
            next[i]->parent=o; created[i]=true;
        }
    }
    if(ok&&!frontend_unified_components_current(o)) ok=q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component candidate lost its admitted recipe");
    bool publishing=false;
    for(size_t i=0;ok&&i<count;++i) {
        const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),states[i].mod->product);
        ok=product&&frontend_unified_events_component_admit_created(o->events,&states[i].presentation_owner,product->identity,admitted+i,e);
    }
    publishing=ok;
    /* Retire old physical clients before replacing their activation metadata.
     * Refusal leaves both old owners and all newly created candidates reachable. */
    for(size_t i=0;ok&&i<o->count;++i) {
        bool retained=false; for(size_t k=0;k<count;++k) if(next[k]==o->rows[i]) retained=true;
        if(!retained&&o->rows[i]) {
            bool retire=false;
            for(size_t k=0;k<count;++k) if(!strcmp(states[k].provider,o->rows[i]->state.provider))
                retire=states[k].owner_generation!=o->rows[i]->state.owner_generation;
            if(retire) ok=frontend_unified_events_component_retire(o->events,&o->rows[i]->state.presentation_owner,e);
            if(ok) {
                remote_component *row=o->rows[i];
                ok=q3remote_component_retire(row,e);
                if(ok) {
                    remote_component **tail=&o->retired; while(*tail) tail=&(*tail)->retired_next;
                    *tail=row; o->rows[i]=NULL;
                }
            }
        }
    }
    if(ok) {
        for(size_t i=0;i<count;++i) {
            q3remote_component_state_free(&next[i]->state); next[i]->state=states[i]; memset(states+i,0,sizeof(*states));
            for(size_t k=0;k<next[i]->state.command_count;++k) next[i]->state.commands[k].arguments=next[i]->state.arguments+k;
        }
        free(o->rows); o->rows=next; o->count=count; o->revision=revision; next=NULL;
    } else {
        if(publishing) o->failed=true;
        qa_error original={0}; if(e) original=*e;
        for(size_t i=0;i<count;++i) if(admitted[i]) {
            qa_error cleanup={0};
            if(!frontend_unified_events_component_cancel(o->events,&states[i].presentation_owner,&cleanup)) o->failed=true;
        }
        if(e) *e=original;
        for(size_t i=0;i<count;++i) if(created[i]) { free(next[i]); next[i]=NULL; }
    }
    for(size_t i=0;i<count;++i) q3remote_component_state_free(states+i);
    free(states); free(created); free(admitted); free(next); return ok;
}
void frontend_unified_components_frame_abort(frontend_unified_component_frame **slot)
{
    if(!slot||!*slot) return;
    frontend_unified_component_frame *candidate=*slot;
    if(candidate->rows) for(size_t i=0;i<candidate->count;++i) q3remote_component_frame_free(candidate->rows[i]);
    if(candidate->owner->prepared==candidate) candidate->owner->prepared=NULL;
    qa_unified_frame_lease *lease=candidate->lease;
    qa_unified_document_destroy(candidate->owned_input);
    if (lease)qa_unified_frame_lease_release(lease);
    else {free(candidate->rows);free(candidate);}
    *slot=NULL;
}
bool frontend_unified_components_frame_prepare(frontend_unified_components *o,const qa_unified_document *d,
    frontend_unified_component_frame **out,bool *ready,qa_error *e)
{
    if(!o||!out||*out||!ready||!d||!frontend_unified_components_current(o)||!frontend_unified_components_idle(o))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote frame preparation requires its returned component roster");
    *ready=false;
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if(!frame) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component preparation requires a typed FRAME");
    const qa_unified_frame_components *frames=frame->components;
    if(!frames&&o->count) return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote frame omitted its admitted component roster");
    uint64_t revision=frames?frames->revision:0;
    if(revision<o->revision) return true;
    size_t count=frames?frames->source_count:0;
    if(revision!=o->revision||count!=o->count)
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component frame lacks reliable admission");
    qa_unified_frame_lease *lease=frame->lease;
    frontend_unified_component_frame *candidate=lease?qa_unified_frame_lease_alloc(lease,1,sizeof(*candidate),_Alignof(frontend_unified_component_frame),e):calloc(1,sizeof(*candidate));
    if(!candidate) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining actual component frame candidate");
    if (lease && !qa_unified_frame_lease_retain(lease,e))return false;
    candidate->lease=lease;candidate->owner=o;candidate->input=d;candidate->count=count;
    candidate->rows=count?(lease?qa_unified_frame_lease_alloc(lease,count,sizeof(*candidate->rows),_Alignof(remote_component_frame *),e):calloc(count,sizeof(*candidate->rows))):NULL;
    o->prepared=candidate; *out=candidate;
    if(count&&!candidate->rows) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining received component frame rows");
    bool ok=true;
    for(size_t i=0;ok&&i<count;++i) {
        const qa_unified_component_source *source=frames->sources+i; remote_component *row=NULL; size_t index=0;
        for(;index<o->count;++index) if(!strcmp(source->owner.provider,o->rows[index]->state.provider)) { row=o->rows[index]; break; }
        if(!row||candidate->rows[index]) ok=q3remote_component_fail(e,QA_ERROR_FORMAT,"Received component frame duplicated or changed its provider");
        else ok=q3remote_component_frame_read(o,row,d,source,candidate->rows+index,e);
    }
    if(ok) *ready=true;
    return ok;
}
bool frontend_unified_components_frame_ready(const frontend_unified_component_frame *c,const qa_unified_document *d,qa_error *e)
{
    if(!c||!d||c->owner->prepared!=c||c->count!=c->owner->count||
        !(frontend_unified_components_current(c->owner)||frontend_unified_components_retained_current(c->owner)))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component token lost its exact returned frame candidate");
    if(c->input!=d) {
        if(!c->owned_input) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component frame token belongs to another live invocation");
        if(!frontend_unified_document_equal(c->input,d))
            return q3remote_component_fail(e,QA_ERROR_FORMAT,"Restored component token changed its retained immutable frame");
    }
    for(size_t i=0;i<c->count;++i) if(!c->rows[i]||!qa_actors_get(frontend_remote_unified_registry(c->owner->replica),c->rows[i]->viewer))
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component candidate viewer is retired");
    return true;
}
void frontend_unified_components_frame_commit(frontend_unified_component_frame **slot)
{
    if(!slot||!*slot) return;
    frontend_unified_component_frame *c=*slot; frontend_unified_components *o=c->owner;
    for(size_t i=0;i<c->count;++i) {
        remote_component *row=o->rows[i];
        if(!row->baseline) row->baseline=c->rows[i];
        else if(row->frame!=row->baseline) q3remote_component_frame_free(row->frame);
        row->frame=c->rows[i]; c->rows[i]=NULL;
    }
    frontend_unified_components_frame_abort(slot);
}
bool frontend_unified_components_destroy(frontend_unified_components **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    frontend_unified_components *o=*slot;
    if(!frontend_unified_components_idle(o)) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component retirement retains actual guest output");
    o->closing=true;
    for(size_t i=0;i<o->count;++i) {
        if(!q3remote_component_close(o->rows+i,e)) return false;
    }
    while(o->retired) {
        remote_component *row=o->retired,*next=row->retired_next;
        if(!q3remote_component_close(&row,e)) return false;
        o->retired=next;
    }
    free(o->rows); free(o->lights); free(o); *slot=NULL; return true;
}
bool frontend_unified_components_visit(const frontend_unified_components *o,const qa_application_content_visitor *visitor,qa_error *e)
{
    if(!o) return true;
    if(!visitor||!frontend_unified_components_checkpoint_ready(o)) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Remote component graph retains entered output continuations");
    /* The actual recipe owns every acquired program and opening; frontend
     * component registry owners independently enumerate private media heaps. */
    return qa_executable_recipe_content_visit(o->recipe,visitor,e);
}
