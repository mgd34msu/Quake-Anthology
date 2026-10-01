#include "native_q3_remote_role_private.h"
#include "native_q3_remote_role_save.h"
#include "startup_flow.h"
#include "qa/cvars_save.h"
#include "qa/launch_save.h"
#include "qa/source_save.h"
#include "qa/binary.h"
#include "qa/application_native_q3_client_modules.h"
#include "qa/application_native_q3_remote_client.h"
#include "qa/application_native_q3_remote_modules_save.h"
#include <stdlib.h>
#include <string.h>

typedef struct saved_descriptor {
    uint64_t catalog, view;
    qa_product_id product;
    qa_sha256_digest identity;
    const qa_launch_instance *source;
    qa_launch_instance_lease *restored;
} saved_descriptor;
typedef struct saved_role {
    uint32_t seat, lifecycle;
    qa_string_id service_owner;
    uint64_t argument_revision, module_sequence, epoch, generation;
    size_t descriptor;
    qa_command_tokens arguments;
    char *system_info;
    char *registry_instance;
    uint32_t registry_seat;
    bool shared_registry;
    qa_buffer cvars;
    qa_buffer modules;
    qa_cvars_restore *ticket;
    struct application_native_q3_remote_role *actual;
} saved_role;
typedef struct saved_roles {
    uint32_t product;
    saved_descriptor *descriptors;
    size_t descriptor_count;
    saved_role *roles;
    size_t count;
    bool reading;
} saved_roles;

