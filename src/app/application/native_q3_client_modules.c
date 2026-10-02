#include "native_q3_client_modules_private.h"
#include "qa/network_q3.h"
#include "qa/q3_host_save.h"
#include "qa/script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool consume(native_client_module *, qa_error *);

static application_provider *receiver(qa_application *app, qa_actor_owner owner)
{
    application_provider *found = NULL;
    application_provider **rows = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    for (size_t i = 0; i < count; ++i) if (rows[i] && rows[i]->owner == owner) {
        if (found) return NULL;
        found = rows[i];
    }
    return found;
}

static bool same_source(const qa_application_q3_remote_source *a,
    const qa_application_q3_remote_source *b)
{
    return a && b && a->descriptor && b->descriptor &&
        a->descriptor->storage == b->descriptor->storage &&
        a->descriptor->content == b->descriptor->content &&
        qa_sha256_equal(&a->descriptor->identity, &b->descriptor->identity) &&
        a->configuration_generation == b->configuration_generation &&
        a->connection_epoch == b->connection_epoch &&
        a->receiver.receiver == b->receiver.receiver && a->receiver.seat == b->receiver.seat &&
        a->receiver.session == b->receiver.session && a->receiver.console == b->receiver.console &&
        a->receiver.cvars == b->receiver.cvars && a->receiver.service_owner == b->receiver.service_owner;
}

bool qa_application_native_q3_client_arguments_read(qa_application *app,
    const qa_application_q3_remote_source *source, qa_native_host_command_view *out,
    uint64_t *revision, qa_error *error)
{
    application_provider *provider = app && source ? receiver(app, source->receiver.receiver) : NULL;
    const qa_command_tokens *arguments = NULL; uint64_t actual_revision;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !out || !revision ||
        !qa_application_q3_remote_source_current(app, source) ||
        !application_native_q3_remote_role_arguments(provider, source->receiver.seat,
            &arguments, &actual_revision, error) || !arguments ||
        !qa_application_q3_remote_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT arguments lost their true physical parser");
    *out = (qa_native_host_command_view){.count = arguments->count,
        .arguments = (const char *const *)arguments->values, .tail = arguments->args_text ? arguments->args_text : ""};
    *revision = actual_revision; return true;
}

