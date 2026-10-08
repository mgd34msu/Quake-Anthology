#include "remote_q2_source.h"
#include "remote_q2_private.h"
#include "client_registry.h"
#include "legacy_render_policy.h"
#include "qa/cvars_save.h"
#include "qa/console_cvar_observer.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct frontend_remote_q2_source {
    qa_frontend *frontend;
    frontend_remote_q2_source_options options;
    qa_launch_instance_lease *metadata;
    qa_launch_instance_lease *constructor_metadata;
    qa_launch_q2_client_metadata selected_request;
    frontend_client_registry *registry;
    frontend_remote_q2 *receiver;
    qa_console *console;
    qa_cvars *pending_cvars;
    qa_vfs *pending_selected;
    qa_buffer imported_commands, imported_current;
    frontend_remote_q2_domain domain;
    qa_application_client_source retirement_receipt;
    size_t references;
    unsigned calls;
    bool closing, configured, configuration_complete, ready, template_retired, console_bound;
    bool retiring, retirement_entered, release_programmes, retirement_receipt_present;
    bool imported_release_discarded, restored_constructor, imported_queue, restore_finished;
};
static bool profile_protocol(const qa_product *profile, qa_net_protocol_id protocol, qa_error *error)
{
    qa_q2_codec codec;
    bool rerelease = protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2PRIVATE_4038 || protocol.kind == QA_NET_Q2KEX_2023 ||
        protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    return profile && qa_q2_codec_init(&codec, protocol, error) &&
        profile->family == QA_GAME_Q2 && profile->edition == (rerelease ? QA_EDITION_RERELEASE : QA_EDITION_CLASSIC);
}
bool frontend_remote_q2_source_recipe(qa_catalog *catalog, qa_net_protocol_id protocol,
    qa_product_id compiled, qa_product_id selected, const char *instance, uint32_t seat,
    qa_launch_q2_client_metadata *out, qa_vfs **prepared, qa_error *error)
{
    qa_q2_codec codec;
    if (!catalog || !instance || !*instance || !out || !prepared || *prepared ||
        !qa_q2_codec_init(&codec, protocol, error))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT recipe requires its actual negotiated protocol and catalog");
    bool rerelease = protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2PRIVATE_4038 || protocol.kind == QA_NET_Q2KEX_2023 ||
        protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    const qa_product *profile = compiled ? qa_catalog_product(catalog, compiled) :
        qa_catalog_find(catalog, rerelease ? "q2-rerelease-baseq2" : "q2-classic-baseq2");
    if (!profile || !profile->builtin || profile->family != QA_GAME_Q2 ||
        profile->edition != (rerelease ? QA_EDITION_RERELEASE : QA_EDITION_CLASSIC))
        return remote_q2_fail(error, QA_ERROR_NOT_FOUND, "Q2 CLIENT wire profile has no installed compiled content owner");
    if (!selected) selected=profile->id;
    if (!qa_catalog_open(catalog, selected, prepared, error)) return false;
    *out = (qa_launch_q2_client_metadata){catalog, profile->id, selected, *prepared, instance, seat};
    return true;
}
static bool retain(void *context, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->retiring || source->references == SIZE_MAX)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 registry callback owner is retiring");
    ++source->references; return true;
}
static bool release(void *context, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->calls || source->references < 2)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 registry callback lease is still entered");
    --source->references; return true;
}
static bool tuple(const frontend_remote_q2_source *source, const qa_command_context *context)
{
    const qa_command_context *actual = source ? &source->domain.command_context : NULL;
    return actual && context && actual->session == context->session && actual->owner == context->owner &&
        actual->client == context->client && actual->seat == context->seat && actual->registry == context->registry &&
        actual->generation == context->generation && actual->cvar_view == context->cvar_view &&
        actual->dialect == context->dialect &&
        qa_actor_id_equal(actual->actor, context->actor);
}
bool frontend_remote_q2_source_owner_retain(void *context, qa_error *error)
{ return retain(context, error); }
bool frontend_remote_q2_source_owner_release(void *context, qa_error *error)
{ return release(context, error); }
bool frontend_remote_q2_source_owner_idle(const frontend_remote_q2_source *source)
{
    return source && !source->calls && qa_console_idle(source->console) &&
        (!source->domain.cvars || qa_cvars_observer_idle(source->domain.cvars)) &&
        (!source->receiver || (!source->receiver->busy && !source->receiver->importing && !source->receiver->image_policy));
}
bool frontend_remote_q2_source_owner_import_idle(const frontend_remote_q2_source *source)
{
    return source && source->frontend->source_restoring && !source->closing && source->configured &&
        source->frontend->application == source->domain.application && !source->calls && source->registry &&
        source->imported_commands.data && source->imported_current.data &&
        qa_console_idle(source->console) && qa_cvars_observer_idle(source->domain.cvars) &&
        source->domain.cvars == frontend_client_registry_cvars(source->registry) &&
        frontend_client_registry_matches(source->registry, qa_launch_instance_lease_view(source->constructor_metadata),
            source->options.metadata.seat) &&
        (!source->receiver || (!source->receiver->busy && !source->receiver->image_policy));
}
bool frontend_remote_q2_source_constructor_read(const frontend_remote_q2_source *source,
    const qa_launch_instance **out, qa_error *error)
{
    const qa_launch_instance *descriptor = source ? qa_launch_instance_lease_view(source->constructor_metadata) : NULL;
    if (!source || !out || source->closing || source->frontend->application != source->domain.application || !descriptor)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT has no held actual constructor descriptor");
    *out = descriptor; return true;
}
bool frontend_remote_q2_source_owner_retirement_idle(const frontend_remote_q2_source *source)
{
    return source && source->frontend->source_restoring && !source->frontend->capture &&
        !source->frontend->resource_inventory && !source->closing && !source->calls &&
        source->frontend->application == source->domain.application && qa_console_idle(source->console) &&
        (!source->domain.cvars || qa_cvars_observer_idle(source->domain.cvars)) &&
        (!source->pending_cvars || qa_cvars_observer_idle(source->pending_cvars)) &&
        (!source->receiver || (!source->receiver->busy && !source->receiver->image_policy));
}
bool frontend_remote_q2_source_owner_current(const frontend_remote_q2_source *source,
    const qa_launch_instance *descriptor, const qa_console *console, const qa_cvars *registry,
    const qa_command_context *context, qa_error *error)
{
    const qa_launch_instance *actual = source ? qa_launch_instance_lease_view(source->metadata) : NULL;
    if (!source || source->closing || source->frontend->application != source->domain.application ||
        !source->registry || !actual || !descriptor ||
        descriptor->storage != actual->storage || descriptor->content != actual->content ||
        descriptor->identity != actual->identity || console != source->console ||
        registry != source->domain.cvars || registry != frontend_client_registry_cvars(source->registry) ||
        !frontend_client_registry_matches(source->registry,
            qa_launch_instance_lease_view(source->constructor_metadata), source->options.metadata.seat) ||
        !tuple(source, context) || context->origin != source->domain.command_context.origin ||
        context->direct != source->domain.command_context.direct ||
        context->console_text != source->domain.command_context.console_text || context->script)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical CLIENT tuple differs from its actual constructor owner");
    return true;
}
static bool current(void *context, const frontend_remote_q2_domain *domain, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->retiring || !source->configured || source->frontend->application != domain->application ||
        !remote_q2_domain_equal(&source->domain, domain) || !source->registry ||
        source->domain.cvars != frontend_client_registry_cvars(source->registry) ||
        !frontend_client_registry_matches(source->registry, qa_launch_instance_lease_view(source->constructor_metadata),
            source->options.metadata.seat) || (source->receiver &&
            source->receiver->options.material_scripts != source->options.client.material_scripts)) return false;
    return source->options.client.current(source->options.client.context, domain, error);
}
static bool retirement_current(void *context, const frontend_remote_q2_domain *domain, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    frontend_remote_q2_source_view held;
    return source && remote_q2_domain_equal(&source->domain, domain) &&
        frontend_remote_q2_source_retirement_custody_read(source, &held, error);
}
static bool retirement_receipt_hold(frontend_remote_q2_source *source, qa_error *error)
{
    qa_application_client_source actual;
    if (!source->options.retirement_current || source->calls ||
        source->domain.command_context.owner > UINT32_MAX ||
        !qa_application_client_physical_read(source->domain.application, (qa_actor_owner)source->domain.command_context.owner,
            source->domain.command_context.seat, &actual, error) ||
        !qa_application_client_retirement_current(source->domain.application, &actual)) return false;
    ++source->calls;
    bool held = source->options.retirement_current(source->options.client.context, &actual);
    --source->calls;
    if (!held || !frontend_remote_q2_source_owner_current(source, actual.descriptor, actual.context.console,
        actual.context.cvars, &actual.context.command, error)) return false;
    source->retirement_receipt = actual; source->retirement_receipt_present = true;
    return true;
}
static bool active(void *context, const qa_command_context *command)
{
    frontend_remote_q2_source *source = context; qa_error error = {0};
    if (!source || source->closing || (source->retiring && !source->retirement_entered) ||
        source->frontend->capture || source->frontend->resource_inventory || source->frontend->source_restoring ||
        source->calls == UINT_MAX || !tuple(source, command) ||
        !qa_application_command_context_active(source->domain.application, command)) return false;
    if (source->retirement_entered) return true;
    return !source->receiver || !source->receiver->bound || current(source, &source->domain, &error);
}
static bool capture(void *context, const qa_command_context *in, qa_command_context *out, qa_error *error)
{
    if (!out || !active(context, in)) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 console caller left its actual CLIENT tuple");
    *out = *in; return true;
}
static void print(void *context, const qa_command_context *command, const char *text)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || (source->retiring && !source->retirement_entered) ||
        source->calls == UINT_MAX || !tuple(source, command)) return;
    ++source->calls; source->options.print(source->options.client.context, command, text); --source->calls;
}
static void cvar_print(void *context, const char *text)
{ frontend_remote_q2_source *source = context; print(source, &source->domain.command_context, text); }
static bool script(void *context, const qa_command_context *command, const char *path,
    qa_bytes *out, void **lease, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, command) || !out || !lease) return false;
    if (source->options.read_script) {
        ++source->calls; bool ok = source->options.read_script(source->options.client.context, command, path, out, lease, error); --source->calls;
        return ok;
    }
    const qa_vfs *files = source->receiver && source->receiver->selected ? source->receiver->content.mounts :
        qa_launch_instance_lease_view(source->metadata)->content;
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire((qa_vfs *)files, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}
static void script_release(void *context, void *lease)
{
    frontend_remote_q2_source *source = context;
    if (!source->options.release_script) { qa_resource_release(lease); return; }
    ++source->calls; source->options.release_script(source->options.client.context, lease); --source->calls;
}
static void script_complete(void *context, const qa_command_context *command, const char *path, bool ok)
{
    frontend_remote_q2_source *source = context;
    if (!tuple(source, command) || !source->options.script_complete) return;
    ++source->calls; source->options.script_complete(source->options.client.context, command, path, ok); --source->calls;
}
static bool allow(void *context, const qa_command_invocation *invocation)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, &invocation->context)) return false;
    ++source->calls; bool ok = source->options.allow_command(source->options.client.context, invocation); --source->calls;
    return ok;
}
static qa_command_result command(void *context, const qa_command_invocation *invocation, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, &invocation->context)) return QA_COMMAND_FAILED;
    ++source->calls;
    qa_command_result result = source->options.command ? source->options.command(source->options.client.context, invocation, error) : QA_COMMAND_UNHANDLED;
    --source->calls; return result;
}
static qa_command_result forward(void *context, const qa_command_invocation *invocation, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, &invocation->context)) return QA_COMMAND_UNHANDLED;
    if (source->options.template_forward) {
        ++source->calls; qa_command_result result = source->options.template_forward(source->options.client.context, invocation, error); --source->calls;
        if (result != QA_COMMAND_UNHANDLED) return result;
    }
    if (!active(source, &invocation->context) || source->retiring || !source->receiver || !source->receiver->bound || !invocation->argc)
        return QA_COMMAND_UNHANDLED;
    uint32_t remote_index;
    if (!frontend_remote_q2_wire_seat(source->receiver, &remote_index, error)) return QA_COMMAND_FAILED;
    const char *text;bool explicit_command;
    if(!qa_console_forward_text(invocation,&text,&explicit_command,error))return QA_COMMAND_FAILED;
    if(explicit_command&&invocation->argc==1)return QA_COMMAND_HANDLED;
    return qa_network_q2_client_command(source->domain.runtime, source->domain.client, text,
        (uint8_t)remote_index, error) ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}
