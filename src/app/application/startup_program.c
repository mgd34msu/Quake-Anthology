#include "startup_program.h"
#include "map_players_private.h"
#include "q3_product.h"
#include "qa/application_players.h"
#include "qa/console_program.h"
#include <stdlib.h>

typedef struct program_actor {
    struct program_actor *next;
    qa_actor_id source;
    uint32_t seat;
} program_actor;

typedef struct program_tuple {
    struct program_tuple *next;
    qa_application_startup_source source,target;
    qa_application_console_scope origin_scope;
    qa_launch_instance_lease *previous_lease,*candidate_lease;
    application_provider *target_provider;
    uint64_t previous_generation;
} program_tuple;

struct application_startup_program {
    struct application_startup_program *next;
    struct application_startup_program *next_application;
    qa_application *application, *previous_application;
    application_publication *publication;
    const qa_launch_snapshot *previous, *candidate;
    qa_application_startup_source source, target;
    qa_console_program *program;
    program_actor *actors;
    program_tuple *tuples;
    bool retained, restoring;
};

struct application_startup_program_roster {
    application_startup_program *head, *tail;
};

static bool same_scope(qa_application_console_scope a, qa_application_console_scope b)
{ return a.provider == b.provider && a.kind == b.kind && a.seat == b.seat; }


static bool current(void *context,const qa_console *source,const qa_console *target,qa_error *error)
{
    application_startup_program *owner=context;
    qa_application *previous=owner->previous_application,*candidate=owner->application;
    bool selected=owner->restoring ? qa_application_launch(candidate)==owner->candidate :
        owner->publication ? owner->publication->candidate==owner->candidate &&
            owner->publication->previous==owner->previous && !owner->publication->published &&
            !owner->publication->failed_retained : qa_application_startup_candidate(candidate)==owner->candidate;
    if (qa_configuration_current(previous->configuration)!=owner->previous || !selected ||
        source!=previous->console || target!=candidate->console ||
        previous->destroy_requested || candidate->destroy_requested ||
        previous->state==QA_APPLICATION_FAULTED || candidate->state==QA_APPLICATION_FAULTED)
        return application_fail(error,QA_ERROR_ARGUMENT,"Root command programme lost its actual publication or console");
    return true;
}

