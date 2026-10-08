#include "network_q2_host_private.h"
#include "qa/application_network.h"
#include "qa/network_q3.h"
#include "qa/network_local.h"
#include "qa/network_events.h"
#include "remote_q2_source.h"
#include "network_q2_events.h"
#include "qa/launch_identity.h"
#include <stdio.h>

static bool demo_publish(frontend_network_q2_host *, qa_error *);
static bool refresh_source(frontend_network_q2_host *, qa_error *);

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
bool frontend_network_q2_host_local_retained(void *context,qa_net_seat_id seat,
    qa_network_local_player *out,qa_error *error)
{
    q2_local_peer *local=context;
    if(!local || !out || !local->host || seat.owner!=local->binding.seat.owner ||
        seat.index!=local->binding.seat.index ||
        !local->host->options.current(local->host->options.context,local->host)) return false;
    qa_application_network_q2_host actual;
    if(!qa_application_network_q2_host_source(local->host->options.frontend->application,
        local->host->options.protocol,&actual,error)) return false;
    if(local->import_historical || local->map_revision!=actual.source.map_revision) {
        if(!local->host->traveling || local->physical<local->host->travel_local_cursor ||
            local->travel_restarted || !local->map_revision || !local->player.actor.registry ||
            !local->player.source_owner || !local->player.source_slot)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Local archive lacks its retained Source travel cursor");
        *out=local->player; return true;
    }
    return local_player(context,seat,out,error);
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
bool frontend_network_q2_host_configs(frontend_network_q2_host *host,
    const qa_q2_config_entry **entries,size_t *count,qa_error *error)
{
    return host && !host->calls && refresh_source(host,error) && current(host,error) &&
        qa_application_network_q2_configs(host->discovery,entries,count,error);
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
    if(raw.kind==QA_MOVEMENT_Q2_RERELEASE) {
        if(wire->upmove>0) raw.buttons|=8u;
        if(wire->upmove<0) raw.buttons|=16u;
        raw.up_move=0;
    }
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
static void configs_dispose(char ***values,size_t *count)
{
    for(size_t i=0;i<*count;++i) free((*values)[i]);
    free(*values); *values=NULL; *count=0;
}
static void configs_free(q2_host_peer *peer)
{
    configs_dispose(&peer->configs,&peer->config_count);
    configs_dispose(&peer->signon_configs,&peer->signon_config_count);
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
    const qa_q2_codec *codec; qa_q2_config_layout layout;
    if(!qa_network_q2_server_codec(peer->host->options.runtime,id,&codec,error)) return false;
    qa_q2_codec declared=*codec;
    if(codec->protocol.kind==QA_NET_Q2PRO_36) {
        uint8_t bytes[sizeof(qa_q2_serverdata)+64]; qa_net_writer writer;
        qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
        if(!qa_q2_write_serverdata(&declared,&writer,&out->data)) return false;
    }
    if(!qa_q2_config_layout_read(&declared,&layout,error)) return false;
    size_t count=layout.max_configs;
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
    configs_dispose(&peer->signon_configs,&peer->signon_config_count);
    peer->signon_configs=configs; peer->signon_config_count=count; return true;
}
static void source_game_state_accepted(void *context,qa_net_client_id id)
{
    q2_host_peer *peer=context;
    configs_dispose(&peer->configs,&peer->config_count);
    peer->configs=peer->signon_configs; peer->config_count=peer->signon_config_count;
    peer->signon_configs=NULL; peer->signon_config_count=0;
    if(peer->source_hooks.game_state_accepted)
        peer->source_hooks.game_state_accepted(peer->source_hooks.context,id);
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
bool frontend_network_q2_host_bind_publisher(frontend_network_q2_host *host,qa_application_network_q2 *publisher,
    qa_network_runtime *runtime,qa_network_q2_server_hooks *hooks,qa_error *error)
{
    if(!host || !publisher || !runtime || !hooks || host->options.runtime!=runtime)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 publisher binding requires its retained HOST and actual runtime");
    qa_application_network_q2_bindings bindings={.runtime=runtime,.context=host,
        .input=host_input,.drop=host_drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
    return qa_application_network_q2_hooks(publisher,&bindings,hooks,error);
}
bool frontend_network_q2_host_bind_peer(q2_host_peer *peer,qa_application_network_q2 *publisher,
    qa_network_runtime *runtime,qa_network_q2_server_hooks *hooks,qa_error *error)
{
    if(!peer || !peer->host || !publisher || !runtime || !hooks || peer->host->options.runtime!=runtime)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 peer publisher binding requires its real retained Source claim");
    qa_application_network_q2_bindings bindings={.runtime=runtime,.context=peer,
        .input=input,.drop=drop,.recipient_context=peer->host,.recipient=recipient,.unicast=unicast};
    if(!qa_application_network_q2_hooks(publisher,&bindings,&peer->source_hooks,error)) return false;
    *hooks=(qa_network_q2_server_hooks){peer,source_player,source_game_state,source_begin,source_input,
        source_expand,source_command,source_userinfo,source_download,source_drop,source_game_state_accepted};
    return true;
}
static bool retire_source(frontend_network_q2_host *host,q2_host_peer *peer,qa_error *error)
{
    if(peer->committed) for(size_t i=0;i<peer->admission.connection.seat_count;++i) {
        if(!qa_application_remote_player_detach(host->options.frontend->application,
            peer->client,peer->bindings[i].seat,error)) return false;
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
    qa_application_network_q2_destroy(peer->import_source);
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
            uint32_t seat_index=257;
            for(;;++seat_index) {
                bool retained=false;
                for(size_t p=0;!retained && p<host->capacity;++p) if(host->peers[p].reserved) {
                    size_t seats_held=&host->peers[p]==peer?count:host->peers[p].admission.connection.seat_count;
                    for(size_t s=0;s<seats_held;++s)
                        if(host->peers[p].bindings[s].seat.index==seat_index) { retained=true; break; }
                }
                if(!retained) break;
            }
            peer->bindings[count]=(qa_net_seat_binding){{QA_NETWORK_COMMAND_OWNER,seat_index},(uint32_t)count}; ++count;
        }
    }
    if(count!=seats) { snprintf(reason,1024,"Server is full."); return true; }
    if(!qa_application_network_q2_create(host->options.frontend->application,request->protocol,host->server_count,&peer->source,error)) return false;
    if(!frontend_remote_q2_source_material_scripts(request,&peer->material_scripts,error) ||
        !qa_application_network_q2_material_capability(peer->source,peer->material_scripts,error)) return false;
    if(!frontend_network_q2_host_bind_peer(peer,peer->source,host->options.runtime,&peer->admission.hooks,error)) return false;
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
    peer->player_event_cursor=qa_application_q2_player_event_count(host->options.frontend->application);
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
qa_q2_server_bootstrap_options frontend_network_q2_host_bootstrap_options(frontend_network_q2_host *host)
{
    return (qa_q2_server_bootstrap_options){.protocols=&host->options.protocol,.protocol_count=1,
        .challenge_capacity=1024,.pending_capacity=32,.random=host->options.random,.random_context=host->options.context,
        .hooks={host,enabled,rejects,host->options.lobby?transport_admitted:NULL,prepare,committed,abort_claim,discovery,download_server}};
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
    if(!host->source.client_slots || host->source.client_slots>256)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 Source exceeds its actual supported client namespace");
    host->capacity=256;
    host->peers=calloc(host->capacity,sizeof(*host->peers));
    if(!host->peers) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating actual Q2 Source claims");
    for(size_t i=0;i<host->capacity;++i) host->peers[i].host=host;
    qa_network_q2_server_hooks source_hooks;
    if(!frontend_network_q2_host_bind_publisher(host,host->discovery,options->runtime,&source_hooks,error)) return false;
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
            .binding={{QA_NETWORK_COMMAND_OWNER,64u+(uint32_t)i},0},.physical=(uint32_t)i,.authored=authored,.admitting=true,
            .map_revision=host->source.source.map_revision,.composition=options->composition};
        qa_net_connect request;
        if(!frontend_network_q2_host_local_request(host,local,&request,error)) return false;
        qa_network_local_hooks hooks={.context=local,.player=local_player,.retained_player=frontend_network_q2_host_local_retained};
        if(!qa_network_attach_local(options->runtime,&request,&hooks,options->frontend->wall_time_ns,&local->client,error)) return false;
        local->admitting=false;
        if(!qa_network_phase(options->runtime,local->client,QA_NET_PRIMED,error) ||
            !qa_network_phase(options->runtime,local->client,QA_NET_ACTIVE,error)) return false;
    }
    qa_q2_server_bootstrap_options bootstrap=frontend_network_q2_host_bootstrap_options(host);
    if(options->local_only) return true;
    return qa_network_q2_bootstrap_server(options->runtime,&bootstrap,&host->bootstrap,error);
}
bool frontend_network_q2_host_admit(frontend_network_q2_host *host,const qa_net_connect *request,
    bool *recognized,qa_error *error)
{
    if(!recognized) return false;
    *recognized=host && request && request->protocol.kind==host->options.protocol.kind &&
        (!host->options.local_only || request->attachment==QA_NET_LOCAL_SEAT);
    if(!*recognized) return true;
    if(!current(host,error)) return false;
    if(request->attachment==QA_NET_LOCAL_SEAT) {
        for(size_t i=0;i<host->local_count;++i) {
            q2_local_peer *local=&host->locals[i];
            if(!local->player.actor.registry || request->seat_count!=1 || !request->seats ||
                request->seats[0].seat.owner!=local->binding.seat.owner ||
                request->seats[0].seat.index!=local->binding.seat.index || request->seats[0].remote_index!=0 ||
                request->composition != host->options.composition) continue;
            qa_net_connect expected;
            if(!frontend_network_q2_host_local_request(host,local,&expected,error)) return false;
            if(!qa_net_address_equal(&request->endpoint,&expected.endpoint,true)) continue;
            qa_network_local_player actual;
            return local_player(local,local->binding.seat,&actual,error);
        }
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local connection has no genuine human Source claim");
    }
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i]; const qa_net_connect *claim=&peer->admission.connection;
        if(peer->committed && peer->travel_installed && host->traveling && host->travel_cursor==i) {
            const qa_net_client *client=qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client);
            if(client && request->attachment==client->attachment && request->seats==client->seats &&
                request->seat_count==client->seat_count && qa_net_address_equal(&request->endpoint,&client->endpoint,true) &&
                request->protocol.kind==client->protocol.kind && request->protocol.revision==client->protocol.revision &&
                request->protocol.flags==client->protocol.flags && (request->composition == host->options.composition))
                return true;
        }
        if(!peer->reserved || peer->committed || request->seat_count!=claim->seat_count ||
            !qa_net_address_equal(&request->endpoint,&claim->endpoint,true) ||
            request->protocol.kind!=claim->protocol.kind || request->protocol.revision!=claim->protocol.revision ||
            request->protocol.flags!=claim->protocol.flags || request->composition != claim->composition) continue;
        bool same=true;
        for(size_t s=0;s<request->seat_count;++s) same=same && request->seats[s].seat.owner==peer->bindings[s].seat.owner &&
            request->seats[s].seat.index==peer->bindings[s].seat.index && request->seats[s].remote_index==s;
        if(same) return true;
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 attach has no actual reserved Source claim");
}
bool frontend_network_q2_host_local_request(const frontend_network_q2_host *host,const q2_local_peer *local,
    qa_net_connect *out,qa_error *error)
{
    char address[64];
    snprintf(address,sizeof(address),"loopback:qa-q2-local-%u",local->physical);
    qa_net_address endpoint;
    if(!qa_net_address_parse(address,0,true,&endpoint,error)) return false;
    *out=(qa_net_connect){.attachment=QA_NET_LOCAL_SEAT,.endpoint=endpoint,
        .protocol=host->options.protocol,.seats=&local->binding,.seat_count=1,.composition=local->composition};
    return true;
}
bool frontend_network_q2_host_receive(frontend_network_q2_host *host,const qa_net_datagram *packet,
    bool *recognized,qa_error *error)
{
    if(!host || host->calls || !current(host,error)) return false;
    if(host->options.local_only) { if(recognized) *recognized=false; return recognized!=NULL; }
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
        if(!actual.client_slots || actual.client_slots>host->capacity || host->server_count==INT32_MAX)
            return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Q2 travel exceeds its retained client namespace or exhausted servercount");
        if(host->options.transport && actual.client_slots>UINT8_MAX)
            return frontend_fail(error,QA_ERROR_UNSUPPORTED,"KEX travel cannot represent the actual Source capacity");
        if(host->options.transport &&
            !qa_kex_transport_set_maximum(host->options.transport,(uint8_t)actual.client_slots,error)) return false;
        for(size_t i=0;i<host->capacity;++i) if(host->peers[i].reserved && !host->peers[i].committed)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 travel retains an unfinished native admission");
        host->traveling=true; host->travel_discovery_installed=false;
        host->travel_target=actual; host->travel_cursor=0; host->travel_local_cursor=0;
    }
    if(!source_identity_equal(&actual,&host->travel_target))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 Source changed during its retained travel continuation");
    if(!host->travel_discovery_installed) {
        if(!host->travel_discovery && host->import_discovery) {
            host->travel_discovery=host->import_discovery; host->import_discovery=NULL;
        }
        if(!host->travel_discovery && !qa_application_network_q2_create(host->options.frontend->application,host->options.protocol,
            host->server_count+1,&host->travel_discovery,error)) return false;
        qa_application_network_q2_bindings bindings={.runtime=host->options.runtime,.context=host,
            .input=host_input,.drop=host_drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
        qa_network_q2_server_hooks hooks;
        if(!qa_application_network_q2_hooks(host->travel_discovery,&bindings,&hooks,error)) return false;
        host->options.composition=qa_application_configuration_generation(host->options.frontend->application);
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
            if(!qa_network_local_player_refresh(host->options.runtime,local->client,error)) return false;
            local->map_revision=actual.source.map_revision; local->import_historical=false;
            if(!qa_network_restart(host->options.runtime,local->client,&host->options.composition,error)) return false;
            const qa_net_client *restarted=qa_net_connections_get(qa_network_connections(host->options.runtime),local->client);
            if(!restarted) return frontend_fail(error,QA_ERROR_ARGUMENT,"Local Source restart lost its canonical connection");
            local->composition=restarted->composition;
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
            if(!peer->travel_source && peer->import_source) {
                peer->travel_source=peer->import_source; peer->import_source=NULL;
            }
            if(!peer->travel_source && !qa_application_network_q2_create(host->options.frontend->application,
                client->protocol,transport.server_count+1,&peer->travel_source,error)) return false;
            uint32_t source_slots[QA_NETWORK_MAX_SEATS];
            for(size_t s=0;s<peer->admission.connection.seat_count;++s) {
                qa_actor_id actor; qa_network_q2_player player;
                if(!qa_application_remote_player_actor(host->options.frontend->application,
                    peer->client,peer->bindings[s].seat,&actor) ||
                    !qa_application_network_q2_player(peer->travel_source,actor,&player,error)) return false;
                source_slots[s]=player.source_slot;
            }
            qa_application_network_q2_bindings bindings={.runtime=host->options.runtime,.context=peer,
                .input=input,.drop=drop,.recipient_context=host,.recipient=recipient,.unicast=unicast};
            qa_network_q2_server_hooks hooks;
            if(!qa_application_network_q2_material_capability(peer->travel_source,peer->material_scripts,error) ||
                !qa_application_network_q2_hooks(peer->travel_source,&bindings,&hooks,error) ||
                !qa_network_q2_server_prepare_restart(host->options.runtime,peer->client,actual.client_slots,
                    actual.source.clock_config.interval_ns,error)) return false;
            peer->admission.policy.max_clients=actual.client_slots;
            peer->admission.policy.source_interval_ns=actual.source.clock_config.interval_ns;
            for(size_t s=0;s<peer->admission.connection.seat_count;++s) peer->slots[s]=source_slots[s];
            qa_application_network_q2_destroy(peer->source); peer->source=peer->travel_source;
            peer->travel_source=NULL; peer->source_hooks=hooks; peer->travel_installed=true;
        }
        if(!qa_network_restart(host->options.runtime,peer->client,&host->options.composition,error)) return false;
        peer->admission.policy.server_count=transport.server_count+1;
        peer->admission.policy.source_interval_ns=actual.source.clock_config.interval_ns;
        peer->admission.connection.composition=host->options.composition;
        configs_free(peer); qa_buffer_free(&peer->event_packet); peer->event_pending=false; peer->event_player=false;
        peer->event_generation=qa_application_protocol_events_generation(host->options.frontend->application);
        peer->event_cursor=qa_application_protocol_event_count(host->options.frontend->application);
        peer->player_event_cursor=qa_application_q2_player_event_count(host->options.frontend->application);
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
    if(host->options.local_only) return true;
    ++host->calls;
    bool ok=qa_network_q2_bootstrap_continue(host->bootstrap,now,error) && qa_network_q2_bootstrap_tick(host->bootstrap,now,error);
    --host->calls; return ok;
}
typedef bool (*config_emit_fn)(void *, const qa_q2_server_event *, qa_error *);
static bool configs_publish(qa_application_network_q2 *source, char **values, size_t capacity,
    config_emit_fn emit, void *context, qa_error *error)
{
    const qa_q2_config_entry *entries=NULL; size_t count=0;
    if(!values || !qa_application_network_q2_configs(source,&entries,&count,error)) return false;
    for(size_t i=0;i<count;++i)
        if(entries[i].index>=capacity || !entries[i].value ||
            (i && entries[i-1].index>=entries[i].index))
            return frontend_fail(error,QA_ERROR_FORMAT,"Q2 config publication lost its ordered Source namespace");
    bool ok=true; size_t entry=0;
    for(size_t c=0;ok && c<capacity;++c) {
        const char *next=entry<count && entries[entry].index==c?entries[entry++].value:"";
        const char *old=values[c]?values[c]:"";
        if(!strcmp(next,old)) continue;
        char *copy=NULL;
        if(!config_copy(&copy,next,error)) { ok=false; break; }
        qa_q2_server_event event={.kind=QA_Q2_SVC_CONFIGSTRING,.data.config={(uint16_t)c,next}};
        ok=emit(context,&event,error);
        if(ok) { free(values[c]); values[c]=copy; } else free(copy);
    }
    return ok;
}
static bool config_transport(void *context, const qa_q2_server_event *event, qa_error *error)
{
    q2_host_peer *peer=context;
    return qa_network_q2_server_event(peer->host->options.runtime,peer->client,event,0,true,error);
}
static bool publish_configs(q2_host_peer *peer,qa_error *error)
{
    return configs_publish(peer->source,peer->configs,peer->config_count,config_transport,peer,error);
}

