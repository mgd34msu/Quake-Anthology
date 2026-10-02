#include "guest_q3_component_source_private.h"

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
        qa_sha256_equal(qa_qvm_digest(s->options.vm),qa_qvm_image_digest(s->options.image))&&
        qa_q3_host_client_context_read(s->options.host,&host)&&host.session==s->options.session&&host.owner==s->options.owner;
}
bool application_q3_component_source_idle(const application_q3_component_source *s)
{ return s&&!s->borrows&&(!s->options.vm||qa_qvm_can_destroy(s->options.vm)); }
static void publication_free(component_source_publication *p)
{
    for(size_t i=0;i<p->command_count;++i) free(p->commands[i].text);
    free(p->commands); free(p->clients); free(p->entities); free(p->game_state); *p=(component_source_publication){0};
}
static bool copy_text(const char *text,char **out,qa_error *e)
{
    size_t n=strlen(text);
    if(n==SIZE_MAX) return q3scene_fail(e,QA_ERROR_MEMORY,"Component source text overflows");
    char *p=malloc(n+1); if(!p) return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining component source text");
    memcpy(p,text,n+1); *out=p; return true;
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
    if((options->vm||options->host)&&!current(s)) { free(s); return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication does not own its actual GAME"); }
    s->game_state=calloc(1,sizeof(*s->game_state));
    if(!s->game_state) { free(s); return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining private component gameState"); }
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
{ for(size_t i=0;i<s->actor_count;++i) if(s->actors[i].row.slot==slot) return s->actors+i; return NULL; }
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
    for(size_t i=0;i<s->actor_count;++i)
        if(qa_actor_id_equal(s->actors[i].row.actor,actor)&&s->actors[i].row.slot!=slot)
            return q3scene_fail(e,QA_ERROR_FORMAT,"Component full actor already has a source slot");
    component_source_actor *row=at(s,slot);
    if(row&&!qa_actor_id_equal(row->row.actor,actor)) return q3scene_fail(e,QA_ERROR_FORMAT,"Component source slot is still occupied");
    if(!row) {
        component_source_actor *rows=realloc(s->actors,(s->actor_count+1)*sizeof(*rows));
        if(!rows) return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining admitted component source actor");
        s->actors=rows; row=rows+s->actor_count++;
    }
    *row=(component_source_actor){.row={slot,actor,owned},.client=admitted_client}; s->dirty=true; return true;
}
bool application_q3_component_source_release_actor(application_q3_component_source *s,qa_actor_id actor,qa_error *e)
{
    if(!s||s->borrows) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component source actor still has a presentation borrow");
    for(size_t i=0;i<s->actor_count;++i) if(qa_actor_id_equal(s->actors[i].row.actor,actor)) {
        memmove(s->actors+i,s->actors+i+1,(s->actor_count-i-1)*sizeof(*s->actors)); --s->actor_count; s->dirty=true; break;
    }
    return true;
}
static bool command(application_q3_component_source *s,qa_actor_id recipient,const char *text,qa_error *e)
{
    if(!current(s)||s->borrows||!text||s->command_sequence==INT32_MAX)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component command requires its current private GAME namespace");
    char *copy=NULL; if(!copy_text(text,&copy,e)) return false;
    int32_t sequence=s->command_sequence+1; component_source_command *row=s->commands+(uint32_t)sequence%64;
    free(row->text); *row=(component_source_command){sequence,recipient,copy}; s->command_sequence=sequence; s->dirty=true; return true;
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
    if(!s->options.scene) {
        if(!qa_q3_configstring_set(s->game_state,index,text,e)) return false;
        ++s->game_state_revision; s->dirty=true; return true;
    }
    qa_buffer quoted={0};
    if(!qa_json_quote((qa_bytes){(const uint8_t *)text,strlen(text)},&quoted,e)) return false;
    size_t size=quoted.size+32; char *line=malloc(size);
    if(!line) { qa_buffer_free(&quoted); return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining reached component config command"); }
    int prefix=snprintf(line,size,"cs %u ",index);
    if(prefix<0||(size_t)prefix>=size||quoted.size>size-(size_t)prefix-1) {
        free(line); qa_buffer_free(&quoted); return q3scene_fail(e,QA_ERROR_FORMAT,"Component configstring command overflows");
    }
    memcpy(line+(size_t)prefix,quoted.data,quoted.size); line[(size_t)prefix+quoted.size]=0;
    bool ok=qa_q3_configstring_set(s->game_state,index,text,e);
    if(ok) { ++s->game_state_revision; s->dirty=true; ok=command(s,(qa_actor_id){0},line,e); }
    free(line); qa_buffer_free(&quoted); return ok;
}
bool application_q3_component_source_command(void *context,int32_t physical,const char *text,qa_error *e)
{
    application_q3_component_source *s=context; qa_actor_id recipient={0};
    if(physical>=0) {
        component_source_actor *row=at(s,(uint32_t)physical);
        if(!row||!row->client||!qa_actors_get(qa_session_actors(s->options.session),row->row.actor)) return true;
        recipient=row->row.actor;
    }
    return !s->options.scene||command(s,recipient,text,e);
}
static bool publication(application_q3_component_source *s,int32_t time,component_source_publication *out,qa_error *e)
{
    component_source_publication p={.revision=s->revision+1,.game_state_revision=s->game_state_revision,
        .time_ms=time,.command_sequence=s->command_sequence};
    p.game_state=malloc(sizeof(*p.game_state));
    p.entities=s->actor_count?calloc(s->actor_count,sizeof(*p.entities)):NULL;
    p.clients=s->actor_count?calloc(s->actor_count,sizeof(*p.clients)):NULL;
    p.commands=calloc(64,sizeof(*p.commands));
    if(!p.game_state||(s->actor_count&&(!p.entities||!p.clients))||!p.commands)
        { publication_free(&p); return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining completed component GAME publication"); }
    *p.game_state=*s->game_state;
    for(size_t i=0;i<s->actor_count;++i) {
        component_source_actor *binding=s->actors+i;
        if(!qa_actors_get(qa_session_actors(s->options.session),binding->row.actor)) continue;
        uint32_t actual;
        if(!qa_q3_host_actor_slot(s->options.host,binding->row.actor,&actual,e)||actual!=binding->row.slot) goto failed;
        component_source_entity *r=p.entities+p.entity_count++; r->actor=binding->row;
        bool visible;
        if(!qa_q3_host_source_entity(s->options.host,actual,&r->state,&r->shared,e)||
            !qa_q3_host_visibility_read(s->options.host,actual,&r->visibility,&visible,e)) goto failed;
        if(r->state.number!=(int32_t)actual) { q3scene_fail(e,QA_ERROR_FORMAT,"Component entity source number differs from its admitted slot"); goto failed; }
        if(!visible&&r->shared.linked&&!(r->shared.server_flags&QA_Q3_SVF_BROADCAST))
            { q3scene_fail(e,QA_ERROR_FORMAT,"Component linked entity lacks its actual world visibility"); goto failed; }
        if(binding->client) {
            component_source_client *client=p.clients+p.client_count++;
            client->actor=binding->row.actor; client->slot=actual;
            if(!qa_q3_host_source_player(s->options.host,actual,&client->state,e)) goto failed;
        }
    }
    int32_t first=s->command_sequence>64?s->command_sequence-63:1;
    for(int64_t n=first;n<=s->command_sequence;++n) {
        const component_source_command *r=s->commands+(uint32_t)n%64;
        if(r->sequence!=(int32_t)n||!r->text) { q3scene_fail(e,QA_ERROR_FORMAT,"Component reached reliable ring is incomplete"); goto failed; }
        component_source_command *copy=p.commands+p.command_count++;
        copy->sequence=r->sequence; copy->recipient=r->recipient;
        if(!copy_text(r->text,&copy->text,e)) goto failed;
    }
    if(!current(s)) { q3scene_fail(e,QA_ERROR_ARGUMENT,"Component source changed while publishing its original records"); goto failed; }
    *out=p; return true;
failed:
    publication_free(&p); return false;
}
bool application_q3_component_source_publish(application_q3_component_source *s,int32_t time,bool baseline,qa_error *e)
{
    if(!current(s)||!application_q3_component_source_idle(s)||time<0||time<s->time_ms||s->revision==INT64_MAX)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication requires its actual completed GAME interval");
    const uint32_t flags[]={QA_CVAR_SERVERINFO,QA_CVAR_SYSTEMINFO};
    for(uint32_t i=0;i<2;++i) {
        qa_buffer text={0};
        bool okay=s->options.information(s->options.context,flags[i],&text,e)&&current(s)&&
            application_q3_component_source_set_configstring(s,i,(const char *)text.data,e);
        qa_buffer_free(&text);
        if(!okay) return false;
    }
    if(!s->dirty&&s->current.game_state&&s->current.time_ms==time&&!baseline) return true;
    component_source_publication p={0};
    if(!publication(s,time,&p,e)) return false;
    if(baseline) {
        if(s->baseline.game_state) { publication_free(&p); return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component baseline is already retained"); }
        s->baseline=p;
    } else { publication_free(&s->current); s->current=p; }
    ++s->revision; s->time_ms=time; s->dirty=false; return true;
}
bool application_q3_component_source_restore_baseline(application_q3_component_source *s,int32_t time,qa_error *e)
{
    if(!s||!s->options.scene) return true;
    if(!application_q3_component_source_publish(s,time,false,e)) return false;
    const component_source_publication *source=&s->current;
    component_source_publication copy=*source;
    copy.game_state=NULL; copy.entities=NULL; copy.clients=NULL; copy.commands=NULL; copy.command_count=0;
    copy.game_state=malloc(sizeof(*copy.game_state));
    copy.entities=source->entity_count?malloc(source->entity_count*sizeof(*copy.entities)):NULL;
    copy.clients=source->client_count?malloc(source->client_count*sizeof(*copy.clients)):NULL;
    copy.commands=source->command_count?calloc(source->command_count,sizeof(*copy.commands)):NULL;
    if(!copy.game_state||(source->entity_count&&!copy.entities)||(source->client_count&&!copy.clients)||
        (source->command_count&&!copy.commands)) {
        publication_free(&copy); return q3scene_fail(e,QA_ERROR_MEMORY,"Retaining imported Source scene baseline");
    }
    *copy.game_state=*source->game_state;
    if(source->entity_count) memcpy(copy.entities,source->entities,source->entity_count*sizeof(*copy.entities));
    if(source->client_count) memcpy(copy.clients,source->clients,source->client_count*sizeof(*copy.clients));
    for(size_t i=0;i<source->command_count;++i) {
        copy.commands[i]=source->commands[i]; copy.commands[i].text=NULL; ++copy.command_count;
        if(!copy_text(source->commands[i].text,&copy.commands[i].text,e)) { publication_free(&copy); return false; }
    }
    publication_free(&s->baseline); s->baseline=copy; return true;
}
static bool view_acquire(void *context,bool baseline,application_q3_scene_context *out,qa_error *e)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    const component_source_publication *p=baseline&&s->baseline.game_state?&s->baseline:&s->current;
    if(!current(s)||!qa_qvm_can_destroy(s->options.vm)||!p->game_state||!out||
        (!baseline&&(view->time_ms<p->time_ms||view->frame_ms<0)))
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component view requires its actual completed GAME publication");
    const component_source_client *client=NULL;
    for(size_t i=0;i<p->client_count;++i) if(qa_actor_id_equal(p->clients[i].actor,view->viewer)) client=p->clients+i;
    if(!client||!qa_actors_get(qa_session_actors(s->options.session),view->viewer))
        return q3scene_fail(e,QA_ERROR_NOT_FOUND,"Component viewer has no admitted original source client");
    component_source_borrow *b=calloc(1,sizeof(*b)); qa_q3_visibility_entity *visibility=calloc(QA_Q3_ENTITIES,sizeof(*visibility));
    if(!b||!visibility) { free(b); free(visibility); return q3scene_fail(e,QA_ERROR_MEMORY,"Borrowing actual component viewer snapshot"); }
    b->view=view;
    for(size_t i=0;i<p->entity_count;++i) {
        const component_source_entity *r=p->entities+i; uint32_t slot=r->actor.slot;
        visibility[slot]=(qa_q3_visibility_entity){.state=&r->state,.linked=r->shared.linked,.flags=(uint32_t)r->shared.server_flags,
            .single_client=r->shared.single_client,.area=r->visibility.area,.area2=r->visibility.area2,
            .last_cluster=r->visibility.last_cluster,.clusters=r->visibility.clusters,.cluster_count=r->visibility.cluster_count};
    }
    bool ok=qa_q3_select_snapshot_entities(&client->state,visibility,QA_Q3_ENTITIES,&s->options.visibility,false,&b->visible,e);
    free(visibility);
    b->actors=p->entity_count?calloc(p->entity_count,sizeof(*b->actors)):NULL;
    b->commands=p->command_count?calloc(p->command_count,sizeof(*b->commands)):NULL;
    if(ok&&((p->entity_count&&!b->actors)||(p->command_count&&!b->commands))) ok=q3scene_fail(e,QA_ERROR_MEMORY,"Retaining component snapshot source bindings");
    if(!ok) { free(b->actors); free(b->commands); free(b); return false; }
    for(size_t i=0;i<p->entity_count;++i) b->actors[i]=p->entities[i].actor;
    for(size_t i=0;i<p->command_count;++i) b->commands[i]=(application_q3_scene_command){.sequence=p->commands[i].sequence,.text=p->commands[i].text,
        .addressed=!p->commands[i].recipient.registry||qa_actor_id_equal(p->commands[i].recipient,view->viewer)};
    b->snapshot=(qa_q3_snapshot){.valid=true,.server_time=p->time_ms,.server_command_number=p->command_sequence,
        .player=client->state,.area_bytes=b->visible.area_bytes,.entity_count=b->visible.count,.entities=b->visible.entities};
    memcpy(b->snapshot.area_mask,b->visible.area_mask,32);
    b->context=(application_q3_scene_context){.generation=s->options.generation,.revision=p->revision,.game_state_revision=p->game_state_revision,
        .baseline=baseline&&s->baseline.game_state!=NULL,
        .time_ms=baseline?p->time_ms:view->time_ms,.frame_ms=baseline?0:view->frame_ms,.client_number=(int32_t)client->slot,
        .origin=view->origin,.axis={view->axis[0],view->axis[1],view->axis[2]},.game_state=p->game_state,.snapshot=&b->snapshot,
        .actors=b->actors,.actor_count=p->entity_count,.commands=b->commands,.command_count=p->command_count};
    if(!current(s)) { free(b->actors); free(b->commands); free(b); return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component publication changed during viewer selection"); }
    b->next=s->borrows; s->borrows=b;
    if(view->weapon_presented) {
        bool presented=false;
        if(!view->weapon_presented(view->weapon_context,view->viewer,&presented,e)||!current(s)) {
            s->borrows=b->next; free(b->actors); free(b->commands); free(b); return false;
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
    for(size_t i=0;i<s->actor_count;++i) if(qa_actor_id_equal(s->actors[i].row.actor,actor)) return true;
    return false;
}
static void view_release(void *context,const application_q3_scene_context *c)
{
    application_q3_component_view *view=context; application_q3_component_source *s=view->source;
    component_source_borrow **at=&s->borrows;
    while(*at&&((*at)->view!=view||(*at)->context.snapshot!=c->snapshot)) at=&(*at)->next;
    if(!*at) return;
    component_source_borrow *b=*at; *at=b->next; free(b->actors); free(b->commands); free(b);
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
    publication_free(&s->current); publication_free(&s->baseline);
    for(size_t i=0;i<64;++i) free(s->commands[i].text);
    free(s->actors); free(s->game_state); qa_qvm_image_release((qa_qvm_image *)s->options.image); free(s); *owner=NULL; return true;
}
