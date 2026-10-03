#include "network_q1_client.h"
#include "internal.h"
#include "network_q1_skin_commands.h"
#include "save_private.h"
#include "neutral_config.h"
#include "qa/input_command_save.h"
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
    qa_input_command_builder input;
    uint64_t input_sequence, input_sample;
    unsigned calls;
    bool configured, admitting, retired, closing, importing, restore_finished, configuration_released, input_center;
    char *pending_allskins, *userinfo, *userinfo_pending, *signon_name, *spawn_parameters;
    char *declared_name, *declared_parameters;
    size_t userinfo_next;
    uint8_t signon_color;
    char reason[1024];
};
static bool retain_policy(frontend_network_q1_client *o,qa_error *error)
{
    const char *name=o->options.policy.nq_identity.name;
    const char *parameters=o->options.policy.nq_identity.spawn_parameters;
    o->options.policy.nq_identity.name=NULL;
    o->options.policy.nq_identity.spawn_parameters=NULL;
    if(name) {
        size_t size=strlen(name)+1;
        o->declared_name=malloc(size);
        if(!o->declared_name) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining declared NQ client name");
        memcpy(o->declared_name,name,size);
        o->options.policy.nq_identity.name=o->declared_name;
    }
    if(parameters) {
        size_t size=strlen(parameters)+1;
        o->declared_parameters=malloc(size);
        if(!o->declared_parameters) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining declared NQ spawn parameters");
        memcpy(o->declared_parameters,parameters,size);
        o->options.policy.nq_identity.spawn_parameters=o->declared_parameters;
    }
    return true;
}
static bool same_protocol(qa_net_protocol_id a,qa_net_protocol_id b)
{ return a.kind==b.kind && a.revision==b.revision && a.flags==b.flags; }
static bool named(const char *a,const char *b)
{
    while(*a && *b) {
        unsigned char left=(unsigned char)*a++,right=(unsigned char)*b++;
        if(left>='A' && left<='Z') left+=(unsigned char)('a'-'A');
        if(right>='A' && right<='Z') right+=(unsigned char)('a'-'A');
        if(left!=right) return false;
    }
    return *a==*b;
}
static bool parent(const frontend_network_q1_client *o)
{ return o && !o->closing && o->options.current(o->options.context,o); }
bool frontend_network_q1_client_owns_input(const frontend_network_q1_client *o,uint32_t physical)
{ return o && !o->closing && o->options.physical_seat==physical; }
bool frontend_network_q1_client_retired(const frontend_network_q1_client *o)
{ return o && o->retired; }
static bool connection(void *context,const qa_application_client_source *source)
{
    frontend_network_q1_client *o=context;
    if(!parent(o) || o->retired || source->runtime!=o->options.runtime || source->context.physical_seat!=o->options.physical_seat)
        return false;
    if(!source->client.owner) return !o->client.owner && !source->client.generation && !source->client.slot &&
        !source->connection_epoch && !source->network_seat.owner && !source->network_seat.index;
    const qa_net_client *client=qa_net_connections_get(qa_network_connections(o->options.runtime),source->client);
    return client && qa_net_client_id_equal(source->client,o->client) && source->connection_epoch==o->epoch &&
        qa_network_epoch(o->options.runtime,o->client)==o->epoch && source->network_seat.owner==o->binding.seat.owner &&
        source->network_seat.index==o->binding.seat.index && client->seat_count==1 &&
        qa_net_client_owns_seat(client,source->network_seat) && !client->seats[0].remote_index &&
        same_protocol(client->protocol,o->options.protocol) &&
        qa_net_address_equal(&client->endpoint,&o->attachment.endpoint,true) &&
        qa_sha256_equal(&client->composition,&o->attachment.composition);
}
static bool retirement(void *context,const qa_application_client_source *source)
{
    frontend_network_q1_client *o=context;
    frontend_client_source_view held;
    if (!o || o->closing || !o->retired || !o->physical || !source ||
        source->runtime!=o->options.runtime || !qa_net_client_id_equal(source->client,o->client) ||
        source->connection_epoch!=o->epoch || source->network_seat.owner!=o->binding.seat.owner ||
        source->network_seat.index!=o->binding.seat.index) return false;
    if (frontend_client_source_preinstall_current(o->physical,source,NULL)) return true;
    if (!frontend_client_source_metadata_read(o->physical,&held,NULL) ||
        !qa_application_client_associated(o->options.frontend->application,source)) return false;
    const qa_application_client_source *actual=&held.source;
    return source->descriptor==actual->descriptor && source->runtime==o->options.runtime &&
        source->runtime==actual->runtime && qa_net_client_id_equal(source->client,o->client) &&
        qa_net_client_id_equal(source->client,actual->client) && source->connection_epoch==o->epoch &&
        source->connection_epoch==actual->connection_epoch && source->network_seat.owner==o->binding.seat.owner &&
        source->network_seat.index==o->binding.seat.index &&
        source->network_seat.owner==actual->network_seat.owner && source->network_seat.index==actual->network_seat.index &&
        source->configuration_generation==actual->configuration_generation &&
        source->context.session==actual->context.session && source->context.receiver==actual->context.receiver &&
        source->context.entity_owner==actual->context.entity_owner && source->context.entity_definition==actual->context.entity_definition &&
        source->context.seat==actual->context.seat && source->context.physical_seat==o->options.physical_seat &&
        source->context.physical_seat==actual->context.physical_seat && source->context.console==actual->context.console &&
        source->context.cvars==actual->context.cvars && source->context.lifetime==actual->context.lifetime;
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
static bool configuration_advance(void *context,const qa_application_client_source *source,
    qa_application_client_preparation *token,bool *ready,qa_error *error)
{
    frontend_network_q1_client *o=context;
    return parent(o)&&o->options.configuration.configuration_advance(
        o->options.configuration.context,source,token,ready,error)&&parent(o);
}
static qa_command_result forward(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(!parent(o)) return QA_COMMAND_FAILED;
    if(o->options.configuration.forward) {
        qa_command_result result=o->options.configuration.forward(o->options.configuration.context,command,error);
        if(result!=QA_COMMAND_UNHANDLED) return result;
        if(!parent(o)) return QA_COMMAND_FAILED;
    }
    if(!o->source) return QA_COMMAND_UNHANDLED;
    frontend_remote_q1_source_view view;
    if(!frontend_remote_q1_source_read(o->source,&view,error)) return QA_COMMAND_FAILED;
    if(!view.domain.client.owner) return QA_COMMAND_UNHANDLED;
    if(command->argc && command->argv && command->argv[0] &&
        (named(command->argv[0],"use") || named(command->argv[0],"weapnext") || named(command->argv[0],"weapprev"))) {
        frontend_remote_q1_player_view player; bool present=false;
        if(!frontend_remote_q1_player_read(view.receiver,&player,&present,error) || !present) return QA_COMMAND_FAILED;
        const char *name=named(command->argv[0],"use")?"use":
            named(command->argv[0],"weapnext")?"weapnext":"weapprev";
        return frontend_remote_q1_player_command(view.receiver,player.actor,name,command->argv+1,
            command->argc-1,error)?QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
    }
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
static bool programme_retire(void *context,const qa_application_client_source *source,qa_error *error)
{ frontend_network_q1_client *o=context; return o->options.configuration.retire(
    o->options.configuration.context,source,error); }
static void programme_released(void *context)
{
    frontend_network_q1_client *o=context;
    o->configuration_released=true;
    o->options.configuration.released(o->options.configuration.context);
}
static bool pending_invocation(frontend_network_q1_client *o,const qa_command_invocation *call,qa_error *error)
{
    frontend_client_source_view view; qa_command_context expected;
    if(!frontend_client_source_metadata_read(o->physical,&view,error) ||
        call->console!=view.source.context.console ||
        !qa_application_capture_command_context(o->options.frontend->application,
            &view.source.context.command,&expected,error)) return false;
    const qa_command_context *actual=&call->context;
    return actual->session==expected.session && actual->owner==expected.owner &&
        actual->client==expected.client && actual->seat==expected.seat && actual->dialect==expected.dialect &&
        actual->registry==expected.registry && actual->generation==expected.generation &&
        qa_actor_id_equal(actual->actor,expected.actor) &&
        qa_application_command_context_active(o->options.frontend->application,actual);
}
static bool skin_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(!parent(o) || !call || !call->argc || !call->argv || !call->argv[0]) return false;
    if(!o->client.owner) {
        if(!pending_invocation(o,call,error)) return false;
        /* The actual startup command can select allskins before a transport
         * exists. Its private value moves into the real child after bind. */
        if(named(call->argv[0],"allskins")) {
            const char *value=call->argc>1?call->argv[1]:"";
            if(!value) return false;
            size_t size=strlen(value)+1; char *copy=malloc(size);
            if(!copy) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining startup allskins command");
            memcpy(copy,value,size); free(o->pending_allskins); o->pending_allskins=copy;
        } else if(named(call->argv[0],"stopdownload"))
            frontend_console_print(o->options.frontend,&call->context,
                "Downloads paused. Use retrydownload to resume.\n");
        return pending_invocation(o,call,error);
    }
    frontend_remote_q1_source_view view;
    return frontend_remote_q1_source_read(o->source,&view,error) &&
        frontend_network_q1_skin_command(view.receiver,call,error)==QA_COMMAND_HANDLED;
}
static uint32_t decimal_word(const char *text)
{
    while(*text==' ' || (*text>=9 && *text<=13)) ++text;
    bool negative=*text=='-'; if(*text=='-' || *text=='+') ++text;
    uint32_t value=0,limit=negative?UINT32_C(2147483648):UINT32_C(2147483647);
    while(*text>='0' && *text<='9') {
        uint32_t digit=(uint32_t)(*text++-'0');
        value=value>(limit-digit)/10u?limit:value*10u+digit;
    }
    return negative?0u-value:value;
}
static bool color_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context; frontend_client_source_view view;
    if(!parent(o) || !call || !call->argc || !call->argv ||
        !pending_invocation(o,call,error) ||
        !frontend_client_source_metadata_read(o->physical,&view,error)) return false;
    if(call->argc==1) {
        const qa_cvar_view *top=qa_cvars_find(view.source.context.cvars,"topcolor"),
            *bottom=qa_cvars_find(view.source.context.cvars,"bottomcolor");
        if(!top || !bottom) return false;
        size_t capacity=strlen(top->value)+strlen(bottom->value)+24; char *text=malloc(capacity);
        if(!text) return frontend_fail(error,QA_ERROR_MEMORY,"Formatting actual QW player colors");
        snprintf(text,capacity,"\"color\" is \"%s %s\"\n",top->value,bottom->value);
        frontend_console_print(o->options.frontend,&call->context,text); free(text);
    } else {
        if(!call->argv[1] || (call->argc>2 && !call->argv[2])) return false;
        unsigned top=decimal_word(call->argv[1])&15u,
            bottom=decimal_word(call->argc>2?call->argv[2]:call->argv[1])&15u;
        if(top>13) top=13;
        if(bottom>13) bottom=13;
        char value[8]; snprintf(value,sizeof(value),"%u",top);
        if(!qa_cvars_set(view.source.context.cvars,"topcolor",value,false,error)) return false;
        snprintf(value,sizeof(value),"%u",bottom);
        if(!qa_cvars_set(view.source.context.cvars,"bottomcolor",value,false,error)) return false;
    }
    return parent(o) && pending_invocation(o,call,error);
}
static bool center_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context; frontend_remote_q1_source_view source;
    frontend_remote_q1_player_view player; bool present=false;
    if(!parent(o) || !call || !o->source || !o->client.owner || !pending_invocation(o,call,error) ||
        !frontend_remote_q1_source_read(o->source,&source,error) ||
        !frontend_remote_q1_player_read(source.receiver,&player,&present,error) || !present) return false;
    o->input_center=true; return pending_invocation(o,call,error);
}
static bool install(void *context,const qa_application_client_source *source,bool restoring,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(!qa_console_register_owned(source->context.console,"centerview","Center the actual CLIENT view",
        source->context.receiver,source->context.receiver,true,center_command,o,error)) return false;
    if(o->options.configuration.install && !o->options.configuration.install(
        o->options.configuration.context,source,restoring,error)) return false;
    if(!qa_q1_is_qw(o->options.protocol)) return true;
    static const char *const names[]={"skins","allskins","stopdownload","retrydownload"};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i)
        if(!qa_console_register(source->context.console,names[i],"",source->context.command.owner,
            false,skin_command,o,error)) return false;
    return qa_console_register(source->context.console,"color","",source->context.command.owner,
        false,color_command,o,error);
}
static frontend_client_source_options physical_options(frontend_network_q1_client *o)
{
    frontend_client_source_options c=o->options.configuration;
    c.context=o; c.runtime=o->options.runtime; c.physical_seat=o->options.physical_seat;
    c.initialize=initialize; c.configure=configure; c.install=install; c.print=print;
    c.connection_current=connection; c.retirement_current=retirement;
    c.entity_current=entity; c.command=command; c.forward=forward;
    c.allow_command=allow;
    if(c.cvar_owner) c.cvar_owner=cvar_owner;
    if(c.visible_cvars) c.visible_cvars=visible;
    if(c.cvar_edit) c.cvar_edit=edit;
    if(c.read_script) { c.read_script=script; c.release_script=script_release; }
    if(c.script_complete) c.script_complete=script_complete;
    if(c.retire) c.retire=programme_retire;
    if(c.released) c.released=programme_released;
    if(c.configuration_advance) c.configuration_advance=configuration_advance;
    return c;
}
static bool skin_current(void *context,const frontend_remote_q1_domain *expected,qa_error *error)
{
    frontend_network_q1_client *o=context; frontend_remote_q1_source_view view;
    if(!parent(o) || !expected) return false;
    if(!o->source) {
        frontend_client_source_view actual;
        return !expected->client.owner && !o->client.owner &&
            frontend_client_source_read(o->physical,&actual,error) && actual.ready &&
            actual.source.runtime==expected->runtime &&
            actual.source.context.physical_seat==expected->physical_seat &&
            actual.source.context.entity_owner==expected->actor_owner &&
            actual.source.context.entity_definition==expected->actor_definition &&
            actual.source.context.console==expected->console && actual.source.context.cvars==expected->cvars &&
            actual.source.configuration_generation==expected->configuration_generation &&
            qa_launch_instance_catalog(actual.source.descriptor)==expected->catalog &&
            actual.source.descriptor->selection.product==expected->product;
    }
    return frontend_remote_q1_source_read(o->source,&view,error) &&
        view.domain.catalog==expected->catalog && view.domain.product==expected->product &&
        qa_net_client_id_equal(view.domain.client,expected->client) && view.domain.epoch==expected->epoch &&
        view.domain.actor_owner==expected->actor_owner && view.domain.cvars==expected->cvars;
}
static bool download_permission(void *context,bool *allowed,bool *recording,bool *playback,qa_error *error)
{ frontend_network_q1_client *o=context; return parent(o) && o->options.downloads(
    o->options.context,allowed,recording,playback,error) && parent(o); }
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
        o->skins=(frontend_remote_q1_skin_bindings){.files=o->skin_files,.root=root,.maximum_bytes=64u*1024u*1024u,
            .context=o,.current=skin_current,.permission=download_permission,.nonce=nonce,.reliable=reliable,.print=skin_print};
    }
    frontend_remote_q1_source_options receiver=receiver_options(o);
    if(!frontend_remote_q1_source_create(o->options.frontend,&receiver,&o->source,error)) return false;
    if(qa_q1_is_qw(o->options.protocol)) {
        qa_buffer info={0};
        bool ok=qa_cvars_info(physical.source.context.cvars,QA_CVAR_USERINFO,512,&info,error);
        if(ok) {
            o->userinfo=(char *)info.data; info=(qa_buffer){0};
            ok=qa_qw_connect_create(o->options.qport,o->userinfo,&o->qw,error);
        }
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
        options->frontend->capture || options->frontend->resource_inventory || options->frontend->source_restoring ||
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
    if(!retain_policy(o,error)) return false;
    o->input.kind=qa_q1_is_qw(options->protocol)?QA_MOVEMENT_QUAKEWORLD:QA_MOVEMENT_NETQUAKE;
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
static bool userinfo_sync(frontend_network_q1_client *o,qa_error *error)
{
    if(!o->qw) return true;
    if(!o->userinfo_pending) {
        frontend_client_source_view physical; qa_buffer info={0};
        if(!frontend_client_source_read(o->physical,&physical,error) ||
            !qa_cvars_info(physical.source.context.cvars,QA_CVAR_USERINFO,512,&info,error)) return false;
        if(strpbrk((char *)info.data,"\"\r\n")) {
            qa_buffer_free(&info); return frontend_fail(error,QA_ERROR_FORMAT,"Invalid QW userinfo");
        }
        if(!strcmp(o->userinfo,(char *)info.data)) { qa_buffer_free(&info); return true; }
        o->userinfo_pending=(char *)info.data; o->userinfo_next=0;
    }
    if(o->client.owner) {
        qa_qw_info previous={0},current={0};
        bool ok=qa_qw_info_parse(o->userinfo,&previous,error) &&
            qa_qw_info_parse(o->userinfo_pending,&current,error);
        size_t total=previous.count+current.count;
        if(ok && o->userinfo_next>total) ok=false;
        for(size_t i=o->userinfo_next;ok && i<total;++i) {
            const char *key=i<previous.count?previous.rules[i].name:current.rules[i-previous.count].name;
            bool duplicate=i>=previous.count && qa_qw_info_get(&previous,key)!=NULL;
            const char *before=qa_qw_info_get(&previous,key),*after=qa_qw_info_get(&current,key);
            if(!after) after="";
            if(!duplicate && (!before || strcmp(before,after))) {
                size_t capacity=strlen(key)+strlen(after)+14;
                char *text=malloc(capacity);
                if(!text) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining reached QW setinfo command");
                else {
                    snprintf(text,capacity,"setinfo \"%s\" \"%s\"",key,after);
                    ok=qa_network_q1_client_command(o->options.runtime,o->client,text,error);
                    free(text);
                }
            }
            if(ok) o->userinfo_next=i+1;
        }
        qa_qw_info_free(&previous); qa_qw_info_free(&current);
        if(!ok) return false;
    }
    if(!qa_qw_connect_userinfo(o->qw,o->userinfo_pending,error)) return false;
    free(o->userinfo); o->userinfo=o->userinfo_pending; o->userinfo_pending=NULL; o->userinfo_next=0;
    return true;
}
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
        if(!policy.nq_identity.spawn_parameters) return false;
        size_t size=strlen(name->value)+1; char *name_copy=malloc(size);
        if(!name_copy) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual NQ signon name");
        memcpy(name_copy,name->value,size);
        size=strlen(policy.nq_identity.spawn_parameters)+1; char *parameters=malloc(size);
        if(!parameters) { free(name_copy); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual NQ spawn parameters"); }
        memcpy(parameters,policy.nq_identity.spawn_parameters,size);
        free(o->signon_name); free(o->spawn_parameters); o->signon_name=name_copy; o->spawn_parameters=parameters;
        policy.nq_identity.name=o->signon_name;
        policy.nq_identity.spawn_parameters=o->spawn_parameters;
        o->signon_color=(uint8_t)((uint32_t)color->integer&255u); policy.nq_identity.color=o->signon_color;
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
    if(!recognized || !packet || !parent(o) || o->calls || o->importing ||
        o->options.frontend->capture || o->options.frontend->resource_inventory ||
        o->options.frontend->source_restoring) return false;
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
    if(!userinfo_sync(o,error)) return false;
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
    if(!parent(o) || o->calls || o->importing || o->options.frontend->capture || o->options.frontend->resource_inventory ||
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
    if(o->options.frontend->capture || o->options.frontend->resource_inventory ||
        !frontend_network_q1_client_idle(o) || !qa_network_callbacks_idle(o->options.runtime)) return false;
    if(o->client.owner && qa_net_connections_get(qa_network_connections(o->options.runtime),o->client)) {
        if(o->importing) {
            bool incomplete=qa_network_connection_incomplete(o->options.runtime,o->client);
            if(incomplete?!qa_network_discard_incomplete(o->options.runtime,o->client,error):
                !qa_network_detach(o->options.runtime,o->client,"Q1 candidate closed",error)) return false;
        } else if(!qa_network_q1_client_disconnect(o->options.runtime,o->client,"Q1 CLIENT closed",error)) return false;
    }
    if(!frontend_remote_q1_source_destroy(&o->source,error) || !frontend_client_source_destroy(&o->physical,error)) return false;
    if(!o->configuration_released) {
        if(o->options.configuration.retire && !o->options.configuration.retire(
            o->options.configuration.context,NULL,error)) return false;
        o->configuration_released=true;
        if(o->options.configuration.released)
            o->options.configuration.released(o->options.configuration.context);
    }
    o->closing=true; qa_nq_connect_destroy(o->nq); qa_qw_connect_destroy(o->qw);
    qa_vfs_destroy(o->skin_files); qa_vfs_destroy(o->content_files); qa_catalog_release(o->content_catalog);
    free(o->pending_allskins); free(o->userinfo); free(o->userinfo_pending); free(o->signon_name); free(o->spawn_parameters);
    free(o->declared_name); free(o->declared_parameters);
    free(o); *owned=NULL; return true;
}
bool frontend_network_q1_client_source_read(const frontend_network_q1_client *o,
    frontend_remote_q1_source_view *out,qa_error *error)
{ return parent(o) && o->source && frontend_remote_q1_source_read(o->source,out,error); }
bool frontend_network_q1_client_input(frontend_network_q1_client *o,uint32_t physical,
    const qa_seat_input_sample *sample,uint64_t sequence,double source_frame_ms,bool *handled,qa_error *error)
{
    if(!sample || !handled || !sequence) return false;
    *handled=o && physical==o->options.physical_seat;
    if(!*handled) return true;
    qa_frontend *f=o->options.frontend;
    if(o->calls || o->admitting || o->importing || f->capture || f->resource_inventory || f->source_restoring ||
        !qa_network_callbacks_idle(o->options.runtime) || !parent(o)) return false;
    if(o->retired || !o->source || !o->client.owner || sequence<=o->input_sample) return true;
    qa_network_q1_client_state transport;
    if(!qa_network_q1_client_state_read(o->options.runtime,o->client,&transport,error)) return false;
    if(!transport.active) return true;
    frontend_remote_q1_source_view source;
    frontend_remote_q1_view received;
    frontend_remote_q1_player_view player; bool present=false;
    if(!frontend_remote_q1_source_read(o->source,&source,error) ||
        !frontend_remote_q1_read(source.receiver,&received,error) ||
        !frontend_remote_q1_player_read(source.receiver,&player,&present,error)) return false;
    if(!present || player.intermission) return true;
    if(o->input_sequence==UINT64_MAX)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q1 physical command sequence is exhausted");
    frontend_neutral_config_view settings;
    if(!frontend_config_store_neutral_movement_adopt(f->config_store,source.physical.source.context.console,
            o->input.kind,error) ||
        !frontend_config_store_neutral_read(f->config_store,source.physical.source.context.console,&settings,error) ||
        !settings.ready || settings.kind!=o->input.kind || settings.physical_seat!=physical ||
        settings.client!=source.physical.source.context.cvars || !frontend_neutral_config_current(&settings)) return false;
    qa_input_command_builder next=o->input;
    qa_input_command_tuning tuning; qa_movement_command movement;
    qa_input_command_frame frame={.kind=next.kind,.sequence=o->input_sequence+1,
        .acknowledged_server_seconds=received.seconds,.has_pitch_drift=true,
        .grounded=player.grounded,.ideal_pitch=player.ideal_pitch,.drift_disabled=player.pitch_drift_disabled};
    if(!qa_input_command_angles(&next,player.angles,error) ||
        !qa_input_settings_read_routed(settings.mouse,settings.movement,next.kind,&tuning,error)) return false;
    if(o->input_center) qa_input_command_center(&next,0);
    if(!qa_input_command_build(&next,&tuning,sample,&frame,source_frame_ms,&movement,error) ||
        !frontend_remote_q1_current(&received) || !frontend_remote_q1_source_current(&source) ||
        !frontend_neutral_config_current(&settings)) return false;
    qa_network_command command={.client=o->client,.seat=o->binding.seat,.actor=player.actor,
        .epoch=o->epoch,.movement=movement};
    ++o->calls;
    bool ok=qa_network_q1_client_submit(o->options.runtime,&command,error);
    --o->calls;
    if(!ok) return false;
    o->input=next; o->input_sequence=frame.sequence; o->input_sample=sequence; o->input_center=false;
    return true;
}
bool frontend_network_q1_client_metadata_read(const frontend_network_q1_client *o,
    frontend_network_q1_client_view *out,qa_error *error)
{
    frontend_client_source_view physical;
    if(!o || !out || o->closing || !o->physical ||
        !frontend_client_source_metadata_read(o->physical,&physical,error)) return false;
    *out=(frontend_network_q1_client_view){.owner=o,.physical=physical,.client=o->client,
        .protocol=o->options.protocol,.profile=o->options.profile,.remote=o->options.remote,
        .epoch=o->epoch,.physical_seat=o->options.physical_seat,.qport=o->options.qport,
        .configured=o->configured,.retired=o->retired,.policy=o->options.policy,
        .connected_remote=o->attachment.endpoint,.composition=o->attachment.composition,.seat=o->binding.seat};
    out->policy.qport=o->options.qport;
    if(o->signon_name && o->spawn_parameters && !qa_q1_is_qw(o->options.protocol)) {
        out->policy.nq_identity.name=o->signon_name;
        out->policy.nq_identity.color=o->signon_color;
        out->policy.nq_identity.spawn_parameters=o->spawn_parameters;
    }
    return true;
}
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
static bool address_fields(qa_source_save_io *io,qa_net_address *address)
{
    uint32_t kind=address->kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_NET_IPX || !qa_source_save_u16(io,&address->port)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) address->kind=(qa_net_address_kind)kind;
    switch(address->kind) {
    case QA_NET_IPV4: return qa_source_save_bytes(io,address->host.ipv4,4);
    case QA_NET_IPV6: return qa_source_save_bytes(io,address->host.ipv6.bytes,16) &&
        qa_source_save_u32(io,&address->host.ipv6.scope);
    case QA_NET_LOOPBACK: return qa_source_save_bytes(io,address->host.loopback,sizeof(address->host.loopback)) &&
        memchr(address->host.loopback,0,sizeof(address->host.loopback));
    case QA_NET_IPX: return qa_source_save_u32(io,&address->host.ipx.network) &&
        qa_source_save_bytes(io,address->host.ipx.node,6);
    }
    return false;
}
static bool controller_fields(frontend_network_q1_client *o,const frontend_remote_q1_restore_refs *refs,
    qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','1','N','C'}; uint64_t catalog=0,files=0,skins=0;
    if(!reading) {
        if(o->content_catalog) catalog=qa_application_content_catalog_id(refs->content,o->content_catalog);
        if(o->content_files) files=qa_application_content_view_id(refs->content,o->content_files);
        if(o->skin_files) skins=qa_application_content_view_id(refs->content,o->skin_files);
        if((o->content_catalog && !catalog) || (o->content_files && !files) || (o->skin_files && !skins)) return false;
    }
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q1NC",4) || !qa_source_save_bool(io,&o->configured) || !qa_source_save_bool(io,&o->retired) ||
        !qa_source_save_u64(io,&o->client.owner) || !qa_source_save_u64(io,&o->client.generation) ||
        !qa_source_save_u32(io,&o->client.slot) || !qa_source_save_u64(io,&o->epoch) ||
        !qa_source_save_u64(io,&o->now_ns) || !address_fields(io,&o->attachment.endpoint) ||
        !qa_source_save_bytes(io,o->attachment.composition.bytes,sizeof(o->attachment.composition.bytes)) ||
        !qa_source_save_u64(io,&catalog) || !qa_source_save_u64(io,&files) || !qa_source_save_u64(io,&skins) ||
        !qa_source_save_u32(io,&o->content_product) || !frontend_save_text(io,&o->pending_allskins) ||
        !frontend_save_text(io,&o->userinfo) || !frontend_save_text(io,&o->userinfo_pending) ||
        !qa_source_save_count(io,&o->userinfo_next,1024) || !frontend_save_text(io,&o->signon_name) ||
        !frontend_save_text(io,&o->spawn_parameters) || !qa_source_save_u8(io,&o->signon_color) ||
        !qa_source_save_bytes(io,o->reason,sizeof(o->reason)) || !memchr(o->reason,0,sizeof(o->reason)) ||
        !qa_source_save_u64(io,&o->input_sequence) || !qa_source_save_u64(io,&o->input_sample) ||
        !qa_source_save_bool(io,&o->input_center)) return false;
    qa_buffer input={0}; size_t input_size=0;
    if(!reading) {
        if(!qa_input_command_checkpoint(&o->input,&input,io->error)) return false;
        input_size=input.size;
    }
    bool input_ok=qa_source_save_count(io,&input_size,128);
    if(input_ok && reading) {
        input_ok=io->offset<=io->input.size && input_size<=io->input.size-io->offset &&
            qa_input_command_restore(&o->input,(qa_bytes){io->input.data+io->offset,input_size},io->error);
        if(input_ok) io->offset+=input_size;
    } else if(input_ok) input_ok=qa_source_save_bytes(io,input.data,input_size);
    qa_buffer_free(&input);
    if(!input_ok || o->input.kind!=(qa_q1_is_qw(o->options.protocol)?QA_MOVEMENT_QUAKEWORLD:QA_MOVEMENT_NETQUAKE) ||
        (o->input_sequence && (!o->input_sample || !o->client.owner))) return false;
    if(!!o->client.owner!=!!o->client.generation || !!o->client.owner!=!!o->epoch ||
        (!o->client.owner && o->client.slot) || (o->client.owner && !o->configured) ||
        !!catalog!=!!files || (!catalog && o->content_product) ||
        (o->configured && qa_q1_is_qw(o->options.protocol)? !skins || !o->userinfo : skins || o->userinfo) ||
        (!o->userinfo_pending && o->userinfo_next) || (o->userinfo_pending && (!o->userinfo ||
            !qa_q1_is_qw(o->options.protocol) || strlen(o->userinfo_pending)>=512 ||
            strpbrk(o->userinfo_pending,"\"\r\n"))) ||
        (o->userinfo && (strlen(o->userinfo)>=512 || strpbrk(o->userinfo,"\"\r\n"))) ||
        (o->client.owner && !qa_q1_is_qw(o->options.protocol) && (!o->signon_name || !o->spawn_parameters))) return false;
    if(reading) {
        if((catalog && !qa_application_content_retain_catalog(refs->content,catalog,&o->content_catalog,io->error)) ||
            (files && !qa_application_content_claim_view(refs->content,files,&o->content_files,io->error)) ||
            (skins && !qa_application_content_claim_view(refs->content,skins,&o->skin_files,io->error))) return false;
    }
    if(o->content_catalog) {
        const qa_product *product=qa_catalog_product(o->content_catalog,o->content_product);
        if(!product || product->family!=QA_GAME_Q1 ||
            (product->edition==QA_EDITION_QUAKEWORLD)!=qa_q1_is_qw(o->options.protocol)) return false;
    }
    return true;
}
void frontend_network_q1_client_state_free(frontend_network_q1_client_state *state)
{
    if(!state) return;
    qa_buffer_free(&state->physical); qa_buffer_free(&state->receiver);
    qa_buffer_free(&state->handshake); qa_buffer_free(&state->controller); *state=(frontend_network_q1_client_state){0};
}
bool frontend_network_q1_client_capture(frontend_network_q1_client *o,
    const frontend_remote_q1_restore_refs *refs,frontend_network_q1_client_state *out,qa_error *error)
{
    if(!o || !out || out->physical.data || out->receiver.data || out->handshake.data || out->controller.data ||
        !refs || !refs->content || !o->options.frontend->capture ||
        (o->importing && (!o->restore_finished ||
            !frontend_network_q1_client_qualified(o,o->options.runtime,true,error))) ||
        !frontend_network_q1_client_idle(o) || !o->physical) return false;
    frontend_network_q1_client_state state={0}; qa_source_save_io io;
    if(!qa_source_save_writer(&io,qa_application_session(o->options.frontend->application),error)) return false;
    bool ok=controller_fields(o,refs,&io) && qa_source_save_finish(&io,&state.controller);
    qa_source_save_dispose(&io);
    if(ok) ok=frontend_client_source_checkpoint(o->physical,refs->content,&state.physical,error);
    if(ok && o->configured) ok=frontend_remote_q1_source_checkpoint(o->source,refs,&state.receiver,error) &&
        (o->qw?qa_qw_connect_checkpoint(o->qw,&state.handshake,error):qa_nq_connect_checkpoint(o->nq,&state.handshake,error));
    if(!ok) { frontend_network_q1_client_state_free(&state); return false; }
    *out=state; return true;
}
bool frontend_network_q1_client_restore_prepare(const frontend_network_q1_client_options *options,
    const frontend_remote_q1_restore_refs *refs,const qa_console_save_resolvers *resolvers,
    const frontend_network_q1_client_state *saved,frontend_network_q1_client **out,qa_error *error)
{
    if(!options || !refs || !refs->content || !resolvers || !saved || !out || *out || !options->frontend ||
        !options->frontend->source_restoring || !options->runtime || !options->current ||
        options->frontend->capture || options->frontend->resource_inventory ||
        options->physical_seat>=options->frontend->options.seats ||
        !options->configuration.configure || !options->service || !qa_q1_profile_valid(options->protocol,error) ||
        !saved->physical.size || !saved->controller.size ||
        (qa_q1_is_qw(options->protocol) && (!options->downloads || !options->download_nonce))) return false;
    frontend_network_q1_client *o=calloc(1,sizeof(*o));
    if(!o) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining restored Q1 CLIENT factory");
    *out=o; o->options=*options; o->importing=true;
    if(!retain_policy(o,error)) return false;
    o->binding=(qa_net_seat_binding){{QA_NETWORK_COMMAND_OWNER,options->physical_seat},0};
    o->attachment=(qa_net_connect){.attachment=QA_NET_REMOTE,.protocol=options->protocol,.seats=&o->binding,.seat_count=1};
    qa_source_save_io io;
    if(!qa_source_save_reader(&io,qa_application_session(options->frontend->application),
        (qa_bytes){saved->controller.data,saved->controller.size},error)) return false;
    bool ok=controller_fields(o,refs,&io) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); if(!ok) return false;
    if(o->configured != (saved->receiver.size!=0) || o->configured != (saved->handshake.size!=0)) return false;
    frontend_client_source_prefix prefix={0};
    if(!frontend_client_source_prefix_read(options->frontend,refs->content,
        (qa_bytes){saved->physical.data,saved->physical.size},&prefix,error)) return false;
    const qa_product *profile=qa_catalog_product(prefix.recipe.catalog,prefix.recipe.profile);
    frontend_client_source_options physical=physical_options(o); physical.metadata=prefix.recipe;
    physical.input_origin=options->configuration.input_origin;
    physical.input_origin.owner=0; physical.input_origin.actor=(qa_actor_id){0}; physical.input_origin.client=0;
    physical.input_origin.registry=physical.input_origin.generation=0; physical.input_origin.script=false;
    physical.input_origin.dialect=qa_q1_is_qw(options->protocol)?QA_CONSOLE_QW:QA_CONSOLE_Q1;
    ok=profile && profile->id==options->profile && profile->family==QA_GAME_Q1 &&
        (profile->edition==QA_EDITION_QUAKEWORLD)==qa_q1_is_qw(options->protocol) &&
        qa_net_client_id_equal(prefix.state.application.client,o->client) &&
        prefix.state.application.connection_epoch==o->epoch &&
        (o->client.owner?prefix.state.application.network_seat.owner==o->binding.seat.owner &&
            prefix.state.application.network_seat.index==o->binding.seat.index:!prefix.state.application.network_seat.owner);
    frontend_client_source_prefix_free(&prefix);
    if(!ok || !frontend_client_source_restore_prefix(options->frontend,&physical,refs->content,resolvers,
        (qa_bytes){saved->physical.data,saved->physical.size},&o->physical,error)) return false;
    if(!o->configured) return true;
    frontend_client_source_view actual;
    if(!frontend_client_source_metadata_read(o->physical,&actual,error) || !actual.ready ||
        !qa_sha256_equal(&actual.source.descriptor->identity,&o->attachment.composition)) return false;
    if(qa_q1_is_qw(options->protocol)) {
        const qa_product *base=qa_catalog_find(qa_launch_instance_catalog(actual.source.descriptor),"q1-quakeworld");
        qa_fs_root *root=base?qa_catalog_product_write_root(qa_launch_instance_catalog(actual.source.descriptor),base->id):NULL;
        if(!root) return false;
        o->skins=(frontend_remote_q1_skin_bindings){.files=o->skin_files,.root=root,.maximum_bytes=64u*1024u*1024u,
            .context=o,.current=skin_current,.permission=download_permission,.nonce=nonce,.reliable=reliable,.print=skin_print};
    }
    frontend_remote_q1_source_options receiver=receiver_options(o);
    return frontend_remote_q1_source_restore_prepare(options->frontend,&receiver,refs,
        (qa_bytes){saved->receiver.data,saved->receiver.size},&o->source,error) &&
        (qa_q1_is_qw(options->protocol)?qa_qw_connect_restore_checkpoint(
            (qa_bytes){saved->handshake.data,saved->handshake.size},options->qport,o->userinfo,&o->qw,error):
            qa_nq_connect_restore_checkpoint((qa_bytes){saved->handshake.data,saved->handshake.size},&o->nq,error));
}
bool frontend_network_q1_client_restore_hooks(frontend_network_q1_client *o,const qa_net_client *client,
    qa_network_q1_client_policy *policy,qa_network_q1_client_hooks *hooks,qa_error *error)
{
    if(!o || !o->importing || !client || !policy || !hooks || !o->configured || !o->source ||
        !qa_net_client_id_equal(client->id,o->client) || client->attachment!=QA_NET_REMOTE ||
        !same_protocol(client->protocol,o->options.protocol) || client->seat_count!=1 ||
        client->seats[0].seat.owner!=o->binding.seat.owner || client->seats[0].seat.index!=o->binding.seat.index ||
        client->seats[0].remote_index || !qa_net_address_equal(&client->endpoint,&o->attachment.endpoint,true) ||
        !qa_sha256_equal(&client->composition,&o->attachment.composition)) return false;
    *policy=o->options.policy; policy->qport=o->options.qport;
    if(!qa_q1_is_qw(o->options.protocol)) {
        policy->nq_identity.name=o->signon_name; policy->nq_identity.spawn_parameters=o->spawn_parameters;
        policy->nq_identity.color=o->signon_color;
    }
    return frontend_remote_q1_source_hooks(o->source,hooks,error);
}
bool frontend_network_q1_client_restore_finish(frontend_network_q1_client *o,
    const frontend_remote_q1_restore_refs *refs,qa_error *error)
{
    if(!o || !o->importing || !frontend_network_q1_client_idle(o)) return false;
    if(o->restore_finished) return true;
    if(o->configured && !frontend_remote_q1_source_restore_finish(o->source,refs,error)) return false;
    o->restore_finished=true; return true;
}
bool frontend_network_q1_client_importing(const frontend_network_q1_client *o)
{ return o && o->importing && (o->options.frontend->source_restoring || o->restore_finished); }
bool frontend_network_q1_client_qualified(const frontend_network_q1_client *o,const qa_network_runtime *runtime,
    bool complete,qa_error *error)
{
    frontend_client_source_view physical;
    if(!parent(o) || runtime!=o->options.runtime || !frontend_network_q1_client_idle(o) ||
        (complete && o->importing && !o->restore_finished) ||
        !frontend_client_source_metadata_read(o->physical,&physical,error) ||
        physical.source.runtime!=runtime || physical.source.context.physical_seat!=o->options.physical_seat ||
        !qa_net_client_id_equal(physical.source.client,o->client) || physical.source.connection_epoch!=o->epoch ||
        (complete && !o->retired && !frontend_client_source_current(&physical))) return false;
    uint32_t cursor=0; const qa_net_client *client=NULL,*actual=NULL;
    while(qa_net_connections_next(qa_network_connections(runtime),&cursor,&client)) {
        if(!qa_net_client_id_equal(client->id,o->client))
            return frontend_fail(error,QA_ERROR_FORMAT,"Q1 CLIENT runtime retains another connection");
        actual=client;
    }
    if(!o->client.owner) {
        if(actual || physical.source.network_seat.owner || physical.source.network_seat.index ||
            (o->configured && !o->source)) return false;
    } else if(o->retired) {
        if(actual) return false;
    } else {
        if(!actual || !connection((void *)o,&physical.source) || !o->source) return false;
        if(complete) {
            qa_network_q1_client_state state;
            if(!qa_network_q1_client_state_read(o->options.runtime,o->client,&state,error) ||
                !same_protocol(state.admitted_protocol,o->options.protocol)) return false;
        }
    }
    if(o->source) {
        frontend_remote_q1_source_view source; frontend_remote_q1_view receiver;
        if(!frontend_remote_q1_source_metadata_read(o->source,&source,error) || source.physical.owner!=o->physical ||
            !frontend_remote_q1_metadata_read(source.receiver,&receiver,error) ||
            receiver.domain.runtime!=runtime || !qa_net_client_id_equal(receiver.domain.client,o->client) ||
            receiver.domain.epoch!=o->epoch || receiver.bound!=!!o->client.owner || receiver.retired!=o->retired) return false;
        if(complete && !o->retired && (!frontend_remote_q1_source_current(&source) ||
            (receiver.bound && !frontend_remote_q1_current(&receiver)))) return false;
    }
    return true;
}
bool frontend_network_q1_client_publication_ready(const frontend_network_q1_client *o,qa_error *error)
{
    return !o || (o->importing && o->restore_finished &&
        frontend_network_q1_client_qualified(o,o->options.runtime,true,error));
}
void frontend_network_q1_client_publish(frontend_network_q1_client *o)
{ if(o) o->importing=false; }
