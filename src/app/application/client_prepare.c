#include "internal.h"
#include "qa/console_cvar_observer.h"
#include "native_client_roles.h"
#include "engine_shutdown.h"
#include "qa/application_client_prepare.h"
#include <stdlib.h>
#include <string.h>

struct qa_application_client_preparation {
    qa_application *application;
    qa_application_client_source source;
    qa_launch_instance_lease *metadata;
    const qa_launch_snapshot *launch;
    qa_world *world;
    qa_cvars *registry;
    uint64_t configuration_generation,command_generation,map_revision;
    qa_application_client_prepare_phase phase;
    void *startup_context;
    bool (*startup_current)(void *,const qa_application_client_source *);
    bool entered,canceling,resources_complete,published;
};
bool qa_application_client_prepare_associated(const qa_application *app,
    const qa_application_client_preparation *p)
{
    qa_console *console=NULL; qa_cvars *registry=NULL;
    bool detached=app && p && p->phase==QA_CLIENT_PREPARE_CLEANUP && app->engine_shutdown &&
        qa_application_engine_shutdown_client(app->engine_shutdown)==p &&
        qa_application_engine_shutdown_read(app->engine_shutdown,&console,&registry,NULL) && registry==p->registry;
    return app && p && p->application==app && app->client_preparation==p &&
        app->operation==APPLICATION_IDLE &&
        !app->startup_flow && !app->startup_publication && (!app->engine_shutdown || detached) &&
        qa_application_launch(app)==p->launch && app->world==p->world &&
        (detached || app->cvars==p->registry) &&
        qa_application_configuration_generation(app)==p->configuration_generation &&
        app->command_generation==p->command_generation && app->map_revision==p->map_revision &&
        application_native_client_source_associated(app,&p->source);
}
bool qa_application_client_prepare_current(const qa_application_client_preparation *p)
{ return p && qa_application_client_prepare_associated(p->application,p); }
bool qa_application_client_prepare_active(const qa_application *app)
{ return app && app->client_preparation; }
bool qa_application_client_prepare_phase_is(const qa_application_client_preparation *p,
    qa_application_client_prepare_phase phase)
{ return qa_application_client_prepare_current(p) && p->phase==phase; }
bool qa_application_client_prepare_entered(const qa_application_client_preparation *p,
    qa_application_client_prepare_phase phase)
{ return qa_application_client_prepare_phase_is(p,phase) && p->entered; }
const qa_application_client_source *qa_application_client_prepare_source(const qa_application_client_preparation *p)
{ return qa_application_client_prepare_current(p)?&p->source:NULL; }
qa_application *qa_application_client_prepare_application(const qa_application_client_preparation *p)
{ return qa_application_client_prepare_current(p)?p->application:NULL; }
const qa_launch_snapshot *qa_application_client_prepare_launch(const qa_application_client_preparation *p)
{ return qa_application_client_prepare_current(p)?p->launch:NULL; }
bool qa_application_client_prepare_holds(const qa_application *app,const qa_application_client_source *source)
{
    const qa_application_client_preparation *p=app?app->client_preparation:NULL;
    return p && source && p->source.context.lifetime==source->context.lifetime;
}
bool qa_application_client_prepare_begin(qa_application *app,const qa_application_client_source *source,
    qa_application_client_preparation **out,qa_error *e)
{
    if (!app || !source || !out || *out || app->client_preparation || app->operation!=APPLICATION_IDLE ||
        app->destroy_requested || app->startup_flow || app->startup_publication || app->engine_shutdown ||
        !app->cvars || !qa_cvars_observer_idle(app->cvars) ||
        !qa_application_client_current(app,source))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT preparation requires its returned physical owner and canonical ENGINE");
    qa_application_client_preparation *p=calloc(1,sizeof(*p));
    if (!p) return application_fail(e,QA_ERROR_MEMORY,"Retaining standalone CLIENT preparation");
    if (!qa_launch_instance_retain_metadata(source->descriptor,&p->metadata,e)) { free(p); return false; }
    p->application=app; p->source=*source; p->source.descriptor=qa_launch_instance_lease_view(p->metadata);
    p->launch=qa_application_launch(app); qa_launch_snapshot_retain(p->launch);
    p->world=app->world; p->registry=app->cvars;
    p->configuration_generation=qa_application_configuration_generation(app);
    p->command_generation=app->command_generation; p->map_revision=app->map_revision;
    app->client_preparation=p; *out=p; return true;
}
bool qa_application_client_prepare_advance(qa_application_client_preparation *p,
    bool (*advance)(void *,qa_application_client_preparation *,bool *,qa_error *),void *context,
    bool *complete,qa_error *e)
{
    if (complete) *complete=false;
    if (!complete || !advance || !qa_application_client_prepare_current(p) ||
        p->application->destroy_requested || p->entered ||
        p->phase>QA_CLIENT_PREPARE_RESOURCES || p->resources_complete)
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT advancement lost its actual retained phase");
    bool done=false; p->entered=true; bool ok=advance(context,p,&done,e); p->entered=false;
    if (!qa_application_client_prepare_current(p))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT advancement changed its physical publication baseline");
    if (!ok || !done) return ok;
    if (p->phase==QA_CLIENT_PREPARE_RESOURCES) p->resources_complete=true;
    else p->phase=(qa_application_client_prepare_phase)(p->phase+1);
    *complete=true; return true;
}
bool qa_application_client_prepare_consume(qa_application_client_preparation *p,
    bool (*ready)(void *,const qa_application_client_preparation *),
    void (*consume)(void *,qa_application_client_preparation *),void *context,qa_error *e)
{
    if (!ready || !consume || !qa_application_client_prepare_current(p) ||
        p->application->destroy_requested || p->entered ||
        p->phase!=QA_CLIENT_PREPARE_RESOURCES || !p->resources_complete || !ready(context,p) ||
        !qa_application_client_prepare_current(p))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT publication requires every actual prepared child");
    p->phase=QA_CLIENT_PREPARE_CONSUMING; p->entered=true;
    consume(context,p); p->entered=false; p->published=true; p->phase=QA_CLIENT_PREPARE_CLEANUP;
    return true;
}
bool qa_application_client_prepare_abort(qa_application_client_preparation *p,qa_error *e)
{
    if (!qa_application_client_prepare_current(p) || p->entered || p->published)
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT abort lost its unpublished physical owner");
    p->phase=QA_CLIENT_PREPARE_CLEANUP; return true;
}
bool qa_application_client_prepare_cancel_entered(const qa_application_client_preparation *p)
{
    return qa_application_client_prepare_entered(p,QA_CLIENT_PREPARE_RELEASE) && p->canceling;
}
bool qa_application_client_prepare_cancel_advance(qa_application_client_preparation *p,
    bool (*cleanup)(void *,qa_application_client_preparation *,bool *,qa_error *),void *context,
    bool *complete,qa_error *e)
{
    if (complete) *complete=false;
    if (!complete || !cleanup || !qa_application_client_prepare_phase_is(p,QA_CLIENT_PREPARE_RELEASE) ||
        p->entered || p->published)
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT release cancellation lost its returned physical owner");
    bool done=false; p->canceling=true; p->entered=true;
    bool ok=cleanup(context,p,&done,e);
    p->entered=false; p->canceling=false;
    if (!qa_application_client_prepare_current(p))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT release cancellation changed its retained physical owner");
    *complete=done; return ok;
}
bool qa_application_client_prepare_finish(qa_application_client_preparation **out,
    bool (*cleanup)(void *,qa_application_client_preparation *,bool *,qa_error *),void *context,
    bool *complete,qa_error *e)
{
    if (complete) *complete=false;
    qa_application_client_preparation *p=out?*out:NULL;
    if (!complete || !cleanup || !qa_application_client_prepare_current(p) || p->entered ||
        p->phase!=QA_CLIENT_PREPARE_CLEANUP)
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT cleanup lost its retained terminal owner");
    bool done=false; p->entered=true; bool ok=cleanup(context,p,&done,e); p->entered=false;
    if (!qa_application_client_prepare_current(p))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT cleanup changed its retained physical owner");
    if (!done) return ok;
    application_engine_shutdown_release_client(p->application,p);
    p->application->client_preparation=NULL;
    qa_launch_snapshot_release(p->launch); qa_launch_instance_lease_release(p->metadata); free(p);
    *out=NULL; *complete=true; return ok;
}
static bool variables(qa_application_client_preparation *p,bool initial,qa_error *e)
{
    if (!qa_application_client_prepare_entered(p,QA_CLIENT_PREPARE_CONFIGURATION))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT startup variables require the actual entered cfg programme");
    if (!p->startup_current) return true;
    if (!qa_application_client_prepare_startup_current(p))
        return application_fail(e,QA_ERROR_ARGUMENT,"CLIENT startup variables lost their actual remote request");
    qa_console_dialect dialect=p->source.context.command.dialect;
    if (dialect==QA_CONSOLE_Q1 || dialect==QA_CONSOLE_QW) return true;
    char *early=NULL; size_t early_size=0;
    for (size_t i=0;i<qa_application_startup_command_count(p->application);++i) {
        const char *text=qa_application_startup_command(p->application,i);
        qa_command_tokens tokens={0};
        if (!text || !qa_command_tokenize(text,dialect,false,&tokens,e)) { free(early); return false; }
        bool ok=true;
        if (tokens.count && !strcmp(tokens.values[0],"set")) {
            if (dialect==QA_CONSOLE_Q3)
                ok=qa_console_cvar_startup_set(p->source.context.console,&p->source.context.command,
                    tokens.count>1?tokens.values[1]:"",tokens.count>2?tokens.values[2]:"",e);
            else {
                size_t size=strlen(text);
                if (early_size>SIZE_MAX-2 || size>SIZE_MAX-early_size-2)
                    ok=application_fail(e,QA_ERROR_MEMORY,"CLIENT early command programme exceeds storage");
                char *next=ok?realloc(early,early_size+size+2):NULL;
                if (ok && !next) ok=application_fail(e,QA_ERROR_MEMORY,"Retaining actual CLIENT early command programme");
                if (ok) { early=next; memcpy(early+early_size,text,size); early_size+=size;
                    early[early_size++]='\n'; early[early_size]=0; }
            }
        }
        qa_command_tokens_free(&tokens);
        if (!ok || !qa_application_client_prepare_startup_current(p)) { free(early); return false; }
    }
    bool ok=!early || (initial?qa_console_append(p->source.context.console,&p->source.context.command,early,e):
        qa_console_insert(p->source.context.console,&p->source.context.command,early,e));
    free(early); return ok && qa_application_client_prepare_startup_current(p);
}
bool qa_application_client_prepare_initial(qa_application_client_preparation *p,qa_error *e)
{ return variables(p,true,e); }
bool qa_application_client_prepare_replay(qa_application_client_preparation *p,qa_error *e)
{ return variables(p,false,e); }
bool qa_application_client_prepare_startup_current(const qa_application_client_preparation *p)
{
    return qa_application_client_prepare_current(p) && p->startup_current &&
        p->startup_current(p->startup_context,&p->source) && qa_application_client_prepare_current(p);
}
bool qa_application_client_prepare_startup_claim(qa_application_client_preparation *p,void *context,
    bool (*current)(void *,const qa_application_client_source *),qa_error *e)
{
    if (!context || !current || !qa_application_client_prepare_phase_is(p,QA_CLIENT_PREPARE_CONFIGURATION) ||
        p->entered || p->startup_current || !current(context,&p->source) ||
        !qa_application_client_prepare_current(p))
        return application_fail(e,QA_ERROR_ARGUMENT,"Startup operands require the genuine primary CLIENT request owner");
    p->startup_context=context; p->startup_current=current; return true;
}
