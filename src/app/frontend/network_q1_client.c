#include "network_q1_client.h"
#include "internal.h"
#include "network_q1_skin_commands.h"
#include "neutral_config.h"
#include "qa/application_network.h"
#include "qa/ui_language.h"
#include "qa/q1_chat_commands.h"
#include "qa/source_frame_time.h"
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
    uint64_t input_clock_ns, input_frame_ns, input_wall_frame_ns;
    qa_input_command_builder input;
    uint64_t input_sequence, input_sample;
    unsigned calls;
    bool configured, admitting, retired, closing, configuration_released, input_center;
    char *pending_allskins, *userinfo, *userinfo_pending, *signon_name, *spawn_parameters;
    char *declared_name, *declared_parameters;
    size_t userinfo_next;
    uint8_t signon_color;
    char reason[1024];
    frontend_demo_packet demo_pending;
    double demo_seconds;
    int32_t demo_forced_track;
    bool demo_has_pending, demo_clock_started;
    frontend_demo_sink demo_follow;
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
        (client->composition == o->attachment.composition);
}
static bool retirement(void *context,const qa_application_client_source *source)
{
    frontend_network_q1_client *o=context;
    frontend_client_source_view held;
    if (!o || o->closing || !o->retired || !o->physical || !source ||
        source->runtime!=o->options.runtime || !qa_net_client_id_equal(source->client,o->client) ||
        source->connection_epoch!=o->epoch ||
        (!o->client.owner && (o->client.generation || o->client.slot || o->epoch))) return false;
    const qa_net_seat_id binding=o->client.owner?o->binding.seat:(qa_net_seat_id){0};
    if (source->network_seat.owner!=binding.owner || source->network_seat.index!=binding.index) return false;
    if (frontend_client_source_preinstall_current(o->physical,source,NULL)) return true;
    if (!frontend_client_source_metadata_read(o->physical,&held,NULL) ||
        !qa_application_client_associated(o->options.frontend->application,source)) return false;
    const qa_application_client_source *actual=&held.source;
    return source->descriptor==actual->descriptor && source->runtime==o->options.runtime &&
        source->runtime==actual->runtime && qa_net_client_id_equal(source->client,o->client) &&
        qa_net_client_id_equal(source->client,actual->client) && source->connection_epoch==o->epoch &&
        source->connection_epoch==actual->connection_epoch &&
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
    const char *text;bool explicit_command;
    if(!qa_console_forward_text(command,&text,&explicit_command,error))return QA_COMMAND_FAILED;
    if(explicit_command&&command->argc==1) {
        if(qa_q1_is_qw(o->options.protocol))return QA_COMMAND_HANDLED;
        text="\n";
    }
    return qa_network_q1_client_command(o->options.runtime,o->client,text,error)?
        QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
}
static qa_command_result command(void *context,const qa_command_invocation *invocation,qa_error *error)
{ frontend_network_q1_client *o=context; return o->options.configuration.command?
    o->options.configuration.command(o->options.configuration.context,invocation,error):QA_COMMAND_UNHANDLED; }
