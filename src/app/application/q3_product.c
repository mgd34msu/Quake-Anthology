#include "q3_product.h"
#include "internal.h"
#include "startup_flow.h"
#include "qa/catalog_save.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_q3_factory.h"
#include "qa/application_client_prepare.h"

#include <string.h>
#include <stdlib.h>

typedef struct application_startup_row {
    char *command, *name, *value;
    char *queued_instance;
    uint32_t queued_kind, queued_seat;
    uint64_t queued_generation;
    bool consumed, completed, pending, shared_seeded, safe_command;
} application_startup_row;
struct application_startup {
    application_startup_row *rows;
    size_t count;
};
static bool ascii_equal(const char *,const char *);

static char *startup_copy(const char *value, qa_error *error)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "Retaining original startup operands"); return NULL; }
    memcpy(copy, value, length + 1);
    return copy;
}

void application_startup_dispose(qa_application *app)
{
    if (!app || !app->startup) return;
    for (size_t i = 0; i < app->startup->count; ++i) {
        free(app->startup->rows[i].command);
        free(app->startup->rows[i].name);
        free(app->startup->rows[i].value);
        free(app->startup->rows[i].queued_instance);
    }
    free(app->startup->rows);
    free(app->startup);
    app->startup = NULL;
}

bool application_startup_create(qa_application *app, const char *const *commands,
    size_t count, qa_error *error)
{
    if (!app || app->startup || (count && !commands) || count > SIZE_MAX / sizeof(application_startup_row))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup continuation requires its original command rows");
    app->startup = calloc(1, sizeof(*app->startup));
    if (!app->startup) return application_fail(error, QA_ERROR_MEMORY, "Retaining startup continuation");
    app->startup->rows = count ? calloc(count, sizeof(*app->startup->rows)) : NULL;
    if (count && !app->startup->rows) { application_startup_dispose(app); return application_fail(error, QA_ERROR_MEMORY, "Retaining startup command ordinals"); }
    app->startup->count = count;
    for (size_t i = 0; i < count; ++i) {
        const char *text = commands[i];
        if (!text || strpbrk(text, "\r\n") || qa_command_separator(text, strlen(text), QA_CONSOLE_Q3) != strlen(text)) {
            application_startup_dispose(app);
            return application_fail(error, QA_ERROR_ARGUMENT, "Startup requires one source command per original row");
        }
        qa_command_tokens tokens = {0};
        bool okay = qa_command_tokenize(text, QA_CONSOLE_Q3, false, &tokens, error);
        app->startup->rows[i].command = startup_copy(text, error);
        okay = okay && app->startup->rows[i].command;
        if (okay && tokens.count) app->startup->rows[i].safe_command=
            ascii_equal(tokens.values[0],"safe") || ascii_equal(tokens.values[0],"cvar_restart");
        if (okay && tokens.count && !strcmp(tokens.values[0], "set")) {
            application_startup_row *row = &app->startup->rows[i];
            row->name = startup_copy(tokens.count > 1 ? tokens.values[1] : "", error);
            row->value = startup_copy(tokens.count > 2 ? tokens.values[2] : "", error);
            okay = row->name && row->value;
        }
        qa_command_tokens_free(&tokens);
        if (!okay) { application_startup_dispose(app); return false; }
    }
    return true;
}

bool application_startup_clone(qa_application *app, const qa_application *current,
    qa_error *error)
{
    if (!app || !current || app == current || app->startup)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup carry requires its current unit and fresh owner");
    if (!current->startup) return true;
    app->startup = calloc(1, sizeof(*app->startup));
    if (!app->startup) return application_fail(error, QA_ERROR_MEMORY, "Retaining current unit startup");
    size_t count = current->startup->count;
    app->startup->rows = count ? calloc(count, sizeof(*app->startup->rows)) : NULL;
    if (count && !app->startup->rows) {
        application_startup_dispose(app);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining current unit startup rows");
    }
    app->startup->count = count;
    for (size_t i = 0; i < app->startup->count; ++i) {
        const application_startup_row *source = current->startup->rows + i;
        application_startup_row *row = app->startup->rows + i;
        *row = *source;
        row->command = row->name = row->value = row->queued_instance = NULL;
        const char *values[] = {source->command, source->name, source->value, source->queued_instance};
        char **outputs[] = {&row->command, &row->name, &row->value, &row->queued_instance};
        for (size_t j = 0; j < sizeof(values) / sizeof(*values); ++j)
            if (values[j] && !(*outputs[j] = startup_copy(values[j], error))) {
                application_startup_dispose(app);
                return false;
            }
    }
    return true;
}

static const char *const variables[] = {
    "com_prereleaseDemo", "com_prereleaseTeamArenaDemo", "fs_restrict"
};

/* FS_SetRestrictions, id Software files.c. GPL-2.0-or-later. */
static const uint8_t scrambled_product_id[] = {
    220,129,255,108,244,163,171,55,133,65,199,36,140,222,53,99,
    65,171,175,232,236,193,210,250,169,104,231,231,21,201,170,208,
    135,175,130,136,85,215,71,23,96,32,96,83,44,240,219,138,
    184,215,73,27,196,247,55,139,148,68,78,203,213,238,139,23,
    45,205,118,186,236,230,231,107,212,1,10,98,30,20,116,180,
    216,248,166,35,45,22,215,229,35,116,250,167,117,3,57,55,
    201,229,218,222,128,12,141,149,32,110,168,215,184,53,31,147,
    62,12,138,67,132,54,125,6,221,148,140,4,21,44,198,3,
    126,12,100,236,61,42,44,251,15,135,14,134,89,92,177,246,
    152,106,124,78,118,80,28,42
};

