#include "remote_config.h"
#include "authored_bindings.h"
#include "config_weapon_defaults.h"
#include "config_userinfo.h"
#include "input_profile.h"
#include "qa/source_frame_time.h"
#include "network_config.h"
#include "save_private.h"
#include "source_restore.h"
#include "native_q3_client.h"
#include "capture.h"
#include "shared_storage.h"
#include "global_settings_storage.h"
#include "qa/cvars_save.h"
#include "qa/console_save.h"
#include "qa/console_cvar_observer.h"
#include "qa/catalog_save.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct remote_input {
    struct remote_input *next;
    frontend_remote_configs *owner;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_input_seat *input,*publication_input;
    qa_seat_console *publication_console;
    qa_command_context source_command,publication_command;
    uint32_t physical_seat,logical_seat;
    qa_movement_kind movement;
    size_t references;
    bool ready;
} remote_input;
struct frontend_remote_config {
    frontend_remote_config *next;
    frontend_remote_configs *owner;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_launch_instance_lease *metadata;
    char *saved_instance,*saved_registry;
    qa_sha256_digest saved_identity;
    qa_application_console_scope scope;
    qa_command_context command;
    qa_application_startup_source retarget_origin;
    const void *retarget_game_storage;
    uint64_t declaration_owner;
    qa_console *console,*hosted_console;
    qa_cvars *owned,*cvars,*q3_mouse,*q3_view,*movement_mouse;
    frontend_client_registry *registry;
    frontend_key_profile *keys;
    frontend_config_files *files;
    frontend_startup_config *phase;
    qa_input_console *bindings;
    qa_input_seat *input;
    remote_input *staging;
    frontend_authored_bindings *authored;
    qa_cvar_archive archive,mouse_archive,view_archive,movement_archive;
    qa_seat_settings settings;
    uint32_t physical_seat;
    qa_movement_kind movement;
    size_t callback_references;
    bool hosted,hosted_pending,found,configured,released,published,running,imported,secondary_pending,write_registered,dump_registered;
    bool frontend_retired,retargeting,initial_variables,input_fresh;
    qa_error failure;
};
struct frontend_remote_configs {
    qa_frontend *frontend;
    frontend_config_store *manager;
    frontend_remote_config *rows;
    remote_input *inputs;
    bool restoring;
};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static bool equal(const char *a,const char *b)
{
    for (;;++a,++b) {
        unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return false;
        if (!x) return true;
    }
}
static const qa_launch_instance *descriptor(const frontend_remote_config *row)
{ return row?qa_launch_instance_lease_view(row->metadata):NULL; }
static bool scope_equal(qa_application_console_scope a,qa_application_console_scope b)
{ return a.provider==b.provider && a.kind==b.kind && a.seat==b.seat; }
static bool active(const frontend_remote_config *row,const qa_command_context *command)
{
    return row && !row->retargeting && command && command->origin==QA_COMMAND_SEAT && command->dialect==QA_CONSOLE_Q3 &&
        command->owner==row->scope.provider && command->seat==row->scope.seat &&
        command->session==row->command.session && qa_application_command_context_active(row->application,command);
}
static bool cvar_context(const frontend_remote_config *row,const qa_command_context *command)
{
    if (active(row,command)) return true;
    return row && command && !row->retargeting && !row->imported && row->configured && row->released &&
        descriptor(row) && row->console && row->cvars && qa_console_cvars(row->console)==row->cvars &&
        command->origin==QA_COMMAND_SEAT && command->dialect==QA_CONSOLE_Q3 &&
        command->owner==row->scope.provider && command->seat==row->scope.seat &&
        command->session==row->command.session && qa_console_cvar_entered(row->console,command);
}
bool frontend_remote_config_cvar_active(const frontend_remote_config *row,const qa_command_context *command)
{ return cvar_context(row,command); }
static void print(void *context,const char *text)
{
    frontend_remote_config *row=context;
    frontend_console_print(row->owner->frontend,&row->command,text);
}
static bool cheats(void *context)
{
    frontend_remote_config *row=context;
    const qa_cvar_view *value=frontend_config_store_engine_value(row->owner->manager,row->application,row->console,"sv_cheats");
    return value && value->integer!=0;
}
static qa_cvars *settings_registry(frontend_remote_config *row,qa_console_dialect dialect,qa_error *error)
{
    qa_cvar_options options={.dialect=dialect,.user=row,.print=print,.cheats_allowed=cheats};
    return qa_cvars_create(&options,error);
}
static bool retain_context(void *context,qa_error *error)
{
    frontend_remote_config *row=context;
    if (!row || row->callback_references==SIZE_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT callback context has no retained configuration owner");
    ++row->callback_references; return true;
}
static bool release_context(void *context,qa_error *error)
{
    frontend_remote_config *row=context;
    if (!row || !row->callback_references)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT callback context has no held reference");
    --row->callback_references; return true;
}
frontend_remote_configs *frontend_remote_configs_create(qa_frontend *f,frontend_config_store *manager,qa_error *error)
{
    if (!f || !manager) return fail(error,QA_ERROR_ARGUMENT,"CLIENT configuration needs its actual frontend owner"),NULL;
    frontend_remote_configs *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Retaining CLIENT configuration roster"),NULL;
    owner->frontend=f; owner->manager=manager; return owner;
}
bool frontend_remote_configs_empty(const frontend_remote_configs *owner) { return !owner || !owner->rows; }
frontend_remote_config *frontend_remote_config_find(const frontend_remote_configs *owner,const qa_console *console)
{
    for (frontend_remote_config *row=owner?owner->rows:NULL;row;row=row->next)
        if (row->console==console) return row;
    return NULL;
}
bool frontend_remote_config_phase(const frontend_remote_configs *owner,const void *phase)
{
    for (const frontend_remote_config *row=owner?owner->rows:NULL;row;row=row->next) if (row==phase) return true;
    return false;
}
const frontend_config_files *frontend_remote_config_files(const frontend_remote_config *row)
{ return row?row->files:NULL; }
bool frontend_remote_config_read(const frontend_remote_config *row,frontend_remote_config_view *out)
{
    if (!row || !out || !row->registry || !row->cvars || !row->keys || row->running || row->imported || row->retargeting || !descriptor(row) ||
        frontend_client_registry_cvars(row->registry)!=row->cvars) return false;
    *out=(frontend_remote_config_view){row,descriptor(row),row->scope,row->console,row->cvars,
        row->q3_mouse,row->q3_view,row->movement_mouse,row->keys,row->physical_seat,row->movement,
        row->configured && row->released,row->published}; return true;
}
bool frontend_remote_config_current(const frontend_remote_config *row,const frontend_remote_config_view *view)
{
    frontend_remote_config_view current;
    return view && frontend_remote_config_read(row,&current) && current.owner==view->owner &&
        current.receiver==view->receiver && scope_equal(current.scope,view->scope) &&
        current.console==view->console && current.cvars==view->cvars && current.keys==view->keys &&
        current.q3_mouse==view->q3_mouse && current.q3_view==view->q3_view && current.movement_mouse==view->movement_mouse &&
        current.physical_seat==view->physical_seat && current.movement==view->movement &&
        current.ready==view->ready && current.published==view->published;
}
static bool local_route(const frontend_remote_config *row,const qa_launch_choices *choices,uint32_t *physical)
{
    for (size_t i=0;choices && i<choices->seat_count;++i)
        if (choices->seats[i].id==row->scope.seat && choices->seats[i].local && !choices->seats[i].bot) {
            *physical=(uint32_t)i; return row->physical_seat!=*physical;
        }
    return false;
}
bool frontend_remote_configs_local_routes(frontend_remote_configs *owner,qa_application *application,qa_error *error)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    if (!f || f->application!=application || f->options.dedicated || frontend_network_remote(f) ||
        f->capture || f->source_restoring || owner->restoring || !frontend_seat_callbacks_returned(f) ||
        qa_application_startup_pending(application))
        return fail(error,QA_ERROR_ARGUMENT,"Local CLIENT routing requires its returned published frontend boundary");
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(application));
    for (frontend_remote_config *row=owner->rows;row;row=row->next) {
        uint32_t physical;
        if (row->application!=application || !row->published || !local_route(row,choices,&physical)) continue;
        qa_application_startup_source actual;
        if (row->running || row->imported || row->retargeting || !row->configured || !row->released ||
            row->phase || row->input || row->staging || physical>=f->options.seats || !f->seats ||
            !f->seats[physical].input || !f->seats[physical].console ||
            qa_input_seat_ordinal(f->seats[physical].input)!=physical ||
            !qa_application_q3_client_configuration_read(application,row->scope.provider,row->scope.seat,&actual,error) ||
            actual.console!=row->console || actual.cvars!=row->cvars || !scope_equal(actual.scope,row->scope) ||
            !descriptor(row) || actual.descriptor->storage!=descriptor(row)->storage ||
            !qa_application_q3_client_configuration_unborrowed(application,&actual))
            return fail(error,QA_ERROR_ARGUMENT,"Local CLIENT routing retains another configuration or actual frontend lease");
        for (size_t i=0;i<frontend_native_q3_count(f);++i) {
            frontend_native_q3_view view;
            if (!frontend_native_q3_read(f,i,&view,error)) return false;
            if (view.receiver==row->scope.provider && view.launch_seat==row->scope.seat)
                return fail(error,QA_ERROR_ARGUMENT,"Local CLIENT routing retains its actual native frontend service");
        }
    }
    for (frontend_remote_config *row=owner->rows;row;row=row->next) {
        uint32_t physical;
        if (row->application==application && row->published && local_route(row,choices,&physical))
            row->physical_seat=physical;
    }
    return true;
}
bool frontend_remote_config_pending(const frontend_remote_config *row,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source)
{
    const qa_launch_instance *held=descriptor(row);
    return row && source && source->descriptor && held && !row->published && !row->imported && !row->retargeting &&
        row->application==application && row->candidate==candidate && row->console==source->console &&
        row->cvars==source->cvars && scope_equal(row->scope,source->scope) && held->storage==source->descriptor->storage;
}
bool frontend_remote_config_tuple(const frontend_remote_config *row,qa_application_startup_source *out)
{
    if (!row || !out || row->imported || row->retargeting || !descriptor(row) || !row->console ||
        !row->cvars || qa_console_cvars(row->console)!=row->cvars) return false;
    *out=(qa_application_startup_source){descriptor(row),row->scope,row->console,row->cvars,
        row->command,row->declaration_owner}; return true;
}
bool frontend_remote_config_refresh(frontend_remote_config *row,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_error *error)
{
    if (!source || !source->declaration_owner || !frontend_remote_config_pending(row,application,candidate,source))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT refresh changed its actual pending physical tuple");
    row->declaration_owner=source->declaration_owner; return true;
}
bool frontend_remote_config_registries(const frontend_remote_config *row,qa_application *application,
    const qa_application_startup_source *source,qa_cvars *namespaces[8],qa_console **hosted_game,qa_error *error)
{
    const qa_launch_instance *held=descriptor(row);
    if (!row || !application || !source || !source->descriptor || !namespaces || !hosted_game ||
        row->application!=application || row->imported || row->retargeting || !row->configured || !row->released ||
        !held || held->storage!=source->descriptor->storage ||
        !scope_equal(row->scope,source->scope) || row->console!=source->console || row->cvars!=source->cvars ||
        !row->registry || frontend_client_registry_cvars(row->registry)!=row->cvars ||
        !row->q3_mouse || !row->q3_view || !row->movement_mouse)
        return fail(error,QA_ERROR_ARGUMENT,"Host namespaces leave their retained CLIENT configuration");
    namespaces[QA_Q3_HOST_CVAR_CLIENT-1]=row->cvars;
    namespaces[QA_Q3_HOST_CVAR_MOUSE-1]=row->q3_mouse;
    namespaces[QA_Q3_HOST_CVAR_Q3_VIEW-1]=row->q3_view;
    namespaces[QA_Q3_HOST_CVAR_SELECTED_VIEW-1]=row->movement_mouse;
    *hosted_game=row->hosted?row->hosted_console:NULL;
    return true;
}
bool frontend_remote_config_bindings(const frontend_remote_config *row,qa_application *application,
    const qa_application_startup_source *source,const qa_input_seat *physical,qa_input_seat **out,qa_error *error)
{
    qa_cvars *namespaces[8]={0}; qa_console *hosted=NULL;
    if (!physical || !out) return fail(error,QA_ERROR_ARGUMENT,"CLIENT binding access needs its physical seat and output");
    if (!frontend_remote_config_registries(row,application,source,namespaces,&hosted,error)) return false;
    qa_frontend *f=row->owner->frontend;
    if (!f->seats || row->physical_seat>=f->options.seats ||
        f->seats[row->physical_seat].input!=physical || qa_input_seat_ordinal(physical)!=row->physical_seat)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT binding access lost its actual stable physical seat");
    qa_input_seat *input=NULL;
    if (hosted) {
        frontend_config_source *game=frontend_config_store_source(row->owner->manager,hosted);
        if (!game || frontend_config_source_seat_cvars(game,row->scope.seat)!=row->cvars)
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT bindings lost their actual hosted GAME-seat heap");
        input=frontend_config_source_input(game,row->scope.seat);
    } else input=row->published?f->seats[row->physical_seat].input:row->input;
    if (!input || qa_input_seat_ordinal(input)!=row->physical_seat)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT bindings have no matching physical dictionary owner");
    *out=input; return true;
}
qa_input_seat *frontend_remote_configs_candidate_input(const frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,unsigned ordinal)
{
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    if (!owner || !application || !choices || ordinal>=choices->seat_count || ordinal>=owner->frontend->options.seats) return NULL;
    for (const remote_input *input=owner->inputs;input;input=input->next)
        if (input->application==application && input->candidate==candidate && input->physical_seat==ordinal &&
            input->logical_seat==choices->seats[ordinal].id) return input->input;
    return NULL;
}
bool frontend_remote_configs_fresh_input(const frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate)
{
    if (!owner || !frontend_network_remote(owner->frontend)) return false;
    for (const frontend_remote_config *row=owner->rows;row;row=row->next)
        if (row->application==application && row->candidate==candidate && !row->published && !row->imported &&
            !row->hosted && row->input_fresh && !row->physical_seat && row->staging &&
            row->scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME) return true;
    return false;
}
bool frontend_remote_config_acquire(const frontend_remote_config *row,frontend_client_registry **out,qa_error *error)
{
    if (!row || !row->configured || !row->released || row->running || row->retargeting || !row->registry ||
        frontend_client_registry_cvars(row->registry)!=row->cvars)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT acquisition requires its completed actual preparation");
    return frontend_client_registry_retain(row->registry,out,error);
}
bool frontend_remote_config_reset_bindings(frontend_remote_config *row,int32_t controller,qa_error *error)
{
    frontend_remote_config_view view;
    qa_frontend *f=row?row->owner->frontend:NULL;
    uint32_t logical;
    if (!f || !frontend_remote_config_read(row,&view) || !view.ready || !view.published ||
        row->physical_seat>=f->options.seats || !f->seats ||
        !frontend_seat_launch_id_read(f,row->physical_seat,&logical) || logical!=row->scope.seat)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT Binding Reset lost its actual published graphical seat");
    return frontend_authored_bindings_reset(row->authored,f->seats[row->physical_seat].input,
        controller<0?0:controller,error);
}
qa_cvars *frontend_remote_config_cvar_owner(frontend_remote_config *row,const qa_command_context *command,const char *name)
{
    if (!cvar_context(row,command) || !name || !strcmp(name,"sv_cheats")) return NULL;
    frontend_config_source *game=row->hosted?frontend_config_store_source(row->owner->manager,row->hosted_console):NULL;
    qa_cvars *server=frontend_config_source_cvars(game);
    if (server && qa_cvars_find(server,name)) return server;
    if (qa_cvars_find(row->q3_mouse,name)) return row->q3_mouse;
    if (row->movement_mouse!=row->q3_mouse && qa_cvars_find(row->movement_mouse,name)) return row->movement_mouse;
    return row->cvars;
}
qa_cvars *frontend_remote_config_visible(frontend_remote_config *row,const qa_command_context *command,size_t index)
{
    if (!cvar_context(row,command)) return NULL;
    frontend_config_source *game=row->hosted?frontend_config_store_source(row->owner->manager,row->hosted_console):NULL;
    qa_cvars *server=frontend_config_source_cvars(game);
    if (server) { if (!index) return server; --index; }
    if (!index) return row->q3_mouse;
    if (row->movement_mouse!=row->q3_mouse) { if (index==1) return row->movement_mouse; --index; }
    return index==1?row->cvars:NULL;
}
static qa_input_seat *binding_seat(void *context,const qa_command_context *command)
{
    frontend_remote_config *row=context;
    if (!active(row,command)) return NULL;
    if (row->hosted) {
        frontend_config_source *game=frontend_config_store_source(row->owner->manager,row->hosted_console);
        if (!game || frontend_config_source_seat_cvars(game,row->scope.seat)!=row->cvars) return NULL;
        return frontend_config_source_input(game,row->scope.seat);
    }
    qa_frontend *f=row->owner->frontend;
    return row->published?f->seats && row->physical_seat<f->options.seats?f->seats[row->physical_seat].input:NULL:row->input;
}
typedef struct archive_filter {
    frontend_remote_config *row;
    const qa_command_context *command;
} archive_filter;
static bool archive_visible(void *context,const qa_cvars *registry,const qa_cvar_view *value)
{
    archive_filter *filter=context;
    return frontend_config_store_cvar_owner(filter->row->owner->manager,filter->row->console,
        filter->command,value->name)==registry;
}
static bool config_command(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_remote_config *row=context;
    if (!active(row,&command->context)) return fail(error,QA_ERROR_ARGUMENT,"CLIENT configuration command lost its actual seat");
    if (!strcmp(command->argv[0],"condump")) {
        if (command->argc!=2) { print(row,"condump <filename>\n"); return true; }
        qa_frontend *f=row->owner->frontend;
        qa_seat_console *console=f->seats[row->physical_seat].console;
        return console && frontend_config_files_dump(row->files,command->argv[1],&command->context,
            qa_seat_console_buffer(console),error);
    }
    if (command->argc>2) { print(row,"writeconfig [filename]\n"); return true; }
    const char *requested=command->argc==2?command->argv[1]:"q3config.cfg";
    size_t length=strlen(requested); bool suffix=length>=4 && !strcmp(requested+length-4,".cfg");
    if (length>SIZE_MAX-5) return fail(error,QA_ERROR_MEMORY,"CLIENT configuration filename exceeds storage");
    char *path=malloc(length+(suffix?1:5));
    if (!path) return fail(error,QA_ERROR_MEMORY,"Retaining CLIENT configuration filename");
    memcpy(path,requested,length); memcpy(path+length,suffix?"":".cfg",suffix?1:5);
    qa_buffer text={0}; qa_input_seat *input=binding_seat(row,&command->context);
    bool ok=input && qa_input_bindings_config(input,false,&text,error);
    archive_filter filter={row,&command->context};
    for (size_t i=0;ok;++i) {
        qa_cvars *registry=frontend_config_store_visible_cvars(row->owner->manager,row->console,&command->context,i);
        if (!registry) break;
        qa_buffer rows={0}; ok=frontend_config_store_config_filtered(row->owner->manager,row->application,
            row->console,&command->context,registry,archive_visible,&filter,&rows,error);
        if (ok && rows.size>SIZE_MAX-text.size-1) ok=fail(error,QA_ERROR_MEMORY,"CLIENT configuration text exceeds storage");
        uint8_t *next=ok?realloc(text.data,text.size+rows.size+1):NULL;
        if (ok && !next) ok=fail(error,QA_ERROR_MEMORY,"Retaining CLIENT configuration text");
        if (ok) { text.data=next; if (rows.size) memcpy(text.data+text.size,rows.data,rows.size);
            text.size+=rows.size; text.data[text.size]=0; }
        qa_buffer_free(&rows);
    }
    if (ok) ok=frontend_config_files_write_config_text(row->files,path,&command->context,(qa_bytes){text.data,text.size},error);
    qa_buffer_free(&text); free(path); return ok;
}
static bool install_commands(frontend_remote_config *row,qa_error *error)
{
    if (!row->bindings) row->bindings=qa_input_console_create(&(qa_input_console_options){.console=row->console,.owner=row->scope.provider,
        .user=row,.seat=binding_seat,.print=print},error);
    if (!row->bindings) return false;
    if (!row->write_registered) row->write_registered=qa_console_register_owned(row->console,"writeconfig","Save the actual CLIENT configuration",
        row->scope.provider,row->scope.provider,true,config_command,row,error);
    if (!row->write_registered) return false;
    if (!row->dump_registered) row->dump_registered=qa_console_register_owned(row->console,"condump","Dump the actual CLIENT console",
        row->scope.provider,row->scope.provider,true,config_command,row,error);
    return row->dump_registered;
}
static bool input_context(void *context,uint32_t ordinal,const qa_command_context *command,qa_error *error)
{
    remote_input *input=context;
    return (input && command && ordinal==input->physical_seat && command->origin==QA_COMMAND_SEAT &&
        command->owner==input->source_command.owner && command->session==input->source_command.session &&
        command->seat==input->logical_seat && command->dialect==QA_CONSOLE_Q3 &&
        qa_application_command_context_active(input->application,command)) ||
        fail(error,QA_ERROR_ARGUMENT,"CLIENT input left its actual authored and physical seat");
}
static bool script_read(void *context,frontend_script_scope scope,const char *name,const qa_command_context *command,
    qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_remote_config *row=context;
    return active(row,command) && frontend_config_files_read(row->files,scope,name,command,bytes,lease,error);
}
static void script_release(void *context,void *lease)
{ frontend_remote_config *row=context; frontend_config_files_release(row->files,lease); }
static bool apply_defaults(void *context,qa_error *error)
{
    frontend_remote_config *row=context;
    return frontend_authored_bindings_ready(row->authored) ||
        frontend_authored_bindings_defaults(row->authored,row->input,(qa_console_dialect)row->movement,error);
}
static bool selected_defaults(frontend_remote_config *row,bool seed,qa_error *error)
{
    qa_strings *strings=qa_session_strings(qa_application_session(row->application));
    frontend_config_weapon_catalog catalog;
    if (!frontend_config_weapon_defaults(row->application,row->candidate,
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=row->scope.seat},strings,&catalog,error)) return false;
    return seed?frontend_authored_bindings_seed(row->authored,(qa_console_dialect)row->movement,strings,
        catalog.items,catalog.count,error):frontend_authored_bindings_select(row->authored,row->input,
        (qa_console_dialect)row->movement,strings,catalog.items,catalog.count,0,error);
}
static frontend_remote_config *same_receiver_previous(const frontend_remote_config *fresh)
{
    const qa_launch_instance *selected=descriptor(fresh);
    const qa_launch_instance *previous=selected?qa_launch_snapshot_find(qa_application_launch(fresh->application),
        selected->selection.instance):NULL;
    for (frontend_remote_config *row=fresh->owner->rows;previous && row;row=row->next) {
        const qa_launch_instance *held=descriptor(row);
        if (row!=fresh && row->application==fresh->application && row->published && !row->imported &&
            row->configured && row->released && !row->phase && !row->running && held &&
            held->storage==previous->storage && held->state==previous->state &&
            scope_equal(row->scope,fresh->scope) && row->physical_seat==fresh->physical_seat) return row;
    }
    return NULL;
}
static bool previous_seat(const frontend_remote_config *fresh,frontend_remote_config **out,qa_error *error)
{
    *out=NULL;
    qa_frontend *f=fresh->owner->frontend;
    if (!frontend_network_remote(f) || fresh->physical_seat) {
        *out=same_receiver_previous(fresh); return true;
    }
    const qa_launch_snapshot *published=qa_application_launch(fresh->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(published);
    if (!choices || !choices->seat_count || fresh->scope.seat!=choices->seats[0].id) return true;
    frontend_remote_config_view view; bool present=false;
    if (!frontend_network_client_previous_configuration_read(f,choices->seats[0].id,&view,&present,error)) return false;
    if (!present) return true;
    frontend_remote_config *old=fresh->owner->rows;
    while (old && old!=view.owner) old=old->next;
    if (!old || old==fresh || old->application!=fresh->application || old->hosted ||
        view.physical_seat!=fresh->physical_seat || view.scope.seat!=choices->seats[0].id ||
        !view.ready || !view.published || !frontend_remote_config_current(old,&view) ||
        !old->authored || !frontend_authored_bindings_completed(old->authored) ||
        !f->seats || !f->seats[fresh->physical_seat].input)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT archive lost its actual previous published input profile");
    *out=old; return true;
}
static bool apply_archive(void *context,qa_error *error)
{
    frontend_remote_config *row=context;
    qa_application_startup_source tuple;
    if (!frontend_remote_config_tuple(row,&tuple))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT archive lost its actual linked physical tuple");
    frontend_config_store *manager=row->owner->manager;
    bool shared=row->physical_seat==0;
    if (!frontend_config_store_apply_archive(manager,row->application,row->candidate,&tuple,row->cvars,&row->archive,shared,error) ||
        !frontend_config_store_apply_archive(manager,row->application,row->candidate,&tuple,row->q3_mouse,&row->mouse_archive,shared,error) ||
        !frontend_config_store_apply_archive(manager,row->application,row->candidate,&tuple,row->q3_view,&row->view_archive,shared,error) ||
        (row->movement_mouse!=row->q3_view && !frontend_config_store_apply_archive(manager,row->application,row->candidate,
            &tuple,row->movement_mouse,&row->movement_archive,shared,error))) return false;
    if (shared && !frontend_config_store_apply_shared_archive(manager,row->application,row->candidate,&tuple,error)) return false;
    frontend_remote_config *old=NULL;
    if (!previous_seat(row,&old,error)) return false;
    if (old) {
        qa_frontend *f=row->owner->frontend;
        qa_input_command_tuning tuning;
        qa_input_seat *live=f->seats[row->physical_seat].input;
        if (!frontend_authored_bindings_restore_previous(row->authored,old->authored,live,row->input,error) ||
            !qa_input_settings_read_routed(old->q3_mouse,old->movement_mouse,old->movement,&tuning,error) ||
            !qa_input_mouse_settings_write(row->q3_mouse,&tuning.mouse,error)) return false;
        const qa_cvar_view *run=qa_cvars_find(old->q3_mouse,"cl_run");
        return !run || qa_cvars_set_flags(row->q3_mouse,"cl_run",run->value,QA_CVAR_ARCHIVE,error);
    }
    if (row->found) {
        if (!qa_input_seat_replace_bindings(row->input,row->settings.bindings,row->settings.binding_count,error) ||
            !qa_input_mouse_settings_write(row->q3_mouse,&row->settings.mouse,error)) return false;
        *qa_input_seat_gamepad_tuning(row->input)=row->settings.gamepad;
        if (row->settings.has_always_run && !qa_cvars_set_flags(row->q3_mouse,"cl_run",
            row->settings.always_run?"1":"0",QA_CVAR_ARCHIVE,error)) return false;
        frontend_authored_bindings_profile(row->authored);
    }
    return true;
}
static bool apply_launch(void *context,qa_error *error)
{ (void)error; ((frontend_remote_config *)context)->configured=true; return true; }
static bool replay(void *context,qa_error *error)
{
    frontend_remote_config *row=context;
    return row->physical_seat || qa_application_startup_replay_variables(row->application,row->console,&row->command,error);
}
static bool clone_input(frontend_remote_config *row,qa_input_seat *live,qa_error *error)
{
    for (remote_input *shared=row->owner->inputs;shared;shared=shared->next) {
        if (shared->application!=row->application || shared->candidate!=row->candidate ||
            shared->physical_seat!=row->physical_seat) continue;
        if (shared->logical_seat!=row->scope.seat || shared->movement!=row->movement || shared->ready ||
            shared->references==SIZE_MAX)
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT rows disagree on their actual staged physical input");
        ++shared->references; row->staging=shared; row->input=shared->input; return true;
    }
    remote_input *shared=calloc(1,sizeof(*shared));
    if (!shared) return fail(error,QA_ERROR_MEMORY,"Retaining the actual CLIENT staging input owner");
    shared->owner=row->owner; shared->application=row->application; shared->candidate=row->candidate;
    shared->source_command=row->command; shared->physical_seat=row->physical_seat;
    shared->logical_seat=row->scope.seat; shared->movement=row->movement; shared->references=1;
    shared->next=row->owner->inputs; row->owner->inputs=shared; row->staging=shared;
    qa_input_seat_options options={.context=row->command,.console=row->console,.cvars=row->q3_mouse,
        .gamepad=*qa_input_seat_gamepad_tuning(live),.seat=row->physical_seat,
        .context_ready=input_context,.context_user=shared};
    row->input=shared->input=qa_input_seat_create(&options,error);
    size_t count=qa_input_seat_binding_count(live);
    if (!row->input || count>SIZE_MAX/sizeof(qa_input_binding)) return false;
    qa_input_binding *bindings=malloc(count?count*sizeof(*bindings):1);
    if (!bindings) return fail(error,QA_ERROR_MEMORY,"Retaining actual CLIENT logical input continuation");
    for (size_t i=0;i<count;++i) bindings[i]=*qa_input_seat_binding_at(live,i);
    bool ok=qa_input_seat_replace_bindings(row->input,bindings,count,error); free(bindings); return ok;
}
static void release_input(frontend_remote_config *row)
{
    remote_input *shared=row->staging;
    row->staging=NULL; row->input=NULL;
    if (!shared || --shared->references) return;
    remote_input **link=&shared->owner->inputs;
    while (*link!=shared) link=&(*link)->next;
    *link=shared->next; qa_input_seat_destroy(shared->input); free(shared);
}
static frontend_remote_config *previous(const frontend_remote_configs *owner,qa_application *app,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *fresh)
{
    const qa_launch_snapshot *published=qa_application_launch(app);
    const qa_launch_instance *old=qa_launch_snapshot_find(published,fresh->descriptor->selection.instance);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    if (!old || !frontend_config_store_same_profile(old,fresh->descriptor)) return NULL;
    for (frontend_remote_config *row=owner->rows;row;row=row->next) {
        const qa_launch_instance *held=descriptor(row);
        if (row->application!=app || !row->published || row->imported || !row->configured || !row->released ||
            row->running || row->phase || !held || held->storage!=old->storage || held->state!=old->state ||
            !scope_equal(row->scope,fresh->scope) || !choices || row->physical_seat>=choices->seat_count ||
            choices->seats[row->physical_seat].id!=row->scope.seat) continue;
        const qa_launch_binding *binding=qa_launch_binding_for(choices,
            (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=row->scope.seat},QA_ROLE_MOVEMENT,"");
        const qa_launch_instance *movement=binding?qa_launch_snapshot_find(candidate,binding->instance):NULL;
        if (movement && (qa_movement_kind)movement->selection.clock.kind==row->movement) return row;
    }
    return NULL;
}
bool frontend_remote_config_program_source(const frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *fresh,
    qa_application_startup_source *out,bool *found,qa_error *error)
{
    if (!owner || !application || !candidate || !fresh || !fresh->descriptor || !out || !found)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT continuation needs its actual fresh tuple");
    *found=false;
    frontend_remote_config *row=previous(owner,application,candidate,fresh);
    if (!row) return true;
    qa_application_console_scope scope;
    if (!qa_application_console_scope_read(application,row->console,&scope) || !scope_equal(scope,row->scope) ||
        qa_console_cvars(row->console)!=row->cvars || !qa_console_idle(row->console) || !qa_cvars_observer_idle(row->cvars))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT continuation lost its published physical console");
    qa_command_context command=row->command;
    command.registry=command.generation=0; command.actor=(qa_actor_id){0};
    *out=(qa_application_startup_source){descriptor(row),row->scope,row->console,row->cvars,command,row->declaration_owner};
    *found=true; return true;
}
static bool copy_registry(qa_cvars *destination,const qa_cvars *source,qa_error *error)
{
    qa_buffer bytes={0}; qa_cvars_restore *ticket=NULL;
    bool ok=qa_cvars_save_capture(source,&bytes,error) &&
        qa_cvars_save_prepare(destination,(qa_bytes){bytes.data,bytes.size},&ticket,error) && qa_cvars_save_commit(ticket,error);
    if (!ok) qa_cvars_save_abort(ticket);
    qa_buffer_free(&bytes); return ok;
}
bool frontend_remote_config_prepare(frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,void **out,qa_error *error)
{
    if (!owner || !application || !candidate || !source || !source->descriptor || !out || *out ||
        !source->console || !source->cvars || !source->scope.provider ||
        (source->scope.kind!=QA_APPLICATION_CONSOLE_Q3_CGAME && source->scope.kind!=QA_APPLICATION_CONSOLE_Q3_UI) ||
        source->command.dialect!=QA_CONSOLE_Q3 || source->command.origin!=QA_COMMAND_SEAT ||
        source->command.seat!=source->scope.seat || source->command.owner!=source->scope.provider ||
        frontend_remote_config_find(owner,source->console))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT preparation needs its fresh physical receiver tuple");
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    size_t ordinal=0;
    while (choices && ordinal<choices->seat_count && choices->seats[ordinal].id!=source->scope.seat) ++ordinal;
    qa_frontend *f=owner->frontend;
    if (!choices || ordinal>=choices->seat_count || ordinal>=f->options.seats || !f->seats || f->options.dedicated)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT preparation lacks its actual graphical authored seat");
    frontend_remote_config *row=calloc(1,sizeof(*row));
    if (!row) return fail(error,QA_ERROR_MEMORY,"Retaining actual CLIENT configuration");
    row->owner=owner; row->application=application; row->candidate=candidate;
    row->scope=source->scope; row->console=source->console; row->command=source->command;
    row->declaration_owner=source->declaration_owner; row->physical_seat=(uint32_t)ordinal;
    /* Link before a heap handoff: constructor failure must leave the taken
     * physical owner reachable until the core enters checked retirement. */
    if (!qa_launch_instance_retain_metadata(source->descriptor,&row->metadata,error)) { free(row); return false; }
    row->next=owner->rows; owner->rows=row; *out=row;
    const qa_launch_binding *movement=qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=row->scope.seat},QA_ROLE_MOVEMENT,"");
    const qa_launch_instance *selected=movement?qa_launch_snapshot_find(candidate,movement->instance):NULL;
    if (!selected || selected->selection.clock.kind>QA_CLOCK_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT input lacks its actual selected movement source");
    row->movement=(qa_movement_kind)selected->selection.clock.kind;
    const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *game=entities?qa_launch_snapshot_find(candidate,entities->instance):NULL;
    qa_console *game_console=NULL; qa_cvars *game_cvars=NULL; qa_command_context game_command;
    if (game && !qa_application_startup_source_read(application,candidate,game,&game_console,&game_cvars,&game_command,error)) return false;
    frontend_config_source *hosted=game_console?frontend_config_store_source(owner->manager,game_console):NULL;
    row->hosted=!frontend_network_remote(f) && hosted && frontend_config_source_cvars(hosted)==game_cvars &&
        frontend_config_source_scope(hosted).kind==QA_APPLICATION_CONSOLE_Q3_GAME;
    frontend_remote_config *old=previous(owner,application,candidate,source);
    if (old && old->hosted!=row->hosted) old=NULL;
    row->input_fresh=!row->hosted && !old;
    if (row->hosted) {
        row->hosted_console=game_console;
        if (!frontend_config_source_acquire_seat_registry(hosted,row->scope.seat,&row->registry,error)) return false;
        row->cvars=frontend_client_registry_cvars(row->registry);
        row->keys=frontend_config_source_keys(hosted);
        if (!row->keys || !frontend_key_profile_retain(row->keys,error)) { row->keys=NULL; return false; }
        row->files=frontend_key_profile_files(row->keys);
        if (!qa_application_q3_client_configuration_bind_cvars(application,source,row->cvars,error)) return false;
    } else {
        if (!qa_application_q3_client_configuration_take_cvars(application,source,&row->owned,error)) return false;
        row->cvars=row->owned;
        qa_q3_product_policy policy;
        qa_catalog *catalog=qa_launch_instance_catalog(source->descriptor);
        const qa_product *product=qa_catalog_product(catalog,source->descriptor->selection.product);
        if (!product || product->family!=QA_GAME_Q3 || !qa_application_q3_product_policy_read(application,&policy) ||
            !policy.restriction_resolved || policy.filesystem_restricted!=qa_catalog_q3_restricted(catalog) ||
            !qa_q3_product_policy_register_source(&policy,row->cvars,row->declaration_owner,error) ||
            !qa_source_frame_time_register(row->cvars,row->declaration_owner,error) ||
            !frontend_config_userinfo_register(row->cvars,row->scope.seat,
                f->options.character_model?f->options.character_model:"sarge",error)) return false;
        if (old && (!copy_registry(row->cvars,old->cvars,error) ||
            !qa_q3_product_policy_register_source(&policy,row->cvars,row->declaration_owner,error))) return false;
        frontend_client_registry_context callback={row,retain_context,release_context};
        if (!frontend_client_registry_create(f,source->descriptor,row->scope.seat,&row->owned,&callback,&row->registry,error)) return false;
        if (old) {
            if (!frontend_config_files_clone(old->files,&row->files,error) ||
                !frontend_keys_carry(f->keys,old->keys,row->files,row->cvars,&row->keys,error)) return false;
        } else {
            row->files=frontend_config_files_create(catalog,product->id,
                frontend_global_settings_storage_user_store(f->global_settings_storage),
                frontend_global_settings_storage_device_store(f->global_settings_storage),error);
            if (!row->files || !frontend_keys_prepare(f->keys,row->files,row->cvars,&policy,false,&row->keys,error)) return false;
        }
        if (!row->keys ||
            !frontend_key_profile_scope(row->keys,row->scope,row->cvars,error)) return false;
        char seat[16]; snprintf(seat,sizeof(seat),"%" PRIu32,row->scope.seat);
        const char *archive_owner[]={"client",product->key,source->descriptor->selection.implementation,seat};
        if (!old && !qa_settings_load_cvars(frontend_config_files_store(row->files,false),archive_owner,4,
            QA_CONSOLE_Q3,&row->archive,error)) return false;
    }
    if (row->hosted) {
        row->q3_mouse=row->q3_view=row->movement_mouse=frontend_config_source_mouse_cvars(hosted,row->scope.seat);
        if (!row->q3_mouse) return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT lacks its actual GAME-seat input settings");
    } else {
        row->q3_mouse=settings_registry(row,QA_CONSOLE_Q3,error);
        row->q3_view=settings_registry(row,QA_CONSOLE_Q3,error);
        row->movement_mouse=row->movement==QA_MOVEMENT_Q3?row->q3_view:settings_registry(row,(qa_console_dialect)row->movement,error);
        if (!row->q3_mouse || !row->q3_view || !row->movement_mouse ||
            !qa_input_mouse_settings_register(row->q3_mouse,row->movement,error) ||
            !qa_input_movement_settings_register(row->q3_view,QA_MOVEMENT_Q3,error) ||
            (row->movement_mouse!=row->q3_view && !qa_input_movement_settings_register(row->movement_mouse,row->movement,error))) return false;
        if (old && (!copy_registry(row->q3_mouse,old->q3_mouse,error) ||
            !copy_registry(row->q3_view,old->q3_view,error) ||
            (row->movement_mouse!=row->q3_view && !copy_registry(row->movement_mouse,old->movement_mouse,error)))) return false;
    }
    if (!qa_application_capture_command_context(application,&row->command,&row->command,error)) return false;
    row->input=frontend_config_store_candidate_input(owner->manager,application,candidate,(unsigned)ordinal);
    if (!row->input && !clone_input(row,f->seats[ordinal].input,error)) return false;
    if (old) {
        if (!frontend_authored_bindings_clone(old->authored,&row->authored,error)) return false;
    } else if (row->hosted) {
        if (!frontend_config_source_clone_bindings(hosted,row->scope.seat,&row->authored,error)) return false;
    } else row->authored=frontend_authored_bindings_create(error);
    if (!row->authored) return false;
    if (!old && !row->hosted && !selected_defaults(row,true,error)) return false;
    if (!f->input_config && !frontend_input_profile_bind_store(f,frontend_config_files_catalog(row->files),
        frontend_config_files_product(row->files),frontend_config_files_store(row->files,false),error)) return false;
    qa_application_startup_source linked;
    if (!frontend_remote_config_tuple(row,&linked) ||
        !frontend_config_store_shared_begin(owner->manager,application,candidate,&linked,error)) return false;
    if (!install_commands(row,error)) return false;
    if (row->hosted) {
        row->hosted_pending=!frontend_authored_bindings_completed(row->authored);
        row->configured=!row->hosted_pending; row->released=true; return true;
    }
    if (old) {
        row->configured=row->released=true; return true;
    }
    row->secondary_pending=row->physical_seat!=0;
    if (!row->secondary_pending && !qa_input_seat_replace_bindings(row->input,NULL,0,error)) return false;
    qa_settings_store input_store=frontend_config_store_input_store(owner->manager);
    if (!input_store.vfs || !input_store.mount)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT configuration lacks its retained input ConfigStore");
    char logical[16],path[64]; snprintf(logical,sizeof(logical),"%" PRIu32,row->scope.seat);
    snprintf(path,sizeof(path),"input/seat-%" PRIu64 ".json",(uint64_t)row->scope.seat+1);
    const char *mouse_owner[]={"input","q3",logical};
    const char *dialects[]={"q1-netquake","q1-quakeworld","q2-classic","q2-rerelease","q3"};
    const char *view_owner[]={"movement","q3"};
    const char *movement_owner[]={"movement",dialects[row->movement]};
    if (!qa_settings_load_cvars(input_store,mouse_owner,3,QA_CONSOLE_Q3,&row->mouse_archive,error) ||
        !qa_settings_load_cvars(input_store,view_owner,2,QA_CONSOLE_Q3,&row->view_archive,error) ||
        (row->movement_mouse!=row->q3_view && !qa_settings_load_cvars(input_store,movement_owner,2,
            (qa_console_dialect)row->movement,&row->movement_archive,error)) ||
        !qa_settings_load_seat(input_store,path,&row->settings,&row->found,error)) return false;
    bool safe=false;
    if (!qa_application_startup_q3_safe_mode(application,source->descriptor,row->console,&safe,error)) return false;
    frontend_startup_config_options options={.command=row->command,.safe_mode=safe,.seat_scope=row->secondary_pending,.context=row,
        .read=script_read,.release=script_release,.apply_defaults=apply_defaults,.apply_archive=apply_archive,
        .apply_launch=apply_launch,.replay_startup_variables=replay};
    row->phase=frontend_startup_config_create(&options,error);
    return row->phase!=NULL;
}
bool frontend_remote_config_advance(frontend_remote_config *row,qa_console *console,bool *complete,qa_error *error)
{
    if (!row || row->console!=console || !complete || row->running)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT frame lacks its retained physical preparation");
    *complete=false;
    if (row->hosted_pending) {
        frontend_config_source *game=frontend_config_store_source(row->owner->manager,row->hosted_console);
        frontend_authored_bindings *authored=NULL;
        if (!game || !frontend_config_source_clone_bindings(game,row->scope.seat,&authored,error)) return false;
        if (!frontend_authored_bindings_completed(authored)) {
            frontend_authored_bindings_destroy(authored);
            return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT precedes its actual completed GAME seat configuration");
        }
        frontend_authored_bindings_destroy(row->authored); row->authored=authored;
        row->hosted_pending=false; row->configured=true;
    }
    if (row->released) { *complete=row->configured; return row->configured; }
    if (!row->initial_variables) {
        if (!replay(row,error)) return false;
        row->initial_variables=true;
    }
    if (row->secondary_pending) {
        frontend_remote_config *primary=row->owner->rows;
        while (primary && (primary==row || primary->application!=row->application || primary->candidate!=row->candidate ||
            primary->scope.provider!=row->scope.provider || primary->scope.kind!=row->scope.kind || primary->physical_seat))
            primary=primary->next;
        if (!primary || !primary->configured || !frontend_authored_bindings_completed(primary->authored))
            return fail(error,QA_ERROR_ARGUMENT,"Secondary CLIENT scripts precede their actual primary authored defaults");
        if (!frontend_authored_bindings_secondary(row->authored,primary->authored,row->input,(qa_console_dialect)row->movement,error)) return false;
        frontend_remote_config *old=NULL;
        if (!previous_seat(row,&old,error) || (old && !selected_defaults(row,false,error))) return false;
        row->secondary_pending=false;
    }
    row->running=true; bool ok=frontend_startup_config_advance(row->phase,console,complete,error); row->running=false;
    if (row->failure.code!=QA_OK) { if (error) *error=row->failure; return false; }
    if (ok && *complete) frontend_authored_bindings_finish(row->authored);
    return ok;
}
bool frontend_remote_config_release_phase(frontend_remote_config *row,qa_error *error)
{
    if (!row || row->running || !frontend_startup_config_destroy(row->phase,error)) return false;
    row->phase=NULL; row->released=true; return true;
}
bool frontend_remote_config_script_read(frontend_remote_config *row,const qa_command_context *command,
    const char *name,qa_bytes *bytes,void **lease,qa_error *error)
{
    if (!active(row,command)) return fail(error,QA_ERROR_ARGUMENT,"CLIENT script lost its actual source context");
    return row->phase?frontend_startup_config_read(row->phase,command,name,bytes,lease,error):
        frontend_config_files_console_read(row->files,name,command,bytes,lease,error);
}
void frontend_remote_config_script_release(frontend_remote_config *row,void *lease)
{ if (row->phase) frontend_startup_config_release(row->phase,lease); else script_release(row,lease); }
void frontend_remote_config_script_complete(frontend_remote_config *row,const qa_command_context *command,const char *name,bool success)
{ if (row && row->phase) frontend_startup_config_script_complete(row->phase,command,name,success); }
bool frontend_remote_config_allow(frontend_remote_config *row,const qa_command_invocation *command)
{
    if (!row || !active(row,&command->context) || row->failure.code!=QA_OK ||
        !frontend_authored_bindings_observe(row->authored,command,&row->failure)) return false;
    if (!row->phase || !frontend_startup_config_restrict_shared(row->phase) || !command->argc) return true;
    const char *name=command->argv[0];
    if (equal(name,"cvar_restart")) { print(row,"Ignoring shared cvar restart in saved secondary-seat configuration.\n"); return false; }
    bool setter=equal(name,"set") || equal(name,"seta") || equal(name,"sets") || equal(name,"setu") ||
        equal(name,"toggle") || equal(name,"reset");
    const char *target=setter?(command->argc>1?command->argv[1]:NULL):name;
    if (!target) return true;
    qa_cvars *owner=frontend_config_store_cvar_owner(row->owner->manager,row->console,&command->context,target);
    if (!setter && (!owner || !qa_cvars_find(owner,target))) return true;
    if (owner==row->cvars || owner==row->q3_mouse || owner==row->movement_mouse) return true;
    print(row,"Ignoring shared cvar in saved secondary-seat configuration; use autoexec.cfg for shared overrides.\n");
    return false;
}
bool frontend_remote_configs_ready(frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,qa_error *error)
{
    for (remote_input *shared=owner?owner->inputs:NULL;shared;shared=shared->next) {
        if (shared->application!=application || shared->candidate!=candidate) continue;
        shared->ready=false; shared->publication_input=NULL; shared->publication_console=NULL;
        shared->publication_command=(qa_command_context){0};
    }
    for (frontend_remote_config *row=owner?owner->rows:NULL;row;row=row->next) {
        if (row->application!=application || row->published || row->candidate!=candidate) continue;
        if (!row->configured || row->running || row->failure.code!=QA_OK || row->imported || row->retargeting ||
            !frontend_authored_bindings_completed(row->authored) || !qa_console_idle(row->console) ||
            qa_console_pending(row->console) || !frontend_config_files_idle(row->files))
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT publication requires its completed actual configuration");
        remote_input *shared=row->staging;
        if (!shared) continue;
        if (shared->owner!=owner || shared->application!=application || shared->candidate!=candidate ||
            shared->input!=row->input || shared->physical_seat!=row->physical_seat ||
            shared->logical_seat!=row->scope.seat || shared->movement!=row->movement)
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT publication lost its actual shared staging input");
        if (shared->ready) continue;
        qa_frontend *f=owner->frontend;
        qa_input_seat *input=f->seats[row->physical_seat].input;
        qa_seat_console *console=f->seats[row->physical_seat].console;
        int32_t controller=f->input?qa_input_platform_controller(f->input,row->physical_seat):-1;
        if ((controller>=0 && !qa_input_seat_remap_controller(row->input,controller,error)) ||
            !qa_input_seat_configuration_ready(input,row->input,error)) return false;
        qa_command_context command=qa_input_seat_context(input);
        command.registry=command.generation=0; command.actor=(qa_actor_id){0};
        command.origin=QA_COMMAND_SEAT; command.seat=row->scope.seat; command.dialect=(qa_console_dialect)row->movement;
        if (!qa_input_seat_context_ready(input,&command,error) || !qa_seat_console_context_ready(console,&command,error)) return false;
        shared->publication_input=input; shared->publication_console=console;
        shared->publication_command=command; shared->ready=true;
    }
    return true;
}
void frontend_remote_configs_finish(frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,bool published)
{
    if (published) for (remote_input *shared=owner?owner->inputs:NULL;shared;shared=shared->next) {
        if (shared->application!=application || shared->candidate!=candidate) continue;
        qa_input_seat_context_publish(shared->publication_input,&shared->publication_command);
        qa_seat_console_context_publish(shared->publication_console,&shared->publication_command);
        qa_input_seat_configuration_publish(shared->publication_input,shared->input);
    }
    for (frontend_remote_config *row=owner?owner->rows:NULL;row;row=row->next) {
        if (row->application!=application || row->published || row->candidate!=candidate) continue;
        if (published && !row->retargeting) {
            release_input(row); row->published=true; row->candidate=NULL;
        }
    }
}
bool frontend_remote_config_preinit(frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_error *error)
{
    frontend_remote_config *row=source?frontend_remote_config_find(owner,source->console):NULL;
    const qa_launch_instance *held=descriptor(row);
    return (row && row->application==application && row->candidate==candidate &&
        source->descriptor && held->storage==source->descriptor->storage && scope_equal(row->scope,source->scope) &&
        row->cvars==source->cvars && row->configured && row->released && !row->running &&
        active(row,&source->command)) || fail(error,QA_ERROR_ARGUMENT,"CLIENT Init requires its completed retained preparation");
}
static bool destroy_row(frontend_remote_config *row,qa_error *error)
{
    if (row->running || (row->files && !frontend_config_files_idle(row->files)) ||
        !frontend_startup_config_destroy(row->phase,error)) return false;
    row->phase=NULL;
    if (!row->hosted && !frontend_client_registry_release_ready(row->registry,error)) return false;
    if (row->keys) {
        qa_cvars *bound=frontend_key_profile_registry(row->keys);
        if (!row->hosted && bound && (bound!=row->cvars || !frontend_key_profile_detach(row->keys,bound,error))) return false;
        if (!frontend_key_profile_release(row->keys,error)) return false;
        row->keys=NULL; row->files=NULL;
    } else if (row->files) {
        if (!frontend_config_files_destroy(row->files,error)) return false;
        row->files=NULL;
    }
    if (!frontend_client_registry_release(&row->registry,error)) return false;
    if (row->callback_references) return fail(error,QA_ERROR_ARGUMENT,"CLIENT callback context remains retained");
    qa_input_console_destroy(row->bindings);
    if (row->write_registered) qa_console_unregister(row->console,"writeconfig",row->scope.provider);
    if (row->dump_registered) qa_console_unregister(row->console,"condump",row->scope.provider);
    release_input(row);
    frontend_authored_bindings_destroy(row->authored);
    qa_cvars_destroy(row->owned);
    if (!row->hosted) {
        if (row->movement_mouse!=row->q3_view) qa_cvars_destroy(row->movement_mouse);
        qa_cvars_destroy(row->q3_view);
        qa_cvars_destroy(row->q3_mouse);
    }
    qa_cvar_archive_free(&row->archive); qa_cvar_archive_free(&row->mouse_archive);
    qa_cvar_archive_free(&row->view_archive); qa_cvar_archive_free(&row->movement_archive);
    qa_seat_settings_free(&row->settings);
    qa_launch_instance_lease_release(row->metadata); free(row->saved_instance); free(row->saved_registry); free(row); return true;
}
static bool retarget_origin(const frontend_remote_config *row,const qa_application_startup_source *source)
{
    const qa_application_startup_source *held=&row->retarget_origin;
    return row->retargeting && source && source->descriptor && held->descriptor &&
        source->descriptor->storage==held->descriptor->storage && source->console==held->console &&
        source->cvars==held->cvars && scope_equal(source->scope,held->scope) &&
        source->declaration_owner==held->declaration_owner;
}
bool frontend_remote_config_retire(frontend_remote_configs *owner,qa_application *application,
    const qa_application_startup_source *source,qa_error *error)
{
    frontend_remote_config **link=&owner->rows;
    while (*link && (*link)->console!=source->console) link=&(*link)->next;
    if (!*link) return true;
    frontend_remote_config *row=*link,*next=row->next;
    const qa_launch_instance *held=descriptor(row);
    if (row->application!=application || (!retarget_origin(row,source) &&
        (row->cvars!=source->cvars || !source->descriptor || !held ||
        held->storage!=source->descriptor->storage || !scope_equal(row->scope,source->scope))))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement names another retained physical source");
    if (!destroy_row(row,error)) return false;
    *link=next; return true;
}
bool frontend_remote_config_retire_hosted(frontend_remote_configs *owner,qa_application *application,
    const qa_application_startup_source *source,qa_error *error)
{
    if (!owner || !application || !source || !source->descriptor ||
        !qa_application_q3_client_configuration_retiring(application,source))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT cleanup requires its entered physical retirement loan");
    frontend_remote_config *row=frontend_remote_config_find(owner,source->console);
    if (!row) return true;
    const qa_launch_instance *held=descriptor(row);
    if (!row->hosted || row->application!=application || !held ||
        (!retarget_origin(row,source) && (held->storage!=source->descriptor->storage ||
        row->cvars!=source->cvars || !scope_equal(row->scope,source->scope))))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT cleanup names another physical configuration");
    if (!row->frontend_retired) {
        if (!row->keys || !frontend_source_retire_client_configuration(owner->frontend,application,source,row->keys,error)) return false;
        row->frontend_retired=true;
    }
    return frontend_remote_config_retire(owner,application,source,error);
}
bool frontend_remote_config_bind_hosted(frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *target,
    const qa_application_startup_source *backing,qa_cvars **out,qa_error *error)
{
    if (!owner || !application || !candidate || candidate!=qa_application_launch(application) ||
        !target || !target->descriptor || !target->console || !target->cvars || !out || *out ||
        !backing || !backing->descriptor || backing->scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME ||
        !backing->console || !backing->cvars || frontend_network_remote(owner->frontend))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT binding needs its actual new publication and GAME tuple");
    frontend_config_source *game=frontend_config_store_source(owner->manager,backing->console);
    qa_application_startup_source actual;
    if (!game || !frontend_config_source_primary(game) || !frontend_config_source_tuple(game,&actual) ||
        actual.descriptor->storage!=backing->descriptor->storage || actual.cvars!=backing->cvars ||
        !scope_equal(actual.scope,backing->scope))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT binding lost its exact configured GAME parent");
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    const qa_launch_instance *receiver=qa_launch_snapshot_find(candidate,target->descriptor->selection.instance);
    const qa_launch_instance *parent=qa_launch_snapshot_find(candidate,backing->descriptor->selection.instance);
    const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    size_t ordinal=0;
    while (choices && ordinal<choices->seat_count && choices->seats[ordinal].id!=target->scope.seat) ++ordinal;
    qa_frontend *f=owner->frontend;
    if (!receiver || receiver->storage!=target->descriptor->storage || !parent ||
        parent->storage!=backing->descriptor->storage || !entities || strcmp(entities->instance,parent->selection.instance) ||
        !choices || ordinal>=choices->seat_count || ordinal>=f->options.seats || !f->seats ||
        !f->seats[ordinal].input || (target->scope.kind!=QA_APPLICATION_CONSOLE_Q3_CGAME && target->scope.kind!=QA_APPLICATION_CONSOLE_Q3_UI))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT binding changed its genuine receiver or authored seat");
    frontend_remote_config *row=frontend_remote_config_find(owner,target->console);
    if (row && (!retarget_origin(row,target) || row->application!=application || row->candidate!=candidate ||
        row->hosted_console!=backing->console))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT binding already owns another retained transition");
    if (!row) {
        row=calloc(1,sizeof(*row));
        if (!row) return fail(error,QA_ERROR_MEMORY,"Retaining actual hosted CLIENT rebinding");
        row->owner=owner; row->application=application; row->candidate=candidate; row->scope=target->scope;
        row->console=target->console; row->command=target->command; row->declaration_owner=target->declaration_owner;
        row->hosted=true; row->hosted_console=backing->console; row->physical_seat=(uint32_t)ordinal;
        row->retargeting=row->frontend_retired=true; row->retarget_game_storage=parent->storage;
        if (!qa_launch_instance_retain_metadata(receiver,&row->metadata,error)) { free(row); return false; }
        row->retarget_origin=*target; row->retarget_origin.descriptor=descriptor(row);
        row->next=owner->rows; owner->rows=row;
    }
    const qa_launch_binding *movement=qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=target->scope.seat},QA_ROLE_MOVEMENT,"");
    const qa_launch_instance *selected=movement?qa_launch_snapshot_find(candidate,movement->instance):NULL;
    if (!selected || selected->selection.clock.kind>QA_CLOCK_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT rebinding lacks its selected movement source");
    row->movement=(qa_movement_kind)selected->selection.clock.kind;
    if (!row->registry && !frontend_config_source_acquire_seat_registry(game,row->scope.seat,&row->registry,error)) return false;
    row->cvars=frontend_client_registry_cvars(row->registry);
    frontend_key_profile *profile=frontend_config_source_keys(game);
    if (!profile || (row->keys && row->keys!=profile))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT rebinding lost its new GAME key profile");
    if (!row->keys) {
        if (!frontend_key_profile_retain(profile,error)) return false;
        row->keys=profile; row->files=frontend_key_profile_files(profile);
    }
    row->q3_mouse=row->q3_view=row->movement_mouse=frontend_config_source_mouse_cvars(game,row->scope.seat);
    if (!row->cvars || !row->files || !row->q3_mouse)
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT rebinding lacks actual GAME-seat settings");
    if (!row->authored && !frontend_config_source_clone_bindings(game,row->scope.seat,&row->authored,error)) return false;
    if (!frontend_authored_bindings_completed(row->authored))
        return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT rebinding precedes its completed GAME seat configuration");
    row->input=frontend_config_source_input(game,row->scope.seat);
    if (!row->input) return fail(error,QA_ERROR_ARGUMENT,"Hosted CLIENT binding lost its actual GAME input owner");
    if (!install_commands(row,error)) return false;
    row->configured=row->released=true; *out=row->cvars; return true;
}
void frontend_remote_config_publish_hosted(frontend_remote_configs *owner,qa_application *application,
    const qa_application_startup_source *source)
{
    frontend_remote_config *row=source?frontend_remote_config_find(owner,source->console):NULL;
    if (!row || !row->retargeting || row->application!=application || !source->descriptor ||
        descriptor(row)->storage!=source->descriptor->storage || !scope_equal(row->scope,source->scope) ||
        row->cvars!=source->cvars || qa_console_cvars(row->console)!=row->cvars || !row->configured || !row->released) return;
    row->command=source->command; row->declaration_owner=source->declaration_owner;
    release_input(row);
    row->published=true; row->candidate=NULL; row->retargeting=false;
    row->retarget_origin=(qa_application_startup_source){0}; row->retarget_game_storage=NULL;
}
bool frontend_remote_configs_retire_staged_parent(frontend_remote_configs *owner,qa_application *application,
    const qa_application_startup_source *source,qa_error *error)
{
    if (!owner || !application || !source || !source->descriptor ||
        source->scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME || !source->console)
        return fail(error,QA_ERROR_ARGUMENT,"Unadopted CLIENT cleanup needs its entered physical GAME parent");
    for (const frontend_remote_config *row=owner->rows;row;row=row->next) {
        if (row->application!=application || !row->retargeting || row->hosted_console!=source->console) continue;
        if (!row->frontend_retired || row->retarget_game_storage!=source->descriptor->storage || row->running || row->phase ||
            !qa_console_idle(row->console) || (row->cvars && qa_console_cvars(row->console)==row->cvars))
            return fail(error,QA_ERROR_ARGUMENT,"Unadopted CLIENT still owns an entered or physically bound namespace");
    }
    frontend_remote_config **link=&owner->rows;
    while (*link) {
        frontend_remote_config *row=*link;
        if (row->application!=application || !row->retargeting || row->hosted_console!=source->console) { link=&row->next; continue; }
        frontend_remote_config *next=row->next;
        if (!destroy_row(row,error)) return false;
        *link=next;
    }
    return true;
}
bool frontend_remote_configs_destroy(frontend_remote_configs *owner,qa_error *error)
{
    if (!owner) return true;
    while (owner->rows) {
        frontend_remote_config *row=owner->rows,*next=row->next;
        if (!destroy_row(row,error)) return false;
        owner->rows=next;
    }
    free(owner); return true;
}
void frontend_remote_configs_rebind(frontend_remote_configs *owner,qa_frontend *f,frontend_config_store *manager)
{ if (owner) { owner->frontend=f; owner->manager=manager; } }
bool frontend_remote_configs_visit(const frontend_remote_configs *owner,const qa_application_content_visitor *visitor,qa_error *error)
{
    for (const frontend_remote_config *row=owner?owner->rows:NULL;row;row=row->next) {
        const qa_launch_instance *held=descriptor(row);
        if (!held || !row->published || row->imported || row->retargeting || row->running || row->phase ||
            !frontend_config_files_visit(row->files,visitor,error) ||
            !visitor->pool(visitor->context,qa_catalog_resources(qa_launch_instance_catalog(held)),error) ||
            !visitor->catalog(visitor->context,qa_launch_instance_catalog(held),error) ||
            !visitor->pool(visitor->context,qa_vfs_resources(held->content),error) ||
            !visitor->view(visitor->context,held->content,error)) return false;
    }
    return true;
}
static bool save_input(frontend_remote_config *row,qa_error *error)
{
    qa_frontend *f=row->owner->frontend;
    qa_input_seat *input=f->seats[row->physical_seat].input;
    qa_console_history *history=qa_seat_console_history(f->seats[row->physical_seat].console);
    size_t count=qa_input_seat_binding_count(input),lines_count=qa_console_history_count(history);
    if (count>SIZE_MAX/sizeof(qa_input_binding) || lines_count>SIZE_MAX/sizeof(char *))
        return fail(error,QA_ERROR_MEMORY,"CLIENT input archive exceeds its actual storage bounds");
    qa_input_binding *bindings=malloc(count?count*sizeof(*bindings):1);
    char **lines=malloc(lines_count?lines_count*sizeof(*lines):1);
    if (!bindings || !lines) { free(bindings); free(lines); return fail(error,QA_ERROR_MEMORY,"Retaining CLIENT live input settings"); }
    for (size_t i=0;i<count;++i) {
        bindings[i]=*qa_input_seat_binding_at(input,i);
        if (bindings[i].input.kind==QA_PHYSICAL_BUTTON || bindings[i].input.kind==QA_PHYSICAL_AXIS) bindings[i].input.device=0;
    }
    for (size_t i=0;i<lines_count;++i) lines[i]=(char *)qa_console_history_at(history,i);
    qa_input_command_tuning tuning;
    bool ok=qa_input_settings_read_routed(row->q3_mouse,row->movement_mouse,row->movement,&tuning,error);
    qa_seat_settings settings=row->settings;
    if (ok) {
        settings.bindings=bindings; settings.binding_count=count;
        settings.history=lines; settings.history_count=lines_count;
        settings.mouse=tuning.mouse; settings.gamepad=*qa_input_seat_gamepad_tuning(input);
        settings.has_always_run=true; settings.always_run=tuning.view.always_run;
        qa_haptic_player *haptics=f->input?qa_input_platform_haptics(f->input,row->physical_seat):NULL;
        if (haptics) { settings.rumble=haptics->enabled; settings.rumble_strength=haptics->strength; }
        if (f->input && !qa_input_platform_selection(f->input,row->physical_seat,&settings.controller))
            ok=fail(error,QA_ERROR_ARGUMENT,"CLIENT input archive lost its actual controller selection");
        char logical[16],path[64]; snprintf(logical,sizeof(logical),"%" PRIu32,row->scope.seat);
        snprintf(path,sizeof(path),"input/seat-%" PRIu64 ".json",(uint64_t)row->scope.seat+1);
        const char *mouse_owner[]={"input","q3",logical};
        const char *dialects[]={"q1-netquake","q1-quakeworld","q2-classic","q2-rerelease","q3"};
        const char *view_owner[]={"movement","q3"};
        const char *movement_owner[]={"movement",dialects[row->movement]};
        qa_settings_store store=frontend_config_store_input_store(row->owner->manager);
        ok=ok && qa_settings_save_cvars(store,mouse_owner,3,row->q3_mouse,error) &&
            qa_settings_save_cvars(store,view_owner,2,row->q3_view,error) &&
            (row->movement_mouse==row->q3_view || qa_settings_save_cvars(store,movement_owner,2,row->movement_mouse,error)) &&
            qa_settings_save_seat(store,path,&settings,error);
    }
    free(bindings); free(lines); return ok;
}
bool frontend_remote_configs_save(frontend_remote_configs *owner,qa_application *application,qa_error *error)
{
    if (!owner || owner->restoring || !application)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT archive requires its installed configuration roster");
    for (frontend_remote_config *row=owner->rows;row;row=row->next) {
        if (row->application!=application || !row->published) continue;
        frontend_remote_config_view view; qa_application_startup_source actual;
        if (!frontend_remote_config_read(row,&view) || !view.ready || !frontend_config_files_idle(row->files) ||
            !qa_application_q3_client_configuration_read(application,row->scope.provider,row->scope.seat,&actual,error) ||
            actual.console!=row->console || actual.cvars!=row->cvars || !scope_equal(actual.scope,row->scope))
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT archive lost its current physical source owner");
        if (row->hosted) continue;
        const qa_product *product=qa_catalog_product(frontend_config_files_catalog(row->files),frontend_config_files_product(row->files));
        char logical[16]; snprintf(logical,sizeof(logical),"%" PRIu32,row->scope.seat);
        const char *archive[]={"client",product->key,descriptor(row)->selection.implementation,logical};
        if (!qa_settings_save_cvars(frontend_config_files_store(row->files,false),archive,4,row->cvars,error) ||
            !frontend_key_profile_save(row->keys,error)) return false;
    }
    if (frontend_network_remote(owner->frontend)) {
        uint32_t logical;
        frontend_remote_config_view view;
        if (!frontend_seat_launch_id_read(owner->frontend,0,&logical) ||
            !frontend_network_client_configuration(owner->frontend,logical,&view,error)) return false;
        frontend_remote_config *row=frontend_remote_config_find(owner,view.console);
        if (!row || !frontend_remote_config_current(row,&view))
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT input save lost its actual network configuration");
        return save_input(row,error);
    }
    return true;
}
static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=bytes->size;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if (count>io->input.size-io->offset) return fail(io->error,QA_ERROR_FORMAT,"CLIENT section exceeds its admitted extent");
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool settings_fields(frontend_remote_config *row,qa_source_save_io *io,qa_cvars **cvars,qa_console_dialect dialect)
{
    qa_buffer captured={0}; qa_bytes bytes={0}; bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    bool ok=true;
    if (writing) { ok=*cvars && qa_cvars_save_capture(*cvars,&captured,io->error); bytes=(qa_bytes){captured.data,captured.size}; }
    if (ok) ok=blob(io,&bytes) && bytes.size;
    if (ok && !writing) {
        *cvars=settings_registry(row,dialect,io->error); qa_cvars_restore *ticket=NULL;
        ok=*cvars && qa_cvars_save_prepare(*cvars,bytes,&ticket,io->error) && qa_cvars_save_commit(ticket,io->error);
        if (!ok) qa_cvars_save_abort(ticket);
    }
    qa_buffer_free(&captured); return ok;
}
static bool fields(frontend_remote_config *row,qa_source_save_io *io,frontend_keys *keys)
{
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    const qa_launch_instance *held=descriptor(row),*physical=NULL;
    char *name=writing?(char *)held->selection.instance:NULL;
    char *registry=NULL;
    qa_sha256_digest identity=writing?held->identity:(qa_sha256_digest){0};
    uint32_t scope=row->scope.kind,movement=row->movement,seat=row->scope.seat;
    uint64_t key=frontend_key_profile_id(row->keys);
    if (writing && (!frontend_client_registry_source(row->registry,&physical,&seat) || seat!=row->scope.seat)) return false;
    if (writing) registry=(char *)physical->selection.instance;
    bool ok=qa_source_save_owned_text(io,&name) && name && *name &&
        qa_source_save_bytes(io,&identity,sizeof(identity)) && qa_source_save_u32(io,&scope) &&
        (scope==QA_APPLICATION_CONSOLE_Q3_CGAME || scope==QA_APPLICATION_CONSOLE_Q3_UI) &&
        qa_source_save_u32(io,&row->scope.seat) && qa_source_save_u32(io,&row->physical_seat) &&
        row->physical_seat<row->owner->frontend->options.seats && qa_source_save_u32(io,&movement) && movement<=QA_MOVEMENT_Q3 &&
        qa_source_save_bool(io,&row->hosted) && qa_source_save_u64(io,&key) && key &&
        qa_source_save_owned_text(io,&registry) && registry && *registry;
    if (!writing) {
        row->saved_instance=name; row->saved_registry=registry; row->saved_identity=identity;
        row->scope.kind=(qa_application_console_kind)scope; row->movement=(qa_movement_kind)movement;
        row->keys=ok?frontend_keys_profile(keys,key):NULL;
        if (ok) ok=row->keys && frontend_key_profile_retain(row->keys,io->error);
        if (!ok) row->keys=NULL;
        else row->files=frontend_key_profile_files(row->keys);
    }
    if (ok && !row->hosted && strcmp(name,registry)) ok=false;
    if (ok && !row->hosted) {
        ok=settings_fields(row,io,&row->q3_mouse,QA_CONSOLE_Q3) && settings_fields(row,io,&row->q3_view,QA_CONSOLE_Q3);
        bool same=row->movement_mouse==row->q3_view;
        if (ok) ok=qa_source_save_bool(io,&same) && same==(row->movement==QA_MOVEMENT_Q3);
        if (ok && !writing && same) row->movement_mouse=row->q3_view;
        else if (ok && !same) ok=settings_fields(row,io,&row->movement_mouse,(qa_console_dialect)row->movement);
    }
    if (ok && !writing) { row->authored=frontend_authored_bindings_create(io->error); ok=row->authored!=NULL; }
    if (ok) ok=frontend_authored_bindings_fields(row->authored,io) && frontend_authored_bindings_completed(row->authored);
    return ok;
}
static bool header(qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','F','R','C'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFRC",4) && qa_source_save_count(io,count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX);
}
bool frontend_remote_configs_checkpoint(const frontend_remote_configs *owner,const qa_application_content_graph *graph,
    qa_buffer *out,qa_error *error)
{
    if (!owner || owner->restoring || !graph || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT capture needs its published configuration roster");
    size_t count=0;
    for (const frontend_remote_config *row=owner->rows;row;row=row->next) {
        frontend_remote_config_view view;
        if (!row->published || row->phase || row->staging || row->input ||
            !frontend_remote_config_read(row,&view) || !view.ready || !frontend_config_files_idle(row->files))
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT capture cannot omit an unfinished physical configuration");
        ++count;
    }
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,error) && header(&io,&count);
    for (frontend_remote_config *row=owner->rows;ok && row;row=row->next) ok=fields(row,&io,NULL);
    if (ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_remote_configs_restore(frontend_remote_configs *owner,qa_application *application,
    qa_application_content_graph *graph,frontend_keys *keys,qa_bytes bytes,qa_error *error)
{
    if (!owner || owner->rows || owner->restoring || !application || !graph || !keys)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT import needs the actual empty configuration roster");
    owner->restoring=true;
    qa_source_save_io io={0}; size_t count=0;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && header(&io,&count);
    frontend_remote_config **tail=&owner->rows;
    for (size_t i=0;ok && i<count;++i) {
        frontend_remote_config *row=calloc(1,sizeof(*row));
        if (!row) { ok=fail(error,QA_ERROR_MEMORY,"Retaining decoded CLIENT configuration"); break; }
        row->owner=owner; row->application=application; row->imported=true;
        row->published=row->configured=row->released=true;
        *tail=row; tail=&row->next;
        ok=fields(row,&io,keys);
        for (frontend_remote_config *previous=owner->rows;ok && previous!=row;previous=previous->next)
            if (!strcmp(previous->saved_instance,row->saved_instance) && previous->scope.seat==row->scope.seat) ok=false;
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK)) fail(error,QA_ERROR_FORMAT,"Invalid retained CLIENT configuration");
    return ok;
}
typedef struct restored_key_scope { const qa_application_startup_source *source; } restored_key_scope;
static bool resolve_key(void *context,const char *name,qa_actor_owner *owner,qa_error *error)
{
    const qa_application_startup_source *source=((restored_key_scope *)context)->source;
    if (strcmp(name,source->descriptor->selection.instance))
        return fail(error,QA_ERROR_FORMAT,"CLIENT key source differs from its saved receiver");
    *owner=source->scope.provider; return true;
}
static bool qualify_key(void *context,const qa_application_console_scope *scope,const qa_cvars *cvars,qa_error *error)
{
    const qa_application_startup_source *source=((restored_key_scope *)context)->source;
    return (scope_equal(*scope,source->scope) && cvars==source->cvars) ||
        fail(error,QA_ERROR_FORMAT,"CLIENT key registry differs from its canonical decoded owner");
}
bool frontend_remote_config_bind_restored(frontend_remote_configs *owner,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_error *error)
{
    if (!owner || !owner->restoring || !application || !candidate || !source || !source->descriptor || !source->console || !source->cvars)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT restore needs its genuine decoded factory tuple");
    frontend_remote_config *row=owner->rows;
    while (row && (!row->saved_instance || strcmp(row->saved_instance,source->descriptor->selection.instance) ||
        row->scope.seat!=source->scope.seat)) row=row->next;
    if (!row || row->application!=application || row->scope.kind!=source->scope.kind ||
        !qa_sha256_equal(&row->saved_identity,&source->descriptor->identity))
        return fail(error,QA_ERROR_FORMAT,"Decoded CLIENT configuration differs from its physical receiver");
    if (!row->imported) return (row->console==source->console && row->cvars==source->cvars &&
        descriptor(row)->storage==source->descriptor->storage) ||
        fail(error,QA_ERROR_FORMAT,"Restored CLIENT alias changed its canonical physical registry");
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    qa_movement_kind movement=QA_MOVEMENT_Q3;
    const qa_launch_binding *binding=qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=row->scope.seat},QA_ROLE_MOVEMENT,"");
    const qa_launch_instance *movement_source=binding?qa_launch_snapshot_find(candidate,binding->instance):NULL;
    if (!choices || row->physical_seat>=choices->seat_count || choices->seats[row->physical_seat].id!=row->scope.seat ||
        !movement_source || (movement=(qa_movement_kind)movement_source->selection.clock.kind)!=row->movement)
        return fail(error,QA_ERROR_FORMAT,"Decoded CLIENT changed its real physical seat or movement selection");
    const qa_product *actual=qa_catalog_product(qa_launch_instance_catalog(source->descriptor),source->descriptor->selection.product);
    const qa_product *saved=qa_catalog_product(frontend_config_files_catalog(row->files),frontend_config_files_product(row->files));
    if (!actual || !saved || (!row->hosted && strcmp(actual->key,saved->key)))
        return fail(error,QA_ERROR_FORMAT,"Decoded CLIENT ConfigStore differs from its actual product");
    if (row->metadata) {
        if (descriptor(row)->storage!=source->descriptor->storage)
            return fail(error,QA_ERROR_FORMAT,"CLIENT import retry changed its held receiver");
    } else if (!qa_launch_instance_retain_metadata(source->descriptor,&row->metadata,error)) return false;
    row->console=source->console; row->scope=source->scope; row->command=source->command;
    row->declaration_owner=source->declaration_owner;
    const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    frontend_config_source *backing=entities?frontend_config_store_named_source(owner->manager,entities->instance):NULL;
    bool hosted=!frontend_network_remote(owner->frontend) && backing && frontend_config_source_primary(backing) &&
        frontend_config_source_scope(backing).kind==QA_APPLICATION_CONSOLE_Q3_GAME;
    if (row->hosted!=hosted)
        return fail(error,QA_ERROR_FORMAT,"Decoded CLIENT changed its actual local GAME or remote backing");
    if (row->hosted) {
        frontend_config_source *game=frontend_config_store_named_source(owner->manager,row->saved_registry);
        if (game!=backing || (!row->registry && !frontend_config_source_acquire_seat_registry(game,row->scope.seat,&row->registry,error))) return false;
        row->hosted_console=frontend_config_source_console(game);
        row->q3_mouse=row->q3_view=row->movement_mouse=frontend_config_source_mouse_cvars(game,row->scope.seat);
        if (!row->q3_mouse) return fail(error,QA_ERROR_FORMAT,"Decoded hosted CLIENT lacks its actual GAME-seat input settings");
        row->cvars=frontend_client_registry_cvars(row->registry);
        if (frontend_config_source_keys(game)!=row->keys ||
            !qa_application_q3_client_configuration_bind_cvars(application,source,row->cvars,error)) return false;
    } else {
        row->cvars=source->cvars;
        if (!row->registry && !row->owned &&
            !qa_application_q3_client_configuration_take_cvars(application,source,&row->owned,error)) return false;
        frontend_client_registry_context callback={row,retain_context,release_context};
        if (!row->registry && !frontend_client_registry_create(owner->frontend,source->descriptor,row->scope.seat,&row->owned,&callback,&row->registry,error)) return false;
        row->cvars=frontend_client_registry_cvars(row->registry);
        qa_application_startup_source canonical=*source; canonical.cvars=row->cvars;
        restored_key_scope context={&canonical};
        frontend_keys_cvar_refs refs={.context=&context,.resolve=resolve_key,.qualify=qualify_key};
        if (!frontend_key_profile_bind(row->keys,row->cvars,&refs,error)) return false;
    }
    if (!install_commands(row,error)) return false;
    row->imported=false; return true;
}
bool frontend_remote_configs_finish_restore(frontend_remote_configs *owner,qa_error *error)
{
    if (!owner || !owner->restoring) return fail(error,QA_ERROR_ARGUMENT,"CLIENT roster has no pending restore admission");
    qa_frontend *f=owner->frontend;
    if (!frontend_global_settings_storage_idle(f->global_settings_storage))
        return fail(error,QA_ERROR_FORMAT,"Restored CLIENT lost its actual global directory owner");
    for (const frontend_remote_config *row=owner->rows;row;row=row->next) {
        frontend_remote_config_view view;
        if (!frontend_remote_config_read(row,&view) || !view.ready || !view.published)
            return fail(error,QA_ERROR_FORMAT,"Restored CLIENT lacks its canonical physical registry binding");
        if (!frontend_config_files_global_current(row->files,
            frontend_global_settings_storage_user_store(f->global_settings_storage),
            frontend_global_settings_storage_device_store(f->global_settings_storage)))
            return fail(error,QA_ERROR_FORMAT,"Restored CLIENT configuration differs from its retained global directories");
    }
    owner->restoring=false; return true;
}