static bool allow(void *context,const qa_command_invocation *invocation)
{ frontend_network_q1_client *o=context; return !o->options.configuration.allow_command ||
    o->options.configuration.allow_command(o->options.configuration.context,invocation); }
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
static bool chat_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if (!parent(o) || !call || !call->argc || !call->argv || !o->client.owner ||
        qa_q1_chat_command_read(call->context.dialect,call->argv[0],true)==QA_Q1_CHAT_UNKNOWN ||
        !pending_invocation(o,call,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Chat requires its actual admitted Q1 CLIENT command");
    return forward(o,call,error)==QA_COMMAND_HANDLED && pending_invocation(o,call,error);
}
static bool bonus_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if (!parent(o) || !call || !qa_console_invocation_current(call->console,call) ||
        !pending_invocation(o,call,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 bonus lost its actual entered CLIENT command");
    /* Serverinfo clears CL state before the first received world. An early
     * startup command has no retained receiver or scene to flash yet. */
    if (!o->source) return true;
    frontend_remote_q1_source_view source;
    return frontend_remote_q1_source_read(o->source,&source,error) &&
        frontend_remote_q1_bonus(source.receiver,error) && pending_invocation(o,call,error);
}
static bool network_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if (!parent(o) || !call || !call->argc || !call->argv ||
        !qa_console_invocation_current(call->console,call) || !pending_invocation(o,call,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"NETWORK command lost its actual Q1 CLIENT invocation");
    qa_console *engine=qa_application_console(o->options.frontend->application);
    qa_command_context lookup={.origin=QA_COMMAND_LOCAL,.dialect=call->context.dialect,.direct=true};
    const qa_console_entry *entry=qa_console_find(engine,&lookup,call->argv[0]);
    uint64_t lifetime=0; qa_command_handler handler=NULL; void *user=NULL;
    if (!entry || !entry->engine_command ||
        !qa_console_registration_read(engine,entry->name,entry->owner,&lifetime,&handler,&user) ||
        lifetime!=QA_NETWORK_COMMAND_OWNER || !handler)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 CLIENT lost its declared NETWORK registration");
    return handler(user,call,error) && parent(o) && pending_invocation(o,call,error);
}
static bool install(void *context,const qa_application_client_source *source,bool restoring,qa_error *error)
{
    frontend_network_q1_client *o=context;
    if(!qa_console_register_owned(source->context.console,"centerview","Center the actual CLIENT view",
        source->context.receiver,source->context.receiver,true,center_command,o,error)) return false;
    if(!qa_console_register_owned(source->context.console,"bf","Flash the actual CLIENT bonus palette",
        source->context.receiver,source->context.receiver,true,bonus_command,o,error)) return false;
    if(o->options.configuration.install && !o->options.configuration.install(
        o->options.configuration.context,source,restoring,error)) return false;
    for (qa_q1_chat_mode mode=QA_Q1_CHAT_ALL;mode<QA_Q1_CHAT_UNKNOWN;++mode) {
        const char *name=qa_q1_chat_command_name(source->context.command.dialect,mode);
        if (name && !qa_console_register_owned(source->context.console,name,NULL,
            source->context.receiver,source->context.receiver,false,chat_command,o,error)) return false;
    }
    qa_console *engine=qa_application_console(o->options.frontend->application);
    for (size_t i=0;;++i) {
        const qa_console_entry *entry=qa_console_entry_at(engine,i);
        if (!entry) break;
        uint64_t lifetime=0; qa_command_handler handler=NULL; void *user=NULL;
        if (!entry->engine_command || !qa_console_registration_read(engine,entry->name,entry->owner,
            &lifetime,&handler,&user) || lifetime!=QA_NETWORK_COMMAND_OWNER || !handler) continue;
        if (!qa_console_register_owned(source->context.console,entry->name,entry->description,
            source->context.receiver,source->context.receiver,true,network_command,o,error)) return false;
    }
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
static bool demo_sample_seconds(void *context, const qa_application_client_source *source, double *out, qa_error *error)
{
    frontend_network_q1_client *o = context; (void)error;
    if (!out || !o->options.demo_playback || !connection(o, source)) return false;
    *out = o->demo_seconds; return true;
}
static frontend_remote_q1_source_options receiver_options(frontend_network_q1_client *o)
{ return (frontend_remote_q1_source_options){.physical=o->physical,.protocol=o->options.protocol,.context=o,
    .load_content=load_content,.service=received,.disconnected=disconnected,
    .skin_bindings=qa_q1_is_qw(o->options.protocol)?&o->skins:NULL,
    .sample_seconds=o->options.demo_playback ? demo_sample_seconds : NULL,
    .demo_forced_track=o->demo_forced_track}; }
static bool current_userinfo(frontend_network_q1_client *o,
    const frontend_client_source_view *physical,qa_buffer *out,qa_error *error)
{
    const size_t capacity=512;
    const char *language=NULL;
    qa_buffer info={0};
    if (!qa_cvars_info(physical->source.context.cvars,QA_CVAR_USERINFO,capacity,&info,error)) return false;
    if (!qa_ui_language_read(qa_application_cvars(o->options.frontend->application),
        physical->source.context.physical_seat,&language,error)) { qa_buffer_free(&info); return false; }
    uint8_t *data=realloc(info.data,capacity);
    if (!data) {
        qa_buffer_free(&info);
        return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual QW userinfo language");
    }
    info.data=data;
    if (!qa_q3_info_set((char *)info.data,capacity,"language",language,error)) {
        qa_buffer_free(&info); return false;
    }
    info.size=strlen((char *)info.data);
    *out=info; return true;
}
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
        bool ok=current_userinfo(o,&physical,&info,error);
        if(ok) {
            o->userinfo=(char *)info.data; info=(qa_buffer){0};
            ok=qa_qw_connect_create(o->options.qport,o->userinfo,&o->qw,error);
        }
        qa_buffer_free(&info); if(!ok) return false;
    } else if(!qa_nq_connect_create(&o->nq,error)) return false;
    o->attachment=(qa_net_connect){.attachment=o->options.demo_playback ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE,.endpoint=o->options.remote,
        .protocol=o->options.protocol,.seats=&o->binding,.seat_count=1,.composition=physical.source.configuration_generation};
    if (o->options.demo_playback) o->attachment.endpoint = (qa_net_address){.kind=QA_NET_LOOPBACK};
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
    *out=o; o->options=*options; o->demo_forced_track=-1;
    o->input_clock_ns=options->frontend->wall_time_ns;
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
            !current_userinfo(o,&physical,&info,error)) return false;
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
    return (parent(o) && o->admitting &&
        request->attachment==(o->options.demo_playback ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE) && request->seats &&
        request->seat_count==1 && request->seats[0].seat.owner==o->binding.seat.owner &&
        request->seats[0].seat.index==o->binding.seat.index && !request->seats[0].remote_index &&
        qa_net_address_equal(&request->endpoint,&o->attachment.endpoint,true) &&
        (request->composition == o->attachment.composition)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 attach differs from its actual reached Source claim");
}
static bool attach(frontend_network_q1_client *o,qa_error *error)
{
    if(o->client.owner || (!o->options.demo_playback && handshake(o).phase!=QA_Q1_CONNECT_CONNECTED) ||
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
        if (!o->options.demo_playback) o->attachment.endpoint.port=handshake(o).port;
    }
    policy.qport=o->options.qport;
    o->admitting=true;
    bool ok=o->options.demo_playback ?
        qa_network_attach_q1_demo(o->options.runtime,&o->attachment,&policy,&hooks,0,&o->client,error) :
        qa_network_attach_q1_client(o->options.runtime,&o->attachment,&policy,&hooks,o->now_ns,&o->client,error);
    o->admitting=false;
    if(!ok) return false;
    o->epoch=qa_network_epoch(o->options.runtime,o->client);
    if(!o->epoch || !frontend_remote_q1_source_bind(o->source,o->client,o->binding.seat,o->epoch,error)) return false;
    if (o->demo_follow.append) {
        frontend_remote_q1_source_view source; bool followed = false;
        if (!frontend_remote_q1_source_read(o->source, &source, error) ||
            !frontend_remote_q1_demo_attach(source.receiver, &o->demo_follow, &followed, error) || !followed) return false;
        o->demo_follow = (frontend_demo_sink){0};
    }
    if (!qa_network_q1_client_start(o->options.runtime,o->client,error)) return false;
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
    if(!recognized || !packet || !parent(o) || o->calls ||
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
    if(!o->options.demo_playback && !userinfo_sync(o,error)) return false;
    if(o->client.owner) {
        size_t executed=0;
        return qa_network_q1_client_continue(o->options.runtime,o->client,error) &&
            frontend_client_source_drain(o->physical,1024,&executed,error);
    }
    size_t executed=0;
    if(!frontend_client_source_drain(o->physical,1024,&executed,error)) return false;
    if(o->options.demo_playback || handshake(o).phase==QA_Q1_CONNECT_CONNECTED) return attach(o,error);
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
bool frontend_network_q1_client_disconnect(frontend_network_q1_client *o,const char *reason,qa_error *error)
{
    if (!parent(o) || !reason || o->options.frontend->capture ||
        o->options.frontend->resource_inventory || o->options.frontend->source_restoring ||
        !frontend_network_q1_client_idle(o) || !qa_network_callbacks_idle(o->options.runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 disconnect requires its returned current CLIENT owner");
    if (o->retired) return true;
    frontend_client_source_view physical;
    if (!frontend_client_source_read(o->physical,&physical,error)) return false;
    if (o->client.owner)
        return qa_network_q1_client_disconnect(o->options.runtime,o->client,reason,error);
    snprintf(o->reason,sizeof(o->reason),"%s",reason);
    o->retired=true;
    return true;
}
void frontend_network_q1_client_disconnected(frontend_network_q1_client *o,qa_net_client_id client)
{ if(o && qa_net_client_id_equal(o->client,client)) o->retired=true; }
bool frontend_network_q1_client_idle(const frontend_network_q1_client *o)
{ return !o || (!o->calls && !o->admitting && (!o->physical || frontend_client_source_idle(o->physical)) &&
    (!o->source || frontend_remote_q1_source_idle(o->source))); }
bool frontend_network_q1_client_destroy(frontend_network_q1_client **owned,qa_error *error)
{
    frontend_network_q1_client *o=owned?*owned:NULL; if(!o) return true;
    if(o->demo_follow.append || o->options.frontend->capture || o->options.frontend->resource_inventory ||
        !frontend_network_q1_client_idle(o) || !qa_network_callbacks_idle(o->options.runtime)) return false;
    if(o->client.owner && qa_net_connections_get(qa_network_connections(o->options.runtime),o->client) &&
        !qa_network_q1_client_disconnect(o->options.runtime,o->client,"Q1 CLIENT closed",error)) return false;
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
static bool demo_record_current(const void *context)
{
    const frontend_network_q1_client *o = context;
    return parent(o) && o->configured && !o->retired && !o->options.demo_playback && o->source;
}
static bool demo_seed(void *context, const frontend_demo_sink *sink, qa_error *error)
{
    frontend_network_q1_client *o = context; frontend_remote_q1_source_view source;
    return demo_record_current(o) && frontend_network_q1_client_source_read(o, &source, error) &&
        frontend_remote_q1_demo_seed(source.receiver, sink, o->options.policy.nq_options, error);
}
static bool demo_attach(void *context, const frontend_demo_sink *sink, bool *attached, qa_error *error)
{
    frontend_network_q1_client *o = context; frontend_remote_q1_source_view source;
    return demo_record_current(o) && frontend_network_q1_client_source_read(o, &source, error) &&
        frontend_remote_q1_demo_attach(source.receiver, sink, attached, error);
}
static bool demo_detach(void *context, const frontend_demo_sink *sink, qa_error *error)
{
    return frontend_network_q1_client_demo_unfollow(context, sink, error);
}
bool frontend_network_q1_client_demo_follow(frontend_network_q1_client *o,
    const frontend_demo_sink *sink, bool *attached, qa_error *error)
{
    if (!o || !sink || !sink->owner || !sink->append || !attached || !parent(o) ||
        o->retired || o->options.demo_playback || !frontend_network_q1_client_idle(o) ||
        !qa_network_callbacks_idle(o->options.runtime) || o->demo_follow.append) return false;
    if (o->client.owner) return demo_attach(o, sink, attached, error);
    frontend_client_source_view physical;
    if (!frontend_client_source_metadata_read(o->physical, &physical, error) ||
        physical.source.context.physical_seat != o->options.physical_seat) return false;
    o->demo_follow = *sink; *attached = true; return true;
}
bool frontend_network_q1_client_demo_unfollow(frontend_network_q1_client *o,
    const frontend_demo_sink *sink, qa_error *error)
{
    if (!o || !sink || o->closing || !frontend_network_q1_client_idle(o)) return false;
    if (o->demo_follow.append) {
        if (o->demo_follow.owner != sink->owner || o->demo_follow.append != sink->append)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Pending QW recorder differs from its actual sink");
        o->demo_follow = (frontend_demo_sink){0};
    }
    if (!o->source) return true;
    frontend_remote_q1_source_view source;
    return frontend_remote_q1_source_metadata_read(o->source, &source, error) &&
        frontend_remote_q1_demo_detach(source.receiver, sink, error);
}
static bool demo_record_release(void **owner, qa_error *error)
{
    (void)error;
    if (!owner) return false;
    *owner = NULL; return true;
}
bool frontend_network_q1_client_demo_record(frontend_network_q1_client *o,
    frontend_demo_record_source *out, qa_error *error)
{
    frontend_remote_q1_source_view source;
    if (!out || !demo_record_current(o) || !frontend_network_q1_client_source_read(o, &source, error)) return false;
    qa_fs_root *root = qa_catalog_product_write_root(source.domain.catalog, source.domain.product);
    if (!root) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 recording lacks its actual selected write directory");
    *out = (frontend_demo_record_source){.owner = o, .format = qa_q1_is_qw(o->options.protocol) ? FRONTEND_DEMO_QW : FRONTEND_DEMO_NQ,
        .protocol = o->options.protocol, .root = root, .forced_track = -1,
        .current = demo_record_current, .seed = demo_seed, .attach = demo_attach, .detach = demo_detach,
        .release = demo_record_release};
    return true;
}
static bool demo_playback_current(const void *context)
{
    const frontend_network_q1_client *o = context;
    return parent(o) && o->options.demo_playback && !o->closing;
}
static bool demo_playback_release(void **owner, qa_error *error)
{
    frontend_network_q1_client *o = owner ? *owner : NULL;
    if (!o) return true;
    if (!frontend_network_q1_client_disconnect(o, "Demo playback completed", error)) return false;
    *owner = NULL; return true;
}
static bool demo_advance(void *context, frontend_demo_reader *reader, uint64_t elapsed,
    uint64_t frame_number, bool timedemo, frontend_demo_end *end, qa_error *error)
{
    frontend_network_q1_client *o = context; (void)frame_number;
    if (!reader || !end || !demo_playback_current(o) || !frontend_network_q1_client_idle(o) ||
        !qa_network_callbacks_idle(o->options.runtime)) return false;
    *end = FRONTEND_DEMO_RUNNING;
    if (o->retired) { *end = FRONTEND_DEMO_DISCONNECTED; return true; }
    if (!frontend_network_q1_client_tick(o, o->options.frontend->wall_time_ns, error)) return false;
    if (!o->client.owner) return true;
    frontend_remote_q1_source_view source;
    qa_network_q1_client_state state;
    if (!frontend_network_q1_client_source_read(o, &source, error) ||
        !qa_network_q1_client_state_read(o->options.runtime, o->client, &state, error)) return false;
    if (o->demo_clock_started) o->demo_seconds += (double)elapsed / 1e9;
    size_t consumed = 0;
    bool packet_received = false;
    while (consumed < o->options.policy.service_limit) {
        if (!o->demo_has_pending) {
            bool present = false;
            if (!frontend_demo_read_next(reader, NULL, NULL, &o->demo_pending, &present, end, error)) return false;
            if (!present) return true;
            o->demo_has_pending = true;
        }
        const frontend_demo_packet *packet = &o->demo_pending;
        const bool qw = qa_q1_is_qw(o->options.protocol);
        if (packet->format != (qw ? FRONTEND_DEMO_QW : FRONTEND_DEMO_NQ))
            return frontend_fail(error, QA_ERROR_FORMAT, "Native Q1 playback record differs from its CLIENT protocol");
        frontend_remote_q1_view view;
        if (!frontend_remote_q1_read(source.receiver, &view, error)) return false;
        if (state.active && !o->demo_clock_started) {
            o->demo_seconds = qw ? packet->value.qw.seconds : view.seconds;
            o->demo_clock_started = true;
        }
        if (state.active && !timedemo && (qw ? packet->value.qw.seconds > o->demo_seconds : view.seconds > o->demo_seconds)) break;
        if (state.active && timedemo && packet_received) break;
        if (!qw) {
            if (!frontend_remote_q1_demo_angles(source.receiver, packet->value.nq.angles, error) ||
                !qa_network_q1_demo_packet(o->options.runtime, o->client, packet->value.nq.message,
                    o->options.frontend->wall_time_ns, error)) return false;
            packet_received = true;
        } else {
            const qa_qw_demo_record *record = &packet->value.qw;
            if (record->seconds < 0 || (double)record->seconds > (double)UINT64_MAX / 1e9)
                return frontend_fail(error, QA_ERROR_FORMAT, "QWD time exceeds its CLIENT clock");
            uint64_t ns = (uint64_t)((double)record->seconds * 1e9);
            switch (record->kind) {
            case QA_QW_DEMO_PACKET:
                if (!qa_network_q1_demo_packet(o->options.runtime, o->client, record->data.packet, ns, error)) return false;
                packet_received = true;
                break;
            case QA_QW_DEMO_SEQUENCES:
                if (!qa_network_q1_demo_sequences(o->options.runtime, o->client,
                    record->data.sequences.outgoing, record->data.sequences.incoming, error)) return false;
                break;
            case QA_QW_DEMO_COMMAND:
                if (!frontend_remote_q1_demo_angles(source.receiver, record->data.input.angles, error) ||
                    !qa_network_q1_demo_command(o->options.runtime, o->client, &record->data.input.command, ns, error)) return false;
                break;
            default: return frontend_fail(error, QA_ERROR_FORMAT, "Unknown native QWD record");
            }
        }
        o->demo_has_pending = false; ++consumed;
        if (o->retired) { *end = FRONTEND_DEMO_DISCONNECTED; return true; }
        if (!qa_network_q1_client_state_read(o->options.runtime, o->client, &state, error)) return false;
    }
    return frontend_remote_q1_sample(source.receiver, o->options.frontend->wall_time_ns, error);
}
bool frontend_network_q1_client_demo_playback(frontend_network_q1_client *o,
    frontend_demo_reader *reader, frontend_demo_playback_source *out, qa_error *error)
{
    if (!reader || !out || !demo_playback_current(o) || !frontend_network_q1_client_idle(o) || o->configured)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Demo playback requires its actual pending physical Q1 CLIENT");
    o->demo_forced_track = frontend_demo_forced_track(reader);
    *out = (frontend_demo_playback_source){.owner = o, .current = demo_playback_current,
        .advance = demo_advance, .release = demo_playback_release}; return true;
}
bool frontend_network_q1_client_frame_time(frontend_network_q1_client *o,
    const qa_cvars **cvars,uint64_t *source_ns,bool *handled,qa_error *error)
{
    if(!cvars || !source_ns || !handled) return false;
    *cvars=NULL; *source_ns=0; *handled=false;
    if(!o || o->options.demo_playback || o->retired) return true;
    qa_frontend *f=o->options.frontend;
    if(o->calls || o->admitting || f->capture || f->resource_inventory || f->source_restoring ||
        !qa_network_callbacks_idle(o->options.runtime) || !parent(o)) return false;
    o->input_frame_ns=o->input_wall_frame_ns=0;
    if(!o->configured) { o->input_clock_ns=f->wall_time_ns; return true; }
    frontend_client_source_view physical;
    if(!frontend_client_source_read(o->physical,&physical,error)) return false;
    if(!physical.ready) { o->input_clock_ns=f->wall_time_ns; return true; }
    if(!frontend_client_source_current(&physical) || f->wall_time_ns<o->input_clock_ns)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 CLIENT clock lost its actual physical Source or host boundary");
    uint64_t pending=f->wall_time_ns-o->input_clock_ns,frame;
    bool accepted;
    if(!qa_source_frame_time_admit(physical.source.context.cvars,pending,false,&accepted,&frame,error)) return false;
    if(accepted) {
        o->input_clock_ns=f->wall_time_ns;
        o->input_frame_ns=frame; o->input_wall_frame_ns=pending;
    }
    *cvars=physical.source.context.cvars; *source_ns=frame; *handled=true; return true;
}
bool frontend_network_q1_client_input_prepare(const frontend_network_q1_client *o,uint32_t physical,
    bool *accepted,uint64_t *source_ns,uint64_t *wall_ns,qa_error *error)
{
    if(!o || physical!=o->options.physical_seat || !accepted || !source_ns || !wall_ns ||
        !parent(o) || !frontend_network_q1_client_idle(o) ||
        o->options.frontend->capture || o->options.frontend->resource_inventory ||
        o->options.frontend->source_restoring || !qa_network_callbacks_idle(o->options.runtime))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 input admission requires its returned physical CLIENT");
    *accepted=o->input_wall_frame_ns!=0;
    *source_ns=o->input_frame_ns; *wall_ns=o->input_wall_frame_ns; return true;
}
bool frontend_network_q1_client_input(frontend_network_q1_client *o,uint32_t physical,
    const qa_seat_input_sample *sample,uint64_t sequence,double source_frame_ms,bool *handled,qa_error *error)
{
    if(!sample || !handled || !sequence) return false;
    *handled=o && physical==o->options.physical_seat;
    if(!*handled) return true;
    qa_frontend *f=o->options.frontend;
    if(o->calls || o->admitting || f->capture || f->resource_inventory || f->source_restoring ||
        !qa_network_callbacks_idle(o->options.runtime) || !parent(o)) return false;
    if(o->options.demo_playback || o->retired || !o->source || !o->client.owner || sequence<=o->input_sample) return true;
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
    if(!frontend_config_store_neutral_movement_adopt(f->config_store,source.physical.source.context.cvars,
            o->input.kind,error) ||
        !frontend_config_store_neutral_read(f->config_store,source.physical.source.context.cvars,&settings,error) ||
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
bool frontend_network_q1_client_qualified(const frontend_network_q1_client *o,const qa_network_runtime *runtime,
    bool complete,qa_error *error)
{
    frontend_client_source_view physical;
    if(!parent(o) || runtime!=o->options.runtime || !frontend_network_q1_client_idle(o) ||
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
    return !o || frontend_network_q1_client_qualified(o,o->options.runtime,true,error);
}
void frontend_network_q1_client_publish(frontend_network_q1_client *o)
{
    if(o) {
        o->input_clock_ns=o->options.frontend->wall_time_ns;
        o->input_frame_ns=o->input_wall_frame_ns=0;
    }
}
