#include "network_q1_client.h"
#include "internal.h"
#include "network_q1_skin_commands.h"
#include "qa/application_network.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_network_q1_client {
    frontend_network_q1_client_options options;
    frontend_client_source *physical;
    frontend_remote_q1_source *source;
    qa_nq_connect_client *nq;
    qa_qw_connect_client *qw;
    frontend_remote_q1_skin_bindings skins;
    qa_vfs *skin_files, *content_files;
    qa_catalog *content_catalog;
    qa_product_id content_product;
    qa_net_seat_binding binding;
    qa_net_connect attachment;
    qa_net_client_id client;
    uint64_t epoch, now_ns;
    unsigned calls;
    bool configured, admitting, retired, closing;
    char *pending_allskins;
    char reason[1024];
};
static bool same_protocol(qa_net_protocol_id a,qa_net_protocol_id b)
{ return a.kind==b.kind && a.revision==b.revision && a.flags==b.flags; }
static bool parent(const frontend_network_q1_client *o)
{ return o && !o->closing && o->options.current(o->options.context,o); }
static bool connection(void *context,const qa_application_client_source *source)
{
    frontend_network_q1_client *o=context;
    if(!parent(o) || source->runtime!=o->options.runtime || source->context.physical_seat!=o->options.physical_seat)
        return false;
    if(!source->client.owner) return !o->client.owner && !source->client.generation && !source->client.slot &&
        !source->connection_epoch && !source->network_seat.owner && !source->network_seat.index;
    const qa_net_client *client=qa_net_connections_get(qa_network_connections(o->options.runtime),source->client);
    return client && qa_net_client_id_equal(source->client,o->client) && source->connection_epoch==o->epoch &&
        qa_network_epoch(o->options.runtime,o->client)==o->epoch && source->network_seat.owner==o->binding.seat.owner &&
        source->network_seat.index==o->binding.seat.index && client->seat_count==1 &&
        qa_net_client_owns_seat(client,source->network_seat) && same_protocol(client->protocol,o->options.protocol);
}
static bool entity(void *context,const qa_application_client_source *source,uint32_t number,uint64_t *generation)
{ frontend_network_q1_client *o=context; return connection(o,source) && o->source &&
    frontend_remote_q1_source_entity_current(o->source,number,generation); }
static void print(void *context,const qa_command_context *command,const char *text)
{ frontend_network_q1_client *o=context; frontend_console_print(o->options.frontend,command,text); }
static bool initialize(void *context,const qa_launch_instance *descriptor,qa_cvars *variables,
    const qa_command_context *command,qa_error *error)
{
    frontend_network_q1_client *o=context;
    return parent(o) && frontend_remote_q1_source_defaults(variables,command->owner,command->seat,error) &&
        (!o->options.configuration.initialize || o->options.configuration.initialize(
            o->options.configuration.context,descriptor,variables,command,error));
}
static bool configure(void *context,const qa_application_client_source *source,bool *ready,qa_error *error)
{ frontend_network_q1_client *o=context; return parent(o) && o->options.configuration.configure(
    o->options.configuration.context,source,ready,error) && parent(o); }
static qa_command_result forward(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(!parent(o)) return QA_COMMAND_FAILED;
    if(o->options.configuration.forward) return o->options.configuration.forward(
        o->options.configuration.context,command,error);
    if(!o->source) return QA_COMMAND_UNHANDLED;
    frontend_remote_q1_source_view view;
    if(!frontend_remote_q1_source_read(o->source,&view,error)) return QA_COMMAND_FAILED;
    if(!view.domain.client.owner) return QA_COMMAND_UNHANDLED;
    return qa_network_q1_client_command(o->options.runtime,o->client,command->raw,error)?
        QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
}
static qa_command_result command(void *context,const qa_command_invocation *invocation,qa_error *error)
{ frontend_network_q1_client *o=context; return o->options.configuration.command?
    o->options.configuration.command(o->options.configuration.context,invocation,error):QA_COMMAND_UNHANDLED; }
static bool allow(void *context,const qa_command_invocation *invocation)
{ frontend_network_q1_client *o=context; return !o->options.configuration.allow_command ||
    o->options.configuration.allow_command(o->options.configuration.context,invocation); }
