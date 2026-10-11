#include "native_q3_remote_client.h"
#include "character_selection.h"
#include <stdlib.h>
#include <string.h>

typedef struct remote_character_owner {
    qa_application *application;
    qa_application_q3_remote_source source;
    qa_launch_instance_lease *receiver, *character;
    qa_native_q3_character_selection selection;
    application_character_names names;
    const qa_launch_choices *choices;
    application_provider *provider;
} remote_character_owner;

static application_provider *character_provider(qa_application *app, const char *instance)
{
    application_provider **providers = app->routing_snapshot ? app->routing_providers : app->providers;
    size_t count = app->routing_snapshot ? app->routing_provider_count : app->provider_count;
    application_provider *found = NULL;
    qa_string_id id = qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)instance, strlen(instance)});
    for (size_t i = 0; providers && i < count; ++i)
        if (providers[i] && providers[i]->launch && providers[i]->owner == id) {
            if (found) return NULL;
            found = providers[i];
        }
    return found;
}
static bool same_physical(const qa_application_q3_remote_source *a, const qa_application_q3_remote_source *b)
{
    const qa_application_q3_client_context *x = &a->receiver, *y = &b->receiver;
    return a->descriptor && b->descriptor && a->descriptor->storage == b->descriptor->storage &&
        a->descriptor->content == b->descriptor->content && a->configuration_generation == b->configuration_generation &&
        a->connection_epoch == b->connection_epoch && x->session == y->session && x->receiver == y->receiver &&
        x->seat == y->seat && x->service_owner == y->service_owner && x->frontend_lifetime == y->frontend_lifetime &&
        x->console == y->console && x->cvars == y->cvars && x->client_time_cvars == y->client_time_cvars &&
        x->client_time_owner == y->client_time_owner && x->native_source && y->native_source;
}
static bool current(void *context, const qa_native_q3_character_selection *selection)
{
    remote_character_owner *owner = context;
    qa_application *app = owner ? owner->application : NULL;
    qa_application_q3_remote_source source;
    qa_application_character_declaration declaration;
    uint64_t publication;
    bool found;
    if (!app || !selection || app->destroy_requested || app->state == QA_APPLICATION_FAULTED ||
        app->publication_generation != owner->selection.publication_generation ||
        !qa_application_q3_remote_source_read(app, owner->source.receiver.receiver, owner->source.receiver.seat,
            owner->source.connection_epoch, &source, NULL) || !same_physical(&source, &owner->source) ||
        !qa_native_q3_remote_client_publication_read(app, &source, &publication, NULL)) return false;
    application_provider *provider = owner->provider;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(app->routing_snapshot
        ? app->routing_snapshot : qa_application_launch(app));
    if (choices != owner->choices) {
        if (!qa_application_character_constructor_read(app, source.receiver.receiver, source.receiver.seat,
                &declaration, &found, NULL) || !found ||
            character_provider(app, declaration.provider->instance) != provider ||
            declaration.product != owner->selection.product ||
            !application_character_names_match(&owner->names, &declaration)) return false;
    }
    const qa_native_q3_character_selection *retained = &owner->selection;
    bool valid = provider && provider->application == app && provider->constructed && provider->attached &&
        !provider->close_pending && provider->owner == retained->owner && provider->launch &&
        provider->launch->storage == retained->launch->storage && provider->launch->content == retained->content &&
        provider->launch->selection.product == retained->product &&
        selection->owner == retained->owner && selection->product == retained->product &&
        selection->publication_generation == retained->publication_generation && selection->launch == retained->launch &&
        selection->content == retained->content && selection->definition == retained->definition &&
        selection->model == retained->model && selection->skin == retained->skin &&
        selection->head_model == retained->head_model && selection->head_skin == retained->head_skin &&
        selection->lifetime == owner && selection->current == current && selection->release == retained->release;
    if (valid) owner->choices = choices;
    return valid;
}
static void release(void *context)
{
    remote_character_owner *owner = context;
    if (!owner) return;
    qa_launch_instance_lease_release(owner->receiver); qa_launch_instance_lease_release(owner->character);
    application_character_names_release(&owner->names); free(owner);
}
bool qa_native_q3_remote_client_character_selection_read(qa_application *app,
    const qa_application_q3_remote_source *source, qa_native_q3_character_selection *out, qa_error *error)
{
    qa_application_character_declaration declaration;
    uint64_t publication;
    bool found;
    if (!out || !qa_native_q3_remote_client_publication_read(app, source, &publication, error) ||
        !qa_application_character_constructor_read(app, source->receiver.receiver, source->receiver.seat,
            &declaration, &found, error) || !found)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT requires its actual authored CHARACTER declaration");
    application_provider *provider = character_provider(app, declaration.provider->instance);
    if (!provider || provider->application != app || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || !provider->launch->content || provider->launch->selection.product != declaration.product)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CHARACTER declaration lost its actual selected provider");
    remote_character_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return native_client_fail(error, QA_ERROR_MEMORY, "Retaining remote CHARACTER declaration");
    owner->application = app; owner->source = *source; owner->provider = provider;
    owner->choices = qa_launch_snapshot_choices(app->routing_snapshot
        ? app->routing_snapshot : qa_application_launch(app));
    if (!application_character_names_retain(qa_session_strings(app->session), &declaration,
            &owner->names, &owner->selection, error) ||
        !qa_launch_instance_retain_metadata(source->descriptor, &owner->receiver, error) ||
        !qa_launch_instance_retain_metadata(provider->launch, &owner->character, error)) {
        release(owner);
        if (!error || error->code == QA_OK) native_client_fail(error, QA_ERROR_MEMORY, "Copying remote CHARACTER metadata");
        return false;
    }
    owner->source.descriptor = qa_launch_instance_lease_view(owner->receiver);
    owner->selection.owner = provider->owner; owner->selection.product = declaration.product;
    owner->selection.publication_generation = publication;
    owner->selection.launch = qa_launch_instance_lease_view(owner->character);
    owner->selection.content = provider->launch->content; owner->selection.lifetime = owner;
    owner->selection.current = current; owner->selection.release = release;
    if (!current(owner, &owner->selection)) {
        release(owner); return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CHARACTER declaration changed during capture");
    }
    *out = owner->selection; return true;
}