static bool allowed(void *context, const char *path, bool *result, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.download_allowed(source->options.client.context, path, result, error); }
static bool download_stage(void *context, qa_fs_root *root, const char *path,
    qa_fs_stage **out, uint64_t *nonce, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    return source->options.client.download_stage(source->options.client.context, root, path, out, nonce, error);
}
static bool restore_stage(void *context, qa_fs_root *root, const char *path, uint64_t logical_nonce,
    bool published, qa_bytes prefix, qa_fs_stage **out, uint64_t *native_nonce, qa_fs_identity *identity, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->calls || !source->frontend->source_restoring ||
        !source->options.client.restore_stage) return false;
    ++source->calls;
    bool ok = source->options.client.restore_stage(source->options.client.context, root, path,
        logical_nonce, published, prefix, out, native_nonce, identity, error);
    --source->calls; return ok;
}
static bool entity_actor(void *context, const frontend_remote_q2_domain *domain,
    uint32_t number, qa_actor_id *out, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    return source->options.client.entity_actor && source->options.client.entity_actor(
        source->options.client.context, domain, number, out, error);
}
static bool records(void *context, const frontend_remote_q2_domain *domain,
    const qa_q2_server_record *batch, size_t count, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.records(source->options.client.context, domain, batch, count, error); }
