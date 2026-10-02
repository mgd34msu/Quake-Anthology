#include "network_q1_recipe_save.h"
#include "save_private.h"
#include <stdlib.h>
#include <string.h>

static bool address_fields(qa_source_save_io *io,qa_net_address *address)
{
    uint32_t kind=(uint32_t)address->kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_NET_IPV6 ||
        !qa_source_save_u16(io,&address->port)) return false;
    address->kind=(qa_net_address_kind)kind;
    return kind==QA_NET_IPV4 ? qa_source_save_bytes(io,address->host.ipv4,4) :
        qa_source_save_bytes(io,address->host.ipv6.bytes,16) &&
        qa_source_save_u32(io,&address->host.ipv6.scope);
}
static bool text_fields(qa_source_save_io *io,const char **text)
{
    char *owned=(char *)*text;
    bool ok=frontend_save_text(io,&owned);
    if(io->direction==QA_SOURCE_SAVE_READ) *text=owned;
    return ok;
}
static bool recipe_fields(qa_source_save_io *io,frontend_network_q1_client_recipe *recipe)
{
    uint32_t kind=(uint32_t)recipe->protocol.kind;
    qa_network_q1_client_policy *policy=&recipe->policy;
    qa_nq_signon *identity=&policy->nq_identity;
    if(!address_fields(io,&recipe->remote) || !address_fields(io,&recipe->connected_remote) ||
        !qa_source_save_u32(io,&kind) || kind>QA_NET_UNIFIED_1) return false;
    recipe->protocol.kind=(qa_net_protocol)kind;
    return qa_source_save_u32(io,&recipe->protocol.revision) && qa_source_save_u32(io,&recipe->protocol.flags) &&
        qa_source_save_u32(io,&recipe->profile) && qa_source_save_u32(io,&recipe->physical_seat) &&
        qa_source_save_u16(io,&recipe->qport) &&
        qa_source_save_count(io,&policy->message_bytes,SIZE_MAX) &&
        qa_source_save_count(io,&policy->fragment_bytes,SIZE_MAX) &&
        qa_source_save_count(io,&policy->queued_bytes,SIZE_MAX) &&
        qa_source_save_count(io,&policy->service_limit,SIZE_MAX) &&
        qa_source_save_count(io,&policy->pending_commands,SIZE_MAX) &&
        qa_source_save_u16(io,&policy->qport) && qa_source_save_u32(io,&policy->bytes_per_second) &&
        qa_source_save_bool(io,&policy->nq_options.standard_quake) &&
        qa_source_save_bool(io,&policy->nq_options.private_rerelease) &&
        qa_source_save_u8(io,&identity->stage) && text_fields(io,&identity->name) &&
        text_fields(io,&identity->spawn_parameters) && qa_source_save_u8(io,&identity->color) &&
        qa_source_save_bool(io,&identity->has_extension_flags) && qa_source_save_u32(io,&identity->extension_flags) &&
        qa_source_save_u64(io,&recipe->client.owner) && qa_source_save_u64(io,&recipe->client.generation) &&
        qa_source_save_u32(io,&recipe->client.slot) && qa_source_save_u64(io,&recipe->seat.owner) &&
        qa_source_save_u32(io,&recipe->seat.index) && qa_source_save_u64(io,&recipe->epoch) &&
        qa_source_save_bytes(io,recipe->composition.bytes,sizeof(recipe->composition.bytes)) &&
        qa_source_save_bool(io,&recipe->retired);
}
static bool recipe_valid(const frontend_network_q1_client_recipe *recipe,qa_error *error)
{
    const qa_network_q1_client_policy *policy=&recipe->policy;
    bool qw=qa_q1_is_qw(recipe->protocol);
    bool attached=recipe->client.owner!=0;
    if(!qa_q1_profile_valid(recipe->protocol,error)) return false;
    if(!recipe->profile || !recipe->remote.port || recipe->remote.kind>QA_NET_IPV6 ||
        recipe->connected_remote.kind>QA_NET_IPV6 || policy->qport!=recipe->qport ||
        policy->message_bytes<64 || policy->message_bytes>(qw?65525u:65527u) ||
        !policy->service_limit || policy->service_limit>policy->message_bytes ||
        !policy->pending_commands || policy->pending_commands>QA_NETWORK_COMMAND_BACKUP ||
        policy->queued_bytes<policy->message_bytes ||
        (qw ? !policy->bytes_per_second :
            !policy->fragment_bytes || policy->fragment_bytes>policy->message_bytes ||
            !policy->nq_identity.name || !policy->nq_identity.spawn_parameters || policy->nq_identity.stage) ||
        (attached ? !recipe->client.generation || !recipe->seat.owner || !recipe->epoch ||
            !recipe->connected_remote.port : recipe->client.generation || recipe->client.slot ||
            recipe->seat.owner || recipe->seat.index || recipe->epoch))
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved Q1 constructor recipe has invalid primitive receipts");
    return true;
}
void frontend_network_q1_recipe_free(frontend_network_q1_client_recipe *recipe)
{
    if(!recipe) return;
    free((char *)recipe->policy.nq_identity.name);
    free((char *)recipe->policy.nq_identity.spawn_parameters);
    *recipe=(frontend_network_q1_client_recipe){0};
}
bool frontend_network_q1_recipe_checkpoint(const frontend_network_q1_client_recipe *recipe,
    qa_buffer *out,qa_error *error)
{
    if(!recipe || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 recipe capture requires an empty owned output");
    if(!recipe_valid(recipe,error)) return false;
    qa_source_save_io io;
    if(!qa_source_save_writer(&io,NULL,error)) return false;
    frontend_network_q1_client_recipe copy=*recipe;
    char magic[5]={'Q','Q','1','R','2'};
    bool ok=qa_source_save_bytes(&io,magic,sizeof(magic)) && recipe_fields(&io,&copy) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    return ok;
}
bool frontend_network_q1_recipe_restore(qa_bytes bytes,frontend_network_q1_client_recipe *out,qa_error *error)
{
    if(!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 recipe restore lacks its owned output");
    qa_source_save_io io;
    if(!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    frontend_network_q1_client_recipe recipe={0};
    char magic[5];
    bool ok=qa_source_save_bytes(&io,magic,sizeof(magic)) && !memcmp(magic,"QQ1R2",sizeof(magic)) &&
        recipe_fields(&io,&recipe) && recipe_valid(&recipe,error) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) {
        frontend_network_q1_recipe_free(&recipe);
        if(!error || error->code==QA_OK)
            frontend_fail(error,QA_ERROR_FORMAT,"Saved Q1 constructor recipe is malformed");
        return false;
    }
    *out=recipe;
    return true;
}
