#include "remote_q1_source.h"
#include "remote_q1_private.h"
#include "internal.h"
#include "legacy_render_policy.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

bool frontend_remote_q1_source_defaults(qa_cvars *cvars,uint64_t owner,uint32_t seat,qa_error *error)
{
    if (!cvars || !owner || qa_cvars_dialect(cvars)>QA_CONSOLE_QW) return false;
    if (!frontend_legacy_source_register(cvars, qa_cvars_dialect(cvars), owner, error)) return false;
    if (qa_cvars_dialect(cvars)==QA_CONSOLE_Q1) {
        char name[48]; if (seat==0) snprintf(name,sizeof(name),"Player");
        else snprintf(name,sizeof(name),"Player %llu",(unsigned long long)seat+1);
        return qa_cvars_register(cvars,"name",name,QA_CVAR_ARCHIVE,owner,"",error) &&
            qa_cvars_register(cvars,"color","0",QA_CVAR_ARCHIVE,owner,"",error) &&
            qa_input_settings_register(cvars,QA_MOVEMENT_NETQUAKE,error);
    }
    static const struct { const char *name,*value; uint32_t flags; } settings[]={
        {"cl_hightrack","0",0},{"cl_chasecam","0",0},
        {"rate","25000",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"noskins","0",QA_CVAR_ARCHIVE},{"baseskin","base",QA_CVAR_ARCHIVE},
        {"name","unnamed",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"team","",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"skin","",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"topcolor","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"bottomcolor","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"noaim","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
        {"msg","1",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"password","",QA_CVAR_USERINFO},
        {"spectator","",QA_CVAR_USERINFO}};
    for (size_t i=0;i<sizeof(settings)/sizeof(*settings);++i)
        if (!qa_cvars_register(cvars,settings[i].name,settings[i].value,settings[i].flags,owner,"",error)) return false;
    return qa_input_settings_register(cvars,QA_MOVEMENT_QUAKEWORLD,error);
}
bool frontend_remote_q1_skin_recipe(qa_catalog *catalog,qa_product_id selected,
    qa_vfs **view,qa_fs_root **root,qa_error *error)
{
    if (!catalog) return false;
    const qa_product *product=qa_catalog_product(catalog,selected),*base=qa_catalog_find(catalog,"q1-quakeworld");
    if (!catalog || !product || !base || !view || *view || !root || *root || product->family!=QA_GAME_Q1 ||
        product->edition!=QA_EDITION_QUAKEWORLD || base->family!=QA_GAME_Q1 || base->edition!=QA_EDITION_QUAKEWORLD)
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW skins require their actual selected catalog and shared base");
    const qa_product *ancestor=product; bool linked=false;
    for (size_t i=0;ancestor && i<=qa_catalog_count(catalog);++i) {
        if (ancestor->id==base->id) { linked=true; break; }
        ancestor=qa_catalog_product(catalog,qa_catalog_configuration_base(catalog,ancestor->id));
    }
    qa_fs_root *actual=linked?qa_catalog_product_write_root(catalog,base->id):NULL;
    if (!actual || !qa_catalog_open(catalog,selected,view,error)) return false;
    bool found=false;
    for (size_t i=0;i<qa_vfs_mount_count(*view);++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(*view,i,&mount) && !mount.is_archive && mount.writable &&
            qa_fs_root_same_object(actual,qa_vfs_mount_root(*view,mount.id))) found=true;
    }
    if (!found) { qa_vfs_destroy(*view); *view=NULL;
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"QW selected read view omits its actual shared skin write directory"); }
    *root=actual; return true;
}