static bool publish_event_packet(q2_host_peer *peer,qa_error *error)
{
    if(!publish_configs(peer,error) ||
        !qa_network_q2_server_bytes(peer->host->options.runtime,peer->client,
            (qa_bytes){peer->event_packet.data,peer->event_packet.size},0,peer->event_reliable,error)) return false;
    qa_buffer_free(&peer->event_packet); peer->event_pending=false;
    if(peer->event_player) ++peer->player_event_cursor; else ++peer->event_cursor;
    peer->event_player=false;
    return true;
}
static bool publish_events(q2_host_peer *peer,const qa_net_client *client,size_t capacity,qa_error *error)
{
    qa_application *app=peer->host->options.frontend->application;
    if(peer->event_pending && !publish_event_packet(peer,error)) return false;
    uint64_t generation=qa_application_protocol_events_generation(app);
    size_t count=qa_application_protocol_event_count(app);
    if(peer->event_generation!=generation) {
        peer->event_generation=generation; peer->event_cursor=0; peer->player_event_cursor=0;
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
    size_t player_count=qa_application_q2_player_event_count(app);
    if(peer->player_event_cursor>player_count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 print cursor exceeds its retained Source generation");
    while(peer->player_event_cursor<player_count) {
        qa_application_q2_player_event event;
        if(!qa_application_q2_player_event_at(app,peer->player_event_cursor,&event))
            return frontend_fail(error,QA_ERROR_FORMAT,"Q2 Source print disappeared before publication");
        if(event.event.kind!=QA_Q2_PLAYER_PRINT) { ++peer->player_event_cursor; continue; }
        if(!frontend_network_q2_print_packet(&event,client,
            qa_network_epoch(peer->host->options.runtime,peer->client),codec,capacity,&peer->event_packet,error)) return false;
        if(!peer->event_packet.size) { ++peer->player_event_cursor; continue; }
        peer->event_reliable=true; peer->event_pending=true; peer->event_player=true;
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
        qa_error publication={0};
        bool frame_ready=false;
        if(!qa_application_network_q2_client_frame_ready(peer->source,peer->client,&frame_ready,&publication)) {
            if(publication.code==QA_ERROR_UNSUPPORTED) {
                if(!qa_network_q2_server_request_drop(host->options.runtime,peer->client,publication.message,error)) return false;
                continue;
            }
            if(error) *error=publication;
            return false;
        }
        if(!publish_events(peer,client,transport.channel.capacity,&publication)) {
            if(publication.code==QA_ERROR_UNSUPPORTED) {
                if(!qa_network_q2_server_request_drop(host->options.runtime,peer->client,publication.message,error)) return false;
                continue;
            }
            if(error) *error=publication;
            return false;
        }
        if(!frame_ready) {
            if(!publish_configs(peer,&publication)) {
                if(publication.code==QA_ERROR_UNSUPPORTED) {
                    if(!qa_network_q2_server_request_drop(host->options.runtime,peer->client,publication.message,error)) return false;
                } else { if(error) *error=publication; return false; }
            }
            continue;
        }
        qa_q2_wire_frame frame; const qa_q2_source_motion *motion;
        if(!qa_application_network_q2_client_frame(peer->source,peer->client,&frame,&publication) ||
            !qa_application_network_q2_motion(peer->source,&motion,&publication) || !publish_configs(peer,&publication) ||
            !qa_network_q2_server_frame(host->options.runtime,peer->client,&frame,motion,now,&publication)) {
            if(publication.code==QA_ERROR_UNSUPPORTED) {
                if(!qa_network_q2_server_request_drop(host->options.runtime,peer->client,publication.message,error)) return false;
                continue;
            }
            if(error) *error=publication;
            return false;
        }
        if(!current(host,error)) return false;
    }
    return demo_publish(host,error);
}
void frontend_network_q2_host_disconnected(frontend_network_q2_host *host,qa_net_client_id id)
{
    if(!host) return;
    for(size_t i=0;i<host->capacity;++i) if(host->peers[i].committed && qa_net_client_id_equal(host->peers[i].client,id))
        host->peers[i].retiring=true;
}
bool frontend_network_q2_host_idle(const frontend_network_q2_host *host)
{ return !host || (!host->calls && !host->traveling); }
bool frontend_network_q2_host_import_retirement_idle(const frontend_network_q2_host *host)
{
    return host && host->importing && !host->calls &&
        host->options.current(host->options.context,host) && qa_network_callbacks_idle(host->options.runtime);
}
bool frontend_network_q2_host_capture_current(const frontend_network_q2_host *host,qa_error *error)
{
    if(!host) return true;
    if(host->calls || !host->options.current(host->options.context,host) ||
        !qa_network_callbacks_idle(host->options.runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 capture retains entered Source or transport work");
    if(!host->traveling) return current((frontend_network_q2_host *)host,error);
    qa_application_network_q2_host actual;
    if(!qa_application_network_q2_host_source(host->options.frontend->application,host->options.protocol,&actual,error) ||
        !source_identity_equal(&actual,&host->travel_target) || host->travel_cursor>host->capacity ||
        host->travel_local_cursor>host->local_count || !host->travel_target.client_slots ||
        host->travel_target.client_slots>host->capacity)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 capture lost its actual retained travel target");
    return true;
}
bool frontend_network_q2_host_local_only(const frontend_network_q2_host *host)
{ return host && host->options.local_only; }
void frontend_network_q2_host_admin_rebind(frontend_network_q2_host *host,qa_server_admin *admin)
{ if(host) host->options.admin=admin; }
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
        !visitor->pool || !visitor->view || !frontend_network_q2_host_capture_current(host,error) ||
        !publisher_content_visit(host->discovery,visitor,error) ||
        !publisher_content_visit(host->travel_discovery,visitor,error)) return false;
    for(size_t i=0;i<host->capacity;++i) {
        const q2_host_peer *peer=&host->peers[i];
        if(!publisher_content_visit(peer->source,visitor,error) ||
            !publisher_content_visit(peer->travel_source,visitor,error)) return false;
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
        *out=(qa_network_local_hooks){.context=local,.player=local_player,
            .retained_player=frontend_network_q2_host_local_retained}; return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Restored local connection has no actual human Source owner");
}
bool frontend_network_q2_host_stop(frontend_network_q2_host *host,uint64_t now,
    bool *complete,qa_error *error)
{
    if(!complete || (host && (host->calls || !current(host,error) ||
        !qa_network_callbacks_idle(host->options.runtime))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 final message requires its returned server peer owners");
    *complete=true;
    if(!host) return true;
    bool remote=false;
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i];
        if(!peer->committed || !qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client)) continue;
        if(!qa_network_q2_server_request_drop(host->options.runtime,peer->client,"Server was killed.",error)) return false;
        remote=true;
    }
    /* The retiring channels consume their actual acknowledgements without
     * dispatching new gameplay input. Their Source publisher stays retained
     * until both the final delivery and its drop callback have returned. */
    if(remote) {
        qa_frontend *f=host->options.frontend;
        if(!frontend_network_intake(f,f->platform_events,now,error)) return false;
        if(!frontend_platform_drain(f,error) || !qa_network_tick(host->options.runtime,now,error)) return false;
    }
    for(size_t i=0;i<host->capacity;++i) {
        q2_host_peer *peer=&host->peers[i];
        if(!peer->committed || !qa_net_connections_get(qa_network_connections(host->options.runtime),peer->client)) continue;
        qa_error issue={0};
        if(!qa_network_q2_server_drop(host->options.runtime,peer->client,"Server was killed.",now,&issue)) {
            if(issue.code!=QA_OK) { if(error) *error=issue; return false; }
            *complete=false;
            continue;
        }
        if(!qa_network_detach(host->options.runtime,peer->client,"Server was killed.\n",error)) return false;
    }
    return true;
}
bool frontend_network_q2_host_destroy(frontend_network_q2_host **owned,qa_error *error)
{
    frontend_network_q2_host *host=owned?*owned:NULL;
    if(!host) return true;
    if(host->calls || host->demo_held || !qa_network_callbacks_idle(host->options.runtime)) return false;
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
    qa_application_network_q2_destroy(host->import_discovery);
    qa_q2_unicast_cache_destroy(host->unicast);
    free(host->peers); free(host->locals); free(host); *owned=NULL; return true;
}

static const qa_net_client *demo_client(frontend_network_q2_host *host, qa_error *error)
{
    for(size_t i=0;i<host->local_count;++i) {
        q2_local_peer *local=&host->locals[i];
        qa_network_local_player player;
        if(!qa_actor_id_equal(local->player.actor,host->demo_actor)) continue;
        if(!local_player(local,local->binding.seat,&player,error)) return NULL;
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(host->options.runtime),local->client);
        if(client && client->phase==QA_NET_ACTIVE) return client;
    }
    frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 demo lost its genuine local player connection");
    return NULL;
}
static bool demo_host_current(const void *context)
{
    frontend_network_q2_host *host=(frontend_network_q2_host *)context; qa_error error={0};
    return host && host->demo_held && !host->calls && !host->importing && current(host,&error) && demo_client(host,&error);
}
static bool demo_host_packet(frontend_network_q2_host *host, qa_bytes bytes, qa_error *error)
{
    frontend_demo_packet packet={.format=FRONTEND_DEMO_Q2,.value.message=bytes};
    (void)error;
    qa_error write_error={0};
    if(bytes.size) (void)host->demo_sink.append(host->demo_sink.owner,&packet,&write_error);
    return true;
}
static bool demo_host_seed(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!demo_host_current(host) || !sink || !sink->append) return false;
    qa_q2_game_state state; qa_q2_wire_frame frame;
    if(!qa_application_network_q2_game_state(host->discovery,&host->demo_actor,1,&state,error) ||
        !qa_application_network_q2_frame(host->discovery,&host->demo_actor,1,&frame,error) ||
        !qa_q2_codec_init(&host->demo_codec,host->options.protocol,error)) return false;
    qa_q2_config_layout layout;
    if(!qa_q2_config_layout_read(&host->demo_codec,&layout,error)) return false;
    size_t config_count=layout.max_configs;
    char **configs=calloc(config_count,sizeof(*configs));
    if(!configs) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Q2 demo config beforeimages");
    bool ok=true;
    for(size_t i=0;ok && i<state.config_count;++i)
        ok=state.configs[i].index<layout.max_configs && config_copy(&configs[state.configs[i].index],state.configs[i].value,error);
    if(ok) ok=frontend_remote_q2_demo_seed(&host->demo_codec,&state,&frame,sink,error);
    if(!ok) { configs_dispose(&configs,&config_count); return false; }
    configs_dispose(&host->demo_configs,&host->demo_config_count);
    host->demo_configs=configs; host->demo_config_count=config_count;
    host->demo_map_revision=host->source.source.map_revision;
    host->demo_frame=(uint32_t)frame.server_frame; host->demo_frame_set=true;
    host->demo_event_generation=qa_application_protocol_events_generation(host->options.frontend->application);
    host->demo_event_cursor=qa_application_protocol_event_count(host->options.frontend->application);
    host->demo_player_event_cursor=qa_application_q2_player_event_count(host->options.frontend->application);
    return true;
}
static bool demo_host_attach(void *context,const frontend_demo_sink *sink,bool *attached,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!attached || !sink || !sink->append || !demo_host_current(host) || host->demo_sink.append || !host->demo_configs)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 HOST demo sink lacks its actual seeded source");
    host->demo_sink=*sink; *attached=true;
    return true;
}
static bool demo_host_detach(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    frontend_network_q2_host *host=context;
    if(!host || !sink || host->calls || host->demo_sink.owner!=sink->owner || host->demo_sink.append!=sink->append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 HOST demo detach differs from its actual sink");
    host->demo_sink=(frontend_demo_sink){0};
    return true;
}
static bool demo_host_release(void **context,qa_error *error)
{
    frontend_network_q2_host *host=context?*context:NULL;
    if(!host) return true;
    if(host->calls || !host->demo_held || host->demo_sink.append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 HOST demo still owns entered or attached work");
    configs_dispose(&host->demo_configs,&host->demo_config_count);
    host->demo_held=false; host->demo_frame_set=false; host->demo_actor=(qa_actor_id){0};
    *context=NULL;
    return true;
}
bool frontend_network_q2_host_demo_record(frontend_network_q2_host *host,qa_actor_id actor,
    qa_fs_root *root,frontend_demo_record_source *out,qa_error *error)
{
    qa_network_q2_player player;
    if(!out || !root || !host || host->calls || host->demo_held || !current(host,error) ||
        !qa_application_network_q2_player(host->discovery,actor,&player,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 native recording requires its real local player and writable content");
    host->demo_actor=actor;
    if(!demo_client(host,error)) { host->demo_actor=(qa_actor_id){0}; return false; }
    host->demo_held=true;
    *out=(frontend_demo_record_source){.owner=host,.format=FRONTEND_DEMO_Q2,.protocol=host->options.protocol,
        .root=root,.current=demo_host_current,.seed=demo_host_seed,.attach=demo_host_attach,
        .detach=demo_host_detach,.release=demo_host_release};
    return true;
}
static bool demo_host_config(void *context,const qa_q2_server_event *event,qa_error *error)
{
    frontend_network_q2_host *host=context;
    uint8_t bytes[65535]; qa_net_writer writer;
    qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
    return qa_q2_server_event_write(&host->demo_codec,&writer,event) &&
        demo_host_packet(host,(qa_bytes){bytes,qa_net_writer_size(&writer)},error);
}
static bool demo_publish(frontend_network_q2_host *host,qa_error *error)
{
    if(!host->demo_sink.append) return true;
    if(!demo_host_current(host)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 native demo left its real Source publication");
    if(host->demo_map_revision!=host->source.source.map_revision)
        return demo_host_seed(host,&host->demo_sink,error);
    qa_application_network_q2_host actual;
    if(!qa_application_network_q2_host_source(host->options.frontend->application,host->options.protocol,&actual,error)) return false;
    uint64_t source_frame=actual.source.clock.frame_number;
    if(host->demo_frame_set && host->demo_frame==source_frame) return true;
    const qa_net_client *client=demo_client(host,error);
    if(!client || !configs_publish(host->discovery,host->demo_configs,host->demo_config_count,demo_host_config,host,error)) return false;
    qa_application *app=host->options.frontend->application;
    uint64_t generation=qa_application_protocol_events_generation(app);
    if(host->demo_event_generation!=generation) {
        host->demo_event_generation=generation; host->demo_event_cursor=0; host->demo_player_event_cursor=0;
    }
    size_t count=qa_application_protocol_event_count(app);
    while(host->demo_event_cursor<count) {
        qa_application_protocol_event event; qa_application_q2_protocol_delivery delivery; qa_buffer bytes={0};
        if(!qa_application_protocol_event_at(app,host->demo_event_cursor,&event) ||
            !qa_application_protocol_q2_delivery_at(app,host->demo_event_cursor,&delivery)) return false;
        bool ok=event.signon || !delivery.original ||
            (frontend_network_q2_event_packet(host->discovery,host->source.source.source_owner,&event,&delivery,
                client,qa_network_epoch(host->options.runtime,client->id),&host->demo_codec,65535,&bytes,error) &&
                demo_host_packet(host,(qa_bytes){bytes.data,bytes.size},error));
        qa_buffer_free(&bytes); if(!ok) return false;
        ++host->demo_event_cursor;
    }
    count=qa_application_q2_player_event_count(app);
    while(host->demo_player_event_cursor<count) {
        qa_application_q2_player_event event; qa_buffer bytes={0};
        if(!qa_application_q2_player_event_at(app,host->demo_player_event_cursor,&event)) return false;
        bool ok=event.event.kind!=QA_Q2_PLAYER_PRINT ||
            (frontend_network_q2_print_packet(&event,client,qa_network_epoch(host->options.runtime,client->id),
                &host->demo_codec,65535,&bytes,error) && demo_host_packet(host,(qa_bytes){bytes.data,bytes.size},error));
        qa_buffer_free(&bytes); if(!ok) return false;
        ++host->demo_player_event_cursor;
    }
    qa_q2_wire_frame frame;
    if(!qa_application_network_q2_frame(host->discovery,&host->demo_actor,1,&frame,error)) return false;
    uint8_t *bytes=malloc(65535);
    if(!bytes) return frontend_fail(error,QA_ERROR_MEMORY,"Encoding Q2 native demo frame");
    qa_net_writer writer; qa_net_writer_init(&writer,bytes,65535,error);
    bool ok=qa_q2_frame_write(&host->demo_codec,&writer,&frame,NULL,(qa_q2_entity_span){0},host->source.client_slots) &&
        demo_host_packet(host,(qa_bytes){bytes,qa_net_writer_size(&writer)},error);
    free(bytes);
    if(ok) { host->demo_frame=source_frame; host->demo_frame_set=true; }
    return ok && generation==qa_application_protocol_events_generation(app);
}
