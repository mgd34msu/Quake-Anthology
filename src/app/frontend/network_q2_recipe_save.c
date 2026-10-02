#include "network_q2_recipe_save.h"
#include "internal.h"
#include "qa/network_q2_bootstrap_save.h"
#include "qa/network_q2_session_save.h"
#include "qa/network_q2_wire_save.h"
#include <string.h>

static bool address_fields(qa_source_save_io *io,qa_net_address *address)
{
    uint32_t kind=(uint32_t)address->kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_NET_IPV6 ||
        !qa_source_save_u16(io,&address->port) || !address->port) return false;
    address->kind=(qa_net_address_kind)kind;
    return kind==QA_NET_IPV4 ? qa_source_save_bytes(io,address->host.ipv4,4) :
        qa_source_save_bytes(io,address->host.ipv6.bytes,16) && qa_source_save_u32(io,&address->host.ipv6.scope);
}
static bool fields(qa_source_save_io *io,frontend_network_q2_client_state *recipe)
{
    qa_q2_codec codec;
    if(!address_fields(io,&recipe->remote) || !qa_q2_save_protocol(io,&recipe->protocol) ||
        recipe->protocol.kind==QA_NET_Q2KEX_DEMO_2022 || !qa_q2_codec_init(&codec,recipe->protocol,io->error) ||
        !qa_source_save_u16(io,&recipe->qport) || !qa_source_save_u32(io,&recipe->physical_seat) ||
        !qa_network_q2_save_client_policy(io,&recipe->policy,true) ||
        !qa_q2_save_connect_request(io,&recipe->negotiated,true) ||
        !qa_source_save_bytes(io,recipe->composition.bytes,sizeof(recipe->composition.bytes)) ||
        !qa_source_save_bool(io,&recipe->retired)) return false;
    bool selected=qa_q2_protocol_version(recipe->negotiated.protocol)!=0;
    bool policy=qa_q2_protocol_version(recipe->policy.channel.protocol)!=0;
    return (selected || (!recipe->negotiated.protocol.kind && !recipe->negotiated.protocol.revision &&
        !recipe->negotiated.protocol.flags)) &&
        (!selected || (recipe->negotiated.protocol.kind==recipe->protocol.kind &&
        recipe->negotiated.qport==recipe->qport)) &&
        (!policy || (recipe->policy.channel.protocol.kind==recipe->protocol.kind &&
        recipe->policy.channel.qport==recipe->qport && !recipe->policy.channel.server));
}
bool frontend_network_q2_recipe_checkpoint(const frontend_network_q2_client_state *recipe,
    qa_buffer *out,qa_error *error)
{
    if(!recipe || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recipe capture requires an empty owned output");
    qa_source_save_io io;
    if(!qa_source_save_writer(&io,NULL,error)) return false;
    frontend_network_q2_client_state copy=*recipe;
    char magic[5]={'Q','Q','2','R','2'};
    bool ok=qa_source_save_bytes(&io,magic,sizeof(magic)) && fields(&io,&copy) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if(!ok && (!error || error->code==QA_OK))
        frontend_fail(error,QA_ERROR_FORMAT,"Q2 constructor recipe has invalid primitive receipts");
    return ok;
}
bool frontend_network_q2_recipe_restore(qa_bytes bytes,frontend_network_q2_client_state *out,qa_error *error)
{
    if(!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recipe restore lacks its value output");
    qa_source_save_io io;
    if(!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    frontend_network_q2_client_state recipe={0}; char magic[5];
    bool ok=qa_source_save_bytes(&io,magic,sizeof(magic)) && !memcmp(magic,"QQ2R2",sizeof(magic)) &&
        fields(&io,&recipe) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) {
        if(!error || error->code==QA_OK)
            frontend_fail(error,QA_ERROR_FORMAT,"Saved Q2 constructor recipe is malformed");
        return false;
    }
    *out=recipe; return true;
}