struct frontend_remote_q1_source {
    qa_frontend *frontend;
    frontend_remote_q1_source_options options;
    frontend_remote_q1 *receiver;
    unsigned calls;
    bool retained, closing;
};
static frontend_remote_q1_domain domain(const frontend_remote_q1_source *owner,
    const qa_application_client_source *source)
{
    return (frontend_remote_q1_domain){.application = owner->frontend->application,
        .runtime = source->runtime, .client = source->client, .seat = source->network_seat,
        .epoch = source->connection_epoch, .configuration_generation = source->configuration_generation,
        .physical_seat = source->context.physical_seat, .protocol = owner->options.protocol,
        .catalog = qa_launch_instance_catalog(source->descriptor), .product = source->descriptor->selection.product,
        .console = source->context.console, .cvars = source->context.cvars, .command_context = source->context.command,
        .actors = qa_session_actor_registry(source->context.session), .actor_owner = source->context.entity_owner,
        .actor_definition = source->context.entity_definition};
}
static bool physical(frontend_remote_q1_source *owner, const frontend_remote_q1_domain *expected,
    frontend_client_source_view *out, qa_error *error)
{
    if (!owner || owner->closing || !owner->retained || owner->calls == UINT_MAX ||
        !frontend_client_source_read(owner->options.physical, out, error) || !out->ready) return false;
    frontend_remote_q1_domain actual = domain(owner, &out->source);
    return remote_q1_domain_equal(&actual, expected);
}
static bool current(void *context, const frontend_remote_q1_domain *expected, qa_error *error)
{ frontend_client_source_view view; return physical(context, expected, &view, error); }
static bool application_read(void *context,const frontend_remote_q1_domain *expected,
    qa_application_client_source *out,qa_error *error)
{
    frontend_client_source_view view;
    if(!out || !physical(context,expected,&view,error)) return false;
    *out=view.source; return true;
}
static bool application_metadata_read(void *context,const frontend_remote_q1_domain *expected,
    qa_application_client_source *out,qa_error *error)
{
    frontend_remote_q1_source *owner=context; frontend_client_source_view view;
    if(!owner || !out || !owner->retained || owner->closing || owner->frontend->application!=expected->application ||
        !frontend_client_source_metadata_read(owner->options.physical,&view,error) || !view.ready) return false;
    frontend_remote_q1_domain actual=domain(owner,&view.source);
    if(!remote_q1_domain_equal(&actual,expected)) return false;
    *out=view.source; return true;
}
static bool load(void *context, const frontend_remote_q1_domain *expected, const qa_nq_serverinfo *info,
    const qa_qw_serverdata *qw, frontend_remote_q1_content *out, qa_error *error)
{
    frontend_remote_q1_source *owner = context; frontend_client_source_view view;
    if (!physical(owner, expected, &view, error)) return false;
    ++owner->calls;
    bool ok = owner->options.load_content(owner->options.context, &view.source, info, qw, out, error);
    --owner->calls;
    return ok && physical(owner, expected, &view, error);
}
static bool service(void *context, const frontend_remote_q1_domain *expected, qa_net_protocol_id protocol,
    const qa_nq_message *message, double seconds, uint64_t sequence, qa_error *error)
{
    frontend_remote_q1_source *owner = context; frontend_client_source_view view;
    if (!physical(owner, expected, &view, error)) return false;
    ++owner->calls;
    bool ok = owner->options.service(owner->options.context, &view.source, protocol, message, seconds, sequence, error);
    --owner->calls;
    return ok && physical(owner, expected, &view, error);
}
static bool disconnected(void *context, const frontend_remote_q1_domain *expected, const char *reason, qa_error *error)
{
    frontend_remote_q1_source *owner = context; frontend_client_source_view view;
    if (!physical(owner, expected, &view, error)) return false;
    ++owner->calls;
    bool ok = owner->options.disconnected(owner->options.context, &view.source, reason, error);
    --owner->calls; return ok;
}
static bool sample_seconds(void *context, const frontend_remote_q1_domain *expected, double *out, qa_error *error)
{
    frontend_remote_q1_source *owner = context; frontend_client_source_view view;
    return out && physical(owner, expected, &view, error) && owner->options.sample_seconds &&
        owner->options.sample_seconds(owner->options.context, &view.source, out, error);
}
bool frontend_remote_q1_source_create(qa_frontend *f, const frontend_remote_q1_source_options *options,
    frontend_remote_q1_source **out, qa_error *error)
{
    if (!f || f->capture || f->resource_inventory || f->source_restoring ||
        !options || !options->physical || !options->load_content || !options->service || !options->disconnected ||
        !out || *out || !qa_q1_profile_valid(options->protocol, error) ||
        (qa_q1_is_qw(options->protocol) && !options->skin_bindings)) return false;
    frontend_client_source_view view;
    if (!frontend_client_source_read(options->physical, &view, error) || !view.ready || view.source.context.session != qa_application_session(f->application)) return false;
    bool linked = false;
    for (size_t i = 0; i < frontend_client_source_count(f); ++i)
        if (frontend_client_source_at(f, i) == options->physical) linked = true;
    if (!linked) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 Source has no actual physical frontend parent");
    frontend_remote_q1_source *owner = calloc(1, sizeof(*owner));
    if (!owner) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining physical Q1 Source adapter");
    *out = owner; owner->frontend = f; owner->options = *options;
    if (!frontend_client_source_retain(options->physical, error)) return false;
    owner->retained = true;
    frontend_remote_q1_options receiver = {.domain = domain(owner, &view.source), .context = owner,
        .current = current, .load_content = load, .service = service, .disconnected = disconnected,
        .skin_bindings = options->skin_bindings,.application_read=application_read,
        .application_metadata_read=application_metadata_read,
        .sample_seconds=options->sample_seconds ? sample_seconds : NULL,
        .demo_forced_track=options->sample_seconds ? options->demo_forced_track : -1};
    return frontend_remote_q1_create(f, &receiver, &owner->receiver, error);
}
bool frontend_remote_q1_source_read(const frontend_remote_q1_source *source,
    frontend_remote_q1_source_view *out, qa_error *error)
{
    frontend_remote_q1_source *owner = (frontend_remote_q1_source *)source;
    if (!owner || !out || owner->closing || !owner->receiver ||
        !frontend_client_source_read(owner->options.physical, &out->physical, error)) return false;
    *out = (frontend_remote_q1_source_view){owner, out->physical, owner->receiver, domain(owner, &out->physical.source)};
    return true;
}
bool frontend_remote_q1_source_metadata_read(const frontend_remote_q1_source *owner,
    frontend_remote_q1_source_view *out, qa_error *error)
{
    frontend_client_source_view physical_view;
    if (!owner || !out || owner->closing || !owner->retained || !owner->receiver ||
        !frontend_client_source_metadata_read(owner->options.physical, &physical_view, error)) return false;
    *out = (frontend_remote_q1_source_view){owner, physical_view, owner->receiver, domain(owner, &physical_view.source)};
    return true;
}
bool frontend_remote_q1_source_current(const frontend_remote_q1_source_view *view)
{
    return view && view->owner && view->receiver && !view->owner->closing && view->receiver == view->owner->receiver &&
        frontend_client_source_current(&view->physical) &&
        remote_q1_domain_equal(&view->domain, &view->receiver->options.domain);
}
bool frontend_remote_q1_source_hooks(frontend_remote_q1_source *owner, qa_network_q1_client_hooks *out, qa_error *error)
{ return owner && !owner->closing && owner->receiver && frontend_remote_q1_hooks(owner->receiver, out, error); }
bool frontend_remote_q1_source_bind(frontend_remote_q1_source *owner, qa_net_client_id client,
    qa_net_seat_id seat, uint64_t epoch, qa_error *error)
{
    if (!owner || owner->closing || owner->calls || !owner->receiver || owner->receiver->bound ||
        !frontend_client_source_bind(owner->options.physical, client, seat, epoch, error)) return false;
    frontend_client_source_view actual;
    if (!frontend_client_source_read(owner->options.physical, &actual, error)) return false;
    frontend_remote_q1_domain d = domain(owner, &actual.source);
    return frontend_remote_q1_bind(owner->receiver, &d, error);
}
bool frontend_remote_q1_source_entity_current(const frontend_remote_q1_source *owner, uint32_t number, uint64_t *generation)
{
    if (!owner || owner->closing || !owner->receiver || !generation || !owner->receiver->loaded) return false;
    const frontend_remote_q1 *row = owner->receiver;
    bool found = number == row->view_entity && number != 0;
    for (size_t i = 0; !found && i < row->current.count; ++i) found = row->current.rows[i].number == number;
    for (size_t i = 0; !found && i < row->statics.count; ++i) found = row->statics.rows[i].number == number;
    if (found) *generation = row->map_generation;
    return found && *generation != 0;
}
bool frontend_remote_q1_source_idle(const frontend_remote_q1_source *owner)
{ return owner && !owner->calls && frontend_client_source_idle(owner->options.physical) &&
    (!owner->receiver || frontend_remote_q1_idle(owner->receiver)); }
bool frontend_remote_q1_source_destroy(frontend_remote_q1_source **owned, qa_error *error)
{
    frontend_remote_q1_source *owner = owned ? *owned : NULL;
    if (!owner) return true;
    if (owner->calls || owner->frontend->capture || owner->frontend->resource_inventory ||
        (owner->receiver && owner->receiver->bound &&
        qa_net_connections_get(qa_network_connections(owner->receiver->options.domain.runtime), owner->receiver->options.domain.client)))
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 Source adapter retains its actual attached transport");
    if (!frontend_remote_q1_destroy(&owner->receiver, error)) return false;
    owner->closing = true;
    if (owner->retained && !frontend_client_source_release(owner->options.physical, error)) return false;
    owner->retained = false; free(owner); *owned = NULL; return true;
}