bool native_client_modules_physical(const application_native_q3_client_modules *owner,
    qa_application_q3_remote_source *out, qa_error *error)
{
    qa_application_q3_remote_source actual;
    if (!owner || !owner->attached || !out ||
        !application_native_q3_remote_role_source_read(owner->provider, owner->source.receiver.seat,
            owner->source.connection_epoch, &actual, error) || !same_source(&owner->source, &actual) ||
        !application_native_q3_remote_role_modules_current(owner->provider, &actual, owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT modules lost their physical source");
    *out = actual; return true;
}

static bool process_current(void *context, const qa_launch_instance *descriptor,
    qa_actor_owner receiver_owner, uint64_t service_owner, qa_error *error)
{
    native_client_module *role = context;
    application_native_q3_client_modules *owner = role ? role->owner : NULL;
    qa_application_q3_remote_source actual;
    if (!owner || !descriptor || !owner->source.descriptor || !role->module ||
        !role->artifact.resource || receiver_owner != owner->source.receiver.receiver ||
        service_owner != role->service_owner ||
        !qa_sha256_equal(&descriptor->identity, &owner->source.descriptor->identity))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT process lost its actual acquired identity");
    if (owner->retiring)
        return application_native_q3_remote_role_modules_retained(owner->provider, &owner->source, owner) ||
            application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT retirement lost its retained parent");
    return native_client_modules_physical(owner, &actual, error);
}

bool qa_application_native_q3_client_modules_current(const application_native_q3_client_modules *owner,
    const qa_application_q3_remote_source *source)
{
    return owner && (!owner->video || owner->video_entering) &&
        !owner->retiring && !owner->restore_pending && same_source(&owner->source, source) &&
        qa_application_q3_remote_source_current(owner->app, source) &&
        application_native_q3_remote_role_modules_current(owner->provider, source, owner);
}

bool native_client_modules_executors_idle(const application_native_q3_client_modules *owner)
{
    if (!owner || owner->calls || owner->initializing || owner->entered || owner->command_arguments) return false;
    const native_client_module *roles[] = {&owner->ui, &owner->cgame};
    for (size_t i = 0; i < 2; ++i) {
        const native_client_module *role = roles[i];
        if ((role->vm && !qa_qvm_can_destroy(role->vm)) ||
            (role->native && !qa_native_host_destroy_ready(role->native)) ||
            (role->host && !qa_q3_host_destroy_ready(role->host)) ||
            role->draw_entry || !application_q3_equipment_idle(role->equipment) ||
            !application_q3_body_idle(role->body)) return false;
    }
    return true;
}

bool qa_application_native_q3_client_modules_idle(const application_native_q3_client_modules *owner)
{
    return owner && (!owner->video || owner->video_entering) && native_client_modules_executors_idle(owner);
}

static bool decoded_pure(const qa_q3_gamestate *state, bool *pure, qa_error *error)
{
    *pure = false;
    if (!state) return true;
    char value[QA_Q3_BIG_INFO_CHARS];
    if (!qa_q3_info_value(qa_q3_configstring(state, 1), "sv_pure", value, sizeof(value), error)) return false;
    *pure = strtol(value, NULL, 10) != 0; return true;
}

bool native_client_modules_policy(const qa_q3_gamestate *state, bool pure, qa_error *error)
{
    bool actual;
    return decoded_pure(state, &actual, error) && (actual == pure ||
        application_fail(error, QA_ERROR_FORMAT, "Acquired CLIENT policy differs from actual received SystemInfo"));
}

bool native_client_modules_allocate(qa_application *app,
    const qa_application_native_q3_client_modules_options *options, bool restoring,
    application_native_q3_client_modules **out, qa_error *error)
{
    if (!app || !options || !out || *out || !options->prepare || !options->current ||
        !options->source.descriptor || !options->source.descriptor->content ||
        options->source.descriptor->selection.runtime != QA_PROGRAM_BUILTIN ||
        options->source.descriptor->artifact || !options->source.receiver.native_source ||
        !options->source.receiver.cvars || !options->source.receiver.console || app->destroy_requested ||
        (restoring ? app->operation != APPLICATION_PERSISTING :
            app->operation != APPLICATION_IDLE && app->operation != APPLICATION_CONFIGURING))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT modules require their real builtin preparation");
    application_provider *provider = receiver(app, options->source.receiver.receiver);
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || provider->close_pending ||
        !application_native_q3_remote_role_source_current(provider, &options->source) ||
        (!restoring && !qa_application_q3_remote_source_current(app, &options->source)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT modules require the actual physical receiver");
    application_native_q3_client_modules *prior = NULL;
    if (!application_native_q3_remote_role_modules_read(provider, &options->source, &prior, error) || prior)
        return application_fail(error, QA_ERROR_ARGUMENT, "Physical CLIENT already retains acquired modules");
    if (!restoring && !options->current(options->context, &options->source, options->gamestate, error)) return false;
    application_native_q3_client_modules *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining acquired CLIENT module owner");
    owner->app = app; owner->provider = provider; owner->source = options->source;
    owner->options = *options; owner->restore_pending = restoring;
    owner->ui.owner = owner; owner->ui.kind = QA_QVM_UI;
    owner->cgame.owner = owner; owner->cgame.kind = QA_QVM_CGAME;
    *out = owner;
    if (!restoring && !qa_script_defines_create(&owner->script_globals, error)) return false;
    if (!qa_launch_instance_retain_metadata(options->source.descriptor, &owner->metadata, error)) return false;
    owner->source.descriptor = qa_launch_instance_lease_view(owner->metadata);
    if (!application_native_q3_remote_role_modules_attach(provider, &options->source, owner, error)) return false;
    owner->attached = true; return true;
}

void native_client_opening_dispose(native_client_opening *opening)
{
    qa_resource_release(opening->resource);
    qa_vfs_acquisition_dispose(&opening->acquisition);
    free(opening->path); *opening = (native_client_opening){0};
}

static bool opening(native_client_module *role, const char *path,
    native_client_opening *out, qa_error *error)
{
    size_t length = strlen(path);
    out->path = malloc(length + 1);
    if (!out->path) return application_fail(error, QA_ERROR_MEMORY, "Retaining acquired CLIENT artifact path");
    memcpy(out->path, path, length + 1);
    return qa_vfs_acquire_receipt(role->owner->source.descriptor->content, path,
        &out->resource, &out->acquisition, error);
}

bool native_client_module_qualify(native_client_module *role, qa_error *error)
{
    if (!role->artifact.resource || !role->artifact.path ||
        !qa_vfs_acquisition_retained(role->owner->source.descriptor->content, &role->artifact.acquisition, error) ||
        (role->declaration.resource && !qa_vfs_acquisition_retained(role->owner->source.descriptor->content,
            &role->declaration.acquisition, error))) return false;
    role->abi = QA_QVM_Q3_MODERN;
    if (role->image) {
        qa_qvm_compatibility compatibility = {0};
        bool okay = !role->declaration.resource || qa_qvm_compatibility_parse(
            qa_resource_bytes(role->declaration.resource), role->artifact.path,
            qa_qvm_image_digest(role->image), role->kind, &compatibility, error);
        if (okay) {
            role->abi = compatibility.abi;
            if (role->kind == QA_QVM_CGAME) okay = application_q3_equipment_profile_read(role->image,
                role->kind, role->abi, (qa_bytes){compatibility.equipment_presentation.data,
                    compatibility.equipment_presentation.size}, &role->profile, error);
        }
        qa_qvm_compatibility_free(&compatibility); return okay;
    }
    return !role->declaration.resource || qa_native_declaration_load(
        qa_resource_bytes(role->declaration.resource), role->artifact.path, role->module,
        &role->native_declaration, error);
}

static bool declaration_open(native_client_module *role, bool bytecode, qa_error *error)
{
    const qa_launch_instance *source = role->owner->source.descriptor;
    qa_catalog *catalog = qa_launch_instance_catalog(source);
    const char *path = NULL;
    for (size_t i = 0; i < qa_catalog_mod_count(catalog); ++i) {
        const qa_catalog_mod *mod = qa_catalog_mod_at(catalog, i);
        if (mod->product != source->selection.product || mod->unavailable || !mod->declaration_path ||
            !mod->program_path || strcmp(mod->program_path, role->artifact.path) ||
            mod->runtime != (bytecode ? QA_PROGRAM_QVM : QA_PROGRAM_NATIVE) ||
            !qa_sha256_equal(&mod->program_digest, qa_resource_digest(role->artifact.resource))) continue;
        if (path && strcmp(path, mod->declaration_path))
            return application_fail(error, QA_ERROR_FORMAT, "Ambiguous acquired CLIENT declaration");
        path = mod->declaration_path;
    }
    if (!path && bytecode) {
        bool found = false; uint64_t extent;
        if (!qa_vfs_probe(source->content, "qvm-compatibility.json", &found, &extent, error)) return false;
        if (found) path = "qvm-compatibility.json";
    }
    return !path || opening(role, path, &role->declaration, error);
}

static const char *native_ui_path(void)
{
    qa_native_target target = qa_native_host_target();
    if (target.arch != QA_NATIVE_ARCH_I386) return NULL;
    return target.os == QA_NATIVE_OS_WINDOWS ? "uix86.dll" :
        target.os == QA_NATIVE_OS_LINUX ? "uii386.so" : NULL;
}

bool qa_application_native_q3_client_modules_recipe_read(qa_application *app,
    const qa_application_q3_remote_source *source, const qa_q3_gamestate *state,
    qa_application_native_q3_client_modules_recipe *out, qa_error *error)
{
    application_provider *provider = app && source ? receiver(app, source->receiver.receiver) : NULL;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !out ||
        !qa_application_q3_remote_source_current(app, source) ||
        (state && (state->client_number < 0 || state->client_number >= 64)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Builtin CLIENT recipe requires its actual source and received state");
    qa_application_native_q3_client_modules_recipe recipe = {.ui_path = "vm/ui.qvm",
        .ui_runtime = QA_PROGRAM_QVM, .cgame_runtime = QA_PROGRAM_BUILTIN};
    if (!decoded_pure(state, &recipe.pure, error)) return false;
    if (recipe.pure) { recipe.cgame_path = "vm/cgame.qvm"; recipe.cgame_runtime = QA_PROGRAM_QVM; }
    else {
        const qa_cvar_view *choice = qa_cvars_find(source->receiver.cvars, "vm_ui");
        const qa_cvar_view *restricted = qa_cvars_find(source->receiver.cvars, "fs_restrict");
        const char *path = native_ui_path();
        if (path && choice && isfinite(choice->number) && truncf(choice->number) == 0 &&
            (!restricted || restricted->number == 0)) {
            recipe.ui_path = path; recipe.ui_runtime = QA_PROGRAM_NATIVE;
            recipe.ui_fallback_path = "vm/ui.qvm";
        }
    }
    *out = recipe; return true;
}

static bool artifact_open(native_client_module *role, bool native_ui, qa_error *error)
{
    const char *path = native_ui ? native_ui_path() : NULL;
    if (path) {
        bool found = false; uint64_t extent;
        if (!qa_vfs_probe(role->owner->source.descriptor->content, path, &found, &extent, error)) return false;
        if (found) {
            if (!opening(role, path, &role->artifact, error)) return false;
            qa_error native_error = {0};
            bool admitted = qa_native_module_load(qa_resource_bytes(role->artifact.resource), path,
                QA_NATIVE_Q3_VMMAIN, NULL, &role->module, &native_error);
            if (!admitted && native_error.code == QA_ERROR_MEMORY) { if (error) *error = native_error; return false; }
            if (admitted) {
                qa_native_target target = qa_native_module_describe(role->module).image.target;
                qa_native_target expected = qa_native_host_target();
                admitted = target.os == expected.os && target.arch == expected.arch &&
                    target.abi == expected.abi && target.pointer_bytes == expected.pointer_bytes;
            }
            if (admitted) return declaration_open(role, false, error) && native_client_module_qualify(role, error);
            qa_native_module_release(role->module); role->module = NULL;
            native_client_opening_dispose(&role->artifact);
        }
    }
    path = role->kind == QA_QVM_UI ? "vm/ui.qvm" : "vm/cgame.qvm";
    return opening(role, path, &role->artifact, error) &&
        qa_qvm_image_load(qa_resource_bytes(role->artifact.resource), &role->image, error) &&
        declaration_open(role, true, error) && native_client_module_qualify(role, error) &&
        (role->kind != QA_QVM_CGAME || native_client_module_body_open(role, error));
}

static bool source_entity(void *context, const qa_qvm_call *call, int32_t pointer,
    const qa_q3_ref_entity *entity, bool *suppress, qa_error *error)
{
    native_client_module *role = context;
    if (!role->equipment)
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CGAME submission lost its equipment owner");
    if (!application_q3_equipment_source_entity(role->equipment, call, pointer, entity, suppress, error)) return false;
    return *suppress || !role->body ||
        application_q3_body_source_entity(role->body, call, pointer, entity, suppress, error);
}

bool native_client_module_namespace(native_client_module *role, bool restoring, qa_error *error)
{
    application_native_q3_client_modules *owner = role->owner;
    qa_application_q3_remote_source actual;
    if (!native_client_modules_physical(owner, &actual, error)) return false;
    uint64_t maximum;
    if (restoring) {
        if (!application_native_q3_remote_role_module_sequence_read(owner->provider, &actual, &maximum, error) ||
            !role->sequence || role->sequence > maximum || !role->service_owner)
            return application_fail(error, QA_ERROR_FORMAT, "Restored CLIENT module leaves its real registration sequence");
    } else if (!application_native_q3_remote_role_module_sequence_reserve(owner->provider,
        &actual, owner, &role->sequence, error)) return false;
    char name[160];
    int count = snprintf(name, sizeof(name), "q3-native-module:%u:%u:%llu:%llu:%u", actual.receiver.receiver,
        actual.receiver.seat, (unsigned long long)actual.receiver.service_owner,
        (unsigned long long)role->sequence, (unsigned)role->kind);
    if (count < 0 || (size_t)count >= sizeof(name))
        return application_fail(error, QA_ERROR_MEMORY, "Acquired CLIENT service namespace overflow");
    qa_strings *strings = qa_session_strings(owner->app->session);
    if (restoring) return qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, (size_t)count}) == role->service_owner ||
        application_fail(error, QA_ERROR_FORMAT, "Restored CLIENT module service namespace differs");
    return qa_strings_intern_cstr(strings, name, &role->service_owner, error);
}

bool native_client_module_construct(native_client_module *role, bool restoring, qa_error *error)
{
    application_native_q3_client_modules *owner = role->owner;
    qa_application_q3_remote_source actual;
    if (!native_client_module_namespace(role, restoring, error) || !native_client_modules_physical(owner, &actual, error)) return false;
    qa_q3_host_options options = {.role = role->kind, .abi = role->abi, .session = owner->app->session,
        .owner = actual.receiver.receiver, .service_owner = role->service_owner, .cvars = actual.receiver.cvars,
        .console = actual.receiver.console, .command_context = actual.receiver.command_context,
        .mounts = actual.descriptor->content, .client_time_cvars = actual.receiver.client_time_cvars,
        .client_time_owner = actual.receiver.client_time_owner, .input_owner = actual.receiver.service_owner,
        .script_globals = owner->script_globals, .script_globals_owner = actual.receiver.service_owner};
    if (role->image && role->kind == QA_QVM_CGAME) {
        options.source_entity = source_entity; options.source_entity_context = role;
    }
    qa_application_q3_equipment_services equipment = {0};
    qa_application_q3_body_services body = {0};
    qa_application_native_q3_module_preparation preparation = {.source = &actual, .role = role->kind,
        .abi = role->abi, .service_owner = role->service_owner, .path = role->artifact.path,
        .artifact = role->artifact.resource, .acquisition = &role->artifact.acquisition,
        .services = &options, .equipment_services = &equipment, .body_services = &body, .restoring = restoring};
    if (!application_native_q3_remote_role_modules_borrow(owner->provider, &actual, owner, error)) return false;
    ++owner->calls;
    bool okay = owner->options.prepare(owner->options.context, &preparation, error);
    --owner->calls;
    qa_error returned = {0};
    if (!application_native_q3_remote_role_modules_return(owner->provider, actual.receiver.seat, owner, &returned)) {
        if (okay && error) *error = returned;
        okay = false;
    }
    if (okay && (options.role != role->kind || options.abi != role->abi || options.owner != actual.receiver.receiver ||
        options.session != owner->app->session || options.service_owner != role->service_owner ||
        options.cvars != actual.receiver.cvars || options.console != actual.receiver.console ||
        options.mounts != actual.descriptor->content || options.command_context.owner != actual.receiver.receiver ||
        options.command_context.seat != actual.receiver.seat || options.command_context.dialect != QA_CONSOLE_Q3 ||
        options.command_context.origin != actual.receiver.command_context.origin ||
        options.command_context.client != actual.receiver.command_context.client ||
        !qa_actor_id_equal(options.command_context.actor, actual.receiver.command_context.actor) ||
        options.input_owner != actual.receiver.service_owner ||
        options.script_globals != owner->script_globals || !options.script_globals ||
        options.script_globals_owner != actual.receiver.service_owner ||
        options.client_time_cvars != actual.receiver.client_time_cvars ||
        options.client_time_owner != actual.receiver.client_time_owner ||
        options.engine_cvars || options.client_time_from_game || !options.client.gamestate ||
        !options.client.current_snapshot || !options.client.server_command ||
        (role->image && role->kind == QA_QVM_CGAME &&
            (options.source_entity != source_entity || options.source_entity_context != role))))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT preparation changed its actual physical ownership");
    if (okay) okay = native_client_modules_physical(owner, &actual, error);
    if (okay) okay = qa_q3_host_create(&options, &role->host, error);
    if (!okay) {
        if (options.release_frontend) options.release_frontend(options.frontend_lifetime);
        return false;
    }
    role->client = options.client;
    role->body_services = body;
    if (role->image) {
        qa_qvm_options vm = qa_q3_host_qvm_options(role->host, QA_QVM_COMPILED_SEMANTICS);
        if (!qa_qvm_create(role->image, &vm, &role->vm, error) ||
            !qa_q3_host_attach_qvm(role->host, role->vm, error)) return false;
        if (role->kind == QA_QVM_CGAME) {
            application_q3_equipment_module module = {.session = owner->app->session, .vm = role->vm,
                .image = role->image, .profile = &role->profile, .receiver = actual.receiver.receiver,
                .seat = actual.receiver.seat, .client = options.client};
            if (!application_q3_equipment_create_module(&module, &equipment, &role->equipment, error)) return false;
            if (body.prepare) {
                application_q3_body_module body_module = {.session = owner->app->session,
                    .vm = role->vm, .image = role->image, .profile = &role->body_profile,
                    .receiver = actual.receiver.receiver, .seat = actual.receiver.seat, .client = options.client};
                if (!application_q3_body_create_module(&body_module, &body, NULL, &role->body, error)) return false;
            }
        }
    } else {
        if (restoring) return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Native CLIENT executor requires its actual mutable continuation owner");
        qa_native_host_q3_options native = {.role = role->kind, .abi = role->abi, .cvars = options.cvars,
            .console = options.console, .command_context = options.command_context,
            .maximum_string_bytes = options.maximum_string_bytes, .bridge = qa_q3_host_native_bridge(role->host)};
        if (!application_q3_guest_native_options(owner->app, owner->provider, role->kind,
            actual.receiver.seat, &native.instance, error)) return false;
        if (role->native_declaration) {
            native.instance.declaration = role->native_declaration;
            native.instance.declaration_digest = qa_native_declaration_digest(role->native_declaration);
        }
        qa_native_module_info module = qa_native_module_describe(role->module);
        qa_native_process_resource_artifact artifact = {.resource = role->artifact.resource,
            .acquisition = &role->artifact.acquisition, .path = role->artifact.path};
        if (!application_native_process_prepare(owner->app, actual.descriptor,
            actual.receiver.receiver, role->service_owner, &artifact, 1, 0, &module.image,
            native.instance.observe || qa_native_declaration_region_count(role->native_declaration) != 0,
            process_current, role, &role->process, error)) {
            role->native_load_failed = true;
            return false;
        }
        native.instance.process = &role->process.process;
        if (!native_client_modules_physical(owner, &actual, error) ||
            !application_native_q3_remote_role_modules_borrow(owner->provider, &actual, owner, error)) return false;
        ++owner->calls;
        native_client_module *previous = owner->entered;
        owner->entered = role;
        okay = qa_native_host_create_q3(role->module, &native, &role->native, error);
        role->native_load_failed = !okay;
        if (okay) okay = qa_q3_host_attach_native(role->host, role->native, error);
        owner->entered = previous;
        --owner->calls;
        if (!application_native_q3_remote_role_modules_return(owner->provider, actual.receiver.seat, owner, &returned)) {
            if (okay && error) *error = returned;
            okay = false;
        }
        if (!okay) return false;
    }
    role->ready = true; return true;
}

bool qa_application_native_q3_client_modules_create(qa_application *app,
    const qa_application_native_q3_client_modules_options *options,
    application_native_q3_client_modules **out, qa_error *error)
{
    if (!native_client_modules_allocate(app, options, false, out, error)) return false;
    application_native_q3_client_modules *owner = *out;
    qa_application_native_q3_client_modules_recipe recipe;
    if (!qa_application_native_q3_client_modules_recipe_read(app, &owner->source, options->gamestate, &recipe, error)) return false;
    owner->pure = recipe.pure;
    bool native_ui = recipe.ui_runtime == QA_PROGRAM_NATIVE;
    if (!artifact_open(&owner->ui, native_ui, error)) return false;
    if (!native_client_module_construct(&owner->ui, false, error)) {
        /* SDK VM_Create falls back only after the real native loader failed.
         * A retained mapping or failed cleanup keeps the owner for retirement. */
        if (!owner->ui.native_load_failed || owner->ui.native ||
            (error && error->code == QA_ERROR_MEMORY) || !consume(&owner->ui, error)) return false;
        owner->ui = (native_client_module){.owner = owner, .kind = QA_QVM_UI};
        if (error) *error = (qa_error){0};
        if (!artifact_open(&owner->ui, false, error) ||
            !native_client_module_construct(&owner->ui, false, error)) return false;
    }
    if (owner->pure && (!artifact_open(&owner->cgame, false, error) ||
        !native_client_module_construct(&owner->cgame, false, error))) return false;
    qa_application_q3_remote_source actual;
    bool okay = native_client_modules_physical(owner, &actual, error) &&
        options->current(options->context, &actual, options->gamestate, error);
    if (okay) owner->prepared = true;
    return okay;
}

static native_client_module *module_role(application_native_q3_client_modules *owner, qa_qvm_role kind)
{
    return kind == QA_QVM_UI ? &owner->ui : kind == QA_QVM_CGAME ? &owner->cgame : NULL;
}

static bool invoke(native_client_module *role, int32_t command, const int32_t *arguments,
    size_t count, int32_t *result, bool initializing, qa_error *error)
{
    application_native_q3_client_modules *owner = role->owner;
    qa_application_q3_remote_source actual;
    if (role->kind == QA_QVM_CGAME && role->abi == QA_QVM_Q3_116N && command > 5)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Legacy CGAME has no requested export");
    bool drawing = role->kind == QA_QVM_CGAME && command == 3;
    if (!role->ready || !result || count > 9 || (count && !arguments) || owner->retiring || owner->restore_pending ||
        (drawing && (count != 3 || role->draw_entry || role->draw_revision == UINT64_MAX)) ||
        owner->app->operation == APPLICATION_PERSISTING || owner->app->destroy_requested ||
        (!initializing && !role->init_succeeded) || !native_client_modules_physical(owner, &actual, error) ||
        !qa_application_native_q3_client_modules_current(owner, &actual))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT entry requires its admitted current executor");
    if (!application_native_q3_remote_role_modules_borrow(owner->provider, &actual, owner, error)) return false;
    ++owner->calls;
    native_client_module *previous = owner->entered;
    owner->entered = role;
    if (drawing) {
        memcpy(role->draw_arguments, arguments, sizeof(role->draw_arguments));
        ++role->draw_revision; role->draw_entry = true;
    }
    bool okay = !drawing || ((!role->equipment || application_q3_equipment_draw_begin(role->equipment, error)) &&
        (!role->body || application_q3_body_draw_begin(role->body, error)));
    if (okay) {
        if (role->native) {
            okay = qa_native_host_q3_vm_call(role->native, command, arguments, count, result, error);
            if (initializing && command == (role->kind == QA_QVM_UI ? 1 : 0) &&
                qa_native_get_lifecycle(qa_native_host_instance(role->native)) == QA_NATIVE_INITIALIZED)
                role->initialized = true;
        } else {
            int32_t words[10] = {command};
            if (count) memcpy(words + 1, arguments, count * sizeof(*arguments));
            bool init = initializing && command == (role->kind == QA_QVM_UI ? 1 : 0);
            okay = init ? qa_qvm_invoke_started(role->vm, 0, words, count + 1,
                result, &role->initialized, error) : qa_qvm_invoke(role->vm, 0, words, count + 1, result, error);
        }
    }
    if (drawing) {
        if (role->body) application_q3_body_draw_end(role->body);
        if (role->equipment) application_q3_equipment_draw_end(role->equipment);
        role->draw_entry = false;
        memset(role->draw_arguments, 0, sizeof(role->draw_arguments));
    }
    owner->entered = previous;
    --owner->calls;
    qa_error returned = {0};
    if (!application_native_q3_remote_role_modules_return(owner->provider, actual.receiver.seat, owner, &returned)) {
        if (okay && error) *error = returned;
        okay = false;
    }
    return okay;
}

static bool command_snapshot(const qa_command_invocation *source, qa_command_tokens *out,
    qa_error *error)
{
    size_t bytes = 0;
    for (size_t i = 0; i < source->argc; ++i) {
        if (!source->argv[i]) return application_fail(error, QA_ERROR_ARGUMENT, "Acquired console argument is absent");
        size_t length = strlen(source->argv[i]);
        if (length == SIZE_MAX || bytes > SIZE_MAX - length - 1)
            return application_fail(error, QA_ERROR_MEMORY, "Acquired console argv exceeds capacity");
        bytes += length + 1;
    }
    size_t tail = strlen(source->args_text ? source->args_text : "");
    if (tail == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Acquired console arguments exceed capacity");
    out->count = source->argc;
    out->values = calloc(source->argc ? source->argc : 1, sizeof(*out->values));
    out->storage = malloc(bytes ? bytes : 1); out->args_text = malloc(tail + 1);
    if (!out->values || !out->storage || !out->args_text) {
        qa_command_tokens_free(out);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining acquired console entry arguments");
    }
    size_t cursor = 0;
    for (size_t i = 0; i < source->argc; ++i) {
        size_t length = strlen(source->argv[i]) + 1;
        out->values[i] = out->storage + cursor;
        memcpy(out->storage + cursor, source->argv[i], length); cursor += length;
    }
    memcpy(out->args_text, source->args_text ? source->args_text : "", tail + 1);
    return true;
}

bool qa_application_native_q3_client_modules_call(application_native_q3_client_modules *owner,
    qa_qvm_role kind, int32_t command, const int32_t *arguments, size_t count, int32_t *result, qa_error *error)
{
    native_client_module *role = owner ? module_role(owner, kind) : NULL;
    if (!role || owner->video || owner->calls || command == (kind == QA_QVM_UI ? 1 : 0) || command == (kind == QA_QVM_UI ? 2 : 1) ||
        command == (kind == QA_QVM_UI ? NATIVE_Q3_UI_CONSOLE_COMMAND : NATIVE_Q3_CG_CONSOLE_COMMAND))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT entry requires its lifecycle or console adapter");
    return invoke(role, command, arguments, count, result, false, error);
}

static bool console_current(application_native_q3_client_modules *owner,
    const qa_command_invocation *call, qa_error *error)
{
    qa_application_q3_remote_source source;
    qa_command_context expected;
    if (!call || !call->argc || !call->argv || !call->raw || !call->args_text ||
        !native_client_modules_physical(owner, &source, error) || call->console != source.receiver.console ||
        !application_command_capture(owner->app, &source.receiver.command_context, &expected, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired console entry lost its physical invocation");
    const qa_command_context *actual = &call->context;
    return (actual->session == expected.session && actual->owner == expected.owner && actual->seat == expected.seat &&
        actual->client == expected.client && actual->dialect == expected.dialect && actual->origin == expected.origin &&
        actual->registry == expected.registry && actual->generation == expected.generation &&
        qa_actor_id_equal(actual->actor, expected.actor) && application_command_active(owner->app, actual)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Acquired console entry changed its captured CLIENT origin");
}

bool qa_application_native_q3_client_modules_console_command(application_native_q3_client_modules *owner,
    qa_qvm_role kind, const qa_command_invocation *call, int32_t milliseconds, int32_t *result, qa_error *error)
{
    native_client_module *role = owner ? module_role(owner, kind) : NULL;
    const qa_command_tokens *reached = NULL;
    qa_command_tokens snapshot = {0}; uint64_t revision = 0;
    if (!role || owner->video || owner->calls || owner->command_arguments)
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired console entry retains another source call");
    if (!console_current(owner, call, error) ||
        !application_native_q3_remote_role_arguments(owner->provider, owner->source.receiver.seat,
            &reached, &revision, error) || !command_snapshot(call, &snapshot, error)) return false;
    owner->command_role = role; owner->command_arguments = &snapshot; owner->command_revision = revision;
    bool okay = invoke(role, kind == QA_QVM_UI ? NATIVE_Q3_UI_CONSOLE_COMMAND : NATIVE_Q3_CG_CONSOLE_COMMAND,
        kind == QA_QVM_UI ? &milliseconds : NULL, kind == QA_QVM_UI ? 1 : 0, result, false, error);
    owner->command_role = NULL; owner->command_arguments = NULL; owner->command_revision = 0;
    qa_command_tokens_free(&snapshot);
    if (okay) okay = console_current(owner, call, error);
    return okay;
}

static bool init_current(application_native_q3_client_modules *owner,
    const qa_application_q3_remote_init *request, qa_error *error)
{
    qa_application_q3_remote_source actual;
    return native_client_modules_physical(owner, &actual, error) && same_source(&actual, &request->source) &&
        qa_application_native_q3_client_modules_current(owner, &actual) &&
        owner->options.current(owner->options.context, &actual, owner->options.gamestate, error) &&
        request->current(request->connection, actual.connection_epoch, request->server_message,
            request->last_executed_server_command, request->client_number, error);
}

static bool initialize_ui(application_native_q3_client_modules *owner, bool connecting, qa_error *error)
{
    if (owner->app->operation == APPLICATION_PERSISTING || owner->app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source UI initialization cannot enter a capture candidate");
    int32_t result = 0;
    if (owner->ui.vm) {
        qa_application_q3_remote_source actual;
        if (!native_client_modules_physical(owner, &actual, error) ||
            !qa_application_native_q3_client_modules_current(owner, &actual) ||
            !application_native_q3_remote_role_modules_borrow(owner->provider, &actual, owner, error)) return false;
        ++owner->calls;
        native_client_module *previous = owner->entered;
        owner->entered = &owner->ui;
        bool okay = qa_qvm_validate_ui(owner->ui.vm, &result, error);
        owner->entered = previous;
        --owner->calls;
        qa_error returned = {0};
        if (!application_native_q3_remote_role_modules_return(owner->provider, actual.receiver.seat, owner, &returned)) {
            if (okay && error) *error = returned;
            okay = false;
        }
        if (!okay) return false;
    } else if (!invoke(&owner->ui, 0, NULL, 0, &result, true, error)) return false;
    if (result != 4 && !(owner->ui.abi == QA_QVM_Q3_MODERN && result == 6))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Acquired source UI API version differs");
    int32_t argument = connecting ? 1 : 0;
    return invoke(&owner->ui, 1, &argument, 1, &result, true, error);
}

bool qa_application_native_q3_client_modules_initialize_ui(application_native_q3_client_modules *owner,
    bool connecting, qa_error *error)
{
    qa_application_q3_remote_source actual;
    if (!owner || (owner->video && !owner->video_entering) ||
        owner->options.gamestate || owner->pure || owner->calls || owner->initializing ||
        owner->ui.initialized || owner->retiring || owner->restore_pending || !owner->ui.ready ||
        !native_client_modules_physical(owner, &actual, error) ||
        !owner->options.current(owner->options.context, &actual, NULL, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Initial source UI requires its actual connecting owner");
    bool okay = initialize_ui(owner, connecting, error);
    if (okay) okay = native_client_modules_physical(owner, &actual, error) &&
        owner->options.current(owner->options.context, &actual, NULL, error);
    if (okay) owner->ui.init_succeeded = true;
    return okay;
}

bool qa_application_native_q3_client_modules_initialize(application_native_q3_client_modules *owner,
    const qa_application_q3_remote_init *request, qa_error *error)
{
    if (!owner || (owner->video && !owner->video_entering) ||
        !request || !request->current || owner->calls || owner->initializing || owner->retiring ||
        owner->restore_pending || request->client_number < 0 || request->client_number >= 64 ||
        owner->ui.initialized || owner->cgame.initialized || !owner->ui.ready ||
        !init_current(owner, request, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT Init requires its actual decoded SDK tuple");
    int32_t result = 0;
    bool okay = initialize_ui(owner, true, error);
    if (okay) okay = init_current(owner, request, error);
    if (okay) owner->ui.init_succeeded = true;
    if (okay && owner->cgame.ready) {
        int32_t arguments[] = {request->server_message, request->last_executed_server_command, request->client_number};
        owner->initializing = &owner->cgame;
        okay = invoke(&owner->cgame, 0, arguments, 3, &result, true, error);
        owner->initializing = NULL;
    }
    if (okay) okay = init_current(owner, request, error);
    if (okay && owner->cgame.ready) owner->cgame.init_succeeded = true;
    if (okay && owner->cgame.ready) {
        qa_application_q3_remote_source actual;
        okay = native_client_modules_physical(owner, &actual, error) &&
            application_native_q3_remote_role_modules_initialized(owner->provider, &actual, owner, error);
    }
    if (!okay) { owner->ui.init_succeeded = false; owner->cgame.init_succeeded = false; }
    return okay;
}

bool qa_application_native_q3_client_modules_loading_screen(application_native_q3_client_modules *owner,
    bool *drawn, qa_error *error)
{
    if (!owner || !drawn) return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT loading screen requires its owner");
    *drawn = false;
    if (owner->initializing != &owner->cgame) return true;
    if (!owner->calls || !owner->ui.init_succeeded || !owner->ui.initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CGAME loading lost its initialized source UI");
    int32_t overlay = 1, result;
    if (!invoke(&owner->ui, NATIVE_Q3_UI_DRAW_CONNECT_SCREEN, &overlay, 1, &result, false, error)) return false;
    *drawn = true; return true;
}

static bool shutdown(native_client_module *role, qa_error *error)
{
    if (!role->initialized) return true;
    if (role->owner->restore_pending) {
        /* An isolated import has not activated this continuation. Discarding
         * its candidate cannot execute restored source teardown. */
        role->initialized = false; role->init_succeeded = false; return true;
    }
    role->initialized = false; role->init_succeeded = false;
    bool started = false, okay;
    native_client_module *previous = role->owner->entered;
    role->owner->entered = role;
    ++role->owner->calls;
    if (role->native) {
        okay = qa_native_host_shutdown(role->native, false, error);
        started = qa_native_get_lifecycle(qa_native_host_instance(role->native)) == QA_NATIVE_SHUT_DOWN;
    } else {
        int32_t word = role->kind == QA_QVM_UI ? 2 : 1, result;
        okay = qa_qvm_invoke_started(role->vm, 0, &word, 1, &result, &started, error);
    }
    --role->owner->calls;
    role->owner->entered = previous;
    role->initialized = !started; return okay;
}

bool native_client_module_close_executor(native_client_module *role, qa_error *error)
{
    if (!shutdown(role, error)) return false;
    if (role->body) {
        if (!application_q3_body_destroy(role->body, error)) return false;
        role->body = NULL;
    }
    if (role->equipment) {
        if (!application_q3_equipment_destroy(role->equipment, error)) return false;
        role->equipment = NULL;
    }
    if (role->native) {
        native_client_module *previous = role->owner->entered;
        role->owner->entered = role;
        ++role->owner->calls;
        bool okay = qa_native_host_destroy_owned(&role->native, error);
        --role->owner->calls;
        role->owner->entered = previous;
        if (!role->native) qa_q3_host_native_consumed(role->host);
        if (!okay) return false;
    }
    if (!application_native_process_release(&role->process, error)) return false;
    if (role->vm) {
        if (!qa_qvm_destroy(role->vm, error)) return false;
        role->vm = NULL; qa_q3_host_qvm_consumed(role->host);
    }
    if (role->host) {
        if (!qa_q3_host_destroy(role->host, error)) return false;
        role->host = NULL;
    }
    role->client = (qa_q3_host_client_services){0};
    role->body_services = (qa_application_q3_body_services){0};
    role->initialized = false;
    role->init_succeeded = false;
    role->draw_revision = 0;
    memset(role->draw_arguments, 0, sizeof(role->draw_arguments));
    role->ready = false;
    role->native_load_failed = false;
    return true;
}

static bool consume(native_client_module *role, qa_error *error)
{
    if (!native_client_module_close_executor(role, error)) return false;
    qa_qvm_image_release(role->image); role->image = NULL;
    qa_native_module_release(role->module); role->module = NULL;
    qa_native_declaration_destroy(role->native_declaration); role->native_declaration = NULL;
    application_q3_equipment_profile_free(&role->profile);
    application_q3_body_profile_free(&role->body_profile);
    role->body_services = (qa_application_q3_body_services){0};
    native_client_opening_dispose(&role->artifact);
    native_client_opening_dispose(&role->declaration);
    native_client_opening_dispose(&role->body_declaration);
    qa_buffer_free(&role->saved_services);
    role->ready = false; return true;
}

bool qa_application_native_q3_client_modules_destroy(application_native_q3_client_modules **pointer,
    qa_error *error)
{
    if (!pointer || !*pointer) return true;
    application_native_q3_client_modules *owner = *pointer;
    if (!qa_application_native_q3_client_modules_idle(owner) ||
        (owner->attached && !application_native_q3_remote_roles_idle(owner->provider)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT teardown requires idle actual executors");
    owner->retiring = true;
    if (!consume(&owner->cgame, error) || !consume(&owner->ui, error)) return false;
    if (owner->attached) {
        if (!application_native_q3_remote_role_modules_detach(owner->provider,
            owner->source.receiver.seat, owner, error)) return false;
        owner->attached = false;
    }
    qa_launch_instance_lease_release(owner->metadata);
    qa_script_defines_release(owner->script_globals);
    qa_buffer_free(&owner->saved);
    free(owner); *pointer = NULL; return true;
}

bool qa_application_native_q3_client_modules_artifact_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, qa_application_q3_role_artifact *out, qa_error *error)
{
    native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_application_q3_remote_source actual;
    if (!role || !out || !role->ready || owner->retiring || owner->restore_pending ||
        !native_client_modules_physical(owner, &actual, error) ||
        !qa_application_native_q3_client_modules_current(owner, &actual) ||
        !qa_vfs_acquisition_retained(actual.descriptor->content, &role->artifact.acquisition, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT artifact requires its actual retained opening");
    *out = (qa_application_q3_role_artifact){.source = {.role = kind, .receiver = actual.receiver.receiver,
        .seat = actual.receiver.seat, .service_owner = role->service_owner,
        .configuration_generation = actual.configuration_generation, .connection_epoch = actual.connection_epoch,
        .descriptor = owner->source.descriptor, .artifact = role->artifact.resource,
        .acquisition = &role->artifact.acquisition, .artifact_view = actual.descriptor->content}, .path = role->artifact.path};
    return true;
}

bool qa_application_native_q3_client_modules_receipt_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, qa_application_q3_role_receipt *out, qa_error *error)
{
    native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_application_q3_role_artifact artifact;
    if (!role || !out || !role->initialized || !role->init_succeeded ||
        !qa_application_native_q3_client_modules_artifact_read(owner, kind, &artifact, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT receipt requires successful current source Init");
    *out = artifact.source; return true;
}

bool qa_application_native_q3_client_modules_optional_receipt_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, qa_application_q3_role_receipt *out, bool *present, qa_error *error)
{
    const native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_application_q3_remote_source source;
    qa_application_q3_role_artifact artifact;
    qa_q3_host *host = NULL;
    qa_q3_host_client_context context;
    if (!role || !out || !present || !owner->prepared || !role->ready ||
        !native_client_modules_physical(owner, &source, error) ||
        !qa_application_native_q3_client_modules_current(owner, &source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Optional CLIENT receipt requires its completed current role");
    if (!qa_application_native_q3_client_modules_host_read(owner, kind, &host, &context, error) ||
        !qa_application_native_q3_client_modules_artifact_read(owner, kind, &artifact, error)) return false;
    if (!role->initialized && !role->init_succeeded) {
        *out = (qa_application_q3_role_receipt){0}; *present = false; return true;
    }
    if (!role->initialized || !role->init_succeeded)
        return application_fail(error, QA_ERROR_ARGUMENT, "Optional CLIENT receipt retains failed source Init");
    if (!qa_application_native_q3_client_modules_receipt_read(owner, kind, out, error)) return false;
    *present = true; return true;
}

bool qa_application_native_q3_client_modules_host_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, qa_q3_host **host, qa_q3_host_client_context *context, qa_error *error)
{
    native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_application_q3_remote_source actual;
    qa_q3_host_client_context retained;
    if (!role || !host || !context || !role->ready || owner->retiring ||
        !native_client_modules_physical(owner, &actual, error) ||
        (!owner->restore_pending && !qa_application_native_q3_client_modules_current(owner, &actual)) ||
        !qa_q3_host_client_context_read(role->host, &retained) || retained.session != owner->app->session ||
        retained.owner != actual.receiver.receiver || retained.role != kind || retained.service_owner != role->service_owner ||
        retained.console != actual.receiver.console || retained.cvars != actual.receiver.cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT host identity lost its real physical parent");
    *host = role->host; *context = retained; return true;
}

bool qa_application_native_q3_client_modules_retained_source_read(const application_native_q3_client_modules *owner,
    qa_application_q3_remote_source *out, qa_error *error)
{
    if (!owner || !out || !owner->attached ||
        !application_native_q3_remote_role_modules_retained(owner->provider, &owner->source, owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Retained CLIENT module lost its attached physical owner");
    *out = owner->source; return true;
}

bool qa_application_native_q3_client_modules_initialization_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, bool *initialized, bool *succeeded, qa_error *error)
{
    const native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_q3_host *host = NULL; qa_q3_host_client_context context;
    if (!initialized || !succeeded || !role ||
        !qa_application_native_q3_client_modules_host_read(owner, kind, &host, &context, error)) return false;
    *initialized = role->initialized; *succeeded = role->init_succeeded;
    return true;
}

bool qa_application_native_q3_client_modules_optional_host_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, qa_q3_host **host, qa_q3_host_client_context *context, bool *present, qa_error *error)
{
    const native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_application_q3_remote_source source;
    if (!role || !host || !context || !present || !owner->prepared ||
        !native_client_modules_physical(owner, &source, error) ||
        !qa_application_native_q3_client_modules_current(owner, &source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Optional CLIENT host inventory requires its completed current owner");
    if (!role->ready) {
        if (kind != QA_QVM_CGAME || role->host || role->vm || role->native || role->image || role->module ||
            role->artifact.path || role->artifact.resource || role->declaration.path || role->declaration.resource ||
            role->body_declaration.path || role->body_declaration.resource || role->body_profile.artifact_path ||
            role->native_declaration || role->equipment || role->body || role->body_services.prepare ||
            role->draw_entry || role->sequence || role->service_owner ||
            role->initialized || role->init_succeeded || role->saved_services.size)
            return application_fail(error, QA_ERROR_ARGUMENT, "Optional CLIENT host inventory retains an incomplete role");
        *host = NULL; *context = (qa_q3_host_client_context){0}; *present = false; return true;
    }
    if (!qa_application_native_q3_client_modules_host_read(owner, kind, host, context, error)) return false;
    *present = true; return true;
}

bool qa_application_native_q3_client_modules_host_entered(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, const qa_q3_host *host, uint64_t service_owner)
{
    const native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_application_q3_remote_source actual;
    qa_q3_host_client_context retained;
    if (!role || !owner->attached || !owner->calls || owner->entered != role || !host || role->host != host ||
        service_owner != role->service_owner || owner->restore_pending) return false;
    if (owner->retiring) {
        if (!application_native_q3_remote_role_modules_retained(owner->provider, &owner->source, owner)) return false;
        actual = owner->source;
    } else if (!native_client_modules_physical(owner, &actual, NULL) ||
        !qa_application_native_q3_client_modules_current(owner, &actual)) return false;
    return qa_q3_host_client_context_read(role->host, &retained) &&
        retained.session == owner->app->session && retained.owner == actual.receiver.receiver &&
        retained.role == kind && retained.service_owner == service_owner &&
        retained.console == actual.receiver.console && retained.cvars == actual.receiver.cvars;
}

bool qa_application_native_q3_client_modules_entered_host_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, uint64_t service_owner, qa_q3_host **host,
    qa_q3_host_client_context *context, qa_error *error)
{
    const native_client_module *role = owner ? module_role((application_native_q3_client_modules *)owner, kind) : NULL;
    qa_q3_host_client_context actual;
    if (!role || !host || !context ||
        !qa_application_native_q3_client_modules_host_entered(owner, kind, role->host, service_owner) ||
        !qa_q3_host_client_context_read(role->host, &actual))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT callback lacks its actual entered host namespace");
    *host = role->host; *context = actual; return true;
}

bool qa_application_native_q3_client_modules_draw_entry_read(const application_native_q3_client_modules *owner,
    qa_application_native_q3_client_draw_entry *out, qa_error *error)
{
    const native_client_module *role = owner ? &owner->cgame : NULL;
    qa_application_native_q3_client_draw_entry value = {0};
    if (!role || !out || !role->draw_entry || !role->draw_revision || !role->ready ||
        owner->retiring || owner->restore_pending || owner->entered != role || !owner->calls ||
        !native_client_modules_physical(owner, &value.source, error) ||
        !qa_application_native_q3_client_modules_entered_host_read(owner, QA_QVM_CGAME,
            role->service_owner, &value.host, &value.context, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CGAME body requires its actual entered Draw tuple");
    value.revision = role->draw_revision;
    value.server_time = role->draw_arguments[0];
    value.stereo_view = role->draw_arguments[1];
    value.demo_playback = role->draw_arguments[2];
    *out = value;
    return true;
}

bool qa_application_native_q3_client_modules_draw_entry_current(const application_native_q3_client_modules *owner,
    const qa_application_native_q3_client_draw_entry *held)
{
    qa_application_native_q3_client_draw_entry actual;
    return held && qa_application_native_q3_client_modules_draw_entry_read(owner, &actual, NULL) &&
        actual.revision == held->revision && actual.host == held->host &&
        actual.context.frontend_lifetime == held->context.frontend_lifetime &&
        actual.context.session == held->context.session && actual.context.owner == held->context.owner &&
        actual.context.service_owner == held->context.service_owner &&
        actual.context.console == held->context.console && actual.context.cvars == held->context.cvars &&
        same_source(&actual.source, &held->source) &&
        qa_application_q3_remote_source_current(owner->app, &held->source) &&
        actual.server_time == held->server_time && actual.stereo_view == held->stereo_view &&
        actual.demo_playback == held->demo_playback;
}

bool qa_application_native_q3_client_modules_entered_arguments_read(const application_native_q3_client_modules *owner,
    qa_qvm_role kind, uint64_t service_owner, qa_native_host_command_view *out,
    uint64_t *revision, qa_error *error)
{
    qa_q3_host *host = NULL;
    qa_q3_host_client_context context;
    const qa_command_tokens *arguments = NULL;
    uint64_t actual_revision;
    if (!out || !revision || !qa_application_native_q3_client_modules_entered_host_read(owner, kind,
            service_owner, &host, &context, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT arguments lost their actual entered parser owner");
    bool lexical = owner->command_arguments && owner->command_role == owner->entered;
    if (lexical) { arguments = owner->command_arguments; actual_revision = owner->command_revision; }
    else if (!application_native_q3_remote_role_arguments(owner->provider, owner->source.receiver.seat,
        &arguments, &actual_revision, error)) return false;
    if (!arguments || !qa_application_native_q3_client_modules_host_entered(owner, kind, host, service_owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT arguments changed their entered owner");
    *out = (qa_native_host_command_view){.count = arguments->count,
        .arguments = (const char *const *)arguments->values, .tail = arguments->args_text ? arguments->args_text : "",
        .canonical_configstrings = kind == QA_QVM_CGAME && !lexical};
    *revision = actual_revision; return true;
}

bool qa_application_native_q3_client_modules_receipt_current(const application_native_q3_client_modules *owner,
    const qa_application_q3_role_receipt *receipt)
{
    qa_application_q3_role_receipt actual;
    return receipt && qa_application_native_q3_client_modules_receipt_read(owner, receipt->role, &actual, NULL) &&
        receipt->receiver == actual.receiver && receipt->seat == actual.seat &&
        receipt->service_owner == actual.service_owner && receipt->configuration_generation == actual.configuration_generation &&
        receipt->connection_epoch == actual.connection_epoch && receipt->descriptor &&
        receipt->descriptor->storage == actual.descriptor->storage && receipt->artifact == actual.artifact &&
        receipt->acquisition == actual.acquisition && receipt->artifact_view == actual.artifact_view;
}

bool qa_application_native_q3_client_modules_equipment_requests(const application_native_q3_client_modules *owner,
    bool *hud, bool *view, qa_error *error)
{
    qa_application_q3_remote_source actual;
    if (!owner || !hud || !view || !owner->cgame.init_succeeded || owner->retiring || owner->restore_pending ||
        !native_client_modules_physical(owner, &actual, error) ||
        !qa_application_native_q3_client_modules_current(owner, &actual))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CGAME equipment requires its current initialized owner");
    *hud = application_q3_equipment_hud(owner->cgame.equipment);
    *view = application_q3_equipment_view(owner->cgame.equipment); return true;
}

bool qa_application_native_q3_client_modules_content_visit(const application_native_q3_client_modules *owner,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!owner || !visitor || !visitor->catalog || !visitor->view || owner->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT content inventory requires its idle retained owner");
    const qa_launch_instance *source = qa_launch_instance_lease_view(owner->metadata);
    return source && visitor->catalog(visitor->context, qa_launch_instance_catalog(source), error) &&
        visitor->view(visitor->context, source->content, error);
}
