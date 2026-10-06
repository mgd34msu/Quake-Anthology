#include "native_client_roles.h"
#include "qa/console_cvar_observer.h"
#include "qa/application_client_save.h"
#include "qa/application_client_prepare.h"
#include "startup_flow.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct application_native_client_role {
    struct application_native_client_role *next;
    application_provider *provider;
    qa_launch_instance_lease *metadata;
    qa_application_client_options options;
    qa_application_client_source source;
    qa_actor_id *actors;
    size_t actor_count, actor_capacity;
    uint64_t entity_generation;
    qa_application_client_entity_publication entity_publication;
    bool entity_publication_set;
    unsigned calls;
    bool retiring, entity_mutating;
};
static application_provider *provider_read(qa_application *app, qa_actor_owner owner)
{
    for (application_provider *p = app ? app->live_providers : NULL; p; p = p->next_live)
        if (p->owner == owner) return p;
    return app ? application_startup_flow_provider(app, owner) : NULL;
}
static struct application_native_client_role *row_read(application_provider *provider, uint32_t seat)
{
    for (struct application_native_client_role *r = provider ? provider->native_client_roles : NULL; r; r = r->next)
        if (r->source.context.seat == seat) return r;
    return NULL;
}
bool application_native_client_only(const application_provider *p)
{
    uint64_t roles = QA_ROLE_BIT(QA_ROLE_HUD) | QA_ROLE_BIT(QA_ROLE_AUDIO) | QA_ROLE_BIT(QA_ROLE_MENU);
    return p && p->launch && p->product && (p->client_only_owned || p->product->family != QA_GAME_Q3) &&
        p->launch->selection.runtime == QA_PROGRAM_BUILTIN && !p->launch->artifact &&
        (p->launch->roles & QA_ROLE_BIT(QA_ROLE_HUD)) && !(p->launch->roles & ~roles);
}
bool qa_application_client_provider_prepare(qa_application *app, const qa_launch_instance *descriptor,
    qa_actor_owner *out, qa_error *error)
{
    uint64_t roles = QA_ROLE_BIT(QA_ROLE_HUD) | QA_ROLE_BIT(QA_ROLE_AUDIO) | QA_ROLE_BIT(QA_ROLE_MENU);
    if (!app || !app->session || !out || !descriptor || !descriptor->storage || !descriptor->content ||
        !descriptor->selection.instance || !*descriptor->selection.instance || descriptor->artifact ||
        descriptor->selection.runtime != QA_PROGRAM_BUILTIN || !(descriptor->roles & QA_ROLE_BIT(QA_ROLE_HUD)) ||
        (descriptor->roles & ~roles))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT provider requires genuine compiled CLIENT-only metadata");
    qa_catalog *catalog = qa_launch_instance_catalog(descriptor);
    const qa_product *product = qa_catalog_product(catalog, descriptor->selection.product);
    if (!product)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT provider lost its selected catalog product");
    qa_actor_owner owner = qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)descriptor->selection.instance, strlen(descriptor->selection.instance)});
    application_provider *old = owner ? provider_read(app, owner) : NULL;
    if (old) {
        if (!old->client_only_owned || old->close_pending || old->launch->storage != descriptor->storage)
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT descriptor cannot alias another physical provider");
        *out = old->owner; return true;
    }
    application_provider *p = NULL;
    if (!application_provider_prepare(app, descriptor, &p, error)) return false;
    qa_catalog_retain(catalog); p->product_catalog = catalog; p->product = product;
    p->client_only_owned = true; p->constructed = true; p->attached = true;
    *out = p->owner; return true;
}
bool qa_application_client_provider_release(qa_application *app, qa_actor_owner owner, qa_error *error)
{
    application_provider *p = provider_read(app, owner);
    if (!p || !p->client_only_owned || !application_native_client_roles_idle(p))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT provider release requires its returned physical owners");
    if (p->native_client_roles)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT provider still owns actual physical rows");
    p->attached = false;
    return application_provider_close(app, p, error);
}
bool qa_application_client_provider_command(qa_application *app, qa_actor_owner receiver, uint32_t seat,
    const qa_command_context *input, qa_command_context *out, uint64_t *generation, qa_error *error)
{
    application_provider *p = provider_read(app, receiver);
    if (!p || !p->client_only_owned || p->close_pending || !input || !out || !generation ||
        input->seat != seat || (input->owner && input->owner != receiver) || input->script || input->actor.registry ||
        input->actor.generation || input->actor.slot ||
        (input->origin != QA_COMMAND_SEAT && input->origin != QA_COMMAND_LOCAL))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT command requires its real local input origin");
    qa_console_dialect dialect;
    switch (p->launch->selection.clock.kind) {
    case QA_CLOCK_NETQUAKE: dialect = QA_CONSOLE_Q1; break;
    case QA_CLOCK_QUAKEWORLD: dialect = QA_CONSOLE_QW; break;
    case QA_CLOCK_Q2_CLASSIC: dialect = QA_CONSOLE_Q2; break;
    case QA_CLOCK_Q2_RERELEASE: dialect = QA_CONSOLE_Q2_RERELEASE; break;
    case QA_CLOCK_Q3: dialect = QA_CONSOLE_Q3; break;
    default: return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT descriptor has no nonQ3 console dialect");
    }
    if (input->dialect != dialect)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT input dialect differs from its selected descriptor");
    qa_command_context source = *input; source.owner = receiver;
    uint64_t actual = qa_application_configuration_generation(app);
    if (!qa_application_capture_command_context(app, &source, out, error)) return false;
    *generation = actual; return true;
}
static bool command_equal(const qa_command_context *a, const qa_command_context *b)
{
    return a->session == b->session && a->owner == b->owner && a->client == b->client && a->seat == b->seat &&
        a->dialect == b->dialect && a->origin == b->origin && a->direct == b->direct &&
        a->console_text == b->console_text && a->script == b->script && a->registry == b->registry &&
        a->generation == b->generation && qa_actor_id_equal(a->actor, b->actor);
}
static bool source_equal(const qa_application_client_source *a, const qa_application_client_source *b)
{
    const qa_application_client_context *x = &a->context, *y = &b->context;
    return a->descriptor && b->descriptor && a->descriptor->storage == b->descriptor->storage &&
        a->descriptor->content == b->descriptor->content && a->descriptor->identity == b->descriptor->identity &&
        x->session == y->session && x->receiver == y->receiver && x->entity_owner == y->entity_owner &&
        x->entity_definition == y->entity_definition &&
        x->seat == y->seat && x->physical_seat == y->physical_seat && x->console == y->console && x->cvars == y->cvars &&
        x->lifetime == y->lifetime && command_equal(&x->command, &y->command) && a->runtime == b->runtime &&
        qa_net_client_id_equal(a->client, b->client) && a->network_seat.owner == b->network_seat.owner &&
        a->network_seat.index == b->network_seat.index && a->connection_epoch == b->connection_epoch &&
        a->configuration_generation == b->configuration_generation;
}
static bool physical_current(struct application_native_client_role *r)
{
    if (!r || r->retiring || r->calls == UINT_MAX || r->provider->close_pending ||
        !application_native_client_only(r->provider)) return false;
    ++r->calls;
    bool current = r->options.owner.current(r->options.owner.context, r->source.descriptor,
        r->source.context.console, r->source.context.cvars, &r->source.context.command);
    --r->calls;
    return current;
}
static bool connection_current(struct application_native_client_role *r, const qa_application_client_source *s)
{
    if (!s->client.owner) return !s->client.generation && !s->client.slot && !s->network_seat.owner &&
        !s->network_seat.index && !s->connection_epoch;
    if (r->calls == UINT_MAX) return false;
    ++r->calls;
    bool current = r->options.owner.connection_current(r->options.owner.context, s);
    --r->calls; return current;
}
static bool retirement_current(struct application_native_client_role *r,const qa_application_client_source *s)
{
    if(!s->client.owner||!s->client.generation||!s->network_seat.owner||!s->connection_epoch||
        !r->options.owner.retirement_current||r->calls==UINT_MAX) return false;
    ++r->calls;
    bool held=r->options.owner.retirement_current(r->options.owner.context,s);
    --r->calls; return held;
}
static bool create(qa_application *app, const qa_application_client_options *o,
    const qa_application_client_state *saved, qa_application_client_source *out, qa_error *error)
{
    application_provider *p = o ? provider_read(app, o->receiver) : NULL;
    if (!app || !out || !o || !application_native_client_only(p) || row_read(p, o->seat) ||
        !o->descriptor || !o->descriptor->storage || !o->descriptor->content || o->descriptor->artifact ||
        o->descriptor->selection.runtime != QA_PROGRAM_BUILTIN ||
        strcmp(o->descriptor->selection.instance, p->launch->selection.instance) ||
        !o->console || !o->cvars || !o->runtime ||
        o->command.owner != p->owner || o->command.seat != o->seat || !o->command.registry ||
        !o->command.generation || o->command.script || qa_cvars_dialect(o->cvars) != o->command.dialect ||
        !qa_application_command_context_active(app, &o->command) ||
        !o->owner.context || !o->owner.retain || !o->owner.release || !o->owner.current ||
        !o->owner.idle || !o->owner.connection_current)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT constructor requires its selected physical descriptor and registry owner");
    const qa_product *product = qa_catalog_product(qa_launch_instance_catalog(o->descriptor), o->descriptor->selection.product);
    if (!product || product->family != p->product->family ||
        !o->owner.current(o->owner.context, o->descriptor, o->console, o->cvars, &o->command))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT constructor lost its actual catalog or physical tuple");
    struct application_native_client_role *r = calloc(1, sizeof(*r));
    if (!r) return application_fail(error, QA_ERROR_MEMORY, "Retaining physical CLIENT row");
    r->provider = p; r->options = *o;
    if (!qa_launch_instance_retain_metadata(o->descriptor, &r->metadata, error)) { free(r); return false; }
    qa_actor_owner entity_owner = saved ? saved->entity_owner : 0;
    qa_actor_definition entity_definition = 0;
    const char *definition = product->family == QA_GAME_Q1 ? "q1:remote-entity" :
        product->family == QA_GAME_Q2 ? "q2:remote-entity" : "q3:remote-entity";
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), definition, &entity_definition, error)) {
        qa_launch_instance_lease_release(r->metadata); free(r); return false;
    }
    bool qualified = true;
    if (saved) {
        char prefix[80]; int prefix_length = snprintf(prefix, sizeof(prefix), "client-entities:%u:%u:", o->receiver, o->seat);
        const char *name = qa_strings_cstr(qa_session_strings(app->session), entity_owner);
        if (!entity_owner || !name || prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix) ||
            strncmp(name, prefix, (size_t)prefix_length) || !name[prefix_length] ||
            saved->receiver != o->receiver || saved->seat != o->seat || saved->physical_seat != o->physical_seat ||
            saved->configuration_generation != o->configuration_generation ||
            saved->actor_count > qa_actors_capacity(qa_session_actors(app->session)) ||
            (saved->actor_count && (!saved->actors || !saved->entity_generation))) qualified = false;
        if (qualified) for (const char *digit = name + prefix_length; *digit; ++digit)
            if (*digit < '0' || *digit > '9') { qualified = false; break; }
        for (application_provider *other = app->live_providers; qualified && other; other = other->next_live)
            for (struct application_native_client_role *row = other->native_client_roles; row; row = row->next)
                if (row->source.context.entity_owner == entity_owner) qualified = false;
        if (qualified && saved->actor_count) {
            r->actors = calloc(saved->actor_count, sizeof(*r->actors));
            if (!r->actors) {
                application_fail(error, QA_ERROR_MEMORY, "Retaining imported CLIENT observer references");
                qualified = false;
            }
            r->actor_capacity = saved->actor_count;
        }
        for (size_t i = 0; qualified && i < saved->actor_count; ++i) {
            const qa_actor_record *record = qa_actors_resolve_saved(qa_session_actors(app->session), saved->actors[i]);
            if (!record || record->owner != entity_owner || record->definition != entity_definition || !record->has_source) {
                qualified = false; break;
            }
            for (size_t j = 0; j < i; ++j)
                if (qa_actor_id_equal(record->id, r->actors[j])) { qualified = false; break; }
            if (qualified) r->actors[r->actor_count++] = record->id;
        }
    } else {
        char name[128]; int n = snprintf(name, sizeof(name), "client-entities:%u:%u:%zu", p->owner, o->seat,
            qa_strings_count(qa_session_strings(app->session)));
        qualified = n >= 0 && (size_t)n < sizeof(name) &&
            qa_strings_intern_cstr(qa_session_strings(app->session), name, &entity_owner, error);
    }
    if (!qualified) {
        qa_launch_instance_lease_release(r->metadata); free(r->actors); free(r);
        if (!error || error->code == QA_OK)
            application_fail(error, QA_ERROR_ARGUMENT, "Saved CLIENT namespace lost its imported references");
        return false;
    }
    r->source = (qa_application_client_source){.descriptor = qa_launch_instance_lease_view(r->metadata),
        .context = {.session = app->session, .receiver = p->owner, .entity_owner = entity_owner,
            .entity_definition = entity_definition,
            .seat = o->seat, .physical_seat = o->physical_seat, .console = o->console, .cvars = o->cvars,
            .command = o->command, .lifetime = r},
        .runtime = o->runtime, .configuration_generation = o->configuration_generation};
    if (saved) {
        r->source.client = saved->client; r->source.network_seat = saved->network_seat;
        r->source.connection_epoch = saved->connection_epoch; r->entity_generation = saved->entity_generation;
    }
    if (!connection_current(r, &r->source) && !(saved && retirement_current(r, &r->source))) {
        qa_launch_instance_lease_release(r->metadata); free(r->actors); free(r);
        return application_fail(error, QA_ERROR_ARGUMENT, "Saved CLIENT connection lost its genuine imported owner");
    }
    if (!o->owner.retain(o->owner.context, error)) {
        qa_launch_instance_lease_release(r->metadata); free(r->actors); free(r); return false;
    }
    r->next = p->native_client_roles; p->native_client_roles = r; *out = r->source; return true;
}
bool qa_application_client_create(qa_application *app, const qa_application_client_options *o,
    qa_application_client_source *out, qa_error *error)
{ return create(app, o, NULL, out, error); }
bool qa_application_client_create_restored(qa_application *app, const qa_application_client_options *o,
    const qa_application_client_state *saved, qa_application_client_source *out, qa_error *error)
{
    if (!saved || (saved->client.owner ? (!saved->client.generation || !saved->network_seat.owner || !saved->connection_epoch) :
        (saved->client.generation || saved->client.slot || saved->network_seat.owner || saved->network_seat.index || saved->connection_epoch)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Saved CLIENT connection tuple is incomplete");
    return create(app, o, saved, out, error);
}
bool qa_application_client_read(qa_application *app, qa_actor_owner receiver, uint32_t seat,
    qa_application_client_source *out, qa_error *error)
{
    struct application_native_client_role *r = row_read(provider_read(app, receiver), seat);
    if (!out || !physical_current(r) || !connection_current(r, &r->source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT observation lost its retained physical receiver");
    *out = r->source; return true;
}
bool qa_application_client_physical_read(qa_application *app, qa_actor_owner receiver, uint32_t seat,
    qa_application_client_source *out, qa_error *error)
{
    struct application_native_client_role *r = row_read(provider_read(app, receiver), seat);
    if (!out || !physical_current(r))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT command namespace lost its retained physical owner");
    *out = r->source; return true;
}
bool qa_application_client_current(qa_application *app, const qa_application_client_source *source)
{
    struct application_native_client_role *r = source ? row_read(provider_read(app, source->context.receiver), source->context.seat) : NULL;
    return r && source_equal(source, &r->source) && physical_current(r) && connection_current(r, source);
}
bool qa_application_client_retirement_current(qa_application *app,const qa_application_client_source *source)
{
    struct application_native_client_role *r=source?row_read(provider_read(app,source->context.receiver),source->context.seat):NULL;
    return r&&source_equal(source,&r->source)&&physical_current(r)&&retirement_current(r,source);
}
bool application_native_client_source_associated(const qa_application *app,const qa_application_client_source *source)
{
    application_provider *p=NULL;
    for (application_provider *at=app?app->live_providers:NULL;at;at=at->next_live)
        if (source && at->owner==source->context.receiver) { p=at; break; }
    struct application_native_client_role *r=source?row_read(p,source->context.seat):NULL;
    return p && p->application==app && p->constructed && p->attached && !p->close_pending &&
        application_native_client_only(p) && r && !r->retiring && source_equal(source,&r->source);
}
bool qa_application_client_associated(const qa_application *app,const qa_application_client_source *source)
{ return application_native_client_source_associated(app,source); }
bool application_native_client_observer_actor(void *context, const qa_session *session,
    const qa_actor_record *actor)
{
    const qa_application *app = context;
    if (!app || app->session != session || !actor || !actor->has_source ||
        qa_actors_get(qa_session_actors(session), actor->id) != actor) return false;
    for (application_provider *p = app->live_providers; p; p = p->next_live) {
        if (!application_native_client_only(p)) continue;
        for (const struct application_native_client_role *r = p->native_client_roles; r; r = r->next) {
            const qa_application_client_context *source = &r->source.context;
            if (r->provider != p || r->retiring || r->entity_mutating || !r->entity_generation ||
                source->session != session || source->lifetime != r || source->receiver != p->owner ||
                source->entity_owner != actor->owner || source->entity_definition != actor->definition ||
                r->source.descriptor != qa_launch_instance_lease_view(r->metadata)) continue;
            for (size_t i = 0; i < r->actor_count; ++i)
                if (qa_actor_id_equal(r->actors[i], actor->id))
                    return application_native_client_source_associated(app, &r->source);
        }
    }
    return false;
}
bool qa_application_client_bind(qa_application *app, const qa_application_client_source *pending,
    qa_net_client_id client, qa_net_seat_id seat, uint64_t epoch, qa_application_client_source *out, qa_error *error)
{
    struct application_native_client_role *r = pending ? row_read(provider_read(app, pending->context.receiver), pending->context.seat) : NULL;
    if (!r || !out || r->calls || qa_application_client_prepare_holds(app,pending) ||
        r->source.client.owner || !client.owner || !client.generation || !seat.owner || !epoch ||
        !qa_application_client_current(app, pending))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT bind requires its pending row and genuine attach receipt");
    qa_application_client_source bound = r->source;
    bound.client = client; bound.network_seat = seat; bound.connection_epoch = epoch;
    if (!connection_current(r, &bound) || !physical_current(r))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT attach receipt is no longer current");
    r->source = bound; *out = bound; return true;
}
bool qa_application_client_rebind(qa_application *app, const qa_application_client_source *retained,
    const qa_launch_instance *descriptor, const qa_command_context *command, uint64_t generation,
    qa_application_client_source *out, qa_error *error)
{
    struct application_native_client_role *r = retained ? row_read(provider_read(app, retained->context.receiver), retained->context.seat) : NULL;
    if (!r || !out || r->calls || r->retiring || qa_application_client_prepare_holds(app,retained) ||
        !source_equal(retained, &r->source) || !descriptor ||
        !descriptor->storage || !descriptor->content || descriptor->artifact || !command || command->script ||
        command->owner != r->source.context.receiver || command->seat != r->source.context.seat ||
        command->dialect != r->source.context.command.dialect || !qa_application_command_context_active(app, command) ||
        descriptor->selection.runtime != QA_PROGRAM_BUILTIN ||
        descriptor->selection.clock.kind != r->source.descriptor->selection.clock.kind ||
        strcmp(descriptor->selection.instance, r->source.descriptor->selection.instance) ||
        strcmp(descriptor->selection.implementation, r->source.descriptor->selection.implementation))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT rebind requires the same retained physical namespace");
    const qa_product *product = qa_catalog_product(qa_launch_instance_catalog(descriptor), descriptor->selection.product);
    if (!product || product->family != r->provider->product->family)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT replacement changed its actual compiled family");
    qa_launch_instance_lease *metadata = NULL;
    if (!qa_launch_instance_retain_metadata(descriptor, &metadata, error)) return false;
    qa_application_client_source next = r->source;
    next.descriptor = qa_launch_instance_lease_view(metadata); next.context.command = *command;
    next.configuration_generation = generation;
    ++r->calls;
    bool current = r->options.owner.current(r->options.owner.context, next.descriptor,
        next.context.console, next.context.cvars, command);
    --r->calls;
    if (!current || !connection_current(r, &next)) {
        qa_launch_instance_lease_release(metadata);
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT replacement lacks its actual admitted graph");
    }
    qa_launch_instance_lease *old = r->metadata; r->metadata = metadata; r->source = next;
    qa_launch_instance_lease_release(old); *out = next; return true;
}
static bool entity_publication_read(struct application_native_client_role *row,
    const qa_application_client_source *source, qa_application_client_entity_publication *out)
{
    if (!row->options.owner.entity_publication || row->calls == UINT_MAX ||
        !qa_application_client_current(row->provider->application, source)) return false;
    ++row->calls;
    bool current = row->options.owner.entity_publication(row->options.owner.context, source, out);
    --row->calls;
    return current && (!out->published || out->map_generation) &&
        qa_application_client_current(row->provider->application, source);
}
static bool entity_publication_equal(const qa_application_client_entity_publication *a,
    const qa_application_client_entity_publication *b)
{
    return a->published == b->published && a->map_generation == b->map_generation &&
        a->source_frame == b->source_frame && a->received_ns == b->received_ns;
}
bool qa_application_client_entities_refresh(qa_application *app,
    const qa_application_client_source *source, qa_error *error)
{
    struct application_native_client_role *row = source ? row_read(provider_read(app,
        source->context.receiver), source->context.seat) : NULL;
    qa_application_client_entity_publication publication = {0};
    if (!row || row->calls || row->entity_mutating || !source->client.owner ||
        !row->options.owner.entity_current || !entity_publication_read(row, source, &publication))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT entity refresh lacks its actual decoded publication");
    row->entity_mutating = true; ++row->calls;
    qa_actor_registry *actors = qa_session_actor_registry(app->session);
    bool ok = true;
    for (size_t i = 0; ok && i < row->actor_count;) {
        qa_actor_id id = row->actors[i];
        const qa_actor_record *record = qa_actors_get(actors, id);
        bool keep = false;
        if (record) {
            if (record->owner != source->context.entity_owner ||
                record->definition != source->context.entity_definition || !record->has_source) {
                ok = application_fail(error, QA_ERROR_FORMAT, "CLIENT observer leaves its actual source namespace"); break;
            }
            if (publication.published && row->entity_generation == publication.map_generation) {
                uint64_t generation = 0;
                keep = row->options.owner.entity_current(row->options.owner.context, source,
                    record->source_slot, &generation);
                if (keep && generation != publication.map_generation) {
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "CLIENT entity changed its decoded map publication"); break;
                }
            }
        }
        qa_application_client_entity_publication current = {0};
        if (!entity_publication_read(row, source, &current) || !entity_publication_equal(&publication, &current)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "CLIENT decoded publication changed during observer refresh"); break;
        }
        if (keep) { ++i; continue; }
        if (record && !qa_actors_release(actors, id, error)) { ok = false; break; }
        memmove(row->actors + i, row->actors + i + 1, (row->actor_count - i - 1) * sizeof(*row->actors));
        --row->actor_count;
    }
    qa_application_client_entity_publication current = {0};
    if (ok && (!entity_publication_read(row, source, &current) || !entity_publication_equal(&publication, &current)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "CLIENT decoded publication retired during observer refresh");
    if (ok) {
        row->entity_generation = publication.published ? publication.map_generation : 0;
        row->entity_publication = publication; row->entity_publication_set = true;
    }
    --row->calls; row->entity_mutating = false;
    return ok;
}
bool qa_application_client_entity_read(qa_application *app, const qa_application_client_source *source,
    uint32_t number, qa_actor_id *out, qa_error *error)
{
    struct application_native_client_role *r = source ? row_read(provider_read(app, source->context.receiver), source->context.seat) : NULL;
    if (!r || !out || r->entity_mutating || r->calls == UINT_MAX || !r->options.owner.entity_current ||
        !source->client.owner || !qa_application_client_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT entity requires a genuine decoded source receipt");
    if (r->options.owner.entity_publication) {
        qa_application_client_entity_publication publication = {0};
        if (!entity_publication_read(r, source, &publication))
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT entity lost its actual decoded publication");
        if ((!r->entity_publication_set || !entity_publication_equal(&publication, &r->entity_publication)) &&
            !qa_application_client_entities_refresh(app, source, error)) return false;
    }
    uint64_t generation = 0;
    ++r->calls; bool current = r->options.owner.entity_current(r->options.owner.context, source, number, &generation); --r->calls;
    if (!current || !generation || !qa_application_client_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT entity publication retired");
    qa_actor_registry *actors = qa_session_actor_registry(app->session);
    if (r->entity_generation != generation) {
        r->entity_mutating = true; ++r->calls;
        while (r->actor_count) {
            qa_actor_id id = r->actors[r->actor_count - 1];
            if (qa_actors_get(actors, id) && !qa_actors_release(actors, id, error)) {
                --r->calls; r->entity_mutating = false; return false;
            }
            --r->actor_count;
        }
        uint64_t actual = 0;
        current = r->options.owner.entity_current(r->options.owner.context, source, number, &actual);
        --r->calls; r->entity_mutating = false;
        if (!current || actual != generation || !qa_application_client_current(app, source))
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT map changed during observer retirement");
        r->entity_generation = generation;
    }
    const qa_actor_record *record = qa_actors_at_source(actors, source->context.entity_owner, number);
    if (record) {
        if (record->definition != source->context.entity_definition)
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT entity changed its genuine source definition");
        *out = record->id; return true;
    }
    if (r->actor_count == r->actor_capacity) {
        size_t capacity = r->actor_capacity ? r->actor_capacity * 2 : 32;
        if (capacity < r->actor_capacity || capacity > SIZE_MAX / sizeof(*r->actors))
            return application_fail(error, QA_ERROR_MEMORY, "CLIENT entity namespace exceeds capacity");
        qa_actor_id *rows = realloc(r->actors, capacity * sizeof(*rows));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining CLIENT entity namespace");
        r->actors = rows; r->actor_capacity = capacity;
    }
    qa_actor_id id;
    if (!qa_actors_allocate_source(actors, source->context.entity_owner, number,
        source->context.entity_definition, &id, error)) return false;
    r->actors[r->actor_count++] = id; *out = id; return true;
}
static bool row_idle(const struct application_native_client_role *r)
{
    return r && !r->calls && !r->entity_mutating && qa_console_idle(r->source.context.console) &&
        qa_cvars_observer_idle(r->source.context.cvars) && r->options.owner.idle(r->options.owner.context);
}
bool qa_application_client_epoch_adopt(qa_application *app,const qa_application_client_source *retained,
    uint64_t epoch,qa_application_client_source *out,qa_error *error)
{
    struct application_native_client_role *r=retained?row_read(provider_read(app,retained->context.receiver),retained->context.seat):NULL;
    if(!r||!out||!epoch||!r->source.client.owner||!r->source.client.generation||
        !r->source.network_seat.owner||r->retiring||!source_equal(retained,&r->source)||
        !row_idle(r)||qa_application_client_prepare_holds(app,retained)||!physical_current(r))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT epoch adoption requires its actual returned physical namespace");
    qa_application_client_source next=r->source;
    next.connection_epoch=epoch;
    if(!connection_current(r,&next)||!physical_current(r))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT epoch adoption lost its genuine restarted transport receipt");
    r->source=next; *out=next; return true;
}
bool qa_application_client_idle(qa_application *app, const qa_application_client_source *source)
{
    const struct application_native_client_role *r = source ? row_read(provider_read(app, source->context.receiver), source->context.seat) : NULL;
    return r && source_equal(source, &r->source) && row_idle(r);
}
static bool client_capture(qa_application *app, const qa_application_client_source *source,
    qa_application_client_state *out, bool retired, qa_error *error)
{
    struct application_native_client_role *r = source ? row_read(provider_read(app, source->context.receiver), source->context.seat) : NULL;
    if (!r || !out || out->actors || app->client_preparation ||
        !source_equal(source, &r->source) || r->retiring || !row_idle(r) ||
        !(retired?qa_application_client_retirement_current(app,source):qa_application_client_current(app,source)))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT capture requires its returned physical namespace");
    qa_application_client_state state = {.receiver = source->context.receiver, .entity_owner = source->context.entity_owner,
        .seat = source->context.seat, .physical_seat = source->context.physical_seat,
        .configuration_generation = source->configuration_generation, .connection_epoch = source->connection_epoch,
        .entity_generation = r->entity_generation, .client = source->client, .network_seat = source->network_seat,
        .actor_count = r->actor_count};
    if (state.actor_count) {
        state.actors = calloc(state.actor_count, sizeof(*state.actors));
        if (!state.actors) return application_fail(error, QA_ERROR_MEMORY, "Capturing CLIENT observer references");
        for (size_t i = 0; i < state.actor_count; ++i)
            if (!qa_actors_save_reference(qa_session_actors(app->session), r->actors[i], state.actors + i, error)) {
                free(state.actors); return false;
            }
    }
    *out = state; return true;
}
bool qa_application_client_capture(qa_application *app,const qa_application_client_source *source,
    qa_application_client_state *out,qa_error *error)
{ return client_capture(app,source,out,false,error); }
bool qa_application_client_capture_retired(qa_application *app,const qa_application_client_source *source,
    qa_application_client_state *out,qa_error *error)
{ return client_capture(app,source,out,true,error); }
void qa_application_client_state_free(qa_application_client_state *state)
{ if (state) { free(state->actors); *state = (qa_application_client_state){0}; } }
static bool retire(struct application_native_client_role *r, qa_error *error)
{
    if (!row_idle(r)) return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT retirement retains entered physical owners");
    if (qa_application_client_prepare_holds(r->provider->application,&r->source))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement retains its settings preparation");
    r->retiring = true;
    qa_actor_registry *actors = qa_session_actor_registry(r->provider->application->session);
    while (r->actor_count) {
        qa_actor_id id = r->actors[r->actor_count - 1];
        if (qa_actors_get(actors, id)) {
            ++r->calls;
            bool released = qa_actors_release(actors, id, error);
            --r->calls;
            if (!released) return false;
        }
        --r->actor_count;
    }
    if (!r->options.owner.release(r->options.owner.context, error)) return false;
    struct application_native_client_role **position = &r->provider->native_client_roles;
    while (*position != r) position = &(*position)->next;
    *position = r->next; qa_launch_instance_lease_release(r->metadata); free(r->actors); free(r); return true;
}
bool qa_application_client_retire(qa_application *app, const qa_application_client_source *source, qa_error *error)
{
    struct application_native_client_role *r = source ? row_read(provider_read(app, source->context.receiver), source->context.seat) : NULL;
    if (!r || !source_equal(source, &r->source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT retirement lost its exact retained row");
    if (qa_application_client_prepare_holds(app,source))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement retains its settings preparation");
    return retire(r, error);
}
bool application_native_client_role_configuration(application_provider *p, uint32_t seat,
    qa_application_startup_source *out, qa_error *error)
{
    struct application_native_client_role *r = row_read(p, seat);
    if (!r || !out || r->retiring) return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT configuration has no retained constructor");
    *out = (qa_application_startup_source){.descriptor = r->source.descriptor,
        .scope = {p->owner, QA_APPLICATION_CONSOLE_CLIENT, seat}, .console = r->source.context.console,
        .cvars = r->source.context.cvars, .command = r->source.context.command, .declaration_owner = p->owner};
    return true;
}
bool application_native_client_role_source_at(application_provider *p, size_t index,
    qa_application_startup_source *out, bool *found, qa_error *error)
{
    if (!out || !found) return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT source inventory requires outputs");
    struct application_native_client_role *r = p ? p->native_client_roles : NULL;
    while (r && index--) r = r->next;
    *found = r != NULL;
    return !r || application_native_client_role_configuration(p, r->source.context.seat, out, error);
}
bool application_native_client_roles_idle(const application_provider *p)
{
    for (const struct application_native_client_role *r = p ? p->native_client_roles : NULL; r; r = r->next)
        if (!row_idle(r)) return false;
    return true;
}
bool application_native_client_roles_destroy(application_provider *p, qa_error *error)
{
    if (!application_native_client_roles_idle(p)) return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT rows retain physical callbacks");
    while (p && p->native_client_roles) if (!retire(p->native_client_roles, error)) return false;
    return true;
}
bool application_native_client_console_scope(const application_provider *p, const qa_console *console,
    qa_application_console_scope *out)
{
    for (const struct application_native_client_role *r = p ? p->native_client_roles : NULL; r; r = r->next)
        if (!r->retiring && r->source.context.console == console) {
            *out = (qa_application_console_scope){p->owner, QA_APPLICATION_CONSOLE_CLIENT, r->source.context.seat}; return true;
        }
    return false;
}
