#include "guest_native_q2_private.h"
#include "native_q2_source_invocation.h"
#include "native_q2_callbacks.h"
#include "native_q2_visibility.h"
#include "map_players_private.h"

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
    bool client,remote,issued;
    qa_error retirement;
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
    if(n->source_invocation!=s||p->state.native.q2_engine!=n||n->provider!=p||n->shutting_down)
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
bool application_native_q2_source_invocation_guard(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_source_invocation *s=n?n->source_invocation:NULL;
    if(!s||current(s))return true;
    s->issued=true;
    qa_error_set(&s->retirement,QA_ERROR_NOT_FOUND,(size_t)(uintptr_t)s,
        "Source invocation retired its captured actor or client");
    if(e)*e=s->retirement;
    return false;
}
static bool accepts(void *context,const qa_error *e)
{
    const struct application_native_q2_source_invocation *s=context;
    return s->engine->source_invocation==s&&s->issued&&e&&e->code==s->retirement.code&&
        e->offset==s->retirement.offset&&!strcmp(e->message,s->retirement.message);
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
    if(!n->callbacks||!declared(n))return application_native_q2_source_original(n,binding,arguments,count,result,e);
    struct application_native_q2_source_invocation scope={.outer=n->source_invocation,
        .engine=n,.provider=n->provider,.actor=actor};
    if(!capture(&scope,e))return false;
    n->source_invocation=&scope;
    if(!current(&scope)) {
        n->source_invocation=scope.outer;*cancelled=true;
        if(result)*result=(qa_native_value){.type=QA_NATIVE_VOID};
        return true;
    }
    application_native_q2_visibility_invalidate(n);
    bool ok=qa_native_invoke_original_cancellable(binding,arguments,count,result,accepts,&scope,cancelled,e);
    n->source_invocation=scope.outer;
    return ok;
}