static bool entities_changed(void *context, const frontend_remote_q2_domain *domain, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->calls || !source->options.client.entities_changed ||
        !remote_q2_domain_equal(domain, &source->domain) || !current(source, domain, error)) return false;
    ++source->calls;
    bool ok = source->options.client.entities_changed(source->options.client.context, domain, error);
    --source->calls;
    return ok && current(source, domain, error);
}
static bool disconnected(void *context, const frontend_remote_q2_domain *domain, const char *reason, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->retiring || source->calls || !source->options.client.disconnected ||
        !remote_q2_domain_equal(domain, &source->domain)) return false;
    ++source->calls;
    bool ok = source->options.client.disconnected(source->options.client.context, domain, reason, error);
    --source->calls; return ok;
}
static bool select_content(void *context, uint64_t generation, const qa_q2_serverdata *data,
    frontend_remote_q2_content *out, qa_q2_preparation *result, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.select_content(source->options.client.context, generation, data, out, result, error); }
static bool content_admit(void *context, uint64_t loading, const frontend_remote_q2_content *content, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->retiring || source->calls || !source->receiver || !source->receiver->bound ||
        !source->receiver->selected || source->receiver->loading_generation != loading ||
        content != &source->receiver->content || source->domain.configuration_generation == UINT64_MAX ||
        !source->options.admit_content) return false;
    const qa_product *original = qa_catalog_product(source->options.metadata.catalog, source->options.metadata.profile);
    const qa_product *profile = original ? qa_catalog_find(content->catalog, original->key) : NULL;
    if (!profile) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 content admission lost its compiled profile");
    qa_launch_q2_client_metadata request = {content->catalog, profile->id, content->selected, content->mounts,
        source->options.metadata.instance, source->options.metadata.seat};
    qa_launch_instance_lease *metadata = NULL;
    if (!qa_launch_instance_prepare_q2_client_metadata(&request, &metadata, error)) return false;
    frontend_remote_q2_domain previous = source->domain, candidate = previous;
    ++candidate.configuration_generation;
    qa_launch_instance_lease *old = source->metadata;
    qa_launch_q2_client_metadata previous_request = source->selected_request;
    source->metadata = metadata; source->domain = candidate; source->receiver->options.domain = candidate;
    source->selected_request = request;
    source->selected_request.prepared = qa_launch_instance_lease_view(metadata)->content;
    ++source->calls;
    bool ok = source->options.admit_content(source->options.client.context, &previous,
        qa_launch_instance_lease_view(metadata), &candidate, error);
    --source->calls;
    if (!ok) {
        source->metadata = old; source->domain = previous; source->receiver->options.domain = previous;
        source->selected_request = previous_request;
        qa_launch_instance_lease_release(metadata); return false;
    }
    qa_launch_instance_lease_release(old); return true;
}
static bool defaults(frontend_remote_q2_source *source, qa_error *error)
{
    static const struct { const char *name, *value; uint32_t flags; } values[] = {
        {"name", "unnamed", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"skin", "male/grunt", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"rate", "25000", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"msg", "1", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"hand", "0", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"fov", "90", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"password", "", QA_CVAR_USERINFO}, {"spectator", "0", QA_CVAR_USERINFO},
        {"gender", "male", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"gender_auto", "1", QA_CVAR_ARCHIVE},
        {"qa_materials", "1", QA_CVAR_USERINFO},
        {"cl_predict", "1", 0}, {"cl_gun", "1", 0}, {"cl_blend", "1", 0}, {"cl_lights", "1", 0},
        {"cl_particles", "1", 0}, {"cl_entities", "1", 0}, {"cl_footsteps", "1", 0}, {"cl_noskins", "0", 0},
        {"cl_vwep", "1", QA_CVAR_ARCHIVE}, {"cl_hit_markers", "2", 0}, {"scr_hit_marker_time", "500", 0},
        {"ch_alpha", "1", 0}, {"ch_scale", "1", 0}, {"ch_x", "0", 0}, {"ch_y", "0", 0},
        {"cl_muzzlelight_time", "100", 0}, {"cl_rerelease_effects", "1", 0}, {"cl_dlight_hacks", "0", 0},
        {"cl_muzzleflashes", "1", 0}, {"cl_disable_particles", "0", 0}, {"cl_disable_explosions", "0", 0},
        {"cl_smooth_explosions", "1", 0}, {"gl_damageblend_frac", "0.2", 0}, {"cl_gunfov", "90", 0},
        {"scr_pois", "1", 0}, {"scr_poi_edge_frac", "0.15", 0}, {"scr_poi_max_scale", "1", 0},
        {"scr_damage_indicators", "1", 0}, {"scr_damage_indicator_time", "1000", 0},
        {"cl_railtrail_type", "0", 0}, {"cl_railtrail_time", "1.0", 0}, {"cl_railcore_color", "red", 0},
        {"cl_railcore_width", "2", 0}, {"cl_railspiral_color", "blue", 0}, {"cl_railspiral_radius", "3", 0}
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!qa_cvars_register(source->domain.cvars, values[i].name, values[i].value, values[i].flags,
            source->domain.command_context.owner, "", error)) return false;
    if (!qa_cvars_register(source->domain.cvars, "crosshair",
        source->domain.command_context.dialect == QA_CONSOLE_Q2_RERELEASE ? "3" : "0", QA_CVAR_ARCHIVE,
        source->domain.command_context.owner, "", error)) return false;
    return frontend_legacy_source_register(source->domain.cvars,
        source->domain.command_context.dialect, source->domain.command_context.owner, error) &&
        qa_input_settings_register(source->domain.cvars,
        source->domain.command_context.dialect == QA_CONSOLE_Q2_RERELEASE ? QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC, error);
}
bool frontend_remote_q2_source_create(qa_frontend *f, const frontend_remote_q2_source_options *options,
    frontend_remote_q2_source **out, qa_error *error)
{
    if (!f || f->capture || f->resource_inventory || !options || !out || *out || !options->print || !options->prepare_namespace || !options->configure ||
        !options->admit_content || !options->client.current || !!options->read_script != !!options->release_script ||
        !options->client.download_allowed || !options->client.download_stage || !options->client.records || !options->client.disconnected ||
        options->client.domain.application != f->application || options->client.domain.console || options->client.domain.cvars ||
        options->client.domain.catalog != options->metadata.catalog || options->client.domain.product != options->metadata.profile ||
        options->client.domain.command_context.script)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source constructor requires its real pending canonical CLIENT claim");
    frontend_remote_q2_source *source = calloc(1, sizeof(*source));
    if (!source) return false;
    source->frontend = f; source->options = *options; source->domain = options->client.domain; source->references = 1;
    *out = source;
    if (!qa_launch_instance_prepare_q2_client_metadata(&options->metadata, &source->metadata, error)) return false;
    if (!qa_launch_instance_retain_metadata(qa_launch_instance_lease_view(source->metadata),
        &source->constructor_metadata, error)) return false;
    const qa_launch_instance *constructor = qa_launch_instance_lease_view(source->constructor_metadata);
    source->options.metadata.prepared = constructor->content;
    source->options.metadata.instance = constructor->selection.instance;
    source->selected_request = source->options.metadata;
    if (!profile_protocol(qa_catalog_product(options->metadata.catalog, options->metadata.profile),
        source->domain.protocol, error)) return false;
    ++source->calls;
    bool prepared = options->prepare_namespace(options->client.context, qa_launch_instance_lease_view(source->metadata),
        &source->domain, error);
    --source->calls;
    if (!prepared) return false;
    frontend_remote_q2_domain expected = options->client.domain;
    expected.command_context = source->domain.command_context;
    expected.configuration_generation = source->domain.configuration_generation;
    if (!remote_q2_domain_equal(&expected, &source->domain) || !source->domain.command_context.owner ||
        !source->domain.command_context.registry || !source->domain.command_context.generation ||
        source->domain.command_context.script)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 app namespace changed its pending transport/content claim");
    const qa_product *profile = qa_catalog_product(options->metadata.catalog, options->metadata.profile);
    qa_console_dialect dialect = profile->edition == QA_EDITION_RERELEASE ? QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
    if (source->domain.command_context.dialect != dialect || source->domain.command_context.seat != options->metadata.seat) return false;
    qa_cvar_options variables = {.dialect = dialect, .side = QA_CVAR_SIDE_CLIENT,
        .role = QA_CVAR_ROLE_CGAME, .seat = options->metadata.seat, .user = source, .print = cvar_print,
        .default_save_policy = QA_CVAR_SAVE_SETTING};
    source->pending_cvars = qa_cvars_create_view(qa_application_cvars(f->application), &variables, error);
    if (!source->pending_cvars) return false;
    source->domain.command_context.cvar_view = qa_cvars_view_identity(source->pending_cvars);
    frontend_client_registry_context callback = {source, retain, release, false};
    if (!frontend_client_registry_create(f, qa_launch_instance_lease_view(source->metadata), options->metadata.seat,
        &source->pending_cvars, &callback, &source->registry, error)) return false;
    source->domain.cvars = frontend_client_registry_cvars(source->registry);
    qa_console_options console = {.context = source->domain.command_context, .cvars = source->domain.cvars, .user = source,
        .print = print, .capture_context = capture, .context_active = active,
        .script_complete = options->script_complete ? script_complete : NULL,
        .allow_command = options->allow_command ? allow : NULL,
        .read_script = script, .release_script = script_release, .source_command = command, .forward = forward};
    source->console = qa_application_console(f->application); source->domain.console = source->console;
    if (!source->console || !qa_console_bind_source(source->console, &console, error)) return false;
    source->console_bound = true;
    if (!qa_application_command_context_active(f->application, &source->domain.command_context))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT constructor lost its actual bound view");
    if (!defaults(source, error)) return false;
    if (options->initialize) {
        ++source->calls; bool initialized = options->initialize(options->client.context,
            qa_launch_instance_lease_view(source->metadata), source->domain.cvars, &source->domain.command_context, error); --source->calls;
        if (!initialized) return false;
    }
    ++source->calls;
    bool configured = options->configure(options->client.context, qa_launch_instance_lease_view(source->metadata),
        source->domain.cvars, source->console, error);
    --source->calls;
    if (!configured) return false;
    source->configured = true;
    if (options->install) {
        ++source->calls; bool installed = options->install(options->client.context, false, error); --source->calls;
        if (!installed) return false;
    }
    bool ready = false;
    return frontend_remote_q2_source_advance(source, &ready, error);
}
bool frontend_remote_q2_source_advance(frontend_remote_q2_source *source, bool *ready, qa_error *error)
{
    if (!source || !ready || !source->configured || source->closing || source->retiring || source->template_retired ||
        !frontend_remote_q2_source_owner_idle(source) || source->frontend->capture ||
        source->frontend->resource_inventory || source->frontend->source_restoring) return false;
    *ready = source->ready;
    if (*ready) return true;
    if (source->receiver) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 receiver construction failed and retains checked cleanup");
    bool complete = source->configuration_complete || !source->options.configure_step;
    if (!complete) {
        complete = false; ++source->calls;
        bool ok = source->options.configure_step(source->options.client.context, &complete, error); --source->calls;
        if (!ok || !current(source, &source->domain, error)) return false;
    }
    if (!complete) return true;
    source->configuration_complete = true;
    const frontend_remote_q2_source_options *options = &source->options;
    frontend_remote_q2_options child = {.domain = source->domain, .context = source, .current = current,
        .retirement_current = retirement_current,
        .material_scripts = options->client.material_scripts,
        .download_allowed = allowed, .download_stage = download_stage,
        .restore_stage = options->client.restore_stage ? restore_stage : NULL, .records = records, .disconnected = disconnected,
        .entity_actor = options->client.entity_actor ? entity_actor : NULL,
        .entities_changed = options->client.entities_changed ? entities_changed : NULL,
        .content_admit = content_admit,
        .select_content = options->client.select_content ? select_content : NULL};
    bool ok = frontend_remote_q2_create(source->frontend, &child, &source->receiver, error);
    if (ok) source->ready = *ready = true;
    return ok;
}
bool frontend_remote_q2_source_configuration_advance(frontend_remote_q2_source *source,
    qa_application_client_preparation *token, bool *complete, qa_error *error)
{
    const qa_application_client_source *held = qa_application_client_prepare_source(token);
    if (!source || !complete || !held || !source->configured || source->closing || source->retiring || source->template_retired ||
        !frontend_remote_q2_source_owner_idle(source) || source->frontend->capture ||
        source->frontend->resource_inventory || source->frontend->source_restoring ||
        qa_application_client_prepare_application(token) != source->domain.application ||
        !qa_application_client_prepare_entered(token, QA_CLIENT_PREPARE_CONFIGURATION) ||
        held->context.receiver != source->domain.command_context.owner || held->context.seat != source->domain.command_context.seat ||
        !frontend_remote_q2_source_owner_current(source, held->descriptor, held->context.console,
            held->context.cvars, &held->context.command, error) ||
        !qa_application_client_current(source->domain.application, held))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT programme leaves its actual entered configuration token");
    bool ready = source->configuration_complete;
    if (!ready) {
        ready = true;
        if (source->options.configure_step) {
            if (!source->options.configuration_advance)
                return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT programme has no actual entered phase callback");
            ready = false; ++source->calls;
            bool ok = source->options.configuration_advance(source->options.client.context, token, &ready, error); --source->calls;
            if (!ok) return false;
        }
        if (!qa_application_client_prepare_entered(token, QA_CLIENT_PREPARE_CONFIGURATION) ||
            !qa_application_client_current(source->domain.application, held))
            return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT programme changed its actual configuration token");
    }
    *complete = ready; return true;
}
bool frontend_remote_q2_source_configuration_continue(frontend_remote_q2_source *source,
    qa_application_client_preparation *token, bool *complete, qa_error *error)
{
    const qa_application_client_source *held = qa_application_client_prepare_source(token);
    if (!source || !complete || !held || !source->configured || source->closing || source->retiring || source->template_retired ||
        !frontend_remote_q2_source_owner_idle(source) || source->frontend->capture || source->frontend->resource_inventory ||
        source->frontend->source_restoring || qa_application_client_prepare_application(token) != source->domain.application ||
        !qa_application_client_prepare_associated(source->domain.application, token) ||
        !qa_application_client_prepare_current(token) ||
        held->context.receiver != source->domain.command_context.owner || held->context.seat != source->domain.command_context.seat ||
        !frontend_remote_q2_source_owner_current(source, held->descriptor, held->context.console,
            held->context.cvars, &held->context.command, error) || !qa_application_client_current(source->domain.application, held))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT programme leaves its actual returned preparation token");
    for (int phase = QA_CLIENT_PREPARE_CONFIGURATION; phase <= QA_CLIENT_PREPARE_CLEANUP; ++phase)
        if (qa_application_client_prepare_entered(token, (qa_application_client_prepare_phase)phase))
            return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT preparation callback remains entered");
    if (!source->configuration_complete) {
        bool ready = true;
        if (source->options.configure_step) {
            ready = false; ++source->calls;
            bool ok = source->options.configure_step(source->options.client.context, &ready, error); --source->calls;
            if (!ok || !current(source, &source->domain, error)) return false;
        }
        source->configuration_complete = ready;
    }
    *complete = source->configuration_complete; return true;
}
bool frontend_remote_q2_source_configuration_completed(const frontend_remote_q2_source *source)
{ return source && source->configuration_complete; }
bool frontend_remote_q2_source_retire(frontend_remote_q2_source *source, qa_error *error)
{
    if (!source) return true;
    bool returned = source->frontend->source_restoring ? frontend_remote_q2_source_owner_retirement_idle(source) :
        frontend_remote_q2_source_owner_idle(source);
    if (!returned || source->frontend->capture || source->frontend->resource_inventory || source->closing) return false;
    if (source->frontend->source_restoring && source->restored_constructor && !source->restore_finished &&
        source->release_programmes && source->imported_queue &&
        qa_console_release_restore_unclaimed(source->console)) {
        if (!qa_console_release_restore_abort(source->console, error)) return false;
        source->imported_release_discarded = true;
    }
    if (source->template_retired) return true;
    source->retiring = true;
    if (source->receiver) source->receiver->retiring = true;
    if (source->configured && source->domain.client.owner && !source->retirement_receipt_present &&
        !retirement_receipt_hold(source, error)) return false;
    if (source->options.retire) {
        ++source->calls; source->retirement_entered = true;
        bool ok = source->options.retire(source->options.client.context, error);
        source->retirement_entered = false; --source->calls;
        if (!ok) return false;
    }
    source->template_retired = true; return true;
}
bool frontend_remote_q2_source_retirement_current(const frontend_remote_q2_source *source,
    const qa_application_client_source *actual, const qa_console *console,
    const qa_command_context *command, qa_error *error)
{
    if (!source || !source->retiring || !source->retirement_entered || !source->calls || source->closing ||
        !source->configured || !actual || console != source->console || !tuple(source, command) ||
        !qa_application_client_associated(source->domain.application, actual) ||
        actual->context.receiver != source->domain.command_context.owner ||
        actual->context.seat != source->domain.command_context.seat || actual->context.console != source->console ||
        actual->context.cvars != source->domain.cvars ||
        !frontend_remote_q2_source_owner_current(source, actual->descriptor, console,
            actual->context.cvars, &actual->context.command, error))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 retirement left its entered physical CLIENT source");
    return true;
}
bool frontend_remote_q2_source_read(const frontend_remote_q2_source *source, frontend_remote_q2_source_view *out, qa_error *error)
{
    if (!source || !out || !source->configured || !source->ready || source->closing || source->retiring || source->template_retired || !source->metadata || !source->receiver || !source->registry)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source observation requires its completed pending constructor");
    *out = (frontend_remote_q2_source_view){source, qa_launch_instance_lease_view(source->metadata),
        source->receiver, source->domain, source->receiver->bound}; return true;
}
bool frontend_remote_q2_source_current(const frontend_remote_q2_source_view *view)
{
    frontend_remote_q2_source_view actual = {0}; qa_error error = {0};
    return view && frontend_remote_q2_source_read(view->owner, &actual, &error) && actual.descriptor == view->descriptor &&
        actual.receiver == view->receiver && actual.bound == view->bound && remote_q2_domain_equal(&actual.domain, &view->domain);
}
bool frontend_remote_q2_source_physical_read(const frontend_remote_q2_source *source,
    frontend_remote_q2_source_view *out, qa_error *error)
{
    const qa_launch_instance *descriptor = source ? qa_launch_instance_lease_view(source->metadata) : NULL;
    if (!source || !out || !source->configured || source->closing || source->retiring || source->template_retired ||
        !descriptor || source->ready != (source->receiver != NULL) ||
        (!source->receiver && (source->domain.client.owner || source->domain.client.generation || source->domain.client.slot ||
            source->domain.epoch || source->domain.seat.owner || source->domain.seat.index)) ||
        (source->receiver && !source->configuration_complete) ||
        !frontend_remote_q2_source_owner_current(source, descriptor, source->console, source->domain.cvars,
            &source->domain.command_context, error))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical CLIENT has no completed retained constructor tuple");
    *out = (frontend_remote_q2_source_view){source, descriptor, source->receiver, source->domain,
        source->receiver && source->receiver->bound};
    return true;
}
bool frontend_remote_q2_source_physical_current(const frontend_remote_q2_source_view *view)
{
    frontend_remote_q2_source_view actual; qa_error error = {0};
    return view && frontend_remote_q2_source_physical_read(view->owner, &actual, &error) &&
        actual.descriptor == view->descriptor && actual.receiver == view->receiver && actual.bound == view->bound &&
        remote_q2_domain_equal(&actual.domain, &view->domain);
}
bool frontend_remote_q2_source_retirement_custody_read(const frontend_remote_q2_source *source,
    frontend_remote_q2_source_view *out, qa_error *error)
{
    const qa_launch_instance *descriptor = source ? qa_launch_instance_lease_view(source->metadata) : NULL;
    qa_application_client_source actual;
    if (!source || !out || source->closing || !source->retiring || !source->configured || source->calls ||
        !descriptor || !source->options.retirement_current || source->ready != (source->receiver != NULL) ||
        !qa_console_idle(source->console) || !qa_cvars_observer_idle(source->domain.cvars) ||
        (source->receiver && (source->receiver->busy || source->receiver->image_policy ||
            !source->receiver->retiring || !remote_q2_domain_equal(&source->domain, &source->receiver->options.domain))) ||
        !frontend_remote_q2_source_owner_current(source, descriptor, source->console, source->domain.cvars,
            &source->domain.command_context, error) ||
        source->domain.command_context.owner > UINT32_MAX ||
        !qa_application_client_physical_read(source->domain.application, (qa_actor_owner)source->domain.command_context.owner,
            source->domain.command_context.seat, &actual, error) ||
        !qa_application_client_retirement_current(source->domain.application, &actual))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 disconnect checkpoint lost its physical CLIENT custody");
    frontend_remote_q2_source *entered = (frontend_remote_q2_source *)source;
    ++entered->calls;
    bool held = source->options.retirement_current(source->options.client.context, &actual);
    --entered->calls;
    if (!held || !frontend_remote_q2_source_owner_current(source, actual.descriptor, actual.context.console,
        actual.context.cvars, &actual.context.command, error)) return false;
    *out = (frontend_remote_q2_source_view){source, descriptor, source->receiver, source->domain,
        source->receiver && source->receiver->bound};
    return true;
}
bool frontend_remote_q2_source_retirement_metadata_current(const frontend_remote_q2 *row, qa_error *error)
{
    if (!row || row->options.retirement_current != retirement_current) return false;
    const frontend_remote_q2_source *source = row->options.context;
    const qa_application_client_source *receipt = source ? &source->retirement_receipt : NULL;
    return source && source->retirement_receipt_present && source->retiring && !source->closing &&
        source->configured && source->ready && source->configuration_complete && !source->calls &&
        source->receiver == row && source->frontend == row->frontend && row->retiring && row->bound &&
        !row->busy && !row->image_policy &&
        (!row->importing || row->frontend->source_restoring) &&
        source->frontend->application == source->domain.application &&
        row->options.current == current && remote_q2_domain_equal(&source->domain, &row->options.domain) &&
        receipt->runtime == source->domain.runtime && receipt->context.receiver == source->domain.command_context.owner &&
        receipt->context.seat == source->domain.command_context.seat &&
        receipt->context.physical_seat == source->domain.physical_seat &&
        receipt->configuration_generation == source->domain.configuration_generation &&
        qa_net_client_id_equal(receipt->client, source->domain.client) &&
        receipt->network_seat.owner == source->domain.seat.owner && receipt->network_seat.index == source->domain.seat.index &&
        receipt->connection_epoch == source->domain.epoch &&
        qa_application_client_associated(source->domain.application, receipt) &&
        frontend_remote_q2_source_owner_current(source, receipt->descriptor, receipt->context.console,
            receipt->context.cvars, &receipt->context.command, error) &&
        !qa_net_connections_get(qa_network_connections(source->domain.runtime), source->domain.client);
}
bool frontend_remote_q2_source_pending_protocol(frontend_remote_q2_source *source,
    qa_net_protocol_id protocol, qa_error *error)
{
    qa_q2_codec codec;
    if (!source || !source->configured || source->closing || source->retiring || source->calls || !source->receiver ||
        source->frontend->capture || source->frontend->resource_inventory || source->receiver->bound || source->receiver->busy ||
        source->receiver->retired || source->receiver->importing || source->receiver->image_policy ||
        protocol.kind != source->domain.protocol.kind || !qa_q2_codec_init(&codec, protocol, error))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 negotiation requires its actual pending same-family CLIENT");
    frontend_remote_q2_domain candidate = source->domain; candidate.protocol = protocol;
    ++source->calls;
    bool ok = source->options.client.current(source->options.client.context, &candidate, error);
    --source->calls;
    if (!ok) return false;
    source->domain.protocol = protocol; source->receiver->options.domain.protocol = protocol;
    return true;
}
bool frontend_remote_q2_source_bind(frontend_remote_q2_source *source, const frontend_remote_q2_domain *actual, qa_error *error)
{
    if (!source || !actual || !source->receiver || source->receiver->bound || source->closing || source->retiring) return false;
    frontend_remote_q2_domain previous = source->domain; source->domain = *actual;
    if (!frontend_remote_q2_bind(source->receiver, actual, error)) { source->domain = previous; return false; }
    return true;
}
bool frontend_remote_q2_source_pending_capabilities(frontend_remote_q2_source *source,
    const qa_q2_connect_request *request, bool *out, qa_error *error)
{
    if (!frontend_remote_q2_source_material_scripts(request, out, error)) return false;
    if (!source || !request || !source->ready || source->calls || source->closing || source->retiring || source->template_retired ||
        source->frontend->capture || source->frontend->resource_inventory || !source->receiver ||
        source->receiver->bound || source->receiver->busy || source->receiver->importing ||
        request->protocol.kind != source->domain.protocol.kind || request->protocol.revision != source->domain.protocol.revision ||
        request->protocol.flags != source->domain.protocol.flags || !current(source, &source->domain, error)) return false;
    source->options.client.material_scripts = *out;
    source->receiver->options.material_scripts = *out;
    return true;
}
bool frontend_remote_q2_source_material_scripts(const qa_q2_connect_request *request,
    bool *out, qa_error *error)
{
    if (!request || !out) return false;
    const char *at = request->userinfo; bool found = false, enabled = false;
    if (!memchr(at, 0, sizeof(request->userinfo))) return false;
    while (*at) {
        if (*at++ != '\\') return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 capability USERINFO is not complete key/value data");
        const char *key = at; while (*at && *at != '\\') ++at;
        size_t key_size = (size_t)(at - key);
        if (!*at++) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 capability USERINFO has no value");
        const char *value = at; while (*at && *at != '\\') ++at;
        if (key_size == 12 && !memcmp(key, "qa_materials", 12)) {
            if (found) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 capability USERINFO repeats its material declaration");
            found = true; enabled = at - value == 1 && *value == '1';
        }
    }
    *out = enabled;
    return true;
}
bool frontend_remote_q2_source_drain(frontend_remote_q2_source *source, size_t budget, size_t *executed, qa_error *error)
{
    if (!source || !source->configured || source->closing || source->retiring || source->calls || !source->console ||
        source->frontend->capture || source->frontend->resource_inventory ||
        (source->receiver && source->receiver->image_policy)) return false;
    ++source->calls; bool ok = qa_console_drain(source->console, budget, executed, error); --source->calls; return ok;
}
bool frontend_remote_q2_source_destroy(frontend_remote_q2_source **owned, qa_error *error)
{
    frontend_remote_q2_source *source = owned ? *owned : NULL;
    if (!source) return true;
    if (!frontend_remote_q2_source_retire(source, error)) return false;
    if (source->frontend->capture || source->frontend->resource_inventory || source->calls || source->references != (source->registry ? 2u : 1u) || !qa_console_idle(source->console) ||
        !frontend_client_registry_release_ready(source->registry, error)) return false;
    source->closing = true;
    if (!frontend_remote_q2_destroy(&source->receiver, error)) { source->closing = false; return false; }
    if (source->console_bound && !qa_console_unbind_source(source->console,
        source->domain.command_context.cvar_view, error)) { source->closing = false; return false; }
    source->console_bound = false; source->console = NULL; source->domain.console = NULL;
    if (!frontend_client_registry_retire(&source->registry, error)) return false;
    source->domain.cvars = NULL;
    qa_cvars_destroy(source->pending_cvars); source->pending_cvars = NULL;
    qa_vfs_destroy(source->pending_selected); source->pending_selected = NULL;
    qa_buffer_free(&source->imported_commands); qa_buffer_free(&source->imported_current);
    qa_launch_instance_lease_release(source->metadata); source->metadata = NULL;
    qa_launch_instance_lease_release(source->constructor_metadata); source->constructor_metadata = NULL;
    if (source->references != 1) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source callback holders remain retained");
    void (*released)(void *) = source->options.released; void *context = source->options.client.context;
    free(source); *owned = NULL;
    if (released) released(context);
    return true;
}
bool frontend_remote_q2_source_rebind_ready(const frontend_remote_q2_source *source, qa_frontend *f, qa_error *error)
{
    if (!source || !f || source->closing || source->retiring || !source->configured || !source->receiver ||
        !frontend_remote_q2_source_owner_idle(source) || source->frontend->capture || source->frontend->resource_inventory ||
        f->capture || f->resource_inventory || f->application != source->domain.application ||
        source->frontend->application != source->domain.application || source->domain.physical_seat >= f->options.seats ||
        !f->seats || f->seats[source->domain.physical_seat].input != source->frontend->seats[source->domain.physical_seat].input)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical handoff changed its actual CLIENT view or physical input");
    return frontend_remote_q2_rebind_ready(source->receiver, f, &source->receiver->options, error);
}
void frontend_remote_q2_source_rebind(frontend_remote_q2_source *source, qa_frontend *f)
{
    if (!source || !f) return;
    frontend_remote_q2_rebind(source->receiver, f, &source->receiver->options); source->frontend = f;
}
bool frontend_remote_q2_source_content_visit(const frontend_remote_q2_source *source,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!source || source->closing || source->calls || !source->configured || !visitor ||
        !visitor->catalog || !visitor->pool || !visitor->view) return false;
    const qa_launch_instance *descriptors[2] = {qa_launch_instance_lease_view(source->constructor_metadata),
        qa_launch_instance_lease_view(source->metadata)};
    if (!frontend_remote_q2_source_owner_current(source, descriptors[1], source->console,
        source->domain.cvars, &source->domain.command_context, error)) return false;
    for (size_t i = 0; i < 2; ++i) {
        const qa_launch_instance *descriptor = descriptors[i];
        if (!descriptor || !visitor->catalog(visitor->context, qa_launch_instance_catalog(descriptor), error) ||
            !visitor->pool(visitor->context, qa_vfs_resources(descriptor->content), error) ||
            !visitor->view(visitor->context, descriptor->content, error)) return false;
    }
    return true;
}
bool frontend_remote_q2_source_restore_abort_ready(const frontend_remote_q2_source *source,
    const qa_application_client_source *actual,qa_error *error)
{
    if(!source || !source->frontend->source_restoring || source->restore_finished || !source->restored_constructor ||
        (source->release_programmes && source->imported_queue ? !source->imported_release_discarded :
            qa_console_release_save_present(source->console)))
        return remote_q2_fail(error,QA_ERROR_ARGUMENT,"Q2 candidate has no actual unclaimed import abort receipt");
    return frontend_remote_q2_source_retirement_current(source,actual,actual?actual->context.console:NULL,
        actual?&actual->context.command:NULL,error);
}