static void dispose(saved_roles *saved)
{
    for (size_t i = 0; saved->roles && i < saved->count; ++i) {
        saved_role *row = saved->roles + i;
        qa_cvars_save_abort(row->ticket);
        qa_buffer_free(&row->cvars);
        qa_buffer_free(&row->modules);
        if (saved->reading) {
            qa_command_tokens_free(&row->arguments);
            free(row->system_info);
            free(row->registry_instance);
        }
    }
    for (size_t i = 0; saved->descriptors && i < saved->descriptor_count; ++i)
        qa_launch_instance_lease_release(saved->descriptors[i].restored);
    free(saved->roles); free(saved->descriptors);
}
static bool text_fields(qa_source_save_io *io, char **text, size_t maximum)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    if (!qa_source_save_count(io, &length, maximum) || length == SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset) return false;
        *text = malloc(length + 1);
        if (!*text) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native CLIENT text");
    }
    if (!qa_source_save_bytes(io, *text, length) || memchr(*text, 0, length)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) (*text)[length] = 0;
    return true;
}
static bool argument_fields(qa_source_save_io *io, qa_command_tokens *arguments)
{
    if (!qa_source_save_count(io, &arguments->count, 1024)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        arguments->values = arguments->count ? calloc(arguments->count, sizeof(*arguments->values)) : NULL;
        arguments->storage = malloc(9216);
        if ((arguments->count && !arguments->values) || !arguments->storage)
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native CLIENT argv");
    }
    size_t offset = 0;
    for (size_t i = 0; i < arguments->count; ++i) {
        if (offset >= 9216) return false;
        char *value = io->direction == QA_SOURCE_SAVE_WRITE ? arguments->values[i] : NULL;
        bool ok = text_fields(io, &value, 9215 - offset) && value;
        if (!ok) { if (io->direction == QA_SOURCE_SAVE_READ) free(value); return false; }
        size_t length = strlen(value) + 1;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            arguments->values[i] = arguments->storage + offset;
            memcpy(arguments->values[i], value, length); free(value);
        }
        offset += length;
    }
    if (!text_fields(io, &arguments->args_text, 9216) || (arguments->count && !arguments->args_text)) return false;
    if (arguments->args_text) {
        size_t offset_args = 0;
        for (size_t i = 1; i < arguments->count; ++i) {
            if (i > 1 && arguments->args_text[offset_args++] != ' ') return false;
            size_t length = strlen(arguments->values[i]);
            if (strncmp(arguments->args_text + offset_args, arguments->values[i], length)) return false;
            offset_args += length;
        }
        if (arguments->args_text[offset_args]) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, saved_roles *saved)
{
    uint8_t magic[4] = {'Q','N','R','S'}; uint32_t version = 5;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QNRS", 4) ||
        !qa_source_save_u32(io, &version) || version != 5 ||
        !qa_source_save_u32(io, &saved->product) || saved->product > QA_Q3_TEAM_ARENA ||
        !qa_source_save_count(io, &saved->descriptor_count, 64) ||
        !qa_source_save_count(io, &saved->count, 64)) return false;
    if (saved->reading) {
        if (saved->descriptor_count > (io->input.size - io->offset) / 52 ||
            saved->count > (io->input.size - io->offset) / 54) return false;
        saved->descriptors = saved->descriptor_count ? calloc(saved->descriptor_count, sizeof(*saved->descriptors)) : NULL;
        saved->roles = saved->count ? calloc(saved->count, sizeof(*saved->roles)) : NULL;
        if ((saved->descriptor_count && !saved->descriptors) || (saved->count && !saved->roles))
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native CLIENT inventory");
    }
    for (size_t i = 0; i < saved->descriptor_count; ++i) {
        saved_descriptor *d = saved->descriptors + i;
        if (!qa_source_save_u64(io, &d->catalog) || !d->catalog ||
            !qa_source_save_u64(io, &d->view) || !d->view ||
            !qa_source_save_u32(io, &d->product) || !d->product ||
            !qa_source_save_bytes(io, d->identity.bytes, sizeof(d->identity.bytes))) return false;
        for (size_t j = 0; j < i; ++j) if (d->view == saved->descriptors[j].view) return false;
    }
    for (size_t i = 0; i < saved->count; ++i) {
        saved_role *row = saved->roles + i;
        if (!qa_source_save_u32(io, &row->seat) || !qa_source_save_string(io, &row->service_owner) || !row->service_owner ||
            !qa_source_save_u32(io, &row->lifecycle) || row->lifecycle > NATIVE_Q3_REMOTE_CLEARED ||
            !qa_source_save_u64(io, &row->argument_revision) ||
            !qa_source_save_u64(io, &row->module_sequence) ||
            !qa_source_save_count(io, &row->descriptor, saved->descriptor_count) ||
            !qa_source_save_u64(io, &row->epoch) || !qa_source_save_u64(io, &row->generation) ||
            (row->lifecycle != NATIVE_Q3_REMOTE_COLD && !row->epoch) ||
            (row->descriptor && (!row->epoch || !row->generation)) || (!row->descriptor && row->generation) ||
            !argument_fields(io, &row->arguments) || (!row->argument_revision &&
                (row->arguments.count || row->arguments.args_text)) ||
            (row->argument_revision && !row->arguments.args_text) ||
            !text_fields(io, &row->system_info, QA_Q3_BIG_INFO_CHARS - 1) ||
            !qa_source_save_bool(io, &row->shared_registry)) return false;
        if (row->shared_registry) {
            if (!text_fields(io, &row->registry_instance, SIZE_MAX - 1) ||
                !row->registry_instance || !*row->registry_instance ||
                !qa_source_save_u32(io, &row->registry_seat)) return false;
        } else {
            if (!qa_source_save_count(io, &row->cvars.size, SIZE_MAX) || !row->cvars.size) return false;
            if (saved->reading) {
                if (row->cvars.size > io->input.size - io->offset) return false;
                row->cvars.data = malloc(row->cvars.size);
                if (!row->cvars.data) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native CLIENT registry bytes");
            }
            if (!qa_source_save_bytes(io, row->cvars.data, row->cvars.size)) return false;
        }
        if (!qa_source_save_count(io, &row->modules.size, SIZE_MAX)) return false;
        if (row->modules.size) {
            if (row->lifecycle != NATIVE_Q3_REMOTE_ATTACHED || !row->epoch || !row->module_sequence || row->modules.size < 12) return false;
            if (saved->reading) {
                if (row->modules.size > io->input.size - io->offset) return false;
                row->modules.data = malloc(row->modules.size);
                if (!row->modules.data) return application_fail(io->error, QA_ERROR_MEMORY, "Retaining staged CLIENT module continuation");
            }
            static const uint8_t signature[8] = {'Q','A','N','C','M',0,0,0};
            if (!qa_source_save_bytes(io, row->modules.data, row->modules.size) ||
                memcmp(row->modules.data, signature, sizeof(signature)) || qa_load_u32le(row->modules.data + 8) != 1) return false;
        }
        for (size_t j = 0; j < i; ++j) if (row->seat == saved->roles[j].seat ||
            row->service_owner == saved->roles[j].service_owner) return false;
    }
    for (size_t i = 1; i <= saved->descriptor_count; ++i) {
        bool used = false;
        for (size_t j = 0; j < saved->count; ++j) used |= saved->roles[j].descriptor == i;
        if (!used) return false;
    }
    return true;
}
static bool owner_ready(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->application ||
        !provider->launch || provider->launch->selection.runtime != QA_PROGRAM_BUILTIN ||
        !provider->product || provider->product->family != QA_GAME_Q3 || provider->close_pending ||
        !application_native_q3_remote_roles_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT codec requires its actual idle builtin receiver");
    return true;
}
bool application_native_q3_remote_roles_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !owner_ready(provider, error)) return false;
    saved_roles saved = {.product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA};
    for (struct application_native_q3_remote_role *row = provider->native_q3_remote_roles; row; row = row->next) ++saved.count;
    saved.roles = saved.count ? calloc(saved.count, sizeof(*saved.roles)) : NULL;
    saved.descriptors = saved.count ? calloc(saved.count, sizeof(*saved.descriptors)) : NULL;
    bool ok = !saved.count || (saved.roles && saved.descriptors);
    const qa_application_content_graph *graph = qa_application_content_graph_read(provider->application);
    size_t index = 0;
    for (struct application_native_q3_remote_role *row = provider->native_q3_remote_roles; ok && row; row = row->next) {
        if (row->retiring || ((row->modules || row->service || row->initialized || row->acquired_initialized || row->modules_restore.size) &&
            row->lifecycle != NATIVE_Q3_REMOTE_ATTACHED) ||
            (row->lifecycle == NATIVE_Q3_REMOTE_CLEARED && !row->connection_epoch)) { ok = false; break; }
        if (row->acquired_initialized) {
            qa_application_q3_remote_source source; bool completed = false;
            ok = row->modules && application_native_q3_remote_role_source_read(provider, row->seat,
                row->connection_epoch, &source, error) &&
                application_native_q3_remote_role_modules_initialized_read(provider, &source,
                    row->modules, &completed, error) && completed;
            if (!ok) break;
        }
        saved_role *r = saved.roles + index++;
        *r = (saved_role){.seat = row->seat, .service_owner = row->service_owner, .lifecycle = row->lifecycle,
            .argument_revision = row->argument_revision, .module_sequence = row->module_sequence, .arguments = row->arguments,
            .system_info = row->system_info, .epoch = row->connection_epoch, .generation = row->configuration_generation};
        if (row->descriptor) {
            const qa_launch_instance *source = qa_launch_instance_lease_view(row->descriptor);
            size_t d = 0;
            while (d < saved.descriptor_count && saved.descriptors[d].source->storage != source->storage) ++d;
            if (d == saved.descriptor_count) {
                saved_descriptor *value = saved.descriptors + saved.descriptor_count++;
                *value = (saved_descriptor){.source = source,
                    .catalog = qa_application_content_catalog_id(graph, qa_launch_instance_catalog(source)),
                    .view = qa_application_content_view_id(graph, source->content),
                    .product = source->selection.product, .identity = source->identity};
                ok = value->catalog && value->view && source->selection.runtime == QA_PROGRAM_BUILTIN &&
                    !source->artifact && !source->artifact_acquisition && !source->declaration &&
                    !source->interface_count && !source->behavior_count &&
                    source->selection.artifact && !*source->selection.artifact &&
                    source->selection.component && !*source->selection.component;
            }
            r->descriptor = d + 1;
        }
        if (ok && provider->application->q3_client_registry_reference) {
            const char *instance = NULL;
            ok = provider->application->q3_client_registry_reference(provider->application->guest_context,
                row->cvars, &instance, &r->registry_seat, &r->shared_registry, error);
            if (ok && r->shared_registry) {
                ok = instance && *instance;
                r->registry_instance = (char *)instance;
            }
        }
        if (ok && !r->shared_registry) ok = row->owns_cvars && qa_cvars_save_capture(row->cvars, &r->cvars, error);
        if (ok && row->modules) ok = qa_application_native_q3_client_modules_checkpoint(row->modules, &r->modules, error);
        else if (ok && row->modules_restore.size) {
            ok = provider->application->operation == APPLICATION_PERSISTING;
            if (ok) {
                r->modules.data = malloc(row->modules_restore.size);
                ok = r->modules.data != NULL;
                if (ok) {
                    r->modules.size = row->modules_restore.size;
                    memcpy(r->modules.data, row->modules_restore.data, r->modules.size);
                } else application_fail(error, QA_ERROR_MEMORY, "Copying staged CLIENT module witness");
            }
        }
    }
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, provider->application->session, error) &&
        fields(&io, &saved) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); dispose(&saved);
    if (!ok && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Native CLIENT continuation lacks its exact retained content or registry");
    return ok;
}
bool application_native_q3_remote_roles_restore_prepare(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    if (!owner_ready(provider, error) || provider->attached ||
        provider->application->operation != APPLICATION_PERSISTING) return false;
    saved_roles saved = {.reading = true}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, bytes, error) &&
        fields(&io, &saved) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    uint32_t product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    struct application_native_q3_remote_role *row = provider->native_q3_remote_roles;
    if (ok) ok = saved.product == product;
    for (size_t i = 0; ok && i < saved.count; ++i) {
        saved_role *r = saved.roles + i;
        ok = row && !row->retiring && row->lifecycle == NATIVE_Q3_REMOTE_COLD &&
            !row->service && !row->modules && !row->descriptor && !row->initialized && !row->acquired_initialized && row->owns_cvars &&
            !row->argument_revision && !row->module_sequence && !row->modules_restore.data && !row->modules_restore.size &&
            !row->system_info && !qa_cvars_count(row->cvars) &&
            r->seat == row->seat && r->service_owner == row->service_owner;
        if (ok) {
            r->actual = row;
            ok = r->shared_registry ? provider->application->q3_client_registry_reference != NULL :
                qa_cvars_save_prepare(row->cvars, (qa_bytes){r->cvars.data, r->cvars.size}, &r->ticket, error);
            row = row->next;
        }
    }
    if (ok) ok = row == NULL;
    qa_application_content_graph *graph = qa_application_content_graph_read(provider->application);
    for (size_t i = 0; ok && i < saved.descriptor_count; ++i) {
        saved_descriptor *d = saved.descriptors + i;
        qa_launch_restored_instance value = {.catalog = qa_application_content_catalog(graph, d->catalog),
            .selection = provider->launch->selection, .identity = d->identity};
        value.selection.product = d->product;
        const qa_product *content_product = qa_catalog_product(value.catalog, d->product);
        qa_vfs *view = qa_application_content_view(graph, d->view);
        ok = content_product && content_product->family == QA_GAME_Q3 &&
            view && view != provider->launch->content &&
            qa_vfs_resources(view) == qa_catalog_resources(value.catalog) &&
            qa_application_content_claim_view(graph, d->view, &value.content, error);
        if (ok) ok = qa_launch_instance_restore_builtin_client_metadata(provider->launch, &value, &d->restored, error);
    }
    for (size_t i = 0; ok && i < saved.count; ++i) {
        saved_role *r = saved.roles + i; row = r->actual;
        if (!r->shared_registry) {
            ok = qa_cvars_save_commit(r->ticket, error);
            if (!ok) break;
            r->ticket = NULL;
        }
        if (r->descriptor) ok = application_native_q3_remote_role_descriptor_bind(provider, row->seat,
            qa_launch_instance_lease_view(saved.descriptors[r->descriptor - 1].restored), r->epoch, r->generation, error);
        if (!ok) break;
        row->connection_epoch = r->epoch;
        qa_command_tokens_free(&row->arguments); row->arguments = r->arguments; r->arguments = (qa_command_tokens){0};
        row->argument_revision = r->argument_revision; row->system_info = r->system_info; r->system_info = NULL;
        row->module_sequence = r->module_sequence;
        row->lifecycle = (native_q3_remote_lifecycle)r->lifecycle;
        row->modules_restore = r->modules; r->modules = (qa_buffer){0};
        qa_application_startup_source source;
        ok = application_native_q3_remote_role_configuration(provider, row->seat, &source, error) &&
            application_startup_tuple_restore(provider, &source, error);
        if (ok && r->shared_registry) {
            const char *instance = NULL;
            uint32_t seat = 0;
            bool found = false;
            ok = provider->application->q3_client_registry_reference(provider->application->guest_context,
                row->cvars, &instance, &seat, &found, error) && found && instance &&
                !strcmp(instance, r->registry_instance) && seat == r->registry_seat;
        }
    }
    dispose(&saved);
    if (!ok && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Saved native CLIENT differs from its empty physical candidate");
    return ok;
}
bool application_native_q3_remote_roles_restore_match(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    qa_buffer actual = {0};
    bool ok = application_native_q3_remote_roles_capture(provider, &actual, error);
    if (ok && (actual.size != bytes.size || memcmp(actual.data, bytes.data, actual.size)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Native CLIENT changed its admitted registry, arguments or private descriptor");
    qa_buffer_free(&actual); return ok;
}
bool application_native_q3_remote_roles_content_visit(const application_provider *provider,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!provider || !visitor || !visitor->catalog || !visitor->view || !application_native_q3_remote_roles_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT content visitor requires its actual idle holders");
    for (const struct application_native_q3_remote_role *row = provider->native_q3_remote_roles; row; row = row->next) {
        if (row->descriptor) {
            const qa_launch_instance *source = qa_launch_instance_lease_view(row->descriptor);
            if (!visitor->catalog(visitor->context, qa_launch_instance_catalog(source), error) ||
                !visitor->view(visitor->context, source->content, error)) return false;
        }
        if (row->modules && !qa_application_native_q3_client_modules_content_visit(row->modules, visitor, error)) return false;
    }
    return true;
}

static struct application_native_q3_remote_role *restoring_row(qa_application *app,
    const qa_application_q3_remote_source *source, qa_error *error)
{
    uint64_t publication;
    if (!app || app->operation != APPLICATION_PERSISTING ||
        !qa_native_q3_remote_client_publication_read(app, source, &publication, error)) return NULL;
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] && app->providers[i]->owner == source->receiver.receiver) {
            if (provider) return NULL;
            provider = app->providers[i];
        }
    for (struct application_native_q3_remote_role *row = provider ? provider->native_q3_remote_roles : NULL;
        row; row = row->next) if (row->seat == source->receiver.seat && !row->retiring) return row;
    return NULL;
}
bool qa_native_q3_remote_client_modules_restore_read(qa_application *app,
    const qa_application_q3_remote_source *source, qa_bytes *out, bool *found, qa_error *error)
{
    struct application_native_q3_remote_role *row = restoring_row(app, source, error);
    if (!row || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Staged CLIENT modules require their real restoration source");
    *out = (qa_bytes){row->modules_restore.data, row->modules_restore.size};
    *found = row->modules_restore.size != 0; return true;
}
bool qa_native_q3_remote_client_modules_restore_complete(qa_application *app,
    const qa_application_q3_remote_source *source, qa_error *error)
{
    struct application_native_q3_remote_role *row = restoring_row(app, source, error);
    if (!row || !row->modules_restore.size || !row->modules ||
        !qa_application_native_q3_client_modules_current(row->modules, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Staged CLIENT completion requires its actual finished module owner");
    qa_buffer actual = {0};
    bool ok = qa_application_native_q3_client_modules_checkpoint(row->modules, &actual, error);
    if (ok && (actual.size != row->modules_restore.size || memcmp(actual.data, row->modules_restore.data, actual.size)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Restored CLIENT modules changed their staged continuation");
    qa_buffer_free(&actual);
    if (ok) qa_buffer_free(&row->modules_restore);
    return ok;
}