static qa_cvars *cvar_owner(void *context,const qa_command_context *origin,const char *name)
{ frontend_network_q1_client *o=context; return o->options.configuration.cvar_owner(
    o->options.configuration.context,origin,name); }
static qa_cvars *visible(void *context,const qa_command_context *origin,size_t index)
{ frontend_network_q1_client *o=context; return o->options.configuration.visible_cvars(
    o->options.configuration.context,origin,index); }
static bool edit(void *context,const qa_command_context *origin,qa_cvars *variables,
    struct qa_cvars_edit **out,qa_error *error)
{ frontend_network_q1_client *o=context; return o->options.configuration.cvar_edit(
    o->options.configuration.context,origin,variables,out,error); }
static bool script(void *context,const qa_command_context *origin,const char *path,
    qa_bytes *out,void **lease,qa_error *error)
{ frontend_network_q1_client *o=context; return o->options.configuration.read_script(
    o->options.configuration.context,origin,path,out,lease,error); }
static void script_release(void *context,void *lease)
{ frontend_network_q1_client *o=context; o->options.configuration.release_script(o->options.configuration.context,lease); }
static void script_complete(void *context,const qa_command_context *origin,const char *path,bool success)
{ frontend_network_q1_client *o=context; o->options.configuration.script_complete(
    o->options.configuration.context,origin,path,success); }
static bool skin_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(!parent(o) || !call || !call->argc || !call->argv) return false;
    if(!o->client.owner) {
        /* The actual startup command can select allskins before a transport
         * exists. Its private value moves into the real child after bind. */
        if(!strcmp(call->argv[0],"allskins")) {
            const char *value=call->argc>1?call->argv[1]:"";
            if(!value) return false;
            size_t size=strlen(value)+1; char *copy=malloc(size);
            if(!copy) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining startup allskins command");
            memcpy(copy,value,size); free(o->pending_allskins); o->pending_allskins=copy;
        }
        return true;
    }
    frontend_remote_q1_source_view view;
    return frontend_remote_q1_source_read(o->source,&view,error) &&
        frontend_network_q1_skin_command(view.receiver,call,error)==QA_COMMAND_HANDLED;
}
static bool install(void *context,const qa_application_client_source *source,bool restoring,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(o->options.configuration.install && !o->options.configuration.install(
        o->options.configuration.context,source,restoring,error)) return false;
    if(!qa_q1_is_qw(o->options.protocol)) return true;
    static const char *const names[]={"skins","allskins","stopdownload","retrydownload"};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i)
        if(!qa_console_register(source->context.console,names[i],"",source->context.command.owner,
            false,skin_command,o,error)) return false;
    return true;
}
static frontend_client_source_options physical_options(frontend_network_q1_client *o)
{
    frontend_client_source_options c=o->options.configuration;
    c.context=o; c.runtime=o->options.runtime; c.physical_seat=o->options.physical_seat;
    c.initialize=initialize; c.configure=configure; c.install=install; c.print=print;
    c.connection_current=connection; c.entity_current=entity; c.command=command; c.forward=forward;
    c.allow_command=allow;
    if(c.cvar_owner) c.cvar_owner=cvar_owner;
    if(c.visible_cvars) c.visible_cvars=visible;
    if(c.cvar_edit) c.cvar_edit=edit;
    if(c.read_script) { c.read_script=script; c.release_script=script_release; }
    if(c.script_complete) c.script_complete=script_complete;
    return c;
}
static bool skin_current(void *context,const frontend_remote_q1_domain *expected,qa_error *error)
{
    frontend_network_q1_client *o=context; frontend_remote_q1_source_view view;
    if(!parent(o)) return false;
    if(!o->source) return !expected->client.owner && !o->client.owner;
    return frontend_remote_q1_source_read(o->source,&view,error) &&
        view.domain.catalog==expected->catalog && view.domain.product==expected->product &&
        qa_net_client_id_equal(view.domain.client,expected->client) && view.domain.epoch==expected->epoch &&
        view.domain.actor_owner==expected->actor_owner && view.domain.cvars==expected->cvars;
}
static bool download_permission(void *context,bool *allowed,bool *demo,qa_error *error)
{ frontend_network_q1_client *o=context; return parent(o) && o->options.downloads(
    o->options.context,allowed,demo,error) && parent(o); }
