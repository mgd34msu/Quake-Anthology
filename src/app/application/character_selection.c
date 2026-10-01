#include "character_selection.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct character_selection_owner {
    qa_application *application;
    qa_actor_id actor;
    uint32_t seat;
    qa_launch_instance_lease *metadata;
    qa_native_q3_character_selection selection;
    char *definition, *model, *skin, *head_model, *head_skin;
} character_selection_owner;

bool qa_native_q3_character_default_declaration(qa_game_family family,
    qa_native_q3_character_declaration *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor declaration requires output");
    const char *model;
    switch (family) {
    case QA_GAME_Q1: model = "player"; break;
    case QA_GAME_Q2: model = "male"; break;
    case QA_GAME_Q3: model = "sarge"; break;
    default: return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor has no declared source family");
    }
    *out = (qa_native_q3_character_declaration){model, "default", model, "default"};
    return true;
}

static const qa_launch_binding *actor_binding(const qa_launch_choices *choices, qa_actor_id actor)
{
    if (!actor.registry) return NULL;
    for (size_t i = 0; i < choices->binding_count; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (binding->role == QA_ROLE_CHARACTER && !*binding->selector &&
            binding->scope.kind == QA_SCOPE_ACTOR && qa_actor_id_equal(binding->scope.actor, actor))
            return binding;
    }
    return NULL;
}

static const qa_launch_binding *seat_binding(const qa_launch_choices *choices, const qa_launch_seat *seat)
{
    const qa_launch_binding *binding = actor_binding(choices, seat->actor);
    return binding ? binding : qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, QA_ROLE_CHARACTER, "");
}

