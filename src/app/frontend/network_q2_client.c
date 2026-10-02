#include "network_q2_client.h"
#include "qa/network_q3.h"
#include "qa/application_network.h"
#include "qa/network_q2_bootstrap_save.h"
#include "remote_q2_effects_bridge.h"
#include "neutral_config.h"
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
    bool importing, restore_finished, cleaning_import;
    const qa_application_client_state *restored_application;
    frontend_client_source_options configuration;
    bool configuration_owned;
    bool material_scripts;
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
{
    frontend_network_q2_client *owner=context;
    if(owner->cleaning_import) return frontend_remote_q2_source_owner_retirement_idle(owner->source);
    return owner->importing && !owner->restore_finished ? frontend_remote_q2_source_owner_import_idle(owner->source) :
        frontend_remote_q2_source_owner_idle(owner->source);
}
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
static bool initialize(void *context,const qa_launch_instance *descriptor,qa_cvars *cvars,
    const qa_command_context *command,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return owner->configuration.initialize(owner->configuration.context,descriptor,cvars,command,error);
}
static bool install(void *context,bool restoring,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return !owner->configuration.install || owner->configuration.install(owner->configuration.context,
        &owner->application_source,restoring,error);
}
static bool configure_step(void *context,bool *complete,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return owner->app_created && owner->configuration.configure(owner->configuration.context,
        &owner->application_source,complete,error);
}
static bool configuration_retire(void *context,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return !owner->configuration.retire || owner->configuration.retire(owner->configuration.context,
        owner->app_created?&owner->application_source:NULL,error);
}
static void configuration_released(void *context)
{
    frontend_network_q2_client *owner=context;
    if(owner->configuration_owned && owner->configuration.released)
        owner->configuration.released(owner->configuration.context);
    owner->configuration_owned=false;
}
static qa_cvars *configuration_cvars(void *context,const qa_command_context *command,const char *name)
{ frontend_network_q2_client *o=context; return o->configuration.cvar_owner(o->configuration.context,command,name); }
static qa_cvars *configuration_visible(void *context,const qa_command_context *command,size_t ordinal)
{ frontend_network_q2_client *o=context; return o->configuration.visible_cvars(o->configuration.context,command,ordinal); }
static bool configuration_edit(void *context,const qa_command_context *command,qa_cvars *cvars,
    struct qa_cvars_edit **edit,qa_error *error)
{ frontend_network_q2_client *o=context; return o->configuration.cvar_edit(o->configuration.context,command,cvars,edit,error); }
static bool configuration_script(void *context,const qa_command_context *command,const char *path,
    qa_bytes *bytes,void **claim,qa_error *error)
{ frontend_network_q2_client *o=context; return o->configuration.read_script(o->configuration.context,command,path,bytes,claim,error); }
static void configuration_script_release(void *context,void *claim)
{ frontend_network_q2_client *o=context; o->configuration.release_script(o->configuration.context,claim); }
static void configuration_script_complete(void *context,const qa_command_context *command,const char *path,bool success)
{ frontend_network_q2_client *o=context; o->configuration.script_complete(o->configuration.context,command,path,success); }
static bool configuration_allow(void *context,const qa_command_invocation *call)
{ frontend_network_q2_client *o=context; return o->configuration.allow_command(o->configuration.context,call); }
static qa_command_result configuration_command(void *context,const qa_command_invocation *call,qa_error *error)
{ frontend_network_q2_client *o=context; return o->configuration.command(o->configuration.context,call,error); }
static qa_command_result configuration_forward(void *context,const qa_command_invocation *call,qa_error *error)
{ frontend_network_q2_client *o=context; return o->configuration.forward(o->configuration.context,call,error); }
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
    frontend_remote_q2_source_view source;
    if(!source_current(owner,domain,error) || !frontend_remote_q2_source_read(owner->source,&source,error)) return false;
    bool ok=owner->options.records?owner->options.records(owner->options.context,&owner->application_source,batch,count,error):
        frontend_remote_q2_effects_records(source.receiver,batch,count,error);
    return ok && source_current(owner,domain,error);
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
    if(!frontend_remote_q2_source_material_scripts(request,&owner->material_scripts,error) ||
        !frontend_remote_q2_source_pending_capabilities(owner->source,request,error)) return false;
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
static frontend_remote_q2_source_options source_options(frontend_network_q2_client *owner)
{
    return (frontend_remote_q2_source_options){.client={.domain=owner->domain,.context=owner,.current=source_current,
        .download_allowed=download_allowed,.download_nonce=download_nonce,.entity_actor=entity_actor,
        .records=records,.disconnected=disconnected_source,.material_scripts=owner->material_scripts},
        .prepare_namespace=namespace_prepare,.print=print_source,
        .configure=configure,.admit_content=admit_content,.initialize=initialize,.install=install,
        .configure_step=configure_step,.retire=configuration_retire,.released=configuration_released,
        .cvar_owner=owner->configuration.cvar_owner?configuration_cvars:NULL,
        .visible_cvars=owner->configuration.visible_cvars?configuration_visible:NULL,
        .cvar_edit=owner->configuration.cvar_edit?configuration_edit:NULL,
        .read_script=owner->configuration.read_script?configuration_script:NULL,
        .release_script=owner->configuration.release_script?configuration_script_release:NULL,
        .script_complete=owner->configuration.script_complete?configuration_script_complete:NULL,
        .allow_command=owner->configuration.allow_command?configuration_allow:NULL,
        .command=owner->configuration.command?configuration_command:NULL,
        .template_forward=owner->configuration.forward?configuration_forward:NULL};
}
static qa_q2_client_bootstrap_options bootstrap_options(frontend_network_q2_client *owner)
{
    return (qa_q2_client_bootstrap_options){.remote=owner->options.remote,.protocols=&owner->options.protocol,.protocol_count=1,
        .payload_bytes=1390,.qport=owner->options.qport,.timeout_ns=UINT64_C(120000000000),
        .hooks={.context=owner,.identity=identity,.prepare=prepare,.committed=committed,.abort=abort_source,
            .cancel=cancel,.print=print_bootstrap,.failed=failed}};
}
bool frontend_network_q2_client_create(const frontend_network_q2_client_options *options,
    frontend_network_q2_client **out,qa_error *error)
{
    qa_q2_codec codec;
    if(!options || !out || *out || !options->frontend || !options->runtime || !options->current ||
        !options->download_nonce ||
        options->physical_seat>=options->frontend->options.seats ||
        !qa_q2_codec_init(&codec,options->protocol,error) || options->protocol.kind==QA_NET_Q2KEX_2023 ||
        options->protocol.kind==QA_NET_Q2KEX_DEMO_2022)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT factory requires its actual one-seat raw transport and Source consumers");
    frontend_network_q2_client *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating Q2 CLIENT connection owner");
    *out=owner; owner->options=*options;
    qa_frontend *f=options->frontend; uint32_t seat=qa_input_seat_context(f->seats[options->physical_seat].input).seat;
    if(!frontend_config_store_neutral_pending_options(f->config_store,options->physical_seat,
        &owner->configuration,error)) return false;
    owner->configuration_owned=true;
    owner->domain=(frontend_remote_q2_domain){.application=f->application,.runtime=options->runtime,
        .physical_seat=options->physical_seat,.protocol=options->protocol,.catalog=qa_application_catalog(f->application)};
    frontend_remote_q2_source_options source=source_options(owner);
    qa_vfs *prepared=NULL;
    if(!frontend_remote_q2_source_recipe(owner->domain.catalog,options->protocol,"remote-q2-client",seat,
        &source.metadata,&prepared,error)) return false;
    owner->domain.product=source.metadata.profile; source.client.domain=owner->domain;
    bool created=frontend_remote_q2_source_create(f,&source,&owner->source,error); qa_vfs_destroy(prepared);
    if(!created) return false;
    qa_q2_client_bootstrap_options bootstrap=bootstrap_options(owner);
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
    if(!parent(owner) || owner->calls || owner->importing) return false;
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
    bool ready=false;
    if(!frontend_remote_q2_source_advance(owner->source,&ready,error)) return false;
    if(!ready) return true;
    if(!qa_network_q2_bootstrap_continue(owner->bootstrap,now,error)) return false;
    if(owner->domain.client.owner && !qa_network_q2_client_continue(owner->options.runtime,owner->domain.client,error)) return false;
    size_t executed;
    return frontend_remote_q2_source_drain(owner->source,1024,&executed,error) &&
        qa_network_q2_bootstrap_tick(owner->bootstrap,now,error);
}
bool frontend_network_q2_client_tick(frontend_network_q2_client *owner,uint64_t now,qa_error *error)
{
    if(!parent(owner) || owner->calls || owner->importing) return false;
    ++owner->calls; bool ok=tick(owner,now,error); --owner->calls; return ok;
}
bool frontend_network_q2_client_idle(const frontend_network_q2_client *owner)
{
    return !owner || (!owner->calls && !owner->negotiating && !owner->admitting &&
        (!owner->source || idle((void *)owner)));
}
bool frontend_network_q2_client_owns_input(const frontend_network_q2_client *owner,uint32_t physical)
{ return owner && !owner->closing && owner->options.physical_seat==physical; }
bool frontend_network_q2_client_configuration_primary(const frontend_network_q2_client *owner,
    const qa_application_client_source *source)
{
    const qa_application_client_source *held=owner?&owner->application_source:NULL;
    return source && owner && !owner->closing && owner->app_created && source->descriptor==held->descriptor &&
        source->runtime==owner->options.runtime && source->context.receiver==held->context.receiver &&
        source->context.seat==held->context.seat && source->context.physical_seat==owner->options.physical_seat &&
        source->context.console==held->context.console && source->context.cvars==held->context.cvars &&
        source->context.entity_owner==held->context.entity_owner && source->context.lifetime==held->context.lifetime &&
        source->configuration_generation==held->configuration_generation &&
        qa_net_client_id_equal(source->client,held->client) && source->connection_epoch==held->connection_epoch;
}
bool frontend_network_q2_client_destroy(frontend_network_q2_client **owned,qa_error *error)
{
    frontend_network_q2_client *owner=owned?*owned:NULL;
    if(!owner) return true;
    if(owner->importing && !owner->restore_finished) owner->cleaning_import=true;
    if(owner->calls || !qa_network_callbacks_idle(owner->options.runtime) ||
        (owner->source && !idle(owner))) return false;
    if(owner->domain.client.owner && qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client)) {
        if(owner->importing) {
            bool incomplete=qa_network_connection_incomplete(owner->options.runtime,owner->domain.client);
            if(incomplete ? !qa_network_discard_incomplete(owner->options.runtime,owner->domain.client,error) :
                !qa_network_detach(owner->options.runtime,owner->domain.client,"Q2 candidate closed",error)) return false;
        } else if(!qa_network_q2_client_disconnect(owner->options.runtime,owner->domain.client,owner->options.frontend->wall_time_ns,error) ||
            !qa_network_detach(owner->options.runtime,owner->domain.client,"Q2 CLIENT closed",error)) return false;
    }
    if(owner->app_created) {
        if(!frontend_remote_q2_source_retire(owner->source,error)) return false;
        if(!qa_application_client_retire(owner->domain.application,&owner->application_source,error)) return false;
        owner->app_created=false;
    }
    if(!frontend_remote_q2_source_destroy(&owner->source,error)) return false;
    if(owner->configuration_owned &&
        !frontend_config_store_neutral_options_cancel(owner->options.frontend->config_store,&owner->configuration,error)) return false;
    owner->configuration_owned=false;
    if(owner->receiver && !qa_application_client_provider_release(owner->domain.application,owner->receiver,error)) return false;
    owner->receiver=0; owner->closing=true;
    qa_network_q2_bootstrap_destroy(owner->bootstrap); free(owner); *owned=NULL; return true;
}
bool frontend_network_q2_client_commands_owned(const frontend_network_q2_client *owner,const qa_application *app,
    const qa_application_console_scope *scope,const qa_console *console)
{
    return owner && !owner->cleaning_import && parent(owner) && owner->app_created && owner->source && app==owner->domain.application &&
        scope && scope->kind==QA_APPLICATION_CONSOLE_CLIENT && scope->provider==owner->receiver &&
        scope->seat==owner->domain.command_context.seat && console==owner->domain.console &&
        idle((void *)owner) && qa_application_client_idle(owner->domain.application,&owner->application_source);
}
bool frontend_network_q2_client_commands_capture(frontend_network_q2_client *owner,qa_application *app,
    const qa_application_console_scope *scope,const qa_console *console,qa_buffer *out,qa_error *error)
{
    return frontend_network_q2_client_commands_owned(owner,app,scope,console) &&
        frontend_remote_q2_source_commands_capture(owner->source,app,scope,console,out,error);
}
bool frontend_network_q2_client_commands_restore(frontend_network_q2_client *owner,qa_application *app,
    const qa_application_console_scope *scope,qa_console *console,qa_bytes bytes,qa_error *error)
{
    return frontend_network_q2_client_commands_owned(owner,app,scope,console) &&
        frontend_remote_q2_source_commands_restore(owner->source,app,scope,console,bytes,error);
}
bool frontend_network_q2_client_finish_restore(frontend_network_q2_client *owner,qa_error *error)
{
    if(!owner) return true;
    if(!parent(owner) || !owner->importing || owner->calls) return false;
    if(owner->restore_finished) return true;
    if(!frontend_remote_q2_source_finish_restore(owner->source,error)) return false;
    owner->restore_finished=true; return true;
}
bool frontend_network_q2_client_publication_ready(const frontend_network_q2_client *owner,qa_error *error)
{
    if(!owner) return true;
    if(!parent(owner) || !owner->importing || !owner->restore_finished || !owner->app_created ||
        !frontend_network_q2_client_idle(owner) || !attachment((void *)owner,&owner->application_source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 publication retains unfinished physical CLIENT import");
    return qa_application_client_idle(owner->domain.application,&owner->application_source);
}
void frontend_network_q2_client_publish(frontend_network_q2_client *owner)
{ if(owner) owner->importing=false; }
bool frontend_network_q2_client_qualified(const frontend_network_q2_client *owner,
    const qa_network_runtime *runtime,bool complete,qa_error *error)
{
    if(!parent(owner) || owner->cleaning_import || runtime!=owner->options.runtime || !frontend_network_q2_client_idle(owner) ||
        (complete && owner->importing && !owner->restore_finished))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 connection retains unfinished physical CLIENT state");
    frontend_remote_q2_source_view view;
    if(!frontend_remote_q2_source_read(owner->source,&view,error) || !frontend_remote_q2_source_current(&view)) return false;
    uint32_t cursor=0; const qa_net_client *client=NULL;
    if(!owner->domain.client.owner)
        return !qa_net_connections_next(qa_network_connections(runtime),&cursor,&client) && !view.bound;
    if(!attachment((void *)owner,&owner->application_source) || !view.bound) return false;
    while(qa_net_connections_next(qa_network_connections(runtime),&cursor,&client))
        if(!qa_net_client_id_equal(client->id,owner->domain.client))
            return frontend_fail(error,QA_ERROR_FORMAT,"Q2 CLIENT runtime carries another physical connection");
    qa_network_q2_state state;
    if(!qa_network_q2_state_read(owner->options.runtime,owner->domain.client,&state,error) || state.server) return false;
    if(complete) {
        frontend_remote_q2_view receiver;
        if(!frontend_remote_q2_metadata_read(view.receiver,&receiver,error) ||
            !frontend_remote_q2_current(&receiver)) return false;
    }
    return true;
}
void frontend_network_q2_client_state_free(frontend_network_q2_client_state *state)
{
    if(!state) return;
    frontend_remote_q2_source_state_free(&state->source);
    qa_application_client_state_free(&state->application);
    qa_buffer_free(&state->receiver); qa_buffer_free(&state->bootstrap);
    *state=(frontend_network_q2_client_state){0};
}
bool frontend_network_q2_client_capture(frontend_network_q2_client *owner,
    const frontend_remote_q2_restore_refs *refs,frontend_network_q2_client_state *out,qa_error *error)
{
    if(!out || !refs || !parent(owner) || owner->importing || owner->retired || !owner->app_created ||
        !frontend_network_q2_client_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 capture requires its returned actual physical CLIENT");
    frontend_network_q2_client_state state={.remote=owner->options.remote,.protocol=owner->options.protocol,
        .qport=owner->options.qport,.physical_seat=owner->options.physical_seat,
        .negotiated=owner->negotiated,.policy=owner->admission.policy,
        .composition=owner->admission.connection.composition};
    frontend_remote_q2_source_view view;
    bool ok=frontend_remote_q2_source_capture(owner->source,&state.source,error) &&
        frontend_remote_q2_source_read(owner->source,&view,error) &&
        qa_application_client_capture(owner->domain.application,&owner->application_source,&state.application,error) &&
        frontend_remote_q2_checkpoint(view.receiver,refs,&state.receiver,error) &&
        qa_network_q2_bootstrap_capture(owner->bootstrap,&state.bootstrap,error);
    if(!ok) { frontend_network_q2_client_state_free(&state); return false; }
    *out=state; return true;
}
static bool physical_restore(void *context,const qa_launch_instance *descriptor,qa_cvars *cvars,
    qa_console *console,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    const qa_launch_instance *constructor=NULL;
    if(!parent(owner) || !owner->importing || !owner->restored_application ||
        !frontend_remote_q2_source_constructor_read(owner->source,&constructor,error) ||
        !qa_application_client_provider_prepare(owner->domain.application,constructor,&owner->receiver,error) ||
        owner->receiver!=owner->restored_application->receiver)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 restored descriptor changed its actual CLIENT provider");
    owner->domain.console=console; owner->domain.cvars=cvars;
    qa_application_client_options options={.descriptor=descriptor,.receiver=owner->receiver,
        .seat=owner->domain.command_context.seat,.physical_seat=owner->options.physical_seat,
        .configuration_generation=owner->domain.configuration_generation,.runtime=owner->options.runtime,
        .console=console,.cvars=cvars,.command=owner->domain.command_context,
        .owner={owner,retain,release,physical,idle,attachment,entity_current}};
    if(!qa_application_client_create_restored(owner->domain.application,&options,owner->restored_application,
        &owner->application_source,error)) return false;
    owner->app_created=true; return true;
}
bool frontend_network_q2_client_importing(const frontend_network_q2_client *owner)
{ return owner && owner->importing && (owner->options.frontend->source_restoring || owner->restore_finished); }
bool frontend_network_q2_client_restore_prepare(const frontend_network_q2_client_options *options,
    const frontend_network_q2_client_restore *saved,frontend_network_q2_client **out,qa_error *error)
{
    if(!options || !saved || !saved->application || !out || *out || !options->frontend ||
        !options->frontend->source_restoring || !options->runtime || !options->current || !options->download_nonce ||
        saved->source.domain.application!=options->frontend->application ||
        saved->source.domain.runtime!=options->runtime || saved->source.domain.physical_seat!=options->physical_seat ||
        saved->source.domain.protocol.kind!=options->protocol.kind ||
        !qa_net_client_id_equal(saved->application->client,saved->source.domain.client) ||
        saved->application->connection_epoch!=saved->source.domain.epoch ||
        saved->application->network_seat.owner!=saved->source.domain.seat.owner ||
        saved->application->network_seat.index!=saved->source.domain.seat.index)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 import requires its real candidate connection and claimed Source recipes");
    frontend_network_q2_client *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining restored Q2 CLIENT owner");
    *out=owner; owner->options=*options; owner->domain=saved->source.domain; owner->importing=true;
    if(!frontend_config_store_neutral_pending_options(options->frontend->config_store,options->physical_seat,
        &owner->configuration,error)) return false;
    owner->configuration_owned=true;
    owner->receiver=saved->application->receiver; owner->binding=(qa_net_seat_binding){saved->source.domain.seat,0};
    owner->negotiated=saved->negotiated; owner->admission.policy=saved->policy;
    if(!frontend_remote_q2_source_material_scripts(&owner->negotiated,&owner->material_scripts,error)) return false;
    owner->admission.connection=(qa_net_connect){.attachment=QA_NET_REMOTE,.endpoint=options->remote,
        .protocol=owner->domain.protocol,.seats=&owner->binding,.seat_count=1,.composition=saved->composition};
    owner->admission.source_claim=owner; owner->restored_application=saved->application;
    frontend_remote_q2_source_options source=source_options(owner);
    frontend_remote_q2_source_restore request=saved->source; request.physical_admit=physical_restore;
    bool ok=frontend_remote_q2_source_restore_prepare(options->frontend,&source,&request,&owner->source,error);
    owner->restored_application=NULL;
    if(!ok) return false;
    qa_q2_client_bootstrap_options bootstrap=bootstrap_options(owner);
    return qa_network_q2_bootstrap_restore_client(options->runtime,&bootstrap,saved->bootstrap,&owner->bootstrap,error);
}
bool frontend_network_q2_client_restore_hooks(frontend_network_q2_client *owner,qa_network_runtime *runtime,
    const qa_net_client *client,qa_network_q2_client_policy *policy,qa_network_q2_client_hooks *hooks,qa_error *error)
{
    if(!owner || !client || !policy || !hooks || !frontend_network_q2_client_importing(owner) ||
        !parent(owner) || runtime!=owner->options.runtime || !qa_net_client_id_equal(client->id,owner->domain.client) ||
        client->attachment!=QA_NET_REMOTE || client->seat_count!=1 ||
        !qa_net_address_equal(&client->endpoint,&owner->options.remote,true) ||
        !protocol_equal(client->protocol,owner->domain.protocol) ||
        client->seats[0].seat.owner!=owner->binding.seat.owner || client->seats[0].seat.index!=owner->binding.seat.index ||
        client->seats[0].remote_index || !qa_sha256_equal(&client->composition,&owner->admission.connection.composition))
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 wire import has another real physical CLIENT attachment");
    frontend_remote_q2_source_view view;
    if(!frontend_remote_q2_source_read(owner->source,&view,error) || !frontend_remote_q2_hooks(view.receiver,hooks,error)) return false;
    *policy=owner->admission.policy; return true;
}
bool frontend_network_q2_client_source_read(const frontend_network_q2_client *owner,
    frontend_remote_q2_source_view *view,qa_error *error)
{
    return parent(owner) && frontend_remote_q2_source_read(owner->source,view,error);
}
bool frontend_network_q2_client_content_visit(const frontend_network_q2_client *owner,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    return !owner || (parent(owner) && !owner->calls &&
        frontend_remote_q2_source_content_visit(owner->source,visitor,error));
}
