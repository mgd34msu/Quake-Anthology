#include "network_q2_host_save.h"
#include "network_q2_host_private.h"
#include "save_private.h"
#include "qa/application_network.h"
#include "qa/network_q2_bootstrap_save.h"
#include "qa/network_q2_wire_save.h"

static bool bad(qa_error *e,const char *message)
{ return frontend_fail(e,QA_ERROR_FORMAT,message); }
static bool protocol_equal(qa_net_protocol_id a,qa_net_protocol_id b)
{ return a.kind==b.kind && a.revision==b.revision && a.flags==b.flags; }
static bool blob(qa_source_save_io *io,qa_buffer *b)
{
    if(!qa_source_save_count(io,&b->size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && b->size) {
        if(b->size>io->input.size-io->offset) return bad(io->error,"Q2 HOST child exceeds its document");
        b->data=malloc(b->size);
        if(!b->data) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining Q2 HOST child custody");
    }
    return (!b->size || b->data) && qa_source_save_bytes(io,b->data,b->size);
}
static bool address(qa_source_save_io *io,qa_net_address *a)
{
    uint32_t kind=(uint32_t)a->kind;
    if(!qa_source_save_u32(io,&kind) || !qa_source_save_u16(io,&a->port) || kind>QA_NET_IPX) return false;
    a->kind=(qa_net_address_kind)kind;
    bool ok=false;
    switch(a->kind) {
    case QA_NET_IPV4: ok=qa_source_save_bytes(io,a->host.ipv4,4); break;
    case QA_NET_IPV6: ok=qa_source_save_bytes(io,a->host.ipv6.bytes,16) && qa_source_save_u32(io,&a->host.ipv6.scope); break;
    case QA_NET_IPX: ok=qa_source_save_u32(io,&a->host.ipx.network) && qa_source_save_bytes(io,a->host.ipx.node,6); break;
    case QA_NET_LOOPBACK:
        ok=qa_source_save_bytes(io,a->host.loopback,sizeof(a->host.loopback)) &&
            memchr(a->host.loopback,0,sizeof(a->host.loopback))!=NULL; break;
    }
    char text[256];
    return ok && qa_net_address_format(a,text,sizeof(text),io->error);
}
static bool clock_config(qa_source_save_io *io,qa_clock_config *v)
{
    uint32_t kind=(uint32_t)v->kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_CLOCK_Q3 ||
        !qa_source_save_u64(io,&v->initial_time_ns) || !qa_source_save_u64(io,&v->interval_ns) ||
        !qa_source_save_u64(io,&v->minimum_frame_ns) || !qa_source_save_u64(io,&v->maximum_frame_ns) ||
        !qa_source_save_u64(io,&v->initial_lead_ns) || !qa_source_save_u32(io,&v->maximum_steps)) return false;
    v->kind=(qa_clock_kind)kind; return v->interval_ns!=0;
}
static bool clock_state(qa_source_save_io *io,qa_clock_state *v)
{
    uint32_t kind=(uint32_t)v->frame.kind,phase=(uint32_t)v->frame.phase;
    if(!qa_source_save_u64(io,&v->host_origin_ns) || !qa_source_save_u64(io,&v->elapsed_ns) ||
        !qa_source_save_u64(io,&v->debt_ns) || !qa_source_save_u64(io,&v->frame_number) ||
        !qa_source_save_string(io,&v->frame.provider) || !qa_source_save_u32(io,&kind) || kind>QA_CLOCK_Q3 ||
        !qa_source_save_u32(io,&phase) || phase>QA_FRAME_EXIT || !qa_source_save_u64(io,&v->frame.number) ||
        !qa_source_save_u64(io,&v->frame.start_ns) || !qa_source_save_u64(io,&v->frame.elapsed_ns) ||
        !qa_source_save_u64(io,&v->frame.time_ns) || !qa_source_save_bool(io,&v->paused)) return false;
    v->frame.kind=(qa_clock_kind)kind; v->frame.phase=(qa_frame_phase)phase; return true;
}
static bool same_config(const qa_clock_config *a,const qa_clock_config *b)
{
    return a->kind==b->kind && a->initial_time_ns==b->initial_time_ns && a->interval_ns==b->interval_ns &&
        a->minimum_frame_ns==b->minimum_frame_ns && a->maximum_frame_ns==b->maximum_frame_ns &&
        a->initial_lead_ns==b->initial_lead_ns && a->maximum_steps==b->maximum_steps;
}
static bool same_clock(const qa_clock_state *a,const qa_clock_state *b)
{
    return a->host_origin_ns==b->host_origin_ns && a->elapsed_ns==b->elapsed_ns && a->debt_ns==b->debt_ns &&
        a->frame_number==b->frame_number && a->paused==b->paused && a->frame.provider==b->frame.provider &&
        a->frame.kind==b->frame.kind && a->frame.phase==b->frame.phase && a->frame.number==b->frame.number &&
        a->frame.start_ns==b->frame.start_ns && a->frame.elapsed_ns==b->frame.elapsed_ns && a->frame.time_ns==b->frame.time_ns;
}
static bool receipt_equal(const qa_application_network_q2_host *a,const qa_application_network_q2_host *b)
{
    return protocol_equal(a->protocol,b->protocol) && a->client_slots==b->client_slots && a->entity_slots==b->entity_slots &&
        a->source.source_owner==b->source.source_owner && a->source.kind==b->source.kind && a->source.edition==b->source.edition &&
        a->source.server_time_ns==b->source.server_time_ns && same_config(&a->source.clock_config,&b->source.clock_config) &&
        same_clock(&a->source.clock,&b->source.clock);
}
static qa_application_network_q2_host metadata_receipt(const qa_application_network_q2_metadata *m)
{
    return (qa_application_network_q2_host){.protocol=m->protocol,.client_slots=m->client_slots,.entity_slots=m->entity_slots,
        .source={.source_owner=m->source_owner,.kind=m->kind,.edition=m->edition,.clock_config=m->clock_config,.clock=m->clock,
            .server_time_ns=m->server_time_ns,.publication_generation=m->publication_generation,.map_revision=m->map_revision}};
}
static bool source_recipe(qa_source_save_io *io,qa_application_network_q2_host *v)
{
    uint32_t kind=(uint32_t)v->source.kind,edition=(uint32_t)v->source.edition;
    if(!qa_q2_save_protocol(io,&v->protocol) || !qa_source_save_string(io,&v->source.source_owner) || !v->source.source_owner ||
        !qa_source_save_u32(io,&kind) || kind>QA_APPLICATION_NATIVE_Q2_ORIGINAL ||
        !qa_source_save_u32(io,&edition) || edition>QA_Q2_RERELEASE ||
        !qa_source_save_u32(io,&v->client_slots) || !v->client_slots || v->client_slots>256 ||
        !qa_source_save_u32(io,&v->entity_slots) || v->entity_slots<=v->client_slots || v->entity_slots>65536 ||
        !clock_config(io,&v->source.clock_config) || !clock_state(io,&v->source.clock) ||
        !qa_source_save_u64(io,&v->source.server_time_ns) || !qa_source_save_u64(io,&v->source.publication_generation) ||
        !qa_source_save_u64(io,&v->source.map_revision)) return false;
    v->source.kind=(qa_application_native_q2_source_kind)kind; v->source.edition=(qa_q2_edition)edition;
    return v->source.clock.frame.provider==v->source.source_owner;
}
static bool current_cut(const qa_application_network_q2_metadata *m,const qa_application_network_q2_host *actual)
{
    return !m->archival && m->source_owner==actual->source.source_owner && m->kind==actual->source.kind &&
        m->edition==actual->source.edition && m->map_revision==actual->source.map_revision &&
        m->publication_generation==actual->source.publication_generation;
}
static bool publisher(qa_source_save_io *io,frontend_network_q2_host *host,
    qa_application_network_q2 **p,const qa_application_network_q2_host *actual)
{
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE,present=*p!=NULL,archive=false,bound=false,capability=false;
    qa_net_protocol_id protocol={0}; int32_t count=0;
    qa_buffer child={0}; qa_application_network_q2_metadata m={0};
    if(writing && present) {
        if(!qa_application_network_q2_metadata_read(*p,&m,io->error)) return false;
        archive=!current_cut(&m,actual); protocol=m.protocol; count=m.server_count;
        bound=m.materials_bound; capability=m.materials_capability;
    }
    bool ok=qa_source_save_bool(io,&present);
    if(!ok || !present) return ok;
    ok=qa_source_save_bool(io,&archive) && qa_q2_save_protocol(io,&protocol) &&
        qa_source_save_i32(io,&count) && count>0 && qa_source_save_bool(io,&bound) &&
        qa_source_save_bool(io,&capability) && (bound || !capability);
    if(ok && writing) ok=archive ? qa_application_network_q2_capture_retained(*p,&child,io->error) :
        qa_application_network_q2_capture(*p,&child,io->error);
    if(ok) ok=blob(io,&child) && child.size!=0;
    if(ok && !writing) {
        if(archive) ok=qa_application_network_q2_restore_retained(host->options.frontend->application,
            (qa_bytes){child.data,child.size},p,io->error);
        else {
            ok=qa_application_network_q2_create(host->options.frontend->application,protocol,count,p,io->error);
            if(ok && bound) ok=qa_application_network_q2_material_capability(*p,capability,io->error);
            if(ok) ok=qa_application_network_q2_restore(*p,(qa_bytes){child.data,child.size},io->error);
        }
        if(ok) ok=qa_application_network_q2_metadata_read(*p,&m,io->error) && m.archival==archive &&
            protocol_equal(m.protocol,protocol) && m.server_count==count && m.materials_bound==bound &&
            m.materials_capability==capability;
    }
    qa_buffer_free(&child); return ok;
}
static bool client_id(qa_source_save_io *io,qa_net_client_id *v,uint64_t owner,bool present)
{
    if(!qa_source_save_u32(io,&v->slot) || !qa_source_save_u64(io,&v->generation)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) v->owner=present?owner:0;
    return present ? v->owner && v->generation : !v->owner && !v->generation && !v->slot;
}
static bool binding(qa_source_save_io *io,qa_net_seat_binding *v)
{
    return qa_source_save_u64(io,&v->seat.owner) && qa_source_save_u32(io,&v->seat.index) &&
        qa_source_save_u32(io,&v->remote_index) && v->seat.owner==QA_NETWORK_COMMAND_OWNER;
}
static bool policy(qa_source_save_io *io,qa_network_q2_server_policy *v)
{
    return qa_q2_save_channel_options(io,&v->channel) && v->channel.server &&
        qa_source_save_u32(io,&v->max_clients) && v->max_clients && v->max_clients<=256 &&
        qa_source_save_count(io,&v->history_capacity,SIZE_MAX/sizeof(qa_q2_wire_frame)) && v->history_capacity &&
        qa_source_save_i32(io,&v->server_count) && v->server_count>0 &&
        qa_source_save_u64(io,&v->source_interval_ns) && v->source_interval_ns && v->source_interval_ns<=UINT64_C(1000000000);
}
static bool roster_actor(frontend_network_q2_host *host,const q2_host_peer *peer,size_t seat,
    qa_actor_id *out,qa_error *error)
{
    size_t cursor=0,found=0; qa_application_network_player row;
    qa_application_network_q2_metadata metadata; qa_application_network_q2_host actual;
    if(!qa_application_network_q2_metadata_read(peer->source,&metadata,error) ||
        !qa_application_network_q2_host_source(host->options.frontend->application,peer->admission.connection.protocol,&actual,error))
        return false;
    bool live=current_cut(&metadata,&actual);
    while(qa_application_network_player_next(host->options.frontend->application,&cursor,&row))
        if(qa_net_client_id_equal(row.client,peer->client) && row.seat.owner==peer->bindings[seat].seat.owner &&
            row.seat.index==peer->bindings[seat].seat.index) {
            if(live && row.source_slot!=peer->slots[seat]) return bad(error,"Q2 HOST Source slot differs from its canonical roster");
            *out=row.actor; ++found;
        }
    return found==1 || bad(error,"Q2 HOST claim lacks one actual full canonical Source actor");
}
static bool configs_fields(qa_source_save_io *io,char ***configs,size_t *count,
    qa_net_protocol_id protocol)
{
    qa_q2_codec codec; qa_q2_config_layout layout;
    if(!qa_q2_codec_init(&codec,protocol,io->error) || !qa_q2_config_layout_read(&codec,&layout,io->error) ||
        !qa_source_save_count(io,count,UINT16_MAX) ||
        (*count && *count!=layout.max_configs && !(protocol.kind==QA_NET_Q2PRO_36 && *count==13630))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && *count) {
        *configs=calloc(*count,sizeof(**configs));
        if(!*configs) return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring genuine Q2 config beforeimages");
    }
    if((*configs!=NULL)!=(*count!=0)) return false;
    for(size_t i=0;i<*count;++i) if(!qa_source_save_owned_text(io,&(*configs)[i])) return false;
    return true;
}
static bool peer_fields(qa_source_save_io *io,frontend_network_q2_host *host,q2_host_peer *p,
    const qa_application_network_q2_host *actual)
{
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    if(!qa_source_save_bool(io,&p->reserved)) return false;
    if(!p->reserved) return !p->committed && !p->source && !p->travel_source && !p->event_pending && !p->configs &&
        !p->signon_configs && !p->signon_config_count;
    if(!qa_source_save_bool(io,&p->committed) || !qa_source_save_bool(io,&p->retiring) ||
        !qa_source_save_bool(io,&p->material_scripts) || !client_id(io,&p->client,host->import_network_owner,p->committed)) return false;
    uint64_t epoch=writing && p->committed?qa_network_epoch(host->options.runtime,p->client):p->import_epoch;
    if(!qa_source_save_u64(io,&epoch) || (p->committed && !epoch) || (!p->committed && epoch)) return false;
    if(!writing) p->import_epoch=epoch;
    qa_net_connect *request=&p->admission.connection;
    uint32_t attachment=(uint32_t)request->attachment;
    if(!qa_source_save_u32(io,&attachment) || attachment!=QA_NET_REMOTE || !address(io,&request->endpoint) ||
        !qa_q2_save_protocol(io,&request->protocol) || !qa_source_save_count(io,&request->seat_count,QA_Q2_MAX_SEATS) ||
        !request->seat_count || request->seat_count>qa_network_protocol_seat_capacity(request->protocol) ||
        !qa_source_save_bytes(io,request->composition.bytes,sizeof(request->composition.bytes)) || !policy(io,&p->admission.policy) ||
        !protocol_equal(request->protocol,p->admission.policy.channel.protocol) ||
        !qa_source_save_bytes(io,p->userinfo,sizeof(p->userinfo)) || !memchr(p->userinfo,0,sizeof(p->userinfo)) ||
        !qa_source_save_bytes(io,p->reason,sizeof(p->reason)) || !memchr(p->reason,0,sizeof(p->reason))) return false;
    request->attachment=(qa_net_attachment)attachment;
    for(size_t s=0;s<request->seat_count;++s) {
        qa_actor_id actor=writing?(qa_actor_id){0}:p->import_actors[s];
        if(!binding(io,&p->bindings[s]) || p->bindings[s].remote_index!=s || !qa_source_save_u32(io,&p->slots[s]) ||
            !p->slots[s] || p->slots[s]>p->admission.policy.max_clients) return false;
        if(writing && p->committed && !roster_actor(host,p,s,&actor,io->error)) return false;
        if(!qa_source_save_actor(io,&actor) || ((actor.registry!=0)!=p->committed)) return false;
        if(!writing) p->import_actors[s]=actor;
        for(size_t t=0;t<s;++t) if(p->slots[s]==p->slots[t] ||
            (p->bindings[s].seat.owner==p->bindings[t].seat.owner && p->bindings[s].seat.index==p->bindings[t].seat.index)) return false;
    }
    request->seats=p->bindings; p->admission.source_claim=p;
    if(!publisher(io,host,&p->source,actual) || !p->source ||
        !publisher(io,host,&p->travel_source,actual) || !qa_source_save_bool(io,&p->travel_installed)) return false;
    qa_application_network_q2_metadata m;
    if(!qa_application_network_q2_metadata_read(p->source,&m,io->error) || !protocol_equal(m.protocol,request->protocol) ||
        !m.materials_bound || m.materials_capability!=p->material_scripts ||
        (m.archival && !host->traveling && !p->retiring) ||
        (p->travel_installed && (!host->traveling || m.archival || p->travel_source))) return false;
    if(p->travel_source && (!host->traveling || !qa_application_network_q2_metadata_read(p->travel_source,&m,io->error) || m.archival ||
        !protocol_equal(m.protocol,request->protocol) || !m.materials_bound || m.materials_capability!=p->material_scripts)) return false;
    if(!configs_fields(io,&p->configs,&p->config_count,request->protocol) ||
        !configs_fields(io,&p->signon_configs,&p->signon_config_count,request->protocol)) return false;
    if(!qa_source_save_u64(io,&p->event_generation) || !qa_source_save_count(io,&p->event_cursor,SIZE_MAX) ||
        !qa_source_save_count(io,&p->player_event_cursor,SIZE_MAX) ||
        !qa_source_save_bool(io,&p->event_pending) || !qa_source_save_bool(io,&p->event_reliable) ||
        !qa_source_save_bool(io,&p->event_player) || !blob(io,&p->event_packet)) return false;
    return p->event_pending==(p->event_packet.size!=0) && (!p->event_pending || p->committed) &&
        (!p->event_player || (p->event_pending && p->event_reliable));
}
static bool local_fields(qa_source_save_io *io,frontend_network_q2_host *host,q2_local_peer *p,size_t index)
{
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE,present=p->client.owner!=0;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) return !p->admitting && !p->travel_restarted && !p->player.actor.registry;
    if(!client_id(io,&p->client,host->import_network_owner,true) || !binding(io,&p->binding) || p->binding.remote_index ||
        !qa_source_save_u32(io,&p->physical) || p->physical!=index || !qa_source_save_u32(io,&p->authored) ||
        !qa_source_save_bytes(io,p->composition.bytes,sizeof(p->composition.bytes)) ||
        !qa_source_save_actor(io,&p->player.actor) || !p->player.actor.registry ||
        !qa_source_save_string(io,&p->player.source_owner) || !p->player.source_owner ||
        !qa_source_save_u32(io,&p->player.source_slot) || !p->player.source_slot || p->player.source_slot>256 ||
        !qa_source_save_bool(io,&p->admitting) || p->admitting || !qa_source_save_bool(io,&p->travel_restarted) ||
        (p->travel_restarted && !host->traveling)) return false;
    qa_application_network_q2_host actual;
    if(!qa_application_network_q2_host_source(host->options.frontend->application,host->options.protocol,&actual,io->error)) return false;
    bool historical=writing?(p->import_historical || p->map_revision!=actual.source.map_revision):false;
    if(!qa_source_save_u64(io,&p->map_revision) || !p->map_revision || !qa_source_save_bool(io,&historical) ||
        (historical && (!host->traveling || index<host->travel_local_cursor || p->travel_restarted)) ||
        (!historical && p->map_revision!=(writing?actual.source.map_revision:host->import_map_revision))) return false;
    if(!writing) {
        p->import_historical=historical;
        if(!historical) p->map_revision=actual.source.map_revision;
    }
    if(p->binding.seat.index!=64u+p->physical) return false;
    if(writing) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(host->options.runtime),p->client);
        if(!client || !qa_sha256_equal(&p->composition,&client->composition)) return false;
    }
    uint64_t epoch=writing?qa_network_epoch(host->options.runtime,p->client):p->import_epoch;
    if(!qa_source_save_u64(io,&epoch) || !epoch) return false;
    if(!writing) p->import_epoch=epoch;
    uint32_t authored;
    return frontend_seat_launch_id_read(host->options.frontend,p->physical,&authored) && authored==p->authored;
}
static bool request_equal(const qa_net_connect *a,const qa_net_connect *b)
{
    if(!a || !b || a->attachment!=b->attachment || !protocol_equal(a->protocol,b->protocol) ||
        !qa_net_address_equal(&a->endpoint,&b->endpoint,true) || !qa_sha256_equal(&a->composition,&b->composition) ||
        !a->seats || !b->seats || !a->seat_count || a->seat_count!=b->seat_count) return false;
    for(size_t i=0;i<a->seat_count;++i) if(a->seats[i].seat.owner!=b->seats[i].seat.owner ||
        a->seats[i].seat.index!=b->seats[i].seat.index || a->seats[i].remote_index!=b->seats[i].remote_index) return false;
    return true;
}
static bool client_request(const qa_net_client *client,const qa_net_connect *saved,qa_net_client_id id)
{
    qa_net_connect actual={.attachment=client->attachment,.endpoint=client->endpoint,.protocol=client->protocol,
        .seats=client->seats,.seat_count=client->seat_count,.composition=client->composition};
    return qa_net_client_id_equal(client->id,id) && request_equal(&actual,saved);
}
static bool cache_client_encode(void *context,qa_net_client_id client,uint64_t *saved,qa_error *e)
{
    frontend_network_q2_host *h=context;
    if(!saved || !qa_net_connections_get(qa_network_connections(h->options.runtime),client)) return bad(e,"Q2 unicast has no live canonical client");
    *saved=(uint64_t)client.slot+1; return true;
}
static bool cache_client_decode(void *context,uint64_t saved,qa_net_client_id *out,qa_error *e)
{
    frontend_network_q2_host *h=context;
    if(!saved || saved-1>UINT32_MAX || !out) return bad(e,"Q2 unicast saved client index is invalid");
    uint32_t cursor=(uint32_t)(saved-1); const qa_net_client *client=NULL;
    if(!qa_net_connections_next(qa_network_connections(h->options.runtime),&cursor,&client) || client->id.slot!=saved-1)
        return bad(e,"Q2 unicast restored client is absent");
    *out=client->id; return true;
}
static bool cache_source_encode(void *context,qa_actor_owner source,uint64_t *saved,qa_error *e)
{
    frontend_network_q2_host *h=context; qa_application_network_q2_host actual;
    if(!saved || !qa_application_network_q2_host_source(h->options.frontend->application,h->options.protocol,&actual,e) ||
        source!=actual.source.source_owner) return bad(e,"Q2 unicast has no genuine current Source anchor");
    *saved=1; return true;
}
static bool cache_source_decode(void *context,uint64_t saved,qa_actor_owner *out,uint64_t *revision,qa_error *e)
{
    frontend_network_q2_host *h=context; qa_application_network_q2_host actual;
    if(saved!=1 || !out || !revision || *revision!=h->import_map_revision ||
        !qa_application_network_q2_host_source(h->options.frontend->application,h->options.protocol,&actual,e)) return false;
    *out=actual.source.source_owner; *revision=actual.source.map_revision; return true;
}
static bool cache_current(void *context,const qa_q2_unicast_claim *claim,bool *present,qa_error *e)
{
    frontend_network_q2_host *h=context; qa_application_network_q2_host actual; qa_application_map_view map;
    if(!present || !claim || !qa_application_network_q2_host_source(h->options.frontend->application,h->options.protocol,&actual,e) ||
        !qa_application_map_read(h->options.frontend->application,&map) || !map.resource) return false;
    const qa_net_client *client=qa_net_connections_get(qa_network_connections(h->options.runtime),claim->client);
    *present=client && claim->connection_epoch==qa_network_epoch(h->options.runtime,client->id) &&
        claim->source==actual.source.source_owner && qa_sha256_equal(&claim->map,qa_resource_digest(map.resource)) &&
        claim->map_revision==actual.source.map_revision &&
        claim->source_frame==actual.source.clock.frame_number && claim->source_time_ns==actual.source.clock.frame.time_ns;
    return true;
}
static qa_q2_unicast_refs cache_refs(frontend_network_q2_host *h)
{
    return (qa_q2_unicast_refs){.context=h,.client_encode=cache_client_encode,.client_decode=cache_client_decode,
        .source_encode=cache_source_encode,.source_decode=cache_source_decode,.current=cache_current};
}
static bool fields(qa_source_save_io *io,frontend_network_q2_host *h)
{
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    uint32_t tag=UINT32_C(0x48423251);
    qa_net_protocol_id protocol=h->options.protocol; bool local_only=h->options.local_only;
    qa_sha256_digest composition=h->options.composition;
    if(!qa_source_save_u32(io,&tag) || tag!=UINT32_C(0x48423251) || !qa_source_save_bool(io,&local_only) || local_only!=h->options.local_only || !qa_q2_save_protocol(io,&protocol) ||
        (!local_only && !protocol_equal(protocol,h->options.protocol)) ||
        !qa_source_save_bytes(io,composition.bytes,sizeof(composition.bytes)) ||
        (!local_only && !qa_sha256_equal(&composition,&h->options.composition)) ||
        !qa_source_save_i32(io,&h->server_count) || h->server_count<1 ||
        !qa_source_save_count(io,&h->capacity,256) || h->capacity!=256 ||
        !qa_source_save_count(io,&h->local_count,UINT16_MAX) || h->local_count!=h->options.frontend->options.seats ||
        !qa_source_save_bool(io,&h->traveling) || !qa_source_save_bool(io,&h->travel_discovery_installed) ||
        !qa_source_save_count(io,&h->travel_cursor,h->capacity) || !qa_source_save_count(io,&h->travel_local_cursor,h->local_count) ||
        (!h->traveling && h->travel_discovery_installed && h->travel_cursor!=h->capacity)) return false;
    if(!writing) {
        h->options.protocol=protocol; h->options.composition=composition;
        h->peers=calloc(h->capacity,sizeof(*h->peers)); h->locals=calloc(h->local_count?h->local_count:1,sizeof(*h->locals));
        if(!h->peers || !h->locals) return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring genuine Q2 HOST claims");
        for(size_t i=0;i<h->capacity;++i) h->peers[i].host=h;
        for(size_t i=0;i<h->local_count;++i) h->locals[i].host=h;
    }
    qa_application_network_q2_host actual;
    if(!qa_application_network_q2_host_source(h->options.frontend->application,h->options.protocol,&actual,io->error)) return false;
    if(local_only && (h->options.protocol.kind!=(actual.source.edition==QA_Q2_RERELEASE?QA_NET_Q2REPRO_1038:QA_NET_Q2_34) ||
        h->options.protocol.revision || h->options.protocol.flags)) return bad(io->error,"Offline Q2 groups differ from their actual Source protocol declaration");
    uint64_t map_revision=writing?actual.source.map_revision:0;
    if(!qa_source_save_u64(io,&map_revision) || !map_revision) return false;
    if(!writing) h->import_map_revision=map_revision;
    if(!publisher(io,h,&h->discovery,&actual) || !h->discovery ||
        !publisher(io,h,&h->travel_discovery,&actual)) return false;
    qa_application_network_q2_metadata m;
    if(!qa_application_network_q2_metadata_read(h->discovery,&m,io->error) || m.server_count!=h->server_count ||
        !protocol_equal(m.protocol,h->options.protocol) || (m.archival && (!h->traveling || h->travel_discovery_installed)) ||
        (h->travel_discovery && (!h->traveling || h->travel_discovery_installed))) return false;
    qa_application_network_q2_host saved=metadata_receipt(&m);
    if(!writing) h->source=m.archival?saved:actual;
    else if(h->source.source.source_owner!=saved.source.source_owner || h->source.source.map_revision!=saved.source.map_revision) return false;
    if(h->traveling) {
        qa_application_network_q2_host target=writing?actual:(qa_application_network_q2_host){0};
        if(!source_recipe(io,&target) || !receipt_equal(&target,&actual)) return bad(io->error,"Q2 HOST travel target differs from its actual restored Source");
        if(!writing) h->travel_target=actual;
    }
    for(size_t i=0;i<h->capacity;++i) if(!peer_fields(io,h,&h->peers[i],&actual)) return false;
    for(size_t i=0;i<h->local_count;++i) if(!local_fields(io,h,&h->locals[i],i)) return false;
    for(size_t i=0;i<h->capacity;++i) {
        const q2_host_peer *a=&h->peers[i];
        if(!a->reserved) continue;
        qa_application_network_q2_metadata am;
        if(!qa_application_network_q2_metadata_read(a->source,&am,io->error)) return false;
        for(size_t j=0;j<i;++j) {
            const q2_host_peer *b=&h->peers[j];
            if(!b->reserved) continue;
            qa_application_network_q2_metadata bm;
            if(!qa_application_network_q2_metadata_read(b->source,&bm,io->error)) return false;
            if(am.source_owner!=bm.source_owner || am.archival!=bm.archival || am.map_revision!=bm.map_revision ||
                !qa_sha256_equal(&am.map_identity,&bm.map_identity)) continue;
            for(size_t s=0;s<a->admission.connection.seat_count;++s)
                for(size_t t=0;t<b->admission.connection.seat_count;++t)
                    if(a->slots[s]==b->slots[t]) return bad(io->error,"Q2 HOST claims repeat one physical Source client slot");
        }
    }
    qa_buffer bootstrap={0},unicast={0};
    bool ok=true;
    if(writing) {
        if(h->options.local_only ? h->bootstrap!=NULL : h->bootstrap==NULL) ok=false;
        if(ok && h->bootstrap) ok=qa_network_q2_bootstrap_capture(h->bootstrap,&bootstrap,io->error);
        qa_q2_unicast_refs refs=cache_refs(h);
        if(ok) ok=qa_q2_unicast_capture(h->unicast,&refs,&unicast,io->error);
    }
    if(ok) ok=blob(io,&bootstrap) && (local_only ? !bootstrap.size : bootstrap.size!=0) && blob(io,&unicast) && unicast.size!=0;
    if(ok && !writing) { h->import_bootstrap=bootstrap; h->import_unicast=unicast; bootstrap=(qa_buffer){0}; unicast=(qa_buffer){0}; }
    qa_buffer_free(&bootstrap); qa_buffer_free(&unicast); return ok;
}
bool frontend_network_q2_host_checkpoint(frontend_network_q2_host *h,qa_buffer *out,qa_error *e)
{
    if(!h || !out || out->data || out->size ||
        (h->importing && (!h->options.frontend->capture || !h->import_ready ||
            !frontend_network_q2_host_qualified(h,h->options.runtime,true,e))) ||
        !frontend_network_q2_host_capture_current(h,e)) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,qa_application_session(h->options.frontend->application),e) && fields(&io,h);
    if(ok) ok=qa_source_save_finish(&io,out);
    if(!ok && (!e || e->code==QA_OK)) bad(e,"Q2 HOST custody differs from its real returned continuation");
    qa_source_save_dispose(&io); return ok;
}
bool frontend_network_q2_host_restore_prepare(const frontend_network_q2_host_options *options,
    uint64_t owner,qa_bytes bytes,frontend_network_q2_host **out,qa_error *e)
{
    if(!options || !options->frontend || !options->frontend->application || !options->current || !options->random ||
        !options->admin || !owner || !out || *out || !bytes.data) return bad(e,"Q2 HOST import lacks its actual candidate owners");
    frontend_network_q2_host *h=calloc(1,sizeof(*h));
    if(!h) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining imported Q2 HOST custody");
    *out=h; h->options=*options; h->importing=true; h->import_network_owner=owner;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(options->frontend->application),bytes,e) && fields(&io,h) &&
        qa_source_save_finish(&io,NULL);
    if(!ok && (!e || e->code==QA_OK)) bad(e,"Saved Q2 HOST child has invalid genuine claims");
    qa_source_save_dispose(&io); return ok;
}
bool frontend_network_q2_host_restore_admit(frontend_network_q2_host *h,const qa_net_connect *request,
    bool *recognized,qa_error *e)
{
    if(!recognized) return false;
    *recognized=h && request && request->protocol.kind==h->options.protocol.kind;
    if(!*recognized) return true;
    if(!h->importing || !request->seats) return bad(e,"Q2 cold admission has no retained imported HOST claim");
    size_t found=0;
    for(size_t i=0;i<h->capacity;++i) if(h->peers[i].reserved && h->peers[i].committed &&
        request_equal(request,&h->peers[i].admission.connection)) ++found;
    for(size_t i=0;i<h->local_count;++i) if(h->locals[i].client.owner) {
        qa_net_connect local;
        if(!frontend_network_q2_host_local_request(h,&h->locals[i],&local,e)) return false;
        if(request_equal(request,&local)) ++found;
    }
    return found==1 || bad(e,"Imported Q2 connection differs from its one genuine retained Source claim");
}
static bool runtime_bind(frontend_network_q2_host *h,qa_network_runtime *runtime,qa_error *e)
{
    if(!h || !h->importing || !runtime || h->calls || !h->options.current(h->options.context,h))
        return bad(e,"Q2 import hooks lack their retained candidate runtime and parent");
    if(h->import_runtime && h->import_runtime!=runtime)
        return bad(e,"Q2 import attempted to replace its actual candidate runtime");
    h->options.runtime=runtime; h->import_runtime=runtime;
    qa_application_network_q2_metadata m;
    if(!qa_application_network_q2_metadata_read(h->discovery,&m,e)) return false;
    qa_application_network_q2 *live=h->discovery;
    if(m.archival) {
        live=h->travel_discovery;
        if(!live && !h->import_discovery && !qa_application_network_q2_create(h->options.frontend->application,
            h->options.protocol,h->server_count+1,&h->import_discovery,e)) return false;
        if(!live) live=h->import_discovery;
    }
    qa_network_q2_server_hooks hooks;
    return frontend_network_q2_host_bind_publisher(h,live,runtime,&hooks,e);
}
bool frontend_network_q2_host_restore_hooks(frontend_network_q2_host *h,qa_network_runtime *runtime,
    const qa_net_client *client,qa_network_q2_server_policy *p,qa_network_q2_server_hooks *hooks,qa_error *e)
{
    if(!client || !p || !hooks || !runtime_bind(h,runtime,e)) return false;
    for(size_t i=0;i<h->capacity;++i) {
        q2_host_peer *peer=&h->peers[i];
        if(!peer->reserved || !peer->committed || !client_request(client,&peer->admission.connection,peer->client)) continue;
        if(qa_network_epoch(runtime,client->id)!=peer->import_epoch) return bad(e,"Q2 imported peer changed its genuine connection epoch");
        for(size_t s=0;s<client->seat_count;++s) {
            qa_actor_id actor;
            if(!roster_actor(h,peer,s,&actor,e) || !qa_actor_id_equal(actor,peer->import_actors[s]))
                return bad(e,"Q2 imported peer lost its retained full canonical Source actor");
        }
        qa_application_network_q2_metadata m;
        if(!qa_application_network_q2_metadata_read(peer->source,&m,e)) return false;
        qa_application_network_q2 *live=peer->source;
        if(m.archival) {
            live=peer->travel_source;
            if(!live && !peer->import_source && !qa_application_network_q2_create(h->options.frontend->application,
                client->protocol,peer->admission.policy.server_count+1,&peer->import_source,e)) return false;
            if(!live) live=peer->import_source;
            if(!qa_application_network_q2_material_capability(live,peer->material_scripts,e)) return false;
        }
        if(!frontend_network_q2_host_bind_peer(peer,live,runtime,hooks,e)) return false;
        peer->admission.hooks=*hooks; *p=peer->admission.policy; peer->import_bound=true; return true;
    }
    return bad(e,"Q2 imported server has no genuine retained ordered Source claim");
}
static bool restored_local_player(void *context,qa_net_seat_id seat,qa_network_local_player *out,qa_error *e)
{
    q2_local_peer *p=context;
    if(!p || !out || !p->host->options.current(p->host->options.context,p->host) ||
        seat.owner!=p->binding.seat.owner || seat.index!=p->binding.seat.index)
        return bad(e,"Imported LOCAL receipt lost its actual ordered human seat");
    qa_actor_id actual;
    if(!qa_application_player_actor(p->host->options.frontend->application,p->authored,&actual) ||
        !qa_actor_id_equal(actual,p->player.actor)) return bad(e,"Imported LOCAL actor differs from its actual human roster");
    *out=p->player; return true;
}
bool frontend_network_q2_host_restore_local_hooks(frontend_network_q2_host *h,qa_network_runtime *runtime,
    const qa_net_client *client,qa_network_local_hooks *hooks,qa_error *e)
{
    if(!client || !hooks || !runtime_bind(h,runtime,e)) return false;
    for(size_t i=0;i<h->local_count;++i) {
        q2_local_peer *local=&h->locals[i]; qa_net_connect request;
        if(!frontend_network_q2_host_local_request(h,local,&request,e)) return false;
        if(!local->client.owner || !client_request(client,&request,local->client)) continue;
        if(qa_network_epoch(runtime,client->id)!=local->import_epoch) return bad(e,"Imported LOCAL connection changed its genuine epoch");
        *hooks=(qa_network_local_hooks){.context=local,.player=restored_local_player,
            .retained_player=frontend_network_q2_host_local_retained}; local->import_bound=true; return true;
    }
    return bad(e,"Imported LOCAL connection has no retained physical human owner");
}
bool frontend_network_q2_host_qualified(const frontend_network_q2_host *h,const qa_network_runtime *runtime,
    bool complete,qa_error *e)
{
    if(!h) return true;
    if(!runtime || h->options.runtime!=runtime || h->calls || !h->options.current(h->options.context,h) ||
        !qa_network_callbacks_idle(runtime) || (complete && h->importing && !h->import_ready))
        return bad(e,"Q2 HOST retains unfinished candidate Source callbacks");
    size_t count=0; uint32_t cursor=0; const qa_net_client *client;
    while(qa_net_connections_next(qa_network_connections(runtime),&cursor,&client)) {
        size_t found=0;
        for(size_t i=0;i<h->capacity;++i) {
            const q2_host_peer *peer=&h->peers[i];
            if(!peer->committed || !client_request(client,&peer->admission.connection,peer->client)) continue;
            if(h->importing && (!peer->import_bound || qa_network_epoch(runtime,client->id)!=peer->import_epoch)) return false;
            qa_network_q2_state state;
            if(!qa_network_q2_state_read((qa_network_runtime *)runtime,client->id,&state,e) || !state.server) return false;
            const qa_q2_codec *codec; qa_q2_config_layout layout;
            if(!qa_network_q2_server_codec((qa_network_runtime *)runtime,client->id,&codec,e) ||
                !qa_q2_config_layout_read(codec,&layout,e) || (peer->config_count && peer->config_count!=layout.max_configs))
                return bad(e,"Q2 issued config beforeimages differ from the actual negotiated namespace");
            uint64_t generation=qa_application_protocol_events_generation(h->options.frontend->application);
            if(peer->event_generation>generation || (peer->event_generation==generation &&
                (peer->event_cursor>qa_application_protocol_event_count(h->options.frontend->application) ||
                    peer->player_event_cursor>qa_application_q2_player_event_count(h->options.frontend->application))))
                return bad(e,"Q2 event cursor exceeds its real retained journal generation");
            ++found;
        }
        for(size_t i=0;i<h->local_count;++i) {
            const q2_local_peer *local=&h->locals[i]; qa_net_connect request;
            if(!frontend_network_q2_host_local_request(h,local,&request,e)) return false;
            if(!local->client.owner || !client_request(client,&request,local->client)) continue;
            if(h->importing && (!local->import_bound || qa_network_epoch(runtime,client->id)!=local->import_epoch)) return false;
            qa_network_local_player actual;
            if(!qa_network_local_player_retained_read(runtime,client->id,&actual,e) || !qa_actor_id_equal(actual.actor,local->player.actor) ||
                actual.source_owner!=local->player.source_owner || actual.source_slot!=local->player.source_slot) return false;
            ++found;
        }
        if(found!=1) return bad(e,"Q2 HOST runtime differs from its exact Source/LOCAL claim inventory");
        ++count;
    }
    size_t claimed=0;
    for(size_t i=0;i<h->capacity;++i) if(h->peers[i].committed) ++claimed;
    for(size_t i=0;i<h->local_count;++i) if(h->locals[i].client.owner) ++claimed;
    return count==claimed || bad(e,"Q2 HOST retains a canonical claim absent from its actual runtime");
}
bool frontend_network_q2_host_finish_restore(frontend_network_q2_host *h,qa_network_runtime *runtime,qa_error *e)
{
    if(!h) return true;
    if(!runtime_bind(h,runtime,e)) return false;
    if(h->import_ready) return frontend_network_q2_host_qualified(h,runtime,true,e);
    if(!frontend_network_q2_host_qualified(h,runtime,false,e)) return false;
    if(!h->options.local_only && !h->bootstrap) {
        qa_q2_server_bootstrap_options options=frontend_network_q2_host_bootstrap_options(h);
        if(!qa_network_q2_bootstrap_restore_server(runtime,&options,
            (qa_bytes){h->import_bootstrap.data,h->import_bootstrap.size},&h->bootstrap,e)) return false;
    }
    if(!h->unicast) {
        qa_q2_unicast_refs refs=cache_refs(h);
        if(!qa_q2_unicast_restore((qa_bytes){h->import_unicast.data,h->import_unicast.size},&refs,&h->unicast,e)) return false;
    }
    h->import_ready=true; qa_buffer_free(&h->import_bootstrap); qa_buffer_free(&h->import_unicast);
    return frontend_network_q2_host_qualified(h,runtime,true,e);
}
bool frontend_network_q2_host_importing(const frontend_network_q2_host *h)
{ return h && h->importing; }
bool frontend_network_q2_host_imported(const frontend_network_q2_host *h)
{ return h && h->importing && h->import_ready; }
bool frontend_network_q2_host_publication_ready(const frontend_network_q2_host *h,qa_error *e)
{ return !h || (h->importing && h->import_ready && frontend_network_q2_host_qualified(h,h->options.runtime,true,e)); }
void frontend_network_q2_host_publish_import(frontend_network_q2_host *h)
{ if(h) { h->importing=false; h->import_published=true; } }
void frontend_network_q2_host_restore_abort(frontend_network_q2_host **owned)
{
    frontend_network_q2_host *h=owned?*owned:NULL;
    if(!h || !h->importing || h->calls) return;
    qa_network_q2_bootstrap_destroy(h->bootstrap);
    qa_application_network_q2_destroy(h->discovery); qa_application_network_q2_destroy(h->travel_discovery);
    qa_application_network_q2_destroy(h->import_discovery); qa_q2_unicast_cache_destroy(h->unicast);
    for(size_t i=0;h->peers && i<h->capacity;++i) {
        q2_host_peer *p=&h->peers[i];
        qa_application_network_q2_destroy(p->source); qa_application_network_q2_destroy(p->travel_source);
        qa_application_network_q2_destroy(p->import_source); qa_buffer_free(&p->event_packet);
        for(size_t j=0;p->configs && j<p->config_count;++j) free(p->configs[j]);
        free(p->configs);
        for(size_t j=0;p->signon_configs && j<p->signon_config_count;++j) free(p->signon_configs[j]);
        free(p->signon_configs);
    }
    qa_buffer_free(&h->import_bootstrap); qa_buffer_free(&h->import_unicast);
    free(h->peers); free(h->locals); free(h); *owned=NULL;
}
