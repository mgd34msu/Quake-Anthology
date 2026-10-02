#include "network_q2_client.h"
#include "qa/network_q3.h"
#include "qa/application_network.h"
#include <math.h>
#include <stdio.h>
#include <ctype.h>

struct frontend_network_q2_client {
    frontend_network_q2_client_options options;
    qa_network_q2_bootstrap *bootstrap;
    frontend_remote_q2_source *source;
    qa_application_client_source application_source;
    frontend_remote_q2_domain domain;
    qa_actor_owner receiver;
    qa_net_seat_binding binding;
    qa_q2_client_admission admission;
    qa_q2_connect_request negotiated;
    unsigned calls;
    bool app_created, negotiating, admitting, retired, closing;
    char reason[1024];
};
static bool parent(const frontend_network_q2_client *owner)
{
    return owner && !owner->closing && owner->options.current(owner->options.context,owner) &&
        owner->options.frontend->application == owner->domain.application;
}
static bool protocol_equal(qa_net_protocol_id a, qa_net_protocol_id b)
{ return a.kind==b.kind && a.revision==b.revision && a.flags==b.flags; }
static bool attachment(void *context,const qa_application_client_source *source)
{
    frontend_network_q2_client *owner=context;
    const qa_net_client *client=owner && owner->options.runtime ?
        qa_net_connections_get(qa_network_connections(owner->options.runtime),source->client):NULL;
    return parent(owner) && client && source->runtime==owner->options.runtime &&
        qa_net_client_id_equal(source->client,owner->domain.client) &&
        source->network_seat.owner==owner->binding.seat.owner && source->network_seat.index==owner->binding.seat.index &&
        source->context.receiver==owner->receiver && source->context.physical_seat==owner->options.physical_seat &&
        source->connection_epoch==owner->domain.epoch && source->connection_epoch==qa_network_epoch(owner->options.runtime,client->id) &&
        client->seat_count==1 && qa_net_client_owns_seat(client,source->network_seat) &&
        client->seats[0].remote_index==0 && protocol_equal(client->protocol,owner->domain.protocol);
}
static bool physical(void *context,const qa_launch_instance *descriptor,qa_console *console,
    qa_cvars *cvars,const qa_command_context *command)
{
    frontend_network_q2_client *owner=context; qa_error error={0};
    return parent(owner) && frontend_remote_q2_source_owner_current(owner->source,descriptor,console,cvars,command,&error);
}
static bool retain(void *context,qa_error *error)
{ return frontend_remote_q2_source_owner_retain(((frontend_network_q2_client *)context)->source,error); }
static bool release(void *context,qa_error *error)
{ return frontend_remote_q2_source_owner_release(((frontend_network_q2_client *)context)->source,error); }
static bool idle(void *context)
{ return frontend_remote_q2_source_owner_idle(((frontend_network_q2_client *)context)->source); }
static bool entity_current(void *context,const qa_application_client_source *source,uint32_t number,uint64_t *generation)
{
    frontend_network_q2_client *owner=context; frontend_remote_q2_source_view view; qa_error error={0};
    return attachment(owner,source) && frontend_remote_q2_source_read(owner->source,&view,&error) &&
        frontend_remote_q2_entity_generation(view.receiver,number,generation);
}
static bool namespace_prepare(void *context,const qa_launch_instance *descriptor,
    frontend_remote_q2_domain *domain,qa_error *error)
{
    frontend_network_q2_client *owner=context; qa_frontend *f=owner->options.frontend;
    if (!parent(owner) || !qa_application_client_provider_prepare(f->application,descriptor,&owner->receiver,error)) return false;
    qa_command_context input=qa_input_seat_context(f->seats[owner->options.physical_seat].input);
    input.owner=0; input.actor=(qa_actor_id){0}; input.client=0; input.registry=0; input.generation=0;
    input.script=false;
    const qa_product *profile=qa_catalog_product(domain->catalog,domain->product);
    if (!profile) return false;
    input.dialect=profile->edition==QA_EDITION_RERELEASE ? QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
    if (!qa_application_client_provider_command(f->application,owner->receiver,input.seat,&input,
        &domain->command_context,&domain->configuration_generation,error)) return false;
    owner->domain=*domain;
    return true;
}
static bool configure(void *context,const qa_launch_instance *descriptor,qa_cvars *cvars,
    qa_console *console,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    static const char *const permissions[]={"allow_download","cl_http_downloads","allow_download_maps",
        "allow_download_models","allow_download_sounds","allow_download_players"};
    for(size_t i=0;i<sizeof(permissions)/sizeof(*permissions);++i)
        if(!qa_cvars_register(cvars,permissions[i],"1",QA_CVAR_ARCHIVE,owner->receiver,"",error)) return false;
    owner->domain.console=console; owner->domain.cvars=cvars;
    qa_application_client_options options={.descriptor=descriptor,.receiver=owner->receiver,
        .seat=owner->domain.command_context.seat,.physical_seat=owner->options.physical_seat,
        .configuration_generation=owner->domain.configuration_generation,.runtime=owner->options.runtime,
        .console=console,.cvars=cvars,.command=owner->domain.command_context,
        .owner={owner,retain,release,physical,idle,attachment,entity_current}};
    if(!qa_application_client_create(owner->domain.application,&options,&owner->application_source,error)) return false;
    owner->app_created=true;
    return true;
}
static bool admit_content(void *context,const frontend_remote_q2_domain *previous,
    const qa_launch_instance *descriptor,const frontend_remote_q2_domain *candidate,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!parent(owner) || !owner->app_created || !previous || !candidate || !descriptor ||
        previous->catalog!=owner->domain.catalog || previous->product!=owner->domain.product ||
        previous->configuration_generation!=owner->domain.configuration_generation ||
        !qa_net_client_id_equal(previous->client,owner->domain.client) || previous->epoch!=owner->domain.epoch ||
        candidate->runtime!=owner->options.runtime || candidate->application!=owner->domain.application ||
        candidate->console!=owner->domain.console || candidate->cvars!=owner->domain.cvars ||
        candidate->physical_seat!=owner->options.physical_seat ||
        !qa_net_client_id_equal(candidate->client,owner->domain.client) || candidate->epoch!=owner->domain.epoch ||
        candidate->seat.owner!=owner->binding.seat.owner || candidate->seat.index!=owner->binding.seat.index ||
        !protocol_equal(candidate->protocol,owner->domain.protocol) ||
        !frontend_remote_q2_source_owner_current(owner->source,descriptor,candidate->console,candidate->cvars,
            &candidate->command_context,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 content admission changed its physical connection or namespace");
    qa_application_client_source admitted;
    if(!qa_application_client_rebind(owner->domain.application,&owner->application_source,descriptor,
        &candidate->command_context,candidate->configuration_generation,&admitted,error)) return false;
    owner->application_source=admitted; owner->domain=*candidate;
    return true;
}
static bool source_current(void *context,const frontend_remote_q2_domain *domain,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    qa_net_protocol_id protocol=owner->negotiating ? owner->negotiated.protocol : owner->domain.protocol;
    if(!parent(owner) || domain->application!=owner->domain.application || domain->runtime!=owner->options.runtime ||
        domain->physical_seat!=owner->options.physical_seat || domain->catalog!=owner->domain.catalog ||
        domain->product!=owner->domain.product || domain->console!=owner->domain.console || domain->cvars!=owner->domain.cvars ||
        domain->configuration_generation!=owner->domain.configuration_generation ||
        domain->command_context.owner!=owner->receiver || !protocol_equal(domain->protocol,protocol))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT left its actual pending or attached source claim");
    if(!domain->client.owner) return !owner->domain.client.owner && !domain->client.generation && !domain->epoch && !domain->seat.owner;
    return qa_net_client_id_equal(domain->client,owner->domain.client) && domain->epoch==owner->domain.epoch &&
        domain->seat.owner==owner->binding.seat.owner && domain->seat.index==owner->binding.seat.index &&
        qa_application_client_current(domain->application,&owner->application_source);
}
static void print_source(void *context,const qa_command_context *command,const char *text)
{ frontend_network_q2_client *owner=context; frontend_console_print(owner->options.frontend,command,text); }
static bool print_bootstrap(void *context,const char *text,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!parent(owner)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 print left its actual network owner");
    frontend_console_print(owner->options.frontend,NULL,text); return true;
}
static bool records(void *context,const frontend_remote_q2_domain *domain,
    const qa_q2_server_record *batch,size_t count,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return source_current(owner,domain,error) && owner->options.records(owner->options.context,
        &owner->application_source,batch,count,error) && source_current(owner,domain,error);
}
static bool disconnected_source(void *context,const frontend_remote_q2_domain *domain,const char *reason,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!source_current(owner,domain,error) || !reason) return false;
    snprintf(owner->reason,sizeof(owner->reason),"%s",reason); owner->retired=true; return true;
}
static bool failed(void *context,const char *reason,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!print_bootstrap(context,reason,error)) return false;
    snprintf(owner->reason,sizeof(owner->reason),"%s",reason); owner->retired=true; return true;
}
static bool entity_actor(void *context,const frontend_remote_q2_domain *domain,uint32_t number,qa_actor_id *actor,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return source_current(owner,domain,error) && qa_application_client_entity_read(domain->application,
        &owner->application_source,number,actor,error);
}
static bool permission(const qa_cvars *cvars,const char *name)
{ const qa_cvar_view *value=qa_cvars_find(cvars,name); return value && value->number!=0; }
static bool path_prefix(const char *path,const char *prefix)
{
    while(*prefix) if(!*path || tolower((unsigned char)*path++)!=(unsigned char)*prefix++) return false;
    return true;
}
static bool path_extension(const char *extension,const char *expected)
{
    return extension && path_prefix(extension,expected) && strlen(extension)==strlen(expected);
}
static bool download_allowed(void *context,const char *path,bool *allowed,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!path || !allowed || !source_current(owner,&owner->domain,error)) return false;
    const qa_cvar_view *all=qa_cvars_find(owner->domain.cvars,"allow_download");
    *allowed=all && all->number>0;
    const char *category=path_prefix(path,"players/")?"players":path_prefix(path,"maps/")?"maps":
        path_prefix(path,"models/")?"models":path_prefix(path,"sound/")?"sounds":NULL;
    const char *extension=strrchr(path,'.');
    if(path_extension(extension,".pak") || path_extension(extension,".pkz")) {
        static const char *const names[]={"allow_download_maps","allow_download_models","allow_download_sounds","allow_download_players"};
        for(size_t i=0;i<4;++i) *allowed=*allowed && permission(owner->domain.cvars,names[i]);
    } else if(category) { char name[64]; snprintf(name,sizeof(name),"allow_download_%s",category); *allowed=*allowed && permission(owner->domain.cvars,name); }
    return true;
}
static bool download_nonce(void *context,uint64_t *out,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return out && source_current(owner,&owner->domain,error) &&
        owner->options.download_nonce(owner->options.context,out,error) && *out &&
        source_current(owner,&owner->domain,error);
}
static bool identity(void *context,qa_q2_client_identity *out,qa_error *error)
{
    frontend_network_q2_client *owner=context; qa_buffer info={0};
    if(!out || !source_current(owner,&owner->domain,error) ||
        !qa_cvars_info(owner->domain.cvars,QA_CVAR_USERINFO,sizeof(out->userinfo),&info,error)) return false;
    memset(out,0,sizeof(*out)); memcpy(out->userinfo,info.data,info.size); qa_buffer_free(&info); return true;
}
static bool abort_source(void *context,const qa_q2_client_admission *claim,qa_error *error)
{
    (void)claim; (void)error; frontend_network_q2_client *owner=context;
    owner->admitting=false; owner->retired=true; return true;
}
static bool cancel(void *context,uint64_t generation,qa_error *error)
{ (void)generation; (void)error; ((frontend_network_q2_client *)context)->retired=true; return true; }
static bool prepare(void *context,const qa_net_address *remote,const qa_q2_connect_request *request,
    const char *download_server,uint64_t generation,qa_q2_client_admission *out,qa_q2_preparation *result,qa_error *error)
{
    frontend_network_q2_client *owner=context; (void)download_server; (void)generation;
    if(!parent(owner) || !request || !out || !result || !qa_net_address_equal(remote,&owner->options.remote,true) ||
        request->protocol.kind!=owner->options.protocol.kind || request->social_count>1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 admission differs from its actual one-seat pending CLIENT");
    owner->negotiated=*request; owner->negotiating=true;
    bool admitted=frontend_remote_q2_source_pending_protocol(owner->source,request->protocol,error);
    owner->negotiating=false; if(!admitted) return false;
    owner->domain.protocol=request->protocol;
    frontend_remote_q2_source_view view;
    if(!frontend_remote_q2_source_read(owner->source,&view,error) ||
        !frontend_remote_q2_hooks(view.receiver,&owner->admission.hooks,error)) return false;
    owner->binding=(qa_net_seat_binding){{QA_NETWORK_COMMAND_OWNER,owner->options.physical_seat},0};
    owner->admission.connection=(qa_net_connect){.attachment=QA_NET_REMOTE,.endpoint=*remote,
        .protocol=request->protocol,.seats=&owner->binding,.seat_count=1,.composition=view.descriptor->identity};
    owner->admission.policy=(qa_network_q2_client_policy){
        .channel={.protocol=request->protocol,.new_channel=request->new_channel,.compress=request->compression,
            .qport=request->qport,.payload_bytes=request->payload_bytes,.datagram_bytes=65507},
        .messages={.config_strings=qa_cvars_dialect(owner->domain.cvars)==QA_CONSOLE_Q2_RERELEASE?12448u:2080u,
            .inventory_slots=256,.history_capacity=64,.max_inflated_bytes=65535},
        .pending_commands=64};
    owner->admission.source_claim=owner; owner->admitting=true;
    *out=owner->admission; *result=QA_Q2_PREPARATION_READY; return true;
}
static bool committed(void *context,const qa_q2_client_admission *claim,qa_net_client_id id,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!parent(owner) || !owner->admitting || claim->source_claim!=owner) return false;
    owner->domain.client=id; owner->domain.seat=owner->binding.seat; owner->domain.epoch=qa_network_epoch(owner->options.runtime,id);
    qa_application_client_source bound;
    if(!qa_application_client_bind(owner->domain.application,&owner->application_source,id,owner->binding.seat,
        owner->domain.epoch,&bound,error)) return false;
    owner->application_source=bound;
    if(!frontend_remote_q2_source_bind(owner->source,&owner->domain,error)) return false;
    owner->admitting=false; return true;
}
bool frontend_network_q2_client_create(const frontend_network_q2_client_options *options,
    frontend_network_q2_client **out,qa_error *error)
{
    qa_q2_codec codec;
    if(!options || !out || *out || !options->frontend || !options->runtime || !options->current ||
        !options->download_nonce || !options->records ||
        options->physical_seat>=options->frontend->options.seats ||
        !qa_q2_codec_init(&codec,options->protocol,error) || options->protocol.kind==QA_NET_Q2KEX_2023 ||
        options->protocol.kind==QA_NET_Q2KEX_DEMO_2022)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT factory requires its actual one-seat raw transport and Source consumers");
    frontend_network_q2_client *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating Q2 CLIENT connection owner");
    *out=owner; owner->options=*options;
    qa_frontend *f=options->frontend; uint32_t seat=qa_input_seat_context(f->seats[options->physical_seat].input).seat;
    owner->domain=(frontend_remote_q2_domain){.application=f->application,.runtime=options->runtime,
        .physical_seat=options->physical_seat,.protocol=options->protocol,.catalog=qa_application_catalog(f->application)};
    frontend_remote_q2_source_options source={.client={.domain=owner->domain,.context=owner,.current=source_current,
        .download_allowed=download_allowed,.download_nonce=download_nonce,.entity_actor=entity_actor,
        .records=records,.disconnected=disconnected_source},.prepare_namespace=namespace_prepare,.print=print_source,
        .configure=configure,.admit_content=admit_content};
    qa_vfs *prepared=NULL;
    if(!frontend_remote_q2_source_recipe(owner->domain.catalog,options->protocol,"remote-q2-client",seat,
        &source.metadata,&prepared,error)) return false;
    owner->domain.product=source.metadata.profile; source.client.domain=owner->domain;
    bool created=frontend_remote_q2_source_create(f,&source,&owner->source,error); qa_vfs_destroy(prepared);
    if(!created) return false;
    qa_q2_client_bootstrap_options bootstrap={.remote=options->remote,.protocols=&owner->options.protocol,.protocol_count=1,
        .payload_bytes=1390,.qport=options->qport,.timeout_ns=UINT64_C(120000000000),
        .hooks={.context=owner,.identity=identity,.prepare=prepare,.committed=committed,.abort=abort_source,
            .cancel=cancel,.print=print_bootstrap,.failed=failed}};
    return qa_network_q2_bootstrap_client(options->runtime,&bootstrap,&owner->bootstrap,error);
}
bool frontend_network_q2_client_admit(frontend_network_q2_client *owner,const qa_net_connect *request,
    bool *recognized,qa_error *error)
{
    if(!recognized) return false;
    *recognized=owner && request && request->protocol.kind==owner->options.protocol.kind;
    if(!*recognized) return true;
    const qa_net_connect *held=&owner->admission.connection;
    return (parent(owner) && owner->admitting && request->attachment==held->attachment &&
        qa_net_address_equal(&request->endpoint,&held->endpoint,true) && protocol_equal(request->protocol,held->protocol) &&
        request->seat_count==1 && request->seats && request->seats[0].seat.owner==owner->binding.seat.owner &&
        request->seats[0].seat.index==owner->binding.seat.index && request->seats[0].remote_index==0 &&
        qa_sha256_equal(&request->composition,&held->composition)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 attach lacks its authentic pending Source claim");
}
bool frontend_network_q2_client_receive(frontend_network_q2_client *owner,const qa_net_datagram *packet,
    bool *recognized,qa_error *error)
{
    if(!parent(owner) || owner->calls) return false;
    ++owner->calls;
    bool ok=qa_network_q2_bootstrap_receive(owner->bootstrap,packet,recognized,error);
    --owner->calls; return ok;
}
void frontend_network_q2_client_disconnected(frontend_network_q2_client *owner,qa_net_client_id id)
{
    if(owner && qa_net_client_id_equal(owner->domain.client,id)) {
        owner->retired=true; (void)qa_network_q2_bootstrap_disconnected(owner->bootstrap,id,NULL);
    }
}
static bool tick(frontend_network_q2_client *owner,uint64_t now,qa_error *error)
{
    if(!parent(owner)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT lost its enclosing network");
    if(owner->retired) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client);
        return !client || qa_network_detach(owner->options.runtime,client->id,owner->reason,error);
    }
    if(!qa_network_q2_bootstrap_continue(owner->bootstrap,now,error)) return false;
    if(owner->domain.client.owner && !qa_network_q2_client_continue(owner->options.runtime,owner->domain.client,error)) return false;
    size_t executed;
    return frontend_remote_q2_source_drain(owner->source,1024,&executed,error) &&
        qa_network_q2_bootstrap_tick(owner->bootstrap,now,error);
}
bool frontend_network_q2_client_tick(frontend_network_q2_client *owner,uint64_t now,qa_error *error)
{
    if(!parent(owner) || owner->calls) return false;
    ++owner->calls; bool ok=tick(owner,now,error); --owner->calls; return ok;
}
bool frontend_network_q2_client_idle(const frontend_network_q2_client *owner)
{
    return !owner || (!owner->calls && !owner->negotiating && !owner->admitting &&
        (!owner->source || frontend_remote_q2_source_owner_idle(owner->source)));
}
bool frontend_network_q2_client_destroy(frontend_network_q2_client **owned,qa_error *error)
{
    frontend_network_q2_client *owner=owned?*owned:NULL;
    if(!owner) return true;
    if(owner->calls || !qa_network_callbacks_idle(owner->options.runtime) ||
        (owner->source && !frontend_remote_q2_source_owner_idle(owner->source))) return false;
    if(owner->domain.client.owner && qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client)) {
        if(!qa_network_q2_client_disconnect(owner->options.runtime,owner->domain.client,owner->options.frontend->wall_time_ns,error) ||
            !qa_network_detach(owner->options.runtime,owner->domain.client,"Q2 CLIENT closed",error)) return false;
    }
    if(owner->app_created) {
        if(!qa_application_client_retire(owner->domain.application,&owner->application_source,error)) return false;
        owner->app_created=false;
    }
    if(!frontend_remote_q2_source_destroy(&owner->source,error)) return false;
    if(owner->receiver && !qa_application_client_provider_release(owner->domain.application,owner->receiver,error)) return false;
    owner->receiver=0; owner->closing=true;
    qa_network_q2_bootstrap_destroy(owner->bootstrap); free(owner); *owned=NULL; return true;
}