static bool ascii_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}

static bool startup_set(qa_cvars *actual, const char *name, const char *value, qa_error *error)
{
    if (!qa_cvars_set(actual, name, value, true, error)) return false;
    if (qa_cvars_dialect(actual) != QA_CONSOLE_Q3) return true;
    const qa_cvar_view *current=qa_cvars_find(actual,name);
    if (!current) return application_fail(error,QA_ERROR_NOT_FOUND,"Startup has no admitted physical cvar");
    uint64_t owner=current->owner;
    /* Com_StartupVariable obtains the actual row with an empty default and
     * marks USER_CREATED without changing its declaration owner. */
    return qa_cvars_register(actual,name,"",0,owner,NULL,error) &&
        qa_cvars_add_flags(actual,name,QA_CVAR_USER_CREATED,error);
}

static bool startup_variable_pending(const application_startup_row *row)
{ return row->name && !row->consumed && !row->completed && !row->pending; }

bool application_startup_seed_root(qa_application *app, qa_console *console,
    const qa_command_context *command, qa_error *error)
{
    if (!app || !console || console != app->console || !command || command->owner ||
        command->cvar_view != qa_cvars_view_identity(app->cvars) ||
        !qa_application_command_context_active(app, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup variables lost their actual ENGINE constructor");
    for (size_t i = 0; app->startup && i < app->startup->count; ++i) {
        const application_startup_row *row = app->startup->rows + i;
        if (startup_variable_pending(row) &&
            !qa_console_cvar_startup_set(console, command, row->name, row->value, error)) return false;
    }
    return true;
}

bool application_startup_seed_engine(qa_application *app, qa_product_id selected, qa_error *error)
{
    const qa_product *product = app ? qa_catalog_product(app->catalog, selected) : NULL;
    if (!product || product->family != QA_GAME_Q3 || !app->startup) return true;
    /* Only the fresh initial physical Q3 selection seeds its genuinely shared
     * ENGINE variable. Later candidate sources never mutate that owner. */
    for (size_t i = 0; i < app->startup->count; ++i) {
        application_startup_row *row = &app->startup->rows[i];
        if (!row->name || !ascii_equal(row->name, "sv_cheats")) continue;
        if (!startup_set(app->cvars, "sv_cheats", row->value, error)) return false;
        row->shared_seeded = true;
    }
    return true;
}

static bool startup_primary(const application_provider *provider)
{
    qa_application *app=provider->application;
    /* Fresh constructors run before the candidate roster attaches. Resolve
     * its actual selected identity and owner pointer without the live-service
     * lookup's attached requirement. Consumption still waits for publication. */
    const qa_launch_snapshot *snapshot = app->routing_snapshot
        ? app->routing_snapshot : qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    const qa_launch_binding *binding = choices ? qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "") : NULL;
    application_provider *const *providers = app->routing_providers
        ? app->routing_providers : app->providers;
    size_t provider_count = app->routing_providers
        ? app->routing_provider_count : app->provider_count;
    if (binding && provider->launch && !strcmp(binding->instance, provider->launch->selection.instance))
        for (size_t i = 0; i < provider_count; ++i)
            if (providers[i] == provider) return true;
    return false;
}
bool application_startup_seed_source(application_provider *provider, qa_cvars *actual, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !provider->product || !actual || !qa_cvars_same_store(actual, app->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup requires the canonical Source view");
    if (!startup_primary(provider)) return true;
    application_publication *publication = app->startup_publication;
    const qa_launch_snapshot *previous = publication ? publication->previous : qa_application_launch(app);
    if (previous && (!publication || !publication->sources_retired) &&
        app->operation != APPLICATION_PERSISTING) return true;
    if (!qa_cvars_select_dialect(actual, qa_cvars_dialect(actual), error)) return false;
    if (app->operation == APPLICATION_PERSISTING) return true;
    const qa_launch_snapshot *snapshot = app->routing_snapshot
        ? app->routing_snapshot : qa_application_launch(app);
    if (!application_provider_seed_cvars(provider, actual,
            qa_launch_snapshot_choices(snapshot), error)) return false;
    if (!app->startup || app->startup_hooks || provider->product->family == QA_GAME_Q1) return true;
    for (size_t i = 0; i < app->startup->count; ++i) {
        application_startup_row *row = &app->startup->rows[i];
        if (!startup_variable_pending(row)) continue;
        if (!startup_set(actual, row->name, row->value, error)) return false;
        if (!app->startup_hooks) {
            if (app->q3_product_preparing) row->pending = true;
            else row->consumed = true;
        }
    }
    return true;
}

bool application_startup_seed_console(application_provider *provider,const qa_application_startup_source *source,
    const qa_command_context *command,qa_error *error)
{
    qa_application *app=provider?provider->application:NULL;
    qa_console *console=source?source->console:NULL;
    if (!app || !provider->product || !console || !command || command->owner!=provider->owner ||
        !source->descriptor || source->scope.provider!=provider->owner ||
        !qa_cvars_same_store(qa_console_cvars(console),source->cvars) ||
        command->cvar_view!=qa_cvars_view_identity(source->cvars) || !qa_application_command_context_active(app,command))
        return application_fail(error,QA_ERROR_ARGUMENT,"Startup replay requires its actual routed source console");
    bool client=source->scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME || source->scope.kind==QA_APPLICATION_CONSOLE_Q3_UI;
    bool game=source->scope.kind==QA_APPLICATION_CONSOLE_QC || source->scope.kind==QA_APPLICATION_CONSOLE_NATIVE_Q2 ||
        source->scope.kind==QA_APPLICATION_CONSOLE_Q1_GAME || source->scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME ||
        source->scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME;
    if (!client && !game)
        return application_fail(error,QA_ERROR_ARGUMENT,"Startup replay has no physical GAME or CLIENT scope");
    if (client) {
        qa_application_startup_source actual;
        if (command->origin!=QA_COMMAND_SEAT || command->dialect!=QA_CONSOLE_Q3 || command->seat!=source->scope.seat ||
            !qa_application_q3_client_configuration_read(app,source->scope.provider,source->scope.kind == QA_APPLICATION_CONSOLE_Q3_UI ? QA_QVM_UI : QA_QVM_CGAME,source->scope.seat,&actual,error) ||
            actual.console!=console || actual.cvars!=source->cvars || actual.scope.kind!=source->scope.kind ||
            actual.descriptor->storage!=source->descriptor->storage)
            return application_fail(error,QA_ERROR_ARGUMENT,"Startup CLIENT replay lost its actual physical configuration slot");
    }
    if (!app->startup || app->operation==APPLICATION_PERSISTING) return true;
    bool primary=!client && startup_primary(provider);
    if (app->startup_hooks && app->startup_hooks->startup_source) {
        const qa_launch_snapshot *snapshot=app->routing_snapshot?app->routing_snapshot:qa_application_launch(app);
        if (!app->startup_hooks->startup_source(app->startup_hooks->context,app,snapshot,source,&primary,error)) return false;
    }
    for (size_t i=0;i<app->startup->count;++i) {
        application_startup_row *row=app->startup->rows+i;
        if (!startup_variable_pending(row)) continue;
        bool shared=(client || provider->product->family==QA_GAME_Q3) && ascii_equal(row->name,"sv_cheats");
        if (!shared) {
            const qa_command_context *recipient=command;
            qa_command_context game_command;
            if (!client) {
                const qa_cvar_view *declared=NULL;
                if (!qa_console_cvar_read(console,command,row->name,&declared,error)) return false;
                /* External GAME declarations arrive at module Init. An
                 * undeclared startup row belongs to that physical GAME;
                 * declared client/input rows keep the phase's seat route. */
                if (!declared || !declared->declared) {
                    if (source->command.origin!=QA_COMMAND_SERVER || source->command.owner!=provider->owner ||
                        !qa_application_capture_command_context(app,&source->command,&game_command,error) ||
                        qa_console_cvar_owner(console,&game_command,row->name)!=source->cvars)
                        return application_fail(error,QA_ERROR_ARGUMENT,"Undeclared startup variable lost its actual GAME recipient");
                    recipient=&game_command;
                }
            }
            if (!qa_console_cvar_startup_set(console,recipient,row->name,row->value,error)) return false;
        }
        if (primary && (!shared || row->shared_seeded)) {
            if (app->q3_product_preparing) row->pending=true; else row->consumed=true;
        }
    }
    return true;
}

bool qa_application_startup_command_seeded(const qa_application *app, size_t ordinal)
{
    return app && app->startup && ordinal < app->startup->count && app->startup->rows[ordinal].consumed;
}
bool qa_application_client_prepare_startup_ready(const qa_application_client_preparation *preparation)
{
    if (!qa_application_client_prepare_phase_is(preparation,QA_CLIENT_PREPARE_RESOURCES) ||
        !qa_application_client_prepare_startup_current(preparation))
        return false;
    qa_application *app=qa_application_client_prepare_application(preparation);
    const qa_application_client_source *source=qa_application_client_prepare_source(preparation);
    if (!source) return false;
    qa_console_dialect dialect=source->context.command.dialect;
    if (dialect!=QA_CONSOLE_Q2 && dialect!=QA_CONSOLE_Q2_RERELEASE && dialect!=QA_CONSOLE_Q3) return true;
    for (size_t i=0;app->startup && i<app->startup->count;++i) {
        application_startup_row *row=app->startup->rows+i;
        if ((dialect==QA_CONSOLE_Q3?row->safe_command:row->name!=NULL) && row->queued_instance)
            return false;
    }
    return true;
}
bool qa_application_client_prepare_safe_mode(const qa_application_client_preparation *preparation,
    bool *safe,qa_error *error)
{
    const qa_application_client_source *source=qa_application_client_prepare_source(preparation);
    if (!safe || !source || !qa_application_client_prepare_phase_is(preparation,QA_CLIENT_PREPARE_CONFIGURATION))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT safe mode requires its actual cfg owner");
    *safe=false;
    if (source->context.command.dialect!=QA_CONSOLE_Q3) return true;
    if (!qa_application_client_prepare_startup_current(preparation))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT safe mode lost its genuine primary startup request");
    qa_application *app=qa_application_client_prepare_application(preparation);
    for (size_t i=0;app->startup && i<app->startup->count;++i) {
        const application_startup_row *row=app->startup->rows+i;
        if (!row->safe_command) continue;
        if (row->queued_instance)
            return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT safe mode cannot consume an admitted command buffer");
        *safe=true; break;
    }
    return true;
}
void qa_application_client_prepare_startup_publish(qa_application_client_preparation *preparation)
{
    qa_application *app=qa_application_client_prepare_application(preparation);
    const qa_application_client_source *source=qa_application_client_prepare_source(preparation);
    if (source->context.command.dialect==QA_CONSOLE_Q3) {
        for (size_t i=0;app->startup && i<app->startup->count;++i)
            if (app->startup->rows[i].safe_command) { app->startup->rows[i].consumed=true; break; }
        return;
    }
    if (source->context.command.dialect!=QA_CONSOLE_Q2 && source->context.command.dialect!=QA_CONSOLE_Q2_RERELEASE) return;
    /* Q2 early variables are the commands excluded from its late programme.
     * Q1/QW stuffed commands and Q3 late sets keep their original ordinals. */
    for (size_t i=0;app->startup && i<app->startup->count;++i)
        if (app->startup->rows[i].name) app->startup->rows[i].consumed=true;
}
bool qa_application_startup_q3_safe_mode(qa_application *app,const qa_launch_instance *selected,
    const qa_application_startup_source *source,bool *safe,qa_error *error)
{
    const qa_launch_snapshot *snapshot=app?app->routing_snapshot:NULL;
    if (!snapshot && app) snapshot=qa_application_startup_candidate(app);
    if (!snapshot && app) snapshot=qa_application_launch(app);
    const qa_launch_instance *actual=selected && snapshot ?
        qa_launch_snapshot_find(snapshot,selected->selection.instance):NULL;
    application_provider *provider=actual?actual->state:NULL;
    const qa_product *product=selected?qa_catalog_product(qa_launch_instance_catalog(selected),selected->selection.product):NULL;
    bool qualified=false;
    if (app && selected && actual && source && safe && provider && product && product->family==QA_GAME_Q3 &&
        actual->storage==selected->storage && source->descriptor && source->descriptor->storage==selected->storage &&
        source->console==app->console && source->cvars && source->scope.provider==provider->owner &&
        source->command.cvar_view==qa_cvars_view_identity(source->cvars) &&
        qa_cvars_dialect(source->cvars)==QA_CONSOLE_Q3) {
        for (size_t index=0;;++index) {
            qa_application_startup_source candidate;
            bool present;
            if (!application_provider_startup_source_at(provider,index,&candidate,&present,error)) return false;
            if (!present) break;
            if (candidate.descriptor->storage==source->descriptor->storage && candidate.console==source->console &&
                candidate.cvars==source->cvars && candidate.scope.kind==source->scope.kind &&
                candidate.scope.seat==source->scope.seat && candidate.command.cvar_view==source->command.cvar_view) {
                qualified=true; break;
            }
        }
    }
    if (!qualified)
        return application_fail(error,QA_ERROR_ARGUMENT,"Safe mode requires its actual selected Q3 source console");
    *safe=false;
    for (size_t i=0;app->startup && i<app->startup->count;++i) {
        application_startup_row *row=app->startup->rows+i;
        if (!row->safe_command) continue;
        if (row->queued_instance)
            return application_fail(error,QA_ERROR_ARGUMENT,"Safe mode cannot consume an already admitted source buffer");
        *safe=true;
        if (!row->completed && !row->consumed) {
            if (app->q3_product_preparing) row->pending=true;
            else row->consumed=true;
        }
        break;
    }
    return true;
}
size_t qa_application_startup_command_count(const qa_application *app)
{ return app && app->startup ? app->startup->count : 0; }
const char *qa_application_startup_command(const qa_application *app, size_t ordinal)
{ return app && app->startup && ordinal < app->startup->count ? app->startup->rows[ordinal].command : NULL; }
bool qa_application_startup_command_pending(const qa_application *app, size_t ordinal)
{
    return app && app->startup && ordinal < app->startup->count &&
        !app->startup->rows[ordinal].consumed && !app->startup->rows[ordinal].completed;
}
bool qa_application_startup_command_complete(qa_application *app, size_t ordinal, qa_error *error)
{
    if (!app || !app->startup || ordinal >= app->startup->count || app->q3_product_preparing)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup completion requires its actual published ordinal");
    qa_console *console = NULL;
    if (!qa_application_startup_command_queued_console(app, ordinal, &console, error)) return false;
    if (!console || !qa_console_idle(console) || qa_console_pending(console) || qa_console_drain_yielded(console))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup command has not returned from its actual source buffer");
    free(app->startup->rows[ordinal].queued_instance);
    app->startup->rows[ordinal].queued_instance = NULL;
    app->startup->rows[ordinal].queued_kind = app->startup->rows[ordinal].queued_seat = 0;
    app->startup->rows[ordinal].queued_generation = 0;
    app->startup->rows[ordinal].completed = true;
    return true;
}

bool qa_application_startup_command_queued_console(qa_application *app, size_t ordinal,
    qa_console **out, qa_error *error)
{
    if (!app || !app->startup || ordinal >= app->startup->count || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup buffer observation requires its retained ordinal");
    *out = NULL;
    application_startup_row *row = &app->startup->rows[ordinal];
    if (!row->queued_instance) return true;
    if (row->queued_generation != app->command_generation)
        return application_fail(error, QA_ERROR_FORMAT, "Queued startup command belongs to another source publication");
    for (size_t i = 0;; ++i) {
        qa_application_startup_source source;
        bool present;
        if (!qa_application_console_source_at(app,i,&source,&present,error)) return false;
        if (!present) break;
        if ((uint32_t)source.scope.kind != row->queued_kind || source.scope.seat != row->queued_seat) continue;
        const char *instance = source.descriptor ? source.descriptor->selection.instance : "";
        if (!strcmp(instance,row->queued_instance)) { *out=source.console; return true; }
    }
    return application_fail(error, QA_ERROR_FORMAT, "Queued startup command lost its actual source console");
}

bool qa_application_startup_command_queue(qa_application *app, size_t ordinal,
    qa_console *console, const qa_command_context *context, qa_error *error)
{
    if (!app || !app->startup || ordinal >= app->startup->count || app->q3_product_preparing ||
        !qa_application_startup_command_pending(app, ordinal) || !console || !context ||
        context->origin == QA_COMMAND_REMOTE || !qa_console_idle(console))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup queue requires its actual idle published source");
    application_startup_row *row = &app->startup->rows[ordinal];
    if (row->queued_instance) return application_fail(error, QA_ERROR_ARGUMENT, "Startup ordinal is already queued");
    for (size_t i = 0; i < app->startup->count; ++i)
        if (app->startup->rows[i].queued_instance || (i < ordinal && qa_application_startup_command_pending(app, i)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Startup source rows must retain their original order");
    qa_application_startup_source source = {0};
    bool matched = false;
    for (size_t i = 0;; ++i) {
        bool present;
        if (!qa_application_console_source_at(app,i,&source,&present,error)) return false;
        if (!present) break;
        if (source.console==console && source.command.cvar_view==context->cvar_view &&
            source.command.owner==context->owner && source.command.seat==context->seat &&
            source.command.dialect==context->dialect) { matched=true; break; }
    }
    if (!matched) return application_fail(error, QA_ERROR_ARGUMENT, "Startup queue lost its actual Source view");
    qa_application_console_scope scope=source.scope;
    const char *instance=source.descriptor?source.descriptor->selection.instance:"";
    char *identity = startup_copy(instance, error);
    size_t length = strlen(row->command);
    char *text = length <= SIZE_MAX - 2 ? malloc(length + 2) : NULL;
    if (!identity || !text) {
        free(identity); free(text);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining actual queued startup command");
    }
    memcpy(text, row->command, length); text[length] = '\n'; text[length + 1] = 0;
    bool okay = qa_console_append(console, context, text, error);
    free(text);
    if (!okay) { free(identity); return false; }
    row->queued_instance = identity; row->queued_kind = scope.kind; row->queued_seat = scope.seat;
    row->queued_generation = app->command_generation;
    return true;
}

bool qa_application_startup_console_queued(const qa_application *app, const qa_console *console)
{
    if (!app || !app->startup || console!=app->console) return false;
    for (size_t i=0;i<app->startup->count;++i)
        if (app->startup->rows[i].queued_instance &&
            app->startup->rows[i].queued_generation==app->command_generation) return true;
    return false;
}
bool application_startup_program_queue_ready(qa_application *app,const qa_application_startup_source *previous,
    const qa_application_startup_source *target,uint64_t generation,qa_error *error)
{
    bool engine=previous && target && previous->scope.kind==QA_APPLICATION_CONSOLE_ENGINE &&
        target->scope.kind==QA_APPLICATION_CONSOLE_ENGINE;
    if (!app || !previous || !target || !target->console ||
        previous->scope.kind!=target->scope.kind || previous->scope.seat!=target->scope.seat ||
        (!engine && (!previous->descriptor || !target->descriptor ||
            strcmp(previous->descriptor->selection.instance,target->descriptor->selection.instance))))
        return application_fail(error,QA_ERROR_ARGUMENT,"Startup ordinal adoption requires its actual compatible source pair");
    const qa_launch_snapshot *published=qa_application_launch(app);
    const qa_launch_instance *selected=engine?NULL:qa_launch_snapshot_find(published,target->descriptor->selection.instance);
    bool matched=false;
    for (size_t i=0;;++i) {
        qa_application_startup_source actual;
        bool present;
        if (!qa_application_console_source_at(app,i,&actual,&present,error)) return false;
        if (!present) break;
        if (actual.console==target->console && actual.cvars==target->cvars &&
            actual.command.cvar_view==target->command.cvar_view &&
            actual.scope.provider==target->scope.provider && actual.scope.kind==target->scope.kind &&
            actual.scope.seat==target->scope.seat) { matched=true; break; }
    }
    if ((!engine && (!selected || selected->storage!=target->descriptor->storage || selected->state!=target->descriptor->state)) || !matched)
        return application_fail(error,QA_ERROR_ARGUMENT,"Startup ordinal target has not actually published");
    const char *instance=engine?"":previous->descriptor->selection.instance;
    for (size_t i=0;app->startup && i<app->startup->count;++i) {
        const application_startup_row *row=app->startup->rows+i;
        if (row->queued_instance && row->queued_kind==(uint32_t)previous->scope.kind &&
            row->queued_seat==previous->scope.seat && !strcmp(row->queued_instance,instance) &&
            row->queued_generation!=generation)
            return application_fail(error,QA_ERROR_FORMAT,"Startup ordinal lost its retained source program generation");
    }
    return true;
}
void application_startup_program_queue_publish(qa_application *app,const qa_application_startup_source *previous,
    const qa_application_startup_source *target,uint64_t generation)
{
    (void)target;
    const char *instance=previous->scope.kind==QA_APPLICATION_CONSOLE_ENGINE?"":previous->descriptor->selection.instance;
    for (size_t i=0;app->startup && i<app->startup->count;++i) {
        application_startup_row *row=app->startup->rows+i;
        if (row->queued_instance && row->queued_generation==generation &&
            row->queued_kind==(uint32_t)previous->scope.kind && row->queued_seat==previous->scope.seat &&
            !strcmp(row->queued_instance,instance)) row->queued_generation=app->command_generation;
    }
}

bool application_startup_fields(qa_source_save_io *io, qa_application *app)
{
    if (!io || !app || (io->direction == QA_SOURCE_SAVE_READ && app->startup)) return false;
    size_t count = app->startup ? app->startup->count : 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 11 : SIZE_MAX;
    if (maximum > SIZE_MAX / sizeof(application_startup_row)) maximum = SIZE_MAX / sizeof(application_startup_row);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        app->startup = calloc(1, sizeof(*app->startup));
        if (!app->startup) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring startup continuation");
        app->startup->rows = count ? calloc(count, sizeof(*app->startup->rows)) : NULL;
        if (count && !app->startup->rows) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring startup ordinals");
        app->startup->count = count;
    }
    for (size_t i = 0; i < count; ++i) {
        application_startup_row *row = &app->startup->rows[i];
        size_t length = row->command ? strlen(row->command) : 0;
        maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1;
        if (!qa_source_save_count(io, &length, maximum) || length == SIZE_MAX) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            row->command = malloc(length + 1);
            if (!row->command) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring genuine startup command");
            row->command[length] = 0;
        }
        if (!qa_source_save_bytes(io, row->command, length) ||
            !qa_source_save_bool(io, &row->consumed) || !qa_source_save_bool(io, &row->completed) ||
            !qa_source_save_bool(io, &row->shared_seeded)) return false;
        bool queued = row->queued_instance != NULL;
        if (!qa_source_save_bool(io, &queued)) return false;
        if (queued) {
            size_t extent = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(row->queued_instance) : 0;
            size_t remaining = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1;
            if (!qa_source_save_u64(io, &row->queued_generation) || !row->queued_generation ||
                !qa_source_save_u32(io, &row->queued_kind) || row->queued_kind > QA_APPLICATION_CONSOLE_CLIENT ||
                !qa_source_save_u32(io, &row->queued_seat) || !qa_source_save_count(io, &extent, remaining) || extent == SIZE_MAX) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) {
                row->queued_instance = malloc(extent + 1);
                if (!row->queued_instance) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring startup source buffer identity");
                row->queued_instance[extent] = 0;
            }
            if (!qa_source_save_bytes(io, row->queued_instance, extent) || memchr(row->queued_instance, 0, extent) ||
                (row->queued_kind == QA_APPLICATION_CONSOLE_ENGINE ? extent || row->queued_seat : !extent) ||
                (row->queued_seat && row->queued_kind != QA_APPLICATION_CONSOLE_Q3_CGAME &&
                    row->queued_kind != QA_APPLICATION_CONSOLE_Q3_UI && row->queued_kind != QA_APPLICATION_CONSOLE_CLIENT) ||
                row->consumed || row->completed)
                return application_fail(io->error, QA_ERROR_FORMAT, "Queued startup ordinal leaves its real source scope");
            for (size_t j = 0; j < i; ++j)
                if (app->startup->rows[j].queued_instance || qa_application_startup_command_pending(app, j))
                    return application_fail(io->error, QA_ERROR_FORMAT, "Saved startup buffer leaves original command order");
            if (io->direction == QA_SOURCE_SAVE_WRITE) {
                qa_console *console = NULL;
                if (!qa_application_startup_command_queued_console(app, i, &console, io->error) || !console) return false;
            }
        }
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (memchr(row->command, 0, length) || strpbrk(row->command, "\r\n") ||
                qa_command_separator(row->command, length, QA_CONSOLE_Q3) != length)
                return application_fail(io->error, QA_ERROR_FORMAT, "Saved startup command leaves its source text domain");
            qa_command_tokens tokens = {0};
            bool okay = qa_command_tokenize(row->command, QA_CONSOLE_Q3, false, &tokens, io->error);
            if (okay && tokens.count) row->safe_command=ascii_equal(tokens.values[0],"safe") ||
                ascii_equal(tokens.values[0],"cvar_restart");
            if (okay && tokens.count && !strcmp(tokens.values[0], "set")) {
                row->name = startup_copy(tokens.count > 1 ? tokens.values[1] : "", io->error);
                row->value = startup_copy(tokens.count > 2 ? tokens.values[2] : "", io->error);
                okay = row->name && row->value;
            }
            qa_command_tokens_free(&tokens);
            if (!okay || (row->consumed && !row->name && !row->safe_command) ||
                (row->shared_seeded && (!row->name || !ascii_equal(row->name, "sv_cheats"))))
                return application_fail(io->error, QA_ERROR_FORMAT, "Saved startup consumption differs from its original command");
            if (row->consumed && row->safe_command)
                for (size_t j=0;j<i;++j) if (app->startup->rows[j].safe_command)
                    return application_fail(io->error,QA_ERROR_FORMAT,"Safe mode consumed another original startup ordinal");
        } else if (row->pending) {
            return application_fail(io->error, QA_ERROR_ARGUMENT, "Startup capture cannot expose a preparing source row");
        }
    }
    return true;
}

static bool valid_policy(const qa_q3_product_policy *policy)
{
    return policy &&
        (!policy->filesystem_restricted || policy->restriction_resolved) &&
        (!(policy->prerelease_demo || policy->fs_restrict) ||
            (policy->restriction_resolved && policy->filesystem_restricted));
}

bool application_q3_product_initial(qa_cvars *cvars, const char *const *commands,
    size_t count, qa_q3_product_policy *out, qa_error *error)
{
    if (!cvars || !out || (count && !commands) || qa_cvars_dialect(cvars) != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 initial policy requires actual startup commands and registry");
    for (size_t i = 0; i < count; ++i) {
        const char *text = commands[i];
        if (!text || strpbrk(text, "\r\n") ||
            qa_command_separator(text, strlen(text), QA_CONSOLE_Q3) != strlen(text))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 startup policy requires one source command per row");
        qa_command_tokens tokens = {0};
        if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &tokens, error)) return false;
        bool okay = true;
        if (tokens.count > 1 && !strcmp(tokens.values[0], "set"))
            for (size_t j = 0; j < sizeof(variables) / sizeof(*variables); ++j)
                if (ascii_equal(tokens.values[1], variables[j])) {
                    okay = startup_set(cvars, variables[j], tokens.count > 2 ? tokens.values[2] : "", error);
                    break;
                }
        qa_command_tokens_free(&tokens);
        if (!okay) return false;
    }
    bool flags[3];
    for (size_t i = 0; i < sizeof(variables) / sizeof(*variables); ++i) {
        if (!qa_cvars_register(cvars, variables[i], "0", QA_CVAR_INIT, 0, NULL, error)) return false;
        flags[i] = qa_cvars_find(cvars, variables[i])->integer != 0;
    }
    bool restricted = flags[0] || flags[2];
    *out = (qa_q3_product_policy){.prerelease_demo = flags[0],
        .prerelease_team_arena_demo = flags[1], .fs_restrict = flags[2],
        .restriction_resolved = restricted, .filesystem_restricted = restricted};
    return true;
}

