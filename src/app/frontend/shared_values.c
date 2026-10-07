#include "shared_values.h"
#include "shared_register.h"
#include "input_settings.h"
#include "capture.h"
#include "qa/application_engine_shutdown.h"
#include "qa/text.h"
#include <stdio.h>

typedef struct shared_source {
    struct shared_source *next;
    qa_application_startup_source tuple;
} shared_source;
struct frontend_shared_values {
    qa_frontend *frontend;
    frontend_config_store *manager;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_application_client_preparation *client;
    qa_cvars *registry;
    qa_cvars_edit *edit;
    qa_console *root_console;
    shared_source *sources;
    bool owns_edit,published,terminal,input_restart;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool current(const frontend_shared_values *owner,qa_error *error)
{
    return (owner && owner->frontend && owner->application==owner->frontend->application &&
        owner->manager==owner->frontend->config_store &&
        owner->registry==qa_application_cvars(owner->application) &&
        (!owner->client || qa_application_client_prepare_associated(owner->application,owner->client)) &&
        (!owner->root_console || owner->root_console==qa_application_console(owner->application))) ||
        fail(error,"Shared values lost their retained actual ENGINE parent");
}
static bool same_tuple(const qa_application_startup_source *a,const qa_application_startup_source *b)
{
    return a && b && a->descriptor && b->descriptor &&
        a->descriptor->storage==b->descriptor->storage && a->console==b->console && a->cvars==b->cvars &&
        a->scope.provider==b->scope.provider && a->scope.kind==b->scope.kind && a->scope.seat==b->scope.seat &&
        a->declaration_owner==b->declaration_owner;
}
static bool pending(const frontend_shared_values *owner,const qa_application_startup_source *source,
    qa_error *error)
{
    if (!current(owner,error) || owner->terminal || owner->published || !owner->edit ||
        !frontend_config_store_source_pending(owner->manager,owner->application,owner->candidate,source))
        return fail(error,"Shared values require their exact pending physical configuration source");
    for (const shared_source *row=owner->sources;row;row=row->next)
        if (same_tuple(&row->tuple,source)) return true;
    return fail(error,"Configuration source has not joined this canonical ENGINE ticket");
}
static bool command_current(const frontend_shared_values *owner,const qa_application_startup_source *source,
    const qa_command_context *command,qa_error *error)
{
    if (!pending(owner,source,error) || !command || command->origin==QA_COMMAND_REMOTE ||
        command->owner!=source->command.owner || command->session!=source->command.session ||
        command->dialect!=source->command.dialect || command->cvar_view!=source->command.cvar_view ||
        (!qa_application_command_context_active(owner->application,command) &&
            !qa_console_cvar_entered(source->console,command)))
        return fail(error,"Shared values require the source's actual captured command context");
    if ((source->scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME ||
         source->scope.kind==QA_APPLICATION_CONSOLE_Q3_UI) &&
        (command->origin!=QA_COMMAND_SEAT || command->seat!=source->scope.seat))
        return fail(error,"Shared CLIENT values require their actual authored recipient");
    return true;
}
bool frontend_shared_values_begin(qa_frontend *frontend,frontend_config_store *manager,
    qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *source,frontend_shared_values **out,qa_error *error)
{
    if (!frontend || !manager || frontend->config_store!=manager || !application || application!=frontend->application || !candidate ||
        !out || !source || !frontend_config_store_source_pending(manager,application,candidate,source))
        return fail(error,"Shared preparation requires the actual linked pending configuration tuple");
    frontend_shared_values *owner=*out;
    if (owner && (owner->frontend!=frontend || owner->manager!=manager ||
        owner->application!=application || owner->candidate!=candidate || owner->terminal || owner->published ||
        !current(owner,error))) return fail(error,"Shared sources cannot join another ENGINE preparation");
    if (owner) for (const shared_source *row=owner->sources;row;row=row->next)
        if (same_tuple(&row->tuple,source)) return true;
    shared_source *row=calloc(1,sizeof(*row));
    if (!row) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining shared configuration source");
    row->tuple=*source;
    if (!owner) {
        owner=calloc(1,sizeof(*owner));
        if (!owner) { free(row); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining canonical ENGINE preparation"); }
        owner->frontend=frontend; owner->manager=manager; owner->application=application;
        owner->candidate=candidate; owner->registry=qa_application_cvars(application);
        owner->edit=qa_cvars_prepared_edit(owner->registry);
        owner->owns_edit=owner->edit==NULL;
        if (owner->owns_edit && !qa_cvars_edit_prepare(owner->registry,&owner->edit,error)) { free(row); free(owner); return false; }
        *out=owner;
    }
    row->next=owner->sources; owner->sources=row; return true;
}
bool frontend_shared_values_begin_root(qa_frontend *f,frontend_config_store *manager,
    qa_application *app,const qa_launch_snapshot *candidate,frontend_shared_values **out,qa_error *error)
{
    qa_console *console=NULL; qa_cvars *registry=NULL; qa_command_context command={0};
    if (!f || !manager || f->config_store!=manager || f->application!=app || !out ||
        !qa_application_startup_root_phase(app,candidate) ||
        !qa_application_startup_root_read(app,candidate,&console,&registry,&command,error) ||
        console!=qa_application_console(app) || registry!=qa_application_cvars(app) || command.owner)
        return fail(error,"Shared root values require the genuine entered ENGINE bootstrap authority");
    frontend_shared_values *owner=*out;
    if (owner) return (owner->frontend==f && owner->manager==manager && owner->application==app &&
        owner->candidate==candidate && owner->root_console==console && !owner->terminal &&
        !owner->published && current(owner,error)) ||
        fail(error,"Shared root values already belong to another physical preparation");
    owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining canonical ENGINE root preparation");
    owner->frontend=f; owner->manager=manager; owner->application=app; owner->candidate=candidate;
    owner->registry=registry; owner->root_console=console;
    owner->edit=qa_cvars_prepared_edit(registry);
    owner->owns_edit=owner->edit==NULL;
    if (owner->owns_edit && !qa_cvars_edit_prepare(registry,&owner->edit,error)) { free(owner); return false; }
    *out=owner; return true;
}
bool frontend_shared_values_root_access(const frontend_shared_values *owner,
    const qa_console *console,const qa_command_context *command,qa_cvars **registry,
    qa_cvars_edit **edit,qa_error *error)
{
    qa_console *actual_console=NULL; qa_cvars *actual_registry=NULL;
    qa_command_context actual={0};
    if (!registry || !edit || !command || !current(owner,error) || owner->client ||
        owner->candidate || owner->terminal || owner->published || !owner->edit ||
        !owner->root_console || console!=owner->root_console || !qa_console_idle(console) ||
        !qa_application_startup_bootstrap_images_ready(owner->application) ||
        !qa_application_startup_root_read(owner->application,owner->candidate,
            &actual_console,&actual_registry,&actual,error) ||
        actual_console!=console || actual_registry!=owner->registry ||
        command->session!=actual.session || command->owner!=actual.owner ||
        command->client!=actual.client || command->seat!=actual.seat ||
        command->dialect!=actual.dialect || command->origin!=actual.origin ||
        command->direct!=actual.direct || command->console_text!=actual.console_text ||
        command->registry!=actual.registry || command->generation!=actual.generation ||
        command->cvar_view!=actual.cvar_view ||
        !qa_actor_id_equal(command->actor,actual.actor) ||
        ((!command->script)!=(!actual.script)) ||
        (command->script && strcmp(command->script,actual.script)) ||
        !qa_cvars_edit_returned_is(owner->edit,owner->registry))
        return fail(error,"Shared root lookup requires its returned images constructor and exact ENGINE command");
    *registry=owner->registry; *edit=owner->edit; return true;
}
bool frontend_shared_values_root_definition_access(const frontend_shared_values *owner,
    const qa_console *console,const qa_command_context *command,qa_cvars **registry,
    qa_cvars_edit **edit,qa_error *error)
{
    qa_console *actual_console=NULL; qa_cvars *actual_registry=NULL;
    qa_command_context actual={0};
    if (!registry || !edit || !command || !current(owner,error) || owner->client ||
        !owner->candidate || owner->terminal || owner->published || !owner->edit ||
        !owner->root_console || console!=owner->root_console || !qa_console_idle(console) ||
        !qa_application_startup_root_definition_phase(owner->application,owner->candidate) ||
        !qa_application_startup_root_read(owner->application,owner->candidate,
            &actual_console,&actual_registry,&actual,error) ||
        actual_console!=console || actual_registry!=owner->registry ||
        command->session!=actual.session || command->owner!=actual.owner ||
        command->client!=actual.client || command->seat!=actual.seat ||
        command->dialect!=actual.dialect || command->origin!=actual.origin ||
        command->direct!=actual.direct || command->console_text!=actual.console_text ||
        command->registry!=actual.registry || command->generation!=actual.generation ||
        command->cvar_view!=actual.cvar_view ||
        !qa_actor_id_equal(command->actor,actual.actor) ||
        ((!command->script)!=(!actual.script)) ||
        (command->script && strcmp(command->script,actual.script)) ||
        qa_cvars_prepared_edit(owner->registry)!=owner->edit ||
        qa_cvars_edit_registry(owner->edit)!=owner->registry)
        return fail(error,"Shared root declarations require their entered provider definition and exact ENGINE command");
    *registry=owner->registry; *edit=owner->edit; return true;
}
qa_cvars *frontend_shared_values_registry(const frontend_shared_values *owner)
{ return owner?owner->registry:NULL; }
bool frontend_shared_values_begin_client(qa_frontend *f,frontend_config_store *manager,
    qa_application *app,qa_application_client_preparation *client,frontend_shared_values **out,qa_error *error)
{
    if (!f || !manager || f->config_store!=manager || f->application!=app || !out || *out ||
        !qa_application_client_prepare_associated(app,client) ||
        !qa_application_client_prepare_phase_is(client,QA_CLIENT_PREPARE_CONFIGURATION))
        return fail(error,"Shared CLIENT values require their actual physical preparation");
    frontend_shared_values *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining canonical CLIENT settings preparation");
    owner->frontend=f; owner->manager=manager; owner->application=app;
    owner->client=client; owner->registry=qa_application_cvars(app);
    owner->edit=qa_cvars_prepared_edit(owner->registry);
    owner->owns_edit=owner->edit==NULL;
    if (owner->owns_edit && !qa_cvars_edit_prepare(owner->registry,&owner->edit,error)) { free(owner); return false; }
    *out=owner; return true;
}
bool frontend_shared_values_client_access(const frontend_shared_values *owner,
    const qa_application_client_preparation *client,const qa_command_context *command,
    qa_cvars **registry,qa_cvars_edit **edit,qa_error *error)
{
    const qa_application_client_source *source=qa_application_client_prepare_source(client);
    if (!registry || !edit || !current(owner,error) || owner->client!=client ||
        owner->terminal || owner->published || !owner->edit || !source || !command ||
        !qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_CONFIGURATION) ||
        command->owner!=source->context.command.owner || command->session!=source->context.command.session ||
        command->seat!=source->context.command.seat || command->client!=source->context.command.client ||
        command->dialect!=source->context.command.dialect || command->origin!=source->context.command.origin ||
        command->registry!=source->context.command.registry || command->generation!=source->context.command.generation ||
        command->cvar_view!=source->context.command.cvar_view ||
        !qa_actor_id_equal(command->actor,source->context.command.actor) ||
        (!qa_application_command_context_active(owner->application,command) &&
            !qa_console_cvar_entered(source->context.console,command)))
        return fail(error,"Shared CLIENT edit requires its actual entered routed console operation");
    *registry=source->context.cvars; *edit=owner->edit; return true;
}
bool frontend_shared_values_refresh(frontend_shared_values *owner,
    const qa_application_startup_source *source,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal || !source ||
        !source->descriptor || !source->declaration_owner ||
        !frontend_config_store_source_pending(owner->manager,owner->application,owner->candidate,source))
        return fail(error,"Shared refresh requires the real validated physical source tuple");
    for (shared_source *row=owner->sources;row;row=row->next) {
        const qa_application_startup_source *held=&row->tuple;
        if (held->descriptor->storage!=source->descriptor->storage || held->console!=source->console ||
            held->cvars!=source->cvars || held->scope.provider!=source->scope.provider ||
            held->scope.kind!=source->scope.kind || held->scope.seat!=source->scope.seat) continue;
        row->tuple.declaration_owner=source->declaration_owner;
        return true;
    }
    return fail(error,"Validated shared source has no retained physical preparation");
}
qa_cvars_edit *frontend_shared_values_prepared(const frontend_shared_values *owner)
{ return owner && !owner->terminal && !owner->published?owner->edit:NULL; }
bool frontend_shared_values_resolve(const frontend_shared_values *owner,
    const qa_application_startup_source *source,const qa_command_context *command,const char *name,
    qa_cvars **out,qa_error *error)
{
    if (!out || !name || !command_current(owner,source,command,error)) return false;
    if (!qa_cvars_same_store(owner->registry,source->cvars))
        return fail(error,"Source values must use the canonical cvar store");
    *out=source->cvars; return true;
}
bool frontend_shared_values_edit(const frontend_shared_values *owner,
    const qa_application_startup_source *source,const qa_command_context *command,qa_cvars *registry,
    qa_cvars_edit **out,qa_error *error)
{
    if (!out || !registry || !command_current(owner,source,command,error)) return false;
    *out=qa_cvars_same_store(registry,owner->registry)?owner->edit:NULL; return true;
}
bool frontend_shared_values_release_access(const frontend_shared_values *owner,
    const frontend_input_settings *input,const qa_console *console,const qa_command_context *command,
    qa_cvars **registry,qa_cvars_edit **edit,qa_error *error)
{
    if (!registry || !edit || !current(owner,error) || owner->terminal || owner->published ||
        !owner->edit || console!=qa_application_console(owner->application) || !command || command->owner ||
        !qa_application_command_context_active(owner->application,command) ||
        !frontend_input_settings_current(input,owner->frontend,error) ||
        !frontend_input_settings_command_current(input,console,command))
        return fail(error,"Shared release access lacks its exact advancing ENGINE programme");
    *registry=owner->registry; *edit=owner->edit; return true;
}
static bool input_restart(frontend_shared_values *owner,qa_error *error)
{
    if (!qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){
        .kind=QA_CVARS_EDIT_APPLY_LATCHED,.name="in_joystick"},error) ||
        !qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){
        .kind=QA_CVARS_EDIT_APPLY_LATCHED,.name="in_joystickProfile"},error)) return false;
    owner->input_restart=true; return true;
}
bool frontend_shared_values_client_input_restart(frontend_shared_values *owner,
    const qa_application_client_preparation *client,const qa_command_context *command,qa_error *error)
{
    qa_cvars *registry=NULL; qa_cvars_edit *edit=NULL;
    return frontend_shared_values_client_access(owner,client,command,&registry,&edit,error) &&
        input_restart(owner,error);
}
bool frontend_shared_values_programme_access(const frontend_shared_values *owner,
    const qa_console *console,const qa_command_context *command,qa_cvars **registry,
    qa_cvars_edit **edit,qa_error *error)
{
    if (!registry || !edit || !current(owner,error) || owner->terminal || owner->published ||
        !owner->edit || !frontend_config_store_images_command_current(owner->manager,
            owner->application,owner->candidate,console,command))
        return fail(error,"Image settings lack their exact executing canonical programme");
    *registry=owner->registry; *edit=owner->edit; return true;
}
bool frontend_shared_values_input_restart(frontend_shared_values *owner,
    const qa_application_startup_source *source,const qa_command_context *command,qa_error *error)
{
    return command_current(owner,source,command,error) && input_restart(owner,error);
}
bool frontend_shared_values_release_input_restart(frontend_shared_values *owner,
    const frontend_input_settings *input,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    qa_cvars *registry=NULL; qa_cvars_edit *edit=NULL;
    return frontend_shared_values_release_access(owner,input,console,command,&registry,&edit,error) &&
        input_restart(owner,error);
}
bool frontend_shared_values_input_restart_pending(const frontend_shared_values *owner)
{ return owner && !owner->published && !owner->terminal && owner->input_restart; }
bool frontend_shared_values_archive(frontend_shared_values *owner,const qa_cvar_archive *archive,qa_error *error)
{
    if (!current(owner,error) || owner->terminal || owner->published || !owner->edit || !archive ||
        (archive->count && !archive->entries)) return fail(error,"Shared archive has no retained canonical values");
    for (size_t i=0;i<archive->count;++i) {
        const qa_cvar_archive_entry *entry=&archive->entries[i];
        if (!entry->name || !entry->value) return fail(error,"Shared archive entry lacks its scalar bytes");
        if (!qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_FLAGS,
            .name=entry->name,.value=entry->value,.flags=QA_CVAR_ARCHIVE},error)) return false;
    }
    return true;
}
bool frontend_shared_values_q3_renderer_initialize(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal ||
        !qa_cvars_edit_returned_is(owner->edit,owner->registry))
        return fail(error,"Source renderer initialization requires its returned mutable canonical edit");
    return frontend_source_renderer_values_initialize(owner->registry,owner->edit,error);
}
bool frontend_shared_values_source_color_register(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal ||
        !qa_cvars_edit_returned_is(owner->edit,owner->registry))
        return fail(error,"Source color registration requires its owned returned canonical edit");
    return frontend_source_color_values_register(owner->registry,owner->edit,error);
}
bool frontend_shared_values_source_color_initialize(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal ||
        !qa_cvars_edit_returned_is(owner->edit,owner->registry))
        return fail(error,"Source color initialization requires its owned returned mutable edit");
    return frontend_source_color_values_initialize(owner->registry,owner->edit,error);
}
bool frontend_shared_values_native_initialize(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal ||
        owner->candidate || owner->client || !owner->root_console ||
        !qa_application_startup_bootstrap_images_ready(owner->application) ||
        !qa_cvars_edit_returned_is(owner->edit,owner->registry))
        return fail(error,"Native initialization requires its owned returned canonical edit");
    qa_console *console=NULL; qa_cvars *registry=NULL; qa_command_context command={0};
    if (!qa_application_startup_root_read(owner->application,NULL,&console,&registry,&command,error) ||
        console!=owner->root_console || registry!=owner->registry || command.owner)
        return fail(error,"Native initialization requires its actual ENGINE bootstrap context");
    const qa_frontend_options *options=&owner->frontend->options;
    const struct { const char *name; double value; bool specified; } settings[]={
        {"r_customwidth",options->display.width,options->width_specified},
        {"r_customheight",options->display.height,options->height_specified},
        {"r_gamma",options->gamma,options->gamma_specified}};
    for (size_t i=0;i<sizeof(settings)/sizeof(*settings);++i) {
        if (!settings[i].specified) continue;
        char value[64];
        if (!qa_format_number(settings[i].value,value,error) ||
            !qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){
                .kind=QA_CVARS_EDIT_SET,.name=settings[i].name,.value=value,.force=true},error)) return false;
    }
    return qa_application_startup_replay_variables(owner->application,console,&command,error) &&
        qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){
        .kind=QA_CVARS_EDIT_APPLY_LATCHED,.name="in_joystick"},error) &&
        qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){
        .kind=QA_CVARS_EDIT_APPLY_LATCHED,.name="in_joystickProfile"},error);
}
bool frontend_shared_values_window_observed(frontend_shared_values *owner,const qa_display_info *info,
    int swap,bool opengl,qa_error *error)
{
    if (!info || !current(owner,error) || owner->published || owner->terminal ||
        !qa_cvars_edit_returned_is(owner->edit,owner->registry))
        return fail(error,"Window observations require their owned returned canonical edit");
    struct { const char *name; uint32_t value; } values[4]={
        {"r_customwidth",info->logical_width},{"r_customheight",info->logical_height},
        {"r_fullscreen",info->fullscreen==QA_DISPLAY_WINDOWED?0:1},{"r_swapInterval",swap?1:0}};
    for (size_t i=opengl && info->fullscreen==QA_DISPLAY_WINDOWED?0:2;i<4;++i) {
        if (i==3 && !opengl) continue;
        char text[32];
        if (!qa_format_number(values[i].value,text,error) ||
            !qa_cvars_edit_apply(owner->edit,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,
                .name=values[i].name,.value=text,.force=true},error)) return false;
    }
    return true;
}
bool frontend_shared_values_ready(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->terminal || owner->published || !owner->edit)
        return fail(error,"Shared scalar publication is unavailable");
    for (const shared_source *row=owner->sources;row;row=row->next)
        if (!pending(owner,&row->tuple,error)) return false;
    return qa_cvars_edit_ready(owner->edit,error);
}
bool frontend_shared_values_ready_is(const frontend_shared_values *owner)
{
    if (!current(owner,NULL) || owner->terminal || owner->published ||
        !qa_cvars_edit_ready_is(owner->edit)) return false;
    for (const shared_source *row=owner->sources;row;row=row->next)
        if (!pending(owner,&row->tuple,NULL)) return false;
    return true;
}
void frontend_shared_values_publish(frontend_shared_values *owner)
{
    if (owner->owns_edit) qa_cvars_edit_publish(owner->edit);
    owner->edit=NULL; owner->published=true;
}
bool frontend_shared_values_finish(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || !owner->published || owner->terminal)
        return fail(error,"Shared scalar notifications require actual publication");
    if (owner->owns_edit && !qa_cvars_edit_finish(owner->registry,error)) return false;
    owner->terminal=true; return true;
}
bool frontend_shared_values_abort(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal ||
        !qa_cvars_edit_abort_is(owner->edit,owner->registry))
        return fail(error,"Shared abort requires its actual unpublished scalar ticket");
    if (owner->owns_edit) qa_cvars_edit_abort(owner->edit);
    owner->edit=NULL; owner->terminal=true; return true;
}
bool frontend_shared_values_destroy(frontend_shared_values **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_shared_values *owner=*in;
    if (!current(owner,error) || !owner->terminal || owner->edit)
        return fail(error,"Shared destruction retains nonterminal canonical values");
    while (owner->sources) { shared_source *row=owner->sources; owner->sources=row->next; free(row); }
    free(owner); *in=NULL; return true;
}
bool frontend_shared_values_engine_shutdown(frontend_shared_values **in,
    const qa_application_engine_shutdown *loan,qa_error *error)
{
    if (!in || !*in) return fail(error,"Detached scalar cleanup lacks its retained candidate owner");
    frontend_shared_values *owner=*in;
    qa_console *console=NULL; qa_cvars *registry=NULL;
    if (owner->published || owner->terminal || !owner->edit ||
        owner->frontend->application!=owner->application || owner->frontend->config_store!=owner->manager ||
        owner->frontend->stepping || owner->frontend->preparing || owner->frontend->capture ||
        owner->frontend->source_restoring || !frontend_seat_callbacks_returned(owner->frontend) ||
        qa_application_engine_shutdown_owner(loan)!=owner->application ||
        qa_application_engine_shutdown_candidate(loan)!=owner->candidate ||
        (owner->client && qa_application_engine_shutdown_client(loan)!=owner->client) ||
        !qa_application_engine_shutdown_read(loan,&console,&registry,error) || registry!=owner->registry ||
        !qa_console_idle(console) || !qa_cvars_edit_abort_is(owner->edit,registry))
        return fail(error,"Detached scalar cleanup lost its exact cancellation loan and edit");
    if (owner->owns_edit) qa_cvars_edit_abort(owner->edit);
    owner->edit=NULL; owner->terminal=true;
    while (owner->sources) { shared_source *row=owner->sources; owner->sources=row->next; free(row); }
    free(owner); *in=NULL; return true;
}