static bool nonce(void *context,uint64_t *out,qa_error *error)
{ frontend_network_q1_client *o=context; return parent(o) && o->options.download_nonce(
    o->options.context,out,error) && *out && parent(o); }
static bool reliable(void *context,const char *text,qa_error *error)
{ frontend_network_q1_client *o=context; return parent(o) && o->client.owner &&
    qa_network_q1_client_command(o->options.runtime,o->client,text,error); }
static bool skin_print(void *context,const char *text,qa_error *error)
{
    frontend_network_q1_client *o=context; frontend_client_source_view view;
    if(!parent(o) || !frontend_client_source_read(o->physical,&view,error)) return false;
    print(o,&view.source.context.command,text); return parent(o);
}
static bool load_content(void *context,const qa_application_client_source *source,
    const qa_nq_serverinfo *nq,const qa_qw_serverdata *qw,frontend_remote_q1_content *out,qa_error *error)
{
    frontend_network_q1_client *o=context; (void)nq;
    if(!parent(o) || !source || !out) return false;
    qa_catalog *catalog=qa_launch_instance_catalog(source->descriptor),*fresh=NULL;
    uint64_t generation=qa_catalog_generation(o->content_catalog?o->content_catalog:catalog);
    if(generation==UINT64_MAX) return frontend_fail(error,QA_ERROR_FORMAT,"Q1 content generation exhausted");
    qa_product_id selected=0; qa_vfs *files=NULL;
    if(!qa_catalog_discover_remote_q1(catalog,o->options.profile,qw?qw->game_directory:"",generation+1,
        &fresh,&selected,error) || !qa_catalog_open(fresh,selected,&files,error)) {
        qa_catalog_release(fresh); return false;
    }
    if(!parent(o)) { qa_vfs_destroy(files); qa_catalog_release(fresh); return false; }
    qa_vfs_destroy(o->content_files); qa_catalog_release(o->content_catalog);
    o->content_files=files; o->content_catalog=fresh; o->content_product=selected;
    *out=(frontend_remote_q1_content){fresh,selected,files}; return true;
}
static bool received(void *context,const qa_application_client_source *source,qa_net_protocol_id protocol,
    const qa_nq_message *message,double seconds,uint64_t sequence,qa_error *error)
{ frontend_network_q1_client *o=context; return parent(o) && o->options.service(
    o->options.context,source,protocol,message,seconds,sequence,error) && parent(o); }
static bool disconnected(void *context,const qa_application_client_source *source,const char *reason,qa_error *error)
{
    frontend_network_q1_client *o=context; (void)error;
    if(!parent(o) || !source || !reason) return false;
    print(o,&source->context.command,reason); snprintf(o->reason,sizeof(o->reason),"%s",reason);
    o->retired=true; return true;
}
static frontend_remote_q1_source_options receiver_options(frontend_network_q1_client *o)
{ return (frontend_remote_q1_source_options){.physical=o->physical,.protocol=o->options.protocol,.context=o,
    .load_content=load_content,.service=received,.disconnected=disconnected,
    .skin_bindings=qa_q1_is_qw(o->options.protocol)?&o->skins:NULL}; }
