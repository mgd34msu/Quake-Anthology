#include "remote_unified_components_private.h"
#include "remote_unified_private.h"
#include "../application/guest_q3_components.h"
#include "qa/binary.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool q3remote_component_fail(qa_error *e,qa_status status,const char *text)
{ qa_error_set(e,status,0,"%s",text); return false; }
bool q3remote_component_player_finite(const qa_q3_player *p)
{
    const float *vectors[]={p->origin,p->velocity,p->grapplePoint,p->viewangles};
    for(size_t i=0;i<4;++i) for(size_t k=0;k<3;++k) if(!isfinite(vectors[i][k])) return false;
    return true;
}
static char *copy_text(const char *text)
{
    size_t size=strlen(text)+1; char *copy=malloc(size);
    if(copy) memcpy(copy,text,size);
    return copy;
}
static bool runtime_qualify(remote_component_state *s,qa_error *e)
{
    qa_json_document *d=NULL;
    if(!qa_json_parse(s->mod->declaration,&d,e)) return false;
    qa_json_id runtime=qa_json_get(d,qa_json_get(d,qa_json_root(d),"presentation"),"runtime");
    bool ok=qa_json_string_equal(d,runtime,s->player_events?"qvm-player-events":"qvm-scene");
    qa_json_destroy(d);
    return ok||q3remote_component_fail(e,QA_ERROR_FORMAT,"Component runtime differs from its actual held declaration");
}
bool q3remote_component_state_qualify(frontend_unified_components *owner,remote_component_state *s,qa_error *e)
{
    if(!owner||!s->provider||!s->owner_generation||s->owner_generation!=s->generation||
        (s->abi!=QA_QVM_Q3_MODERN&&s->abi!=QA_QVM_Q3_116N))
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component lacks its actual identity and ABI");
    s->presentation_owner=(qa_source_owner){s->provider,s->owner_generation};
    const qa_recipe_choices *choices=qa_executable_recipe_choices(owner->recipe);
    for(size_t i=0;i<choices->mod_count;++i) {
        const qa_launch_mod_selection *selected=choices->mods+i;
        if(!selected->enabled||!s->identity.provider||strcmp(s->identity.provider,selected->instance)) continue;
        for(size_t k=0;k<qa_executable_recipe_provider_count(owner->recipe);++k) {
            const qa_recipe_provider *provider=qa_executable_recipe_provider(owner->recipe,k);
            if(!provider||strcmp(provider->selection.instance,selected->instance)) continue;
            const qa_catalog_mod *mod=qa_catalog_mod_find(qa_executable_recipe_catalog(owner->recipe),selected->component);
            const qa_product *product=mod?qa_catalog_product(qa_executable_recipe_catalog(owner->recipe),mod->product):NULL;
            if(!mod||mod->unavailable||!product) continue;
            qa_unified_component_identity expected={0};
            bool ok=application_unified_component_identity_create(mod,product,selected->instance,&expected,e);
            bool equal=ok&&qa_unified_component_identity_equal(&s->identity,&expected);
            qa_unified_component_identity_dispose(&expected);
            if(!ok) return false;
            if(!equal) continue;
            s->mod=mod; s->provider_row=provider;
            return qa_strings_intern_cstr(owner->replica->strings,product->identity,&s->content_name,e)&&runtime_qualify(s,e);
        }
    }
    return q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component has no genuine admitted recipe identity");
}
void q3remote_component_state_free(remote_component_state *s)
{
    for(size_t i=0;i<s->command_count;++i) {
        free((void *)s->commands[i].text); qa_command_tokens_free(s->arguments+i);
    }
    qa_unified_component_identity_dispose(&s->identity); memset(s,0,sizeof(*s));
}
static void *frame_allocate(void *lease,size_t bytes,size_t alignment,qa_error *e)
{ return qa_unified_frame_lease_alloc(lease,1,bytes,alignment,e); }
static bool frame_source_copy(const remote_component_state *source,remote_component_state *out,
    qa_unified_frame_lease *lease,qa_error *e)
{
    out->game_state=source->game_state;
    out->game_state_revision=source->game_state_revision;
    out->command_sequence=source->command_sequence;
    for(size_t i=0;i<source->command_count;++i) {
        out->commands[i]=source->commands[i];
        if(lease) {
            size_t bytes=strlen(source->commands[i].text)+1;
            char *text=qa_unified_frame_lease_alloc(lease,bytes,1,1,e);
            if(text) memcpy(text,source->commands[i].text,bytes);
            out->commands[i].text=text;
        } else out->commands[i].text=copy_text(source->commands[i].text);
        ++out->command_count;
        if(!out->commands[i].text||!qa_command_tokens_copy(source->arguments+i,out->arguments+i,lease?frame_allocate:NULL,lease,e))
            return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining received frame command history");
        out->commands[i].arguments=out->arguments+i;
    }
    return true;
}
static bool command_text(const qa_unified_control_arguments *args,char **out,qa_command_tokens *tokens,qa_error *e)
{
    size_t size=1;
    for(size_t i=0;i<args->count;++i) size+=strlen(args->values[i])+3;
    char *joined=malloc(size),*args_text=malloc(size);
    if(!joined||!args_text) {
        free(joined); free(args_text);
        return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining exact component command argv");
    }
    char *p=joined;
    for(size_t i=0;i<args->count;++i) {
        size_t n=strlen(args->values[i]);
        if(i) *p++=' ';
        *p++='"'; memcpy(p,args->values[i],n); p+=n; *p++='"';
    }
    *p=0; p=args_text;
    for(size_t i=1;i<args->count;++i) {
        if(i>1) *p++=' ';
        size_t n=strlen(args->values[i]); memcpy(p,args->values[i],n); p+=n;
    }
    *p=0;
    qa_command_tokens borrowed={.count=args->count,.values=args->values,.args_text=args_text};
    bool okay=qa_command_tokens_copy(&borrowed,tokens,NULL,NULL,e); free(args_text);
    if(!okay) { free(joined); return false; }
    *out=joined; return true;
}
bool q3remote_component_state_read(frontend_unified_components *owner,const qa_unified_component_q3 *row,
    const remote_component *previous,remote_component_state *out,qa_error *e)
{
    remote_component_state s={.owner_generation=row->owner.generation,.generation=row->generation,
        .abi=row->abi,.player_events=!row->scene,.game_state_revision=row->game_state_revision};
    bool ok=qa_strings_intern_cstr(owner->replica->strings,row->owner.provider,&s.provider_name,e);
    s.provider=(char *)qa_strings_cstr(owner->replica->strings,s.provider_name);
    if(ok) ok=qa_unified_component_identity_clone(&row->identity,&s.identity,e);
    s.presentation_owner=(qa_source_owner){s.provider,s.owner_generation};
    if(ok&&previous) {
        ok=s.owner_generation==previous->state.owner_generation&&s.generation==previous->state.generation&&
            s.abi==previous->state.abi&&s.player_events==previous->state.player_events&&
            qa_unified_component_identity_equal(&s.identity,&previous->state.identity);
        if(ok) { s.mod=previous->state.mod; s.provider_row=previous->state.provider_row;
            s.content_name=previous->state.content_name; }
    } else if(ok) ok=q3remote_component_state_qualify(owner,&s,e);
    int32_t base=row->command_base;
    if(ok) ok=!previous||base==previous->state.command_sequence;
    if(ok&&!row->game_state) {
        if(!previous||s.game_state_revision!=previous->state.game_state_revision) ok=false;
        else s.game_state=previous->state.game_state;
    } else if(ok) {
        ok=!previous||s.game_state_revision>=previous->state.game_state_revision;
        if(ok) s.game_state=*row->game_state;
    }
    size_t count=row->command_count,keep=previous?previous->state.command_count:0;
    if(keep>64-count) keep=64-count;
    for(size_t i=0;ok&&i<keep;++i) {
        const application_q3_scene_command *old=previous->state.commands+previous->state.command_count-keep+i;
        s.commands[i]=*old; s.commands[i].text=copy_text(old->text); ++s.command_count;
        if(!s.commands[i].text) ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining reached remote command history");
        if(ok) ok=qa_command_tokens_copy(previous->state.arguments+previous->state.command_count-keep+i,s.arguments+i,NULL,NULL,e);
    }
    for(size_t i=0;ok&&i<count;++i) {
        application_q3_scene_command *command=s.commands+s.command_count;
        ok=command_text(&row->commands[i].arguments,(char **)&command->text,s.arguments+s.command_count,e);
        if(ok) { command->sequence=row->commands[i].sequence; command->addressed=row->commands[i].arguments.count!=0; ++s.command_count; }
    }
    s.command_sequence=base+(int32_t)count;
    if(!ok) { q3remote_component_state_free(&s); return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote reliable component history has a gap"); }
    *out=s;
    for(size_t i=0;i<out->command_count;++i) out->commands[i].arguments=out->arguments+i;
    return true;
}
void q3remote_component_frame_free(remote_component_frame *f)
{
    if(!f) return;
    qa_unified_frame_lease *lease=f->lease;
    if(!lease) {
        if(!f->packet) free((void *)f->snapshot.entities);
        free(f->actors); q3remote_component_state_free(&f->source);
    }
    qa_unified_document_destroy(f->packet);
    if(lease) qa_unified_frame_lease_release(lease); else free(f);
}
static bool module_matches(const qa_unified_mod_identity *a,const qa_unified_mod_identity *b)
{
    return !strcmp(a->id,b->id)&&!strcmp(a->artifact_path,b->artifact_path);
}
static bool player_event_read(frontend_unified_components *o,const qa_unified_presentation_event *row,
    remote_component **target,remote_component_event *out,bool *handled,qa_error *e)
{
    *handled=false; *target=NULL;
    if(!o||!row||!frontend_unified_components_current(o)||o->busy)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component event lost its actual replica roster");
    if(row->payload.kind!=QA_UNIFIED_PRESENTATION_Q3||row->payload.value.q3.kind!=QA_UNIFIED_Q3_PLAYER_EVENT) return true;
    const qa_unified_q3_event *event=&row->payload.value.q3;
    qa_string_id provider=qa_strings_find(o->replica->strings,
        (qa_bytes){(const uint8_t *)row->provider,strlen(row->provider)});
    qa_string_id content=qa_strings_find(o->replica->strings,
        (qa_bytes){(const uint8_t *)row->content,strlen(row->content)});
    remote_component *r=NULL; bool known=false;
    for(size_t i=0;!r&&i<q3remote_component_physical_count(o);++i) {
        remote_component *candidate=q3remote_component_physical_at(o,i);
        if(!candidate||!candidate->state.player_events||provider!=candidate->state.provider_name) continue;
        known=true;
        if(module_matches(&event->module,&candidate->state.identity.module)&&content==candidate->state.content_name&&event->abi==candidate->state.abi) r=candidate;
    }
    if(!r) return !known||q3remote_component_fail(e,QA_ERROR_FORMAT,"Component event has no actual admitted gameplay identity");
    *handled=true;
    if(row->owner.provider&&(qa_strings_find(o->replica->strings,
        (qa_bytes){(const uint8_t *)row->owner.provider,strlen(row->owner.provider)})!=r->state.provider_name||!row->owner.generation))
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component event lost its genuine presentation token domain");
    const qa_unified_frame *frame=qa_unified_document_frame(frontend_remote_unified_frame(o->replica));
    if(row->recipient.registry) {
        qa_actor_id receiver,viewer; uint32_t slot;
        if(!frontend_remote_unified_source_actor(o->replica,frame,row->recipient,false,&receiver,e)||
            !frontend_remote_unified_player(o->replica,&viewer,&slot)) return false;
        if(!qa_actor_id_equal(receiver,viewer)) return true;
    }
    application_q3_scene_player_event *v=&out->value;
    if(!frontend_remote_unified_source_actor(o->replica,frame,event->actor,false,&v->actor,e)) return false;
    out->sequence=row->sequence; v->event=event->event; v->parameter=event->parameter;
    v->time_ms=event->time_ms; v->player=event->player; v->external=event->external;
    v->source_sequence=event->source_sequence; v->origin=event->origin;
    *target=r->retired?NULL:r; return true;
}
bool frontend_unified_components_player_event_validate(frontend_unified_components *o,const qa_unified_presentation_event *row,
    bool *handled,qa_error *e)
{
    if(!handled) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component event requires a reached-handler result");
    remote_component *target=NULL; remote_component_event event={0};
    return player_event_read(o,row,&target,&event,handled,e);
}
bool frontend_unified_components_player_event(frontend_unified_components *o,const qa_unified_presentation_event *row,
    bool *handled,qa_error *e)
{
    if(!handled) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component event requires a reached-handler result");
    remote_component *target=NULL; remote_component_event event={0};
    if(!player_event_read(o,row,&target,&event,handled,e)) return false;
    if(!target||(target->event_present&&event.sequence<=target->event_sequence)) return true;
    size_t slot;
    remote_component_event *owned=qa_pool_take(&o->event_records,&slot);
    if(!owned) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining reached original component player event");
    *owned=event; owned->slot=slot; remote_component_event **tail=&target->events;
    while(*tail) tail=&(*tail)->next;
    *tail=owned; target->event_sequence=event.sequence; target->event_present=true; return true;
}
bool q3remote_component_frame_read(frontend_unified_components *o,remote_component *row,const qa_unified_document *d,
    const qa_unified_component_source *source,remote_component_frame **out,qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    qa_unified_frame_lease *lease=frame->lease;
    if(lease&&!qa_unified_frame_lease_retain(lease,e)) return false;
    remote_component_frame *f=lease?qa_unified_frame_lease_alloc(lease,1,sizeof(*f),_Alignof(remote_component_frame),e):calloc(1,sizeof(*f));
    if(!f) {
        qa_unified_frame_lease_release(lease);
        return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining actual remote component frame");
    }
    f->lease=lease;
    qa_actor_id viewer={0}; uint32_t viewer_slot=0;
    bool ok=source->provider==row->state.provider_name&&source->owner_generation==row->state.owner_generation&&
        source->owner_generation==row->state.generation&&source->game_state_revision==(int64_t)row->state.game_state_revision&&
        source->abi==row->state.abi&&frontend_remote_unified_source_actor(o->replica,frame,source->viewer,false,&f->viewer,e)&&
        frontend_remote_unified_player(o->replica,&viewer,&viewer_slot)&&qa_actor_id_equal(viewer,f->viewer)&&
        qa_unified_document_retain(d,&f->packet,e);
    if(ok) f->snapshot=source->snapshot;
    size_t count=source->binding_count; bool has_viewer=false;
    if(ok&&count) { f->actors=lease?qa_unified_frame_lease_alloc(lease,count,sizeof(*f->actors),_Alignof(application_q3_scene_actor),e):calloc(count,sizeof(*f->actors)); if(!f->actors) ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining real replica actor bindings"); }
    for(size_t i=0;ok&&i<count;++i) {
        const qa_unified_component_binding *binding=source->bindings+i;
        f->actors[i].slot=binding->slot; f->actors[i].owned=binding->owned;
        ok=frontend_remote_unified_source_actor(o->replica,frame,binding->actor,false,&f->actors[i].actor,e);
        has_viewer|=ok&&binding->slot==(uint32_t)source->client_number&&!binding->owned&&qa_actor_id_equal(f->actors[i].actor,viewer);
    }
    f->has_scene=source->scene;
    if(ok) ok=has_viewer&&f->has_scene!=row->state.player_events;
    if(ok&&f->has_scene) ok=source->snapshot.server_command_number==row->state.command_sequence;
    if(ok) {
        if(!f->has_scene) f->snapshot.server_command_number=row->state.command_sequence;
        f->context=(application_q3_scene_context){.generation=source->owner_generation,.revision=source->scene_revision,
            .game_state_revision=source->game_state_revision,.time_ms=source->snapshot.server_time,.client_number=source->client_number,
            .snapshot=&f->snapshot,.actors=f->actors,.actor_count=count,.has_weapon_presented=true,.weapon_presented=source->weapon_presented};
        ok=frame_source_copy(&row->state,&f->source,lease,e);
        if(ok) {
            f->context.game_state=&f->source.game_state; f->context.commands=f->source.commands; f->context.command_count=f->source.command_count;
            if(row->frame&&source->snapshot.server_time>=row->frame->context.time_ms)
                f->context.frame_ms=(int32_t)((int64_t)source->snapshot.server_time-row->frame->context.time_ms);
        }
    }
    if(!ok) { q3remote_component_frame_free(f); return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component frame differs from reliable admission"); }
    *out=f; return true;
}
