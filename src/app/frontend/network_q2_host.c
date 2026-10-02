#include "network_q2_host.h"
#include "qa/application_network.h"
#include "qa/network_q3.h"
#include "qa/network_local.h"
#include "remote_q2_source.h"
#include "network_q2_events.h"
#include "qa/launch_identity.h"
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
    uint64_t event_generation;
    size_t event_cursor;
    qa_buffer event_packet;
    bool event_pending,event_reliable;
    bool reserved,committed,retiring;
    bool material_scripts;
    qa_application_network_q2 *travel_source;
    bool travel_installed;
} q2_host_peer;
typedef struct q2_local_peer {
    frontend_network_q2_host *host;
    qa_network_local_player player;
    qa_net_seat_binding binding;
    qa_net_client_id client;
    uint32_t physical,authored;
    bool admitting,travel_restarted;
} q2_local_peer;
struct frontend_network_q2_host {
    frontend_network_q2_host_options options;
    qa_network_q2_bootstrap *bootstrap;
    qa_application_network_q2 *discovery;
    qa_application_network_q2_host source;
    qa_q2_unicast_cache *unicast;
    q2_host_peer *peers;
    q2_local_peer *locals;
    size_t local_count;
    size_t capacity;
    unsigned calls;
    qa_application_network_q2 *travel_discovery;
    qa_application_network_q2_host travel_target;
    size_t travel_cursor,travel_local_cursor;
    int32_t server_count;
    bool traveling,travel_discovery_installed;
};
static bool local_player(void *context,qa_net_seat_id seat,qa_network_local_player *out,qa_error *error)
{
    q2_local_peer *local=context; qa_actor_id actor;
    if(!local || !out || seat.owner!=local->binding.seat.owner || seat.index!=local->binding.seat.index ||
        !local->host->options.current(local->host->options.context,local->host) ||
        !qa_application_player_actor(local->host->options.frontend->application,local->authored,&actor) ||
        !qa_actor_id_equal(actor,local->player.actor))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local connection lost its actual human Source binding");
    *out=local->player; return true;
}
static bool current(frontend_network_q2_host *host,qa_error *error)
{
    qa_application_network_q2_host source;
    return host && host->options.current(host->options.context,host) &&
        qa_application_network_q2_host_source(host->options.frontend->application,host->options.protocol,&source,error) &&
        source.source.source_owner==host->source.source.source_owner && source.client_slots==host->source.client_slots &&
        source.source.map_revision==host->source.source.map_revision &&
        source.source.publication==host->source.source.publication;
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
            bool found=qa_application_remote_player_actor(host->options.frontend->application,client->id,client->seats[s].seat,&actual);
            if(!found && client->attachment==QA_NET_LOCAL_SEAT) {
                qa_network_local_player local;
                found=qa_network_local_player_read(host->options.runtime,client->id,&local,error);
                if(!found) return false;
                actual=local.actor;
            }
            if(!found || !qa_actor_id_equal(actual,actor)) continue;
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
static bool host_input(void *context,qa_net_client_id id,qa_net_seat_id seat,const qa_network_q2_player *player,
    const qa_q2_usercmd *wire,uint64_t sequence,qa_error *error)
{
    frontend_network_q2_host *host=context;
    for(size_t i=0;i<host->capacity;++i) if(host->peers[i].committed && qa_net_client_id_equal(host->peers[i].client,id))
        return input(&host->peers[i],id,seat,player,wire,sequence,error);
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source input has no actual admitted remote owner");
}
static bool unicast(void *context,const qa_q2_unicast_claim *claim,bool remember,
    bool *duplicate,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!host || !claim || !duplicate || !host->unicast ||
        !host->options.current(host->options.context,host) ||
        claim->source!=host->source.source.source_owner ||
        !qa_net_connections_get(qa_network_connections(host->options.runtime),claim->client) ||
        claim->connection_epoch!=qa_network_epoch(host->options.runtime,claim->client))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 unicast lost its real Source and canonical client");
    if(!qa_q2_unicast_check(host->unicast,claim,duplicate,error)) return false;
    if(*duplicate) return true;
    return remember ? qa_q2_unicast_remember(host->unicast,claim,error) :
        qa_q2_unicast_reserve(host->unicast,error);
}
static bool host_drop(void *context,qa_net_client_id id,const char *reason,qa_error *error)
{
    frontend_network_q2_host *host=context;
    for(size_t i=0;i<host->capacity;++i) if(host->peers[i].committed && qa_net_client_id_equal(host->peers[i].client,id))
        return drop(&host->peers[i],id,reason,error);
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source drop has no actual admitted remote owner");
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
static bool retire_source(frontend_network_q2_host *host,q2_host_peer *peer,qa_error *error)
{
    if(peer->committed) for(size_t i=0;i<peer->admission.connection.seat_count;++i) {
        qa_actor_id actor;
        if(qa_application_remote_player_actor(host->options.frontend->application,peer->client,peer->bindings[i].seat,&actor) &&
            !qa_application_remote_player_detach(host->options.frontend->application,peer->client,peer->bindings[i].seat,error)) return false;
    }
    return true;
}
static bool abort_claim(void *context,const qa_q2_server_admission *claim,qa_error *error)
{
    frontend_network_q2_host *host=context; q2_host_peer *peer=claim?claim->source_claim:NULL;
    if(!peer) return true;
    if(peer->host!=host) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 cleanup names another Source claim");
    if(!retire_source(host,peer,error)) return false;
    qa_q2_unicast_remove_client(host->unicast,peer->client);
    configs_free(peer); qa_buffer_free(&peer->event_packet);
    qa_application_network_q2_destroy(peer->source); qa_application_network_q2_destroy(peer->travel_source);
    *peer=(q2_host_peer){.host=host}; return true;
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
    if(!qa_application_network_q2_create(host->options.frontend->application,request->protocol,host->server_count,&peer->source,error)) return false;
    if(!frontend_remote_q2_source_material_scripts(request,&peer->material_scripts,error) ||
        !qa_application_network_q2_material_capability(peer->source,peer->material_scripts,error)) return false;
    qa_application_network_q2_bindings bindings={.runtime=host->options.runtime,.context=peer,
        .input=input,.drop=drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
    if(!qa_application_network_q2_hooks(peer->source,&bindings,&peer->source_hooks,error)) return false;
    peer->admission.hooks=(qa_network_q2_server_hooks){peer,source_player,source_game_state,source_begin,source_input,
        source_expand,source_command,source_userinfo,source_download,source_drop};
    peer->admission.connection=(qa_net_connect){.attachment=QA_NET_REMOTE,.endpoint=*address,
        .protocol=request->protocol,.seats=peer->bindings,.seat_count=seats,.composition=host->options.composition};
    peer->admission.policy=(qa_network_q2_server_policy){
        .channel={.protocol=request->protocol,.server=true,.new_channel=request->new_channel,
            .compress=request->compression,.qport=request->qport,.payload_bytes=request->payload_bytes,.datagram_bytes=65507},
        .max_clients=host->source.client_slots,.history_capacity=64,.server_count=host->server_count,
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
    peer->event_generation=qa_application_protocol_events_generation(host->options.frontend->application);
    peer->event_cursor=qa_application_protocol_event_count(host->options.frontend->application);
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
    *out=host; host->options=*options; host->server_count=1;
    if(!qa_q2_unicast_cache_create(&host->unicast,error)) return false;
    if(!qa_application_network_q2_host_source(options->frontend->application,options->protocol,&host->source,error) ||
        !qa_application_network_q2_create(options->frontend->application,options->protocol,1,&host->discovery,error)) return false;
    host->capacity=host->source.client_slots;
    host->peers=calloc(host->capacity,sizeof(*host->peers));
    if(!host->peers) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating actual Q2 Source claims");
    for(size_t i=0;i<host->capacity;++i) host->peers[i].host=host;
    qa_application_network_q2_bindings source_bindings={.runtime=options->runtime,.context=host,
        .input=host_input,.drop=host_drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
    qa_network_q2_server_hooks source_hooks;
    if(!qa_application_network_q2_hooks(host->discovery,&source_bindings,&source_hooks,error)) return false;
    host->local_count=options->frontend->options.seats;
    host->locals=calloc(host->local_count?host->local_count:1,sizeof(*host->locals));
    if(!host->locals) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine human Source connections");
    for(size_t i=0;i<host->local_count;++i) {
        q2_local_peer *local=&host->locals[i]; uint32_t authored; qa_actor_id actor;
        if(!frontend_seat_launch_id_read(options->frontend,(uint32_t)i,&authored) ||
            !qa_application_player_actor(options->frontend->application,authored,&actor)) continue;
        qa_network_q2_player physical;
        if(!qa_application_network_q2_player(host->discovery,actor,&physical,error)) return false;
        *local=(q2_local_peer){.host=host,.player={actor,physical.source_owner,physical.source_slot},
            .binding={{QA_NETWORK_COMMAND_OWNER,64u+(uint32_t)i},0},.physical=(uint32_t)i,.authored=authored,.admitting=true};
        qa_net_connect request={.attachment=QA_NET_LOCAL_SEAT,.endpoint={.kind=QA_NET_LOOPBACK,.port=(uint16_t)(i+1)},
            .protocol=options->protocol,.seats=&local->binding,.seat_count=1,.composition=options->composition};
        qa_network_local_hooks hooks={local,local_player};
        if(!qa_network_attach_local(options->runtime,&request,&hooks,options->frontend->wall_time_ns,&local->client,error)) return false;
        local->admitting=false;
        if(!qa_network_phase(options->runtime,local->client,QA_NET_PRIMED,error) ||
            !qa_network_phase(options->runtime,local->client,QA_NET_ACTIVE,error)) return false;
    }
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
    if(request->attachment==QA_NET_LOCAL_SEAT) {
        for(size_t i=0;i<host->local_count;++i) {
            q2_local_peer *local=&host->locals[i];
            if(!local->player.actor.registry || request->seat_count!=1 || !request->seats ||
                request->seats[0].seat.owner!=local->binding.seat.owner ||
                request->seats[0].seat.index!=local->binding.seat.index || request->seats[0].remote_index!=0 ||
                request->endpoint.kind!=QA_NET_LOOPBACK || request->endpoint.port!=i+1 ||
                !qa_sha256_equal(&request->composition,&host->options.composition)) continue;
            qa_network_local_player actual;
            return local_player(local,local->binding.seat,&actual,error);
        }
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local connection has no genuine human Source claim");
    }
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
static bool source_identity_equal(const qa_application_network_q2_host *a,const qa_application_network_q2_host *b)
{
    return a->source.source_owner==b->source.source_owner && a->source.publication==b->source.publication &&
        a->source.map_revision==b->source.map_revision && a->client_slots==b->client_slots &&
        a->source.clock_config.interval_ns==b->source.clock_config.interval_ns;
}
static bool refresh_source(frontend_network_q2_host *host,qa_error *error)
{
    qa_application_network_q2_host actual;
    if(!host->options.current(host->options.context,host) ||
        !qa_application_network_q2_host_source(host->options.frontend->application,host->options.protocol,&actual,error)) return false;
    if(!host->traveling && source_identity_equal(&actual,&host->source)) return true;
    if(!host->traveling) {
        if(actual.client_slots!=host->source.client_slots || host->server_count==INT32_MAX)
            return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Q2 travel changed its retained connection capacity or exhausted servercount");
        for(size_t i=0;i<host->capacity;++i) if(host->peers[i].reserved && !host->peers[i].committed)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 travel retains an unfinished native admission");
        host->traveling=true; host->travel_discovery_installed=false;
        host->travel_target=actual; host->travel_cursor=0; host->travel_local_cursor=0;
    }
    if(!source_identity_equal(&actual,&host->travel_target))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source changed during its retained travel continuation");
    if(!host->travel_discovery_installed) {
        if(!host->travel_discovery && !qa_application_network_q2_create(host->options.frontend->application,host->options.protocol,
            host->server_count+1,&host->travel_discovery,error)) return false;
        qa_application_network_q2_bindings bindings={.runtime=host->options.runtime,.context=host,
            .input=host_input,.drop=host_drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
        qa_network_q2_server_hooks hooks;
        if(!qa_application_network_q2_hooks(host->travel_discovery,&bindings,&hooks,error)) return false;
        qa_buffer identity={0};
        if(!qa_launch_identity_encode(qa_application_launch(host->options.frontend->application),
            qa_session_actors(qa_application_session(host->options.frontend->application)),&identity,error)) return false;
        qa_sha256((qa_bytes){identity.data,identity.size},&host->options.composition); qa_buffer_free(&identity);
        qa_application_network_q2_destroy(host->discovery); host->discovery=host->travel_discovery;
        host->travel_discovery=NULL; host->source=actual; ++host->server_count;
        host->travel_discovery_installed=true;
    }
    for(;host->travel_local_cursor<host->local_count;++host->travel_local_cursor) {
        q2_local_peer *local=&host->locals[host->travel_local_cursor];
        if(!local->client.owner) continue;
        qa_actor_id actor; qa_network_q2_player player;
        if(!qa_application_player_actor(host->options.frontend->application,local->authored,&actor) ||
            !qa_application_network_q2_player(host->discovery,actor,&player,error)) return false;
        local->player=(qa_network_local_player){actor,player.source_owner,player.source_slot};
        if(!local->travel_restarted) {
            if(!qa_network_local_player_refresh(host->options.runtime,local->client,error) ||
                !qa_network_restart(host->options.runtime,local->client,&host->options.composition,error)) return false;
            local->travel_restarted=true;
        }
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(host->options.runtime),local->client);
        if(!client || (client->phase<QA_NET_PRIMED &&
            !qa_network_phase(host->options.runtime,local->client,QA_NET_PRIMED,error)) ||
            (client->phase<QA_NET_ACTIVE && !qa_network_phase(host->options.runtime,local->client,QA_NET_ACTIVE,error))) return false;
    }
    for(;host->travel_cursor<host->capacity;++host->travel_cursor) {
        q2_host_peer *peer=&host->peers[host->travel_cursor];
        const qa_net_client *client=peer->committed?qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client):NULL;
        if(!client || peer->retiring) continue;
        qa_network_q2_state transport;
        if(!qa_network_q2_state_read(host->options.runtime,peer->client,&transport,error)) return false;
        if(transport.retiring) continue;
        if(!peer->travel_installed) {
            if(transport.server_count==INT32_MAX) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 peer servercount is exhausted");
            if(!peer->travel_source && !qa_application_network_q2_create(host->options.frontend->application,
                client->protocol,transport.server_count+1,&peer->travel_source,error)) return false;
            qa_application_network_q2_bindings bindings={.runtime=host->options.runtime,.context=peer,
                .input=input,.drop=drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
            qa_network_q2_server_hooks hooks;
            if(!qa_application_network_q2_material_capability(peer->travel_source,peer->material_scripts,error) ||
                !qa_application_network_q2_hooks(peer->travel_source,&bindings,&hooks,error) ||
                !qa_network_q2_server_prepare_restart(host->options.runtime,peer->client,actual.client_slots,
                    actual.source.clock_config.interval_ns,error)) return false;
            qa_application_network_q2_destroy(peer->source); peer->source=peer->travel_source;
            peer->travel_source=NULL; peer->source_hooks=hooks; peer->travel_installed=true;
        }
        if(!qa_network_restart(host->options.runtime,peer->client,&host->options.composition,error)) return false;
        peer->admission.policy.server_count=transport.server_count+1;
        peer->admission.policy.source_interval_ns=actual.source.clock_config.interval_ns;
        peer->admission.connection.composition=host->options.composition;
        configs_free(peer); qa_buffer_free(&peer->event_packet); peer->event_pending=false;
        peer->event_generation=qa_application_protocol_events_generation(host->options.frontend->application);
        peer->event_cursor=qa_application_protocol_event_count(host->options.frontend->application);
        peer->travel_installed=false;
    }
    for(size_t i=0;i<host->local_count;++i) host->locals[i].travel_restarted=false;
    host->traveling=false; return current(host,error);
}
bool frontend_network_q2_host_tick(frontend_network_q2_host *host,uint64_t now,qa_error *error)
{
    if(!host || host->calls || !refresh_source(host,error) || !current(host,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i];
        if(!peer->retiring) continue;
        if(!retire_source(host,peer,error)) return false;
        if(qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client) &&
            !qa_network_detach(host->options.runtime,peer->client,peer->reason,error)) return false;
        qa_q2_server_admission claim=peer->admission;
        if(!abort_claim(host,&claim,error)) return false;
    }
    ++host->calls;
    bool ok=qa_network_q2_bootstrap_continue(host->bootstrap,now,error) && qa_network_q2_bootstrap_tick(host->bootstrap,now,error);
    --host->calls; return ok;
}
static bool publish_configs(q2_host_peer *peer,qa_error *error)
{
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
        ok=qa_network_q2_server_event(peer->host->options.runtime,peer->client,&event,0,true,error);
        if(ok) { free(peer->configs[c]); peer->configs[c]=copy; } else free(copy);
    }
    free(values); return ok;
}
static bool publish_event_packet(q2_host_peer *peer,qa_error *error)
{
    if(!publish_configs(peer,error) ||
        !qa_network_q2_server_bytes(peer->host->options.runtime,peer->client,
            (qa_bytes){peer->event_packet.data,peer->event_packet.size},0,peer->event_reliable,error)) return false;
    qa_buffer_free(&peer->event_packet); peer->event_pending=false; ++peer->event_cursor;
    return true;
}
static bool publish_events(q2_host_peer *peer,const qa_net_client *client,size_t capacity,qa_error *error)
{
    qa_application *app=peer->host->options.frontend->application;
    if(peer->event_pending && !publish_event_packet(peer,error)) return false;
    uint64_t generation=qa_application_protocol_events_generation(app);
    size_t count=qa_application_protocol_event_count(app);
    if(peer->event_generation!=generation) {
        peer->event_generation=generation; peer->event_cursor=0;
    }
    if(peer->event_cursor>count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 event cursor exceeds its retained Source generation");
    const qa_q2_codec *codec;
    if(!qa_network_q2_server_codec(peer->host->options.runtime,peer->client,&codec,error)) return false;
    while(peer->event_cursor<count) {
        qa_application_protocol_event event;
        qa_application_q2_protocol_delivery delivery;
        if(!qa_application_protocol_event_at(app,peer->event_cursor,&event) ||
            !qa_application_protocol_q2_delivery_at(app,peer->event_cursor,&delivery))
            return frontend_fail(error,QA_ERROR_FORMAT,"Q2 Source event disappeared before publication");
        if(event.signon || !delivery.original) { ++peer->event_cursor; continue; }
        if(!frontend_network_q2_event_packet(peer->source,peer->host->source.source.source_owner,
            &event,&delivery,client,qa_network_epoch(peer->host->options.runtime,peer->client),codec,
            capacity,&peer->event_packet,error)) return false;
        if(!peer->event_packet.size) { ++peer->event_cursor; continue; }
        peer->event_reliable=event.reliable; peer->event_pending=true;
        if(!publish_event_packet(peer,error)) return false;
    }
    return generation==qa_application_protocol_events_generation(app) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 publication replaced its retained Source event generation");
}
bool frontend_network_q2_host_publish(frontend_network_q2_host *host,uint64_t now,qa_error *error)
{
    if(!host || host->calls || !current(host,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i];
        const qa_net_client *client=peer->committed?qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client):NULL;
        if(!client || peer->retiring || client->phase!=QA_NET_ACTIVE) continue;
        qa_network_q2_state transport;
        if(!qa_network_q2_state_read(host->options.runtime,peer->client,&transport,error)) return false;
        if(transport.retiring) continue;
        if(!publish_events(peer,client,transport.channel.capacity,error)) return false;
        qa_q2_wire_frame frame; const qa_q2_source_motion *motion;
        if(!qa_application_network_q2_client_frame(peer->source,peer->client,&frame,error) ||
            !qa_application_network_q2_motion(peer->source,&motion,error) || !publish_configs(peer,error) ||
            !qa_network_q2_server_frame(host->options.runtime,peer->client,&frame,motion,now,error) || !current(host,error)) return false;
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
{ return !host || (!host->calls && !host->traveling); }
static bool publisher_content_visit(const qa_application_network_q2 *publisher,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    for(size_t i=0;i<qa_application_network_q2_resource_count(publisher);++i) {
        qa_application_network_q2_resource_view held;
        if(!qa_application_network_q2_resource_read(publisher,i,&held,error) ||
            !visitor->pool(visitor->context,qa_vfs_resources(held.view),error) ||
            !visitor->view(visitor->context,held.view,error)) return false;
    }
    return true;
}
bool frontend_network_q2_host_content_visit(const frontend_network_q2_host *host,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    if(!host) return true;
    if(host->calls || !host->options.current(host->options.context,host) || !visitor ||
        !visitor->pool || !visitor->view || !publisher_content_visit(host->discovery,visitor,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        const q2_host_peer *peer=&host->peers[i];
        if(!publisher_content_visit(peer->source,visitor,error)) return false;
        if(!peer->committed || !qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client)) continue;
        const qa_vfs *view=NULL; const qa_resource *resource=NULL; const qa_vfs_acquisition *opening=NULL;
        if(!qa_network_q2_server_download(host->options.runtime,peer->client,&view,&resource,&opening,error)) return false;
        if(view && ((resource?(!opening || !qa_vfs_acquisition_retained(view,opening,error)):opening!=NULL) ||
            !visitor->pool(visitor->context,qa_vfs_resources(view),error) || !visitor->view(visitor->context,view,error))) return false;
    }
    return host->options.current(host->options.context,host);
}
bool frontend_network_q2_host_local_hooks(frontend_network_q2_host *host,const qa_net_client *client,
    qa_network_local_hooks *out,qa_error *error)
{
    if(!host || !client || !out || client->attachment!=QA_NET_LOCAL_SEAT || client->seat_count!=1) return false;
    for(size_t i=0;i<host->local_count;++i) {
        q2_local_peer *local=&host->locals[i];
        if(client->seats[0].seat.owner!=local->binding.seat.owner || client->seats[0].seat.index!=local->binding.seat.index) continue;
        qa_network_local_player actual;
        if(!local_player(local,local->binding.seat,&actual,error)) return false;
        *out=(qa_network_local_hooks){local,local_player}; return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Restored local connection has no actual human Source owner");
}
bool frontend_network_q2_host_destroy(frontend_network_q2_host **owned,qa_error *error)
{
    frontend_network_q2_host *host=owned?*owned:NULL;
    if(!host) return true;
    if(host->calls || !qa_network_callbacks_idle(host->options.runtime)) return false;
    for(size_t i=0;host->locals && i<host->local_count;++i) {
        q2_local_peer *local=&host->locals[i];
        if(local->client.owner && qa_net_connections_get(qa_network_connections(host->options.runtime),local->client) &&
            !qa_network_detach(host->options.runtime,local->client,"Local Source closed",error)) return false;
    }
    for(size_t i=0;host->peers && i<host->capacity;++i) if(host->peers[i].reserved) {
        q2_host_peer *peer=&host->peers[i];
        if(!retire_source(host,peer,error)) return false;
        if(peer->committed && qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client) &&
            !qa_network_detach(host->options.runtime,peer->client,"Q2 HOST closed",error)) return false;
        qa_q2_server_admission claim=peer->admission;
        if(!abort_claim(host,&claim,error)) return false;
    }
    qa_network_q2_bootstrap_destroy(host->bootstrap); qa_application_network_q2_destroy(host->discovery);
    qa_application_network_q2_destroy(host->travel_discovery);
    qa_q2_unicast_cache_destroy(host->unicast);
    free(host->peers); free(host->locals); free(host); *owned=NULL; return true;
}
