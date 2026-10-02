#include "guest_native_q2_private.h"
#include "native_q2_source_invocation.h"
#include "native_q2_callbacks.h"
#include "native_q2_visibility.h"
#include "map_players_private.h"

typedef struct source_retirement {
    struct source_retirement *next;
    application_native_q2_source_authority authority;
    struct application_native_q2_source_invocation *scope;
    qa_error error;
    bool retained,consumed;
} source_retirement;
struct application_native_q2_source_invocation {
    struct application_native_q2_source_invocation *outer;
    struct application_native_q2 *engine;
    application_provider *provider;
    qa_actor_id actor;
    struct application_player_roster *roster;
    application_provider *primary;
    uint32_t seat,client_slot,source_slot,native_client,native_seat;
    qa_net_client_id remote_client;
    qa_net_seat_id remote_seat;
    bool client,remote,returned;
    struct application_native_q2_source_invocation *root,*all_next;
    source_retirement *retirements;
};
static bool declared(const struct application_native_q2 *n)
{
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id rows=qa_json_get(d,qa_json_root(d),"protection");
    for(size_t i=0;i<qa_json_size(d,rows);++i)
        if(qa_json_string_equal(d,qa_json_get(d,qa_json_get(d,qa_json_at(d,rows,i),"absorb"),"abi"),"source-region"))return true;
    return false;
}
static bool capture(struct application_native_q2_source_invocation *s,qa_error *e)
{
    struct application_native_q2 *n=s->engine;qa_application *app=s->provider->application;
    if(!s->actor.registry)return true;
    if(app->players)for(size_t i=0;i<app->players->count;++i) {
        const application_player_record *r=app->players->records+i;
        if(r->retiring||!qa_actor_id_equal(r->actor,s->actor))continue;
        if(s->client)return application_fail(e,QA_ERROR_ARGUMENT,"Source invocation repeats its actual canonical client");
        s->client=true;s->roster=app->players;s->primary=app->players->map_provider;
        s->seat=r->seat;s->client_slot=r->client_slot;s->source_slot=r->source_slot;
        s->remote=r->remote;s->remote_client=r->remote_client;s->remote_seat=r->remote_seat;
    }
    for(uint32_t i=1;i<257;++i)if(n->clients[i].reserved&&qa_actor_id_equal(n->clients[i].actor,s->actor)) {
        if(s->native_client)return application_fail(e,QA_ERROR_ARGUMENT,"Source invocation repeats its actual reserved client");
        s->native_client=i;s->native_seat=n->clients[i].seat;
    }
    return true;
}
static bool current(const struct application_native_q2_source_invocation *s)
{
    struct application_native_q2 *n=s->engine;application_provider *p=s->provider;
    qa_application *app=p->application;
    if(p->state.native.q2_engine!=n||n->provider!=p||n->shutting_down)
        return false;
    if(!s->actor.registry)return true;
    if(!qa_actors_get(qa_session_actors(app->session),s->actor))return false;
    if(s->native_client) {
        const application_native_q2_client *r=n->clients+s->native_client;
        if(!r->reserved||!qa_actor_id_equal(r->actor,s->actor)||r->seat!=s->native_seat||r->denied||
            (r->disconnect_started&&n->disconnect_client!=s->native_client))return false;
    }
    if(!s->client)return true;
    if(app->players!=s->roster||app->players->map_provider!=s->primary)return false;
    size_t found=0;
    for(size_t i=0;i<app->players->count;++i) {
        const application_player_record *r=app->players->records+i;
        if(!qa_actor_id_equal(r->actor,s->actor))continue;
        if(r->retiring||r->seat!=s->seat||r->client_slot!=s->client_slot||r->source_slot!=s->source_slot||
            r->remote!=s->remote||r->remote_client.owner!=s->remote_client.owner||
            r->remote_client.generation!=s->remote_client.generation||r->remote_client.slot!=s->remote_client.slot||
            r->remote_seat.owner!=s->remote_seat.owner||r->remote_seat.index!=s->remote_seat.index)return false;
        ++found;
    }
    return found==1;
}
bool application_native_q2_source_invocation_begin(struct application_native_q2 *n,qa_actor_id actor,
    struct application_native_q2_source_invocation **out,qa_error *e)
{
    if(!n||!out||*out)return application_fail(e,QA_ERROR_ARGUMENT,"Source invocation requires its actual empty owner");
    if(!n->callbacks||!declared(n))return true;
    struct application_native_q2_source_invocation *s=calloc(1,sizeof(*s));
    if(!s)return application_fail(e,QA_ERROR_MEMORY,"Retaining actual Source invocation scope");
    s->outer=n->source_invocation;s->engine=n;s->provider=n->provider;s->actor=actor;
    if(!capture(s,e)) {free(s);return false;}
    s->root=s->outer?s->outer->root:s;
    if(s->outer) {s->all_next=s->root->all_next;s->root->all_next=s;}
    n->source_invocation=s;*out=s;return true;
}
bool application_native_q2_source_invocation_guard(struct application_native_q2 *n,
    const application_native_q2_source_authority *authority,const qa_error *retirement,qa_error *e)
{
    struct application_native_q2_source_invocation *s=n?n->source_invocation:NULL;
    if(!s||!authority||!authority->current||!authority->retain||!authority->release||
        !retirement||retirement->code==QA_OK)
        return application_fail(e,QA_ERROR_ARGUMENT,"Source region has no owning invocation and exact authority error");
    source_retirement *r=s->root->retirements;
    while(r&&(r->error.code!=retirement->code||r->error.offset!=retirement->offset||
        strcmp(r->error.message,retirement->message)))r=r->next;
    if(!r) {
        r=calloc(1,sizeof(*r));
        if(!r)return application_fail(e,QA_ERROR_MEMORY,"Retaining reached Source region authority");
        if(!authority->retain(authority->context,e)) {free(r);return false;}
        r->authority=*authority;r->scope=s;r->error=*retirement;r->retained=true;
        r->next=s->root->retirements;s->root->retirements=r;
    }
    if(authority->current(authority->context,e))return true;
    if(e)*e=r->error;
    return false;
}
void application_native_q2_source_invocation_unguard(struct application_native_q2 *n,
    const qa_error *retirement,const qa_error *failure)
{
    struct application_native_q2_source_invocation *s=n?n->source_invocation:NULL;
    if(!s||!retirement)return;
    source_retirement **link=&s->root->retirements;
    while(*link) {
        source_retirement *r=*link;
        if(r->error.code!=retirement->code||r->error.offset!=retirement->offset||
            strcmp(r->error.message,retirement->message)) {link=&r->next;continue;}
        qa_error ignored={0};
        if(!r->consumed&&failure&&failure->code==r->error.code&&failure->offset==r->error.offset&&
            !strcmp(failure->message,r->error.message)&&!r->authority.current(r->authority.context,&ignored))return;
        *link=r->next;
        if(r->retained)r->authority.release(r->authority.context);
        free(r);return;
    }
}
bool application_native_q2_source_invocation_accepts(void *context,const qa_error *e)
{
    struct application_native_q2_source_invocation *s=context;
    if(!s||!e)return false;
    struct application_native_q2_source_invocation *active=s->engine->source_invocation;
    bool present=false;
    for(struct application_native_q2_source_invocation *p=active;p;p=p->outer)if(p==s)present=true;
    if(!present)return false;
    for(source_retirement **link=&s->root->retirements;*link;link=&(*link)->next) {
        source_retirement *r=*link;
        if(r->consumed||e->code!=r->error.code||e->offset!=r->error.offset||strcmp(e->message,r->error.message))continue;
        qa_error ignored={0};
        if(r->authority.current(r->authority.context,&ignored))return false;
        struct application_native_q2_source_invocation *selected=NULL;
        for(struct application_native_q2_source_invocation *p=active;p;p=p->outer)
            if(!current(p))selected=p;
        if((selected?selected:r->scope)!=s)return false;
        r->authority.release(r->authority.context);r->retained=false;r->consumed=true;return true;
    }
    return false;
}
bool application_native_q2_source_invocation_close(struct application_native_q2_source_invocation **owned,qa_error *e)
{
    struct application_native_q2_source_invocation *s=owned?*owned:NULL;
    if(!s)return true;
    s->returned=true;
    if(s->engine->source_invocation!=s)
        return application_fail(e,QA_ERROR_ARGUMENT,"Source invocation retains an unfinished inner owner");
    s->engine->source_invocation=s->outer;*owned=NULL;
    if(s->outer)return true;
    while(s->retirements) {
        source_retirement *r=s->retirements;s->retirements=r->next;
        if(r->retained)r->authority.release(r->authority.context);
        free(r);
    }
    while(s->all_next) {
        struct application_native_q2_source_invocation *p=s->all_next;s->all_next=p->all_next;free(p);
    }
    free(s);return true;
}
bool application_native_q2_source_invocation_drain(struct application_native_q2 *n,qa_error *e)
{
    if(!n||!n->source_invocation)return true;
    if(n->calls||qa_native_active(application_native_q2_callbacks_instance(n->callbacks)))
        return application_fail(e,QA_ERROR_ARGUMENT,"Source invocation drain requires all actual calls returned");
    struct application_native_q2_source_invocation *root=n->source_invocation->root;
    while(root->retirements) {
        source_retirement *r=root->retirements;root->retirements=r->next;
        if(r->retained)r->authority.release(r->authority.context);
        free(r);
    }
    while(n->source_invocation&&n->source_invocation->returned) {
        struct application_native_q2_source_invocation *s=n->source_invocation;
        if(!application_native_q2_source_invocation_close(&s,e))return false;
    }
    return true;
}
bool application_native_q2_source_original(struct application_native_q2 *n,
    qa_native_entry_observer *binding,const qa_native_value *arguments,size_t count,
    qa_native_value *result,qa_error *e)
{
    if(!n||!binding)return application_fail(e,QA_ERROR_ARGUMENT,"Original Source call requires its actual owner and entry");
    application_native_q2_visibility_invalidate(n);
    return qa_native_invoke_original(binding,arguments,count,result,e);
}
bool application_native_q2_source_invoke_original(struct application_native_q2 *n,qa_actor_id actor,
    qa_native_entry_observer *binding,const qa_native_value *arguments,size_t count,
    qa_native_value *result,bool *cancelled,qa_error *e)
{
    if(!n||!binding||!cancelled)return application_fail(e,QA_ERROR_ARGUMENT,"Source execution requires its actual invocation receipt");
    *cancelled=false;
    struct application_native_q2_source_invocation *scope=NULL;
    if(!application_native_q2_source_invocation_begin(n,actor,&scope,e))return false;
    if(!scope)return application_native_q2_source_original(n,binding,arguments,count,result,e);
    application_native_q2_visibility_invalidate(n);
    bool ok=qa_native_invoke_original_cancellable(binding,arguments,count,result,
        application_native_q2_source_invocation_accepts,scope,cancelled,e);
    qa_error cleanup={0};
    bool closed=application_native_q2_source_invocation_close(&scope,&cleanup);
    if(!closed&&ok&&e)*e=cleanup;
    return ok&&closed;
}