static bool complete_configuration(frontend_network_q1_client *o,qa_error *error)
{
    bool ready=false;
    if(!frontend_client_source_advance(o->physical,&ready,error)) return false;
    if(!ready) return true;
    frontend_client_source_view physical;
    if(!frontend_client_source_read(o->physical,&physical,error)) return false;
    if(qa_q1_is_qw(o->options.protocol)) {
        qa_fs_root *root=NULL;
        if(!frontend_remote_q1_skin_recipe(qa_launch_instance_catalog(physical.source.descriptor),
            physical.source.descriptor->selection.product,&o->skin_files,&root,error)) return false;
        o->skins=(frontend_remote_q1_skin_bindings){.files=o->skin_files,.root=root,.maximum_bytes=1024u*1024u,
            .context=o,.current=skin_current,.permission=download_permission,.nonce=nonce,.reliable=reliable,.print=skin_print};
    }
    frontend_remote_q1_source_options receiver=receiver_options(o);
    if(!frontend_remote_q1_source_create(o->options.frontend,&receiver,&o->source,error)) return false;
    if(qa_q1_is_qw(o->options.protocol)) {
        qa_buffer info={0};
        bool ok=qa_cvars_info(physical.source.context.cvars,QA_CVAR_USERINFO,65501,&info,error) &&
            qa_qw_connect_create(o->options.qport,(const char *)info.data,&o->qw,error);
        qa_buffer_free(&info); if(!ok) return false;
    } else if(!qa_nq_connect_create(&o->nq,error)) return false;
    o->attachment=(qa_net_connect){.attachment=QA_NET_REMOTE,.endpoint=o->options.remote,
        .protocol=o->options.protocol,.seats=&o->binding,.seat_count=1,.composition=physical.source.descriptor->identity};
    o->configured=true; return true;
}
bool frontend_network_q1_client_create(const frontend_network_q1_client_options *options,
    frontend_network_q1_client **out,qa_error *error)
{
    if(!options || !out || *out || !options->frontend || !options->runtime || !options->current ||
        !options->configuration.configure || !options->service || !qa_q1_profile_valid(options->protocol,error) ||
        options->physical_seat>=options->frontend->options.seats || !options->frontend->seats ||
        !options->frontend->seats[options->physical_seat].input ||
        (qa_q1_is_qw(options->protocol) && (!options->downloads || !options->download_nonce))) return false;
    qa_catalog *catalog=qa_application_catalog(options->frontend->application);
    const qa_product *profile=qa_catalog_product(catalog,options->profile);
    if(!profile || profile->family!=QA_GAME_Q1 ||
        (profile->edition==QA_EDITION_QUAKEWORLD)!=qa_q1_is_qw(options->protocol))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 CLIENT requires its genuine selected execution profile");
    frontend_network_q1_client *o=calloc(1,sizeof(*o));
    if(!o) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Q1 CLIENT factory");
    *out=o; o->options=*options;
    o->binding=(qa_net_seat_binding){{QA_NETWORK_COMMAND_OWNER,options->physical_seat},0};
    qa_vfs *prepared=NULL;
    if(!qa_catalog_open(catalog,profile->id,&prepared,error)) return false;
    frontend_client_source_options source=physical_options(o);
    source.input_origin=qa_input_seat_context(options->frontend->seats[options->physical_seat].input);
    source.input_origin.owner=0; source.input_origin.actor=(qa_actor_id){0}; source.input_origin.client=0;
    source.input_origin.registry=source.input_origin.generation=0; source.input_origin.script=false;
    source.input_origin.dialect=qa_q1_is_qw(options->protocol)?QA_CONSOLE_QW:QA_CONSOLE_Q1;
    source.metadata=(qa_launch_client_metadata){.catalog=catalog,.profile=profile->id,.selected=profile->id,
        .prepared=prepared,.instance="remote-q1-client",.seat=source.input_origin.seat,
        .clock=qa_q1_is_qw(options->protocol)?QA_CLOCK_QUAKEWORLD:QA_CLOCK_NETQUAKE};
    bool ok=frontend_client_source_create(options->frontend,&source,&o->physical,error);
    qa_vfs_destroy(prepared); return ok;
}
static qa_q1_connect_state handshake(const frontend_network_q1_client *o)
{ return o->qw?qa_qw_connect_state(o->qw):qa_nq_connect_state(o->nq); }
bool frontend_network_q1_client_admit(frontend_network_q1_client *o,const qa_net_connect *request,
    bool *recognized,qa_error *error)
{
    if(!recognized) return false;
    *recognized=o && request && same_protocol(request->protocol,o->options.protocol);
    if(!*recognized) return true;
    return (parent(o) && o->admitting && request->attachment==QA_NET_REMOTE && request->seats &&
        request->seat_count==1 && request->seats[0].seat.owner==o->binding.seat.owner &&
        request->seats[0].seat.index==o->binding.seat.index && !request->seats[0].remote_index &&
        qa_net_address_equal(&request->endpoint,&o->attachment.endpoint,true) &&
        qa_sha256_equal(&request->composition,&o->attachment.composition)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 attach differs from its actual reached Source claim");
}
static bool attach(frontend_network_q1_client *o,qa_error *error)
{
    if(o->client.owner || handshake(o).phase!=QA_Q1_CONNECT_CONNECTED ||
        !qa_network_callbacks_idle(o->options.runtime)) return false;
    frontend_remote_q1_source_view view; qa_network_q1_client_hooks hooks;
    if(!frontend_remote_q1_source_read(o->source,&view,error) ||
        !frontend_remote_q1_source_hooks(o->source,&hooks,error)) return false;
    qa_network_q1_client_policy policy=o->options.policy;
    if(!qa_q1_is_qw(o->options.protocol)) {
        const qa_cvar_view *name=qa_cvars_find(view.domain.cvars,"name"),*color=qa_cvars_find(view.domain.cvars,"color");
        if(!name || !color) return frontend_fail(error,QA_ERROR_ARGUMENT,"NQ signon lacks its actual CLIENT identity");
        policy.nq_identity.name=name->value;
        double word=isfinite(color->number)?fmod(trunc((double)color->number),256.0):0;
        if(word<0) word+=256;
        policy.nq_identity.color=(uint8_t)word;
        o->attachment.endpoint.port=handshake(o).port;
    }
    policy.qport=o->options.qport;
    o->admitting=true;
    bool ok=qa_network_attach_q1_client(o->options.runtime,&o->attachment,&policy,&hooks,o->now_ns,&o->client,error);
    o->admitting=false;
    if(!ok) return false;
    o->epoch=qa_network_epoch(o->options.runtime,o->client);
    if(!o->epoch || !frontend_remote_q1_source_bind(o->source,o->client,o->binding.seat,o->epoch,error) ||
        !qa_network_q1_client_start(o->options.runtime,o->client,error)) return false;
    if(o->pending_allskins) {
        frontend_remote_q1_source_view bound; bool ready=false;
        if(!frontend_remote_q1_source_read(o->source,&bound,error) ||
            !frontend_remote_q1_skins_all(frontend_remote_q1_skins_owner(bound.receiver),o->pending_allskins,&ready,error)) return false;
        free(o->pending_allskins); o->pending_allskins=NULL;
    }
    return true;
}
bool frontend_network_q1_client_receive(frontend_network_q1_client *o,const qa_net_datagram *packet,
    bool *recognized,qa_error *error)
{
    if(!recognized || !packet || !parent(o)) return false;
    *recognized=false;
    if(!o->configured || o->client.owner || o->retired || packet->kind!=QA_NET_POLL_PACKET ||
        !qa_net_address_equal(&packet->from,&o->options.remote,true)) return true;
    if(o->qw) {
        qa_bytes text; qa_error issue={0};
        if(!qa_qw_oob_decode(packet->payload,&text,&issue) || !text.size ||
            (text.data[0]!='c' && text.data[0]!='j' && text.data[0]!='n')) return true;
    } else {
        qa_nq_control control; qa_error issue={0};
        if(!qa_nq_control_decode(packet->payload,&control,&issue) ||
            (control.kind!=QA_NQ_ACCEPT && control.kind!=QA_NQ_REJECT)) return true;
    }
    *recognized=true;
    bool ok=o->qw?qa_qw_connect_receive(o->qw,packet->payload,error):qa_nq_connect_receive(o->nq,packet->payload,error);
    if(ok && handshake(o).phase==QA_Q1_CONNECT_REJECTED) {
        o->retired=true; snprintf(o->reason,sizeof(o->reason),"%s",handshake(o).reason?handshake(o).reason:"");
        frontend_console_print(o->options.frontend,NULL,o->reason);
    }
    /* Runtime admission runs at tick's returned idle boundary. */
    return ok;
}
static bool tick(frontend_network_q1_client *o,uint64_t now,qa_error *error)
{
    o->now_ns=now;
    if(o->retired) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(o->options.runtime),o->client);
        return !client || qa_network_detach(o->options.runtime,client->id,o->reason,error);
    }
    if(!o->configured && !complete_configuration(o,error)) return false;
    if(!o->configured) return true;
    if(o->client.owner) {
        size_t executed=0;
        return qa_network_q1_client_continue(o->options.runtime,o->client,error) &&
            frontend_client_source_drain(o->physical,1024,&executed,error);
    }
    if(handshake(o).phase==QA_Q1_CONNECT_CONNECTED) return attach(o,error);
    uint8_t data[65535]; qa_net_writer writer; qa_net_writer_init(&writer,data,sizeof(data),error);
    bool present=false;
    bool ok=o->qw?qa_qw_connect_next(o->qw,now,&present,&writer,error):qa_nq_connect_next(o->nq,now,&present,&writer,error);
    if(ok && present) ok=qa_network_send_address(o->options.runtime,&o->options.remote,
        (qa_bytes){data,qa_net_writer_size(&writer)},error);
    if(ok && handshake(o).phase==QA_Q1_CONNECT_REJECTED) {
        o->retired=true; snprintf(o->reason,sizeof(o->reason),"%s",handshake(o).reason?handshake(o).reason:"");
        frontend_console_print(o->options.frontend,NULL,o->reason);
    }
    return ok;
}
bool frontend_network_q1_client_tick(frontend_network_q1_client *o,uint64_t now,qa_error *error)
{
    if(!parent(o) || o->calls || o->options.frontend->capture || o->options.frontend->resource_inventory ||
        o->options.frontend->source_restoring || !qa_network_callbacks_idle(o->options.runtime)) return false;
    ++o->calls; bool ok=tick(o,now,error); --o->calls; return ok;
}
void frontend_network_q1_client_disconnected(frontend_network_q1_client *o,qa_net_client_id client)
{ if(o && qa_net_client_id_equal(o->client,client)) o->retired=true; }
bool frontend_network_q1_client_idle(const frontend_network_q1_client *o)
{ return !o || (!o->calls && !o->admitting && (!o->physical || frontend_client_source_idle(o->physical)) &&
    (!o->source || frontend_remote_q1_source_idle(o->source))); }
