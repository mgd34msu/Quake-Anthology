#include "shared_values.h"

typedef struct shared_source {
    struct shared_source *next;
    qa_application_startup_source tuple;
} shared_source;
struct frontend_shared_values {
    qa_frontend *frontend;
    frontend_config_store *manager;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_cvars *registry;
    qa_cvars_edit *edit;
    shared_source *sources;
    bool published,terminal,input_restart;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool current(const frontend_shared_values *owner,qa_error *error)
{
    return (owner && owner->frontend && owner->application==owner->frontend->application &&
        owner->manager==owner->frontend->config_store &&
        owner->registry==qa_application_cvars(owner->application)) ||
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
        command->dialect!=source->command.dialect ||
        !qa_application_command_context_active(owner->application,command))
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
        if (!qa_cvars_edit_prepare(owner->registry,&owner->edit,error)) { free(row); free(owner); return false; }
        *out=owner;
    }
    row->next=owner->sources; owner->sources=row; return true;
}
qa_cvars *frontend_shared_values_registry(const frontend_shared_values *owner)
{ return owner?owner->registry:NULL; }
const qa_cvars_edit *frontend_shared_values_prepared(const frontend_shared_values *owner)
{ return owner && !owner->terminal && !owner->published?owner->edit:NULL; }
bool frontend_shared_values_resolve(const frontend_shared_values *owner,
    const qa_application_startup_source *source,const qa_command_context *command,const char *name,
    qa_cvars **out,qa_error *error)
{
    if (!out || !name || !command_current(owner,source,command,error)) return false;
    *out=qa_cvars_edit_find(owner->edit,name)?owner->registry:NULL; return true;
}
bool frontend_shared_values_edit(const frontend_shared_values *owner,
    const qa_application_startup_source *source,const qa_command_context *command,qa_cvars *registry,
    qa_cvars_edit **out,qa_error *error)
{
    if (!out || !registry || !command_current(owner,source,command,error)) return false;
    *out=registry==owner->registry?owner->edit:NULL; return true;
}
bool frontend_shared_values_input_restart(frontend_shared_values *owner,
    const qa_application_startup_source *source,const qa_command_context *command,qa_error *error)
{
    if (!command_current(owner,source,command,error)) return false;
    owner->input_restart=true; return true;
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
bool frontend_shared_values_ready(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->terminal || owner->published || !owner->edit)
        return fail(error,"Shared scalar publication is unavailable");
    for (const shared_source *row=owner->sources;row;row=row->next)
        if (!pending(owner,&row->tuple,error)) return false;
    return qa_cvars_edit_ready(owner->edit,error);
}
void frontend_shared_values_publish(frontend_shared_values *owner)
{ qa_cvars_edit_publish(owner->edit); owner->edit=NULL; owner->published=true; }
bool frontend_shared_values_finish(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || !owner->published || owner->terminal)
        return fail(error,"Shared scalar notifications require actual publication");
    if (!qa_cvars_edit_finish(owner->registry,error)) return false;
    owner->terminal=true; return true;
}
bool frontend_shared_values_abort(frontend_shared_values *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published || owner->terminal)
        return fail(error,"Shared abort requires its actual unpublished scalar ticket");
    qa_cvars_edit_abort(owner->edit); owner->edit=NULL; owner->terminal=true; return true;
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