static bool declared(qa_catalog *catalog, const qa_launch_choices *choices, const qa_launch_seat *seat,
    const qa_launch_binding *binding, qa_application_character_declaration *out, bool *found, qa_error *error)
{
    *found = false;
    const char *fields[] = {seat->character_model, seat->character_skin,
        seat->character_head_model, seat->character_head_skin};
    if (!fields[0] && !fields[1] && !fields[2] && !fields[3]) return true;
    for (size_t i = 0; i < 4; ++i)
        if (!fields[i] || (i != 2 && !*fields[i]) || strpbrk(fields[i], "\\\";\r\n"))
            return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor declaration is incomplete or invalid");
    const qa_launch_provider *provider = NULL;
    if (binding)
        for (size_t i = 0; i < choices->provider_count; ++i)
            if (!strcmp(choices->providers[i].instance, binding->instance)) {
                provider = &choices->providers[i]; break;
            }
    const qa_product *product = provider ? qa_catalog_product(catalog, provider->product) : NULL;
    if (!binding || !binding->definition || !provider || !product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration lost its actual selected binding or product");
    *out = (qa_application_character_declaration){.provider = provider, .binding = binding,
        .product = provider->product, .family = product->family, .definition = binding->definition,
        .appearance = {fields[0], fields[1], fields[2], fields[3]}};
    *found = true;
    return true;
}

bool qa_application_character_declaration_read(qa_catalog *catalog, const qa_launch_choices *choices,
    const qa_launch_seat *seat, qa_application_character_declaration *out, bool *found, qa_error *error)
{
    if (!catalog || !choices || !seat || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration requires its actual candidate choices");
    return declared(catalog, choices, seat, seat_binding(choices, seat), out, found, error);
}

bool application_character_userinfo(qa_catalog *catalog, const qa_launch_choices *choices,
    const qa_launch_seat *seat, qa_game_family protocol, bool local_ip,
    char *out, size_t capacity, qa_error *error)
{
    qa_application_character_declaration declaration;
    bool found;
    if (!out || !capacity || !seat || !seat->name)
        return application_fail(error, QA_ERROR_ARGUMENT, "Initial userinfo requires its actual CHARACTER declaration");
    if (!qa_application_character_declaration_read(catalog, choices, seat, &declaration, &found, error)) return false;
    const char *name = strchr(seat->name, '\\') ? "badinfo" : seat->name;
    int length;
    if (protocol == QA_GAME_Q3) {
        const char *team = seat->spectator ? "s" : seat->team && *seat->team ? seat->team : "free";
        const char *ip = local_ip ? "\\ip\\localhost" : "";
        if (found) {
            const qa_native_q3_character_declaration *appearance = &declaration.appearance;
            const char *head = *appearance->head_model ? appearance->head_model : appearance->model;
            length = snprintf(out, capacity,
                "\\name\\%.900s\\model\\%s/%s\\headmodel\\%s/%s\\team\\%s%s",
                name, appearance->model, appearance->skin, head, appearance->head_skin, team, ip);
        } else length = snprintf(out, capacity, "\\name\\%.900s\\team\\%s%s", name, team, ip);
    } else if (protocol == QA_GAME_Q2) {
        if (found) {
            /* registerPlayerUserinfo selects Q2's protocol model independently
             * of foreign CHARACTER appearance, then its source skin default. */
            const char *model = declaration.family == QA_GAME_Q2 ? declaration.appearance.model : "male";
            const char *skin = !strcmp(model, "female") ? "athena" : !strcmp(model, "cyborg") ? "oni911" : "grunt";
            length = snprintf(out, capacity, "\\name\\%.2000s\\skin\\%s/%s\\spectator\\%d",
                name, model, skin, seat->spectator ? 1 : 0);
        } else length = snprintf(out, capacity, "\\name\\%.2000s\\spectator\\%d", name, seat->spectator ? 1 : 0);
    } else return application_fail(error, QA_ERROR_ARGUMENT, "Initial userinfo has no declared protocol constructor");
    return (length >= 0 && (size_t)length < capacity) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Initial CHARACTER userinfo exceeds its source extent");
}

bool application_character_q2_initial_skin(qa_catalog *catalog, const qa_launch_choices *choices,
    const qa_launch_seat *seat, const char *source, char *out, size_t capacity, qa_error *error)
{
    if (!source || !out || !capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Initial Q2 skin requires actual source userinfo");
    size_t length = strlen(source);
    if (length >= capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Initial Q2 userinfo exceeds its source extent");
    memcpy(out, source, length + 1);
    /* Q2's source lookup is case-sensitive and distinguishes an existing
     * empty skin from an absent key. Preserve every existing wire byte. */
    const char *p = source;
    if (*p == '\\') ++p;
    while (*p) {
        const char *key = p;
        while (*p && *p != '\\') ++p;
        size_t key_length = (size_t)(p - key);
        if (!*p) break;
        ++p;
        if (key_length == 4 && !memcmp(key, "skin", 4)) return true;
        while (*p && *p != '\\') ++p;
        if (*p) ++p;
    }
    qa_application_character_declaration declaration;
    bool found;
    if (!qa_application_character_declaration_read(catalog, choices, seat, &declaration, &found, error)) return false;
    if (!found || declaration.family != QA_GAME_Q2) return true;
    const char *model = declaration.appearance.model;
    const char *skin = !strcmp(model, "female") ? "athena" : !strcmp(model, "cyborg") ? "oni911" : "grunt";
    int added = snprintf(out + length, capacity - length, "\\skin\\%s/%s", model, skin);
    return (added >= 0 && (size_t)added < capacity - length) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Initial declared Q2 skin exceeds its source extent");
}

static bool constructor_choices(qa_application *app, qa_actor_owner receiver,
    const qa_launch_snapshot **out, qa_error *error)
{
    if (!app || !receiver || !out || app->destroy_requested || app->state == QA_APPLICATION_FAULTED)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor requires its actual source callback owner");
    const qa_launch_snapshot *snapshot = app->routing_snapshot ? app->routing_snapshot : qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    application_provider **providers = app->routing_snapshot ? app->routing_providers : app->providers;
    size_t count = app->routing_snapshot ? app->routing_provider_count : app->provider_count;
    application_provider *source = NULL;
    for (size_t i = 0; providers && i < count; ++i)
        if (providers[i] && providers[i]->owner == receiver) { source = providers[i]; break; }
    const qa_launch_provider *selected = NULL;
    if (choices && source && source->launch)
        for (size_t i = 0; i < choices->provider_count; ++i)
            if (!strcmp(choices->providers[i].instance, source->launch->selection.instance)) {
                selected = &choices->providers[i]; break;
            }
    if (!snapshot || !choices || !source || source->application != app || source->close_pending ||
        !selected || selected->product != source->launch->selection.product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor callback left its actual routing source");
    *out = snapshot;
    return true;
}

bool qa_application_character_constructor_read(qa_application *app, qa_actor_owner receiver,
    uint32_t id, qa_application_character_declaration *out, bool *found, qa_error *error)
{
    const qa_launch_snapshot *snapshot;
    if (!out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor requires declaration output");
    if (!constructor_choices(app, receiver, &snapshot, error)) return false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    *found = false;
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == id)
            return qa_application_character_declaration_read(qa_launch_snapshot_catalog(snapshot), choices,
                &choices->seats[i], out, found, error);
    return true;
}

bool qa_application_constructor_seat_ordinal(qa_application *app, qa_actor_owner receiver,
    uint32_t id, uint32_t *physical_ordinal, qa_error *error)
{
    const qa_launch_snapshot *snapshot;
    if (!physical_ordinal)
        return application_fail(error, QA_ERROR_ARGUMENT, "Constructor seat requires physical ordinal output");
    if (!constructor_choices(app, receiver, &snapshot, error)) return false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == id) {
            if (i > UINT32_MAX)
                return application_fail(error, QA_ERROR_ARGUMENT, "Constructor seat exceeds source client extent");
            *physical_ordinal = (uint32_t)i;
            return true;
        }
    return application_fail(error, QA_ERROR_ARGUMENT, "Constructor seat is absent from the actual routing choices");
}

static bool published(qa_application *app, uint32_t id, qa_actor_id actor,
    qa_application_character_declaration *out, bool *found, qa_error *error)
{
    const qa_launch_snapshot *snapshot = qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!snapshot || !choices)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration has no actual publication");
    const qa_launch_seat *seat = NULL;
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == id) { seat = &choices->seats[i]; break; }
    *found = false;
    if (!seat) return true;
    const qa_launch_binding *binding = actor_binding(choices, actor);
    qa_actor_id configured;
    if (!binding && application_player_source_actor(app, actor, &configured))
        binding = actor_binding(choices, configured);
    if (!binding) binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, QA_ROLE_CHARACTER, "");
    return declared(qa_launch_snapshot_catalog(snapshot), choices, seat, binding, out, found, error);
}

static bool same_declaration(const qa_native_q3_character_declaration *a,
    const qa_native_q3_character_declaration *b)
{
    return a && a->model && a->skin && a->head_model && a->head_skin &&
        !strcmp(a->model, b->model) && !strcmp(a->skin, b->skin) &&
        !strcmp(a->head_model, b->head_model) && !strcmp(a->head_skin, b->head_skin);
}

static bool selection_current(void *context, const qa_native_q3_character_selection *selection)
{
    character_selection_owner *owner = context;
    qa_application *app = owner ? owner->application : NULL;
    qa_actor_id actor;
    qa_application_character_declaration declaration;
    bool found;
    if (!app || !selection || app->destroy_requested || app->routing_snapshot ||
        app->state == QA_APPLICATION_FAULTED || app->publication_generation != owner->selection.publication_generation ||
        !qa_application_player_actor(app, owner->seat, &actor) || !qa_actor_id_equal(actor, owner->actor) ||
        !published(app, owner->seat, actor, &declaration, &found, NULL) || !found) return false;
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    qa_native_q3_character_declaration retained = {owner->model, owner->skin, owner->head_model, owner->head_skin};
    return provider && provider->application == app && provider->constructed && provider->attached &&
        !provider->close_pending && provider->owner == owner->selection.owner && provider->launch &&
        provider->launch->storage == owner->selection.launch->storage && provider->launch->content == owner->selection.content &&
        provider->launch->selection.product == owner->selection.product &&
        !strcmp(declaration.provider->instance, provider->launch->selection.instance) &&
        !strcmp(declaration.definition, owner->selection.definition) &&
        same_declaration(&retained, &declaration.appearance) &&
        selection->owner == owner->selection.owner && selection->product == owner->selection.product &&
        selection->publication_generation == owner->selection.publication_generation &&
        selection->launch == owner->selection.launch && selection->content == owner->selection.content &&
        selection->definition == owner->selection.definition && selection->model == owner->selection.model &&
        selection->skin == owner->selection.skin && selection->head_model == owner->selection.head_model &&
        selection->head_skin == owner->selection.head_skin && selection->lifetime == owner &&
        selection->current == selection_current;
}

static void selection_release(void *context)
{
    character_selection_owner *owner = context;
    if (!owner) return;
    qa_launch_instance_lease_release(owner->metadata);
    free(owner->definition); free(owner->model); free(owner->skin);
    free(owner->head_model); free(owner->head_skin); free(owner);
}

static char *copy_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy) memcpy(copy, text, length + 1);
    return copy;
}

