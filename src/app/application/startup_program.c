#include "startup_program.h"
#include "guest_q3_factory.h"
#include "q3_product.h"
#include "qa/application_players.h"
#include "qa/console_program.h"
#include <stdlib.h>

typedef struct program_actor {
    struct program_actor *next;
    qa_actor_id source;
    uint32_t seat;
} program_actor;

struct application_startup_program {
    struct application_startup_program *next;
    struct application_startup_program *next_application;
    qa_application *application, *previous_application;
    application_publication *publication;
    const qa_launch_snapshot *previous, *candidate;
    qa_launch_instance_lease *previous_lease, *candidate_lease;
    qa_application_startup_source source, target;
    qa_application_console_scope origin_scope;
    application_provider *source_provider, *target_provider;
    qa_console_program *program;
    program_actor *actors;
    uint64_t previous_generation;
    bool retained, parked, restoring, engine;
};

struct application_startup_program_roster {
    application_startup_program *head, *tail;
};

static bool same_scope(qa_application_console_scope a, qa_application_console_scope b)
{ return a.provider == b.provider && a.kind == b.kind && a.seat == b.seat; }

static bool physical(application_provider *provider, const qa_application_startup_source *source,
    qa_error *error)
{
    for (size_t i = 0;; ++i) {
        qa_application_startup_source actual;
        bool found;
        if (!application_provider_startup_source_at(provider, i, &actual, &found, error)) return false;
        if (!found) break;
        if (actual.console == source->console && actual.cvars == source->cvars &&
            actual.descriptor->storage == source->descriptor->storage && same_scope(actual.scope, source->scope))
            return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Retained command program lost its physical source tuple");
}

static bool current(void *context, const qa_console *source, const qa_console *target, qa_error *error)
{
    application_startup_program *owner = context;
    if (owner->restoring) {
        qa_application *previous = owner->previous_application, *candidate = owner->application;
        if (qa_application_launch(previous) != owner->previous ||
            qa_application_launch(candidate) != owner->candidate ||
            previous->operation != APPLICATION_PERSISTING ||
            (candidate->operation != APPLICATION_PERSISTING && candidate->operation != APPLICATION_IDLE) ||
            previous->destroy_requested || candidate->destroy_requested ||
            previous->state == QA_APPLICATION_FAULTED || candidate->state == QA_APPLICATION_FAULTED ||
            source != owner->source.console || target != owner->target.console)
            return application_fail(error, QA_ERROR_ARGUMENT, "Live command continuation changed its actual current unit or restored world");
        if (owner->engine)
            return (source == previous->console && target == candidate->console &&
                owner->source.cvars == previous->cvars && owner->target.cvars == candidate->cvars) ||
                application_fail(error, QA_ERROR_ARGUMENT, "Live ENGINE continuation lost its actual console tuple");
        return owner->source_provider->attached && owner->target_provider->attached &&
            physical(owner->source_provider, &owner->source, error) &&
            physical(owner->target_provider, &owner->target, error);
    }
    bool candidate = qa_application_startup_candidate(owner->application) == owner->candidate;
    if (owner->publication) {
        application_publication *publication = owner->publication;
        candidate = publication->candidate == owner->candidate && publication->previous == owner->previous &&
            !publication->published && !publication->failed_retained;
        bool selected = false;
        for (size_t i = 0; candidate && i < publication->next_count; ++i)
            if (publication->next[i] == owner->target_provider) { selected = true; break; }
        candidate = candidate && selected;
    }
    if (qa_configuration_current(owner->application->configuration) != owner->previous ||
        !candidate ||
        source != owner->source.console || target != owner->target.console ||
        (!owner->parked && !owner->source_provider->attached) || owner->target_provider->attached != owner->retained)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command program changed its actual publication plan");
    const qa_application_startup_hooks *hooks=owner->application->startup_hooks;
    bool source_current=owner->parked?hooks && hooks->parked_program_source_current &&
        hooks->parked_program_source_current(hooks->context,owner->application,&owner->source,NULL,NULL,error):
        physical(owner->source_provider, &owner->source, error);
    return source_current &&
        physical(owner->target_provider, &owner->target, error);
}

static bool identity(void *context, qa_console_program_identity kind, uint64_t source,
    uint64_t *out, qa_error *error)
{
    application_startup_program *owner = context;
    if (!source) { *out = 0; return true; }
    /* The retained console keeps its actual declaration/client lifetimes.
     * Genuine Shutdown retirement filters any work whose role ends. */
    if (owner->retained) { *out = source; return true; }
    if (kind == QA_CONSOLE_PROGRAM_OWNER && source == owner->source.scope.provider) {
        *out = owner->target.scope.provider; return true;
    }
    bool handled = false;
    if (!owner->parked && !owner->engine && !application_guest_q3_program_identity(owner->source_provider, owner->target_provider,
        &owner->source, &owner->target, kind, source, out, &handled, error)) return false;
    if (handled) return true;
    if (kind == QA_CONSOLE_PROGRAM_OWNER && source == owner->source.declaration_owner) {
        *out = owner->target.declaration_owner; return true;
    }
    if (kind == QA_CONSOLE_PROGRAM_OWNER)
        for (size_t i = 0; i < qa_launch_snapshot_instance_count(owner->previous); ++i) {
            const qa_launch_instance *old = qa_launch_snapshot_instance(owner->previous, i);
            application_provider *provider = old->state;
            if (!provider || provider->owner != source) continue;
            const qa_launch_instance *selected = qa_launch_snapshot_find(owner->candidate, old->selection.instance);
            application_provider *candidate = selected ? selected->state : NULL;
            if (!candidate)
                return application_fail(error, QA_ERROR_ARGUMENT, "Queued command owner has no actual candidate declaration");
            *out = candidate->owner; return true;
        }
    /* Historical retired lifetime IDs keep their identity. The lower owner
     * checks that none can retire a genuine fresh declaration. */
    *out = source; return true;
}

static bool command_context(void *context, const qa_command_context *source,
    qa_command_context *out, qa_error *error)
{
    application_startup_program *owner = context;
    qa_command_context target = *source;
    target.session = owner->target.command.session;
    if (!identity(owner, QA_CONSOLE_PROGRAM_OWNER, source->owner, &target.owner, error) ||
        !identity(owner, QA_CONSOLE_PROGRAM_CLIENT, source->client, &target.client, error)) return false;
    if (source->actor.registry) {
        uint32_t seat;
        if (qa_application_player_seat(owner->previous_application, source->actor, &seat)) {
            program_actor *row = owner->actors;
            while (row && !qa_actor_id_equal(row->source, source->actor)) row = row->next;
            if (!row) {
                row = malloc(sizeof(*row));
                if (!row) return application_fail(error, QA_ERROR_MEMORY, "Retaining queued command player identity");
                *row = (program_actor){.next = owner->actors, .source = source->actor, .seat = seat};
                owner->actors = row;
            }
        }
    }
    *out = target; return true;
}

static bool context_retained(void *context, const qa_command_context *source,
    bool *retained, qa_error *error)
{
    (void)error;
    application_startup_program *owner = context;
    *retained = true;
    if (owner->restoring && source->actor.registry) {
        uint32_t seat;
        qa_actor_id carried;
        *retained = qa_application_player_seat(owner->previous_application, source->actor, &seat) &&
            qa_application_player_actor(owner->application, seat, &carried);
    }
    return true;
}

static bool published_context(void *context, const qa_command_context *source,
    const qa_command_context *prepared, qa_command_context *out, qa_error *error)
{
    application_startup_program *owner = context;
    qa_command_context target = *prepared;
    target.registry = target.generation = 0;
    if (source->actor.registry) {
        program_actor *row = owner->actors;
        while (row && !qa_actor_id_equal(row->source, source->actor)) row = row->next;
        if (row) {
            if (!qa_application_player_actor(owner->application, row->seat, &target.actor))
                return application_fail(error, QA_ERROR_ARGUMENT, "Published command program lost its actual controlled player");
        } else if (!qa_actors_get(qa_session_actors(owner->application->session), target.actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Published command program lost its retained actor");
    }
    return qa_application_capture_command_context(owner->application, &target, out, error);
}

static bool published_candidate_context(void *context, const qa_command_context *captured,
    qa_command_context *out, qa_error *error)
{
    application_startup_program *owner = context;
    if (qa_configuration_current(owner->application->configuration) != owner->candidate ||
        captured->session != owner->target.command.session ||
        (owner->engine ? owner->target.console != owner->application->console :
            !owner->target_provider->attached || !physical(owner->target_provider, &owner->target, error)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Init command lost its actual published source");
    qa_command_context target = *captured;
    target.registry = target.generation = 0;
    if (target.origin == QA_COMMAND_SEAT ||
        (target.origin == QA_COMMAND_LOCAL && target.actor.registry)) {
        target.actor = (qa_actor_id){0};
        (void)qa_application_player_actor(owner->application, target.seat, &target.actor);
    } else if (target.actor.registry &&
        !qa_actors_get(qa_session_actors(owner->application->session), target.actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Init command lost its actual source actor");
    return qa_application_capture_command_context(owner->application, &target, out, error);
}

static bool published(void *context, const qa_command_context *basis,
    const qa_console *target, qa_error *error)
{
    application_startup_program *owner = context;
    if (!basis || target != owner->target.console ||
        qa_configuration_current(owner->application->configuration) != owner->candidate ||
        owner->application->state == QA_APPLICATION_FAULTED ||
        (owner->engine ? target != owner->application->console : !owner->target_provider->attached))
        return application_fail(error, QA_ERROR_ARGUMENT, "Command program requires its actual successful publication");
    return owner->engine || physical(owner->target_provider, &owner->target, error);
}

static bool retained_abort(void *context, bool *restore_original, qa_error *error)
{
    application_startup_program *owner = context;
    qa_application *app = owner->application;
    application_provider *provider = owner->source_provider;
    if (app->startup_retiring_provider == provider && !provider->attached &&
        !provider->component_attached && !provider->policy_attached &&
        physical(provider, &owner->target, error)) {
        *restore_original = false; return true;
    }
    const qa_launch_snapshot *current = qa_configuration_current(app->configuration);
    if (current == owner->previous) { *restore_original = true; return true; }
    if (current == owner->candidate) { *restore_original = false; return true; }
    return application_fail(error, QA_ERROR_ARGUMENT, "Retained program lost its actual publication disposition");
}

static void dispose(application_startup_program *owner)
{
    while (owner->actors) {
        program_actor *row = owner->actors;
        owner->actors = row->next; free(row);
    }
    qa_launch_instance_lease_release(owner->previous_lease);
    qa_launch_instance_lease_release(owner->candidate_lease);
    qa_launch_snapshot_release(owner->previous);
    qa_launch_snapshot_release(owner->candidate);
    application_startup_program **position = &owner->application->startup_program_owners;
    while (*position && *position != owner) position = &(*position)->next_application;
    if (*position) *position = owner->next_application;
    free(owner);
}

static bool prepare_program(qa_application *app, qa_application *previous_app,
    const qa_launch_snapshot *candidate,
    const qa_application_startup_source *source, const qa_application_startup_source *target,
    application_publication *publication, application_startup_program **out, qa_error *error)
{
    bool restoring = app != previous_app;
    const qa_application_startup_hooks *hooks=app?app->startup_hooks:NULL;
    qa_application_console_scope origin=source?source->scope:(qa_application_console_scope){0};
    uint64_t generation=previous_app?previous_app->command_generation:0;
    bool parked=!restoring && source && source->scope.kind==QA_APPLICATION_CONSOLE_ENGINE && hooks &&
        hooks->parked_program_source_current &&
        hooks->parked_program_source_current(hooks->context,app,source,&origin,&generation,error);
    bool engine=restoring && source && source->scope.kind==QA_APPLICATION_CONSOLE_ENGINE;
    if (!app || !previous_app || !candidate || !source || !target || !out ||
        (!engine && (!source->descriptor || !target->descriptor)) ||
        !source->console || !target->console ||
        (!parked && source->scope.kind != target->scope.kind) || source->scope.seat != target->scope.seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command program needs its compatible physical source pair");
    *out = NULL;
    application_startup_program *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining source command continuation");
    owner->application = app; owner->previous_application = previous_app; owner->candidate = candidate;
    owner->restoring = restoring; owner->engine = engine;
    owner->publication = publication; owner->parked=parked;
    owner->previous_generation = generation; owner->origin_scope=origin;
    owner->previous = qa_configuration_current(previous_app->configuration);
    qa_launch_snapshot_retain(owner->previous); qa_launch_snapshot_retain(candidate);
    owner->source = *source; owner->target = *target;
    bool ok = true;
    if (!engine) {
        const qa_launch_instance *old = owner->previous
            ? qa_launch_snapshot_find(owner->previous, source->descriptor->selection.instance) : parked?source->descriptor:NULL;
        const qa_launch_instance *selected = qa_launch_snapshot_find(candidate, target->descriptor->selection.instance);
        ok = old && selected && old->storage == source->descriptor->storage &&
            selected->storage == target->descriptor->storage && (parked || old->state) && selected->state;
        if (!ok) application_fail(error, QA_ERROR_ARGUMENT, "Command continuation lost its retained actual descriptors");
        if (ok) {
            owner->source_provider = parked?NULL:old->state; owner->target_provider = selected->state;
            owner->retained = source->console == target->console;
            if (owner->retained && (owner->source_provider != owner->target_provider ||
                source->cvars != target->cvars || !same_scope(source->scope, target->scope))) {
                dispose(owner);
                return application_fail(error, QA_ERROR_ARGUMENT, "Retained program selected another physical lifetime");
            }
            ok = qa_launch_instance_retain_metadata(old, &owner->previous_lease, error) &&
                qa_launch_instance_retain_metadata(selected, &owner->candidate_lease, error);
        }
    }
    if (ok) {
        if (!engine) {
            owner->source.descriptor = qa_launch_instance_lease_view(owner->previous_lease);
            owner->target.descriptor = qa_launch_instance_lease_view(owner->candidate_lease);
        }
        qa_console_program_resolvers resolve = {.context = owner, .fresh_namespace = restoring,
            .context_retained = restoring ? context_retained : NULL,
            .identity = identity,
            .command_context = command_context, .published_context = published_context,
            .published_candidate_context = published_candidate_context,
            .current = current, .published = published, .retained_abort = retained_abort};
        owner->program = owner->retained ? qa_console_program_retain(target->console, &resolve, error)
            : qa_console_program_prepare(source->console, target->console, &resolve, error);
        ok = owner->program != NULL;
    }
    if (!ok) { dispose(owner); return false; }
    owner->next_application = app->startup_program_owners;
    app->startup_program_owners = owner;
    *out = owner; return true;
}

bool application_startup_program_prepare(qa_application *app, const qa_launch_snapshot *candidate,
    const qa_application_startup_source *source, const qa_application_startup_source *target,
    application_startup_program **out, qa_error *error)
{ return prepare_program(app, app, candidate, source, target, NULL, out, error); }

bool application_startup_program_refresh(application_startup_program *owner,
    const qa_application_startup_source *target, qa_error *error)
{
    if (!owner || !target || !target->descriptor ||
        target->descriptor->storage != owner->target.descriptor->storage ||
        target->console != owner->target.console || !same_scope(target->scope, owner->target.scope) ||
        !target->cvars || !target->declaration_owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command preparation changed its actual physical target");
    qa_application_startup_source actual;
    bool found = false;
    for (size_t index = 0;; ++index) {
        if (!application_provider_startup_source_at(owner->target_provider, index, &actual, &found, error)) return false;
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Command preparation lost its actual target declarations");
        if (actual.console == target->console) break;
    }
    if (!actual.descriptor || actual.descriptor->storage != target->descriptor->storage ||
        actual.cvars != target->cvars || !same_scope(actual.scope, target->scope) ||
        actual.declaration_owner != target->declaration_owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command preparation selected another declaration owner");
    owner->target.cvars = target->cvars;
    owner->target.declaration_owner = target->declaration_owner;
    return true;
}

void application_startup_program_bound_client(qa_application *app, application_provider *provider,
    const qa_application_startup_source *source)
{
    for (application_startup_program *owner = app->startup_program_owners; owner;
         owner = owner->next_application)
        if (owner->target_provider == provider && owner->target.console == source->console &&
            owner->target.descriptor->storage == source->descriptor->storage &&
            same_scope(owner->target.scope, source->scope))
            owner->target.cvars = source->cvars;
}

bool application_startup_program_preflight(application_startup_program *owner, qa_error *error)
{ return !owner || qa_console_program_preflight(owner->program, error); }
bool application_startup_program_seal(application_startup_program *owner, qa_error *error)
{ return !owner || qa_console_program_seal(owner->program, error); }
bool application_startup_program_adopt(application_startup_program **slot, qa_error *error)
{
    application_startup_program *owner = slot ? *slot : NULL;
    if (!owner) return true;
    /* Startup ordinals identify the admitted programme's original Source
     * receipt. This historical scope never grants a live GAME callback. */
    qa_application_startup_source ordinal_source=owner->source;
    ordinal_source.scope=owner->origin_scope;
    if (!application_startup_program_queue_ready(owner->application, &ordinal_source, &owner->target,
        owner->previous_generation, error)) return false;
    if (!qa_console_program_adopt(owner->program, error)) return false;
    application_startup_program_queue_publish(owner->application, &ordinal_source, &owner->target,
        owner->previous_generation);
    *slot = NULL; dispose(owner); return true;
}
bool application_startup_program_abort(application_startup_program **slot, qa_error *error)
{
    application_startup_program *owner = slot ? *slot : NULL;
    if (!owner) return true;
    if (!qa_console_program_abort(owner->program, error)) return false;
    *slot = NULL; dispose(owner); return true;
}

static bool roster_prepare(qa_application *app, qa_application *previous,
    const qa_launch_snapshot *candidate, const qa_application_startup_source *source,
    const qa_application_startup_source *target, application_publication *publication,
    application_startup_program_roster **out, qa_error *error)
{
    if (!*out) {
        *out = calloc(1, sizeof(**out));
        if (!*out) return application_fail(error, QA_ERROR_MEMORY, "Retaining live command programs");
    }
    application_startup_program *program = NULL;
    if (!prepare_program(app, previous, candidate, source, target, publication, &program, error)) return false;
    if ((*out)->tail) (*out)->tail->next = program;
    else (*out)->head = program;
    (*out)->tail = program;
    return true;
}

static bool restore_source(qa_application *app, qa_console *console,
    qa_application_console_scope scope, qa_application_startup_source *out, qa_error *error)
{
    if (scope.kind == QA_APPLICATION_CONSOLE_ENGINE) {
        *out = (qa_application_startup_source){.scope=scope, .console=console, .cvars=app->cvars};
        return console == app->console && qa_console_context_read(console, &out->command, error);
    }
    const char *instance = qa_application_provider_instance(app, scope.provider);
    const qa_launch_instance *selected = instance ? qa_launch_snapshot_find(qa_application_launch(app), instance) : NULL;
    application_provider *provider = selected ? selected->state : NULL;
    if (!provider) return application_fail(error, QA_ERROR_ARGUMENT, "Live command source lost its selected GAME instance");
    for (size_t i=0;;++i) {
        qa_application_startup_source actual;
        bool found;
        if (!application_provider_startup_source_at(provider, i, &actual, &found, error)) return false;
        if (!found) break;
        if (actual.console == console && same_scope(actual.scope, scope)) {
            *out=actual; out->descriptor=selected; return true;
        }
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Live command source lost its actual GAME console tuple");
}

bool application_startup_program_restore_prepare(qa_application *candidate, qa_application *previous,
    application_startup_program_roster **out, qa_error *error)
{
    if (!candidate || !previous || candidate == previous || !out || *out ||
        previous->operation != APPLICATION_PERSISTING || candidate->operation != APPLICATION_IDLE ||
        !qa_application_launch(candidate) || !qa_application_launch(previous))
        return application_fail(error, QA_ERROR_ARGUMENT, "Live unit commands require their completed restored GAME and current source");
    size_t previous_count=qa_application_console_count(previous);
    for (size_t i=0,count=qa_application_console_count(candidate);i<count;++i) {
        qa_console *target_console=qa_application_console_at(candidate,i,NULL);
        qa_application_console_scope target_scope;
        if (!qa_application_console_scope_read(candidate,target_console,&target_scope))
            return application_fail(error, QA_ERROR_ARGUMENT, "Restored GAME console has no actual source scope");
        if (target_scope.kind==QA_APPLICATION_CONSOLE_CLIENT ||
            target_scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME || target_scope.kind==QA_APPLICATION_CONSOLE_Q3_UI) continue;
        const char *target_instance=target_scope.provider?
            qa_application_provider_instance(candidate,target_scope.provider):"";
        if (!target_instance) return application_fail(error, QA_ERROR_ARGUMENT, "Restored command source has no actual instance");
        for (size_t j=0;j<previous_count;++j) {
            qa_console *source_console=qa_application_console_at(previous,j,NULL);
            qa_application_console_scope source_scope;
            if (!qa_application_console_scope_read(previous,source_console,&source_scope))
                return application_fail(error, QA_ERROR_ARGUMENT, "Current GAME console has no actual source scope");
            const char *source_instance=source_scope.provider?
                qa_application_provider_instance(previous,source_scope.provider):"";
            if (source_scope.kind!=target_scope.kind || source_scope.seat!=target_scope.seat ||
                !source_instance || strcmp(source_instance,target_instance)) continue;
            qa_application_startup_source source, target;
            if (!restore_source(previous,source_console,source_scope,&source,error) ||
                !restore_source(candidate,target_console,target_scope,&target,error) ||
                !roster_prepare(candidate,previous,qa_application_launch(candidate),&source,&target,NULL,out,error))
                return false;
            break;
        }
    }
    return true;
}

bool application_startup_program_publication_prepare(qa_application *app, application_publication *publication,
    application_startup_program_roster **out, qa_error *error)
{
    if (!app || !publication || !out || *out || !publication->candidate ||
        publication->published || publication->failed_retained || app->startup_flow ||
        app->operation != APPLICATION_CONFIGURING ||
        publication->previous != qa_configuration_current(app->configuration))
        return application_fail(error, QA_ERROR_ARGUMENT, "Command programs require their actual validated replacement ticket");
    const qa_application_startup_hooks *hooks = app->startup_hooks;
    if (!hooks || !hooks->program_source) return true;
    for (size_t i = 0; i < publication->next_count; ++i) {
        application_provider *provider = publication->next[i];
        if (!provider || provider->application != app || !provider->constructed || provider->close_pending)
            return application_fail(error, QA_ERROR_ARGUMENT, "Replacement command program lost its constructed source");
        for (size_t index = 0;; ++index) {
            qa_application_startup_source target, previous;
            bool found, inherited = false;
            if (!application_provider_startup_source_at(provider, index, &target, &found, error)) return false;
            if (!found) break;
            const qa_launch_instance *selected = target.descriptor
                ? qa_launch_snapshot_find(publication->candidate, target.descriptor->selection.instance) : NULL;
            if (!selected || selected->state != provider || selected->storage != target.descriptor->storage)
                return application_fail(error, QA_ERROR_ARGUMENT, "Replacement program lost its physical candidate descriptor");
            target.descriptor = selected;
            if (!hooks->program_source(hooks->context, app, publication->candidate,
                &target, &previous, &inherited, error)) return false;
            if (!inherited) continue;
            if (!roster_prepare(app,app,publication->candidate,&previous,&target,publication,out,error)) return false;
        }
    }
    return true;
}

bool application_startup_program_publication_seal(application_startup_program_roster *owner, qa_error *error)
{
    if (!owner) return true;
    for (application_startup_program *program = owner->head; program; program = program->next)
        if (!application_startup_program_preflight(program, error)) return false;
    for (application_startup_program *program = owner->head; program; program = program->next)
        if (!application_startup_program_seal(program, error)) return false;
    return true;
}

bool application_startup_program_publication_adopt(application_startup_program_roster **slot, qa_error *error)
{
    application_startup_program_roster *owner = slot ? *slot : NULL;
    if (!owner) return true;
    while (owner->head) {
        application_startup_program *program = owner->head, *next = program->next;
        if (!application_startup_program_adopt(&program, error)) return false;
        owner->head = next;
    }
    free(owner); *slot = NULL; return true;
}

bool application_startup_program_publication_abort(application_startup_program_roster **slot, qa_error *error)
{
    application_startup_program_roster *owner = slot ? *slot : NULL;
    if (!owner) return true;
    while (owner->head) {
        application_startup_program *program = owner->head, *next = program->next;
        if (!application_startup_program_abort(&program, error)) return false;
        owner->head = next;
    }
    free(owner); *slot = NULL; return true;
}