static bool identity(void *context, qa_console_program_identity kind, uint64_t source,
    uint64_t *out, qa_error *error)
{
    application_startup_program *owner = context;
    if (!source) { *out = 0; return true; }
    /* The retained console keeps its actual declaration/client lifetimes.
     * Genuine Shutdown retirement filters any work whose role ends. */
    for (program_tuple *tuple=owner->tuples; tuple; tuple=tuple->next) {
        if (kind==QA_CONSOLE_PROGRAM_OWNER && source==tuple->source.scope.provider) {
            *out=tuple->target.scope.provider; return true;
        }
        if (source==tuple->source.declaration_owner) {
            *out=tuple->target.declaration_owner; return true;
        }
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

static bool carried_player_seat(const application_startup_program *owner, qa_actor_id actor,
    uint32_t *seat)
{
    if (qa_application_player_seat(owner->previous_application, actor, seat)) return true;
    const application_campaign_travel *campaign = owner->previous_application->campaign_travel;
    const application_player_travel *players = owner->publication
        ? owner->publication->players : campaign ? campaign->players : NULL;
    for (size_t i = 0; players && i < players->count; ++i)
        if (qa_actor_id_equal(players->carry[i].previous_actor, actor)) {
            *seat = players->seats[i].id;
            return true;
        }
    return false;
}

static bool candidate_tuple(application_startup_program *owner,const qa_command_context *context,
    bool *found,qa_error *error)
{
    *found=false;
    for (size_t i=0;i<qa_launch_snapshot_instance_count(owner->candidate);++i) {
        const qa_launch_instance *descriptor=qa_launch_snapshot_instance(owner->candidate,i);
        application_provider *provider=descriptor ? descriptor->state : NULL;
        if (!provider || provider->owner!=context->owner) continue;
        for (size_t index=0;;++index) {
            qa_application_startup_source actual; bool present;
            if (!application_provider_startup_source_at(provider,index,&actual,&present,error)) return false;
            if (!present) break;
            if (actual.console==owner->application->console && actual.descriptor &&
                actual.descriptor->storage==descriptor->storage &&
                qa_cvars_view_identity(actual.cvars)==context->cvar_view &&
                actual.command.owner==context->owner && actual.command.dialect==context->dialect &&
                (actual.command.origin==QA_COMMAND_SERVER || actual.command.seat==context->seat)) {
                *found=true; return true;
            }
        }
    }
    return true;
}

static bool command_context(void *context, const qa_command_context *source,
    qa_command_context *out, qa_error *error)
{
    application_startup_program *owner = context;
    qa_command_context target = *source;
    target.session = owner->target.command.session;
    if (!source->cvar_view || source->cvar_view==owner->source.command.cvar_view)
        target.cvar_view=owner->target.command.cvar_view;
    else {
        program_tuple *tuple=owner->tuples;
        while (tuple && tuple->source.command.cvar_view!=source->cvar_view) tuple=tuple->next;
        if (!tuple) {
            bool candidate=false;
            if (!candidate_tuple(owner,source,&candidate,error)) return false;
            if (!candidate) return application_fail(error,QA_ERROR_ARGUMENT,"Queued command has no actual Source tuple");
        } else if (source->owner!=tuple->source.command.owner ||
            (tuple->source.command.origin!=QA_COMMAND_SERVER && source->seat!=tuple->source.command.seat) ||
            source->dialect!=tuple->source.command.dialect)
            return application_fail(error,QA_ERROR_ARGUMENT,"Queued command lost its actual Source view tuple");
        if (tuple) target.cvar_view=tuple->target.command.cvar_view;
    }
    if (!identity(owner, QA_CONSOLE_PROGRAM_OWNER, source->owner, &target.owner, error) ||
        !identity(owner, QA_CONSOLE_PROGRAM_CLIENT, source->client, &target.client, error)) return false;
    if (source->actor.registry) {
        uint32_t seat;
        if (carried_player_seat(owner, source->actor, &seat)) {
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
    application_startup_program *owner = context;
    *retained = !source->cvar_view || source->cvar_view==owner->source.command.cvar_view;
    for (program_tuple *tuple=owner->tuples; !*retained && tuple; tuple=tuple->next)
        *retained=tuple->source.command.cvar_view==source->cvar_view;
    if (!*retained && !candidate_tuple(owner,source,retained,error)) return false;
    if (*retained && owner->restoring && source->actor.registry) {
        uint32_t seat;
        qa_actor_id carried;
        *retained = carried_player_seat(owner, source->actor, &seat) &&
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
        owner->target.console != owner->application->console)
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
        target != owner->application->console)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command program requires its actual successful publication");
    return true;
}

static bool retained_abort(void *context,bool *restore_original,qa_error *error)
{
    application_startup_program *owner=context;
    const qa_launch_snapshot *current=qa_configuration_current(owner->application->configuration);
    if (current==owner->previous) { *restore_original=true; return true; }
    if (current==owner->candidate) { *restore_original=false; return true; }
    return application_fail(error,QA_ERROR_ARGUMENT,"Root programme lost its actual publication disposition");
}

static void dispose(application_startup_program *owner)
{
    while (owner->actors) {
        program_actor *row = owner->actors;
        owner->actors = row->next; free(row);
    }
    while (owner->tuples) {
        program_tuple *tuple=owner->tuples; owner->tuples=tuple->next;
        qa_launch_instance_lease_release(tuple->previous_lease);
        qa_launch_instance_lease_release(tuple->candidate_lease); free(tuple);
    }
    qa_launch_snapshot_release(owner->previous);
    qa_launch_snapshot_release(owner->candidate);
    application_startup_program **position = &owner->application->startup_program_owners;
    while (*position && *position != owner) position = &(*position)->next_application;
    if (*position) *position = owner->next_application;
    free(owner);
}

static bool tuple_add(application_startup_program *owner,const qa_application_startup_source *source,
    const qa_application_startup_source *target,qa_error *error)
{
    if (source->scope.kind==QA_APPLICATION_CONSOLE_ENGINE && target->scope.kind==QA_APPLICATION_CONSOLE_ENGINE)
        return true;
    if (!source->descriptor || !target->descriptor || !source->cvars || !target->cvars ||
        source->scope.seat!=target->scope.seat)
        return application_fail(error,QA_ERROR_ARGUMENT,"Root programme needs its actual Source descriptor pair");
    uint64_t source_view=source->command.cvar_view?source->command.cvar_view:qa_cvars_view_identity(source->cvars);
    for (program_tuple *tuple=owner->tuples; tuple; tuple=tuple->next)
        if (tuple->source.command.cvar_view==source_view)
            return (same_scope(tuple->target.scope,target->scope) && tuple->target.cvars==target->cvars) ||
                application_fail(error,QA_ERROR_ARGUMENT,"Queued Source view selected another actual target");
    const qa_launch_instance *old=owner->previous ?
        qa_launch_snapshot_find(owner->previous,source->descriptor->selection.instance) : source->descriptor;
    const qa_launch_instance *selected=qa_launch_snapshot_find(owner->candidate,target->descriptor->selection.instance);
    if (!old || !selected || old->storage!=source->descriptor->storage ||
        selected->storage!=target->descriptor->storage || !selected->state)
        return application_fail(error,QA_ERROR_ARGUMENT,"Root programme lost its actual Source descriptors");
    program_tuple *tuple=calloc(1,sizeof(*tuple));
    if (!tuple) return application_fail(error,QA_ERROR_MEMORY,"Retaining queued Source tuple");
    tuple->source=*source; tuple->target=*target;
    tuple->source.command.cvar_view=source_view;
    tuple->target.command.cvar_view=qa_cvars_view_identity(target->cvars);
    tuple->target_provider=selected->state;
    tuple->origin_scope=source->scope; tuple->previous_generation=owner->previous_application->command_generation;
    if (!qa_launch_instance_retain_metadata(old,&tuple->previous_lease,error) ||
        !qa_launch_instance_retain_metadata(selected,&tuple->candidate_lease,error)) {
        qa_launch_instance_lease_release(tuple->previous_lease);
        qa_launch_instance_lease_release(tuple->candidate_lease); free(tuple); return false;
    }
    tuple->source.descriptor=qa_launch_instance_lease_view(tuple->previous_lease);
    tuple->target.descriptor=qa_launch_instance_lease_view(tuple->candidate_lease);
    tuple->next=owner->tuples; owner->tuples=tuple; return true;
}

static bool prepare_program(qa_application *app,qa_application *previous_app,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,
    const qa_application_startup_source *target,application_publication *publication,
    application_startup_program **out,qa_error *error)
{
    if (!app || !previous_app || !candidate || !source || !target || !out || *out ||
        source->console!=previous_app->console || target->console!=app->console)
        return application_fail(error,QA_ERROR_ARGUMENT,"Root programme requires the one physical console pair");
    for (application_startup_program *owner=app->startup_program_owners; owner; owner=owner->next_application)
        if (owner->candidate==candidate && owner->previous_application==previous_app &&
            owner->publication==publication) return tuple_add(owner,source,target,error);
    application_startup_program *owner=calloc(1,sizeof(*owner));
    if (!owner) return application_fail(error,QA_ERROR_MEMORY,"Retaining root command programme");
    owner->application=app; owner->previous_application=previous_app;
    owner->candidate=candidate; owner->previous=qa_configuration_current(previous_app->configuration);
    owner->publication=publication; owner->restoring=app!=previous_app;
    owner->retained=app->console==previous_app->console;
    owner->source=(qa_application_startup_source){.scope={.kind=QA_APPLICATION_CONSOLE_ENGINE},
        .console=previous_app->console,.cvars=previous_app->cvars};
    owner->target=(qa_application_startup_source){.scope={.kind=QA_APPLICATION_CONSOLE_ENGINE},
        .console=app->console,.cvars=app->cvars};
    qa_launch_snapshot_retain(owner->previous); qa_launch_snapshot_retain(candidate);
    bool ok=qa_console_context_read(previous_app->console,&owner->source.command,error) &&
        qa_console_context_read(app->console,&owner->target.command,error) && tuple_add(owner,source,target,error);
    if (!ok) { dispose(owner); return false; }
    owner->next_application=app->startup_program_owners; app->startup_program_owners=owner;
    *out=owner; return true;
}

bool application_startup_program_prepare(qa_application *app, application_publication *publication,
    const qa_application_startup_source *source, const qa_application_startup_source *target,
    application_startup_program **out, qa_error *error)
{ return prepare_program(app, app, publication->candidate, source, target, publication, out, error); }

bool application_startup_program_refresh(application_startup_program *owner,
    const qa_application_startup_source *target,qa_error *error)
{
    if (!owner || !target) return application_fail(error,QA_ERROR_ARGUMENT,"Root programme refresh lacks its actual Source");
    for (program_tuple *tuple=owner->tuples; tuple; tuple=tuple->next)
        if (same_scope(tuple->target.scope,target->scope)) {
            if (!target->descriptor || target->descriptor->storage!=tuple->target.descriptor->storage ||
                target->console!=owner->application->console || target->cvars!=tuple->target.cvars ||
                qa_cvars_view_identity(target->cvars)!=tuple->target.command.cvar_view)
                return application_fail(error,QA_ERROR_ARGUMENT,"Queued Source refresh changed its actual view tuple");
            tuple->target.declaration_owner=target->declaration_owner;
            tuple->target.command=target->command;
            tuple->target.command.cvar_view=qa_cvars_view_identity(target->cvars);
            return true;
        }
    return application_fail(error,QA_ERROR_ARGUMENT,"Root programme refresh lost its selected Source tuple");
}

bool application_startup_program_preflight(application_startup_program *owner,qa_error *error)
{
    if (!owner) return true;
    if (!owner->program) {
        for (program_tuple *tuple=owner->tuples; tuple; tuple=tuple->next) {
            bool found=false;
            for (size_t index=0;;++index) {
                qa_application_startup_source actual;
                if (!application_provider_startup_source_at(tuple->target_provider,index,&actual,&found,error)) return false;
                if (!found) return application_fail(error,QA_ERROR_ARGUMENT,"Root programme lost its actual candidate Source");
                if (same_scope(actual.scope,tuple->target.scope)) {
                    if (!application_startup_program_refresh(owner,&actual,error)) return false;
                    break;
                }
            }
        }
        qa_console_program_resolvers resolve={.context=owner,.fresh_namespace=owner->restoring,
            .context_retained=context_retained,.identity=identity,.command_context=command_context,
            .published_context=published_context,.published_candidate_context=published_candidate_context,
            .current=current,.published=published,.retained_abort=retained_abort};
        owner->program=owner->retained ? qa_console_program_retain(owner->target.console,&resolve,error) :
            qa_console_program_prepare(owner->source.console,owner->target.console,&resolve,error);
        if (!owner->program) return false;
    }
    return qa_console_program_preflight(owner->program,error);
}

bool application_startup_program_seal(application_startup_program *owner, qa_error *error)
{ return !owner || qa_console_program_seal(owner->program, error); }
bool application_startup_program_adopt(application_startup_program **slot, qa_error *error)
{
    application_startup_program *owner = slot ? *slot : NULL;
    if (!owner) return true;
    if (!application_startup_program_queue_ready(owner->application,&owner->source,&owner->target,
        owner->source.command.generation,error)) return false;
    for (program_tuple *tuple=owner->tuples; tuple; tuple=tuple->next) {
        qa_application_startup_source ordinal=tuple->source; ordinal.scope=tuple->origin_scope;
        if (!application_startup_program_queue_ready(owner->application,&ordinal,&tuple->target,
            tuple->previous_generation,error)) return false;
    }
    if (!qa_console_program_adopt(owner->program,error)) return false;
    application_startup_program_queue_publish(owner->application,&owner->source,&owner->target,
        owner->source.command.generation);
    bool q2=false;
    for (program_tuple *tuple=owner->tuples; tuple; tuple=tuple->next) {
        qa_application_startup_source ordinal=tuple->source; ordinal.scope=tuple->origin_scope;
        application_startup_program_queue_publish(owner->application,&ordinal,&tuple->target,tuple->previous_generation);
        q2=q2 || tuple->target.scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME ||
            tuple->target.scope.kind==QA_APPLICATION_CONSOLE_NATIVE_Q2;
    }
    qa_application *application=owner->application; qa_console *console=owner->target.console;
    *slot=NULL; dispose(owner);
    return !q2 || application_players_q2_commands_resume(application,console,error);
}
bool application_startup_program_abort(application_startup_program **slot, qa_error *error)
{
    application_startup_program *owner = slot ? *slot : NULL;
    if (!owner) return true;
    if (owner->program && !qa_console_program_abort(owner->program, error)) return false;
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
    if (!program) return true;
    if ((*out)->tail) (*out)->tail->next = program;
    else (*out)->head = program;
    (*out)->tail = program;
    return true;
}

bool application_startup_program_restore_prepare(qa_application *candidate,qa_application *previous,
    application_startup_program_roster **out,qa_error *error)
{
    if (!candidate || !previous || candidate==previous || !out || *out ||
        previous->operation!=APPLICATION_PERSISTING || candidate->operation!=APPLICATION_IDLE ||
        !qa_application_launch(candidate))
        return application_fail(error,QA_ERROR_ARGUMENT,"Live root commands require the completed restored GAME");
    qa_application_startup_source source={.scope={.kind=QA_APPLICATION_CONSOLE_ENGINE},
        .console=previous->console,.cvars=previous->cvars};
    qa_application_startup_source target={.scope={.kind=QA_APPLICATION_CONSOLE_ENGINE},
        .console=candidate->console,.cvars=candidate->cvars};
    if (!roster_prepare(candidate,previous,qa_application_launch(candidate),&source,&target,NULL,out,error)) return false;
    const qa_launch_snapshot *old=qa_application_launch(previous),*next=qa_application_launch(candidate);
    for (size_t i=0;i<qa_launch_snapshot_instance_count(old);++i) {
        const qa_launch_instance *descriptor=qa_launch_snapshot_instance(old,i);
        const qa_launch_instance *selected=qa_launch_snapshot_find(next,descriptor->selection.instance);
        application_provider *old_provider=descriptor->state,*new_provider=selected?selected->state:NULL;
        if (!old_provider || !new_provider) continue;
        for (size_t index=0;;++index) {
            bool found;
            if (!application_provider_startup_source_at(old_provider,index,&source,&found,error)) return false;
            if (!found) break;
            for (size_t ordinal=0;;++ordinal) {
                if (!application_provider_startup_source_at(new_provider,ordinal,&target,&found,error)) return false;
                if (!found) break;
                if (source.scope.kind==target.scope.kind && source.scope.seat==target.scope.seat) {
                    if (!tuple_add((*out)->head,&source,&target,error)) return false;
                    break;
                }
            }
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
    qa_application_startup_source engine={.scope={.kind=QA_APPLICATION_CONSOLE_ENGINE},
        .console=app->console,.cvars=app->cvars};
    if (!roster_prepare(app,app,publication->candidate,&engine,&engine,publication,out,error)) return false;
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

bool application_startup_program_publication_preflight(application_startup_program_roster *owner,qa_error *error)
{
    if (!owner) return true;
    for (application_startup_program *program=owner->head; program; program=program->next)
        if (!application_startup_program_preflight(program,error)) return false;
    return true;
}

bool application_startup_program_publication_seal(application_startup_program_roster *owner, qa_error *error)
{
    if (!application_startup_program_publication_preflight(owner,error)) return false;
    if (!owner) return true;
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
