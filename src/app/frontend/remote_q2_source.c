#include "remote_q2_source.h"
#include "remote_q2_private.h"
#include "client_registry.h"
#include "legacy_render_policy.h"
#include "qa/cvars_save.h"
#include "qa/console_cvar_observer.h"
#include <stdlib.h>
#include <string.h>

struct frontend_remote_q2_source {
    qa_frontend *frontend;
    frontend_remote_q2_source_options options;
    qa_launch_instance_lease *metadata;
    qa_launch_instance_lease *constructor_metadata;
    frontend_client_registry *registry;
    frontend_remote_q2 *receiver;
    qa_console *console;
    qa_cvars *pending_cvars;
    qa_vfs *pending_selected;
    qa_buffer imported_commands, imported_current;
    frontend_remote_q2_domain domain;
    size_t references;
    unsigned calls;
    bool closing, configured, ready, commands_verified, template_retired;
};
static bool profile_protocol(const qa_product *profile, qa_net_protocol_id protocol, qa_error *error)
{
    qa_q2_codec codec;
    bool rerelease = protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2KEX_2023 ||
        protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    return profile && qa_q2_codec_init(&codec, protocol, error) &&
        profile->family == QA_GAME_Q2 && profile->edition == (rerelease ? QA_EDITION_RERELEASE : QA_EDITION_CLASSIC);
}
bool frontend_remote_q2_source_recipe(qa_catalog *catalog, qa_net_protocol_id protocol,
    const char *instance, uint32_t seat, qa_launch_q2_client_metadata *out, qa_vfs **prepared, qa_error *error)
{
    qa_q2_codec codec;
    if (!catalog || !instance || !*instance || !out || !prepared || *prepared ||
        !qa_q2_codec_init(&codec, protocol, error))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT recipe requires its actual negotiated protocol and catalog");
    bool rerelease = protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2KEX_2023 ||
        protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    const qa_product *profile = qa_catalog_find(catalog, rerelease ? "q2-rerelease-baseq2" : "q2-classic-baseq2");
    if (!profile || !profile->builtin || profile->family != QA_GAME_Q2 ||
        profile->edition != (rerelease ? QA_EDITION_RERELEASE : QA_EDITION_CLASSIC))
        return remote_q2_fail(error, QA_ERROR_NOT_FOUND, "Q2 CLIENT wire profile has no installed compiled content owner");
    if (!qa_catalog_open(catalog, profile->id, prepared, error)) return false;
    *out = (qa_launch_q2_client_metadata){catalog, profile->id, profile->id, *prepared, instance, seat};
    return true;
}
static bool retain(void *context, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->references == SIZE_MAX)
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
        actual->generation == context->generation && actual->dialect == context->dialect &&
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
        !qa_sha256_equal(&descriptor->identity, &actual->identity) || console != source->console ||
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
    if (!source || source->closing || !source->configured || source->frontend->application != domain->application ||
        !remote_q2_domain_equal(&source->domain, domain) || !source->registry ||
        source->domain.cvars != frontend_client_registry_cvars(source->registry) ||
        !frontend_client_registry_matches(source->registry, qa_launch_instance_lease_view(source->constructor_metadata),
            source->options.metadata.seat) || (source->receiver &&
            source->receiver->options.material_scripts != source->options.client.material_scripts)) return false;
    return source->options.client.current(source->options.client.context, domain, error);
}
static bool active(void *context, const qa_command_context *command)
{
    frontend_remote_q2_source *source = context; qa_error error = {0};
    return source && !source->closing && tuple(source, command) &&
        (!source->receiver || !source->receiver->bound || current(source, &source->domain, &error));
}
static bool capture(void *context, const qa_command_context *in, qa_command_context *out, qa_error *error)
{
    if (!out || !active(context, in)) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 console caller left its actual CLIENT tuple");
    *out = *in; return true;
}
static void print(void *context, const qa_command_context *command, const char *text)
{
    frontend_remote_q2_source *source = context;
    if (!source || !tuple(source, command)) return;
    ++source->calls; source->options.print(source->options.client.context, command, text); --source->calls;
}
static void cvar_print(void *context, const char *text)
{ frontend_remote_q2_source *source = context; print(source, &source->domain.command_context, text); }
static qa_cvars *cvars(void *context, const qa_command_context *command, const char *name)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, command)) return NULL;
    if (!source->options.cvar_owner) return source->domain.cvars;
    ++source->calls; qa_cvars *owner = source->options.cvar_owner(source->options.client.context, command, name); --source->calls;
    return owner;
}
static qa_cvars *visible(void *context, const qa_command_context *command, size_t ordinal)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, command)) return NULL;
    if (!source->options.visible_cvars) return ordinal ? NULL : source->domain.cvars;
    ++source->calls; qa_cvars *owner = source->options.visible_cvars(source->options.client.context, command, ordinal); --source->calls;
    return owner;
}
static bool edit(void *context, const qa_command_context *command, qa_cvars *heap, struct qa_cvars_edit **out, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!active(source, command) || !source->options.cvar_edit) return false;
    ++source->calls; bool ok = source->options.cvar_edit(source->options.client.context, command, heap, out, error); --source->calls;
    return ok;
}
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
    if (!active(source, &invocation->context) || !source->receiver || !source->receiver->bound || !invocation->argc)
        return QA_COMMAND_UNHANDLED;
    uint32_t remote_index;
    if (!frontend_remote_q2_wire_seat(source->receiver, &remote_index, error)) return QA_COMMAND_FAILED;
    return qa_network_q2_client_command(source->domain.runtime, source->domain.client, invocation->raw,
        (uint8_t)(remote_index + 1), error) ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}
