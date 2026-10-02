#include "network_unified_client.h"
#include "internal.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct frontend_network_unified_client_service {
    frontend_network_unified_client_options options;
    frontend_client_source *physical;
    frontend_remote_unified *replica;
    qa_net_client_id client;
    uint64_t epoch;
    qa_buffer userinfo;
    bool binding, configuration_released;
};
static bool parent(const frontend_network_unified_client_service *o)
{ return o && o->options.current(o->options.context,o); }
static bool connection(void *context,const qa_application_client_source *source)
{
    frontend_network_unified_client_service *o=context;
    if (!parent(o) || !source || source->runtime!=o->options.runtime ||
        source->context.physical_seat!=o->options.physical_seat) return false;
    if (!source->client.owner) return (!o->client.owner || o->binding) &&
        !source->client.slot && !source->client.generation && !source->connection_epoch &&
        !source->network_seat.owner && !source->network_seat.index;
    const qa_net_client *peer=qa_net_connections_get(qa_network_connections(o->options.runtime),source->client);
    return peer && qa_net_client_id_equal(source->client,o->client) &&
        source->connection_epoch==o->epoch && qa_network_epoch(o->options.runtime,o->client)==o->epoch &&
        source->network_seat.owner==o->options.seat.owner && source->network_seat.index==o->options.seat.index &&
        peer->protocol.kind==QA_NET_UNIFIED_1 && !peer->protocol.revision && !peer->protocol.flags &&
        peer->seat_count==1 && qa_net_client_owns_seat(peer,source->network_seat) &&
        !peer->seats[0].remote_index && qa_net_address_equal(&peer->endpoint,&o->options.remote,true);
}
static bool initialize(void *context,const qa_launch_instance *descriptor,qa_cvars *variables,
    const qa_command_context *command,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return parent(o) && o->options.configuration.initialize(o->options.configuration.context,
        descriptor,variables,command,e) && parent(o);
}
static bool configure(void *context,const qa_application_client_source *source,bool *ready,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return parent(o) && o->options.configuration.configure(o->options.configuration.context,source,ready,e) && parent(o);
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
static qa_cvars *route(void *context,const qa_command_context *origin,const char *name)
{
    frontend_network_unified_client_service *o=context;
    return o->options.configuration.cvar_owner(o->options.configuration.context,origin,name);
}
static qa_cvars *visible(void *context,const qa_command_context *origin,size_t index)
{
    frontend_network_unified_client_service *o=context;
    return o->options.configuration.visible_cvars(o->options.configuration.context,origin,index);
}
static bool edit(void *context,const qa_command_context *origin,qa_cvars *variables,struct qa_cvars_edit **out,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    return o->options.configuration.cvar_edit(o->options.configuration.context,origin,variables,out,e);
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
    c.connection_current=connection; c.entity_current=NULL; c.command=command; c.forward=forward;
    c.allow_command=allow;
    if (c.cvar_owner) c.cvar_owner=route;
    if (c.visible_cvars) c.visible_cvars=visible;
    if (c.cvar_edit) c.cvar_edit=edit;
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
static bool domain_current(void *context,const frontend_remote_unified_domain *d,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    frontend_client_source_view v;
    if (!d || !parent(o) || !frontend_client_source_read(o->physical,&v,e) || !v.ready) return false;
    bool client=qa_net_client_id_equal(d->client,v.source.client);
    /* remote_bind first qualifies its still-pending stored domain, then the
     * bound candidate. Only that same actual replica may retain this cut. */
    if (!client && !d->client.owner && !d->client.slot && !d->client.generation && o->replica) {
        const frontend_remote_unified_domain *pending=frontend_remote_unified_domain_read(o->replica);
        client=d==pending && !pending->client.owner && connection(o,&v.source);
    }
    return client && d->application==o->options.frontend->application && d->runtime==o->options.runtime &&
        d->seat.owner==o->options.seat.owner && d->seat.index==o->options.seat.index &&
        d->physical_seat==o->options.physical_seat && d->catalog==qa_launch_instance_catalog(v.source.descriptor) &&
        d->resources==qa_application_resources(d->application) && d->console==v.source.context.console &&
        d->cvars==v.source.context.cvars && same_command(&d->command_context,&v.source.context.command);
}
static bool userinfo(void *context,const frontend_remote_unified_domain *d,const char **out,qa_error *e)
{
    frontend_network_unified_client_service *o=context;
    qa_buffer value={0};
    if (!out || !domain_current(o,d,e) || !qa_cvars_info(d->cvars,QA_CVAR_USERINFO,0,&value,e)) return false;
    if (!domain_current(o,d,e)) { qa_buffer_free(&value); return false; }
    qa_buffer_free(&o->userinfo); o->userinfo=value; *out=(const char *)o->userinfo.data; return true;
}
static bool disconnected(void *context,const frontend_remote_unified_domain *d,const char *reason,qa_error *e)
{
    frontend_network_unified_client_service *o=context; frontend_client_source_view v;
    return reason && domain_current(o,d,e) && frontend_client_source_read(o->physical,&v,e) &&
        o->options.disconnected(o->options.context,&v.source,reason,e);
}
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
        profile->availability!=QA_CONTENT_INSTALLED || profile->family>QA_GAME_Q2)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT requires its genuine installed compiled profile");
    frontend_network_unified_client_service *o=calloc(1,sizeof(*o));
    if (!o) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Unified CLIENT services");
    *out=o; o->options=*options;
    qa_clock_kind clock=profile->family==QA_GAME_Q1?
        (profile->edition==QA_EDITION_QUAKEWORLD?QA_CLOCK_QUAKEWORLD:QA_CLOCK_NETQUAKE):
        profile->family==QA_GAME_Q2?(profile->edition==QA_EDITION_RERELEASE?QA_CLOCK_Q2_RERELEASE:QA_CLOCK_Q2_CLASSIC):QA_CLOCK_Q3;
    qa_vfs *prepared=NULL;
    if (!parent(o) || !qa_catalog_open(catalog,profile->id,&prepared,e)) return false;
    frontend_client_source_options source=physical_options(o);
    source.input_origin=qa_input_seat_context(f->seats[options->physical_seat].input);
    source.input_origin.owner=0; source.input_origin.actor=(qa_actor_id){0}; source.input_origin.client=0;
    source.input_origin.registry=source.input_origin.generation=0; source.input_origin.script=false;
    source.input_origin.dialect=(qa_console_dialect)clock;
    source.metadata=(qa_launch_client_metadata){.catalog=catalog,.profile=profile->id,.selected=profile->id,
        .prepared=prepared,.instance="remote-unified-client",.seat=source.input_origin.seat,.clock=clock};
    bool ok=frontend_client_source_create(f,&source,&o->physical,e);
    qa_vfs_destroy(prepared); return ok;
}
bool frontend_network_unified_client_advance(frontend_network_unified_client_service *o,bool *ready,qa_error *e)
{ return parent(o) && frontend_client_source_advance(o->physical,ready,e); }
bool frontend_network_unified_client_drain(frontend_network_unified_client_service *o,size_t budget,
    size_t *executed,qa_error *e)
{
    frontend_client_source_view v;
    return frontend_network_unified_client_source_read(o,&v,e) && v.ready &&
        frontend_client_source_drain(o->physical,budget,executed,e);
}
bool frontend_network_unified_client_source_read(const frontend_network_unified_client_service *o,
    frontend_client_source_view *out,qa_error *e)
{ return parent(o) && frontend_client_source_read(o->physical,out,e); }
bool frontend_network_unified_client_metadata_read(const frontend_network_unified_client_service *o,
    frontend_network_unified_client_view *out,qa_error *e)
{
    frontend_client_source_view physical;
    if (!o || !out || o->binding || !frontend_client_source_metadata_read(o->physical,&physical,e)) return false;
    *out=(frontend_network_unified_client_view){o,physical,o->options.remote,o->options.seat,o->options.profile};
    return true;
}
bool frontend_network_unified_client_options_read(frontend_network_unified_client_service *o,
    frontend_remote_unified_options *out,qa_error *e)
{
    frontend_client_source_view v;
    if (!out || !frontend_network_unified_client_source_read(o,&v,e) || !v.ready)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT programme has not completed its real physical namespace");
    *out=(frontend_remote_unified_options){.domain={.application=o->options.frontend->application,
        .runtime=o->options.runtime,.client=v.source.client,.seat=o->options.seat,.physical_seat=o->options.physical_seat,
        .catalog=qa_launch_instance_catalog(v.source.descriptor),.resources=qa_application_resources(o->options.frontend->application),
        .console=v.source.context.console,.cvars=v.source.context.cvars,.command_context=v.source.context.command},
        .context=o,.current=domain_current,.userinfo=userinfo,.disconnected=disconnected,.identity_capacity=65536};
    return true;
}
bool frontend_network_unified_client_bind(frontend_network_unified_client_service *o,qa_net_client_id client,
    qa_net_seat_id seat,frontend_remote_unified *replica,qa_error *e)
{
    if (!parent(o) || !replica || !client.owner || !client.generation ||
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
bool frontend_network_unified_client_destroy(frontend_network_unified_client_service **owned,qa_error *e)
{
    frontend_network_unified_client_service *o=owned?*owned:NULL;
    if (!o) return true;
    if (!frontend_network_unified_client_idle(o) || !frontend_client_source_destroy(&o->physical,e)) return false;
    if (!o->configuration_released && o->options.configuration.released)
        o->options.configuration.released(o->options.configuration.context);
    qa_buffer_free(&o->userinfo); free(o); *owned=NULL; return true;
}
static bool capsule_fields(qa_source_save_io *io,qa_net_address *remote,qa_net_seat_id *seat,
    uint32_t *physical,qa_buffer *encoded,qa_bytes *decoded)
{
    uint8_t magic[8]={'Q','U','S','C',1,0,0,0};
    const uint8_t expected[8]={'Q','U','S','C',1,0,0,0};
    uint32_t kind=(uint32_t)remote->kind;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bytes(io,magic,8) || memcmp(magic,expected,8) ||
        !qa_source_save_u32(io,&kind) || !qa_source_save_u16(io,&remote->port)) return false;
    remote->kind=(qa_net_address_kind)kind;
    if (remote->kind==QA_NET_IPV4) {
        if (!qa_source_save_bytes(io,remote->host.ipv4,4)) return false;
    } else if (remote->kind==QA_NET_IPV6) {
        if (!qa_source_save_bytes(io,remote->host.ipv6.bytes,16) ||
            !qa_source_save_u32(io,&remote->host.ipv6.scope)) return false;
    } else if (remote->kind==QA_NET_LOOPBACK) {
        size_t length=0;
        if (!reading) {
            const char *end=memchr(remote->host.loopback,0,sizeof(remote->host.loopback));
            if (!end) return false;
            length=(size_t)(end-remote->host.loopback)+1;
        }
        if (!qa_source_save_count(io,&length,sizeof(remote->host.loopback)) || length<2 ||
            !qa_source_save_bytes(io,remote->host.loopback,length) || remote->host.loopback[length-1] ||
            memchr(remote->host.loopback,0,length-1)) return false;
    } else return false;
    if (!qa_source_save_u64(io,&seat->owner) || !seat->owner ||
        !qa_source_save_u32(io,&seat->index) || !qa_source_save_u32(io,physical)) return false;
    size_t length=reading?0:encoded->size;
    if (!qa_source_save_count(io,&length,SIZE_MAX) || !length) return false;
    if (!reading) return qa_source_save_bytes(io,encoded->data,length);
    if (io->offset>io->input.size || length>io->input.size-io->offset) return false;
    *decoded=(qa_bytes){io->input.data+io->offset,length}; io->offset+=length; return true;
}
bool frontend_network_unified_client_checkpoint(frontend_network_unified_client_service *o,
    const qa_application_content_graph *graph,qa_buffer *out,qa_error *e)
{
    if (!o || !out || out->data || out->size || !frontend_network_unified_client_idle(o)) return false;
    qa_buffer physical={0}; qa_source_save_io io={0};
    qa_net_address remote=o->options.remote; qa_net_seat_id seat=o->options.seat;
    uint32_t ordinal=o->options.physical_seat;
    bool ok=frontend_client_source_checkpoint(o->physical,graph,&physical,e) &&
        qa_source_save_writer(&io,NULL,e) && capsule_fields(&io,&remote,&seat,&ordinal,&physical,NULL) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&physical); return ok;
}
static bool capsule_read(qa_bytes bytes,qa_net_address *remote,qa_net_seat_id *seat,
    uint32_t *physical,qa_bytes *prefix,qa_error *e)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) &&
        capsule_fields(&io,remote,seat,physical,NULL,prefix) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_network_unified_client_saved_read(qa_frontend *f,qa_application_content_graph *graph,qa_bytes bytes,
    frontend_client_source_prefix *out,qa_net_address *remote,qa_net_seat_id *seat,qa_error *e)
{
    qa_bytes prefix={0}; uint32_t physical=0; qa_net_address address={0}; qa_net_seat_id binding={0};
    if (!f || !out || !remote || !seat || !capsule_read(bytes,&address,&binding,&physical,&prefix,e) ||
        !frontend_client_source_prefix_read(f,graph,prefix,out,e)) return false;
    if (out->state.application.physical_seat!=physical || (out->state.application.client.owner &&
        (out->state.application.network_seat.owner!=binding.owner || out->state.application.network_seat.index!=binding.index))) {
        frontend_client_source_prefix_free(out);
        return frontend_fail(e,QA_ERROR_FORMAT,"Unified CLIENT capsule changes its actual physical seat");
    }
    *remote=address; *seat=binding; return true;
}
bool frontend_network_unified_client_restore(const frontend_network_unified_client_options *options,
    qa_application_content_graph *graph,const qa_console_save_resolvers *resolvers,qa_bytes bytes,
    frontend_network_unified_client_service **out,qa_error *e)
{
    qa_frontend *f=options?options->frontend:NULL;
    if (!f || !f->application || !f->source_restoring || f->capture || f->resource_inventory ||
        !options->runtime || !options->current || !options->disconnected || !options->seat.owner ||
        !graph || !resolvers || !out || *out || options->physical_seat>=f->options.seats)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT import requires its actual cold graph and transport owners");
    frontend_network_unified_client_service *o=calloc(1,sizeof(*o));
    if (!o) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining restored Unified CLIENT services");
    *out=o; o->options=*options;
    frontend_client_source_prefix prefix={0}; qa_net_address remote={0}; qa_net_seat_id seat={0};
    qa_bytes physical={0}; uint32_t ordinal=0;
    bool ok=parent(o) && capsule_read(bytes,&remote,&seat,&ordinal,&physical,e) &&
        ordinal==options->physical_seat && seat.owner==options->seat.owner && seat.index==options->seat.index &&
        qa_net_address_equal(&remote,&options->remote,true) && frontend_client_source_prefix_read(f,graph,physical,&prefix,e);
    if (ok) ok=prefix.recipe.profile==options->profile && prefix.recipe.selected==options->profile &&
        prefix.state.application.physical_seat==options->physical_seat &&
        (!prefix.state.application.client.owner ||
            (prefix.state.application.network_seat.owner==options->seat.owner &&
             prefix.state.application.network_seat.index==options->seat.index));
    if (ok) {
        o->client=prefix.state.application.client; o->epoch=prefix.state.application.connection_epoch;
        frontend_client_source_options source=physical_options(o);
        source.metadata=prefix.recipe; source.input_origin=prefix.state.command;
        ok=frontend_client_source_restore_prefix(f,&source,graph,resolvers,physical,&o->physical,e);
    }
    frontend_client_source_prefix_free(&prefix);
    if (!ok && e && e->code==QA_OK) frontend_fail(e,QA_ERROR_FORMAT,"Saved Unified CLIENT differs from its retained physical namespace");
    return ok;
}
