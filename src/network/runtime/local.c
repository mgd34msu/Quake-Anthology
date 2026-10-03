#include "internal.h"
#include "qa/network_local.h"
#include "qa/network_save.h"
#include <stdlib.h>
typedef struct local_peer {
    qa_network_runtime *runtime;
    qa_network_local_hooks hooks;
    qa_net_seat_id seat;
    qa_network_local_player player;
} local_peer;
static bool current(local_peer *peer,qa_error *error)
{
    qa_network_local_player actual;
    return peer && peer->hooks.player(peer->hooks.context,peer->seat,&actual,error) &&
        qa_actor_id_equal(actual.actor,peer->player.actor) && actual.source_owner==peer->player.source_owner &&
        actual.source_slot==peer->player.source_slot;
}
static bool retained(local_peer *peer,qa_error *error)
{
    qa_network_local_player actual;
    bool ok=peer && (peer->hooks.retained_player ?
        peer->hooks.retained_player(peer->hooks.context,peer->seat,&actual,error) :
        peer->hooks.player(peer->hooks.context,peer->seat,&actual,error));
    return ok && qa_actor_id_equal(actual.actor,peer->player.actor) &&
        actual.source_owner==peer->player.source_owner && actual.source_slot==peer->player.source_slot;
}
static bool receive(void *context,qa_network_runtime *runtime,qa_net_client_id client,
    const qa_net_datagram *packet,qa_error *error)
{
    (void)context; (void)runtime; (void)client; (void)packet;
    return qa_network_fail(error,"A local Source connection receives no wire datagrams");
}
static bool flush(void *context,qa_network_runtime *runtime,qa_net_client_id client,uint64_t now,qa_error *error)
{ (void)runtime; (void)client; (void)now; return current(context,error); }
static bool command(void *context,const qa_network_command *input,qa_error *error)
{
    local_peer *peer=context;
    return current(peer,error) && qa_actor_id_equal(input->actor,peer->player.actor) &&
        qa_network_accept(peer->runtime,input,error);
}
static bool restart(void *context,uint64_t epoch,const qa_sha256_digest *composition,qa_error *error)
{ (void)epoch; (void)composition; return current(context,error); }
static bool rebind(void *context,const qa_net_address *address,qa_error *error)
{ return address && address->kind==QA_NET_LOOPBACK && current(context,error); }
static void close_peer(void *context) { free(context); }
static const qa_network_peer_ops ops={.receive=receive,.flush=flush,.command=command,
    .restart=restart,.rebind=rebind,.close=close_peer};