static bool allowed(void *context, const char *path, bool *result, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.download_allowed(source->options.client.context, path, result, error); }
static bool nonce(void *context, uint64_t *out, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.download_nonce(source->options.client.context, out, error); }
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
static bool disconnected(void *context, const frontend_remote_q2_domain *domain, const char *reason, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.disconnected(source->options.client.context, domain, reason, error); }
static bool select_content(void *context, uint64_t generation, const qa_q2_serverdata *data,
    frontend_remote_q2_content *out, qa_q2_preparation *result, qa_error *error)
{ frontend_remote_q2_source *source = context; return source->options.client.select_content(source->options.client.context, generation, data, out, result, error); }
static bool content_admit(void *context, uint64_t loading, const frontend_remote_q2_content *content, qa_error *error)
{
    frontend_remote_q2_source *source = context;
    if (!source || source->closing || source->calls || !source->receiver || !source->receiver->bound ||
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
    source->metadata = metadata; source->domain = candidate; source->receiver->options.domain = candidate;
    ++source->calls;
    bool ok = source->options.admit_content(source->options.client.context, &previous,
        qa_launch_instance_lease_view(metadata), &candidate, error);
    --source->calls;
    if (!ok) {
        source->metadata = old; source->domain = previous; source->receiver->options.domain = previous;
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
        {"cl_predict", "1", 0}, {"cl_gun", "1", 0}, {"cl_blend", "1", 0}, {"cl_lights", "1", 0},
        {"cl_particles", "1", 0}, {"cl_entities", "1", 0}, {"cl_footsteps", "1", 0}, {"cl_noskins", "0", 0},
        {"cl_vwep", "1", QA_CVAR_ARCHIVE}, {"cl_hit_markers", "2", 0}, {"scr_hit_marker_time", "500", 0},
        {"ch_alpha", "1", 0}, {"ch_scale", "1", 0}, {"ch_x", "0", 0}, {"ch_y", "0", 0},
        {"cl_muzzlelight_time", "100", 0}, {"cl_rerelease_effects", "1", 0}, {"cl_dlight_hacks", "0", 0},
        {"cl_muzzleflashes", "1", 0}, {"cl_disable_particles", "0", 0}, {"cl_disable_explosions", "0", 0}
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
        !options->client.download_allowed || !options->client.download_nonce || !options->client.records || !options->client.disconnected ||
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
    qa_cvar_options variables = {.dialect = dialect, .user = source, .print = cvar_print};
    source->pending_cvars = qa_cvars_create(&variables, error); source->domain.cvars = source->pending_cvars;
    if (!source->pending_cvars || !defaults(source, error)) return false;
    if (options->initialize) {
        ++source->calls; bool initialized = options->initialize(options->client.context,
            qa_launch_instance_lease_view(source->metadata), source->domain.cvars, &source->domain.command_context, error); --source->calls;
        if (!initialized) return false;
    }
    frontend_client_registry_context callback = {source, retain, release};
    if (!frontend_client_registry_create(f, qa_launch_instance_lease_view(source->metadata), options->metadata.seat,
        &source->pending_cvars, &callback, &source->registry, error)) return false;
    source->domain.cvars = frontend_client_registry_cvars(source->registry);
    qa_console_options console = {.context = source->domain.command_context, .cvars = source->domain.cvars, .user = source,
        .print = print, .cvar_owner = cvars, .visible_cvars = visible, .capture_context = capture, .context_active = active,
        .cvar_edit = options->cvar_edit ? edit : NULL, .script_complete = options->script_complete ? script_complete : NULL,
        .allow_command = options->allow_command ? allow : NULL,
        .read_script = script, .release_script = script_release, .source_command = command, .forward = forward};
    source->console = qa_console_create(&console, error); source->domain.console = source->console;
    if (!source->console) return false;
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
    if (!source || !ready || !source->configured || source->closing || source->template_retired ||
        !frontend_remote_q2_source_owner_idle(source) || source->frontend->capture ||
        source->frontend->resource_inventory || source->frontend->source_restoring) return false;
    *ready = source->ready;
    if (*ready) return true;
    if (source->receiver) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 receiver construction failed and retains checked cleanup");
    bool complete = true;
    if (source->options.configure_step) {
        complete = false; ++source->calls;
        bool ok = source->options.configure_step(source->options.client.context, &complete, error); --source->calls;
        if (!ok || !current(source, &source->domain, error)) return false;
    }
    if (!complete) return true;
    const frontend_remote_q2_source_options *options = &source->options;
    frontend_remote_q2_options child = {.domain = source->domain, .context = source, .current = current,
        .material_scripts = options->client.material_scripts,
        .download_allowed = allowed, .download_nonce = nonce, .records = records, .disconnected = disconnected,
        .entity_actor = options->client.entity_actor ? entity_actor : NULL,
        .content_admit = content_admit,
        .select_content = options->client.select_content ? select_content : NULL};
    bool ok = frontend_remote_q2_create(source->frontend, &child, &source->receiver, error);
    if (ok) source->ready = *ready = true;
    return ok;
}
bool frontend_remote_q2_source_retire(frontend_remote_q2_source *source, qa_error *error)
{
    if (!source || source->template_retired) return true;
    bool returned = source->frontend->source_restoring ? frontend_remote_q2_source_owner_retirement_idle(source) :
        frontend_remote_q2_source_owner_idle(source);
    if (!returned || source->frontend->capture || source->frontend->resource_inventory || source->closing) return false;
    if (source->options.retire) {
        ++source->calls; bool ok = source->options.retire(source->options.client.context, error); --source->calls;
        if (!ok) return false;
    }
    source->template_retired = true; return true;
}
bool frontend_remote_q2_source_read(const frontend_remote_q2_source *source, frontend_remote_q2_source_view *out, qa_error *error)
{
    if (!source || !out || !source->configured || !source->ready || source->closing || source->template_retired || !source->metadata || !source->receiver || !source->registry)
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
bool frontend_remote_q2_source_pending_protocol(frontend_remote_q2_source *source,
    qa_net_protocol_id protocol, qa_error *error)
{
    qa_q2_codec codec;
    if (!source || !source->configured || source->closing || source->calls || !source->receiver ||
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
    if (!source || !actual || !source->receiver || source->receiver->bound || source->closing) return false;
    frontend_remote_q2_domain previous = source->domain; source->domain = *actual;
    if (!frontend_remote_q2_bind(source->receiver, actual, error)) { source->domain = previous; return false; }
    return true;
}
bool frontend_remote_q2_source_pending_capabilities(frontend_remote_q2_source *source,
    const qa_q2_connect_request *request, qa_error *error)
{
    if (!source || !request || !source->ready || source->calls || source->closing || source->template_retired ||
        source->frontend->capture || source->frontend->resource_inventory || !source->receiver ||
        source->receiver->bound || source->receiver->busy || source->receiver->importing ||
        request->protocol.kind != source->domain.protocol.kind || request->protocol.revision != source->domain.protocol.revision ||
        request->protocol.flags != source->domain.protocol.flags || !current(source, &source->domain, error)) return false;
    bool enabled = false;
    if (!frontend_remote_q2_source_material_scripts(request, &enabled, error)) return false;
    source->options.client.material_scripts = enabled;
    source->receiver->options.material_scripts = enabled;
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
    if (!source || !source->configured || source->closing || source->calls || !source->console ||
        source->frontend->capture || source->frontend->resource_inventory ||
        (source->receiver && source->receiver->image_policy)) return false;
    ++source->calls; bool ok = qa_console_drain(source->console, budget, executed, error); --source->calls; return ok;
}
bool frontend_remote_q2_source_destroy(frontend_remote_q2_source **owned, qa_error *error)
{
    frontend_remote_q2_source *source = owned ? *owned : NULL;
    if (!source) return true;
    if (!frontend_remote_q2_source_retire(source, error)) return false;
    if (source->frontend->capture || source->frontend->resource_inventory || source->calls || source->references != (source->registry ? 2u : 1u) || !qa_console_destroy_ready(source->console) ||
        !frontend_client_registry_release_ready(source->registry, error)) return false;
    source->closing = true;
    if (!frontend_remote_q2_destroy(&source->receiver, error)) { source->closing = false; return false; }
    qa_console_destroy(source->console); source->console = NULL; source->domain.console = NULL;
    if (!frontend_client_registry_release(&source->registry, error)) return false;
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
    if (!source || !f || source->closing || !source->configured || !source->receiver ||
        !frontend_remote_q2_source_owner_idle(source) || source->frontend->capture || source->frontend->resource_inventory ||
        f->capture || f->resource_inventory || f->application != source->domain.application ||
        source->frontend->application != source->domain.application || source->domain.physical_seat >= f->options.seats ||
        !f->seats || f->seats[source->domain.physical_seat].input != source->frontend->seats[source->domain.physical_seat].input)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical handoff changed its actual CLIENT heap or physical input");
    return frontend_remote_q2_rebind_ready(source->receiver, f, &source->receiver->options, error);
}
void frontend_remote_q2_source_rebind(frontend_remote_q2_source *source, qa_frontend *f)
{
    if (!source || !f) return;
    frontend_remote_q2_rebind(source->receiver, f, &source->receiver->options); source->frontend = f;
}
bool frontend_remote_q2_source_capture(const frontend_remote_q2_source *source,
    frontend_remote_q2_source_state *out, qa_error *error)
{
    if (!source || !out || out->console.data || out->console.size || source->closing ||
        !source->configured || source->calls || !source->receiver || source->receiver->image_policy ||
        source->receiver->busy || source->receiver->importing || !qa_console_idle(source->console) ||
        !qa_cvars_observer_idle(source->domain.cvars) ||
        (source->frontend->capture ? !remote_q2_capture_owned(source->receiver) :
            !frontend_remote_q2_idle(source->frontend)))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical checkpoint requires returned genuine owners");
    frontend_remote_q2_source_state state = {.constructor = qa_launch_instance_lease_view(source->constructor_metadata),
        .selected = qa_launch_instance_lease_view(source->metadata), .domain = source->domain};
    if (!state.constructor || !state.selected || !qa_console_save_capture(source->console,
        qa_application_session(source->domain.application), &state.console, error)) return false;
    *out = state; return true;
}
void frontend_remote_q2_source_state_free(frontend_remote_q2_source_state *state)
{
    if (!state) return;
    qa_buffer_free(&state->console); *state = (frontend_remote_q2_source_state){0};
}
bool frontend_remote_q2_source_content_visit(const frontend_remote_q2_source *source,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!source || source->closing || source->calls || !source->configured || !visitor ||
        !visitor->catalog || !visitor->pool || !visitor->view) return false;
    const qa_launch_instance *descriptors[2] = {qa_launch_instance_lease_view(source->constructor_metadata),
        qa_launch_instance_lease_view(source->metadata)};
    for (size_t i = 0; i < 2; ++i) {
        const qa_launch_instance *descriptor = descriptors[i];
        if (!descriptor || !visitor->catalog(visitor->context, qa_launch_instance_catalog(descriptor), error) ||
            !visitor->pool(visitor->context, qa_vfs_resources(descriptor->content), error) ||
            !visitor->view(visitor->context, descriptor->content, error)) return false;
    }
    return true;
}
bool frontend_remote_q2_source_restore_prepare(qa_frontend *f,
    const frontend_remote_q2_source_options *options, const frontend_remote_q2_source_restore *saved,
    frontend_remote_q2_source **out, qa_error *error)
{
    const frontend_remote_q2_domain *domain = saved ? &saved->domain : NULL;
    if (!f || !f->source_restoring || f->capture || f->resource_inventory || !f->client_registry_import ||
        !options || !saved || !out || *out || !saved->constructor || !saved->constructor->catalog || !saved->constructor->content ||
        !saved->physical_admit || !saved->console_resolvers || !saved->receiver_refs ||
        !saved->console.data || !saved->console.size || !saved->receiver.data || !saved->receiver.size ||
        !options->print || !options->admit_content || !options->client.current || !!options->read_script != !!options->release_script ||
        !options->client.download_allowed || !options->client.download_nonce || !options->client.records ||
        !options->client.disconnected || domain->application != f->application || !domain->runtime ||
        (domain->client.owner ? (!domain->client.generation || !domain->epoch || !domain->seat.owner) :
            (domain->client.generation || domain->client.slot || domain->epoch || domain->seat.owner || domain->seat.index)) ||
        domain->physical_seat >= f->options.seats ||
        domain->console || domain->cvars || !domain->command_context.owner ||
        !domain->command_context.registry || !domain->command_context.generation || domain->command_context.script ||
        domain->catalog != saved->constructor_request.catalog || domain->product != saved->constructor_request.profile ||
        domain->command_context.seat != saved->constructor_request.seat ||
        (saved->selected && (!saved->selected->catalog || !saved->selected->content ||
            saved->selected->content == saved->constructor->content ||
            saved->selected_request.seat != saved->constructor_request.seat ||
            !saved->selected_request.instance || !saved->constructor_request.instance ||
            strcmp(saved->selected_request.instance, saved->constructor_request.instance))))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical import requires its actual staged heap and claimed descriptors");
    frontend_remote_q2_source *source = calloc(1, sizeof(*source));
    if (!source) return remote_q2_fail(error, QA_ERROR_MEMORY, "Restoring Q2 physical Source");
    *out = source; source->frontend = f; source->options = *options;
    source->options.metadata = saved->constructor_request; source->domain = *domain; source->references = 1;
    source->pending_selected = saved->selected ? saved->selected->content : NULL;
    if (!qa_launch_instance_restore_q2_client_metadata(&saved->constructor_request, saved->constructor,
        &source->constructor_metadata, error)) return false;
    const qa_launch_instance *constructor = qa_launch_instance_lease_view(source->constructor_metadata);
    source->options.metadata.instance = constructor->selection.instance;
    source->options.metadata.prepared = constructor->content;
    if (saved->selected) {
        source->pending_selected = NULL; /* The metadata producer takes this claimed view on every outcome. */
        if (!qa_launch_instance_restore_q2_client_metadata(&saved->selected_request, saved->selected,
            &source->metadata, error)) return false;
    } else if (!qa_launch_instance_retain_metadata(constructor, &source->metadata, error)) return false;
    const qa_launch_instance *selected = qa_launch_instance_lease_view(source->metadata);
    if (strcmp(selected->selection.implementation, constructor->selection.implementation) ||
        selected->selection.clock.kind != constructor->selection.clock.kind)
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 restored selection changed its physical compiled profile");
    const qa_product *profile = qa_catalog_product(source->options.metadata.catalog, source->options.metadata.profile);
    qa_console_dialect dialect = profile && profile->edition == QA_EDITION_RERELEASE ? QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
    if (!profile_protocol(profile, domain->protocol, error) || domain->command_context.dialect != dialect) return false;
    qa_cvar_options variables = {.dialect = dialect, .user = source, .print = cvar_print};
    source->pending_cvars = qa_cvars_create(&variables, error); source->domain.cvars = source->pending_cvars;
    if (!source->pending_cvars) return false;
    frontend_client_registry_context callback = {source, retain, release};
    if (!frontend_client_registry_create(f, constructor, source->options.metadata.seat,
        &source->pending_cvars, &callback, &source->registry, error)) return false;
    source->domain.cvars = frontend_client_registry_cvars(source->registry);
    qa_console_options console = {.context = source->domain.command_context, .cvars = source->domain.cvars, .user = source,
        .print = print, .cvar_owner = cvars, .visible_cvars = visible, .capture_context = capture, .context_active = active,
        .cvar_edit = options->cvar_edit ? edit : NULL, .script_complete = options->script_complete ? script_complete : NULL,
        .allow_command = options->allow_command ? allow : NULL,
        .read_script = script, .release_script = script_release, .source_command = command, .forward = forward};
    source->console = qa_console_create(&console, error); source->domain.console = source->console;
    if (!source->console) return false;
    ++source->calls;
    bool admitted = saved->physical_admit(options->client.context, qa_launch_instance_lease_view(source->metadata),
        source->domain.cvars, source->console, error);
    --source->calls;
    if (!admitted) return false;
    source->configured = true;
    if (options->install) {
        ++source->calls; bool installed = options->install(options->client.context, true, error); --source->calls;
        if (!installed) return false;
    }
    source->imported_commands.data = malloc(saved->console.size);
    if (!source->imported_commands.data) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 console import receipt");
    source->imported_commands.size = saved->console.size;
    memcpy(source->imported_commands.data, saved->console.data, saved->console.size);
    if (!qa_console_save_restore(source->console, qa_application_session(domain->application),
        saved->console_resolvers, saved->console, error) ||
        !qa_console_save_capture(source->console, qa_application_session(domain->application),
            &source->imported_current, error)) return false;
    frontend_remote_q2_options child = {.domain = source->domain, .context = source, .current = current,
        .material_scripts = options->client.material_scripts,
        .download_allowed = allowed, .download_nonce = nonce, .records = records, .disconnected = disconnected,
        .entity_actor = options->client.entity_actor ? entity_actor : NULL, .content_admit = content_admit,
        .select_content = options->client.select_content ? select_content : NULL};
    bool restored = frontend_remote_q2_restore_prepare(f, &child, saved->receiver_refs, saved->receiver,
        &source->receiver, error);
    if (restored) source->ready = true;
    return restored;
}
static bool console_scope(const frontend_remote_q2_source *source, qa_application *app,
    const qa_application_console_scope *scope, const qa_console *console)
{
    qa_application_console_scope actual;
    return source && !source->closing && source->configured && !source->calls && app == source->domain.application &&
        source->frontend->application == app && console == source->console && scope &&
        scope->kind == QA_APPLICATION_CONSOLE_CLIENT && scope->provider == source->domain.command_context.owner &&
        scope->seat == source->domain.command_context.seat && qa_console_idle(console) &&
        qa_application_console_scope_read(app, console, &actual) && actual.kind == scope->kind &&
        actual.provider == scope->provider && actual.seat == scope->seat;
}
bool frontend_remote_q2_source_commands_capture(const frontend_remote_q2_source *source, qa_application *app,
    const qa_application_console_scope *scope, const qa_console *console, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !console_scope(source, app, scope, console))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 command capture names a different physical CLIENT console");
    return qa_console_save_capture(console, qa_application_session(app), out, error);
}
bool frontend_remote_q2_source_commands_restore(frontend_remote_q2_source *source, qa_application *app,
    const qa_application_console_scope *scope, qa_console *console, qa_bytes bytes, qa_error *error)
{
    if (!console_scope(source, app, scope, console) || !source->frontend->source_restoring ||
        !source->imported_commands.data || !source->imported_current.data ||
        bytes.size != source->imported_commands.size || !bytes.data ||
        memcmp(bytes.data, source->imported_commands.data, bytes.size))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 aggregate commands differ from the genuine physical prefix");
    qa_buffer current = {0};
    bool ok = qa_console_save_capture(console, qa_application_session(app), &current, error) &&
        current.size == source->imported_current.size &&
        !memcmp(current.data, source->imported_current.data, current.size);
    qa_buffer_free(&current);
    if (!ok) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 imported CLIENT commands changed before aggregate admission");
    source->commands_verified = true; return true;
}
bool frontend_remote_q2_source_finish_restore(frontend_remote_q2_source *source, qa_error *error)
{
    if (!source || source->closing || source->calls || !source->frontend->source_restoring ||
        !source->configured || !source->receiver || source->receiver->importing || !source->commands_verified ||
        !source->imported_commands.data || !source->imported_current.data)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 physical restore retains unfinished receiver or command admission");
    qa_buffer current = {0};
    bool ok = qa_console_save_capture(source->console, qa_application_session(source->domain.application), &current, error) &&
        current.size == source->imported_current.size &&
        !memcmp(current.data, source->imported_current.data, current.size);
    qa_buffer_free(&current);
    if (!ok) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 imported console changed before physical finish");
    qa_buffer_free(&source->imported_commands); qa_buffer_free(&source->imported_current);
    return true;
}