bool frontend_network_q1_client_destroy(frontend_network_q1_client **owned,qa_error *error)
{
    frontend_network_q1_client *o=owned?*owned:NULL; if(!o) return true;
    if(!frontend_network_q1_client_idle(o) || !qa_network_callbacks_idle(o->options.runtime)) return false;
    if(o->client.owner && qa_net_connections_get(qa_network_connections(o->options.runtime),o->client) &&
        !qa_network_q1_client_disconnect(o->options.runtime,o->client,"Q1 CLIENT closed",error)) return false;
    if(!frontend_remote_q1_source_destroy(&o->source,error) || !frontend_client_source_destroy(&o->physical,error)) return false;
    o->closing=true; qa_nq_connect_destroy(o->nq); qa_qw_connect_destroy(o->qw);
    qa_vfs_destroy(o->skin_files); qa_vfs_destroy(o->content_files); qa_catalog_release(o->content_catalog);
    free(o->pending_allskins); free(o); *owned=NULL; return true;
}
bool frontend_network_q1_client_source_read(const frontend_network_q1_client *o,
    frontend_remote_q1_source_view *out,qa_error *error)
{ return parent(o) && o->source && frontend_remote_q1_source_read(o->source,out,error); }
bool frontend_network_q1_client_content_visit(const frontend_network_q1_client *o,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    if(!o || !visitor || !visitor->view || !visitor->catalog || !visitor->pool || !frontend_network_q1_client_idle(o)) return false;
    return (!o->skin_files || (visitor->pool(visitor->context,qa_vfs_resources(o->skin_files),error) &&
        visitor->view(visitor->context,o->skin_files,error))) &&
        (!o->content_catalog || visitor->catalog(visitor->context,o->content_catalog,error)) &&
        (!o->content_files || (visitor->pool(visitor->context,qa_vfs_resources(o->content_files),error) &&
        visitor->view(visitor->context,o->content_files,error)));
}