static bool register_policy_values(const qa_q3_product_policy *policy,
    qa_cvars *cvars, uint64_t owner, bool restricted, qa_error *error)
{
    if (!valid_policy(policy) || !cvars || qa_cvars_dialect(cvars) != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source registry requires retained initial policy");
    bool flags[] = {policy->prerelease_demo, policy->prerelease_team_arena_demo, restricted};
    for (size_t i = 0; i < sizeof(variables) / sizeof(*variables); ++i)
        if (!qa_cvars_set(cvars, variables[i], flags[i] ? "1" : "0", true, error) ||
            !qa_cvars_register(cvars, variables[i], "0", QA_CVAR_INIT, owner, NULL, error))
            return false;
    return true;
}

bool qa_q3_product_policy_register_source(const qa_q3_product_policy *policy,
    qa_cvars *cvars, uint64_t owner, qa_error *error)
{
    if (!valid_policy(policy) || !policy->restriction_resolved)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source requires resolved retained media policy");
    return register_policy_values(policy, cvars, owner, policy->filesystem_restricted, error);
}

bool application_q3_product_register_source(const qa_q3_product_policy *policy,
    qa_cvars *cvars, uint64_t owner, qa_error *error)
{
    return qa_q3_product_policy_register_source(policy, cvars, owner, error);
}

const qa_q3_product_policy *application_q3_product_source_policy(const qa_application *app)
{
    return app ? (app->q3_product_preparing ? app->q3_product_preparing : &app->q3_product) : NULL;
}

bool application_q3_product_import(qa_cvars *cvars, const qa_q3_product_policy *saved,
    qa_q3_product_policy *out, qa_error *error)
{
    if (!out || !valid_policy(saved))
        return application_fail(error, QA_ERROR_FORMAT, "Saved Q3 product policy has inconsistent retained fields");
    if (!register_policy_values(saved, cvars, 0, saved->fs_restrict, error)) return false;
    *out = *saved;
    return true;
}

static bool identify(qa_catalog *catalog, qa_product_id selected, bool *restricted, qa_error *error)
{
    /* The selected retail product may be missing required packages. Identity
     * lookup still uses its real discovered search path, as source files.c. */
    qa_vfs *files = NULL;
    if (!qa_catalog_q3_identification_open(catalog, selected, &files, error)) return false;
    qa_resource *resource = NULL;
    qa_error issue = {0};
    bool found = qa_vfs_acquire(files, "productid.txt", &resource, NULL, &issue);
    if (!found) {
        qa_vfs_destroy(files);
        if (issue.code != QA_ERROR_NOT_FOUND) { if (error) *error = issue; return false; }
        *restricted = true;
        return true;
    }
    qa_bytes bytes = qa_resource_bytes(resource);
    bool valid = bytes.size >= sizeof(scrambled_product_id);
    uint32_t seed = 5000;
    for (size_t i = 0; valid && i < sizeof(scrambled_product_id); ++i) {
        valid = (uint8_t)(scrambled_product_id[i] ^ (seed & 255u)) == bytes.data[i];
        seed = UINT32_C(69069) * seed + 1u;
    }
    qa_resource_release(resource);
    qa_vfs_destroy(files);
    if (!valid) return application_fail(error, QA_ERROR_FORMAT, "Invalid product identification");
    *restricted = false;
    return true;
}

bool application_q3_product_prepare(qa_catalog *catalog, qa_product_id selected,
    qa_q3_product_policy *policy, qa_error *error)
{
    const qa_product *product = qa_catalog_product(catalog, selected);
    if (!product || !valid_policy(policy))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 preparation requires its retained startup policy and selected product");
    if (product->family != QA_GAME_Q3) return true;
    qa_q3_product_policy prepared = *policy;
    if (!prepared.restriction_resolved) {
        if (!identify(catalog, selected, &prepared.filesystem_restricted, error)) return false;
        prepared.restriction_resolved = true;
    }
    if (prepared.filesystem_restricted && !qa_catalog_q3_restrict(catalog, error)) return false;
    *policy = prepared;
    return true;
}

bool application_q3_product_fields(qa_source_save_io *io, qa_q3_product_policy *policy)
{
    if (!io || !policy || (io->direction == QA_SOURCE_SAVE_WRITE && !valid_policy(policy))) return false;
    if (!qa_source_save_bool(io, &policy->prerelease_demo) ||
        !qa_source_save_bool(io, &policy->prerelease_team_arena_demo) ||
        !qa_source_save_bool(io, &policy->fs_restrict) ||
        !qa_source_save_bool(io, &policy->restriction_resolved) ||
        !qa_source_save_bool(io, &policy->filesystem_restricted)) return false;
    return valid_policy(policy) ||
        application_fail(io->error, QA_ERROR_FORMAT, "Q3 product checkpoint leaves its initial policy domain");
}

static qa_product_id selected_q3(const qa_launch_draft *draft)
{
    const qa_launch_choices *choices = qa_launch_draft_choices(draft);
    qa_catalog *catalog = qa_launch_draft_catalog(draft);
    const qa_launch_binding *game = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    qa_product_id fallback = QA_PRODUCT_NONE;
    for (size_t i = 0; i < choices->provider_count; ++i) {
        const qa_launch_provider *provider = &choices->providers[i];
        const qa_product *product = qa_catalog_product(catalog, provider->product);
        if (!product || product->family != QA_GAME_Q3) continue;
        if (game && !strcmp(game->instance, provider->instance)) return product->id;
        if (!fallback) fallback = product->id;
    }
    return fallback;
}

bool application_q3_product_validate_draft(const qa_q3_product_policy *policy,
    const qa_launch_draft *draft, qa_error *error)
{
    if (!valid_policy(policy) || !draft)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 content admission lacks its retained policy");
    bool restricted = qa_catalog_q3_restricted(qa_launch_draft_catalog(draft));
    if ((restricted && (!policy->restriction_resolved || !policy->filesystem_restricted)) ||
        (selected_q3(draft) && (!policy->restriction_resolved || restricted != policy->filesystem_restricted)))
        return application_fail(error, QA_ERROR_FORMAT, "Selected Q3 catalog differs from its retained initial media policy");
    return true;
}

bool application_q3_product_prepare_draft(qa_application *app,
    const qa_launch_draft *draft, application_q3_product_preparation *prepared, qa_error *error)
{
    if (!app || !draft || !prepared || prepared->catalog || prepared->draft || app->q3_product_preparing)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 preparation requires a fresh application candidate");
    prepared->policy = app->q3_product;
    qa_catalog *catalog = qa_launch_draft_catalog(draft);
    qa_product_id selected = selected_q3(draft);
    bool okay;
    if (selected && (!prepared->policy.restriction_resolved ||
        (prepared->policy.filesystem_restricted && !qa_catalog_q3_restricted(catalog)))) {
        okay = qa_catalog_clone(catalog, &prepared->catalog, error) &&
            application_q3_product_prepare(prepared->catalog, selected, &prepared->policy, error) &&
            qa_launch_draft_rebase(draft, prepared->catalog, &prepared->draft, error);
    } else {
        okay = qa_launch_draft_copy(draft, &prepared->draft, error);
    }
    if (okay) okay = application_q3_product_validate_draft(&prepared->policy, prepared->draft, error);
    if (!okay) {
        qa_launch_draft_destroy(prepared->draft);
        qa_catalog_release(prepared->catalog);
        *prepared = (application_q3_product_preparation){0};
        return false;
    }
    app->q3_product_preparing = &prepared->policy;
    return true;
}

bool application_q3_product_validate_snapshot(const qa_q3_product_policy *policy,
    const qa_launch_snapshot *snapshot, qa_error *error)
{
    if (!valid_policy(policy) || !snapshot)
        return application_fail(error, QA_ERROR_FORMAT, "Restored Q3 sources lack their retained policy");
    for (size_t i = 0; i < qa_launch_snapshot_instance_count(snapshot); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(snapshot, i);
        qa_catalog *catalog = qa_launch_instance_catalog(instance);
        const qa_product *product = qa_catalog_product(catalog, instance->selection.product);
        if (!product || product->family != QA_GAME_Q3) continue;
        if (!policy->restriction_resolved || qa_catalog_q3_restricted(catalog) != policy->filesystem_restricted)
            return application_fail(error, QA_ERROR_FORMAT, "Restored Q3 source catalog differs from its initial media policy");
        if (policy->filesystem_restricted)
            for (size_t j = 0; j < qa_vfs_mount_count(instance->content); ++j) {
                qa_vfs_mount_info mount;
                if (!qa_vfs_mount_at(instance->content, j, &mount) || !mount.q3_demo)
                    return application_fail(error, QA_ERROR_FORMAT, "Restored Q3 source lost its actual per-mount demo admission");
            }
    }
    return true;
}

void application_q3_product_finish(qa_application *app,
    application_q3_product_preparation *prepared, bool published)
{
    if (published) {
        app->q3_product = prepared->policy;
        if (prepared->catalog) {
            qa_catalog *previous = app->catalog;
            app->catalog = prepared->catalog;
            prepared->catalog = NULL;
            app->catalog_generation = qa_catalog_generation(app->catalog);
            qa_catalog_release(previous);
        }
    }
    app->q3_product_preparing = NULL;
    if (app->startup) for (size_t i = 0; i < app->startup->count; ++i) {
        if (published && app->startup->rows[i].pending) app->startup->rows[i].consumed = true;
        app->startup->rows[i].pending = false;
    }
    qa_launch_draft_destroy(prepared->draft);
    qa_catalog_release(prepared->catalog);
    *prepared = (application_q3_product_preparation){0};
}

bool qa_application_q3_product_policy_read(const qa_application *app, qa_q3_product_policy *out)
{
    if (!app || !out || app->destroy_requested) return false;
    const qa_q3_product_policy *policy = application_q3_product_source_policy(app);
    if (!policy->restriction_resolved) return false;
    *out = *policy;
    return true;
}
