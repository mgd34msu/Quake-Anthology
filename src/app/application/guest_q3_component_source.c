#include "guest_q3_component_source_private.h"
#include "unified_q3_events.h"
#include "control_frame.h"

static bool q3scene_fail(qa_error *,qa_status,const char *);

bool application_q3_component_source_continuation_read(const application_q3_component_source *source,
    int64_t *game_state_revision,int32_t *command_sequence,int64_t *publication_revision,qa_error *e)
{
    if(!source||!game_state_revision||!command_sequence||!publication_revision)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component continuation requires its retained source publication");
    *game_state_revision=source->game_state_revision;
    *command_sequence=source->command_sequence;
    *publication_revision=source->revision;
    return true;
}
#include "qa/json.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool q3scene_fail(qa_error *e,qa_status code,const char *message)
{ qa_error_set(e,code,0,"%s",message); return false; }

static bool current(const application_q3_component_source *s)
{
    qa_q3_host_client_context host;
    return s&&s->options.vm&&s->options.host&&s->options.current(s->options.context)&&qa_qvm_get_role(s->options.vm)==QA_QVM_GAME&&
        qa_qvm_get_abi(s->options.vm)==s->options.abi&&
        (qa_qvm_image_of(s->options.vm) == s->options.image)&&
        qa_q3_host_client_context_read(s->options.host,&host)&&host.session==s->options.session&&host.owner==s->options.owner;
}
bool application_q3_component_source_idle(const application_q3_component_source *s)
{ return s&&!s->borrows&&(!s->options.vm||qa_qvm_can_destroy(s->options.vm)); }
static void publication_free(component_source_publication *p)
{
    qa_actor_index_destroy(&p->client_rows);
    free(p->clients); free(p->entities); free(p->game_state); *p=(component_source_publication){0};
}
static bool publication_prepare(component_source_publication *p,const qa_actor_registry *registry,qa_error *e)
{
    p->game_state=calloc(1,sizeof(*p->game_state));
    p->entities=calloc(QA_Q3_ENTITIES,sizeof(*p->entities));
    p->clients=calloc(QA_Q3_ENTITIES,sizeof(*p->clients));
    if(!p->game_state || !p->entities || !p->clients)
        return q3scene_fail(e,QA_ERROR_MEMORY,"Preparing component publication storage");
    return qa_actor_index_create(&p->client_rows,registry,NULL,e) &&
        qa_id_index_reserve(&p->client_rows.retained,QA_Q3_ENTITIES,e);
}
static bool publication_clients_rebuild(component_source_publication *p,qa_error *e)
{
    qa_id_index_clear(&p->client_rows.retained);
    for(size_t i=0;i<p->client_count;++i)
        if(!qa_actor_index_remember(&p->client_rows,p->clients[i].actor,i,e)) return false;
    return true;
}
bool q3component_source_indexes_rebuild(application_q3_component_source *s,qa_error *e)
{
    qa_id_index_clear(&s->actor_slots);
    qa_actor_index_clear(&s->actor_rows);
    for(size_t i=0;i<s->actor_count;++i) {
        qa_id_index_insert(&s->actor_slots,s->actors[i].row.slot,0,i);
        if(!qa_actor_index_remember(&s->actor_rows,s->actors[i].row.actor,i,e)) return false;
    }
    return publication_clients_rebuild(&s->current,e) &&
        publication_clients_rebuild(&s->baseline,e) && publication_clients_rebuild(&s->pending,e);
}
bool application_q3_component_source_create(const application_q3_component_source_options *options,
    application_q3_component_source **out,qa_error *e)
{
    if(!options||!out||*out||!options->session||!options->owner||!options->generation||
        !options->image||!options->current||!options->information||!options->visibility.point||!options->visibility.area_bits||
        !options->visibility.areas_connected||!options->visibility.cluster_visible)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication requires its physical GAME and shared world visibility");
    application_q3_component_source *s=calloc(1,sizeof(*s));
    if(!s) return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining real component GAME publication owner");
    s->options=*options;
    qa_stamp_set_init(&s->visibility_marks,s->visibility_storage,QA_Q3_ENTITIES);
    if((options->vm||options->host)&&!current(s)) { free(s); return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication does not own its actual GAME"); }
    s->game_state=calloc(1,sizeof(*s->game_state));
    s->actors=calloc(QA_Q3_ENTITIES,sizeof(*s->actors));
    const qa_actor_registry *registry=qa_session_actors(options->session);
    if(!s->game_state || !s->actors || !qa_actor_index_create(&s->actor_rows,registry,NULL,e) ||
        !qa_id_index_reserve(&s->actor_rows.retained,QA_Q3_ENTITIES,e) ||
        !qa_id_index_reserve(&s->actor_slots,QA_Q3_ENTITIES,e) ||
        !publication_prepare(&s->current,registry,e) || !publication_prepare(&s->baseline,registry,e) ||
        !publication_prepare(&s->pending,registry,e)) {
        publication_free(&s->current);publication_free(&s->baseline);publication_free(&s->pending);
        qa_actor_index_destroy(&s->actor_rows);qa_id_index_destroy(&s->actor_slots);
        free(s->actors);free(s->game_state);free(s);
        if(e && e->code!=QA_OK) return false;
        return q3scene_fail(e,QA_ERROR_MEMORY,"Preparing component source storage");
    }
    qa_q3_gamestate_init(s->game_state); qa_qvm_image_retain((qa_qvm_image *)options->image);
    s->dirty=true; *out=s; return true;
}
bool application_q3_component_source_attach(application_q3_component_source *s,qa_qvm *vm,qa_q3_host *host,qa_error *e)
{
    if(s&&s->options.vm==vm&&s->options.host==host&&vm&&host&&!s->borrows&&current(s)) return true;
    if(!s||s->options.vm||s->options.host||!vm||!host||s->borrows)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication attach requires its unbound physical constructor");
    s->options.vm=vm; s->options.host=host;
    if(current(s)) return true;
    s->options.vm=NULL; s->options.host=NULL;
    return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication attach differs from its retained GAME");
}
static component_source_actor *at(application_q3_component_source *s,uint32_t slot)
{
    const qa_id_index_entry *entry=qa_id_index_find(&s->actor_slots,slot,0);
    return entry?s->actors+entry->value:NULL;
}
static bool actor_read(void *context,uint32_t slot,qa_actor_id *actor,bool *owned,bool *found,qa_error *e)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    *actor=(qa_actor_id){0}; *owned=*found=false;
    if(!current(s)) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component source actor mapping is stale");
    component_source_actor *row=at(s,slot);
    if(!row||!qa_actors_get(qa_session_actors(s->options.session),row->row.actor)) return true;
    uint32_t actual;
    if(!qa_q3_host_actor_slot(s->options.host,row->row.actor,&actual,e)) return false;
    if(actual!=slot) return q3scene_fail(e,QA_ERROR_FORMAT,"Component source actor changed its physical host binding");
    *actor=row->row.actor; *owned=row->row.owned; *found=true; return true;
}
bool application_q3_component_source_bind(application_q3_component_source *s,uint32_t slot,
    qa_actor_id actor,bool owned,bool admitted_client,qa_error *e)
{
    uint32_t actual; const qa_actor_record *record;
    if(!current(s)||s->borrows||slot>=QA_Q3_ENTITY_NONE||
        !(record=qa_actors_get(qa_session_actors(s->options.session),actor))||
        (owned&&record->owner!=s->options.owner)||
        !qa_q3_host_actor_slot(s->options.host,actor,&actual,e)||actual!=slot)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication binding requires its reached full source actor");
    size_t index;
    if(qa_actor_index_find_retained(&s->actor_rows,actor,&index)&&s->actors[index].row.slot!=slot)
        return q3scene_fail(e,QA_ERROR_FORMAT,"Component full actor already has a source slot");
    component_source_actor *row=at(s,slot);
    if(row&&!qa_actor_id_equal(row->row.actor,actor)) return q3scene_fail(e,QA_ERROR_FORMAT,"Component source slot is still occupied");
    if(!row) {
        if(!qa_actor_index_remember(&s->actor_rows,actor,s->actor_count,e)) return false;
        qa_id_index_insert(&s->actor_slots,slot,0,s->actor_count);
        row=s->actors+s->actor_count++;
    }
    *row=(component_source_actor){.row={slot,actor,owned},.client=admitted_client}; s->dirty=true; return true;
}
bool application_q3_component_source_release_actor(application_q3_component_source *s,qa_actor_id actor,qa_error *e)
{
    if(!s||s->borrows) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component source actor still has a presentation borrow");
    size_t index;
    if(qa_actor_index_find_retained(&s->actor_rows,actor,&index)) {
        qa_id_index_remove(&s->actor_slots,s->actors[index].row.slot,0);
        qa_actor_index_forget_retained(&s->actor_rows,actor);
        memmove(s->actors+index,s->actors+index+1,(s->actor_count-index-1)*sizeof(*s->actors)); --s->actor_count;
        for(size_t i=index;i<s->actor_count;++i) {
            qa_id_index_find(&s->actor_slots,s->actors[i].row.slot,0)->value=i;
            if(!qa_actor_index_remember(&s->actor_rows,s->actors[i].row.actor,i,e)) return false;
        }
        s->dirty=true;
    }
    return true;
}
static bool output(application_q3_component_source *s, qa_actor_id recipient,
    qa_unified_q3_event *event, qa_error *e)
{
    if(!current(s)||s->borrows||s->command_sequence==INT32_MAX)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component output requires its reached GAME namespace");
    event->source_sequence=s->command_sequence+1;
    if(s->options.application && !application_unified_q3_component_output(s->options.application,
        s->options.owner,event,recipient,s->time_ms,e)) return false;
    ++s->command_sequence; s->dirty=true; return true;
}
bool application_q3_component_source_configstring(void *context,uint32_t index,const char **out,qa_error *e)
{
    application_q3_component_source *s=context;
    if(!current(s)||!out||index>=QA_Q3_CONFIGSTRINGS) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component configstring source is unavailable");
    *out=qa_q3_configstring(s->game_state,index); return true;
}
bool application_q3_component_source_set_configstring(void *context,uint32_t index,const char *text,qa_error *e)
{
    application_q3_component_source *s=context;
    if(!current(s)||s->borrows||!text||index>=QA_Q3_CONFIGSTRINGS||s->game_state_revision==INT64_MAX)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component configstring mutation lost its private source");
    const char *old=qa_q3_configstring(s->game_state,index);
    if(old&&!strcmp(old,text)) return true;
    if(!qa_q3_configstring_set(s->game_state,index,text,e)) return false;
    ++s->game_state_revision; s->dirty=true;
    qa_unified_q3_event event={.kind=QA_UNIFIED_Q3_CONFIGSTRING,.client=-1,
        .index=index,.text=(char *)text};
    return output(s,(qa_actor_id){0},&event,e);
}
bool application_q3_component_source_command(void *context,int32_t physical,const char *text,qa_error *e)
{
    application_q3_component_source *s=context; qa_actor_id recipient={0};
    if(physical>=0) {
        component_source_actor *row=at(s,(uint32_t)physical);
        if(!row||!row->client||!qa_actors_get(qa_session_actors(s->options.session),row->row.actor)) return true;
        recipient=row->row.actor;
    }
    qa_unified_q3_event event={.kind=QA_UNIFIED_Q3_SERVER_COMMAND,.client=-1,.text=(char *)text};
    return output(s,recipient,&event,e);
}
static bool publication(application_q3_component_source *s,int32_t time,component_source_publication *out,qa_error *e)
{
    component_source_publication p=*out;
    p.ready=false;p.revision=s->revision+1;p.game_state_revision=s->game_state_revision;
    p.time_ms=time;p.command_sequence=s->command_sequence;p.entity_count=p.client_count=0;
    *p.game_state=*s->game_state;
    for(size_t i=0;i<s->actor_count;++i) {
        component_source_actor *binding=s->actors+i;
        if(!qa_actors_get(qa_session_actors(s->options.session),binding->row.actor)) continue;
        uint32_t actual;
        if(!qa_q3_host_actor_slot(s->options.host,binding->row.actor,&actual,e)||actual!=binding->row.slot) goto failed;
        component_source_entity *r=p.entities+p.entity_count++; *r=(component_source_entity){.actor=binding->row};
        bool visible;
        if(!qa_q3_host_source_entity(s->options.host,actual,&r->state,&r->shared,e)||
            !qa_q3_host_visibility_read(s->options.host,actual,&r->visibility,&visible,e)) goto failed;
        if(r->state.number!=(int32_t)actual) { q3scene_fail(e,QA_ERROR_FORMAT,"Component entity source number differs from its admitted slot"); goto failed; }
        if(!visible&&r->shared.linked&&!(r->shared.server_flags&QA_Q3_SVF_BROADCAST))
            { q3scene_fail(e,QA_ERROR_FORMAT,"Component linked entity lacks its actual world visibility"); goto failed; }
        if(binding->client) {
            component_source_client *client=p.clients+p.client_count++;
            *client=(component_source_client){.actor=binding->row.actor,.slot=actual};
            if(!qa_q3_host_source_player(s->options.host,actual,&client->state,e)) goto failed;
        }
    }
    if(!current(s)) { q3scene_fail(e,QA_ERROR_ARGUMENT,"Component source changed while publishing its original records"); goto failed; }
    if(!publication_clients_rebuild(&p,e)) goto failed;
    p.ready=true;*out=p;return true;
failed:
    return false;
}
bool application_q3_component_source_publish(application_q3_component_source *s,int32_t time,bool baseline,qa_error *e)
{
    if(!current(s)||!application_q3_component_source_idle(s)||time<0||time<s->time_ms||s->revision==INT64_MAX)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication requires its actual completed GAME interval");
    const uint32_t flags[]={QA_CVAR_SERVERINFO,QA_CVAR_SYSTEMINFO};
    for(uint32_t i=0;i<2;++i) {
        char text[1024];
        if(!s->options.information(s->options.context,flags[i],text,sizeof(text),e) || !current(s) ||
            !application_q3_component_source_set_configstring(s,i,text,e)) return false;
    }
    if(!s->dirty && s->current.ready && s->current.time_ms==time && !baseline) return true;
    if(baseline && s->baseline.ready) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component baseline is already retained");
    if(!publication(s,time,&s->pending,e)) return false;
    component_source_publication *target=baseline?&s->baseline:&s->current;
    component_source_publication previous=*target;*target=s->pending;s->pending=previous;
    ++s->revision; s->time_ms=time; s->dirty=false; return true;
}
bool application_q3_component_source_restore_baseline(application_q3_component_source *s,int32_t time,qa_error *e)
{
    if(!s||!s->options.scene) return true;
    if(!application_q3_component_source_publish(s,time,false,e)) return false;
    const component_source_publication *source=&s->current;
    component_source_publication copy=*source;
    copy.game_state=s->baseline.game_state;copy.entities=s->baseline.entities;copy.clients=s->baseline.clients;
    copy.client_rows=s->baseline.client_rows;
    *copy.game_state=*source->game_state;
    memcpy(copy.entities,source->entities,source->entity_count*sizeof(*copy.entities));
    memcpy(copy.clients,source->clients,source->client_count*sizeof(*copy.clients));
    if(!publication_clients_rebuild(&copy,e)) return false;
    s->baseline=copy;return true;
}
static bool view_acquire(void *context,bool baseline,application_q3_scene_context *out,qa_error *e)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    const component_source_publication *p=baseline&&s->baseline.ready?&s->baseline:&s->current;
    if(!current(s)||!qa_qvm_can_destroy(s->options.vm)||!p->ready||!out||
        (!baseline&&(view->time_ms<p->time_ms||view->frame_ms<0)))
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component view requires its actual completed GAME publication");
    size_t index;
    const component_source_client *client=qa_actor_index_find_retained(&p->client_rows,view->viewer,&index)?p->clients+index:NULL;
    if(!client||!qa_actors_get(qa_session_actors(s->options.session),view->viewer))
        return q3scene_fail(e,QA_ERROR_NOT_FOUND,"Component viewer has no admitted original source client");
    qa_unified_frame_lease *storage=view->storage;
    if(storage) { if(!qa_unified_frame_lease_retain(storage,e)) return false; }
    else storage=application_control_storage_acquire(s->options.application,e);
    if(!storage) return false;
    component_source_borrow *b=qa_unified_frame_lease_alloc(storage,1,sizeof(*b),_Alignof(component_source_borrow),e);
    qa_q3_visibility_entity *visibility=qa_unified_frame_lease_alloc(storage,QA_Q3_ENTITIES,sizeof(*visibility),_Alignof(qa_q3_visibility_entity),e);
    if(!b || !visibility) { qa_unified_frame_lease_release(storage);return false; }
    b->storage=storage;
    b->view=view;
    for(size_t i=0;i<p->entity_count;++i) {
        const component_source_entity *r=p->entities+i; uint32_t slot=r->actor.slot;
        visibility[slot]=(qa_q3_visibility_entity){.state=&r->state,.linked=r->shared.linked,.flags=(uint32_t)r->shared.server_flags,
            .single_client=r->shared.single_client,.area=r->visibility.area,.area2=r->visibility.area2,
            .last_cluster=r->visibility.last_cluster,.clusters=r->visibility.clusters,.cluster_count=r->visibility.cluster_count};
    }
    bool ok=qa_q3_select_snapshot_entities(&client->state,visibility,QA_Q3_ENTITIES,&s->options.visibility,
        &s->visibility_marks,false,&b->visible,e);
    b->actors=ok?qa_unified_frame_lease_alloc(storage,p->entity_count,sizeof(*b->actors),_Alignof(application_q3_scene_actor),e):NULL;
    if(!ok || (p->entity_count && !b->actors)) { qa_unified_frame_lease_release(storage);return false; }
    for(size_t i=0;i<p->entity_count;++i) b->actors[i]=p->entities[i].actor;
    b->command_next=view->command_cursor;
    qa_application *app=s->options.application;
    bool pending=false;
    qa_application_event_range cursor={.next=b->command_next,.end=app?qa_application_events_next(app):0};
    const qa_application_event_filter filter={.presentation_kind=QA_UNIFIED_PRESENTATION_Q3,.provider=s->options.owner};
    const application_unified_event_record *row;
    while(app && (row=qa_application_event_range_next(app,&cursor,&filter))) {
        const qa_unified_q3_event *event=&row->presentation->value.q3;
        if(event->kind!=QA_UNIFIED_Q3_SERVER_COMMAND && event->kind!=QA_UNIFIED_Q3_CONFIGSTRING) continue;
        if(event->source_sequence>p->command_sequence) { pending=true; break; }
        if((int64_t)event->source_sequence<=(int64_t)p->command_sequence-64) continue;
        if(b->command_count<64) b->commands[b->command_count++]=(application_q3_scene_command){
            .sequence=event->source_sequence,.text=event->text,.output=event,
            .addressed=!row->recipient.registry||qa_actor_id_equal(row->recipient,view->viewer)};
    }
    b->command_next=pending ? cursor.id : cursor.next;
    b->snapshot=(qa_q3_snapshot){.valid=true,.server_time=p->time_ms,.server_command_number=p->command_sequence,
        .player=client->state,.area_bytes=b->visible.area_bytes,.entity_count=b->visible.count,.entities=b->visible.entities};
    memcpy(b->snapshot.area_mask,b->visible.area_mask,32);
    b->context=(application_q3_scene_context){.generation=s->options.generation,.revision=p->revision,.game_state_revision=p->game_state_revision,
        .baseline=baseline&&s->baseline.ready,
        .time_ms=baseline?p->time_ms:view->time_ms,.frame_ms=baseline?0:view->frame_ms,.client_number=(int32_t)client->slot,
        .origin=view->origin,.axis={view->axis[0],view->axis[1],view->axis[2]},.game_state=p->game_state,.snapshot=&b->snapshot,
        .actors=b->actors,.actor_count=p->entity_count,.commands=b->commands,.command_count=b->command_count};
    if(!current(s)) { qa_unified_frame_lease_release(storage); return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication changed during viewer selection"); }
    b->next=s->borrows; s->borrows=b;
    if(view->weapon_presented) {
        bool presented=false;
        if(!view->weapon_presented(view->weapon_context,view->viewer,&presented,e)||!current(s)) {
            s->borrows=b->next; qa_unified_frame_lease_release(storage); return false;
        }
        b->context.has_weapon_presented=true; b->context.weapon_presented=presented;
    }
    *out=b->context; return true;
}
static bool view_current(void *context,const application_q3_scene_context *c)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    if(!current(s)||c->generation!=s->options.generation||!qa_actors_get(qa_session_actors(s->options.session),view->viewer)) return false;
    component_source_actor *row=at(s,(uint32_t)c->client_number);
    return row&&row->client&&qa_actor_id_equal(row->row.actor,view->viewer);
}
static bool view_live(void *context,qa_actor_id actor)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    if(!current(s)||!qa_actors_get(qa_session_actors(s->options.session),actor)) return false;
    size_t index;
    return qa_actor_index_find_retained(&s->actor_rows,actor,&index);
}
static void view_release(void *context,const application_q3_scene_context *c)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    component_source_borrow **at=&s->borrows;
    while(*at&&((*at)->view!=view||(*at)->context.snapshot!=c->snapshot)) at=&(*at)->next;
    if(!*at) return;
    component_source_borrow *b=*at; *at=b->next; view->command_next=b->command_next; qa_unified_frame_lease_release(b->storage);
}
static bool weapon(void *context,qa_actor_id actor,bool *out,qa_error *e)
{
    application_q3_component_view *view=context;
    return view->weapon_presented?view->weapon_presented(view->weapon_context,actor,out,e):
        q3scene_fail(e,QA_ERROR_UNSUPPORTED,"Component condition has no actual presented-weapon owner");
}
application_q3_scene_source application_q3_component_view_services(application_q3_component_view *view)
{ return (application_q3_scene_source){.context=view,.acquire=view_acquire,.current=view_current,.actor=actor_read,.live=view_live,.release=view_release,.weapon_presented=weapon}; }
bool application_q3_component_source_destroy(application_q3_component_source **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_q3_component_source *s=*owner;
    if(!application_q3_component_source_idle(s)) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component source still has actual scene/executor borrowers");
    publication_free(&s->current); publication_free(&s->baseline);publication_free(&s->pending);
    qa_actor_index_destroy(&s->actor_rows);qa_id_index_destroy(&s->actor_slots);
    free(s->actors); free(s->game_state); qa_qvm_image_release((qa_qvm_image *)s->options.image); free(s); *owner=NULL; return true;
}
