#include "network_q2_host.h"
#include "qa/application_network.h"
#include "qa/network_q3.h"
#include <stdio.h>

typedef struct q2_host_peer {
    frontend_network_q2_host *host;
    qa_application_network_q2 *source;
    qa_q2_server_admission admission;
    qa_network_q2_server_hooks source_hooks;
    qa_net_seat_binding bindings[QA_Q2_MAX_SEATS];
    uint32_t slots[QA_Q2_MAX_SEATS];
    qa_net_client_id client;
    char userinfo[8193],reason[1024];
    char **configs;
    size_t config_count;
    bool reserved,committed,retiring;
} q2_host_peer;
struct frontend_network_q2_host {
    frontend_network_q2_host_options options;
    qa_network_q2_bootstrap *bootstrap;
    qa_application_network_q2 *discovery;
    qa_application_network_q2_host source;
    q2_host_peer *peers;
    size_t capacity;
    unsigned calls;
};
static bool current(frontend_network_q2_host *host,qa_error *error)
{
    qa_application_network_q2_host source;
    return host && host->options.current(host->options.context,host) &&
        qa_application_network_q2_host_source(host->options.frontend->application,host->options.protocol,&source,error) &&
        source.source.source_owner==host->source.source.source_owner && source.client_slots==host->source.client_slots &&
        source.source.map_revision==host->source.source.map_revision;
}
static bool drop(void *context,qa_net_client_id id,const char *reason,qa_error *error)
{
    q2_host_peer *peer=context;
    if(!peer || !reason || !peer->committed || !qa_net_client_id_equal(peer->client,id) || !current(peer->host,error)) return false;
    snprintf(peer->reason,sizeof(peer->reason),"%s",reason); peer->retiring=true; return true;
}
static bool input(void *context,qa_net_client_id id,qa_net_seat_id seat,
    const qa_network_q2_player *player,const qa_q2_usercmd *wire,uint64_t sequence,qa_error *error)
{
    q2_host_peer *peer=context; qa_actor_id actor;
    if(!peer || !wire || !player || !peer->committed || peer->retiring || !qa_net_client_id_equal(peer->client,id) ||
        !current(peer->host,error) || !qa_application_remote_player_actor(peer->host->options.frontend->application,id,seat,&actor) ||
        !qa_actor_id_equal(actor,player->actor)) return false;
    qa_application_network_q2_host source;
    if(!qa_application_network_q2_host_source(peer->host->options.frontend->application,
        peer->admission.connection.protocol,&source,error)) return false;
    qa_movement_command raw={.kind=source.source.edition==QA_Q2_RERELEASE?QA_MOVEMENT_Q2_RERELEASE:QA_MOVEMENT_Q2_CLASSIC,
        .sequence=sequence,.server_frame=wire->server_frame,.milliseconds=wire->msec,.buttons=wire->buttons,
        .impulse=wire->impulse,.light_level=wire->lightlevel,
        .forward_move=wire->forwardmove,.side_move=wire->sidemove,.up_move=wire->upmove};
    raw.angles=qa_v3((float)wire->angles[0]*(360.0f/65536.0f),
        (float)wire->angles[1]*(360.0f/65536.0f),(float)wire->angles[2]*(360.0f/65536.0f));
    for(size_t i=0;i<3;++i) raw.angle_words[i]=wire->angles[i];
    return qa_application_control_q2_command(peer->host->options.frontend->application,actor,sequence,&raw,error);
}
static bool recipient(void *context,qa_actor_id actor,qa_application_network_q2_recipient_view *out,
    bool *present,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!out || !present || !host || !actor.registry || !host->options.current(host->options.context,host))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recipient requires its actual retained HOST");
    *out=(qa_application_network_q2_recipient_view){0}; *present=false;
    uint32_t cursor=0; const qa_net_client *client;
    while(qa_net_connections_next(qa_network_connections(host->options.runtime),&cursor,&client)) {
        for(size_t s=0;s<client->seat_count;++s) {
            qa_actor_id actual;
            if(!qa_application_remote_player_actor(host->options.frontend->application,client->id,client->seats[s].seat,&actual) ||
                !qa_actor_id_equal(actual,actor)) continue;
            if(*present || client->seats[s].remote_index>=QA_Q2_MAX_SEATS)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 actor has ambiguous canonical recipient ownership");
            *out=(qa_application_network_q2_recipient_view){.actor=actual,.client=client->id,
                .seat=client->seats[s].seat,.connection_epoch=qa_network_epoch(host->options.runtime,client->id),
                .remote_index=(uint8_t)client->seats[s].remote_index};
            *present=true;
        }
    }
    return true;
}
static bool enabled(void *context,bool *out,qa_error *error)
{ frontend_network_q2_host *host=context; if(!out || !current(host,error)) return false; *out=true; return true; }
static bool rejects(void *context,const qa_net_address *address,bool *out,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!out || !address || !current(host,error)) return false;
    *out=qa_server_admin_rejects(host->options.admin,address); return true;
}
static bool discovery(void *context,qa_q2_discovery *out,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!out || !current(host,error)) return false;
    uint32_t count=0;
    for(uint32_t i=1;i<=host->source.client_slots;++i) {
        qa_application_network_q2_client_slot slot;
        if(!qa_application_network_q2_slot(host->discovery,i,&slot,error)) return false;
        if(slot.connected) ++count;
    }
    return qa_application_network_q2_discovery(host->discovery,&out->status,&out->name,&out->map,error) &&
        (out->players=count,out->maximum=host->source.client_slots,true);
}
static bool download_server(void *context,const char **out,qa_error *error)
{
    frontend_network_q2_host *host=context;
    return current(host,error) && qa_application_network_q2_download_server(host->discovery,out,error);
}
static bool transport_admitted(void *context,const qa_net_address *address,bool *out,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!out || !address || !current(host,error) || !host->options.lobby) return false;
    *out=qa_kex_lan_admitted(host->options.lobby,address); return true;
}
static void configs_free(q2_host_peer *peer)
{
    for(size_t i=0;i<peer->config_count;++i) free(peer->configs[i]);
    free(peer->configs); peer->configs=NULL; peer->config_count=0;
}
static bool config_copy(char **out,const char *value,qa_error *error)
{
    *out=NULL;
    if(!value || !*value) return true;
    size_t length=strlen(value);
    char *copy=malloc(length+1);
    if(!copy) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual Q2 configstring publication");
    memcpy(copy,value,length+1); *out=copy; return true;
}
static bool source_game_state(void *context,qa_net_client_id id,qa_q2_game_state *out,qa_error *error)
{
    q2_host_peer *peer=context;
    if(!peer->source_hooks.game_state(peer->source_hooks.context,id,out,error)) return false;
    size_t count=peer->host->source.source.edition==QA_Q2_RERELEASE?12448u:2080u;
    char **configs=calloc(count,sizeof(*configs));
    if(!configs) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining issued Q2 gamestate configstrings");
    bool ok=true;
    for(size_t i=0;ok && i<out->config_count;++i) {
        uint16_t index=out->configs[i].index;
        ok=index<count && !configs[index] && config_copy(&configs[index],out->configs[i].value,error);
    }
    if(!ok) {
        for(size_t i=0;i<count;++i) free(configs[i]);
        free(configs); return false;
    }
    configs_free(peer); peer->configs=configs; peer->config_count=count; return true;
}
static bool source_player(void *context,qa_net_client_id id,qa_net_seat_id seat,qa_network_q2_player *out,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.player(peer->source_hooks.context,id,seat,out,error); }
static bool source_begin(void *context,qa_net_client_id id,qa_net_seat_id seat,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.begin(peer->source_hooks.context,id,seat,error); }
static bool source_input(void *context,qa_net_client_id id,qa_net_seat_id seat,const qa_network_q2_player *player,
    const qa_q2_usercmd *command,uint64_t sequence,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.input(peer->source_hooks.context,id,seat,player,command,sequence,error); }
