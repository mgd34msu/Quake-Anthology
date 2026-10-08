#include "network_q2_host_save.h"
#include "network_q2_host_private.h"
#include <stdlib.h>
#include <string.h>
#include "qa/application_network.h"

static bool bad(qa_error *e,const char *message)
{ return frontend_fail(e,QA_ERROR_FORMAT,message); }
static bool protocol_equal(qa_net_protocol_id a,qa_net_protocol_id b)
{ return a.kind==b.kind && a.revision==b.revision && a.flags==b.flags; }

static bool request_equal(const qa_net_connect *a,const qa_net_connect *b)
{
    if(!a || !b || a->attachment!=b->attachment || !protocol_equal(a->protocol,b->protocol) ||
        !qa_net_address_equal(&a->endpoint,&b->endpoint,true) || a->composition != b->composition ||
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
    return found==1 || bad(e,"Imported Q2 connection differs from its one genuine retained Source claim");
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
        if(found!=1) return bad(e,"Q2 HOST runtime differs from its exact Source claim inventory");
        ++count;
    }
    size_t claimed=0;
    for(size_t i=0;i<h->capacity;++i) if(h->peers[i].committed) ++claimed;
    return count==claimed || bad(e,"Q2 HOST retains a canonical claim absent from its actual runtime");
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
    free(h->peers); free(h); *owned=NULL;
}
