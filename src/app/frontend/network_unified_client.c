#include "network_unified_client.h"
#include "remote_unified_presentation.h"
#include "remote_unified_save.h"
#include "internal.h"
#include "qa/source_frame_time.h"
#include "qa/game_domains.h"
#include "qa/application_character_selection.h"
#include "qa/application_native_q3_cvars.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_network_unified_client_service {
    frontend_network_unified_client_options options;
    frontend_client_source *physical;
    frontend_remote_unified *replica;
    qa_net_client_id client;
    uint64_t epoch;
    qa_buffer userinfo;
    bool binding, configuration_released, restored, published, retired;
};
static bool parent(const frontend_network_unified_client_service *o)
{ return o && o->options.current(o->options.context,o); }
static bool connection_tuple(const frontend_network_unified_client_service *o,
    const qa_application_client_source *source,bool absent_retired)
{
    if (!parent(o) || !source || source->runtime!=o->options.runtime ||
        source->context.physical_seat!=o->options.physical_seat) return false;
    if (!source->client.owner) return (!o->client.owner || o->binding) &&
        !source->client.slot && !source->client.generation && !source->connection_epoch &&
        !source->network_seat.owner && !source->network_seat.index;
    const qa_net_client *peer=qa_net_connections_get(qa_network_connections(o->options.runtime),source->client);
    if(!peer) return absent_retired&&o->retired&&
        qa_net_client_id_equal(source->client,o->client)&&source->connection_epoch==o->epoch&&o->epoch&&
        source->network_seat.owner==o->options.seat.owner&&source->network_seat.index==o->options.seat.index;
    return peer && qa_net_client_id_equal(source->client,o->client) &&
        source->connection_epoch==o->epoch && qa_network_epoch(o->options.runtime,o->client)==o->epoch &&
        source->network_seat.owner==o->options.seat.owner && source->network_seat.index==o->options.seat.index &&
        peer->protocol.kind==QA_NET_UNIFIED_1 && !peer->protocol.revision && !peer->protocol.flags &&
        peer->seat_count==1 && qa_net_client_owns_seat(peer,source->network_seat) &&
        !peer->seats[0].remote_index && qa_net_address_equal(&peer->endpoint,&o->options.remote,true);
}
static bool connection(void *context,const qa_application_client_source *source)
{
    frontend_network_unified_client_service *o=context;
    return o&&!o->retired&&connection_tuple(o,source,false);
}
static bool initialize(void *context,const qa_launch_instance *descriptor,qa_cvars *variables,
    const qa_command_context *command,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    if (!parent(o) || !descriptor || !variables || !command || !command->owner ||
        command->origin!=QA_COMMAND_SEAT || qa_cvars_dialect(variables)!=command->dialect ||
        !qa_source_frame_time_register(variables,command->owner,e)) return false;
    const uint32_t identity=QA_CVAR_ARCHIVE|QA_CVAR_USERINFO;
    const char *description="Unified CLIENT baseline";
    char name[64];
    if (!command->seat) memcpy(name,"Player",7);
    else snprintf(name,sizeof(name),"Player %" PRIu64,(uint64_t)command->seat+1);
    bool ok=true;
    switch (command->dialect) {
    case QA_CONSOLE_Q1:
        ok=qa_cvars_register(variables,"name",name,identity,command->owner,description,e) &&
            qa_cvars_register(variables,"color","0",QA_CVAR_ARCHIVE,command->owner,description,e) &&
            qa_cvars_register(variables,"password","",QA_CVAR_USERINFO,command->owner,description,e);
        break;
    case QA_CONSOLE_QW: {
        static const struct { const char *name,*value; uint32_t flags; } values[]={
            {"cl_hightrack","0",0},{"cl_chasecam","0",0},
            {"rate","25000",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"noskins","0",QA_CVAR_ARCHIVE},{"baseskin","base",QA_CVAR_ARCHIVE},
            {"name","unnamed",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"team","",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"skin","",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"topcolor","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"bottomcolor","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"noaim","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"msg","1",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"password","",QA_CVAR_USERINFO}};
        for (size_t i=0;ok && i<sizeof(values)/sizeof(*values);++i)
            ok=qa_cvars_register(variables,values[i].name,values[i].value,values[i].flags,command->owner,description,e);
        break;
    }
    case QA_CONSOLE_Q2: case QA_CONSOLE_Q2_RERELEASE: {
        const char *model=o->options.frontend->options.character_model;
        qa_native_q3_character_declaration defaults;
        if (!model) {
            if (!qa_native_q3_character_default_declaration(QA_GAME_Q2,&defaults,e)) return false;
            model=defaults.model;
        }
        size_t length=strlen(model);
        if (!length || length>SIZE_MAX-9)
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified Q2 CLIENT model declaration is invalid");
        const char *skin=!strcmp(model,"female")?"athena":!strcmp(model,"cyborg")?"oni911":"grunt";
        char *appearance=malloc(length+strlen(skin)+2);
        if (!appearance) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Unified Q2 CLIENT appearance");
        memcpy(appearance,model,length);appearance[length]='/';strcpy(appearance+length+1,skin);
        const struct { const char *name,*value; } values[]={
            {"name",name},{"skin",appearance},{"rate","15000"},{"msg","1"},{"hand","0"},
            {"fov","90"},{"gender",!strcmp(model,"female")?"female":"male"}};
        for (size_t i=0;ok && i<sizeof(values)/sizeof(*values);++i)
            ok=qa_cvars_register(variables,values[i].name,values[i].value,identity,command->owner,description,e);
        free(appearance);
        if (ok) ok=qa_cvars_register(variables,"password","",QA_CVAR_USERINFO,command->owner,description,e) &&
            qa_cvars_register(variables,"spectator","0",QA_CVAR_USERINFO,command->owner,description,e);
        break;
    }
    case QA_CONSOLE_Q3:
        ok=qa_native_q3_client_defaults(descriptor,variables,command,o->options.frontend->options.character_model,e);
        break;
    default: return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT has no admitted source dialect");
    }
    return ok && parent(o) && o->options.configuration.initialize(o->options.configuration.context,
        descriptor,variables,command,e) && parent(o);
}
static bool configure(void *context,const qa_application_client_source *source,bool *ready,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return parent(o) && o->options.configuration.configure(o->options.configuration.context,source,ready,e) && parent(o);
}
static bool configuration_advance(void *context,const qa_application_client_source *source,
    qa_application_client_preparation *preparation,bool *complete,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return parent(o)&&o->options.configuration.configuration_advance&&
        o->options.configuration.configuration_advance(o->options.configuration.context,source,preparation,complete,e)&&parent(o);
}
static bool install(void *context,const qa_application_client_source *source,bool restoring,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return parent(o) && (!o->options.configuration.install ||
        o->options.configuration.install(o->options.configuration.context,source,restoring,e)) && parent(o);
}
static void print(void *context,const qa_command_context *command,const char *text)
{
    frontend_network_unified_client_service *o=context;
    if (o->options.configuration.print) o->options.configuration.print(o->options.configuration.context,command,text);
    else frontend_console_print(o->options.frontend,command,text);
}
static qa_command_result command(void *context,const qa_command_invocation *call,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    if (!parent(o)) return QA_COMMAND_FAILED;
    return o->options.configuration.command?o->options.configuration.command(o->options.configuration.context,call,e):
        QA_COMMAND_UNHANDLED;
}
static qa_command_result forward(void *context,const qa_command_invocation *call,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    if (!parent(o) || !call || !call->argc || !call->argv) return QA_COMMAND_FAILED;
    if (o->options.configuration.forward) {
        qa_command_result result=o->options.configuration.forward(o->options.configuration.context,call,e);
        if (result!=QA_COMMAND_UNHANDLED) return result;
    }
    frontend_client_source_view source;
    if (!o->replica || !frontend_client_source_read(o->physical,&source,e)) return QA_COMMAND_UNHANDLED;
    if (!connection(o,&source.source) || call->console!=source.source.context.console) return QA_COMMAND_FAILED;
    return frontend_remote_unified_command(o->replica,call->argv[0],call->argv+1,call->argc-1,e)?
        QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
}
static bool allow(void *context,const qa_command_invocation *call)
{
    frontend_network_unified_client_service *o=context;
    return parent(o) && (!o->options.configuration.allow_command ||
        o->options.configuration.allow_command(o->options.configuration.context,call));
}
static bool script(void *context,const qa_command_context *origin,const char *path,qa_bytes *out,void **lease,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return o->options.configuration.read_script(o->options.configuration.context,origin,path,out,lease,e);
}
static void script_release(void *context,void *lease)
{
    frontend_network_unified_client_service *o=context;
    o->options.configuration.release_script(o->options.configuration.context,lease);
}
static void script_complete(void *context,const qa_command_context *origin,const char *path,bool success)
{
    frontend_network_unified_client_service *o=context;
    o->options.configuration.script_complete(o->options.configuration.context,origin,path,success);
}
static bool retire(void *context,const qa_application_client_source *source,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return !o->options.configuration.retire || o->options.configuration.retire(o->options.configuration.context,source,e);
}
static void released(void *context)
{
    frontend_network_unified_client_service *o=context;
    o->configuration_released=true;
    if (o->options.configuration.released) o->options.configuration.released(o->options.configuration.context);
}
static frontend_client_source_options physical_options(frontend_network_unified_client_service *o)
{
    frontend_client_source_options c=o->options.configuration;
    c.context=o; c.runtime=o->options.runtime; c.physical_seat=o->options.physical_seat;
    c.initialize=initialize; c.configure=configure; c.install=install; c.print=print;
    if(c.configuration_advance) c.configuration_advance=configuration_advance;
    c.connection_current=connection;c.retirement_current=frontend_network_unified_client_retirement_current;
    c.entity_current=NULL; c.command=command; c.forward=forward;
    c.allow_command=allow;
    if (c.read_script) { c.read_script=script; c.release_script=script_release; }
    if (c.script_complete) c.script_complete=script_complete;
    c.retire=retire; c.released=released;
    return c;
}
static bool same_command(const qa_command_context *a,const qa_command_context *b)
{
    return a->session==b->session && a->owner==b->owner && a->seat==b->seat && a->client==b->client &&
        a->dialect==b->dialect && a->origin==b->origin && a->direct==b->direct &&
        a->console_text==b->console_text && a->registry==b->registry && a->generation==b->generation &&
        qa_actor_id_equal(a->actor,b->actor) && !a->script && !b->script;
}
static bool domain_namespace(const frontend_network_unified_client_service *o,
    const frontend_remote_unified_domain *d,const frontend_client_source_view *v)
{
    return d&&d->application==o->options.frontend->application&&d->runtime==o->options.runtime&&
        d->seat.owner==o->options.seat.owner&&d->seat.index==o->options.seat.index&&
        d->physical_seat==o->options.physical_seat&&d->catalog==qa_launch_instance_catalog(v->source.descriptor)&&
        d->resources==qa_application_resources(d->application)&&d->console==v->source.context.console&&
        d->cvars==v->source.context.cvars&&same_command(&d->command_context,&v->source.context.command);
}
static bool domain_current(void *context,const frontend_remote_unified_domain *d,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    frontend_client_source_view v;
    if (!d || !parent(o)) return false;
    if(o->retired) {
        if(!frontend_client_source_metadata_read(o->physical,&v,e)||
            !qa_application_client_associated(o->options.frontend->application,&v.source)||
            !connection_tuple(o,&v.source,true)) return false;
    } else if(!frontend_client_source_read(o->physical,&v,e)) return false;
    if(!v.ready) return false;
    bool client=qa_net_client_id_equal(d->client,v.source.client);
    /* remote_bind first qualifies its still-pending stored domain, then the
     * bound candidate. Only that same actual replica may retain this cut. */
    if (!client && !d->client.owner && !d->client.slot && !d->client.generation && o->replica) {
        const frontend_remote_unified_domain *pending=frontend_remote_unified_domain_read(o->replica);
        client=d==pending && !pending->client.owner && connection(o,&v.source);
    }
    return client&&domain_namespace(o,d,&v);
}
static bool userinfo(void *context,const frontend_remote_unified_domain *d,const char **out,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    qa_buffer value={0};
    if (!out || !o || o->retired || !domain_current(o,d,e) || !qa_cvars_info(d->cvars,QA_CVAR_USERINFO,0,&value,e)) return false;
    if (!domain_current(o,d,e)) { qa_buffer_free(&value); return false; }
    qa_buffer_free(&o->userinfo); o->userinfo=value; *out=(const char *)o->userinfo.data; return true;
}
static bool disconnected(void *context,const frontend_remote_unified_domain *d,const char *reason,qa_error *e)
{
    frontend_network_unified_client_service *o=context; frontend_client_source_view v;
    return reason && domain_current(o,d,e) && frontend_client_source_read(o->physical,&v,e) &&
        o->options.disconnected(o->options.context,&v.source,reason,e)&&
        frontend_network_unified_client_request_retirement(o,&v.source,e);
}
bool frontend_network_unified_client_request_retirement(frontend_network_unified_client_service *o,
    const qa_application_client_source *source,qa_error *e)
{
    frontend_client_source_view physical;
    if(!parent(o)||!source||!o->client.owner||o->binding||
        !frontend_client_source_metadata_read(o->physical,&physical,e)||!physical.ready||
        !qa_application_client_associated(o->options.frontend->application,source)||
        source->runtime!=o->options.runtime||!qa_net_client_id_equal(source->client,o->client)||
        source->connection_epoch!=o->epoch||source->context.receiver!=physical.source.context.receiver||
        source->context.seat!=physical.source.context.seat||source->context.lifetime!=physical.source.context.lifetime||
        source->context.console!=physical.source.context.console||source->context.cvars!=physical.source.context.cvars||
        source->context.physical_seat!=o->options.physical_seat||
        source->network_seat.owner!=o->options.seat.owner||source->network_seat.index!=o->options.seat.index||
        !same_command(&source->context.command,&physical.source.context.command))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified retirement lost its retained physical CLIENT receipt");
    o->retired=true;return true;
}
bool frontend_network_unified_client_retired(const frontend_network_unified_client_service *o)
{ return o&&o->retired; }
bool frontend_network_unified_client_retirement_current(void *context,const qa_application_client_source *source)
{
    frontend_network_unified_client_service *o=context;
    return o&&o->retired&&connection_tuple(o,source,true);
}
static bool retirement(void *context,const frontend_remote_unified_domain *d,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    frontend_client_source_view physical;
    return parent(o)&&d&&frontend_client_source_metadata_read(o->physical,&physical,e)&&physical.ready&&
        qa_net_client_id_equal(d->client,physical.source.client)&&domain_namespace(o,d,&physical)&&
        frontend_network_unified_client_request_retirement(o,&physical.source,e);
}
static bool command_text(void *context,const frontend_remote_unified_domain *d,const char *text,qa_error *e)
{
    frontend_network_unified_client_service *o=context; frontend_client_source_view v;
    return text&&o&&!o->retired&&domain_current(o,d,e)&&frontend_client_source_read(o->physical,&v,e)&&
        frontend_network_unified_client_dispatch(o,&v.source,text,e)&&domain_current(o,d,e);
}
static bool source_command(void *context,const frontend_remote_unified_domain *d,const char *instance,
    uint64_t publication,uint64_t map_revision,const qa_command_context *origin,
    const qa_command_tokens *tokens,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    frontend_client_source_view physical;
    qa_unified_session *session=NULL,*after=NULL;
    if(!o||o->retired||!o->replica||d!=frontend_remote_unified_domain_read(o->replica)||
        !tokens||!tokens->values||!tokens->count||tokens->count>128||
        !domain_current(o,d,e)||!frontend_client_source_read(o->physical,&physical,e)||
        !physical.ready||!connection(o,&physical.source)||
        !frontend_remote_unified_presentation_source_command_current(o->replica,instance,
            publication,map_revision,origin,e)||
        !qa_unified_session_find(d->runtime,d->client,&session,e)) return false;
    const char *arguments[128];
    for(size_t i=0;i<tokens->count;++i) arguments[i]=tokens->values[i];
    const qa_unified_source_command command={.instance=instance,.publication=publication,
        .map_revision=map_revision,.arguments=arguments,.argument_count=tokens->count};
    return qa_unified_session_source_command(session,&command,e)&&
        domain_current(o,d,e)&&connection(o,&physical.source)&&
        frontend_remote_unified_presentation_source_command_current(o->replica,instance,
            publication,map_revision,origin,e)&&
        qa_unified_session_find(d->runtime,d->client,&after,e)&&after==session;
}
bool frontend_network_unified_client_restart_adopt(frontend_network_unified_client_service *o,
    const frontend_remote_unified_domain *d,uint64_t epoch,qa_error *e)
{
    frontend_client_source_view v;
    if(!o||o->retired||!d||!o->replica||d!=frontend_remote_unified_domain_read(o->replica)||
        !epoch||epoch!=qa_network_epoch(o->options.runtime,o->client)||
        !frontend_network_unified_client_idle(o)||!parent(o)||
        !frontend_client_source_metadata_read(o->physical,&v,e)||!v.ready||
        !qa_net_client_id_equal(d->client,o->client)||!qa_net_client_id_equal(v.source.client,o->client)||
        d->application!=o->options.frontend->application||d->runtime!=o->options.runtime||
        d->physical_seat!=o->options.physical_seat||d->seat.owner!=o->options.seat.owner||d->seat.index!=o->options.seat.index||
        d->catalog!=qa_launch_instance_catalog(v.source.descriptor)||d->resources!=qa_application_resources(d->application)||
        d->console!=v.source.context.console||d->cvars!=v.source.context.cvars||
        !same_command(&d->command_context,&v.source.context.command))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT restart lost its actual physical transport namespace");
    uint64_t previous=o->epoch;
    o->epoch=epoch;
    if(!frontend_client_source_epoch_adopt(o->physical,epoch,e)) {o->epoch=previous;return false;}
    return domain_current(o,d,e);
}
static bool transport_restart(void *context,const frontend_remote_unified_domain *d,uint64_t epoch,qa_error *e)
{ return frontend_network_unified_client_restart_adopt(context,d,epoch,e); }
bool frontend_network_unified_client_create(const frontend_network_unified_client_options *options,
    frontend_network_unified_client_service **out,qa_error *e)
{
    qa_frontend *f=options?options->frontend:NULL;
    if (!f || !f->application || !options->runtime || !options->current || !options->disconnected ||
        !out || *out || !options->seat.owner || !options->configuration.initialize || !options->configuration.configure ||
        options->physical_seat>=f->options.seats || !f->seats || !f->seats[options->physical_seat].input ||
        f->capture || f->resource_inventory || f->source_restoring)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT requires its real physical and configuration owners");
    qa_catalog *catalog=qa_application_catalog(f->application);
    const qa_product *profile=qa_catalog_product(catalog,options->profile);
    if (!profile || !profile->builtin || profile->program_kind!=QA_PROGRAM_BUILTIN ||
        profile->availability!=QA_CONTENT_INSTALLED || profile->family>QA_GAME_Q3)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT requires its genuine installed compiled profile");
    frontend_network_unified_client_service *o=calloc(1,sizeof(*o));
    if (!o) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Unified CLIENT services");
    *out=o; o->options=*options;
    if (!o->options.selected) o->options.selected=profile->id;
    qa_clock_kind clock=profile->family==QA_GAME_Q1?
        (profile->edition==QA_EDITION_QUAKEWORLD?QA_CLOCK_QUAKEWORLD:QA_CLOCK_NETQUAKE):
        profile->family==QA_GAME_Q2?(profile->edition==QA_EDITION_RERELEASE?QA_CLOCK_Q2_RERELEASE:QA_CLOCK_Q2_CLASSIC):QA_CLOCK_Q3;
    qa_vfs *prepared=NULL;
    if (!parent(o) || !qa_catalog_open(catalog,o->options.selected,&prepared,e)) return false;
    frontend_client_source_options source=physical_options(o);
    source.input_origin=qa_input_seat_context(f->seats[options->physical_seat].input);
    source.input_origin.owner=0; source.input_origin.actor=(qa_actor_id){0}; source.input_origin.client=0;
    source.input_origin.registry=source.input_origin.generation=0; source.input_origin.script=false;
    source.input_origin.dialect=qa_clock_console_dialect(clock);
    source.metadata=(qa_launch_client_metadata){.catalog=catalog,.profile=profile->id,.selected=o->options.selected,
        .prepared=prepared,.instance="remote-unified-client",.seat=source.input_origin.seat,.clock=clock};
    bool ok=frontend_client_source_create(f,&source,&o->physical,e);
    qa_vfs_destroy(prepared); return ok;
}
bool frontend_network_unified_client_advance(frontend_network_unified_client_service *o,bool *ready,qa_error *e)
{
    if(!ready||!parent(o)) return false;
    if(o->retired) {*ready=false;return true;}
    return frontend_client_source_advance(o->physical,ready,e);
}
bool frontend_network_unified_client_drain(frontend_network_unified_client_service *o,size_t budget,
    size_t *executed,qa_error *e)
{
    frontend_client_source_view v;
    if(!executed||!parent(o)) return false;
    if(o->retired) {*executed=0;return true;}
    return frontend_network_unified_client_source_read(o,&v,e) && v.ready &&
        frontend_client_source_drain(o->physical,budget,executed,e);
}
bool frontend_network_unified_client_source_read(const frontend_network_unified_client_service *o,
    frontend_client_source_view *out,qa_error *e)
{ return parent(o) && !o->retired && frontend_client_source_read(o->physical,out,e); }
bool frontend_network_unified_client_dispatch(frontend_network_unified_client_service *o,
    const qa_application_client_source *source,const char *text,qa_error *e)
{
    return parent(o)&&source&&text&&connection(o,source)&&
        frontend_client_source_remote(o->physical,source,text,e)&&parent(o)&&connection(o,source);
}
bool frontend_network_unified_client_metadata_read(const frontend_network_unified_client_service *o,
    frontend_network_unified_client_view *out,qa_error *e)
{
    frontend_client_source_view physical;
    if (!o || !out || o->binding || !frontend_client_source_metadata_read(o->physical,&physical,e)) return false;
    *out=(frontend_network_unified_client_view){.owner=o,.physical=physical,.remote=o->options.remote,
        .seat=o->options.seat,.profile=o->options.profile,.retired=o->retired};
    return true;
}
static void options_value(frontend_network_unified_client_service *o,const frontend_client_source_view *v,
    frontend_remote_unified_options *out)
{
    *out=(frontend_remote_unified_options){.domain={.application=o->options.frontend->application,
        .runtime=o->options.runtime,.client=v->source.client,.seat=o->options.seat,.physical_seat=o->options.physical_seat,
        .catalog=qa_launch_instance_catalog(v->source.descriptor),.resources=qa_application_resources(o->options.frontend->application),
        .console=v->source.context.console,.cvars=v->source.context.cvars,.frame_time=v->frame_time,
        .command_context=v->source.context.command},
        .context=o,.current=domain_current,.userinfo=userinfo,.disconnected=disconnected,.retirement=retirement,
        .command_text=command_text,.source_command=source_command,
        .transport_restart=transport_restart,.identity_capacity=65536};
}
bool frontend_network_unified_client_options_read(frontend_network_unified_client_service *o,
    frontend_remote_unified_options *out,qa_error *e)
{
    frontend_client_source_view v;
    if (!out || !frontend_network_unified_client_source_read(o,&v,e) || !v.ready)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT programme has not completed its real physical namespace");
    options_value(o,&v,out);return true;
}
bool frontend_network_unified_client_import_options_read(frontend_network_unified_client_service *o,
    frontend_remote_unified_options *out,qa_error *e)
{
    frontend_client_source_view v;
    if(!out||!parent(o)||!frontend_network_unified_client_idle(o)||
        !((o->restored&&o->options.frontend->source_restoring)||o->options.frontend->capture)||
        !frontend_client_source_metadata_read(o->physical,&v,e)||!v.ready||
        !qa_application_client_associated(o->options.frontend->application,&v.source)||
        !qa_net_client_id_equal(v.source.client,o->client)||v.source.connection_epoch!=o->epoch)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified retained options require their actual capture or import owner");
    frontend_remote_unified_options value;
    options_value(o,&v,&value);
    if(!domain_current(o,&value.domain,e)) return false;
    *out=value;
    return true;
}
bool frontend_network_unified_client_retirement_options_read(frontend_network_unified_client_service *o,
    frontend_remote_unified_options *out,qa_error *e)
{
    frontend_client_source_view physical;
    if(!out||!parent(o)||!o->retired||!frontend_network_unified_client_idle(o)||
        !frontend_client_source_metadata_read(o->physical,&physical,e)||!physical.ready||
        !qa_application_client_retirement_current(o->options.frontend->application,&physical.source))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified retained options lost their actual retired physical CLIENT custody");
    frontend_remote_unified_options value;
    options_value(o,&physical,&value);
    if(!domain_current(o,&value.domain,e)) return false;
    *out=value;
    return true;
}
bool frontend_network_unified_client_bind(frontend_network_unified_client_service *o,qa_net_client_id client,
    qa_net_seat_id seat,frontend_remote_unified *replica,qa_error *e)
{
    if (!parent(o) || o->retired || !replica || !client.owner || !client.generation ||
        seat.owner!=o->options.seat.owner || seat.index!=o->options.seat.index || !frontend_network_unified_client_idle(o))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT bind requires its genuine pending physical owner");
    uint64_t epoch=qa_network_epoch(o->options.runtime,client);
    if (!epoch) return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT attach has no real connection epoch");
    if (o->client.owner) {
        frontend_client_source_view actual;
        if (o->replica || !qa_net_client_id_equal(client,o->client) || epoch!=o->epoch ||
            !frontend_client_source_read(o->physical,&actual,e) || !actual.ready || !connection(o,&actual.source))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Restored Unified CLIENT differs from its actual retained attach");
        o->replica=replica; return true;
    }
    o->client=client; o->epoch=epoch; o->binding=true;
    bool ok=frontend_client_source_bind(o->physical,client,seat,epoch,e);
    o->binding=false;
    if (!ok) { o->client=(qa_net_client_id){0}; o->epoch=0; return false; }
    o->replica=replica; return true;
}
bool frontend_network_unified_client_idle(const frontend_network_unified_client_service *o)
{ return !o || (!o->binding && (!o->physical || frontend_client_source_idle(o->physical))); }
bool frontend_network_unified_client_bind_restored(frontend_network_unified_client_service *o,
    qa_net_client_id client,qa_net_seat_id seat,frontend_remote_unified *replica,qa_error *e)
{
    frontend_client_source_view physical;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(replica);
    if(!parent(o)||!o->restored||!o->options.frontend->source_restoring||!replica||
        (o->replica&&o->replica!=replica)||!frontend_network_unified_client_idle(o)||
        !qa_net_client_id_equal(client,o->client)||!client.owner||
        seat.owner!=o->options.seat.owner||seat.index!=o->options.seat.index||
        !frontend_client_source_metadata_read(o->physical,&physical,e)||!physical.ready||
        !qa_net_client_id_equal(physical.source.client,client)||physical.source.connection_epoch!=o->epoch||
        !domain_current(o,domain,e)||!frontend_remote_unified_checkpoint_current(replica,e))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Restored Unified bind changed its retained CLIENT and replica graph");
    o->replica=replica;
    return true;
}
bool frontend_network_unified_client_publication_ready(const frontend_network_unified_client_service *o,qa_error *e)
{
    if(!o) return true;
    frontend_client_source_view physical;
    return o->restored&&frontend_network_unified_client_idle(o)&&parent(o)&&
        frontend_client_source_restore_finished(o->physical)&&
        frontend_client_source_metadata_read(o->physical,&physical,e)&&
        physical.source.runtime==o->options.runtime&&physical.source.context.physical_seat==o->options.physical_seat&&
        qa_net_client_id_equal(physical.source.client,o->client)&&physical.source.connection_epoch==o->epoch&&
        (o->retired?qa_application_client_associated(o->options.frontend->application,&physical.source)&&
            connection_tuple(o,&physical.source,true):connection((void *)o,&physical.source));
}
void frontend_network_unified_client_publish(frontend_network_unified_client_service *o)
{ if(o) o->published=true; }

bool frontend_network_unified_client_destroy(frontend_network_unified_client_service **owned,qa_error *e)
{
    frontend_network_unified_client_service *o=owned?*owned:NULL;
    if (!o) return true;
    if (!frontend_network_unified_client_idle(o) || !frontend_client_source_destroy(&o->physical,e)) return false;
    if (!o->configuration_released && o->options.configuration.released)
        o->options.configuration.released(o->options.configuration.context);
    qa_buffer_free(&o->userinfo); free(o); *owned=NULL; return true;
}
