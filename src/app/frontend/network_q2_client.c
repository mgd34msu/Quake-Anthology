#include "network_q2_client.h"
#include "qa/network_q3.h"
#include "qa/application_network.h"
#include "qa/network_q2_bootstrap_save.h"
#include "remote_q2_effects_bridge.h"
#include "neutral_config.h"
#include "view_settings.h"
#include "qa/bot_bsp.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <ctype.h>

struct frontend_network_q2_client {
    frontend_network_q2_client_options options;
    qa_network_q2_bootstrap *bootstrap;
    frontend_remote_q2_source *source;
    qa_application_client_source application_source;
    frontend_remote_q2_domain domain;
    qa_source_frame_time_binding frame_time;
    qa_q2_download_policy download_policy;
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
    qa_fs_stage *restore_stage;
    frontend_demo_sink demo_sink;
    bool demo_held, demo_waiting, demo_clock_started;
    double demo_ms;
    uint64_t demo_generation;
    qa_unified_frame_pool *mvd_pool;
    qa_unified_frame_lease *mvd_header_storage, *mvd_frame_storage, *mvd_geometry_storage;
    qa_q2_mvd_header mvd_header;
    qa_q2_mvd_frame mvd_frame;
    qa_q2_player mvd_spectator;
    qa_net_reader mvd_cursor;
    qa_collision_geometry *mvd_geometry;
    qa_trace_scratch *mvd_trace;
    frontend_remote_q2 *mvd_receiver;
    int32_t *mvd_clusters;
    size_t mvd_cluster_count;
    qa_stamp_set mvd_cluster_seen;
    qa_collision_leaf mvd_view_leaf;
    int32_t mvd_ordinal;
    uint8_t mvd_viewpoint;
    bool mvd_cursor_held, mvd_frame_pending, mvd_header_pending, mvd_viewpoint_changed;
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
    return parent(owner) && !owner->retired && client && source->runtime==owner->options.runtime &&
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
static bool retirement_custody(void *context,const qa_application_client_source *source)
{
    frontend_network_q2_client *owner=context;
    if(!source || !parent(owner) || !owner->retired) return false;
    if(!owner->app_created) {
        const qa_application_client_state *saved=owner->restored_application;
        return owner->importing && owner->options.frontend->source_restoring && saved &&
            source->runtime==owner->options.runtime && source->context.receiver==saved->receiver &&
            source->context.entity_owner==saved->entity_owner && source->context.lifetime &&
            source->context.seat==saved->seat && source->context.physical_seat==saved->physical_seat &&
            source->configuration_generation==saved->configuration_generation &&
            qa_net_client_id_equal(source->client,saved->client) &&
            qa_net_client_id_equal(source->client,owner->domain.client) &&
            source->connection_epoch==saved->connection_epoch && source->connection_epoch==owner->domain.epoch &&
            source->network_seat.owner==saved->network_seat.owner && source->network_seat.index==saved->network_seat.index &&
            source->network_seat.owner==owner->domain.seat.owner && source->network_seat.index==owner->domain.seat.index &&
            !qa_net_connections_get(qa_network_connections(owner->options.runtime),source->client) &&
            physical(owner,source->descriptor,source->context.console,source->context.cvars,&source->context.command);
    }
    if(!qa_application_client_associated(owner->domain.application,source)) return false;
    const qa_application_client_source *held=&owner->application_source;
    return source->runtime==owner->options.runtime && source->descriptor==held->descriptor &&
        source->context.receiver==owner->receiver && source->context.lifetime==held->context.lifetime &&
        source->context.entity_owner==held->context.entity_owner &&
        source->context.entity_definition==held->context.entity_definition &&
        source->context.seat==held->context.seat && source->context.physical_seat==owner->options.physical_seat &&
        source->configuration_generation==held->configuration_generation &&
        qa_net_client_id_equal(source->client,owner->domain.client) && source->connection_epoch==owner->domain.epoch &&
        source->network_seat.owner==owner->domain.seat.owner && source->network_seat.index==owner->domain.seat.index &&
        physical(owner,source->descriptor,source->context.console,source->context.cvars,&source->context.command);
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
static bool entity_publication(void *context, const qa_application_client_source *source,
    qa_application_client_entity_publication *out)
{
    frontend_network_q2_client *owner = context;
    frontend_remote_q2_source_view view; qa_error error = {0};
    return attachment(owner, source) && frontend_remote_q2_source_read(owner->source, &view, &error) &&
        frontend_remote_q2_entity_publication_read(view.receiver, out);
}
static bool namespace_prepare(void *context,const qa_launch_instance *descriptor,
    frontend_remote_q2_domain *domain,qa_error *error)
{
    frontend_network_q2_client *owner=context; qa_frontend *f=owner->options.frontend;
    if (!parent(owner) || !qa_application_client_provider_prepare(f->application,descriptor,&owner->receiver,error)) return false;
    qa_command_context input=qa_input_seat_context(f->seats[owner->options.physical_seat].input);
    input.seat=owner->options.seat.index;
    input.owner=0; input.actor=(qa_actor_id){0}; input.client=0; input.registry=0; input.generation=0;
    input.script=false;
    const qa_product *profile=qa_catalog_product(domain->catalog,domain->product);
    if (!profile) return false;
    input.dialect=qa_product_ruleset(profile,QA_GAME_Q2);
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
    qa_q2_download_policy_bind(&owner->download_policy,cvars);
    qa_source_frame_time_bind(cvars,&owner->frame_time);
    owner->domain.command_context.cvar_view=qa_cvars_view_identity(cvars);
    qa_application_client_options options={.descriptor=descriptor,.receiver=owner->receiver,
        .seat=owner->domain.command_context.seat,.physical_seat=owner->options.physical_seat,
        .configuration_generation=owner->domain.configuration_generation,.runtime=owner->options.runtime,
        .console=console,.cvars=cvars,.command=owner->domain.command_context,
        .owner={.context=owner,.retain=retain,.release=release,.current=physical,.idle=idle,
            .connection_current=attachment,.entity_current=entity_current,.entity_publication=entity_publication,
            .retirement_current=retirement_custody}};
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
static bool configuration_advance(void *context,qa_application_client_preparation *token,
    bool *complete,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return owner->app_created && owner->configuration.configuration_advance &&
        owner->configuration.configuration_advance(owner->configuration.context,
            &owner->application_source,token,complete,error);
}
static void configuration_released(void *context)
{
    frontend_network_q2_client *owner=context;
    if(owner->configuration_owned && owner->configuration.released)
        owner->configuration.released(owner->configuration.context);
    owner->configuration_owned=false;
}
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
        domain->command_context.owner!=owner->receiver ||
        domain->command_context.cvar_view!=owner->domain.command_context.cvar_view || !protocol_equal(domain->protocol,protocol))
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
    bool ok=!owner->options.records ||
        owner->options.records(owner->options.context,&owner->application_source,batch,count,error);
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
static bool entities_changed(void *context, const frontend_remote_q2_domain *domain, qa_error *error)
{
    frontend_network_q2_client *owner = context;
    return source_current(owner, domain, error) && qa_application_client_entities_refresh(domain->application,
        &owner->application_source, error);
}
static bool permission(frontend_network_q2_client *owner,qa_q2_download_category category)
{ const qa_cvar_view *value=qa_q2_download_policy_read(&owner->download_policy,owner->domain.cvars,category); return value && value->number!=0; }
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
    if (owner->options.demo) { *allowed=false; return true; }
    const qa_cvar_view *all=qa_q2_download_policy_read(&owner->download_policy,owner->domain.cvars,QA_Q2_DOWNLOAD_ALL);
    *allowed=all && all->number>0;
    qa_q2_download_category category=path_prefix(path,"players/")?QA_Q2_DOWNLOAD_PLAYERS:path_prefix(path,"maps/")?QA_Q2_DOWNLOAD_MAPS:
        path_prefix(path,"models/")?QA_Q2_DOWNLOAD_MODELS:path_prefix(path,"sound/")?QA_Q2_DOWNLOAD_SOUNDS:QA_Q2_DOWNLOAD_ALL;
    const char *extension=strrchr(path,'.');
    if(path_extension(extension,".pak") || path_extension(extension,".pkz")) {
        for(size_t i=QA_Q2_DOWNLOAD_MAPS;i<QA_Q2_DOWNLOAD_CATEGORY_COUNT;++i) *allowed=*allowed && permission(owner,(qa_q2_download_category)i);
    } else if(category!=QA_Q2_DOWNLOAD_ALL) *allowed=*allowed && permission(owner,category);
    return true;
}
static bool download_stage(void *context,qa_fs_root *root,const char *path,
    qa_fs_stage **out,uint64_t *nonce,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    return out && !*out && nonce && !*nonce && source_current(owner,&owner->domain,error) &&
        owner->options.download_stage(owner->options.context,root,path,out,nonce,error) && *out && *nonce &&
        source_current(owner,&owner->domain,error);
}
static bool restore_stage(void *context,qa_fs_root *root,const char *path,uint64_t logical_nonce,
    bool published,qa_bytes prefix,qa_fs_stage **out,uint64_t *native_nonce,qa_fs_identity *identity,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!owner || !parent(owner) || !owner->importing || !owner->options.frontend->source_restoring ||
        !owner->options.restore_stage || owner->restore_stage || !out || *out || !native_nonce || *native_nonce)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 download import lost its retained native stage constructor");
    if(!owner->options.restore_stage(owner->options.context,root,path,logical_nonce,published,prefix,
        &owner->restore_stage,native_nonce,identity,error)) return false;
    *out=owner->restore_stage; owner->restore_stage=NULL; return true;
}
static bool identity(void *context,qa_q2_client_identity *out,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!out || !source_current(owner,&owner->domain,error)) return false;
    memset(out,0,sizeof(*out));
    if(!qa_cvars_info_write(owner->domain.cvars,QA_CVAR_USERINFO,sizeof(out->userinfo),out->userinfo,error)) return false;
    if(owner->options.protocol.kind==QA_NET_Q2KEX_2023) out->social_count=1;
    return true;
}
static bool transport_ready(void *context,bool *ready,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!ready || !parent(owner) || !owner->options.lobby)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"KEX CLIENT has no actual retained LAN join");
    *ready=qa_kex_lan_ready(owner->options.lobby); return true;
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
    if(!frontend_remote_q2_source_pending_capabilities(owner->source,request,&owner->material_scripts,error)) return false;
    frontend_remote_q2_source_view view;
    if(!frontend_remote_q2_source_read(owner->source,&view,error) ||
        !frontend_remote_q2_hooks(view.receiver,&owner->admission.hooks,error)) return false;
    owner->binding=(qa_net_seat_binding){owner->options.seat,0};
    owner->admission.connection=(qa_net_connect){.attachment=owner->options.demo?QA_NET_LOCAL_SEAT:QA_NET_REMOTE,.endpoint=*remote,
        .protocol=request->protocol,.seats=&owner->binding,.seat_count=1,.composition=view.descriptor->identity};
    qa_q2_codec codec; qa_q2_config_layout layout;
    if(!qa_q2_codec_init(&codec,request->protocol,error) ||
        !qa_q2_config_layout_read(&codec,&layout,error)) return false;
    owner->admission.policy=(qa_network_q2_client_policy){
        .channel={.protocol=request->protocol,.new_channel=request->new_channel,.compress=request->compression,
            .qport=request->qport,.payload_bytes=request->payload_bytes,.datagram_bytes=65507},
        .messages={.config_strings=layout.max_configs,
            .inventory_slots=256,.history_capacity=64,.max_inflated_bytes=65535,.demo=owner->options.demo},
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
        .download_allowed=download_allowed,.download_stage=download_stage,.entity_actor=entity_actor,
        .restore_stage=restore_stage,
        .records=records,.disconnected=disconnected_source,.material_scripts=owner->material_scripts,.demo=owner->options.demo,
        .entities_changed=entities_changed},
        .prepare_namespace=namespace_prepare,.print=print_source,
        .configure=configure,.admit_content=admit_content,.initialize=initialize,.install=install,
        .configure_step=configure_step,.retire=configuration_retire,.retirement_current=retirement_custody,
        .released=configuration_released,
        .configuration_advance=configuration_advance,
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
        .hooks={.context=owner,.identity=identity,.transport_ready=owner->options.lobby?transport_ready:NULL,
            .prepare=prepare,.committed=committed,.abort=abort_source,
            .cancel=cancel,.print=print_bootstrap,.failed=failed}};
}
bool frontend_network_q2_client_create(const frontend_network_q2_client_options *options,
    frontend_network_q2_client **out,qa_error *error)
{
    qa_q2_codec codec;
    if(!options || !out || *out || !options->frontend || !options->runtime || !options->current ||
        !options->download_stage ||
        options->physical_seat>=options->frontend->options.seats ||
        !qa_q2_codec_init(&codec,options->protocol,error) ||
        (options->protocol.kind==QA_NET_Q2KEX_2023 && !options->lobby && !options->demo &&
            options->remote.kind!=QA_NET_LOOPBACK) ||
        (options->protocol.kind==QA_NET_Q2KEX_DEMO_2022 && !options->demo) ||
        (options->demo && options->remote.kind!=QA_NET_LOOPBACK))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT factory requires its actual one-seat raw transport and Source consumers");
    frontend_network_q2_client *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating Q2 CLIENT connection owner");
    *out=owner; owner->options=*options;
    owner->mvd_viewpoint=options->demo_viewpoint;
    if (options->demo && options->demo_format==FRONTEND_DEMO_MVD) {
        owner->mvd_pool=qa_unified_frame_pool_create(0,0,error);
        if (!owner->mvd_pool) return false;
    }
    if (!owner->options.seat.owner) owner->options.seat=(qa_net_seat_id){QA_NETWORK_COMMAND_OWNER,0};
    qa_frontend *f=options->frontend;
    if(!frontend_config_store_neutral_pending_options(f->config_store,options->physical_seat,
        &owner->configuration,error)) return false;
    owner->configuration_owned=true;
    owner->domain=(frontend_remote_q2_domain){.application=f->application,.runtime=options->runtime,
        .physical_seat=options->physical_seat,.protocol=options->protocol,.catalog=qa_application_catalog(f->application)};
    frontend_remote_q2_source_options source=source_options(owner);
    qa_vfs *prepared=NULL;
    if(!frontend_remote_q2_source_recipe(owner->domain.catalog,options->protocol,
        options->profile,options->selected,"remote-q2-client",owner->options.seat.index,
        &source.metadata,&prepared,error)) return false;
    owner->options.profile=source.metadata.profile; owner->options.selected=source.metadata.selected;
    owner->domain.product=source.metadata.profile; source.client.domain=owner->domain;
    bool created=frontend_remote_q2_source_create(f,&source,&owner->source,error); qa_vfs_destroy(prepared);
    if(!created) return false;
    if (options->demo) return true;
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
        request->composition==held->composition) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 attach lacks its authentic pending Source claim");
}
bool frontend_network_q2_client_receive(frontend_network_q2_client *owner,const qa_net_datagram *packet,
    bool *recognized,qa_error *error)
{
    if(!parent(owner) || owner->calls || owner->importing) return false;
    if (owner->options.demo) { *recognized=false; return true; }
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
    if (owner->options.demo) return true;
    if(owner->retired) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client);
        return !client || qa_network_detach(owner->options.runtime,client->id,owner->reason,error);
    }
    bool ready=false;
    if(!frontend_remote_q2_source_advance(owner->source,&ready,error)) return false;
    if(!ready) return true;
    if(!qa_network_q2_bootstrap_continue(owner->bootstrap,now,error)) return false;
    if(owner->domain.client.owner) {
        if(!qa_network_q2_client_continue(owner->options.runtime,owner->domain.client,error)) return false;
        if(owner->retired) return true;
        if(!qa_network_q2_client_send(owner->options.runtime,owner->domain.client,now,error)) return false;
    }
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
bool frontend_network_q2_client_retired(const frontend_network_q2_client *owner)
{ return owner && owner->retired; }
bool frontend_network_q2_client_configuration_primary(const frontend_network_q2_client *owner,
    const qa_application_client_source *source)
{
    const qa_application_client_source *held=owner?&owner->application_source:NULL;
    return source && owner && !owner->closing && owner->app_created &&
        frontend_client_source_descriptor_equal(source->descriptor,held->descriptor) &&
        source->runtime==owner->options.runtime && source->context.receiver==held->context.receiver &&
        source->context.seat==held->context.seat && source->context.physical_seat==owner->options.physical_seat &&
        source->context.console==held->context.console && source->context.cvars==held->context.cvars &&
        source->context.entity_owner==held->context.entity_owner && source->context.lifetime==held->context.lifetime &&
        source->configuration_generation==held->configuration_generation &&
        qa_net_client_id_equal(source->client,held->client) && source->connection_epoch==held->connection_epoch;
}
const qa_source_frame_time_binding *frontend_network_q2_client_frame_time(const frontend_network_q2_client *owner)
{ return owner ? &owner->frame_time : NULL; }
bool frontend_network_q2_client_configuration_read(const frontend_network_q2_client *owner,
    qa_application_client_source *out,bool *ready,qa_error *error)
{
    if(!out || !ready || !owner || !parent(owner) || !owner->app_created ||
        !qa_application_client_current(owner->options.frontend->application,&owner->application_source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 CLIENT configuration lost its actual physical owner");
    *out=owner->application_source;
    *ready=frontend_remote_q2_source_configuration_completed(owner->source); return true;
}
bool frontend_network_q2_client_configuration_advance(frontend_network_q2_client *owner,
    qa_application_client_preparation *token,bool *complete,qa_error *error)
{
    return owner && owner->source && !owner->calls && !owner->closing &&
        frontend_network_q2_client_configuration_primary(owner,qa_application_client_prepare_source(token)) &&
        frontend_remote_q2_source_configuration_continue(owner->source,token,complete,error);
}
bool frontend_network_q2_client_retired_recipient_read(const frontend_network_q2_client *owner,
    qa_application_client_source *out,bool *ready,qa_error *error)
{
    if(!owner || !owner->retired || !owner->app_created || owner->closing || !out || !ready ||
        !parent(owner) ||
        !qa_application_client_retirement_current(owner->options.frontend->application,&owner->application_source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 retired recipient lost its physical Source custody");
    *out=owner->application_source;
    *ready=frontend_remote_q2_source_configuration_completed(owner->source);
    return true;
}
bool frontend_network_q2_client_retirement_current(const frontend_network_q2_client *owner,
    const qa_application_client_source *source,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    if(!owner || !owner->source || !owner->app_created || !source ||
        source->context.lifetime!=owner->application_source.context.lifetime ||
        source->context.receiver!=owner->receiver || console!=owner->domain.console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 retirement lost its retained physical CLIENT owner");
    return frontend_remote_q2_source_retirement_current(owner->source,source,console,command,error);
}
bool frontend_network_q2_client_destroy(frontend_network_q2_client **owned,qa_error *error)
{
    frontend_network_q2_client *owner=owned?*owned:NULL;
    if(!owner) return true;
    if(owner->importing && !owner->restore_finished) owner->cleaning_import=true;
    if(owner->calls || owner->demo_held || !qa_network_callbacks_idle(owner->options.runtime) ||
        (owner->source && !idle(owner))) return false;
    if(!qa_fs_stage_close_checked(&owner->restore_stage,false,error)) return false;
    if(owner->domain.client.owner && qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client)) {
        if(owner->importing) {
            bool incomplete=qa_network_connection_incomplete(owner->options.runtime,owner->domain.client);
            if(incomplete ? !qa_network_discard_incomplete(owner->options.runtime,owner->domain.client,error) :
                !qa_network_detach(owner->options.runtime,owner->domain.client,"Q2 candidate closed",error)) return false;
        } else if(!qa_network_q2_client_disconnect(owner->options.runtime,owner->domain.client,owner->options.frontend->wall_time_ns,error) ||
            !qa_network_detach(owner->options.runtime,owner->domain.client,"Q2 CLIENT closed",error)) return false;
    }
    if(owner->app_created) {
        owner->retired=true;
        if(owner->bootstrap && !qa_network_q2_bootstrap_disconnected(owner->bootstrap,owner->domain.client,error)) return false;
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
    qa_network_q2_bootstrap_destroy(owner->bootstrap);
    qa_unified_frame_lease_release(owner->mvd_header_storage);
    qa_unified_frame_lease_release(owner->mvd_frame_storage);
    qa_trace_scratch_destroy(owner->mvd_trace);
    qa_collision_destroy(owner->mvd_geometry);
    qa_unified_frame_lease_release(owner->mvd_geometry_storage);
    qa_unified_frame_pool_destroy(&owner->mvd_pool);
    free(owner); *owned=NULL; return true;
}
bool frontend_network_q2_client_publication_ready(const frontend_network_q2_client *owner,qa_error *error)
{
    if(!owner) return true;
    if(!parent(owner) || !owner->importing || !owner->restore_finished || !owner->app_created ||
        !frontend_network_q2_client_idle(owner) ||
        !(owner->retired ? retirement_custody((void *)owner,&owner->application_source) :
            (!owner->domain.client.owner || attachment((void *)owner,&owner->application_source))))
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
    if(owner->retired) {
        if(!frontend_remote_q2_source_retirement_custody_read(owner->source,&view,error) ||
            !qa_application_client_retirement_current(owner->domain.application,&owner->application_source)) return false;
        if(qa_net_connections_get(qa_network_connections(runtime),owner->domain.client))
            return frontend_fail(error,QA_ERROR_FORMAT,"Retired Q2 CLIENT still has a canonical peer");
    } else if(!frontend_remote_q2_source_physical_read(owner->source,&view,error) ||
        !frontend_remote_q2_source_physical_current(&view)) return false;
    uint32_t cursor=0; const qa_net_client *client=NULL;
    if(owner->retired)
        return !qa_net_connections_next(qa_network_connections(runtime),&cursor,&client);
    if(!owner->domain.client.owner)
        return !qa_net_connections_next(qa_network_connections(runtime),&cursor,&client) && !view.bound;
    if(!view.receiver || !attachment((void *)owner,&owner->application_source) || !view.bound) return false;
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
bool frontend_network_q2_client_importing(const frontend_network_q2_client *owner)
{ return owner && owner->importing && (owner->options.frontend->source_restoring || owner->restore_finished); }
bool frontend_network_q2_client_content_visit(const frontend_network_q2_client *owner,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    return !owner || (parent(owner) && !owner->calls &&
        frontend_remote_q2_source_content_visit(owner->source,visitor,error));
}

bool frontend_network_q2_client_restore_abort_ready(const frontend_network_q2_client *owner,
    const qa_application_client_source *source,qa_error *error)
{
    if(!owner || !parent(owner) || !owner->importing || owner->restore_finished ||
        !frontend_network_q2_client_configuration_primary(owner,source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 import abort lost its actual physical Source association");
    return frontend_remote_q2_source_restore_abort_ready(owner->source,source,error);
}

static bool demo_current(const void *context)
{
    const frontend_network_q2_client *owner=context;
    return owner && owner->demo_held && parent(owner) && !owner->calls && !owner->importing;
}
static bool demo_packet(void *context,qa_bytes bytes,bool full_frame,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!owner->demo_sink.append || !parent(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 accepted packet lost its recording sink");
    if(owner->demo_waiting) {
        if(!full_frame) return true;
        owner->demo_waiting=false;
    }
    frontend_demo_packet packet={.format=FRONTEND_DEMO_Q2,.value.message=bytes};
    qa_error write_error={0};
    (void)owner->demo_sink.append(owner->demo_sink.owner,&packet,&write_error);
    return true;
}
static bool demo_seed(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    frontend_network_q2_client *owner=context; frontend_remote_q2_source_view view;
    return demo_current(owner) && frontend_remote_q2_source_read(owner->source,&view,error) &&
        frontend_remote_q2_demo_record_seed(view.receiver,sink,error);
}
static bool demo_attach(void *context,const frontend_demo_sink *sink,bool *attached,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!attached || !sink || !sink->append || !demo_current(owner) || owner->demo_sink.append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recording sink is already held or lost its source");
    *attached=false;
    qa_q2_packet_sink feed={owner,demo_packet};
    if(!qa_network_q2_client_record(owner->options.runtime,owner->domain.client,&feed,true,error)) return false;
    owner->demo_sink=*sink; owner->demo_waiting=true; *attached=true;
    return qa_network_q2_client_request_full_frame(owner->options.runtime,owner->domain.client,error);
}
static bool demo_detach(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    if(!owner || !sink || owner->calls || owner->demo_sink.owner!=sink->owner || owner->demo_sink.append!=sink->append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recording returned a different sink");
    qa_q2_packet_sink feed={owner,demo_packet};
    if(qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client) &&
        !qa_network_q2_client_record(owner->options.runtime,owner->domain.client,&feed,false,error)) return false;
    owner->demo_sink=(frontend_demo_sink){0}; owner->demo_waiting=false;
    return true;
}
static qa_vec3 mvd_player_origin(const frontend_network_q2_client *owner, const qa_q2_player *player)
{
    return owner->mvd_header.profile.rerelease ?
        qa_v3(player->pmove.origin_f[0],player->pmove.origin_f[1],player->pmove.origin_f[2]) :
        qa_v3((float)player->pmove.origin[0]/8,(float)player->pmove.origin[1]/8,(float)player->pmove.origin[2]/8);
}
static void mvd_spectator_prepare(frontend_network_q2_client *owner, const qa_bsp_view *bsp)
{
    owner->mvd_spectator=(qa_q2_player){.clientnum=owner->mvd_header.dummy,.fov=90,
        .pmove={.type=owner->mvd_header.profile.rerelease?3:1}};
    qa_entities entities={0};
    qa_error error={0};
    if (!qa_entities_parse(bsp->lumps[QA_BSP_ENTITIES].bytes,
        owner->mvd_header.profile.rerelease?QA_ENTITY_Q2_RERELEASE:QA_ENTITY_Q2_CLASSIC,&entities,&error)) {
        qa_application_feature_report(owner->options.frontend->application,"MVD spectator spawn",&error);
        qa_entities_free(&entities);
        return;
    }
    static const char *const classes[]={"info_player_intermission","info_player_start","info_player_deathmatch"};
    for (size_t i=0;i<entities.count;++i) {
        qa_bytes name;
        if (!qa_entity_value(&entities,i,"classname",&name)) continue;
        size_t kind=0;
        while (kind<3 && !qa_text_bytes_equal(name,
            (qa_bytes){(const uint8_t *)classes[kind],strlen(classes[kind])},false)) ++kind;
        if (kind==3) continue;
        qa_vec3 origin={0}, angles={0}; qa_bytes value;
        if (qa_entity_value(&entities,i,"origin",&value)) (void)qa_bot_bsp_parse_vector(value,&origin,NULL);
        if (qa_entity_value(&entities,i,"angles",&value)) (void)qa_bot_bsp_parse_vector(value,&angles,NULL);
        if (qa_entity_value(&entities,i,"angle",&value)) (void)qa_bot_bsp_parse_float(value,&angles.y,NULL);
        owner->mvd_spectator.pmove.origin[0]=qa_source_to_i32(origin.x*8);
        owner->mvd_spectator.pmove.origin[1]=qa_source_to_i32(origin.y*8);
        owner->mvd_spectator.pmove.origin[2]=qa_source_to_i32(origin.z*8);
        owner->mvd_spectator.pmove.origin_f[0]=origin.x;
        owner->mvd_spectator.pmove.origin_f[1]=origin.y;
        owner->mvd_spectator.pmove.origin_f[2]=origin.z;
        owner->mvd_spectator.viewangles[0]=angles.x;
        owner->mvd_spectator.viewangles[1]=angles.y;
        if (kind==0) break;
    }
    qa_entities_free(&entities);
}
static bool mvd_geometry_prepare(frontend_network_q2_client *owner, qa_error *error)
{
    frontend_remote_q2_source_view source; frontend_remote_q2_view view;
    if (!frontend_remote_q2_source_read(owner->source,&source,error) ||
        !frontend_remote_q2_metadata_read(source.receiver,&view,error)) return false;
    owner->mvd_receiver=source.receiver;
    if (view.geometry==owner->mvd_geometry) return true;
    qa_unified_frame_lease *storage=qa_unified_frame_lease_acquire(owner->mvd_pool,error);
    if (!storage) return false;
    size_t count=qa_collision_cluster_count(view.geometry);
    int32_t *clusters=qa_unified_frame_lease_alloc(storage,count,sizeof(*clusters),_Alignof(int32_t),error);
    uint32_t *marks=qa_unified_frame_lease_alloc(storage,count,sizeof(*marks),_Alignof(uint32_t),error);
    qa_trace_scratch *scratch=NULL;
    if ((count && (!clusters || !marks)) || !qa_trace_scratch_create(view.geometry,&scratch,error) ||
        !qa_collision_retain(view.geometry,error)) {
        qa_trace_scratch_destroy(scratch); qa_unified_frame_lease_release(storage); return false;
    }
    qa_trace_scratch_destroy(owner->mvd_trace);
    qa_collision_destroy(owner->mvd_geometry);
    qa_unified_frame_lease_release(owner->mvd_geometry_storage);
    owner->mvd_geometry=view.geometry; owner->mvd_trace=scratch;
    owner->mvd_geometry_storage=storage; owner->mvd_clusters=clusters;
    qa_stamp_set_init(&owner->mvd_cluster_seen,marks,count);
    mvd_spectator_prepare(owner,qa_collision_bsp(view.geometry));
    return true;
}
static qa_leaf_visit mvd_view_cluster(void *context,const qa_collision_leaf *leaf,qa_error *error)
{
    (void)error;
    frontend_network_q2_client *owner=context;
    if (leaf->cluster>=0 && qa_stamp_set_mark(&owner->mvd_cluster_seen,(size_t)leaf->cluster))
        owner->mvd_clusters[owner->mvd_cluster_count++]=(int32_t)leaf->cluster;
    return QA_LEAF_CONTINUE;
}
static bool mvd_area_bits(void *context,const qa_q2_player *player,qa_bytes portals,
    qa_arena *storage,qa_bytes *out,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    const qa_bsp_view *bsp=qa_collision_bsp(owner->mvd_geometry);
    size_t count=qa_bsp_record_count(bsp,QA_BSP_AREA_PORTALS);
    for (size_t i=0;i<count;++i) {
        qa_bsp_area_portal portal;
        if (!qa_bsp_read_area_portal(bsp,i,&portal,error)) return false;
        bool open=portal.portal/8>=portals.size ||
            (portals.data[portal.portal/8]&(1u<<(portal.portal%8)))!=0;
        if (!qa_collision_set_portal(owner->mvd_geometry,portal.portal,open,error)) return false;
    }
    qa_vec3 origin=mvd_player_origin(owner,player);
    origin=qa_vec_add(origin,qa_v3(player->viewoffset[0],player->viewoffset[1],player->viewoffset[2]));
    if (owner->mvd_header.profile.rerelease) origin.z+=(float)player->pmove.viewheight;
    owner->mvd_cluster_count=0;
    qa_stamp_set_begin(&owner->mvd_cluster_seen);
    qa_leaf_list leaves;
    if (!qa_collision_point_leaf(owner->mvd_geometry,origin,QA_LEAF_COLLISION,&owner->mvd_view_leaf,error) ||
        !qa_collision_walk_leaves(owner->mvd_geometry,owner->mvd_trace,
            (qa_bounds){qa_vec_sub(origin,qa_v3(8,8,8)),qa_vec_add(origin,qa_v3(8,8,8))},
            QA_LEAF_COLLISION,mvd_view_cluster,owner,&leaves,error)) return false;
    size_t capacity=((size_t)qa_collision_area_count(owner->mvd_geometry)+7)/8;
    if (!capacity) capacity=1;
    uint8_t *bits=qa_arena_alloc(storage,capacity,1,error);
    if (!bits) return false;
    size_t written=0;
    if (!qa_collision_area_bits(owner->mvd_geometry,(int32_t)owner->mvd_view_leaf.area,
        bits,capacity,&written,error)) return false;
    if (!written) { bits[0]=255; written=1; }
    *out=(qa_bytes){bits,written}; return true;
}
typedef struct mvd_entity_visibility {
    frontend_network_q2_client *owner;
    bool phs, visible;
} mvd_entity_visibility;
static qa_leaf_visit mvd_entity_leaf(void *context,const qa_collision_leaf *leaf,qa_error *error)
{
    mvd_entity_visibility *query=context; frontend_network_q2_client *owner=query->owner;
    if (leaf->cluster<0) return QA_LEAF_CONTINUE;
    bool connected;
    if (!qa_collision_areas_connected(owner->mvd_geometry,(int32_t)owner->mvd_view_leaf.area,
            (int32_t)leaf->area,&connected,error)) return QA_LEAF_FAILED;
    if (!connected) return QA_LEAF_CONTINUE;
    size_t count=query->phs?1:owner->mvd_cluster_count;
    for (size_t i=0;i<count;++i) {
        int32_t cluster=query->phs?(int32_t)owner->mvd_view_leaf.cluster:owner->mvd_clusters[i];
        if (cluster<0) continue;
        if (!qa_collision_cluster_visible(owner->mvd_geometry,cluster,(int32_t)leaf->cluster,
            query->phs,&query->visible,error)) return QA_LEAF_FAILED;
        if (query->visible) return QA_LEAF_STOP;
    }
    return QA_LEAF_CONTINUE;
}
static bool mvd_entity_visible(void *context,const qa_q2_entity *entity,const qa_q2_player *player,
    qa_bytes portals,bool *out,qa_error *error)
{
    (void)portals;
    frontend_network_q2_client *owner=context;
    if (entity->number==(uint32_t)player->clientnum+1) { *out=true; return true; }
    qa_bounds bounds; bool brush;
    if (!frontend_remote_q2_entity_bounds(owner->mvd_receiver,entity,&bounds,&brush,error)) return false;
    qa_vec3 origin=qa_v3(entity->origin[0],entity->origin[1],entity->origin[2]);
    bounds=qa_collision_link_bounds(bounds,origin,qa_v3(entity->angles[0],entity->angles[1],entity->angles[2]),
        brush && entity->solid==31,qa_v3(1,1,1),owner->mvd_header.profile.rerelease?
            QA_RULESET_Q2_RERELEASE:QA_RULESET_Q2_CLASSIC);
    mvd_entity_visibility query={.owner=owner,.phs=(entity->renderfx&128u)!=0 || entity->sound!=0};
    qa_leaf_list leaves;
    bool okay=qa_collision_walk_leaves(owner->mvd_geometry,owner->mvd_trace,bounds,
        QA_LEAF_COLLISION,mvd_entity_leaf,&query,&leaves,error);
    *out=query.visible; return okay;
}
static bool mvd_route_visible(void *context,uint16_t number,bool phs,const qa_q2_player *player,
    qa_bytes portals,bool *out,qa_error *error)
{
    (void)portals;
    frontend_network_q2_client *owner=context;
    qa_collision_leaf from,to;
    if (!qa_collision_leaf_at(owner->mvd_geometry,number,&from,error) ||
        !qa_collision_point_leaf(owner->mvd_geometry,mvd_player_origin(owner,player),QA_LEAF_COLLISION,&to,error)) return false;
    *out=false;
    if (from.cluster<0 || to.cluster<0) return true;
    bool connected;
    if (!qa_collision_areas_connected(owner->mvd_geometry,(int32_t)from.area,(int32_t)to.area,&connected,error)) return false;
    return !connected || qa_collision_cluster_visible(owner->mvd_geometry,(int32_t)from.cluster,
        (int32_t)to.cluster,phs,out,error);
}
static bool mvd_sound_origin(void *context,const qa_q2_entity *entity,float out[3],qa_error *error)
{
    frontend_network_q2_client *owner=context;
    qa_vec3 origin=qa_v3(entity->origin[0],entity->origin[1],entity->origin[2]);
    qa_bounds bounds; bool brush;
    if (!frontend_remote_q2_entity_bounds(owner->mvd_receiver,entity,&bounds,&brush,error)) return false;
    if (brush && entity->solid==31)
        origin=qa_vec_add(origin,qa_vec_scale(qa_vec_add(bounds.mins,bounds.maxs),.5f));
    out[0]=origin.x; out[1]=origin.y; out[2]=origin.z; return true;
}
static bool mvd_sound_audible(void *context,const float source[3],const qa_q2_player *player,
    qa_bytes portals,bool *out,qa_error *error)
{
    (void)portals;
    frontend_network_q2_client *owner=context;
    qa_collision_leaf from,to;
    if (!qa_collision_point_leaf(owner->mvd_geometry,qa_v3(source[0],source[1],source[2]),QA_LEAF_COLLISION,&from,error) ||
        !qa_collision_point_leaf(owner->mvd_geometry,mvd_player_origin(owner,player),QA_LEAF_COLLISION,&to,error)) return false;
    *out=false;
    if (from.cluster<0 || to.cluster<0) return true;
    bool connected;
    if (!qa_collision_areas_connected(owner->mvd_geometry,(int32_t)from.area,(int32_t)to.area,&connected,error)) return false;
    return !connected || qa_collision_cluster_visible(owner->mvd_geometry,(int32_t)from.cluster,
        (int32_t)to.cluster,true,out,error);
}
static qa_q2_mvd_projection mvd_projection(frontend_network_q2_client *owner)
{
    double fov=90; bool explicit_override;
    (void)frontend_view_settings_read(owner->options.frontend->view_settings,&fov,&explicit_override);
    owner->mvd_spectator.fov=(float)(fov<1?90:fmin(fov,160));
    return (qa_q2_mvd_projection){.context=owner,.spectator=&owner->mvd_spectator,
        .entity_visible=mvd_entity_visible,.area_bits=mvd_area_bits,
        .route_visible=mvd_route_visible,.sound_origin=mvd_sound_origin,.sound_audible=mvd_sound_audible};
}
static bool mvd_frame_decode(frontend_network_q2_client *owner,bool full,qa_error *error)
{
    qa_unified_frame_lease *storage=qa_unified_frame_lease_acquire(owner->mvd_pool,error);
    qa_unified_frame_lease *scratch=qa_unified_frame_lease_acquire(owner->mvd_pool,error);
    if (!storage || !scratch) {
        qa_unified_frame_lease_release(storage); qa_unified_frame_lease_release(scratch); return false;
    }
    qa_q2_mvd_frame decoded;
    bool okay=qa_q2_mvd_read_frame_arena(&owner->mvd_header.profile,&owner->mvd_cursor,
        full?NULL:&owner->mvd_frame,owner->mvd_header.max_clients,owner->mvd_header.dummy,
        qa_unified_frame_lease_arena(scratch),qa_unified_frame_lease_arena(storage),&decoded);
    qa_unified_frame_lease_release(scratch);
    if (!okay) { qa_unified_frame_lease_release(storage); return false; }
    qa_unified_frame_lease_release(owner->mvd_frame_storage);
    owner->mvd_frame_storage=storage; owner->mvd_frame=decoded;
    owner->mvd_frame_pending=true;
    uint32_t ordinal=(uint32_t)owner->mvd_ordinal+1; memcpy(&owner->mvd_ordinal,&ordinal,sizeof(ordinal));
    return true;
}
static bool mvd_header_decode(frontend_network_q2_client *owner,qa_q2_mvd_command command,qa_error *error)
{
    qa_unified_frame_lease *storage=qa_unified_frame_lease_acquire(owner->mvd_pool,error);
    if (!storage) return false;
    qa_q2_mvd_header decoded;
    if (!qa_q2_mvd_read_header_arena(&owner->mvd_cursor,command,qa_unified_frame_lease_arena(storage),&decoded)) {
        qa_unified_frame_lease_release(storage); return false;
    }
    qa_unified_frame_lease_release(owner->mvd_header_storage);
    owner->mvd_header_storage=storage; owner->mvd_header=decoded; owner->mvd_ordinal=0;
    if (!mvd_frame_decode(owner,true,error)) return false;
    owner->mvd_header_pending=true; owner->demo_clock_started=false;
    return true;
}
static bool mvd_project(void *context,qa_q2_messages *messages,qa_unified_frame_lease *storage,
    qa_q2_server_emit_fn emit,void *user,qa_error *error)
{
    frontend_network_q2_client *owner=context;
    const qa_q2_mvd_player *selected=qa_q2_mvd_player_select(&owner->mvd_frame,owner->mvd_viewpoint);
    uint8_t viewpoint=selected?selected->number:
        owner->mvd_viewpoint<owner->mvd_header.max_clients?owner->mvd_viewpoint:0;
    if (owner->mvd_header_pending) {
        bool okay=qa_q2_mvd_project_header(&owner->mvd_header,viewpoint,messages,emit,user,error);
        if (okay) owner->mvd_header_pending=false;
        return okay;
    }
    if ((owner->mvd_frame_pending || owner->mvd_viewpoint_changed) && !mvd_geometry_prepare(owner,error))
        return false;
    qa_q2_mvd_projection projection=mvd_projection(owner);
    if (owner->mvd_frame_pending || owner->mvd_viewpoint_changed) {
        owner->mvd_spectator.clientnum=owner->mvd_header.dummy;
        frontend_remote_q2_demo_viewpoint(owner->mvd_receiver,selected?selected->number:owner->mvd_header.dummy);
        bool okay=qa_q2_mvd_project_frame(&owner->mvd_header,&owner->mvd_frame,viewpoint,
            owner->mvd_ordinal,&projection,storage,messages,emit,user,error);
        if (okay) owner->mvd_frame_pending=owner->mvd_viewpoint_changed=false;
        return okay;
    }
    qa_q2_mvd_command command;
    if (!qa_q2_mvd_read_command(&owner->mvd_cursor,&command)) return false;
    if (command.opcode==QA_MVD_SERVERDATA) {
        if (!mvd_header_decode(owner,command,error)) return false;
        return mvd_project(owner,messages,storage,emit,user,error);
    }
    if (command.opcode==QA_MVD_FRAME || command.opcode==QA_MVD_FRAME_NODELTA) {
        if (!mvd_frame_decode(owner,command.opcode==QA_MVD_FRAME_NODELTA,error)) return false;
        return mvd_project(owner,messages,storage,emit,user,error);
    }
    qa_q2_mvd_record record;
    return qa_q2_mvd_read_record(&owner->mvd_header.profile,&owner->mvd_cursor,command,&record) &&
        qa_q2_mvd_project_record(&owner->mvd_header,&owner->mvd_frame,viewpoint,&record,
            &projection,messages,emit,user,error);
}
void frontend_network_q2_client_demo_viewpoint(frontend_network_q2_client *owner,uint8_t viewpoint)
{
    if (owner && owner->options.demo_format==FRONTEND_DEMO_MVD && owner->mvd_viewpoint!=viewpoint) {
        owner->mvd_viewpoint=viewpoint; owner->mvd_viewpoint_changed=owner->mvd_frame_storage!=NULL;
    }
}
static bool demo_release(void **context,qa_error *error)
{
    frontend_network_q2_client *owner=context?*context:NULL;
    if(!owner) return true;
    if(owner->calls || !owner->demo_held || owner->demo_sink.append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 demo still owns entered or attached work");
    if(owner->options.demo && owner->domain.client.owner &&
        qa_net_connections_get(qa_network_connections(owner->options.runtime),owner->domain.client) &&
        !qa_network_detach(owner->options.runtime,owner->domain.client,"Q2 demo closed",error)) return false;
    if(!frontend_remote_q2_source_owner_release(owner->source,error)) return false;
    owner->demo_held=false;
    if(owner->options.demo) owner->retired=true;
    *context=NULL;
    return true;
}
bool frontend_network_q2_client_demo_record(frontend_network_q2_client *owner,
    frontend_demo_record_source *out,qa_error *error)
{
    frontend_remote_q2_source_view source; frontend_remote_q2_view view;
    if(!out || !parent(owner) || owner->calls || owner->demo_held || owner->options.demo || owner->retired ||
        !frontend_remote_q2_source_read(owner->source,&source,error) ||
        !frontend_remote_q2_read(source.receiver,&view,error) || !view.media_ready || !view.frame->valid ||
        !view.content.selected_write_root)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 recording requires its actual active CLIENT and writable content");
    if(!frontend_remote_q2_source_owner_retain(owner->source,error)) return false;
    owner->demo_held=true;
    *out=(frontend_demo_record_source){.owner=owner,.format=FRONTEND_DEMO_Q2,.protocol=view.domain.protocol,
        .root=view.content.selected_write_root,.current=demo_current,.seed=demo_seed,.attach=demo_attach,
        .detach=demo_detach,.release=demo_release};
    return true;
}
static bool demo_advance(void *context,frontend_demo_reader *reader,uint64_t elapsed,uint64_t time_ns,uint64_t frame,
    bool timedemo,frontend_demo_end *end,qa_error *error)
{
    (void)time_ns;(void)frame;
    frontend_network_q2_client *owner=context;
    if(!reader || !end || !demo_current(owner)) return false;
    *end=FRONTEND_DEMO_RUNNING;
    if(owner->retired) { *end=FRONTEND_DEMO_DISCONNECTED; return true; }
    bool ready=false;
    if(!frontend_remote_q2_source_advance(owner->source,&ready,error)) return false;
    if(!ready) return true;
    if(!owner->domain.client.owner) {
        qa_q2_connect_request request={.protocol=owner->options.protocol,.payload_bytes=65527};
        qa_q2_client_admission admission; qa_q2_preparation preparation;
        if(!prepare(owner,&owner->options.remote,&request,NULL,0,&admission,&preparation,error)) return false;
        qa_net_client_id id;
        if(preparation!=QA_Q2_PREPARATION_READY ||
            !qa_network_attach_q2_client(owner->options.runtime,&admission.connection,&admission.policy,
                &admission.hooks,owner->options.frontend->wall_time_ns,&id,error)) return false;
        if(!committed(owner,&admission,id,error)) {
            qa_network_detach(owner->options.runtime,id,"Q2 demo admission failed",NULL); return false;
        }
    }
    frontend_remote_q2_source_view source;
    if(!frontend_remote_q2_source_read(owner->source,&source,error)) return false;
    if(owner->demo_clock_started) {
        frontend_remote_q2_view view;
        if(!frontend_remote_q2_metadata_read(source.receiver,&view,error)) return false;
        double step=view.server_data->server_fps?1000.0/(double)view.server_data->server_fps:100.0;
        owner->demo_ms=timedemo?((double)view.frame->server_frame+1)*step:
            owner->demo_ms+(double)elapsed/1000000.0;
    }
    for(;;) {
        if(!qa_network_q2_client_continue(owner->options.runtime,owner->domain.client,error)) return false;
        if(owner->retired) { *end=FRONTEND_DEMO_DISCONNECTED; return true; }
        frontend_remote_q2_view view; qa_network_q2_state state;
        if(!frontend_remote_q2_metadata_read(source.receiver,&view,error) ||
            !qa_network_q2_state_read(owner->options.runtime,owner->domain.client,&state,error)) return false;
        if(state.receive_pending) return true;
        if(view.content_generation!=owner->demo_generation) {
            owner->demo_generation=view.content_generation; owner->demo_clock_started=false;
        }
        double step=view.server_data->server_fps?1000.0/(double)view.server_data->server_fps:100.0;
        bool mvd=owner->options.demo_format==FRONTEND_DEMO_MVD;
        if(view.frame->valid) {
            double server_ms=(double)view.frame->server_frame*step;
            if(!owner->demo_clock_started) {
                owner->demo_ms=server_ms; owner->demo_clock_started=true;
                if (!mvd) return frontend_remote_q2_demo_clock(source.receiver,owner->demo_ms,error);
            }
            if(owner->demo_ms<=server_ms && (!mvd ||
                (!owner->mvd_frame_pending && !owner->mvd_viewpoint_changed &&
                    (!owner->mvd_cursor_held || !qa_net_reader_remaining(&owner->mvd_cursor)))))
                return frontend_remote_q2_demo_clock(source.receiver,owner->demo_ms,error);
        }
        if (mvd) {
            if (owner->mvd_frame_pending && !owner->mvd_header_pending && !view.media_ready) return true;
            owner->mvd_cursor.error=error;
            if (!owner->mvd_frame_pending && !owner->mvd_header_pending && !owner->mvd_viewpoint_changed) {
                if (!owner->mvd_cursor_held || !qa_net_reader_remaining(&owner->mvd_cursor)) {
                    frontend_demo_packet packet; bool present=false;
                    if (!frontend_demo_read_next(reader,NULL,NULL,&packet,&present,end,error)) return false;
                    if (!present) return true;
                    if (packet.format!=FRONTEND_DEMO_MVD) return false;
                    qa_net_reader_init(&owner->mvd_cursor,packet.value.message,error);
                    owner->mvd_cursor_held=true;
                }
                qa_net_reader peek=owner->mvd_cursor; qa_q2_mvd_command next;
                if (!qa_q2_mvd_read_command(&peek,&next)) return false;
                if (view.frame->valid && owner->demo_clock_started &&
                    owner->demo_ms<=(double)view.frame->server_frame*100 &&
                    (next.opcode==QA_MVD_FRAME || next.opcode==QA_MVD_FRAME_NODELTA))
                    return frontend_remote_q2_demo_clock(source.receiver,owner->demo_ms,error);
            }
            bool projected=qa_network_q2_client_demo_project(owner->options.runtime,owner->domain.client,
                mvd_project,owner,owner->options.frontend->wall_time_ns,error);
            owner->mvd_cursor.error=NULL;
            if (!projected) return false;
            continue;
        }
        frontend_demo_packet packet; bool present=false;
        if(!frontend_demo_read_next(reader,NULL,NULL,&packet,&present,end,error)) return false;
        if(!present) return true;
        if(packet.format!=FRONTEND_DEMO_Q2 ||
            !qa_network_q2_client_demo_receive(owner->options.runtime,owner->domain.client,
                packet.value.message,owner->options.frontend->wall_time_ns,error)) return false;
    }
}
bool frontend_network_q2_client_demo_playback(frontend_network_q2_client *owner,
    frontend_demo_playback_source *out,qa_error *error)
{
    if(!out || !parent(owner) || owner->calls || !owner->options.demo || owner->demo_held || owner->retired)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 playback requires its actual pending demo CLIENT");
    if(!frontend_remote_q2_source_owner_retain(owner->source,error)) return false;
    owner->demo_held=true;
    *out=(frontend_demo_playback_source){owner,demo_current,demo_advance,demo_release};
    return true;
}

bool frontend_network_q2_demo_protocol(const frontend_demo_reader *reader,
    qa_net_protocol_id *out,qa_error *error)
{
    qa_bytes message;
    qa_bytes bytes=frontend_demo_reader_bytes(reader);
    if (out && bytes.size>=4 && qa_load_u32le(bytes.data)==QA_Q2_MVD_MAGIC) {
        qa_net_reader file; qa_net_reader_init(&file,bytes,error);
        (void)qa_net_read_u32(&file);
        uint16_t length=qa_net_read_u16(&file);
        if (!length || length>QA_Q2_MVD_MESSAGE_BYTES || !qa_net_read_bytes(&file,length,&message))
            return qa_net_reader_fail(&file,"MVD demo has no initial protocol message");
        qa_net_reader wire; qa_net_reader_init(&wire,message,error);
        qa_q2_mvd_command command;
        if (!qa_q2_mvd_read_command(&wire,&command)) return false;
        qa_arena storage={0}; qa_q2_mvd_header header;
        bool okay=qa_q2_mvd_read_header_arena(&wire,command,&storage,&header);
        if (okay) *out=header.profile.protocol;
        qa_arena_destroy(&storage); return okay;
    }
    if(!out || !frontend_demo_peek_message(reader,&message,error)) return false;
    qa_net_reader wire; qa_net_reader_init(&wire,message,error);
    if(qa_net_read_u8(&wire)!=12)
        return qa_net_reader_fail(&wire,"Q2 demo must begin with genuine serverdata");
    uint32_t version=qa_net_read_u32(&wire);
    return !wire.failed && qa_q2_protocol_from_version(version==26?34:version,0,out,error);
}