bool qa_native_q3_character_selection_create(qa_application *app, uint32_t seat,
    const qa_native_q3_character_declaration *constructor, qa_native_q3_character_selection *out, qa_error *error)
{
    qa_actor_id actor;
    qa_application_character_declaration declaration;
    bool found;
    if (!app || !out || app->destroy_requested || app->routing_snapshot || app->state == QA_APPLICATION_FAULTED ||
        !qa_application_player_actor(app, seat, &actor) ||
        !published(app, seat, actor, &declaration, &found, error) || !found ||
        !same_declaration(constructor, &declaration.appearance))
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor does not match the actual selected seat declaration");
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || !provider->launch->content ||
        strcmp(declaration.provider->instance, provider->launch->selection.instance))
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration does not name its actual live provider");
    character_selection_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected CHARACTER declaration");
    owner->application = app; owner->actor = actor; owner->seat = seat;
    owner->definition = copy_text(declaration.definition); owner->model = copy_text(constructor->model);
    owner->skin = copy_text(constructor->skin); owner->head_model = copy_text(constructor->head_model);
    owner->head_skin = copy_text(constructor->head_skin);
    if (!owner->definition || !owner->model || !owner->skin || !owner->head_model || !owner->head_skin ||
        !qa_launch_instance_retain_metadata(provider->launch, &owner->metadata, error)) {
        selection_release(owner);
        if (!error || error->code == QA_OK) application_fail(error, QA_ERROR_MEMORY, "Copying actual CHARACTER declaration");
        return false;
    }
    owner->selection = (qa_native_q3_character_selection){.owner = provider->owner,
        .product = provider->launch->selection.product, .publication_generation = app->publication_generation,
        .launch = qa_launch_instance_lease_view(owner->metadata), .content = provider->launch->content,
        .definition = owner->definition, .model = owner->model, .skin = owner->skin,
        .head_model = owner->head_model, .head_skin = owner->head_skin,
        .lifetime = owner, .current = selection_current, .release = selection_release};
    if (!selection_current(owner, &owner->selection)) {
        selection_release(owner);
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration changed during constructor capture");
    }
    *out = owner->selection;
    return true;
}

bool qa_application_character_selection_read(qa_application *app, uint32_t seat,
    qa_native_q3_character_selection *out, bool *found, qa_error *error)
{
    if (!app || !out || !found || app->destroy_requested || !qa_application_launch(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Character selection requires its actual published application");
    *found = false;
    qa_actor_id actor;
    if (!qa_application_player_actor(app, seat, &actor)) return true;
    qa_application_character_declaration declaration;
    bool declared_seat;
    if (!published(app, seat, actor, &declaration, &declared_seat, error)) return false;
    if (!declared_seat) return true;
    if (!qa_native_q3_character_selection_create(app, seat, &declaration.appearance, out, error)) return false;
    *found = true;
    return true;
}