bool qa_network_local_peer(const qa_network_peer *peer)
{
    return peer && peer->occupied && peer->ops.receive==receive && peer->ops.flush==flush &&
        peer->ops.command==command && peer->ops.restart==restart && peer->ops.rebind==rebind &&
        peer->ops.close==close_peer && peer->state && peer->seat_count==1;
}
bool qa_network_attach_local(qa_network_runtime *runtime,const qa_net_connect *request,
    const qa_network_local_hooks *hooks,uint64_t now,qa_net_client_id *out,qa_error *error)
{
    if(!runtime || !request || request->attachment!=QA_NET_LOCAL_SEAT || request->endpoint.kind!=QA_NET_LOOPBACK ||
        request->seat_count!=1 || !request->seats || !hooks || !hooks->player)
        return qa_network_fail(error,"Local attachment requires its actual human Source seat");
    local_peer *peer=calloc(1,sizeof(*peer));
    if(!peer) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining local Source connection"); return false; }
    peer->runtime=runtime; peer->hooks=*hooks; peer->seat=request->seats[0].seat;
    bool ok=hooks->player(hooks->context,peer->seat,&peer->player,error) && peer->player.actor.registry &&
        peer->player.source_owner && peer->player.source_slot &&
        qa_network_attach(runtime,request,&ops,peer,now,out,error);
    if(!ok) free(peer);
    return ok;
}
bool qa_network_local_player_read(const qa_network_runtime *runtime,qa_net_client_id id,
    qa_network_local_player *out,qa_error *error)
{
    if(!runtime || !out || !qa_net_connections_get(runtime->connections,id) || id.slot>=runtime->options.clients ||
        !qa_network_local_peer(&runtime->peers[id.slot])) return qa_network_fail(error,"Missing actual local Source connection");
    local_peer *peer=runtime->peers[id.slot].state;
    if(peer->runtime!=runtime || !current(peer,error)) return false;
    *out=peer->player; return true;
}
bool qa_network_local_player_refresh(qa_network_runtime *runtime,qa_net_client_id id,qa_error *error)
{
    if(!runtime || runtime->callback || runtime->pumping)
        return qa_network_fail(error,"Local Source refresh requires its returned runtime");
    qa_network_peer *installed=qa_network_peer_get(runtime,id,error);
    const qa_net_client *client=qa_net_connections_get(runtime->connections,id);
    if(!qa_network_local_peer(installed) || !client || client->attachment!=QA_NET_LOCAL_SEAT ||
        client->seat_count!=1) return qa_network_fail(error,"Local Source refresh lacks its genuine human connection");
    local_peer *peer=installed->state; qa_network_local_player actual;
    runtime->callback=true;
    bool ok=peer->hooks.player(peer->hooks.context,peer->seat,&actual,error);
    runtime->callback=false;
    if(!ok || !actual.actor.registry || !actual.source_owner || !actual.source_slot) return false;
    peer->player=actual; return true;
}
bool qa_network_local_player_retained_read(const qa_network_runtime *runtime,qa_net_client_id id,
    qa_network_local_player *out,qa_error *error)
{
    if(!runtime || !out || !qa_net_connections_get(runtime->connections,id) || id.slot>=runtime->options.clients ||
        !qa_network_local_peer(&runtime->peers[id.slot])) return qa_network_fail(error,"Missing retained local Source connection");
    local_peer *peer=runtime->peers[id.slot].state;
    if(peer->runtime!=runtime || !retained(peer,error)) return false;
    *out=peer->player; return true;
}
bool qa_network_local_checkpoint_peer(const qa_network_peer *peer,const qa_network_checkpoint_refs *refs,
    qa_buffer *out,qa_error *error)
{
    if(!qa_network_local_peer(peer) || !refs || !refs->save_actor || !out || !retained(peer->state,error)) return false;
    local_peer *local=peer->state; qa_saved_actor_id actor;
    if(!refs->save_actor(refs->context,local->player.actor,&actor,error)) return false;
    uint8_t *bytes=malloc(32); if(!bytes) { qa_error_set(error,QA_ERROR_MEMORY,0,"Saving local Source connection"); return false; }
    qa_net_writer writer; qa_net_writer_init(&writer,bytes,32,error);
    bool ok=qa_net_write_u32(&writer,UINT32_C(0x4c4e4151)) &&
        qa_net_write_u64(&writer,actor.generation) && qa_net_write_u32(&writer,actor.slot) &&
        qa_net_write_u32(&writer,local->player.source_slot);
    if(!ok) { free(bytes); return false; }
    *out=(qa_buffer){bytes,qa_net_writer_size(&writer)}; return true;
}
bool qa_network_local_restore_peer(qa_network_runtime *runtime,const qa_net_client *client,qa_bytes bytes,
    const qa_network_checkpoint_refs *refs,qa_network_peer *out,qa_error *error)
{
    if(!runtime || !client || client->attachment!=QA_NET_LOCAL_SEAT || client->seat_count!=1 || !refs ||
        !refs->source_local || !refs->restore_actor) return false;
    qa_net_reader reader; qa_net_reader_init(&reader,bytes,error);
    if(qa_net_read_u32(&reader)!=UINT32_C(0x4c4e4151)) return false;
    qa_saved_actor_id saved={.generation=qa_net_read_u64(&reader),.slot=qa_net_read_u32(&reader)};
    uint32_t slot=qa_net_read_u32(&reader); qa_actor_id actor; qa_network_local_hooks hooks;
    if(reader.failed || qa_net_reader_remaining(&reader) || !refs->restore_actor(refs->context,saved,&actor,error) ||
        !refs->source_local(refs->context,runtime,client,&hooks,error) || !hooks.player) return false;
    local_peer *local=calloc(1,sizeof(*local));
    if(!local) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring local Source connection"); return false; }
    local->runtime=runtime; local->seat=client->seats[0].seat; local->hooks=hooks;
    bool custody=hooks.retained_player ? hooks.retained_player(hooks.context,local->seat,&local->player,error) :
        hooks.player(hooks.context,local->seat,&local->player,error);
    if(!custody || !qa_actor_id_equal(actor,local->player.actor) ||
        slot!=local->player.source_slot || !local->player.source_owner) { free(local); return false; }
    out->ops=ops; out->state=local; return true;
}