static bool source_expand(void *context,qa_net_client_id id,const char *text,qa_buffer *out,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.expand_command(peer->source_hooks.context,id,text,out,error); }
static bool source_command(void *context,qa_net_client_id id,qa_net_seat_id seat,const char *text,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.command(peer->source_hooks.context,id,seat,text,error); }
static bool source_userinfo(void *context,qa_net_client_id id,qa_net_seat_id seat,const char *text,qa_buffer *out,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.userinfo(peer->source_hooks.context,id,seat,text,out,error); }
static bool source_download(void *context,qa_net_client_id id,qa_network_q2_download_source *out,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.download_source(peer->source_hooks.context,id,out,error); }
static bool source_drop(void *context,qa_net_client_id id,const char *reason,qa_error *error)
{ q2_host_peer *peer=context; return peer->source_hooks.drop(peer->source_hooks.context,id,reason,error); }
static bool abort_claim(void *context,const qa_q2_server_admission *claim,qa_error *error)
{
    frontend_network_q2_host *host=context; q2_host_peer *peer=claim?claim->source_claim:NULL;
    if(!peer) return true;
    if(peer->host!=host) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 cleanup names another Source claim");
    if(peer->committed) for(size_t i=0;i<peer->admission.connection.seat_count;++i) {
        qa_actor_id actor;
        if(qa_application_remote_player_actor(host->options.frontend->application,peer->client,peer->bindings[i].seat,&actor) &&
            !qa_application_remote_player_detach(host->options.frontend->application,peer->client,peer->bindings[i].seat,error)) return false;
    }
    configs_free(peer); qa_application_network_q2_destroy(peer->source); *peer=(q2_host_peer){.host=host}; return true;
}
static bool prepare(void *context,const qa_net_address *address,const qa_q2_connect_request *request,
    qa_q2_server_admission *out,bool *allowed,char reason[1024],qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!out || !allowed || !reason || !request || !current(host,error)) return false;
    *allowed=false; *out=(qa_q2_server_admission){0}; reason[0]=0;
    size_t seats=request->social_count?request->social_count:1;
    if(host->options.lobby && !qa_kex_lan_admitted(host->options.lobby,address)) {
        snprintf(reason,1024,"Connection has not completed LAN admission."); return true;
    }
    if(seats>qa_network_protocol_seat_capacity(request->protocol)) {
        snprintf(reason,1024,"Connection has too many Source seats."); return true;
    }
    q2_host_peer *peer=NULL;
    for(size_t i=0;i<host->capacity;++i) if(!host->peers[i].reserved) { peer=&host->peers[i]; break; }
    if(!peer) { snprintf(reason,1024,"Server is full."); return true; }
    *peer=(q2_host_peer){.host=host,.reserved=true};
    out->source_claim=peer;
    size_t count=0;
    for(uint32_t i=1;i<=host->source.client_slots && count<seats;++i) {
        qa_application_network_q2_client_slot slot;
        if(!qa_application_network_q2_slot(host->discovery,i,&slot,error)) return false;
        bool claimed=slot.occupied;
        for(size_t p=0;!claimed && p<host->capacity;++p) if(host->peers[p].reserved && &host->peers[p]!=peer)
            for(size_t s=0;s<host->peers[p].admission.connection.seat_count;++s) if(host->peers[p].slots[s]==i) claimed=true;
        if(!claimed) {
            peer->slots[count]=i;
            peer->bindings[count]=(qa_net_seat_binding){{QA_NETWORK_COMMAND_OWNER,256u+i},(uint32_t)count}; ++count;
        }
    }
    if(count!=seats) { snprintf(reason,1024,"Server is full."); return true; }
    if(!qa_application_network_q2_create(host->options.frontend->application,request->protocol,1,&peer->source,error)) return false;
    qa_application_network_q2_bindings bindings={.runtime=host->options.runtime,.context=peer,
        .input=input,.drop=drop,.recipient_context=host,.recipient=recipient};
    if(!qa_application_network_q2_hooks(peer->source,&bindings,&peer->source_hooks,error)) return false;
    peer->admission.hooks=(qa_network_q2_server_hooks){peer,source_player,source_game_state,source_begin,source_input,
        source_expand,source_command,source_userinfo,source_download,source_drop};
    peer->admission.connection=(qa_net_connect){.attachment=QA_NET_REMOTE,.endpoint=*address,
        .protocol=request->protocol,.seats=peer->bindings,.seat_count=seats,.composition=host->options.composition};
    peer->admission.policy=(qa_network_q2_server_policy){
        .channel={.protocol=request->protocol,.server=true,.new_channel=request->new_channel,
            .compress=request->compression,.qport=request->qport,.payload_bytes=request->payload_bytes,.datagram_bytes=65507},
        .max_clients=host->source.client_slots,.history_capacity=64,.server_count=1,
        .source_interval_ns=host->source.source.clock_config.interval_ns};
    peer->admission.source_claim=peer;
    memcpy(peer->userinfo,request->userinfo,sizeof(peer->userinfo));
    *out=peer->admission; *allowed=true; return true;
}
static bool committed(void *context,const qa_q2_server_admission *claim,qa_net_client_id id,qa_error *error)
{
    frontend_network_q2_host *host=context; q2_host_peer *peer=claim?claim->source_claim:NULL;
    if(!peer || peer->host!=host || !peer->reserved || peer->committed || !current(host,error)) return false;
    peer->client=id; peer->committed=true;
    for(size_t i=0;i<peer->admission.connection.seat_count;++i) {
        char name[256];
        if(!qa_q3_info_value(peer->userinfo,"name",name,sizeof(name),error)) return false;
        qa_application_remote_player_request player={.client=id,.seat=peer->bindings[i].seat,
            .application_seat=peer->bindings[i].seat.index,.source_slot=peer->slots[i],
            .name=name,.team="",.skin="",.userinfo=peer->userinfo,.defer_source_begin=true};
        qa_actor_id actor;
        if(!qa_application_remote_player_attach(host->options.frontend->application,&player,&actor,error)) return false;
        qa_network_q2_player actual;
        if(!qa_application_network_q2_player(peer->source,actor,&actual,error) || actual.source_slot!=peer->slots[i]) return false;
    }
    return true;
}
bool frontend_network_q2_host_create(const frontend_network_q2_host_options *options,
    frontend_network_q2_host **out,qa_error *error)
{
    if(!options || !out || *out || !options->frontend || !options->runtime || !options->admin || !options->current || !options->random)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 HOST needs its actual Source and sole runtime");
    frontend_network_q2_host *host=calloc(1,sizeof(*host));
    if(!host) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating Q2 HOST");
    *out=host; host->options=*options;
    if(!qa_application_network_q2_host_source(options->frontend->application,options->protocol,&host->source,error) ||
        !qa_application_network_q2_create(options->frontend->application,options->protocol,1,&host->discovery,error)) return false;
    host->capacity=host->source.client_slots;
    host->peers=calloc(host->capacity,sizeof(*host->peers));
    if(!host->peers) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating actual Q2 Source claims");
    for(size_t i=0;i<host->capacity;++i) host->peers[i].host=host;
    qa_q2_server_bootstrap_options bootstrap={.protocols=&host->options.protocol,.protocol_count=1,
        .challenge_capacity=1024,.pending_capacity=32,.random=options->random,.random_context=options->context,
        .hooks={host,enabled,rejects,options->lobby?transport_admitted:NULL,prepare,committed,abort_claim,discovery,download_server}};
    return qa_network_q2_bootstrap_server(options->runtime,&bootstrap,&host->bootstrap,error);
}
bool frontend_network_q2_host_admit(frontend_network_q2_host *host,const qa_net_connect *request,
    bool *recognized,qa_error *error)
{
    if(!recognized) return false;
    *recognized=host && request && request->protocol.kind==host->options.protocol.kind;
    if(!*recognized) return true;
    if(!current(host,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i]; const qa_net_connect *claim=&peer->admission.connection;
        if(!peer->reserved || peer->committed || request->seat_count!=claim->seat_count ||
            !qa_net_address_equal(&request->endpoint,&claim->endpoint,true) ||
            request->protocol.kind!=claim->protocol.kind || request->protocol.revision!=claim->protocol.revision ||
            request->protocol.flags!=claim->protocol.flags || !qa_sha256_equal(&request->composition,&claim->composition)) continue;
        bool same=true;
        for(size_t s=0;s<request->seat_count;++s) same=same && request->seats[s].seat.owner==peer->bindings[s].seat.owner &&
            request->seats[s].seat.index==peer->bindings[s].seat.index && request->seats[s].remote_index==s;
        if(same) return true;
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 attach has no actual reserved Source claim");
}
bool frontend_network_q2_host_receive(frontend_network_q2_host *host,const qa_net_datagram *packet,
    bool *recognized,qa_error *error)
{
    if(!host || host->calls || !current(host,error)) return false;
    ++host->calls; bool ok=qa_network_q2_bootstrap_receive(host->bootstrap,packet,recognized,error); --host->calls; return ok;
}
bool frontend_network_q2_host_tick(frontend_network_q2_host *host,uint64_t now,qa_error *error)
{
    if(!host || host->calls || !current(host,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i];
        if(!peer->retiring) continue;
        if(qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client) &&
            !qa_network_detach(host->options.runtime,peer->client,peer->reason,error)) return false;
        qa_q2_server_admission claim=peer->admission;
        if(!abort_claim(host,&claim,error)) return false;
    }
    ++host->calls;
    bool ok=qa_network_q2_bootstrap_continue(host->bootstrap,now,error) && qa_network_q2_bootstrap_tick(host->bootstrap,now,error);
    --host->calls; return ok;
}
bool frontend_network_q2_host_publish(frontend_network_q2_host *host,uint64_t now,qa_error *error)
{
    if(!host || host->calls || !current(host,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i];
        const qa_net_client *client=peer->committed?qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client):NULL;
        if(!client || peer->retiring || client->phase!=QA_NET_ACTIVE) continue;
        qa_q2_wire_frame frame; const qa_q2_source_motion *motion;
        if(!qa_application_network_q2_client_frame(peer->source,peer->client,&frame,error) ||
            !qa_application_network_q2_motion(peer->source,&motion,error)) return false;
        qa_q2_game_state state;
        if(!peer->configs || !peer->source_hooks.game_state(peer->source_hooks.context,peer->client,&state,error)) return false;
        const char **values=calloc(peer->config_count,sizeof(*values));
        if(!values) return frontend_fail(error,QA_ERROR_MEMORY,"Observing actual Q2 configstring changes");
        bool ok=true;
        for(size_t c=0;ok && c<state.config_count;++c) {
            uint16_t index=state.configs[c].index;
            ok=index<peer->config_count && !values[index];
            if(ok) values[index]=state.configs[c].value;
        }
        for(size_t c=0;ok && c<peer->config_count;++c) {
            const char *next=values[c]?values[c]:"",*old=peer->configs[c]?peer->configs[c]:"";
            if(!strcmp(next,old)) continue;
            char *copy=NULL;
            if(!config_copy(&copy,next,error)) { ok=false; break; }
            qa_q2_server_event event={.kind=QA_Q2_SVC_CONFIGSTRING,.data.config={(uint16_t)c,next}};
            ok=qa_network_q2_server_event(host->options.runtime,peer->client,&event,0,true,error);
            if(ok) { free(peer->configs[c]); peer->configs[c]=copy; } else free(copy);
        }
        free(values);
        if(!ok || !qa_network_q2_server_frame(host->options.runtime,peer->client,&frame,motion,now,error) || !current(host,error)) return false;
    }
    return true;
}
void frontend_network_q2_host_disconnected(frontend_network_q2_host *host,qa_net_client_id id)
{
    if(!host) return;
    for(size_t i=0;i<host->capacity;++i) if(host->peers[i].committed && qa_net_client_id_equal(host->peers[i].client,id))
        host->peers[i].retiring=true;
}
bool frontend_network_q2_host_idle(const frontend_network_q2_host *host)
{ return !host || !host->calls; }
bool frontend_network_q2_host_destroy(frontend_network_q2_host **owned,qa_error *error)
{
    frontend_network_q2_host *host=owned?*owned:NULL;
    if(!host) return true;
    if(host->calls || !qa_network_callbacks_idle(host->options.runtime)) return false;
    for(size_t i=0;host->peers && i<host->capacity;++i) if(host->peers[i].reserved) {
        q2_host_peer *peer=&host->peers[i];
        if(peer->committed && qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client) &&
            !qa_network_detach(host->options.runtime,peer->client,"Q2 HOST closed",error)) return false;
        qa_q2_server_admission claim=peer->admission;
        if(!abort_claim(host,&claim,error)) return false;
    }
    qa_network_q2_bootstrap_destroy(host->bootstrap); qa_application_network_q2_destroy(host->discovery);
    free(host->peers); free(host); *owned=NULL; return true;
}
